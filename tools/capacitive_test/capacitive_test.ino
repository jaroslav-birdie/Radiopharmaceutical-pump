// ============================================================
//  Odladění kapacitního snímání hladiny (FDC1004)
//  SAMOSTATNÝ diagnostický sketch - nemá nic společného s firmware
//  čerpadla. Slouží k charakterizaci elektrod PŘED tím, než se
//  detekce hladiny zapojí do stavového automatu injektoru.
//
//  Elektrody (viz CLAUDE.md):
//    CIN1 = kruhová, hlídá kritickou (minimální) hladinu
//    CIN2 = svislá, sleduje pohyb hladiny (od horního okraje
//           lahvičky až po spodní okraj CIN1)
//    CIN3 = běžně aktivní shield na SHLD1/SHLD2 - zde ho lze
//           dočasně měřit jako 3. kanál pro diagnostiku
//
//  Výstup je CSV (oddělovač ';') pro přímé vložení do tabulky.
//  Desetinný oddělovač je '.', v české lokalizaci Excelu je nutné
//  při importu přepnout, nebo nahradit '.' za ','.
//
//  Příkazy po sériové lince (9600 Bd, zakončit Enterem):
//    h          nápověda
//    s          start/stop streamování
//    t          tare - nastaví aktuální hodnoty jako referenci (d1..d3)
//    a          automatická volba CAPDAC pro všechny kanály
//    c<ch> <v>  ruční CAPDAC, např. "c1 12" (ch 1-3, v 0-31)
//    e<ch>      zapnout/vypnout kanál, např. "e3"
//    r<1|2|3>   vzorkovací frekvence 100 / 200 / 400 S/s
//    n          test šumu (256 vzorků: min, max, p-p, směr. odchylka)
//    p<ms>      perioda výpisu, např. "p100"
//    i          informace o konfiguraci
//    #<text>    značka do logu, např. "#hladina 6 ml"
// ============================================================
#include <Arduino.h>
#include <Wire.h>

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
#define RAW_PER_PF        524288.0f    // 2^19 dle datasheetu
#define RAW_NEAR_FULL     7000000L     // ~13,4 pF - blízko limitu +-15 pF

#define N_CH              3
#define NOISE_SAMPLES     256

static uint8_t  capdac[N_CH]    = { 0, 0, 0 };
static bool     chEnabled[N_CH] = { true, true, false };
static float    baseline[N_CH]  = { 0.0f, 0.0f, 0.0f };
static uint8_t  rateSel         = 1;      // 1=100, 2=200, 3=400 S/s
static bool     streaming       = false;
static uint16_t outPeriodMs     = 200;
static uint32_t lastOut         = 0;
static char     line[24];
static uint8_t  lineLen         = 0;

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

// ---------- konfigurace ----------

// CONF_MEASx: CHA[15:13]=kanál, CHB[12:10]=100 (CAPDAC), CAPDAC[9:5]
static void applyConfig() {
    uint16_t measMask = 0;
    for (uint8_t i = 0; i < N_CH; i++) {
        writeReg(REG_CONF_MEAS1 + i,
                 ((uint16_t)i << 13) | (0x4 << 10) | ((uint16_t)capdac[i] << 5));
        if (chEnabled[i]) {
            measMask |= (uint16_t)1 << (7 - i);      // MEAS1->b7, MEAS2->b6, MEAS3->b5
        }
    }
    // FDC_CONF: RATE[11:10], REPEAT[8], INIT_MEASx[7:4]
    writeReg(REG_FDC_CONF, ((uint16_t)rateSel << 10) | (1 << 8) | measMask);
    delay(30);                                        // ustálení po překonfigurování
}

// 24bitový výsledek se znaménkem: MSB registr + horních 8 bitů LSB registru
static int32_t readRaw(uint8_t ch) {
    int16_t msb = (int16_t)readReg(REG_MEAS1_MSB + ch * 2);
    uint16_t lsb = readReg(REG_MEAS1_MSB + ch * 2 + 1);
    return ((int32_t)msb << 8) | (lsb >> 8);
}

static float readPf(uint8_t ch) {
    return readRaw(ch) / RAW_PER_PF + capdac[ch] * CAPDAC_STEP_PF;
}

// Zvyšuje CAPDAC, dokud se hodnota nevejde do rozsahu +-15 pF.
static void autoCapdac(uint8_t ch) {
    capdac[ch] = 0;
    applyConfig();
    for (uint8_t i = 0; i < CAPDAC_MAX; i++) {
        int32_t raw = readRaw(ch);
        if (raw < RAW_NEAR_FULL) {
            break;
        }
        capdac[ch]++;
        applyConfig();
    }
}

// ---------- diagnostika ----------

static void printInfo() {
    Serial.print(F("# rate="));
    Serial.print(rateSel == 1 ? 100 : (rateSel == 2 ? 200 : 400));
    Serial.print(F(" S/s, perioda="));
    Serial.print(outPeriodMs);
    Serial.println(F(" ms"));
    for (uint8_t i = 0; i < N_CH; i++) {
        Serial.print(F("# CIN"));
        Serial.print(i + 1);
        Serial.print(chEnabled[i] ? F(" ON  capdac=") : F(" off capdac="));
        Serial.print(capdac[i]);
        Serial.print(F(" ("));
        Serial.print(capdac[i] * CAPDAC_STEP_PF, 2);
        Serial.print(F(" pF), aktualne "));
        Serial.print(readPf(i), 4);
        Serial.println(F(" pF"));
    }
}

// Statistika šumu - klíčová pro volbu prahů a hystereze.
static void noiseTest() {
    Serial.println(F("# test sumu, nehybat sestavou..."));
    for (uint8_t ch = 0; ch < N_CH; ch++) {
        if (!chEnabled[ch]) {
            continue;
        }
        float mn = 1e9f, mx = -1e9f, sum = 0.0f, sumSq = 0.0f;
        for (uint16_t i = 0; i < NOISE_SAMPLES; i++) {
            float v = readPf(ch);
            if (v < mn) mn = v;
            if (v > mx) mx = v;
            sum += v;
            sumSq += v * v;
            delay(5);
        }
        float mean = sum / NOISE_SAMPLES;
        float var = sumSq / NOISE_SAMPLES - mean * mean;
        if (var < 0.0f) {
            var = 0.0f;
        }
        Serial.print(F("# CIN"));
        Serial.print(ch + 1);
        Serial.print(F(": prum="));
        Serial.print(mean, 4);
        Serial.print(F(" min="));
        Serial.print(mn, 4);
        Serial.print(F(" max="));
        Serial.print(mx, 4);
        Serial.print(F(" p-p="));
        Serial.print(mx - mn, 4);
        Serial.print(F(" sigma="));
        Serial.print(sqrt(var), 5);
        Serial.println(F(" pF"));
    }
}

static void printHelp() {
    Serial.println(F("# h=napoveda s=stream t=tare a=autoCAPDAC n=sum i=info"));
    Serial.println(F("# c<ch> <v>=capdac  e<ch>=on/off  r<1-3>=rate  p<ms>=perioda"));
    Serial.println(F("# #<text>=znacka do logu"));
}

static void printHeader() {
    Serial.println(F("# t_ms;C1_pF;C2_pF;C3_pF;d1;d2;d3"));
}

// ---------- příkazy ----------

static void handleLine() {
    if (lineLen == 0) {
        return;
    }
    char cmd = line[0];
    switch (cmd) {
        case 'h':
            printHelp();
            break;
        case 's':
            streaming = !streaming;
            if (streaming) {
                printHeader();
            } else {
                Serial.println(F("# stop"));
            }
            break;
        case 't':
            for (uint8_t i = 0; i < N_CH; i++) {
                baseline[i] = readPf(i);
            }
            Serial.println(F("# tare"));
            break;
        case 'a':
            for (uint8_t i = 0; i < N_CH; i++) {
                if (chEnabled[i]) {
                    autoCapdac(i);
                }
            }
            printInfo();
            break;
        case 'c': {                                   // c<ch> <hodnota>
            uint8_t ch = line[1] - '1';
            int v = atoi(&line[2]);
            if (ch < N_CH && v >= 0 && v <= CAPDAC_MAX) {
                capdac[ch] = (uint8_t)v;
                applyConfig();
                printInfo();
            } else {
                Serial.println(F("# chybny parametr"));
            }
            break;
        }
        case 'e': {
            uint8_t ch = line[1] - '1';
            if (ch < N_CH) {
                chEnabled[ch] = !chEnabled[ch];
                applyConfig();
                printInfo();
            }
            break;
        }
        case 'r': {
            uint8_t v = line[1] - '0';
            if (v >= 1 && v <= 3) {
                rateSel = v;
                applyConfig();
                printInfo();
            }
            break;
        }
        case 'p': {
            int v = atoi(&line[1]);
            if (v >= 20 && v <= 5000) {
                outPeriodMs = (uint16_t)v;
                Serial.print(F("# perioda "));
                Serial.println(outPeriodMs);
            }
            break;
        }
        case 'n':
            noiseTest();
            break;
        case 'i':
            printInfo();
            break;
        case '#':
            Serial.println(line);                     // značka se jen zopakuje do logu
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

// ---------- setup / loop ----------

void setup() {
    Serial.begin(9600);
    Wire.begin();
    Wire.setClock(100000UL);
    delay(100);

    Serial.println(F("# FDC1004 - test kapacitniho snimani hladiny"));
    uint16_t manuf = readReg(REG_MANUF_ID);
    uint16_t dev = readReg(REG_DEVICE_ID);
    Serial.print(F("# MANUFACTURER_ID=0x"));
    Serial.print(manuf, HEX);
    Serial.print(F(" DEVICE_ID=0x"));
    Serial.println(dev, HEX);
    if (manuf != MANUF_ID_VAL || dev != DEVICE_ID_VAL) {
        Serial.println(F("# CHYBA: cip neodpovida (ocekavano 0x5449 / 0x1004)"));
        Serial.println(F("# zkontroluj I2C, adresu 0x50 a napajeni"));
    }

    applyConfig();
    for (uint8_t i = 0; i < N_CH; i++) {
        if (chEnabled[i]) {
            autoCapdac(i);
        }
    }
    printInfo();
    printHelp();
}

void loop() {
    pollSerial();
    if (!streaming) {
        return;
    }
    uint32_t now = millis();
    if (now - lastOut < outPeriodMs) {
        return;
    }
    lastOut = now;

    float v[N_CH];
    for (uint8_t i = 0; i < N_CH; i++) {
        v[i] = chEnabled[i] ? readPf(i) : 0.0f;
    }
    Serial.print(now);
    for (uint8_t i = 0; i < N_CH; i++) {
        Serial.print(';');
        Serial.print(v[i], 4);
    }
    for (uint8_t i = 0; i < N_CH; i++) {
        Serial.print(';');
        Serial.print(v[i] - baseline[i], 4);
    }
    Serial.println();
}
