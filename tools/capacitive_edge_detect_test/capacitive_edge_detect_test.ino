// ============================================================
//  Ziva detekce DOLNI (KRITICKE) hrany kruhove elektrody (CIN1)
//  - automatizovana davkova serie (vychozi 10x) pro rychle nasbirani
//  dat z vetsiho poctu opakovani.
//  SAMOSTATNY diagnosticky sketch - nema nic spolecneho s firmware
//  cerpadla. Ovlada fyziologicky krokovy motor (D6/D7, DRV8825 na
//  sdilenem nENBL A3) - odsava z lahvicky I davkuje zpet, ze stejne
//  strikacky (zpetna klapka byla pro tenhle test odstranena).
//
//  v5 - REDESIGN pro davkove testovani (viz CLAUDE.md/README diskuze):
//   1) Odstranen koncept "maximalniho vytlaceneho objemu, po kterem
//      se vyhlasi alarm ze hrana nebyla nalezena" (byval `searchSafetyMl`
//      pouzity jako detekcni-selhani alarm). Skutecna aplikace tohle
//      nema - objem vytlaceny vzduchem proti neznamemu odporu je
//      NEZNAMA velicina (proto se vubec pouziva kapacitni snimani),
//      takze zadny volumovy "failsafe" v produkci neexistuje a bench
//      nastroj by ho nemel predstirat jako smysluplny koncept.
//      MECH_LIMIT_ML nize je vyhradne HARDWAROVA ochrana zdvihu
//      strikacky (aby motor nemlel proti mechanickemu dorazu pri
//      neprerusenem davkovem behu), NE napodobenina produkcni logiky.
//   2) Prestalo se sledovat "celkem odebrano ze strikacky od tare"
//      (`maxSyringeMl`/`withdrawnMl()` s vyzvou k rucnimu doplneni
//      strikacky) - odsavani i davkovani zpet uz jde ze stejne
//      strikacky v uzavrenem cyklu (viz bod 4), takze se pozice
//      strikacky v prumeru sama vyrovnava (odebrany objem kazdeho
//      cyklu ~ odpovida nahodnemu doplneni z konce predchoziho cyklu).
//   3) Rucni doplnovani lahvicky (puvodni 'y' + fyzicke doliti mimo
//      system) nahrazeno AUTOMATICKYM davkovanim nahodneho objemu
//      6-17 ml zpet do lahvicky tou stejnou strikackou/motorem - viz
//      dispenseRandomRefill(). Obsluha uz nedolieva rucne.
//   4) Vizualni kontrola po detekci kriticke hrany potvrzuje binarne:
//      '1' = detekce vypadala spravne, '0' = detekce vypadala chybne.
//      Sketch pak AUTOMATICKY pokracuje (doplneni + dalsi cyklus) bez
//      dalsiho rucniho zasahu - cely davkovy beh (vychozi 10 cyklu) tak
//      vyzaduje jen jedno stisknuti klavesy na cyklus.
//
//  Postup jednoho cyklu:
//    ODSAVANI: odsava spojite od aktualni pozice, dokud nenajde DOLNI
//              (kritickou) hranu (pokles od prubezneho maxima C1 -
//              stejny overeny algoritmus jako v4, viz README).
//            -> zastavi motor, obsluha vytahne lahvicku, zkontroluje
//               hladinu, vrati zpet do studny, potvrdi '1' (OK) nebo
//               '0' (chyba detekce).
//    AUTO   : sketch sam dF1004vkuje nahodny objem 6-17 ml zpet do
//             lahvicky (ta stejna strikacka/motor, opacny smer), pak
//             rovnou spusti dalsi cyklus odsavani. Celkem
//             `cycleCountTarget`x (vychozi 10).
//
//  DULEZITE - pocatecni hladina v lahvicce pro CYKLUS 1 se plni rucne
//  (obsluha, libovolne v rozmezi ~6-17 ml, stejny rozsah jako pozdejsi
//  automaticke doplnovani) - to jeste nejde automatizovat, protoze pred
//  prvnim cyklem nemame zadnou znamou referenci. Od cyklu 2 uz plni
//  system sam.
//
//  Zpetna klapka na fyziologicke vetvi byla pro tento test
//  odstranena (viz CLAUDE.md) - motor tedy zvlada odsavat i davkovat.
//
//  Postup (viz README.md v tomto adresari):
//    1. Naplnit strikacku na rozumnou stredni hodnotu (doporuceno
//       ~30 ml) - viz MECH_LIMIT_ML nize, proc.
//    2. Lahvicku naplnit na libovolnou hladinu ~6-17 ml, vlozit do studny.
//    3. 'o' - driver ON, volitelne 'j'/'k' - odvzdusneni.
//    4. 't' - tare (referencni bod pro krokove pocitadlo).
//    5. 'g' - spustit celou automatickou sekvenci (cycleCountTarget cyklu).
//    6. Po kazde detekci kriticke hrany: zkontrolovat, vratit lahvicku
//       do studny, potvrdit '1' nebo '0' - sketch pokracuje sam.
//    7. 'x' kdykoliv za behu = okamzite zastaveni (nouzove).
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

#define FLOW_S_PER_ML      5UL
#define SAL_STEP_INTERVAL_US  ((uint32_t)(1000000.0f * FLOW_S_PER_ML / SAL_STEPS_PER_ML))  // ~7812 us

#define JOG_ML             0.5f

// ---------- vyhlazeni signalu (overeno na datech z capacitive_cycle_test) ----------
#define SMOOTH_WINDOW      25    // ~5 s pri 200 ms/vzorek - overeny kompromis sum/zpozdeni
#define SETTLE_MS          5000UL  // klidova doba pred kazdym cyklem (motor stoji), po fyzickem zasahu

#define MAX_CYCLES         10

// Vyhradne HARDWAROVA ochrana zdvihu strikacky (60 ml, cca 96 mm zdvihu) -
// NENI to detekcni ani "alarm" koncept (viz v5 poznamka vyse). Symetricky
// strop kolem pozice pri 't' - pri ocekavanem vyvazenem odber/doplneni
// cyklu (viz bod 2 vyse) by se nikdy nemel priblizit, pokud se detekce
// nezacne chovat vyrazne mimo ocekavani.
#define MECH_LIMIT_ML      25.0f

// ---------- parametry (nastavit pred 'g'; 'dc'/'cf' jsou 2-znakove prikazy) ----------
static float    deltaCritical   = 0.22f;   // pF - pokles od (neomezeneho) maxima = "kriticka (dolni) hladina"
static uint8_t  confirmSamples  = 5;       // kolik po sobe jdoucich vzorku musi prah drzet
static uint16_t samplePeriodMs  = 200;
static uint8_t  cycleCountTarget = 10;

static uint8_t  capdac[N_CH]   = { 0, 0 };
static float    baseline[N_CH] = { 0.0f, 0.0f };
static bool     tared          = false;
static bool     driverEnabled  = false;
static int32_t  posSteps       = 0;      // 0 = pri tare; zaporne = odsato, kladne = davkovano nad tare
static char     line[24];
static uint8_t  lineLen        = 0;
static uint32_t runStartMs     = 0;

enum RunState { RS_IDLE, RS_AWAIT_CONFIRM };
static RunState runState = RS_IDLE;
static uint8_t  cycleIndex = 0;   // 1-based

static float   resultPeakLevel[MAX_CYCLES], resultPeakVal[MAX_CYCLES];      // informativni, NENI stop bod
static float   resultLowerLevel[MAX_CYCLES], resultLowerVal[MAX_CYCLES];    // skutecny bezpecnostni bod
static uint8_t resultConfirmOk[MAX_CYCLES];                                 // 1='1' (OK), 0='0' (chyba)
static float   resultRefillMl[MAX_CYCLES];                                  // nahodny objem doplneny PO tomhle cyklu (-1 = zadny, posledni cyklus)

// ---------- klouzavy prumer C1 (kruhovy buffer, O(1) update) ----------
static float   smoothBuf[SMOOTH_WINDOW];
static uint8_t smoothIdx = 0;
static uint8_t smoothCount = 0;
static float   smoothSum = 0.0f;

static void resetSmooth() {
    smoothIdx = 0; smoothCount = 0; smoothSum = 0.0f;
}

static float pushSmooth(float v) {
    if (smoothCount == SMOOTH_WINDOW) {
        smoothSum -= smoothBuf[smoothIdx];
    } else {
        smoothCount++;
    }
    smoothBuf[smoothIdx] = v;
    smoothSum += v;
    smoothIdx = (smoothIdx + 1) % SMOOTH_WINDOW;
    return smoothSum / smoothCount;
}

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

static void applyConfig() {
    uint16_t measMask = 0;
    for (uint8_t i = 0; i < N_CH; i++) {
        writeReg(REG_CONF_MEAS1 + i,
                 ((uint16_t)i << 13) | (0x4 << 10) | ((uint16_t)capdac[i] << 5));
        measMask |= (uint16_t)1 << (7 - i);
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

// ---------- stepper ----------
static void stepperInit() {
    pinMode(PIN_SAL_STEP, OUTPUT);
    pinMode(PIN_SAL_DIR, OUTPUT);
    pinMode(PIN_STEPPER_EN, OUTPUT);
    digitalWrite(PIN_SAL_STEP, LOW);
    digitalWrite(PIN_STEPPER_EN, STEPPER_DISABLED_LEVEL);
}

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

// level_ml je od posledniho 'tare' - NENI to obsah lahvicky (ta uz po prvnim
// cyklu neni systemu znama v absolutnich ml), jen orientacni krokove
// pocitadlo pozice strikacky pro log a mechanickou ochranu.
static float levelMl() {
    return (float)posSteps / SAL_STEPS_PER_ML;
}

static bool withinMechLimit() {
    float ml = levelMl();
    if (ml < 0.0f) ml = -ml;
    return ml <= MECH_LIMIT_ML;
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
        delayMicroseconds(SAL_STEP_INTERVAL_US - 3);
        posSteps += push ? 1 : -1;
    }
}

// ---------- nahodny objem pro automaticke doplneni (6.00-17.00 ml) ----------
static float randomRefillMl() {
    long hundredths = random(0, 1101);   // 0..1100 -> 0.00..11.00
    return 6.0f + (float)hundredths / 100.0f;
}

// ---------- diagnostika ----------
static const char *stateName() {
    switch (runState) {
        case RS_IDLE: return "IDLE";
        case RS_AWAIT_CONFIRM: return "CEKA NA '1'/'0' (potvrzeni detekce)";
    }
    return "?";
}

static void printInfo() {
    Serial.print(F("# CAPDAC1=")); Serial.print(capdac[0]);
    Serial.print(F(" CAPDAC2=")); Serial.println(capdac[1]);
    Serial.print(F("# deltaCritical=")); Serial.print(deltaCritical, 4);
    Serial.print(F(" confirmSamples=")); Serial.println(confirmSamples);
    Serial.print(F("# MECH_LIMIT_ML=")); Serial.print(MECH_LIMIT_ML, 1);
    Serial.print(F(" (hardwarova ochrana, ne detekcni alarm)"));
    Serial.print(F(" samplePeriod=")); Serial.println(samplePeriodMs);
    Serial.print(F("# cycleCountTarget=")); Serial.print(cycleCountTarget);
    Serial.print(F(" cycleIndex=")); Serial.print(cycleIndex);
    Serial.print(F(" state=")); Serial.println(stateName());
    Serial.print(F("# driver=")); Serial.print(driverEnabled ? F("ON") : F("OFF"));
    Serial.print(F(" tared=")); Serial.print(tared ? F("ano") : F("ne"));
    Serial.print(F(" poloha strikacky(od tare)=")); Serial.print(levelMl(), 3);
    Serial.println(F(" ml"));
}

static void printHelp() {
    Serial.println(F("# h=napoveda i=info a=autoCAPDAC n=sum"));
    Serial.println(F("# o=driver ON  x=driver OFF / STOP behem behu"));
    Serial.println(F("# t=tare (reset pocitadla, po naplneni strikacky)"));
    Serial.println(F("# j/k=jog +-0.5ml (odvzdusneni)"));
    Serial.println(F("# g=spustit celou davkovou sekvenci (cycleCountTarget cyklu)"));
    Serial.println(F("# 1=potvrdit detekci OK   0=potvrdit chybnou detekci"));
    Serial.println(F("#   (obojí automaticky pokracuje dalsim cyklem)"));
    Serial.println(F("# r<n>=pocet cyklu  dc<pF>=delta kriticka hrana"));
    Serial.println(F("# cf<n>=potvrzovacich vzorku  p<ms>=perioda vzorku"));
    Serial.println(F("# #<text>=znacka"));
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

static void logSample(char phase, float raw1, float sm1, float raw2, float ref) {
    Serial.print(cycleIndex);
    Serial.print(';'); Serial.print(millis() - runStartMs);
    Serial.print(';'); Serial.print(levelMl(), 2);
    Serial.print(';'); Serial.print(phase);
    Serial.print(';'); Serial.print(raw1, 4);
    Serial.print(';'); Serial.print(sm1, 4);
    Serial.print(';'); Serial.print(ref, 4);
    Serial.print(';'); Serial.print(raw2, 4);
    Serial.print(';'); Serial.print(raw1 - baseline[0], 4);
    Serial.print(';'); Serial.println(raw2 - baseline[1], 4);
}

// Klidova doba pred kazdym cyklem odsavani (motor stoji) - po vytazeni/
// vraceni lahvicky se signal muze na chvili vychylit; radeji zacit cyklus
// s cerstvym, plne naplnenym vyhlazovacim oknem.
static void settleBeforeCycle() {
    resetSmooth();
    Serial.println(F("# ustaleni (motor stoji, cca 5 s)..."));
    uint32_t startMs = millis();
    uint32_t lastSampleMs = millis() - samplePeriodMs;
    while (millis() - startMs < SETTLE_MS || smoothCount < SMOOTH_WINDOW) {
        if (abortRequested()) return;
        uint32_t now = millis();
        if (now - lastSampleMs >= samplePeriodMs) {
            lastSampleMs = now;
            pushSmooth(readPf(0));
        }
    }
}

static void printSummary();
static void startCycle();

// ---------- odsavani: hledani DOLNI (kriticke) hrany, vrchol jen informativne ----------
static void runWithdrawCycle() {
    Serial.print(F("# --- CYKLUS ")); Serial.print(cycleIndex);
    Serial.println(F(" : odsavani, hledani DOLNI (KRITICKE) HRANY ---"));
    settleBeforeCycle();

    digitalWrite(PIN_STEPPER_EN, STEPPER_ENABLED_LEVEL);
    driverEnabled = true;
    digitalWrite(PIN_SAL_DIR, SAL_DIR_PUSH_LEVEL == HIGH ? LOW : HIGH);  // smer odsavani
    delayMicroseconds(10);

    // Neomezene maximum od zacatku tohohle cyklu - zadne klouzave okno.
    // Po prechodu skutecneho vrcholu uz C1 jen monotonne klesa (overeno
    // na capacitive_cycle_test datech), takze se sam od sebe "zamkne"
    // na spravne hodnote bez rizika, ze by "zapomnel" driv nez treba.
    float peakMax = -1e9f;
    float peakLevelAtMax = 0.0f;

    uint8_t belowCount = 0;
    uint32_t lastStepUs = micros();
    uint32_t lastSampleMs = millis();

    while (true) {
        if (abortRequested()) {
            stopMotorDisable();
            runState = RS_IDLE;
            Serial.println(F("# ABORT - sekvence zastavena"));
            return;
        }
        uint32_t nowUs = micros();
        if (nowUs - lastStepUs >= SAL_STEP_INTERVAL_US) {
            lastStepUs = nowUs;
            digitalWrite(PIN_SAL_STEP, HIGH);
            delayMicroseconds(3);
            digitalWrite(PIN_SAL_STEP, LOW);
            posSteps--;
            if (!withinMechLimit()) {
                stopMotorDisable();
                runState = RS_IDLE;
                Serial.print(F("# *** MECHANICKY DORAZ STRIKACKY (odsavani, cyklus "));
                Serial.print(cycleIndex);
                Serial.println(F(") *** kriticka hrana NEBYLA nalezena, sekvence zastavena - zkontroluj hardware/senzor ***"));
                return;
            }
        }
        uint32_t nowMs = millis();
        if (nowMs - lastSampleMs >= samplePeriodMs) {
            lastSampleMs = nowMs;
            float raw1 = readPf(0);
            float raw2 = readPf(1);
            float sm = pushSmooth(raw1);
            if (sm > peakMax) {
                peakMax = sm;
                peakLevelAtMax = levelMl();
            }
            logSample('w', raw1, sm, raw2, peakMax);

            float drop = peakMax - sm;
            if (drop >= deltaCritical) {
                belowCount++;
                if (belowCount >= confirmSamples) {
                    stopMotorDisable();
                    resultPeakLevel[cycleIndex - 1] = peakLevelAtMax;
                    resultPeakVal[cycleIndex - 1] = peakMax;
                    resultLowerLevel[cycleIndex - 1] = levelMl();
                    resultLowerVal[cycleIndex - 1] = sm;
                    resultConfirmOk[cycleIndex - 1] = 2;   // 2 = zatim nepotvrzeno
                    resultRefillMl[cycleIndex - 1] = -1.0f;
                    Serial.print(F("# *** DOLNI (KRITICKA) HRANA DETEKOVANA *** cyklus="));
                    Serial.print(cycleIndex);
                    Serial.print(F(" poloha(od tare)=")); Serial.print(levelMl(), 2);
                    Serial.print(F(" ml  C1_ted=")); Serial.print(sm, 4);
                    Serial.print(F(" pokles_od_vrcholu=")); Serial.println(drop, 4);
                    Serial.print(F("# (info) vrchol C1 byl pri poloze="));
                    Serial.print(peakLevelAtMax, 2);
                    Serial.print(F(" ml C1="));
                    Serial.println(peakMax, 4);
                    Serial.println(F("# Vytahni lahvicku, zkontroluj hladinu, vrat zpet do studny."));
                    Serial.println(F("# '1' = detekce OK   '0' = detekce chybna  (obojí -> automaticky dalsi cyklus)"));
                    runState = RS_AWAIT_CONFIRM;
                    return;
                }
            } else {
                belowCount = 0;
            }
        }
    }
}

// ---------- automaticke davkovani nahodneho objemu zpet do lahvicky ----------
// Vraci false, pokud beh skoncil predcasne (ABORT / mechanicky doraz) -
// volajici pak nesmi pokracovat dalsim cyklem.
static bool dispenseRandomRefill(float ml) {
    Serial.print(F("# --- CYKLUS ")); Serial.print(cycleIndex);
    Serial.print(F(" : automaticke doplneni lahvicky, ")); Serial.print(ml, 2);
    Serial.println(F(" ml ---"));

    digitalWrite(PIN_STEPPER_EN, STEPPER_ENABLED_LEVEL);
    driverEnabled = true;
    digitalWrite(PIN_SAL_DIR, SAL_DIR_PUSH_LEVEL);   // smer davkovani
    delayMicroseconds(10);

    int32_t stepsToGo = stepsFor(ml);
    int32_t stepsDone = 0;
    uint32_t lastStepUs = micros();
    uint32_t lastSampleMs = millis();

    while (stepsDone < stepsToGo) {
        if (abortRequested()) {
            stopMotorDisable();
            runState = RS_IDLE;
            Serial.println(F("# ABORT - sekvence zastavena"));
            return false;
        }
        uint32_t nowUs = micros();
        if (nowUs - lastStepUs >= SAL_STEP_INTERVAL_US) {
            lastStepUs = nowUs;
            digitalWrite(PIN_SAL_STEP, HIGH);
            delayMicroseconds(3);
            digitalWrite(PIN_SAL_STEP, LOW);
            stepsDone++;
            posSteps++;
            if (!withinMechLimit()) {
                stopMotorDisable();
                runState = RS_IDLE;
                Serial.print(F("# *** MECHANICKY DORAZ STRIKACKY (doplnovani, cyklus "));
                Serial.print(cycleIndex);
                Serial.println(F(") *** sekvence zastavena - zkontroluj hardware ***"));
                return false;
            }
        }
        uint32_t nowMs = millis();
        if (nowMs - lastSampleMs >= samplePeriodMs) {
            lastSampleMs = nowMs;
            float raw1 = readPf(0);
            float raw2 = readPf(1);
            logSample('r', raw1, raw1, raw2, 0.0f);
        }
    }
    stopMotorDisable();
    return true;
}

static void printSummary() {
    Serial.println(F("# === SOUHRN VSECH CYKLU ==="));
    Serial.println(F("# cyklus;vrchol_level_ml;vrchol_C1;dolni_level_ml;dolni_C1;pokles_pF;potvrzeno;doplneno_po_cyklu_ml"));
    uint8_t okCount = 0, badCount = 0;
    for (uint8_t i = 0; i < cycleIndex; i++) {
        Serial.print(i + 1);
        Serial.print(';'); Serial.print(resultPeakLevel[i], 2);
        Serial.print(';'); Serial.print(resultPeakVal[i], 4);
        Serial.print(';'); Serial.print(resultLowerLevel[i], 2);
        Serial.print(';'); Serial.print(resultLowerVal[i], 4);
        Serial.print(';'); Serial.print(resultPeakVal[i] - resultLowerVal[i], 4);
        Serial.print(';');
        if (resultConfirmOk[i] == 1) { Serial.print('1'); okCount++; }
        else if (resultConfirmOk[i] == 0) { Serial.print('0'); badCount++; }
        else { Serial.print('?'); }
        Serial.print(';');
        if (resultRefillMl[i] >= 0.0f) Serial.println(resultRefillMl[i], 2);
        else Serial.println('-');
    }
    Serial.print(F("# potvrzeno OK=")); Serial.print(okCount);
    Serial.print(F(" chybne=")); Serial.println(badCount);
    Serial.println(F("# vrchol_* je jen INFORMATIVNI (poloha maxima C1), NENI to detekovana/pouzita hrana"));
    Serial.println(F("# === SEKVENCE HOTOVA ==="));
}

static void startCycle() {
    runWithdrawCycle();
}

// ---------- prikazy ----------
static void handleLine() {
    if (lineLen == 0) return;

    if (lineLen >= 2 && line[0] == 'd' && line[1] == 'c') {
        float v = atof(&line[2]);
        if (v > 0.0f) { deltaCritical = v; Serial.print(F("# deltaCritical=")); Serial.println(deltaCritical, 4); }
        return;
    }
    if (lineLen >= 2 && line[0] == 'c' && line[1] == 'f') {
        int v = atoi(&line[2]);
        if (v >= 1 && v <= 100) { confirmSamples = (uint8_t)v; Serial.print(F("# confirmSamples=")); Serial.println(confirmSamples); }
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
            for (uint8_t i = 0; i < N_CH; i++) baseline[i] = readPf(i);
            tared = true;
            runState = RS_IDLE;
            cycleIndex = 0;
            Serial.println(F("# tare - pocitadlo pozice vynulovano"));
            break;
        case 'j':
            if (!driverEnabled) { Serial.println(F("# driver je OFF, napred 'o'")); break; }
            jogOnce(true);
            Serial.print(F("# jog +0.5ml, poloha=")); Serial.println(levelMl(), 2);
            break;
        case 'k':
            if (!driverEnabled) { Serial.println(F("# driver je OFF, napred 'o'")); break; }
            jogOnce(false);
            Serial.print(F("# jog -0.5ml, poloha=")); Serial.println(levelMl(), 2);
            break;
        case 'r': {
            int v = atoi(&line[1]);
            if (v >= 1 && v <= MAX_CYCLES) { cycleCountTarget = (uint8_t)v; Serial.print(F("# cycleCountTarget=")); Serial.println(cycleCountTarget); }
            break;
        }
        case 'g':
            if (runState != RS_IDLE) { Serial.println(F("# CHYBA: sekvence uz bezi/ceka na potvrzeni")); break; }
            if (!tared) { Serial.println(F("# CHYBA: neprovedeno tare, nejdriv 't'")); break; }
            randomSeed(analogRead(A0) + micros());
            runStartMs = millis();
            cycleIndex = 1;
            Serial.println(F("# cycle;t_ms;level_ml;faze;C1_raw;C1_smooth;peak_ref;C2_raw;d1;d2"));
            startCycle();
            break;
        case '1':
        case '0':
            if (runState != RS_AWAIT_CONFIRM) { Serial.println(F("# CHYBA: neni na co navazat (viz 'i')")); break; }
            resultConfirmOk[cycleIndex - 1] = (line[0] == '1') ? 1 : 0;
            Serial.print(F("# potvrzeno: ")); Serial.println(line[0] == '1' ? F("OK") : F("CHYBNA DETEKCE"));
            if (cycleIndex >= cycleCountTarget) {
                printSummary();
                runState = RS_IDLE;
                break;
            }
            {
                float r = randomRefillMl();
                resultRefillMl[cycleIndex - 1] = r;
                if (!dispenseRandomRefill(r)) break;   // ABORT / mech. doraz uz vypsal duvod
                cycleIndex++;
                startCycle();
            }
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

    Serial.println(F("# FDC1004 - davkova ziva detekce kriticke hrany (10x, auto-doplneni)"));
    Serial.println(F("# pouziva fyziologicky stepper D6/D7, EN=A3 (odsavani i davkovani)"));
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

    printInfo();
    printHelp();
    Serial.println(F("# POSTUP: 1) strikacka na strednich ~30 ml (viz README - mech. rezerva)"));
    Serial.println(F("#         2) lahvicka na libovolnou hladinu ~6-17 ml, do studny"));
    Serial.println(F("#         3) 'o' driver ON, 't' tare, 'g' start (cycleCountTarget cyklu)"));
    Serial.println(F("#         4) po kazde detekci: zkontrolovat, vratit do studny, '1'/'0'"));
    Serial.println(F("#            - dal uz sketch pokracuje sam (doplneni + dalsi cyklus)"));
}

void loop() {
    pollSerial();
}
