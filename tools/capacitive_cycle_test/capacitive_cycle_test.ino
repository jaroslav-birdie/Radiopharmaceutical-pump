// ============================================================
//  Opakovaný spojitý cyklus odsávání/doplňování - potvrzení
//  detekovatelnosti horní a dolní hrany kruhové elektrody (CIN1)
//  SAMOSTATNÝ diagnostický sketch - nemá nic společného s firmware
//  čerpadla. Ovládá fyziologický krokový motor (D6/D7, DRV8825 na
//  sdíleném nENBL A3).
//
//  Liší se od capacitive_sweep_test.ino:
//    - SPOJITÉ pomalé odsávání/doplňování (motor běží plynule,
//      žádné kroky-a-čekej), stejné tempo jako reálný provoz
//      (FLOW_S_PER_ML = 5 s/ml, config.h).
//    - AUTOMATICKY OPAKOVANÉ (výchozí 5×): odsaje cycleMl (výchozí
//      20 ml), pak stejným tempem doplní cycleMl zpět, a znovu.
//    - Cílem NENÍ přesná poloha hrany, ale SPOLEHLIVOST detekce
//      (opakovatelnost, bezpečná marže nad šumem) - viz README.
//    - CIN2 (svislá) se loguje jen jako kontrolní signál pohybu
//      hladiny (systém není ucpaný), bez zvláštní analýzy.
//
//  Zpětná klapka na fyziologické větvi byla pro tento test
//  odstraněna (viz CLAUDE.md) - motor tedy zvládá oba směry.
//
//  Postup (viz README.md v tomto adresáři):
//    1. Naplnit lahvičku NA PLNO.
//    2. 'o' - driver ON, volitelně 'j'/'k' - odvzdušnění.
//    3. 't' - tare (aktuální poloha = cycleMl, "plná").
//    4. 'g' - spustit automatických `cycleCount` cyklů
//       (odsaj cycleMl -> doplň cycleMl -> opakuj).
//    5. 'x' kdykoliv za běhu = okamžité zastavení (nouzové).
//
//  Poznámka k zarovnání cyklů: skutečná poloha vrcholu CIN1 se
//  mezi cykly může mírně lišit (vůle pístu, drobné ztráty) -
//  jednotlivé průběhy se PO SBĚRU DAT zarovnávají podle polohy
//  vlastního vrcholu, ne podle nominální hodnoty level_ml. To je
//  úkol až pro zpracování dat, sketch to neřeší.
//
//  Výstup je CSV (oddělovač ';'), řádky '#' jsou komentáře/značky -
//  zkopírovat celý výstup ze Serial Monitoru. Při výchozích
//  parametrech (20 ml, 5 cyklů, perioda vzorku 50 ms) vygeneruje
//  řádově desítky tisíc řádků - pro velký objem dat zvaž terminál
//  s logováním do souboru místo Arduino Serial Monitoru, nebo zvyš
//  'p' (perioda vzorku) pro menší objem dat.
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
#define SAL_STEP_INTERVAL_US  ((uint32_t)(1000000.0f * FLOW_S_PER_ML / SAL_STEPS_PER_ML))  // ~7812 us, stejne tempo jako provoz

#define JOG_ML             0.5f   // j/k - rucni odvzdusneni pred startem

// ---------- parametry (laditelné příkazy m/r/p PŘED 't') ----------
static float    cycleMl        = 20.0f;   // 'm<ml>' - kolik se odsaje a zase dopljni za jeden cyklus
static uint8_t  cycleCount     = 5;       // 'r<n>'  - pocet opakovani (min. doporuceno 5)
static uint16_t samplePeriodMs = 50;      // 'p<ms>' - perioda vzorku behem spojiteho pohybu

static uint8_t  capdac[N_CH]   = { 0, 0 };
static float    baseline[N_CH] = { 0.0f, 0.0f };
static bool     tared          = false;
static bool     driverEnabled  = false;
static int32_t  posSteps       = 0;      // odchylka od tare (cycleMl) v krocich
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

static int32_t stepsFor(float ml) {
    return (int32_t)(ml * SAL_STEPS_PER_ML + 0.5f);
}

static float levelMl() {
    return cycleMl + (float)posSteps / SAL_STEPS_PER_ML;
}

// Jeden krátký (blokující) pohyb pro ruční odvzdušnění - mimo cyklus.
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
    Serial.print(F("# cycleMl=")); Serial.print(cycleMl, 1);
    Serial.print(F(" cycleCount=")); Serial.print(cycleCount);
    Serial.print(F(" samplePeriod=")); Serial.println(samplePeriodMs);
    Serial.print(F("# driver=")); Serial.print(driverEnabled ? F("ON") : F("OFF"));
    Serial.print(F(" tared=")); Serial.print(tared ? F("ano") : F("ne"));
    Serial.print(F(" level=")); Serial.print(levelMl(), 3);
    Serial.println(F(" ml (orientacni)"));
}

static void printHelp() {
    Serial.println(F("# h=napoveda i=info a=autoCAPDAC n=sum"));
    Serial.println(F("# o=driver ON  x=driver OFF / STOP behem behu"));
    Serial.println(F("# t=tare (poloha=cycleMl, plna)  j/k=jog +-0.5ml"));
    Serial.println(F("# g=start automatickych cyklu (odsaj->doplni x cycleCount)"));
    Serial.println(F("# m<ml>=cycleMl (nastavit PRED t)  r<n>=pocet cyklu"));
    Serial.println(F("# p<ms>=perioda vzorku behem pohybu   #<text>=znacka"));
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
static void logSample(uint8_t cycleNum, const char *dirLabel) {
    float c1 = readPf(0);
    float c2 = readPf(1);
    Serial.print(cycleNum);
    Serial.print(';'); Serial.print(millis() - runStartMs);
    Serial.print(';'); Serial.print(levelMl(), 2);
    Serial.print(';'); Serial.print(dirLabel);
    Serial.print(';'); Serial.print(c1, 4);
    Serial.print(';'); Serial.print(c2, 4);
    Serial.print(';'); Serial.print(c1 - baseline[0], 4);
    Serial.print(';'); Serial.println(c2 - baseline[1], 4);
}

// ---------- spojitý pohyb s průběžným vzorkováním ----------
// Motor běží plynule (stejné tempo jako provoz, FLOW_S_PER_ML), CIN1/CIN2
// se loguje každých samplePeriodMs nezávisle na krokování. Vrátí false,
// pokud přišel abort (motor se zastaví okamžitě).
static bool driveContinuous(bool push, float ml, const char *dirLabel, uint8_t cycleNum) {
    int32_t totalSteps = stepsFor(ml);
    int32_t stepsDone = 0;
    digitalWrite(PIN_SAL_DIR, push ? SAL_DIR_PUSH_LEVEL
                                    : (SAL_DIR_PUSH_LEVEL == HIGH ? LOW : HIGH));
    delayMicroseconds(10);

    uint32_t lastStepUs = micros();
    uint32_t lastSampleMs = millis();
    logSample(cycleNum, dirLabel);

    while (stepsDone < totalSteps) {
        if (abortRequested()) {
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
        }
        uint32_t nowMs = millis();
        if (nowMs - lastSampleMs >= samplePeriodMs) {
            lastSampleMs = nowMs;
            logSample(cycleNum, dirLabel);
        }
    }
    logSample(cycleNum, dirLabel);
    return true;
}

// ---------- automatické opakované cykly ----------
static void runCycles() {
    if (!driverEnabled) {
        Serial.println(F("# CHYBA: driver je vypnuty, nejdriv 'o'"));
        return;
    }
    if (!tared) {
        Serial.println(F("# CHYBA: neprovedeno tare, nejdriv 't'"));
        return;
    }

    runStartMs = millis();
    Serial.println(F("# cycle;t_ms;level_ml;dir;C1_pF;C2_pF;d1;d2"));

    for (uint8_t c = 1; c <= cycleCount; c++) {
        Serial.print(F("# --- CYKLUS ")); Serial.print(c);
        Serial.println(F(" ODSAVANI START ---"));
        if (!driveContinuous(false, cycleMl, "down", c)) { return; }

        Serial.print(F("# --- CYKLUS ")); Serial.print(c);
        Serial.println(F(" ODSAVANI HOTOVO, DOPLNENI START ---"));
        if (!driveContinuous(true, cycleMl, "up", c)) { return; }

        Serial.print(F("# --- CYKLUS ")); Serial.print(c);
        Serial.println(F(" DOPLNENI HOTOVO ---"));
    }

    Serial.print(F("# --- VSECHNY CYKLY HOTOVO, trvani "));
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
            Serial.print(F("# tare - aktualni poloha = cycleMl ("));
            Serial.print(cycleMl, 1);
            Serial.println(F(" ml, plna)"));
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
            runCycles();
            break;
        case 'm': {
            float v = atof(&line[1]);
            if (v > 0.0f && v <= 60.0f) {
                cycleMl = v;
                Serial.print(F("# cycleMl=")); Serial.print(cycleMl, 1);
                Serial.println(F(" ml - pozor, nastav PRED 't' (tare)"));
            }
            break;
        }
        case 'r': {
            int v = atoi(&line[1]);
            if (v >= 1 && v <= 50) { cycleCount = (uint8_t)v; Serial.print(F("# cycleCount=")); Serial.println(cycleCount); }
            break;
        }
        case 'p': {
            int v = atoi(&line[1]);
            if (v >= 10 && v <= 2000) { samplePeriodMs = (uint16_t)v; Serial.print(F("# samplePeriod=")); Serial.println(samplePeriodMs); }
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

    Serial.println(F("# FDC1004 - opakovany spojity cyklus (detekce hran)"));
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
    Serial.println(F("# POSTUP: 1) naplnit lahvicku NA PLNO"));
    Serial.println(F("#         2) 'o' driver ON, volitelne 'j'/'k' odvzdusneni"));
    Serial.println(F("#         3) 't' tare (poloha=cycleMl)"));
    Serial.println(F("#         4) 'g' start automatickych cyklu"));
}

void loop() {
    pollSerial();
}
