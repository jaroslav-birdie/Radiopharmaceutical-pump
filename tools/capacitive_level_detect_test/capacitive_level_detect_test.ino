// ============================================================
//  Test 2 - opakovana detekce KRITICKE HLADINY overenym
//  algoritmem. SAMOSTATNY diagnosticky sketch, nema nic
//  spolecneho s firmware cerpadla. Ovlada fyziologicky krokovy
//  motor (D6/D7, DRV8825 na sdilenem nENBL A3) - odsava z
//  lahvicky i davkuje zpet ze stejne strikacky.
//
//  Navazuje na tools/capacitive_response_test (test 1), ze
//  ktereho je algoritmus i jeho parametry odvozeny.
//
//  PRUBEH JEDNOHO POKUSU (opakuje se attemptCount x):
//    1) settle  - motor stoji, medianove okno se naplni
//    2) pull    - odsava se, dokud detektor nehlasi kritickou
//                 hladinu (nebo dokud se nevycerpa maxPullMl)
//    3) hold    - motor stoji, obsluha ocima zkontroluje hladinu
//                 a potvrdi '1' (spravne) / '0' (spatne)
//    4) refill  - doplni se NAHODNY objem z <refillMinMl,
//                 refillMaxMl>, pak zase od bodu 1
//  Vzorkuje a loguje se ve VSECH ctyrech fazich vcetne cekani
//  na obsluhu - klidova data mezi pohyby jsou soucast mereni.
//
//  DETEKCNI ALGORITMUS (overen na 20 cyklech testu 1, kde
//  sepnul 20/20 s rezervou 11.8x nad nejhorsim falesnym
//  poklesem):
//    - klouzavy MEDIAN 25 vzorku z C1 (ne prumer - median
//      zahodi kratke vykyvy, ktere by jinak nafoukly sledovane
//      maximum a posunuly detekci)
//    - sledovane MAXIMUM medianu, monotonne neklesajici,
//      pocita se od zacatku odsavani (fazi 'pull')
//    - detekce = median klesl o >= deltaCritical pod sledovane
//      maximum, potvrzeno confirmSamples vzorky po sobe
//  Medianove okno se plni uz behem 'settle', takze do faze
//  'pull' vstupuje uz naplnene - odpada rozbehovy problem.
//
//  RUSENI SE ZAMERNE NEDETEKUJE. Hlidka pres CIN2 tu neni a
//  nic odsavani nezastavi - to je zamer. C2 se jen loguje,
//  aby se rusive useky daly najit az pri offline analyze.
//
//  Sloupec 'slope10' (zmena medianu pres 10 vzorku) se take
//  jen LOGUJE, nepouziva se jako podminka. Slouzi k tomu, aby
//  se offline dalo vyhodnotit, jestli by AND-podminka na sklon
//  pomohla, nebo uskodila - bez rizika, ze ted potlaci
//  detekci a znehodnoti mereni.
//
//  POZOR NA LAHVICKU: obsah osciluje mezi kritickou hladinou a
//  kritickou hladinou + refillMaxMl. Pri vychozim refillMaxMl
//  = 15 ml je potreba 20ml varianta lahvicky, do 10ml by se to
//  neveslo.
//
//  Postup (viz README.md v tomto adresari):
//    1. Naplnit strikacku na ~30 ml (potrebuje prostor na obe
//       strany).
//    2. Lahvicku naplnit RUCNE vyrazne NAD kritickou hladinu,
//       vlozit do studny.
//    3. 'a' - overit CAPDAC, volitelne 'n' - staticky sum.
//    4. 'o' - driver ON, volitelne 'j'/'k' - odvzdusneni.
//    5. 't' - tare (aktualni poloha = 0.00 ml, referencni).
//    6. 'g' - spusti vsech attemptCount pokusu.
//    7. Po kazdem 'hold' zkontrolovat hladinu a poslat '1'/'0'.
//    8. 'x' kdykoliv za behu = okamzite zastaveni (nouzove).
// ============================================================
#include <Arduino.h>
#include <Wire.h>

// ---------- FDC1004 ----------
#define FDC_ADDR          0x50
#define REG_MEAS1_MSB     0x00
#define REG_CONF_MEAS1    0x08
#define REG_FDC_CONF      0x0C
#define REG_MANUF_ID      0xFE
#define REG_DEVICE_ID     0xFF
#define MANUF_ID_VAL      0x5449
#define DEVICE_ID_VAL     0x1004

#define CAPDAC_STEP_PF    3.125f
#define CAPDAC_MAX        31
#define RAW_PER_PF        524288.0f
#define RAW_NEAR_FULL     7000000L

#define N_CH              2   // CIN1 + CIN2

// ---------- fyziologicky krokovy motor (piny sdileny s hlavnim firmware) ----------
#define PIN_SAL_STEP      6
#define PIN_SAL_DIR       7
#define PIN_STEPPER_EN    A3
#define STEPPER_ENABLED_LEVEL   LOW
#define STEPPER_DISABLED_LEVEL  HIGH
#define SAL_DIR_PUSH_LEVEL      HIGH     // HIGH = davkovani do lahvicky

#define SCREW_PITCH_MM     8.0f
#define STEPS_PER_REV      200
#define MICROSTEP_DIV      16
#define STEPS_PER_MM       ((STEPS_PER_REV * MICROSTEP_DIV) / SCREW_PITCH_MM)   // 400
#define SAL_SYR_ML_PER_MM  0.625f
#define SAL_STEPS_PER_ML   (STEPS_PER_MM / SAL_SYR_ML_PER_MM)                   // 640

#define JOG_ML             0.5f   // j/k - rucni odvzdusneni pred startem
#define SETTLE_MS          5000UL // klid pred kazdym odsavanim (naplni medianove okno)

// Mechanicka ochrana zdvihu strikacky (60 ml), dvoustranna.
// Poloha se pocita od tare: zaporne = odsato do strikacky.
// Nejhlubsi ocekavany bod je bod detekce prvniho pokusu minus
// pripadny nedetekujici zdvih (maxPullMl), nejvyssi je bod
// detekce plus refillMaxMl.
#define MECH_LIMIT_DOWN_ML  30.0f
#define MECH_LIMIT_UP_ML    12.0f

// ---------- detektor ----------
#define MED_N              25     // delka medianoveho okna (pevna - velikost bufferu)
#define SLOPE_K            10     // slope10 = median[i] - median[i-10], jen se loguje

// ---------- parametry (laditelne pred 'g') ----------
static float    deltaCriticalPf = 0.22f;  // pokles medianu pod sledovane maximum
static uint8_t  confirmSamples  = 5;      // potvrzeni N vzorky po sobe
static uint8_t  attemptCount    = 20;     // pocet pokusu
static uint16_t samplePeriodMs  = 200;    // perioda vzorku (shodna s testem 1)
static float    refillMinMl     = 5.0f;   // dolni mez nahodneho doplneni
static float    refillMaxMl     = 15.0f;  // horni mez nahodneho doplneni
static float    maxPullMl       = 25.0f;  // pojistka: max. zdvih odsavani bez detekce
static uint8_t  flowSecPerMl    = 5;      // tempo pohybu (5 s/ml = tempo provozu i testu 1)
static uint32_t rngSeed         = 0;      // 0 = nasadit z micros() pri 'g'

static uint32_t stepIntervalUs  = (uint32_t)(1000000.0f * 5 / SAL_STEPS_PER_ML);

// ---------- stav ----------
static uint8_t  capdac[N_CH]   = { 0, 0 };
static bool     tared          = false;
static bool     driverEnabled  = false;
static int32_t  posSteps       = 0;      // odchylka od tare v krocich (zaporne = odsato)
static int32_t  pullStartSteps = 0;      // poloha na zacatku aktualniho odsavani
static char     line[24];
static uint8_t  lineLen        = 0;
static uint32_t runStartMs     = 0;
static uint8_t  attempt        = 0;
static bool     testStarted    = false;

// medianovy filtr - kruhovy buffer + pracovni kopie pro razeni
static float    medBuf[MED_N];
static uint8_t  medIdx         = 0;
static uint8_t  medFill        = 0;
static float    medSort[MED_N];

// historie medianu pro slope10
static float    slopeBuf[SLOPE_K + 1];
static uint8_t  slopeIdx       = 0;
static uint8_t  slopeFill      = 0;

// sledovane maximum + potvrzovaci citac
static float    trackedMax     = 0.0f;
static bool     trackingMax    = false;
static uint8_t  confirmRun     = 0;

// posledni spocitane hodnoty (pro log)
static float    lastMed        = 0.0f;
static float    lastSlope      = 0.0f;
static bool     medValid       = false;

// ---------- nizka uroven I2C ----------
static void writeReg(uint8_t reg, uint16_t value) {
    Wire.beginTransmission(FDC_ADDR);
    Wire.write(reg);
    Wire.write((uint8_t)(value >> 8));
    Wire.write((uint8_t)(value & 0xFF));
    Wire.endTransmission();
}

static uint16_t readReg(uint8_t reg) {
    Wire.beginTransmission(FDC_ADDR);
    Wire.write(reg);
    Wire.endTransmission(false);
    if (Wire.requestFrom((uint8_t)FDC_ADDR, (uint8_t)2) != 2) {
        return 0;
    }
    uint16_t hi = Wire.read();
    return (hi << 8) | Wire.read();
}

// CONF_MEASx: CHA[15:13]=kanal, CHB[12:10]=100 (CAPDAC), CAPDAC[9:5]
static void applyConfig() {
    uint16_t measMask = 0;
    for (uint8_t i = 0; i < N_CH; i++) {
        writeReg(REG_CONF_MEAS1 + i,
                 ((uint16_t)i << 13) | (0x4 << 10) | ((uint16_t)capdac[i] << 5));
        measMask |= (uint16_t)1 << (7 - i);          // MEAS1->b7, MEAS2->b6
    }
    writeReg(REG_FDC_CONF, (1 << 10) | (1 << 8) | measMask);   // 100 S/s, REPEAT
    delay(30);
}

static int32_t readRaw(uint8_t ch) {
    int16_t msb = (int16_t)readReg(REG_MEAS1_MSB + ch * 2);
    uint16_t lsb = readReg(REG_MEAS1_MSB + ch * 2 + 1);
    return ((int32_t)msb << 8) | (lsb >> 8);
}

static float readPf(uint8_t ch) {
    return readRaw(ch) / RAW_PER_PF + capdac[ch] * CAPDAC_STEP_PF;
}

static void autoCapdac(uint8_t ch) {
    capdac[ch] = 0;
    applyConfig();
    for (uint8_t i = 0; i < CAPDAC_MAX; i++) {
        if (readRaw(ch) < RAW_NEAR_FULL) {
            break;
        }
        capdac[ch]++;
        applyConfig();
    }
}

// ---------- detektor ----------
static void detectorReset() {
    medIdx = 0;
    medFill = 0;
    slopeIdx = 0;
    slopeFill = 0;
    trackedMax = 0.0f;
    trackingMax = false;
    confirmRun = 0;
    medValid = false;
    lastMed = 0.0f;
    lastSlope = 0.0f;
}

// Vlozi vzorek do medianoveho okna a prepocita median + slope10.
// Sledovane maximum se aktualizuje jen kdyz trackingMax (tj. od
// zacatku odsavani) - behem 'settle' se okno jen plni.
static void detectorPush(float c1) {
    medBuf[medIdx] = c1;
    medIdx = (uint8_t)((medIdx + 1) % MED_N);
    if (medFill < MED_N) {
        medFill++;
    }
    if (medFill < MED_N) {
        medValid = false;
        return;
    }

    // vkladaci razeni pracovni kopie (25 prvku, 5 Hz - zanedbatelne)
    for (uint8_t i = 0; i < MED_N; i++) {
        float v = medBuf[i];
        uint8_t j = i;
        while (j > 0 && medSort[j - 1] > v) {
            medSort[j] = medSort[j - 1];
            j--;
        }
        medSort[j] = v;
    }
    lastMed = medSort[MED_N / 2];
    medValid = true;

    slopeBuf[slopeIdx] = lastMed;
    slopeIdx = (uint8_t)((slopeIdx + 1) % (SLOPE_K + 1));
    if (slopeFill <= SLOPE_K) {
        slopeFill++;
    }
    lastSlope = (slopeFill > SLOPE_K) ? (lastMed - slopeBuf[slopeIdx]) : 0.0f;

    if (trackingMax && lastMed > trackedMax) {
        trackedMax = lastMed;
    }
}

static float detectorDrop() {
    if (!trackingMax || !medValid) {
        return 0.0f;
    }
    return trackedMax - lastMed;
}

// Zacatek odsavani - odtud se sleduje maximum. Okno uz je plne
// ze 'settle', takze maximum startuje na skutecne plosine.
static void detectorStartTracking() {
    trackedMax = medValid ? lastMed : 0.0f;
    trackingMax = true;
    confirmRun = 0;
}

// true = kriticka hladina potvrzena confirmSamples vzorky po sobe
static bool detectorCritical() {
    if (detectorDrop() >= deltaCriticalPf) {
        if (confirmRun < 255) {
            confirmRun++;
        }
        return confirmRun >= confirmSamples;
    }
    confirmRun = 0;
    return false;
}

// ---------- stepper ----------
static void stepperInit() {
    pinMode(PIN_SAL_STEP, OUTPUT);
    pinMode(PIN_SAL_DIR, OUTPUT);
    pinMode(PIN_STEPPER_EN, OUTPUT);
    digitalWrite(PIN_SAL_STEP, LOW);
    digitalWrite(PIN_STEPPER_EN, STEPPER_DISABLED_LEVEL);
}

// Neblokujici kontrola nouzoveho zastaveni - 'x' kdykoliv za behu.
static bool abortRequested() {
    if (Serial.available() > 0 && Serial.peek() == 'x') {
        Serial.read();
        return true;
    }
    return false;
}

static int32_t stepsFor(float ml) {
    return (int32_t)(ml * SAL_STEPS_PER_ML + 0.5f);
}

static float posMl() {
    return (float)posSteps / SAL_STEPS_PER_ML;
}

// Kolik ml uz se odsalo v ramci aktualniho zdvihu (kladne).
static float pullMl() {
    return (float)(pullStartSteps - posSteps) / SAL_STEPS_PER_ML;
}

static bool withinMechLimit() {
    float ml = posMl();
    return (ml >= -MECH_LIMIT_DOWN_ML) && (ml <= MECH_LIMIT_UP_ML);
}

static void stopMotorDisable() {
    digitalWrite(PIN_STEPPER_EN, STEPPER_DISABLED_LEVEL);
    driverEnabled = false;
}

static void jogOnce(bool push) {
    int32_t steps = stepsFor(JOG_ML);
    digitalWrite(PIN_SAL_DIR, push ? SAL_DIR_PUSH_LEVEL
                                   : (SAL_DIR_PUSH_LEVEL == HIGH ? LOW : HIGH));
    delayMicroseconds(10);
    for (int32_t i = 0; i < steps; i++) {
        digitalWrite(PIN_SAL_STEP, HIGH);
        delayMicroseconds(3);
        digitalWrite(PIN_SAL_STEP, LOW);
        delayMicroseconds(stepIntervalUs - 3);
        posSteps += push ? 1 : -1;
    }
}

// ---------- log jednoho vzorku ----------
static void logSample(const char *phase) {
    float c1 = readPf(0);
    float c2 = readPf(1);
    detectorPush(c1);

    Serial.print(attempt);
    Serial.print(';'); Serial.print(millis() - runStartMs);
    Serial.print(';'); Serial.print(posMl(), 2);
    Serial.print(';'); Serial.print(pullMl(), 2);
    Serial.print(';'); Serial.print(phase);
    Serial.print(';'); Serial.print(c1, 4);
    Serial.print(';'); Serial.print(c2, 4);
    if (medValid) {
        Serial.print(';'); Serial.print(lastMed, 4);
        Serial.print(';'); Serial.print(trackingMax ? trackedMax : lastMed, 4);
        Serial.print(';'); Serial.print(detectorDrop(), 4);
        Serial.print(';'); Serial.println(lastSlope, 4);
    } else {
        Serial.println(F(";;;;"));
    }
}

// ---------- diagnostika ----------
static void printInfo() {
    Serial.print(F("# CAPDAC1=")); Serial.print(capdac[0]);
    Serial.print(F(" CAPDAC2=")); Serial.println(capdac[1]);
    Serial.print(F("# delta=")); Serial.print(deltaCriticalPf, 3);
    Serial.print(F(" pF  confirm=")); Serial.print(confirmSamples);
    Serial.print(F("  median=")); Serial.println(MED_N);
    Serial.print(F("# pokusu=")); Serial.print(attemptCount);
    Serial.print(F("  doplneni=")); Serial.print(refillMinMl, 1);
    Serial.print('-'); Serial.print(refillMaxMl, 1);
    Serial.print(F(" ml  maxPull=")); Serial.print(maxPullMl, 1);
    Serial.println(F(" ml"));
    Serial.print(F("# samplePeriod=")); Serial.print(samplePeriodMs);
    Serial.print(F(" ms  tempo=")); Serial.print(flowSecPerMl);
    Serial.println(F(" s/ml"));
    Serial.print(F("# testStarted=")); Serial.println(testStarted ? F("ano (pro novy beh 't')") : F("ne"));
    Serial.print(F("# driver=")); Serial.print(driverEnabled ? F("ON") : F("OFF"));
    Serial.print(F(" tared=")); Serial.print(tared ? F("ano") : F("ne"));
    Serial.print(F(" pos=")); Serial.print(posMl(), 3);
    Serial.println(F(" ml (odchylka od tare)"));
}

static void printHelp() {
    Serial.println(F("# h=napoveda i=info a=autoCAPDAC n=sum"));
    Serial.println(F("# o=driver ON  x=driver OFF / STOP behem behu"));
    Serial.println(F("# t=tare (poloha=0.00 ml)  j/k=jog +-0.5ml"));
    Serial.println(F("# g=spustit vsechny pokusy"));
    Serial.println(F("# behem 'hold': 1=hladina spravne  0=spatne"));
    Serial.println(F("# d<pF>=delta  c<n>=potvrzeni  r<n>=pocet pokusu"));
    Serial.println(F("# wl<ml>/wh<ml>=mez doplneni  m<ml>=max zdvih bez detekce"));
    Serial.println(F("# p<ms>=perioda vzorku  f<s>=tempo s/ml  s<n>=seed generatoru"));
}

static void noiseTest() {
    Serial.println(F("# test sumu, nehybat sestavou..."));
    for (uint8_t ch = 0; ch < N_CH; ch++) {
        float mn = 1e9f, mx = -1e9f, sum = 0.0f, sumSq = 0.0f;
        const uint16_t N = 256;
        for (uint16_t i = 0; i < N; i++) {
            float v = readPf(ch);
            if (v < mn) mn = v;
            if (v > mx) mx = v;
            sum += v; sumSq += v * v;
            delay(5);
        }
        float mean = sum / N;
        float var = sumSq / N - mean * mean;
        if (var < 0.0f) var = 0.0f;
        Serial.print(F("# CIN")); Serial.print(ch + 1);
        Serial.print(F(": prum=")); Serial.print(mean, 4);
        Serial.print(F(" min=")); Serial.print(mn, 4);
        Serial.print(F(" max=")); Serial.print(mx, 4);
        Serial.print(F(" p-p=")); Serial.print(mx - mn, 4);
        Serial.print(F(" sigma=")); Serial.print(sqrt(var), 5);
        Serial.println(F(" pF"));
    }
}

// ---------- faze jednoho pokusu ----------
// Vsechny vraci false pri ABORTu nebo mechanickem dorazu.

// Klid pred odsavanim - naplni medianove okno, maximum se jeste nesleduje.
static bool phaseSettle() {
    pullStartSteps = posSteps;      // aby pull_ml behem 'settle' ukazovalo 0.00
    uint32_t startMs = millis();
    uint32_t lastSampleMs = millis() - samplePeriodMs;
    while (millis() - startMs < SETTLE_MS) {
        if (abortRequested()) {
            Serial.println(F("# ABORT behem ustaleni"));
            return false;
        }
        uint32_t now = millis();
        if (now - lastSampleMs >= samplePeriodMs) {
            lastSampleMs = now;
            logSample("settle");
        }
    }
    return true;
}

// Odsavani do detekce kriticke hladiny. detected=true pri detekci,
// false pri vycerpani maxPullMl (to je taky platny vysledek pokusu).
static bool phasePull(bool *detected) {
    *detected = false;
    pullStartSteps = posSteps;
    detectorStartTracking();

    int32_t limitSteps = stepsFor(maxPullMl);
    digitalWrite(PIN_SAL_DIR, SAL_DIR_PUSH_LEVEL == HIGH ? LOW : HIGH);
    delayMicroseconds(10);

    uint32_t lastStepUs = micros();
    uint32_t lastSampleMs = millis() - samplePeriodMs;

    while ((pullStartSteps - posSteps) < limitSteps) {
        if (abortRequested()) {
            Serial.println(F("# ABORT behem odsavani"));
            return false;
        }
        uint32_t nowUs = micros();
        if (nowUs - lastStepUs >= stepIntervalUs) {
            lastStepUs = nowUs;
            digitalWrite(PIN_SAL_STEP, HIGH);
            delayMicroseconds(3);
            digitalWrite(PIN_SAL_STEP, LOW);
            posSteps--;
            if (!withinMechLimit()) {
                Serial.println(F("# *** MECHANICKY DORAZ STRIKACKY *** beh zastaven ***"));
                return false;
            }
        }
        uint32_t nowMs = millis();
        if (nowMs - lastSampleMs >= samplePeriodMs) {
            lastSampleMs = nowMs;
            logSample("pull");
            if (detectorCritical()) {
                *detected = true;
                return true;
            }
        }
    }
    Serial.print(F("# att=")); Serial.print(attempt);
    Serial.print(F(" BEZ DETEKCE - vycerpano maxPull ")); Serial.print(maxPullMl, 1);
    Serial.println(F(" ml"));
    return true;
}

// Cekani na obsluhu. Motor stoji, driver zustava ENABLED (drzi
// pistu proti zpetnemu tlaku). Vzorkuje se dal.
// confirm: 1 = hladina spravne, 0 = spatne.
static bool phaseHold(uint8_t *confirm) {
    Serial.print(F("# att=")); Serial.print(attempt);
    Serial.println(F(" HOLD - zkontroluj hladinu, posli 1 (spravne) / 0 (spatne)"));

    uint32_t lastSampleMs = millis() - samplePeriodMs;
    for (;;) {
        while (Serial.available() > 0) {
            char c = (char)Serial.read();
            if (c == '1' || c == '0') {
                *confirm = (uint8_t)(c - '0');
                return true;
            }
            if (c == 'x') {
                Serial.println(F("# ABORT behem cekani na potvrzeni"));
                return false;
            }
        }
        uint32_t now = millis();
        if (now - lastSampleMs >= samplePeriodMs) {
            lastSampleMs = now;
            logSample("hold");
        }
    }
}

// Doplneni nahodneho objemu zpet do lahvicky.
static bool phaseRefill(float ml) {
    int32_t totalSteps = stepsFor(ml);
    int32_t done = 0;
    digitalWrite(PIN_SAL_DIR, SAL_DIR_PUSH_LEVEL);
    delayMicroseconds(10);

    uint32_t lastStepUs = micros();
    uint32_t lastSampleMs = millis() - samplePeriodMs;

    while (done < totalSteps) {
        if (abortRequested()) {
            Serial.println(F("# ABORT behem doplnovani"));
            return false;
        }
        uint32_t nowUs = micros();
        if (nowUs - lastStepUs >= stepIntervalUs) {
            lastStepUs = nowUs;
            digitalWrite(PIN_SAL_STEP, HIGH);
            delayMicroseconds(3);
            digitalWrite(PIN_SAL_STEP, LOW);
            done++;
            posSteps++;
            if (!withinMechLimit()) {
                Serial.println(F("# *** MECHANICKY DORAZ STRIKACKY *** beh zastaven ***"));
                return false;
            }
        }
        uint32_t nowMs = millis();
        if (nowMs - lastSampleMs >= samplePeriodMs) {
            lastSampleMs = nowMs;
            logSample("refill");
        }
    }
    return true;
}

// ---------- cely test ----------
static void runTest() {
    if (!driverEnabled) { Serial.println(F("# CHYBA: driver je vypnuty, nejdriv 'o'")); return; }
    if (!tared) { Serial.println(F("# CHYBA: neprovedeno tare, nejdriv 't'")); return; }
    if (testStarted) { Serial.println(F("# CHYBA: test uz probehl (nebo byl prerusen) - pro novy beh znovu 't'")); return; }
    testStarted = true;

    uint32_t seed = (rngSeed != 0) ? rngSeed : micros();
    randomSeed(seed);
    runStartMs = millis();

    Serial.print(F("# === TEST 2: ")); Serial.print(attemptCount);
    Serial.println(F(" pokusu o detekci kriticke hladiny ==="));
    Serial.print(F("# seed=")); Serial.println(seed);
    Serial.println(F("# att;t_ms;pos_ml;pull_ml;phase;C1_pF;C2_pF;med_pF;max_pF;drop_pF;slope10"));

    for (attempt = 1; attempt <= attemptCount; attempt++) {
        Serial.print(F("# --- pokus ")); Serial.print(attempt);
        Serial.print('/'); Serial.println(attemptCount);

        detectorReset();
        if (!phaseSettle()) { stopMotorDisable(); break; }

        bool detected = false;
        if (!phasePull(&detected)) { stopMotorDisable(); break; }

        float pulled = pullMl();
        float c1AtDetect = lastMed;

        uint8_t confirm = 0;
        if (!phaseHold(&confirm)) { stopMotorDisable(); break; }

        // nahodne doplneni v rozsahu <refillMinMl, refillMaxMl> po 0.1 ml
        int32_t lo = (int32_t)(refillMinMl * 10.0f + 0.5f);
        int32_t hi = (int32_t)(refillMaxMl * 10.0f + 0.5f);
        float refill = random(lo, hi + 1) / 10.0f;

        Serial.print(F("# VYSLEDEK att=")); Serial.print(attempt);
        Serial.print(F(" detekce=")); Serial.print(detected ? F("ano") : F("ne"));
        Serial.print(F(" pull=")); Serial.print(pulled, 2);
        Serial.print(F(" pos=")); Serial.print(posMl(), 2);
        Serial.print(F(" potvrzeno=")); Serial.print(confirm);
        Serial.print(F(" refill=")); Serial.println(refill, 1);

        if (attempt == attemptCount) {
            break;   // po poslednim pokusu uz se nedoplnuje
        }
        if (!phaseRefill(refill)) { stopMotorDisable(); break; }

        // Pasivni kontrola: pokud C1 po doplneni nestoupla, dalsi pokus
        // nema na cem detekovat (hladina se nevratila nad prstenec).
        // Nic to nezastavuje, jen se to oznaci v datech.
        if (medValid && (lastMed - c1AtDetect) < 0.30f) {
            Serial.print(F("# VAROVANI att=")); Serial.print(attempt);
            Serial.print(F(": C1 po doplneni stoupla jen o "));
            Serial.print(lastMed - c1AtDetect, 4);
            Serial.println(F(" pF"));
        }
    }

    stopMotorDisable();
    Serial.println(F("# === TEST 2 KONEC ==="));
}

// ---------- prikazy ----------
static void handleLine() {
    if (lineLen == 0) return;

    if (lineLen >= 3 && line[0] == 'w' && (line[1] == 'l' || line[1] == 'h')) {
        float v = atof(&line[2]);
        if (v > 0.0f && v <= 30.0f) {
            if (line[1] == 'l') { refillMinMl = v; } else { refillMaxMl = v; }
            Serial.print(F("# doplneni=")); Serial.print(refillMinMl, 1);
            Serial.print('-'); Serial.print(refillMaxMl, 1); Serial.println(F(" ml"));
        }
        return;
    }

    switch (line[0]) {
        case 'h': printHelp(); break;
        case 'i': printInfo(); break;
        case 'a':
            for (uint8_t i = 0; i < N_CH; i++) autoCapdac(i);
            printInfo();
            break;
        case 'n': noiseTest(); break;
        case 'o':
            digitalWrite(PIN_STEPPER_EN, STEPPER_ENABLED_LEVEL);
            driverEnabled = true;
            Serial.println(F("# driver ON"));
            break;
        case 'x':
            digitalWrite(PIN_STEPPER_EN, STEPPER_DISABLED_LEVEL);
            driverEnabled = false;
            Serial.println(F("# driver OFF"));
            break;
        case 't':
            posSteps = 0;
            pullStartSteps = 0;
            tared = true;
            testStarted = false;
            detectorReset();
            Serial.println(F("# tare - aktualni poloha = 0.00 ml (referencni)"));
            break;
        case 'j':
            if (!driverEnabled) { Serial.println(F("# driver je OFF, napred 'o'")); break; }
            jogOnce(true);
            Serial.print(F("# jog +0.5ml, poloha=")); Serial.println(posMl(), 2);
            break;
        case 'k':
            if (!driverEnabled) { Serial.println(F("# driver je OFF, napred 'o'")); break; }
            jogOnce(false);
            Serial.print(F("# jog -0.5ml, poloha=")); Serial.println(posMl(), 2);
            break;
        case 'g': runTest(); break;
        case 'd': {
            float v = atof(&line[1]);
            if (v > 0.0f && v <= 2.0f) { deltaCriticalPf = v; Serial.print(F("# delta=")); Serial.println(deltaCriticalPf, 3); }
            break;
        }
        case 'c': {
            int v = atoi(&line[1]);
            if (v >= 1 && v <= 50) { confirmSamples = (uint8_t)v; Serial.print(F("# confirm=")); Serial.println(confirmSamples); }
            break;
        }
        case 'r': {
            int v = atoi(&line[1]);
            if (v >= 1 && v <= 50) { attemptCount = (uint8_t)v; Serial.print(F("# pokusu=")); Serial.println(attemptCount); }
            break;
        }
        case 'm': {
            float v = atof(&line[1]);
            if (v > 0.0f && v <= MECH_LIMIT_DOWN_ML) { maxPullMl = v; Serial.print(F("# maxPull=")); Serial.println(maxPullMl, 1); }
            break;
        }
        case 'p': {
            int v = atoi(&line[1]);
            if (v >= 20 && v <= 2000) { samplePeriodMs = (uint16_t)v; Serial.print(F("# samplePeriod=")); Serial.println(samplePeriodMs); }
            break;
        }
        case 'f': {
            int v = atoi(&line[1]);
            if (v >= 1 && v <= 30) {
                flowSecPerMl = (uint8_t)v;
                stepIntervalUs = (uint32_t)(1000000.0f * flowSecPerMl / SAL_STEPS_PER_ML);
                Serial.print(F("# tempo=")); Serial.print(flowSecPerMl); Serial.println(F(" s/ml"));
            }
            break;
        }
        case 's': {
            rngSeed = (uint32_t)atol(&line[1]);
            Serial.print(F("# seed=")); Serial.println(rngSeed);
            break;
        }
        case '#':
            Serial.println(line);
            break;
        default:
            Serial.println(F("# neznamy prikaz, h=napoveda"));
            break;
    }
}

static void pollSerial() {
    while (Serial.available() > 0) {
        char c = (char)Serial.read();
        if (c == '\n' || c == '\r') {
            if (lineLen > 0) {
                line[lineLen] = '\0';
                handleLine();
                lineLen = 0;
            }
        } else if (lineLen < sizeof(line) - 1) {
            line[lineLen++] = c;
        }
    }
}

void setup() {
    Serial.begin(9600);
    Wire.begin();
    Wire.setClock(100000UL);
    delay(100);

    Serial.println(F("# Test 2 - opakovana detekce kriticke hladiny"));
    Serial.println(F("# pouziva fyziologicky stepper D6/D7, EN=A3 (odsavani i davkovani)"));
    Serial.println(F("# RUSENI SE NEDETEKUJE a nic nezastavuje - C2 se jen loguje"));
    uint16_t manuf = readReg(REG_MANUF_ID);
    uint16_t dev = readReg(REG_DEVICE_ID);
    Serial.print(F("# MANUFACTURER_ID=0x")); Serial.print(manuf, HEX);
    Serial.print(F(" DEVICE_ID=0x")); Serial.println(dev, HEX);
    if (manuf != MANUF_ID_VAL || dev != DEVICE_ID_VAL) {
        Serial.println(F("# CHYBA: cip neodpovida (ocekavano 0x5449 / 0x1004)"));
    }

    applyConfig();
    for (uint8_t i = 0; i < N_CH; i++) autoCapdac(i);
    stepperInit();
    detectorReset();

    printInfo();
    printHelp();
    Serial.println(F("# POSTUP: 1) strikacka na strednich ~30 ml"));
    Serial.println(F("#         2) lahvicka (20ml varianta) RUCNE nad kritickou hladinu"));
    Serial.println(F("#         3) 'a' overit CAPDAC, volitelne 'n' sum"));
    Serial.println(F("#         4) 'o' driver ON, 't' tare"));
    Serial.println(F("#         5) 'g' - spusti vsechny pokusy"));
    Serial.println(F("#         6) po kazdem HOLD zkontroluj hladinu a posli 1 nebo 0"));
}

void loop() {
    pollSerial();
}
