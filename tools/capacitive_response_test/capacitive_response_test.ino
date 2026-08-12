// ============================================================
//  Test 1 - odezva komory a elektrod na plneni/odcerpavani PO
//  DOKONCENI KOMPLETNIHO STINENI (olovo uzemnene na GND FDC1004 +
//  Cu kolem cele studny). SAMOSTATNY diagnosticky sketch, nema nic
//  spolecneho s firmware cerpadla. Ovlada fyziologicky krokovy
//  motor (D6/D7, DRV8825 na sdilenem nENBL A3) - odsava z lahvicky
//  I davkuje zpet, ze stejne strikacky (zpetna klapka byla pro
//  tenhle test odstranena, viz CLAUDE.md).
//
//  Vychazi z tools/capacitive_cycle_test (spojity pohyb, opakovane
//  cykly), rozsireno o:
//   1) DVE VARIANTY POCATECNIHO OBJEMU za sebou v jednom behu -
//      Faze A (~20 ml, odber/doplneni 18 ml, cycleCount cyklu),
//      pak Faze B (~10 ml, odber/doplneni 8 ml, cycleCount cyklu).
//      Mezi fazemi obsluha RUCNE upravi objem v lahvicce - dalsi
//      'g' to bez noveho 't' odmitne (viz awaitingRetare nize).
//   2) USTALENI (5 s, motor stoji) PRED KAZDYM smerem pohybu se
//      taky loguje (dir=settle) - zajima nas i chovani "v klidu
//      pred/po pohybu", ne jen samotny pohyb.
//   3) ZADNA detekce hrany, zadny prah - jen syrova data pro
//      offline analyzu, drive nez se z ni navrhne novy prah pro
//      hledani kriticke hladiny (viz CLAUDE.md a README.md tady).
//
//  DULEZITE - tohle NENI test odolnosti proti dotyku/ruseni (to je
//  soucasti navazujiciho testu hledani hladiny) - studny se BEHEM
//  BEHU zamerne NESAHA.
//
//  Postup (viz README.md v tomto adresari):
//    1. Naplnit strikacku na rozumnou stredni hodnotu (~30 ml).
//    2. Lahvicku naplnit RUCNE na ~20 ml, vlozit do studny.
//    3. 'a' - overit CAPDAC (po zmene stineni se mohl posunout),
//       volitelne 'n' - staticky test sumu.
//    4. 'o' - driver ON, volitelne 'j'/'k' - odvzdusneni.
//    5. 't' - tare (aktualni poloha = 20 ml, "plna" pro Fazi A).
//    6. 'g' - spusti Fazi A (cycleCount cyklu, 18 ml odber/doplneni).
//    7. Po dokonceni Faze A: vyprazdnit/upravit lahvicku na ~10 ml,
//       vratit do studny, znovu 't' (poloha=10 ml), pak 'g' - Faze B.
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

#define FLOW_S_PER_ML      5UL
#define SAL_STEP_INTERVAL_US  ((uint32_t)(1000000.0f * FLOW_S_PER_ML / SAL_STEPS_PER_ML))  // ~7812 us, stejne tempo jako provoz

#define JOG_ML             0.5f   // j/k - rucni odvzdusneni pred startem

#define SETTLE_MS          5000UL   // klid pred kazdym smerem pohybu (motor stoji), loguje se taky

// Vyhradne HARDWAROVA ochrana zdvihu strikacky (60 ml) - stejny princip
// jako v capacitive_edge_detect_test.ino. Faze A pohne az 18 ml od tare,
// pohodlne pod timhle stropem.
#define MECH_LIMIT_ML      25.0f

// ---------- parametry (laditelne prikazy pred 't'/'g': wa/wb/r/p) ----------
static float    phaseStartMl[2] = { 20.0f, 10.0f };  // jen popisek/level_ml, "plna" pri tare dane faze
static float    phaseMoveMl[2]  = { 18.0f, 8.0f };   // kolik se odsaje a zase doplni za jeden cyklus
static uint8_t  cycleCount      = 10;
static uint16_t samplePeriodMs  = 200;

static uint8_t  capdac[N_CH]   = { 0, 0 };
static float    baseline[N_CH] = { 0.0f, 0.0f };
static bool     tared          = false;
static bool     driverEnabled  = false;
static int32_t  posSteps       = 0;      // odchylka od tare (phaseStartMl[phaseIdx]) v krocich
static char     line[24];
static uint8_t  lineLen        = 0;
static uint32_t runStartMs     = 0;

// 0 = dalsi 'g' spusti Fazi A (20/18 ml), 1 = dalsi 'g' spusti Fazi B
// (10/8 ml, az po rucni uprave a novem 't'), 2 = obe faze hotovy
static uint8_t  phaseIdx       = 0;
static bool     awaitingRetare = false;   // true hned po dokonceni Faze A, dokud obsluha znovu neda 't'

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

static float levelMl() {
    return phaseStartMl[phaseIdx < 2 ? phaseIdx : 1] + (float)posSteps / SAL_STEPS_PER_ML;
}

static bool withinMechLimit() {
    float ml = (float)posSteps / SAL_STEPS_PER_ML;
    if (ml < 0.0f) ml = -ml;
    return ml <= MECH_LIMIT_ML;
}

static void stopMotorDisable() {
    digitalWrite(PIN_STEPPER_EN, STEPPER_DISABLED_LEVEL);
    driverEnabled = false;
}

// Jeden kratky (blokujici) pohyb pro rucni odvzdusneni - mimo cyklus.
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
static void printInfo() {
    Serial.print(F("# CAPDAC1=")); Serial.print(capdac[0]);
    Serial.print(F(" CAPDAC2=")); Serial.println(capdac[1]);
    Serial.print(F("# faze A: start=")); Serial.print(phaseStartMl[0], 1);
    Serial.print(F(" ml odber/doplneni=")); Serial.print(phaseMoveMl[0], 1);
    Serial.print(F(" ml   faze B: start=")); Serial.print(phaseStartMl[1], 1);
    Serial.print(F(" ml odber/doplneni=")); Serial.println(phaseMoveMl[1], 1);
    Serial.print(F("# cycleCount=")); Serial.print(cycleCount);
    Serial.print(F(" samplePeriod=")); Serial.println(samplePeriodMs);
    Serial.print(F("# dalsi faze: "));
    Serial.println(phaseIdx == 0 ? F("A (20 ml)") : (phaseIdx == 1 ? F("B (10 ml)") : F("zadna, obe hotovy")));
    Serial.print(F("# awaitingRetare=")); Serial.println(awaitingRetare ? F("ano") : F("ne"));
    Serial.print(F("# driver=")); Serial.print(driverEnabled ? F("ON") : F("OFF"));
    Serial.print(F(" tared=")); Serial.print(tared ? F("ano") : F("ne"));
    Serial.print(F(" level=")); Serial.print(levelMl(), 3);
    Serial.println(F(" ml (orientacni)"));
}

static void printHelp() {
    Serial.println(F("# h=napoveda i=info a=autoCAPDAC n=sum"));
    Serial.println(F("# o=driver ON  x=driver OFF / STOP behem behu"));
    Serial.println(F("# t=tare (poloha=start aktualni faze)  j/k=jog +-0.5ml"));
    Serial.println(F("# g=spustit dalsi fazi (nejdriv A, pak po rucni uprave a 't' B)"));
    Serial.println(F("# r<n>=pocet cyklu na fazi  p<ms>=perioda vzorku"));
    Serial.println(F("# wa<ml>=odber/doplneni Faze A  wb<ml>=odber/doplneni Faze B"));
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

// ---------- log jednoho vzorku ----------
static void logSample(uint8_t variantMl, uint8_t cycleNum, const char *dirLabel) {
    float c1 = readPf(0);
    float c2 = readPf(1);
    Serial.print(variantMl);
    Serial.print(';'); Serial.print(cycleNum);
    Serial.print(';'); Serial.print(millis() - runStartMs);
    Serial.print(';'); Serial.print(levelMl(), 2);
    Serial.print(';'); Serial.print(dirLabel);
    Serial.print(';'); Serial.print(c1, 4);
    Serial.print(';'); Serial.print(c2, 4);
    Serial.print(';'); Serial.print(c1 - baseline[0], 4);
    Serial.print(';'); Serial.println(c2 - baseline[1], 4);
}

// ---------- klid pred kazdym smerem pohybu (motor stoji), loguje se taky ----------
// Vraci false pri ABORTu.
static bool settleAndLog(uint8_t variantMl, uint8_t cycleNum) {
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
            logSample(variantMl, cycleNum, "settle");
        }
    }
    return true;
}

// ---------- spojity pohyb s prubeznym vzorkovanim (shodne s capacitive_cycle_test) ----------
// Vraci false pri ABORTu nebo mechanickem dorazu (motor uz je vypnuty).
static bool driveContinuous(uint8_t variantMl, bool push, float ml, const char *dirLabel, uint8_t cycleNum) {
    int32_t totalSteps = stepsFor(ml);
    int32_t stepsDone = 0;
    digitalWrite(PIN_SAL_DIR, push ? SAL_DIR_PUSH_LEVEL
                                    : (SAL_DIR_PUSH_LEVEL == HIGH ? LOW : HIGH));
    delayMicroseconds(10);

    uint32_t lastStepUs = micros();
    uint32_t lastSampleMs = millis();
    logSample(variantMl, cycleNum, dirLabel);

    while (stepsDone < totalSteps) {
        if (abortRequested()) {
            stopMotorDisable();
            Serial.println(F("# ABORT behem spojiteho pohybu"));
            return false;
        }
        uint32_t nowUs = micros();
        if (nowUs - lastStepUs >= SAL_STEP_INTERVAL_US) {
            lastStepUs = nowUs;
            digitalWrite(PIN_SAL_STEP, HIGH);
            delayMicroseconds(3);
            digitalWrite(PIN_SAL_STEP, LOW);
            stepsDone++;
            posSteps += push ? 1 : -1;
            if (!withinMechLimit()) {
                stopMotorDisable();
                Serial.println(F("# *** MECHANICKY DORAZ STRIKACKY *** beh zastaven - zkontroluj hardware ***"));
                return false;
            }
        }
        uint32_t nowMs = millis();
        if (nowMs - lastSampleMs >= samplePeriodMs) {
            lastSampleMs = nowMs;
            logSample(variantMl, cycleNum, dirLabel);
        }
    }
    logSample(variantMl, cycleNum, dirLabel);
    return true;
}

// ---------- jedna faze: cycleCount cyklu {ustaleni, odsavani, ustaleni, doplneni} ----------
static void runPhase() {
    if (!driverEnabled) { Serial.println(F("# CHYBA: driver je vypnuty, nejdriv 'o'")); return; }
    if (!tared) { Serial.println(F("# CHYBA: neprovedeno tare, nejdriv 't'")); return; }
    if (phaseIdx == 1 && awaitingRetare) {
        Serial.println(F("# CHYBA: nejdriv uprav lahvicku na 10 ml a znovu 't'"));
        return;
    }
    if (phaseIdx > 1) { Serial.println(F("# obe faze uz jsou hotove (pro novy beh restartuj desku)")); return; }

    uint8_t variantMl = (uint8_t)phaseStartMl[phaseIdx];
    float moveMl = phaseMoveMl[phaseIdx];
    runStartMs = millis();
    Serial.print(F("# === FAZE ")); Serial.print(variantMl);
    Serial.print(F(" ml: ")); Serial.print(cycleCount);
    Serial.print(F(" cyklu, odber/doplneni ")); Serial.print(moveMl, 2);
    Serial.println(F(" ml ==="));
    Serial.println(F("# variant;cycle;t_ms;level_ml;dir;C1_pF;C2_pF;d1;d2"));

    for (uint8_t c = 1; c <= cycleCount; c++) {
        Serial.print(F("# --- cyklus ")); Serial.print(c); Serial.print('/'); Serial.println(cycleCount);
        if (!settleAndLog(variantMl, c)) { stopMotorDisable(); return; }
        if (!driveContinuous(variantMl, false, moveMl, "down", c)) { return; }
        if (!settleAndLog(variantMl, c)) { stopMotorDisable(); return; }
        if (!driveContinuous(variantMl, true, moveMl, "up", c)) { return; }
    }

    stopMotorDisable();
    Serial.print(F("# === FAZE ")); Serial.print(variantMl); Serial.println(F(" ml HOTOVA ==="));

    if (phaseIdx == 0) {
        phaseIdx = 1;
        awaitingRetare = true;
        Serial.println(F("# Vyprazdni/uprav lahvicku na ~10 ml, vrat do studny,"));
        Serial.println(F("# znovu 't' (tare), pak 'g' pro Fazi B."));
    } else {
        phaseIdx = 2;
        Serial.println(F("# Obe faze dokonceny - test 1 kompletni."));
    }
}

// ---------- prikazy ----------
static void handleLine() {
    if (lineLen == 0) return;

    if (lineLen >= 3 && line[0] == 'w' && line[1] == 'a') {
        float v = atof(&line[2]);
        if (v > 0.0f && v <= 60.0f) { phaseMoveMl[0] = v; Serial.print(F("# faze A odber/doplneni=")); Serial.println(phaseMoveMl[0], 2); }
        return;
    }
    if (lineLen >= 3 && line[0] == 'w' && line[1] == 'b') {
        float v = atof(&line[2]);
        if (v > 0.0f && v <= 60.0f) { phaseMoveMl[1] = v; Serial.print(F("# faze B odber/doplneni=")); Serial.println(phaseMoveMl[1], 2); }
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
            awaitingRetare = false;
            Serial.print(F("# tare - aktualni poloha = "));
            Serial.print(phaseStartMl[phaseIdx < 2 ? phaseIdx : 1], 1);
            Serial.println(F(" ml"));
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
            runPhase();
            break;
        case 'r': {
            int v = atoi(&line[1]);
            if (v >= 1 && v <= 50) { cycleCount = (uint8_t)v; Serial.print(F("# cycleCount=")); Serial.println(cycleCount); }
            break;
        }
        case 'p': {
            int v = atoi(&line[1]);
            if (v >= 20 && v <= 2000) { samplePeriodMs = (uint16_t)v; Serial.print(F("# samplePeriod=")); Serial.println(samplePeriodMs); }
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

    Serial.println(F("# Test 1 - odezva komory a elektrod (po kompletnim stineni Pb+Cu)"));
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
    Serial.println(F("# POSTUP: 1) strikacka na strednich ~30 ml"));
    Serial.println(F("#         2) lahvicka RUCNE na ~20 ml, do studny"));
    Serial.println(F("#         3) 'a' overit CAPDAC, volitelne 'n' sum"));
    Serial.println(F("#         4) 'o' driver ON, 't' tare"));
    Serial.println(F("#         5) 'g' - Faze A (10x 18 ml odber/doplneni)"));
    Serial.println(F("#         6) po dokonceni: uprav lahvicku na 10 ml, 't', 'g' - Faze B"));
}

void loop() {
    pollSerial();
}
