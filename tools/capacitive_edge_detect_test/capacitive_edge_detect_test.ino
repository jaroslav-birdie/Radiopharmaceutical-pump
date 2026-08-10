// ============================================================
//  Živá detekce horní a dolní hrany kruhové elektrody (CIN1)
//  s pauzou na vizuální kontrolu, 5× opakováno.
//  SAMOSTATNÝ diagnostický sketch - nemá nic společného s firmware
//  čerpadla. Ovládá fyziologický krokový motor (D6/D7, DRV8825 na
//  sdíleném nENBL A3) - POUZE odsávání, nikdy nedávkuje zpět.
//
//  Na rozdíl od predchozích nástrojů (capacitive_sweep_test,
//  capacitive_cycle_test) tohle NENÍ jen logování dat - běží tu
//  přímo navržený detekční algoritmus (vyhlazení 25 vzorků +
//  pokles od průběžného maxima + potvrzení přes N vzorků), a podle
//  jeho výstupu se motor SÁM zastavuje.
//
//  Postup jednoho cyklu:
//    FÁZE 1: odsává, dokud nenajde HORNÍ hranu (vrchol C1).
//            -> zastaví motor, obsluha vytáhne lahvičku, zkontroluje.
//    'y'   : obsluha vrátí lahvičku, potvrdí -> FÁZE 2.
//    FÁZE 2: odsává dál, dokud nenajde DOLNÍ (kritickou) hranu.
//            -> zastaví motor, obsluha vytáhne lahvičku, zkontroluje
//               hladinu, RUČNĚ doplní nějaké množství kapaliny do
//               lahvičky (mimo řízení systému - motor nedávkuje!)
//               a vrátí lahvičku zpět.
//    'y'   : potvrzení -> další cyklus (znovu FÁZE 1), celkem
//            `cycleCount`-krát (výchozí 5).
//
//  DŮLEŽITÉ - počáteční hladina po ručním doplnění NENÍ a nemůže
//  být systému známá (viz CLAUDE.md diskuze) - očekává se jen, že
//  bude ležet v rozmezí ~8-20 ml. Detekční algoritmus na přesné
//  počáteční hladině nezávisí (funguje jen podle tvaru signálu).
//  `level_ml` ve výstupu je čistě orientační krokové počítadlo od
//  posledního 't', NE skutečný obsah lahvičky.
//
//  DŮLEŽITÉ - fyzická kapacita stříkačky: protože motor mezi cykly
//  nikdy nedávkuje zpět (doplňuje se ručně mimo systém), kumulativní
//  odběr přes 5 cyklů se sčítá. Sketch hlídá `maxSyringeMl` a před
//  každým dalším cyklem zkontroluje, jestli zbývá dost zdvihu - pokud
//  ne, zastaví se a vyzve k ručnímu doplnění STŘÍKAČKY (ne lahvičky)
//  a novému 't'.
//
//  Zpětná klapka na fyziologické větvi byla pro tento test
//  odstraněna (viz CLAUDE.md) - motor tedy zvládá odsávat.
//
//  Postup (viz README.md v tomto adresáři):
//    1. Naplnit stříkačku vydatně (>= maxSyringeMl + rezerva).
//    2. Lahvičku naplnit na libovolnou hladinu v rozmezí ~8-20 ml,
//       vložit do studny.
//    3. 'o' - driver ON, volitelně 'j'/'k' - odvzdušnění.
//    4. 't' - tare (referenční bod pro krokové počítadlo).
//    5. 'g' - spustit celou automatickou sekvenci (5 cyklů).
//    6. Řídit se pokyny na sériové lince ('y' pro pokračování).
//    7. 'x' kdykoliv za běhu = okamžité zastavení (nouzové).
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

// ---------- fyziologický krokový motor (piny sdíleny s hlavním firmware) ----------
#define PIN_SAL_STEP      6
#define PIN_SAL_DIR       7
#define PIN_STEPPER_EN    A3
#define STEPPER_ENABLED_LEVEL   LOW
#define STEPPER_DISABLED_LEVEL  HIGH
#define SAL_DIR_PUSH_LEVEL      HIGH     // HIGH = dávkování do lahvičky (tady se nepoužívá)

#define SCREW_PITCH_MM     8.0f
#define STEPS_PER_REV      200
#define MICROSTEP_DIV      16
#define STEPS_PER_MM       ((STEPS_PER_REV * MICROSTEP_DIV) / SCREW_PITCH_MM)   // 400
#define SAL_SYR_ML_PER_MM  0.625f
#define SAL_STEPS_PER_ML   (STEPS_PER_MM / SAL_SYR_ML_PER_MM)                   // 640

#define FLOW_S_PER_ML      5UL
#define SAL_STEP_INTERVAL_US  ((uint32_t)(1000000.0f * FLOW_S_PER_ML / SAL_STEPS_PER_ML))  // ~7812 us

#define JOG_ML             0.5f

// ---------- vyhlazení signálu (ověřeno na datech z capacitive_cycle_test) ----------
#define SMOOTH_WINDOW      25    // ~5 s pri 200 ms/vzorek - overeny kompromis sum/zpozdeni
#define SETTLE_MS          5000UL  // klidova doba pred kazdou fazi (motor stoji), po fyzickem zasahu

#define MAX_CYCLES         10

// ---------- parametry (nastavit pred 'g'; 'du'/'dc'/'cf'/'sm' jsou 2-znakove prikazy) ----------
static float    deltaUpper      = 0.08f;   // pF - pokles od maxima = "prekroceni horni hrany"
static float    deltaCritical   = 0.22f;   // pF - pokles od vrcholu = "kriticka (dolni) hladina"
static uint8_t  confirmSamples  = 5;       // kolik po sobe jdoucich vzorku musi prah drzet
static uint16_t samplePeriodMs  = 200;
static float    phaseSafetyMl   = 18.0f;   // bezpecnostni strop na jednu fazi (mel by staci i pro start pri 20 ml)
static float    maxSyringeMl    = 55.0f;   // kolik smi strikacka celkem od tare odebrat (60ml strikacka - rezerva)
static uint8_t  cycleCountTarget = 5;

static uint8_t  capdac[N_CH]   = { 0, 0 };
static float    baseline[N_CH] = { 0.0f, 0.0f };
static bool     tared          = false;
static bool     driverEnabled  = false;
static int32_t  posSteps       = 0;      // 0 = pri tare; zaporne = odsato od tare (kumulativne pres vsechny cykly)
static char     line[24];
static uint8_t  lineLen        = 0;
static uint32_t runStartMs     = 0;

enum RunState { RS_IDLE, RS_AWAIT_PHASE2, RS_AWAIT_NEXT_CYCLE };
static RunState runState = RS_IDLE;
static uint8_t  cycleIndex = 0;   // 1-based

static float resultUpperLevel[MAX_CYCLES], resultUpperVal[MAX_CYCLES];
static float resultLowerLevel[MAX_CYCLES], resultLowerVal[MAX_CYCLES];
static float peakRef = 0.0f;   // uzamcena vrcholova hodnota pro aktualni cyklus (faze 2)

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

// ---------- nízká úroveň I2C ----------
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

// level_ml je od posledniho 'tare' - NENI to obsah lahvicky (ten po rucnim
// doplneni neznáme), jen orientacni krokove pocitadlo pro log.
static float levelMl() {
    return (float)posSteps / SAL_STEPS_PER_ML;
}

static float withdrawnMl() {
    return -levelMl();
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

// ---------- diagnostika ----------
static const char *stateName() {
    switch (runState) {
        case RS_IDLE: return "IDLE";
        case RS_AWAIT_PHASE2: return "CEKA NA 'y' (-> faze 2)";
        case RS_AWAIT_NEXT_CYCLE: return "CEKA NA 'y' (-> dalsi cyklus)";
    }
    return "?";
}

static void printInfo() {
    Serial.print(F("# CAPDAC1=")); Serial.print(capdac[0]);
    Serial.print(F(" CAPDAC2=")); Serial.println(capdac[1]);
    Serial.print(F("# deltaUpper=")); Serial.print(deltaUpper, 4);
    Serial.print(F(" deltaCritical=")); Serial.print(deltaCritical, 4);
    Serial.print(F(" confirmSamples=")); Serial.println(confirmSamples);
    Serial.print(F("# phaseSafetyMl=")); Serial.print(phaseSafetyMl, 1);
    Serial.print(F(" maxSyringeMl=")); Serial.print(maxSyringeMl, 1);
    Serial.print(F(" samplePeriod=")); Serial.println(samplePeriodMs);
    Serial.print(F("# cycleCountTarget=")); Serial.print(cycleCountTarget);
    Serial.print(F(" cycleIndex=")); Serial.print(cycleIndex);
    Serial.print(F(" state=")); Serial.println(stateName());
    Serial.print(F("# driver=")); Serial.print(driverEnabled ? F("ON") : F("OFF"));
    Serial.print(F(" tared=")); Serial.print(tared ? F("ano") : F("ne"));
    Serial.print(F(" level(od tare)=")); Serial.print(levelMl(), 3);
    Serial.print(F(" ml  odebrano celkem=")); Serial.print(withdrawnMl(), 2);
    Serial.println(F(" ml"));
}

static void printHelp() {
    Serial.println(F("# h=napoveda i=info a=autoCAPDAC n=sum"));
    Serial.println(F("# o=driver ON  x=driver OFF / STOP behem behu"));
    Serial.println(F("# t=tare (reset pocitadla, po naplneni strikacky)"));
    Serial.println(F("# j/k=jog +-0.5ml (odvzdusneni)"));
    Serial.println(F("# g=spustit celou sekvenci (cycleCountTarget cyklu)"));
    Serial.println(F("# y=potvrdit pokracovani (kontextove, viz vypis)"));
    Serial.println(F("# r<n>=pocet cyklu  m<ml>=bezp. strop na fazi"));
    Serial.println(F("# du<pF>=delta horni hrana  dc<pF>=delta kriticka hrana"));
    Serial.println(F("# cf<n>=potvrzovacich vzorku  sm<ml>=max. odber ze strikacky"));
    Serial.println(F("# p<ms>=perioda vzorku   #<text>=znacka"));
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

static void logSample(uint8_t phase, float raw1, float sm1, float raw2) {
    Serial.print(cycleIndex);
    Serial.print(';'); Serial.print(millis() - runStartMs);
    Serial.print(';'); Serial.print(levelMl(), 2);
    Serial.print(';'); Serial.print(phase == 1 ? "p1" : "p2");
    Serial.print(';'); Serial.print(raw1, 4);
    Serial.print(';'); Serial.print(sm1, 4);
    Serial.print(';'); Serial.print(raw2, 4);
    Serial.print(';'); Serial.print(raw1 - baseline[0], 4);
    Serial.print(';'); Serial.println(raw2 - baseline[1], 4);
}

// Klidova doba pred kazdou fazi (motor stoji) - po vytazeni/vraceni lahvicky
// nebo rucnim doplneni se signal muze na chvili vychylit; radeji zacit
// faze s cerstvym, plne naplnenym vyhlazovacim oknem.
static void settleBeforePhase() {
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

// ---------- FAZE 1: hledani HORNI hrany (vrchol C1) ----------
static void runPhase1() {
    Serial.print(F("# --- CYKLUS ")); Serial.print(cycleIndex);
    Serial.println(F(" / FAZE 1: hledani HORNI HRANY ---"));
    settleBeforePhase();

    digitalWrite(PIN_STEPPER_EN, STEPPER_ENABLED_LEVEL);
    driverEnabled = true;
    digitalWrite(PIN_SAL_DIR, SAL_DIR_PUSH_LEVEL == HIGH ? LOW : HIGH);  // smer odsavani
    delayMicroseconds(10);

    float runMax = -1e9f;
    uint8_t belowCount = 0;
    int32_t stepsDone = 0;
    int32_t safetyLimit = stepsFor(phaseSafetyMl);
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
        if (stepsDone < safetyLimit && nowUs - lastStepUs >= SAL_STEP_INTERVAL_US) {
            lastStepUs = nowUs;
            digitalWrite(PIN_SAL_STEP, HIGH);
            delayMicroseconds(3);
            digitalWrite(PIN_SAL_STEP, LOW);
            stepsDone++;
            posSteps--;
        }
        uint32_t nowMs = millis();
        if (nowMs - lastSampleMs >= samplePeriodMs) {
            lastSampleMs = nowMs;
            float raw1 = readPf(0);
            float raw2 = readPf(1);
            float sm = pushSmooth(raw1);
            logSample(1, raw1, sm, raw2);

            if (sm > runMax) {
                runMax = sm;
                belowCount = 0;
            } else if (runMax - sm >= deltaUpper) {
                belowCount++;
                if (belowCount >= confirmSamples) {
                    stopMotorDisable();
                    peakRef = runMax;
                    resultUpperLevel[cycleIndex - 1] = levelMl();
                    resultUpperVal[cycleIndex - 1] = runMax;
                    Serial.print(F("# *** HORNI HRANA DETEKOVANA *** cyklus="));
                    Serial.print(cycleIndex);
                    Serial.print(F(" level(od tare)=")); Serial.print(levelMl(), 2);
                    Serial.print(F(" ml  C1_vrchol=")); Serial.print(runMax, 4);
                    Serial.print(F(" C1_ted=")); Serial.println(sm, 4);
                    Serial.println(F("# Vytahni lahvicku ze studny, zkontroluj stav."));
                    Serial.println(F("# Az bude lahvicka zpet ve studni, potvrd 'y' -> FAZE 2."));
                    runState = RS_AWAIT_PHASE2;
                    return;
                }
            } else {
                belowCount = 0;
            }
        }
        if (stepsDone >= safetyLimit) {
            stopMotorDisable();
            runState = RS_IDLE;
            Serial.print(F("# *** BEZPECNOSTNI LIMIT: horni hrana NEDETEKOVANA (faze 1, cyklus "));
            Serial.print(cycleIndex);
            Serial.println(F(") - sekvence zastavena ***"));
            return;
        }
    }
}

// ---------- FAZE 2: hledani DOLNI (kriticke) hrany ----------
static void runPhase2() {
    Serial.print(F("# --- CYKLUS ")); Serial.print(cycleIndex);
    Serial.println(F(" / FAZE 2: hledani DOLNI (KRITICKE) HRANY ---"));
    settleBeforePhase();

    digitalWrite(PIN_STEPPER_EN, STEPPER_ENABLED_LEVEL);
    driverEnabled = true;
    digitalWrite(PIN_SAL_DIR, SAL_DIR_PUSH_LEVEL == HIGH ? LOW : HIGH);
    delayMicroseconds(10);

    uint8_t belowCount = 0;
    int32_t stepsDone = 0;
    int32_t safetyLimit = stepsFor(phaseSafetyMl);
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
        if (stepsDone < safetyLimit && nowUs - lastStepUs >= SAL_STEP_INTERVAL_US) {
            lastStepUs = nowUs;
            digitalWrite(PIN_SAL_STEP, HIGH);
            delayMicroseconds(3);
            digitalWrite(PIN_SAL_STEP, LOW);
            stepsDone++;
            posSteps--;
        }
        uint32_t nowMs = millis();
        if (nowMs - lastSampleMs >= samplePeriodMs) {
            lastSampleMs = nowMs;
            float raw1 = readPf(0);
            float raw2 = readPf(1);
            float sm = pushSmooth(raw1);
            logSample(2, raw1, sm, raw2);

            float drop = peakRef - sm;
            if (drop >= deltaCritical) {
                belowCount++;
                if (belowCount >= confirmSamples) {
                    stopMotorDisable();
                    resultLowerLevel[cycleIndex - 1] = levelMl();
                    resultLowerVal[cycleIndex - 1] = sm;
                    Serial.print(F("# *** DOLNI (KRITICKA) HRANA DETEKOVANA *** cyklus="));
                    Serial.print(cycleIndex);
                    Serial.print(F(" level(od tare)=")); Serial.print(levelMl(), 2);
                    Serial.print(F(" ml  C1_ted=")); Serial.print(sm, 4);
                    Serial.print(F(" pokles_od_vrcholu=")); Serial.println(drop, 4);
                    Serial.println(F("# Vytahni lahvicku, zkontroluj hladinu."));

                    if (cycleIndex >= cycleCountTarget) {
                        printSummary();
                        runState = RS_IDLE;
                    } else {
                        Serial.println(F("# RUCNE doplň nejake mnozstvi kapaliny do lahvicky"));
                        Serial.println(F("# (motor NEDAVKUJE - doplnujes mimo system), vrat zpet."));
                        Serial.println(F("# Az bude lahvicka zpet, potvrd 'y' -> dalsi cyklus."));
                        runState = RS_AWAIT_NEXT_CYCLE;
                    }
                    return;
                }
            } else {
                belowCount = 0;
            }
        }
        if (stepsDone >= safetyLimit) {
            stopMotorDisable();
            runState = RS_IDLE;
            Serial.print(F("# *** BEZPECNOSTNI LIMIT: dolni hrana NEDETEKOVANA (faze 2, cyklus "));
            Serial.print(cycleIndex);
            Serial.println(F(") - sekvence zastavena ***"));
            return;
        }
    }
}

static void printSummary() {
    Serial.println(F("# === SOUHRN VSECH CYKLU ==="));
    Serial.println(F("# cyklus;horni_level_ml;horni_C1;dolni_level_ml;dolni_C1;pokles_pF"));
    for (uint8_t i = 0; i < cycleIndex; i++) {
        Serial.print(i + 1);
        Serial.print(';'); Serial.print(resultUpperLevel[i], 2);
        Serial.print(';'); Serial.print(resultUpperVal[i], 4);
        Serial.print(';'); Serial.print(resultLowerLevel[i], 2);
        Serial.print(';'); Serial.print(resultLowerVal[i], 4);
        Serial.print(';'); Serial.println(resultUpperVal[i] - resultLowerVal[i], 4);
    }
    Serial.print(F("# celkem odebrano ze strikacky od tare: "));
    Serial.print(withdrawnMl(), 2);
    Serial.println(F(" ml"));
    Serial.println(F("# === SEKVENCE HOTOVA ==="));
}

// ---------- příkazy ----------
static void handleLine() {
    if (lineLen == 0) return;

    if (lineLen >= 2 && line[0] == 'd' && line[1] == 'u') {
        float v = atof(&line[2]);
        if (v > 0.0f) { deltaUpper = v; Serial.print(F("# deltaUpper=")); Serial.println(deltaUpper, 4); }
        return;
    }
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
    if (lineLen >= 2 && line[0] == 's' && line[1] == 'm') {
        float v = atof(&line[2]);
        if (v > 0.0f && v <= 60.0f) { maxSyringeMl = v; Serial.print(F("# maxSyringeMl=")); Serial.println(maxSyringeMl, 1); }
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
            Serial.println(F("# tare - pocitadlo odberu vynulovano"));
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
        case 'g':
            if (runState != RS_IDLE) { Serial.println(F("# CHYBA: sekvence uz bezi/ceka na 'y'")); break; }
            if (!tared) { Serial.println(F("# CHYBA: neprovedeno tare, nejdriv 't'")); break; }
            runStartMs = millis();
            cycleIndex = 1;
            Serial.println(F("# cycle;t_ms;level_ml;faze;C1_raw;C1_smooth;C2_raw;d1;d2"));
            runPhase1();
            break;
        case 'y':
            if (runState == RS_AWAIT_PHASE2) {
                runPhase2();
            } else if (runState == RS_AWAIT_NEXT_CYCLE) {
                if (withdrawnMl() + phaseSafetyMl > maxSyringeMl) {
                    Serial.println(F("# CHYBA: dosla by kapacita strikacky pro dalsi cyklus."));
                    Serial.println(F("# Rucne doplň STRIKACKU (ne lahvicku), pak znovu 't' a 'g'."));
                    runState = RS_IDLE;
                    break;
                }
                cycleIndex++;
                runPhase1();
            } else {
                Serial.println(F("# CHYBA: neni na co navazat (viz 'i')"));
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

    Serial.println(F("# FDC1004 - ziva detekce hran s pauzou na kontrolu (5x)"));
    Serial.println(F("# pouziva fyziologicky stepper D6/D7, EN=A3 (jen odsavani)"));
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
    Serial.println(F("# POSTUP: 1) strikacka vydatne naplnena (>= maxSyringeMl)"));
    Serial.println(F("#         2) lahvicka na libovolnou hladinu ~8-20 ml, do studny"));
    Serial.println(F("#         3) 'o' driver ON, 't' tare, 'g' start (5 cyklu)"));
    Serial.println(F("#         4) ridit se pokyny, potvrzovat 'y'"));
}

void loop() {
    pollSerial();
}
