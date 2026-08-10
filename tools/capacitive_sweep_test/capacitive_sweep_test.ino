// ============================================================
//  Automatizovaný sweep test kapacitního senzoru FDC1004
//  SAMOSTATNÝ diagnostický sketch – nemá nic společného s firmware
//  čerpadla, ale ovládá stejný fyziologický krokový motor (D6/D7,
//  DRV8825 na sdíleném nENBL A3), aby automaticky dávkoval a odsával
//  tekutinu po přesných 0,5 ml krocích a měřil kapacitu FDC1004.
//
//  Zpětná klapka na fyziologické větvi byla pro tento test odstraněna
//  (viz CLAUDE.md, sekce Mechanika) – motor tedy může tekutinu jak
//  dávkovat do lahvičky, tak ji odsávat zpět. Bez odstraněné klapky by
//  odsávání nefungovalo (klapka propouští tok jen jedním směrem).
//
//  Postup (viz README.md v tomto adresáři):
//    1. Naplnit fyziologickou stříkačku (min. ~30 ml), lahvička prázdná
//       a na místě.
//    2. 'o' - zapnout driver.
//    3. Volitelně 'j'/'k' - poposunout tekutinu k odvzdušnění hadičky
//       (pak znovu 't', aby se posun nezapočítal do 0 ml).
//    4. 't' - tare (aktuální poloha = 0 ml, referenční kapacita).
//    5. 'g' - spustit automatický sweep 0 -> MAX -> 0 po 0,5 ml.
//    6. 'x' kdykoliv za běhu = okamžité zastavení (nouzové).
//
//  Výstup je CSV (oddělovač ';'), řádky '#' jsou komentáře/značky/
//  souhrny za hladinu - zkopírovat celý výstup ze Serial Monitoru.
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
#define SAL_DIR_PUSH_LEVEL      HIGH     // HIGH = dávkování do lahvičky

#define SCREW_PITCH_MM     8.0f
#define STEPS_PER_REV      200
#define MICROSTEP_DIV      16
#define STEPS_PER_MM       ((STEPS_PER_REV * MICROSTEP_DIV) / SCREW_PITCH_MM)   // 400
#define SAL_SYR_ML_PER_MM  0.625f
#define SAL_STEPS_PER_ML   (STEPS_PER_MM / SAL_SYR_ML_PER_MM)                   // 640

#define FLOW_S_PER_ML      5UL
#define SAL_STEP_INTERVAL_US  ((uint32_t)(1000000.0f * FLOW_S_PER_ML / SAL_STEPS_PER_ML))  // ~7812 us

#define SWEEP_STEP_ML      0.5f
#define SWEEP_STEP_STEPS   ((int32_t)(SWEEP_STEP_ML * SAL_STEPS_PER_ML + 0.5f))  // 320

// ---------- parametry sweepu (laditelné za běhu příkazy e/w/c/p/m) ----------
static float    maxVolumeMl     = 20.0f;
static float    settleEpsPf     = 0.01f;
static uint16_t settleTimeoutMs = 6000;
static uint16_t samplesPerLevel = 60;
static uint16_t fastSampleMs    = 20;
static const uint16_t settleSampleMs = 25;
static const uint8_t  settleWindow   = 8;

static uint8_t  capdac[N_CH]   = { 0, 0 };
static float    baseline[N_CH] = { 0.0f, 0.0f };
static bool     tared          = false;
static bool     driverEnabled  = false;
static int32_t  posSteps       = 0;      // dávkovaný objem od tare, v krocích
static char     line[24];
static uint8_t  lineLen        = 0;
static uint32_t runStartMs     = 0;

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

// CONF_MEASx: CHA[15:13]=kanál, CHB[12:10]=100 (CAPDAC), CAPDAC[9:5]
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

// Neblokující kontrola nouzového zastavení - 'x' kdykoliv za běhu.
static bool abortRequested() {
    if (Serial.available() > 0 && Serial.peek() == 'x') {
        Serial.read();
        return true;
    }
    return false;
}

// Vrátí false, pokud byl pohyb přerušen příkazem 'x'.
static bool moveSteps(int32_t steps, bool push) {
    digitalWrite(PIN_SAL_DIR, push ? SAL_DIR_PUSH_LEVEL
                                    : (SAL_DIR_PUSH_LEVEL == HIGH ? LOW : HIGH));
    delayMicroseconds(10);
    for (int32_t i = 0; i < steps; i++) {
        if ((i & 0x1F) == 0 && abortRequested()) {
            Serial.println(F("# ABORT behem pohybu"));
            return false;
        }
        digitalWrite(PIN_SAL_STEP, HIGH);
        delayMicroseconds(3);
        digitalWrite(PIN_SAL_STEP, LOW);
        delayMicroseconds(SAL_STEP_INTERVAL_US - 3);
        posSteps += push ? 1 : -1;
    }
    return true;
}

static float levelMl() {
    return posSteps / SAL_STEPS_PER_ML;
}

// ---------- diagnostika ----------
static void printInfo() {
    Serial.print(F("# CAPDAC1=")); Serial.print(capdac[0]);
    Serial.print(F(" CAPDAC2=")); Serial.println(capdac[1]);
    Serial.print(F("# maxVol=")); Serial.print(maxVolumeMl, 1);
    Serial.print(F(" stepMl=")); Serial.print(SWEEP_STEP_ML, 2);
    Serial.print(F(" settleEps=")); Serial.print(settleEpsPf, 4);
    Serial.print(F(" settleTimeout=")); Serial.print(settleTimeoutMs);
    Serial.print(F(" samples=")); Serial.print(samplesPerLevel);
    Serial.print(F(" fastPeriod=")); Serial.println(fastSampleMs);
    Serial.print(F("# driver=")); Serial.print(driverEnabled ? F("ON") : F("OFF"));
    Serial.print(F(" tared=")); Serial.print(tared ? F("ano") : F("ne"));
    Serial.print(F(" level=")); Serial.print(levelMl(), 3);
    Serial.println(F(" ml"));
}

static void printHelp() {
    Serial.println(F("# h=napoveda i=info a=autoCAPDAC n=sum"));
    Serial.println(F("# o=driver ON  x=driver OFF / STOP behem behu"));
    Serial.println(F("# t=tare (0 ml)  j=jog +0.5ml  k=jog -0.5ml"));
    Serial.println(F("# g=start automatickeho sweepu 0->MAX->0"));
    Serial.println(F("# e<pF>=settle epsilon  w<ms>=settle timeout"));
    Serial.println(F("# c<n>=vzorku/uroven (min 50)  p<ms>=perioda vzorku"));
    Serial.println(F("# m<ml>=max objem sweepu   #<text>=znacka"));
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

// ---------- měření jedné hladiny ----------
// Počká na ustálení (klouzavé okno p-p pod settleEpsPf na obou kanálech),
// pak zapíše >= samplesPerLevel vzorků + souhrnnou statistiku za hladinu.
// Vrátí false, pokud přišel abort.
static bool settleAndMeasure(const char *dirLabel) {
    float winC1[settleWindow], winC2[settleWindow];
    uint8_t winLen = 0, winIdx = 0;
    uint32_t startMs = millis();
    uint32_t settledAtMs;
    bool timedOut = false;

    while (true) {
        if (abortRequested()) {
            Serial.println(F("# ABORT behem ustaleni"));
            return false;
        }
        float c1 = readPf(0);
        float c2 = readPf(1);
        winC1[winIdx] = c1;
        winC2[winIdx] = c2;
        winIdx = (winIdx + 1) % settleWindow;
        if (winLen < settleWindow) winLen++;

        if (winLen == settleWindow) {
            float mn1 = 1e9f, mx1 = -1e9f, mn2 = 1e9f, mx2 = -1e9f;
            for (uint8_t i = 0; i < settleWindow; i++) {
                if (winC1[i] < mn1) mn1 = winC1[i];
                if (winC1[i] > mx1) mx1 = winC1[i];
                if (winC2[i] < mn2) mn2 = winC2[i];
                if (winC2[i] > mx2) mx2 = winC2[i];
            }
            if ((mx1 - mn1) <= settleEpsPf && (mx2 - mn2) <= settleEpsPf) {
                break;
            }
        }
        if (millis() - startMs >= settleTimeoutMs) {
            timedOut = true;
            break;
        }
        delay(settleSampleMs);
    }
    settledAtMs = millis();

    Serial.print(F("# LEVEL "));
    Serial.print(levelMl(), 2);
    Serial.print(F(" ml dir=")); Serial.print(dirLabel);
    Serial.print(F(" settle_ms=")); Serial.print(settledAtMs - startMs);
    Serial.print(F(" timeout=")); Serial.println(timedOut ? 1 : 0);

    float sum1 = 0, sum2 = 0, sq1 = 0, sq2 = 0;
    float mn1 = 1e9f, mx1 = -1e9f, mn2 = 1e9f, mx2 = -1e9f;
    for (uint16_t i = 0; i < samplesPerLevel; i++) {
        if ((i & 0x0F) == 0 && abortRequested()) {
            Serial.println(F("# ABORT behem mereni"));
            return false;
        }
        float c1 = readPf(0);
        float c2 = readPf(1);
        Serial.print(millis() - runStartMs);
        Serial.print(';'); Serial.print(levelMl(), 2);
        Serial.print(';'); Serial.print(dirLabel);
        Serial.print(';'); Serial.print(c1, 4);
        Serial.print(';'); Serial.print(c2, 4);
        Serial.print(';'); Serial.print(c1 - baseline[0], 4);
        Serial.print(';'); Serial.println(c2 - baseline[1], 4);

        sum1 += c1; sum2 += c2; sq1 += c1 * c1; sq2 += c2 * c2;
        if (c1 < mn1) mn1 = c1;
        if (c1 > mx1) mx1 = c1;
        if (c2 < mn2) mn2 = c2;
        if (c2 > mx2) mx2 = c2;
        delay(fastSampleMs);
    }

    float mean1 = sum1 / samplesPerLevel, mean2 = sum2 / samplesPerLevel;
    float var1 = sq1 / samplesPerLevel - mean1 * mean1;
    float var2 = sq2 / samplesPerLevel - mean2 * mean2;
    if (var1 < 0) var1 = 0;
    if (var2 < 0) var2 = 0;

    Serial.print(F("# LEVEL_SUMMARY "));
    Serial.print(levelMl(), 2);
    Serial.print(F(" ml dir=")); Serial.print(dirLabel);
    Serial.print(F(" mean1=")); Serial.print(mean1, 4);
    Serial.print(F(" pp1=")); Serial.print(mx1 - mn1, 4);
    Serial.print(F(" sigma1=")); Serial.print(sqrt(var1), 5);
    Serial.print(F(" mean2=")); Serial.print(mean2, 4);
    Serial.print(F(" pp2=")); Serial.print(mx2 - mn2, 4);
    Serial.print(F(" sigma2=")); Serial.println(sqrt(var2), 5);

    return true;
}

// ---------- automatický sweep 0 -> MAX -> 0 ----------
static void runSweep() {
    if (!driverEnabled) {
        Serial.println(F("# CHYBA: driver je vypnuty, nejdriv 'o'"));
        return;
    }
    if (!tared) {
        Serial.println(F("# CHYBA: neprovedeno tare, nejdriv 't'"));
        return;
    }

    uint16_t numLevels = (uint16_t)(maxVolumeMl / SWEEP_STEP_ML + 0.5f);
    runStartMs = millis();

    Serial.println(F("# t_ms;level_ml;dir;C1_pF;C2_pF;d1;d2"));
    Serial.println(F("# --- SWEEP START ---"));
    if (!settleAndMeasure("up")) { return; }

    for (uint16_t i = 0; i < numLevels; i++) {
        if (!moveSteps(SWEEP_STEP_STEPS, true)) { return; }
        if (!settleAndMeasure("up")) { return; }
    }

    Serial.println(F("# --- SWEEP OBRAT (dolu) ---"));
    for (uint16_t i = 0; i < numLevels; i++) {
        if (!moveSteps(SWEEP_STEP_STEPS, false)) { return; }
        if (!settleAndMeasure("down")) { return; }
    }

    Serial.print(F("# --- SWEEP HOTOVO, trvani "));
    Serial.print((millis() - runStartMs) / 1000UL);
    Serial.println(F(" s ---"));

    digitalWrite(PIN_STEPPER_EN, STEPPER_DISABLED_LEVEL);
    driverEnabled = false;
    Serial.println(F("# driver vypnut"));
}

// ---------- příkazy ----------
static void handleLine() {
    if (lineLen == 0) return;
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
            Serial.println(F("# tare - aktualni poloha = 0 ml"));
            break;
        case 'j':
            if (!driverEnabled) { Serial.println(F("# driver je OFF, napred 'o'")); break; }
            moveSteps(SWEEP_STEP_STEPS, true);
            Serial.print(F("# jog +0.5ml, poloha=")); Serial.println(levelMl(), 2);
            break;
        case 'k':
            if (!driverEnabled) { Serial.println(F("# driver je OFF, napred 'o'")); break; }
            moveSteps(SWEEP_STEP_STEPS, false);
            Serial.print(F("# jog -0.5ml, poloha=")); Serial.println(levelMl(), 2);
            break;
        case 'g':
            runSweep();
            break;
        case 'e': {
            float v = atof(&line[1]);
            if (v > 0.0f) { settleEpsPf = v; Serial.print(F("# settleEps=")); Serial.println(settleEpsPf, 4); }
            break;
        }
        case 'w': {
            long v = atol(&line[1]);
            if (v >= 200 && v <= 60000) { settleTimeoutMs = (uint16_t)v; Serial.print(F("# settleTimeout=")); Serial.println(settleTimeoutMs); }
            break;
        }
        case 'c': {
            int v = atoi(&line[1]);
            if (v < 50) { Serial.println(F("# min. 50 vzorku")); break; }
            if (v <= 500) { samplesPerLevel = (uint16_t)v; Serial.print(F("# samples=")); Serial.println(samplesPerLevel); }
            break;
        }
        case 'p': {
            int v = atoi(&line[1]);
            if (v >= 10 && v <= 2000) { fastSampleMs = (uint16_t)v; Serial.print(F("# fastPeriod=")); Serial.println(fastSampleMs); }
            break;
        }
        case 'm': {
            float v = atof(&line[1]);
            if (v > 0.0f && v <= 60.0f) { maxVolumeMl = v; Serial.print(F("# maxVol=")); Serial.println(maxVolumeMl, 1); }
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

    Serial.println(F("# FDC1004 - automaticky sweep test hladiny"));
    Serial.println(F("# pouziva fyziologicky stepper D6/D7, EN=A3"));
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
    Serial.println(F("# POSTUP: 1) naplnit strikacku (min ~30ml), lahvicka prazdna"));
    Serial.println(F("#         2) 'o' driver ON, volitelne 'j'/'k' odvzdusneni"));
    Serial.println(F("#         3) 't' tare (0 ml), 'g' start sweepu"));
}

void loop() {
    pollSerial();
}
