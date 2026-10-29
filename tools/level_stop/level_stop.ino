// ============================================================
//  TEST 5 – ZASTAVENI HLADINY MEZI PRSTENCI (naslepo)
//
//  Otazka: zastavi se hladina VZDY mezi prstenci (C3 odkryty, C1
//  zakryty), at obsluha pred cyklem prida jakykoli objem? Sketch o
//  pridanem objemu nic nevi - obsluha ho schvalne voli od oka a pokazde
//  jiny.
//
//  Detekce je cela relativni (viz TEST5.md):
//    cyklus 1 (uceni) - tlaci se, dokud C3 neprestane klesat; hodnota
//        C3 v tom okamziku je F3 (suchy prstenec), D1 = C1 - F3
//    cykly 2+ - zastavi se pri delta = (C1 - C3) / (C1 - F3) >= 0,7
//    pojistka - C1 klesne pod polovinu sveho maxima v cyklu (vztazeno
//        k zakladu) => hladina prochazi C1, konec serie
//  Pri detekci se pacientsky ventil zavira OKAMZITE (uvnitr vzorkovani),
//  motor stoji, teprve pak se lahvicka odvzdusni.
//
//  Loguji se syrova data jako v testech 1-4; detekce bezi nad medianem
//  z 0,5 s. Udalosti detekce jsou v logu jako radky '#'.
//
//  Serial Monitor: 115200 Bd. Log si zachyt do souboru.
// ============================================================
#include <Arduino.h>
#include <Wire.h>
#include <EEPROM.h>

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

#define N_CH              3   // CIN1 (spodni prstenec), CIN2 (svisla), CIN3 (horni prstenec)
#define BAUD             115200UL

// ---------- piny (shodne s config.h hlavniho firmware) ----------
#define PIN_AIR_STEP      4
#define PIN_AIR_DIR       5
#define PIN_STEPPER_EN    A3
#define PIN_SERVO_PATIENT 9    // OC1A
#define PIN_SERVO_AIR    10    // OC1B

#define AIR_DIR_PUSH_LEVEL      HIGH
#define STEPPER_ENABLED_LEVEL   LOW
#define STEPPER_DISABLED_LEVEL  HIGH

// ---------- serva (Timer1 HW PWM, prevzato ze servo_valve.cpp) ----------
#define SERVO_MIN_US      544
#define SERVO_MAX_US     2503
#define SERVO_SETTLE_MS  1000UL

#define PATIENT_VALVE_OPEN_DEFAULT            0
#define PATIENT_VALVE_ISOLATE_DEFAULT        90
#define AIR_VALVE_SYRINGE_TO_VIAL_DEFAULT    90
#define AIR_VALVE_SYRINGE_TO_FILTER_DEFAULT   0
#define AIR_VALVE_VIAL_TO_FILTER_DEFAULT    180

#define EEPROM_MAGIC_VALUE           0xA6
#define EEPROM_ADDR_VALID_FLAG        0
#define EEPROM_ADDR_PATIENT_OPEN      1
#define EEPROM_ADDR_PATIENT_ISOLATE   2
#define EEPROM_ADDR_AIR_SYR_TO_VIAL   3
#define EEPROM_ADDR_AIR_SYR_TO_FILT   4
#define EEPROM_ADDR_AIR_VIAL_TO_FILT  5

// ---------- mechanika ----------
#define SCREW_PITCH_MM     8.0f
#define STEPS_PER_REV      200
#define MICROSTEP_DIV      16
#define STEPS_PER_MM       ((STEPS_PER_REV * MICROSTEP_DIV) / SCREW_PITCH_MM)   // 400
#define AIR_SYR_ML_PER_MM  0.2f
#define AIR_STEPS_PER_ML   (STEPS_PER_MM / AIR_SYR_ML_PER_MM)                   // 2000
#define AIR_SYR_MAX_ML     10.0f
#define AIR_FILL_SPEED_FACTOR  2

// ---------- casovani ----------
#define KLID_PRE_MS      5000UL   // klid pred rozjezdem (referencni bod)
#define KLID_POST_MS    15000UL   // klid po zastaveni, pacient uzavren, jeste pod tlakem
#define KLID_REST_MS    30000UL   // klid po odvzdusneni - mereni polohy zastaveni
#define VALVE_SETTLE_MS  1500UL   // po prejezdu ventilu, nez se pokracuje
#define JOG_ML             0.5f
#define KLID_PRECOMP_MS  3000UL   // klid po predtlakovani, pacient jeste uzavren
#define KLID_OPEN_MS     5000UL   // klid po otevreni pacienta, pred tlacenim

// ---------- predtlakovani (konzervativni odhad, NEMERENO) ----------
// V hadicce k pacientovi muze byt po uzavreni pacientskeho ventilu
// pretlak proti lahvicce (pacient vys, zilni tlak). Kdyby se pacient
// otevrel pri atmosferickem tlaku v lahvicce, kapalina by se vratila.
// Proto se s pacientem uzavrenym a lahvickou na S<->V nejdriv vytlaci
//     V_pre = V_plyn * dP / p_atm
// dP = nejhorsi tlak v hadicce, V_plyn odhadnut spis nahoru (lahvicka
// po okraj - kapalina + strikacka + mrtvy objem). Prebytek vytece
// k pacientovi, coz je bezpecny smer.
#define VIAL_BRIM_ML          30.0f   // objem lahvicky po okraj (odhad)
#define AIR_DEAD_ML            1.0f   // mrtvy objem vzduchove cesty (odhad)
#define BACKPRESS_PATIENT_CM  40.0f   // pacient max. o delku hadicky vys
#define BACKPRESS_VENOUS_CM   20.0f   // horni odhad periferniho zilniho tlaku
#define KPA_PER_CMH2O          0.0981f
#define P_ATM_MIN_KPA         90.0f   // konzervativne nizky tlak okoli (~1000 m n. m.)

// ---------- detekce (vse relativni, zadne pF ani ml) ----------
// Median z DET_MED_N vzorku (0,5 s pri 20 vzorcich/s). Vsechny prahy jsou
// pomery k hodnotam namerenym v tomto behu - viz TEST5.md.
#define DET_MED_N           10
#define DET_HIST_STEP_ML     0.25f  // krok historie C3 pro strmost
#define DET_HIST_LEN         5      // 5 bodu = strmost pres 1 ml
#define DET_RISE_SIGMA       5.0f   // vzestup prstence nad klid 'k' > 5 sum. odchylek
#define DET_FALL_FRAC        0.5f   // uceni: pokles C3 od maxima > 0,5 x jeho vzestupu
#define DET_PLATEAU_FRAC     0.3f   // ucenie: konec poklesu = strmost < 0,3 x max. strmost
#define DET_DENOM_FRAC       0.3f   // delta se pocita, az C1 - F3 > 0,3 x D1
#define DET_GUARD_FRAC       0.5f   // pojistka: C1 - zaklad < 0,5 x (max C1 - zaklad)
#define MAX_REFILLS          3      // max. doplneni vzduchu v jednom cyklu

// ---------- hlidani senzoru ----------
// Ve skutecnych datech se stejna hodnota opakuje nejvys 2x za sebou (sum
// ~2000 LSB). 10x po sobe bitove stejna hodnota = FDC1004 nemeri.
#define FROZEN_LIMIT        10
#define FDC_CHECK_SAMPLES   20
#define FDC_CHECK_PERIOD_MS 10
#define MONITOR_PERIOD_MS  500       // prikaz 'm': vypis C1..C3 kazdych 0,5 s

// ---------- parametry behu (menitelne pres Serial) ----------
static const uint16_t samplePeriodMs = 50;   // 20 vzorku/s (DET_MED_N je na to naladen)
static uint8_t  cycleCount     = 10;    // pocet cyklu (1 uceni + 9 iteraci)
static uint8_t  flowSecPerMl   = 5;     // rychlost tlaceni
static float    precompManualMl = -1.0f; // <0 = vypocet, jinak rucne (r<ml>)
static float    deltaStop      = 0.7f;  // prah delta pro zastaveni (d<x>)

// ---------- typy detekce ----------
// Pred prvni funkci a v parametrech funkci jen jako uint8_t: Arduino IDE
// vklada automaticky generovane prototypy pred prvni funkci a vyctovy typ
// v nich by jeste nemusel byt znamy.
enum DetMode : uint8_t { DET_OFF, DET_LEARN, DET_STOP };
enum DetHit  : uint8_t { HIT_NONE, HIT_STOP, HIT_GUARD };

// ---------- uhly ventilu ----------
static uint8_t angPatOpen, angPatIsolate;
static uint8_t angAirSyrVial, angAirSyrFilt, angAirVialFilt;
static uint8_t curPatAngle = 255, curAirAngle = 255;

// ---------- stav ----------
static uint8_t  capdac[N_CH] = { 0, 0, 0 };
static bool     tared        = false;
static bool     running      = false;
static char     line[20];
static char     opText[12];          // posledni odpoved obsluhy (jen do logu)
static uint8_t  lineLen      = 0;
static int32_t  airSteps     = 0;   // obsah vzduchove strikacky v krocich
static int32_t  pushedSteps  = 0;   // kumulativne vytlaceno v tomto cyklu
static uint32_t cycleT0      = 0;   // zacatek cyklu (t_ms je relativni)
static uint8_t  cycleNo      = 0;

static bool detectHit();
static bool checkFdc();

static float airMl()    { return (float)airSteps / AIR_STEPS_PER_ML; }
static float pushedMl() { return (float)pushedSteps / AIR_STEPS_PER_ML; }

static uint32_t pushIntervalUs() {
    return (uint32_t)(1000000.0f * flowSecPerMl / AIR_STEPS_PER_ML);
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

// 400 S/s (RATE=11) misto 100 S/s - kanaly se prevadi postupne (~2,5 ms
// kazdy), takze pri 20 vzorcich/s a trech kanalech pochazi kazdy vzorek
// z cerstve dokoncene konverze, ne z opakovane precteneho stareho vysledku.
static void applyConfig() {
    uint16_t measMask = 0;
    for (uint8_t i = 0; i < N_CH; i++) {
        writeReg(REG_CONF_MEAS1 + i,
                 ((uint16_t)i << 13) | (0x4 << 10) | ((uint16_t)capdac[i] << 5));
        measMask |= (uint16_t)1 << (7 - i);
    }
    writeReg(REG_FDC_CONF, (3 << 10) | (1 << 8) | measMask);   // 400 S/s, REPEAT
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

// ============================================================
//  Serva – Timer1 fast PWM 50 Hz (prevzato ze servo_valve.cpp)
// ============================================================
static void servoTimerInit() {
    pinMode(PIN_SERVO_PATIENT, OUTPUT);
    pinMode(PIN_SERVO_AIR, OUTPUT);
    TCCR1A = _BV(WGM11);
    TCCR1B = _BV(WGM13) | _BV(WGM12) | _BV(CS11);   // prescaler 8 -> 0,5 us/tik
    ICR1 = 39999;                                    // 20 ms
    OCR1A = 0;
    OCR1B = 0;
}

static uint16_t angleTicks(uint8_t angle) {
    if (angle > 180) angle = 180;
    uint16_t us = SERVO_MIN_US
                + (uint16_t)(((uint32_t)(SERVO_MAX_US - SERVO_MIN_US) * angle) / 180);
    return us * 2;
}

static void servoAttach() {
    OCR1A = angleTicks(curPatAngle);
    OCR1B = angleTicks(curAirAngle);
    TCCR1A |= _BV(COM1A1) | _BV(COM1B1);   // vystupy az po nastaveni OCR
}

static void loadValveAngles() {
    if (EEPROM.read(EEPROM_ADDR_VALID_FLAG) == EEPROM_MAGIC_VALUE) {
        angPatOpen     = EEPROM.read(EEPROM_ADDR_PATIENT_OPEN);
        angPatIsolate  = EEPROM.read(EEPROM_ADDR_PATIENT_ISOLATE);
        angAirSyrVial  = EEPROM.read(EEPROM_ADDR_AIR_SYR_TO_VIAL);
        angAirSyrFilt  = EEPROM.read(EEPROM_ADDR_AIR_SYR_TO_FILT);
        angAirVialFilt = EEPROM.read(EEPROM_ADDR_AIR_VIAL_TO_FILT);
        Serial.println(F("# uhly ventilu nacteny z EEPROM"));
    } else {
        angPatOpen     = PATIENT_VALVE_OPEN_DEFAULT;
        angPatIsolate  = PATIENT_VALVE_ISOLATE_DEFAULT;
        angAirSyrVial  = AIR_VALVE_SYRINGE_TO_VIAL_DEFAULT;
        angAirSyrFilt  = AIR_VALVE_SYRINGE_TO_FILTER_DEFAULT;
        angAirVialFilt = AIR_VALVE_VIAL_TO_FILTER_DEFAULT;
        Serial.println(F("# EEPROM neplatna - vychozi uhly z config.h"));
    }
}

// ============================================================
//  Motor
// ============================================================
static void steppersEnable()  { digitalWrite(PIN_STEPPER_EN, STEPPER_ENABLED_LEVEL); }
static void steppersDisable() { digitalWrite(PIN_STEPPER_EN, STEPPER_DISABLED_LEVEL); }

static void airDir(bool push) {
    digitalWrite(PIN_AIR_DIR, push ? AIR_DIR_PUSH_LEVEL
                                   : (AIR_DIR_PUSH_LEVEL == HIGH ? LOW : HIGH));
    delayMicroseconds(10);
}

static void stepPulse() {
    digitalWrite(PIN_AIR_STEP, HIGH);
    delayMicroseconds(3);           // DRV8825 potrebuje >= 1,9 us
    digitalWrite(PIN_AIR_STEP, LOW);
}

// POZOR: musi ODEBRAT i znaky, ktere 'x' nejsou. Pouhy peek() by se zaseknul
// na prvnim cizim znaku v bufferu a nouzove zastaveni by bylo mrtve - typicky
// na '\n', ktery zbyde, kdyz Serial Monitor posila "Both NL & CR".
static bool sensorFrozen = false;   // nastavi logSample(), ukonci beh

static bool abortRequested() {
    bool abort = sensorFrozen;
    while (Serial.available() > 0) {
        char c = (char)Serial.read();
        if (c == 'x' || c == 'X') abort = true;
    }
    return abort;
}

// ============================================================
//  Log
// ============================================================
// faze: v=prejezd/ustaleni ventilu  k=klid pred tlacenim  w=tlaceni vzduchem
//       a=nasavani vzduchu do strikacky  e=ustaleni po prepnuti
//       d=klid po dojezdu (pacient uz uzavren, lahvicka jeste pod tlakem)
//       o=klid po odvzdusneni (atm. tlak)
//       r=predtlakovani (pacient uzavren)  p=otevreni pacienta + klid
//       s=zastaveno detekci (pacient uz uzavren, prejezd serva)
static void detectFeed(float c1, float c3);

// Hlidani zamrzleho senzoru: bitove stejna hodnota FROZEN_LIMIT x po sobe
// na kteremkoli kanalu ukonci beh (pres abortRequested()).
static void watchFrozen(float c1, float c2, float c3) {
    static float prev[N_CH];
    static uint8_t same[N_CH];
    float v[N_CH] = { c1, c2, c3 };
    for (uint8_t i = 0; i < N_CH; i++) {
        same[i] = (v[i] == prev[i]) ? same[i] + 1 : 0;
        prev[i] = v[i];
        if (same[i] >= FROZEN_LIMIT && !sensorFrozen) {
            sensorFrozen = true;
            Serial.print(F("# *** CHYBA: FDC1004 nemeri - kanal C"));
            Serial.print(i + 1);
            Serial.println(F(" se nemeni, beh zastaven ***"));
        }
    }
}
static float lastC1, lastC3;            // posledni zalogovany vzorek

static void logSample(char phase) {
    float c1 = readPf(0);
    float c2 = readPf(1);
    float c3 = readPf(2);
    watchFrozen(c1, c2, c3);
    lastC1 = c1; lastC3 = c3;
    detectFeed(c1, c3);
    Serial.print(cycleNo);
    Serial.print(';'); Serial.print(millis() - cycleT0);
    Serial.print(';'); Serial.print(phase);
    Serial.print(';'); Serial.print(pushedMl(), 3);
    Serial.print(';'); Serial.print(c1, 4);
    Serial.print(';'); Serial.print(c2, 4);
    Serial.print(';'); Serial.println(c3, 4);
}

static void printLogHeader() {
    Serial.println(F("# cyklus;t_ms;faze;vytlaceno_ml;C1;C2;C3"));
    Serial.println(F("# t_ms je relativni k zacatku cyklu; C1..C3 v pF, BEZ filtrace"));
    Serial.println(F("# C1 = spodni prstenec, C2 = svisla, C3 = horni prstenec"));
}

// ============================================================
//  Vzorkovani behem cekani a behem pohybu
// ============================================================
static bool sampleFor(uint32_t ms, char phase) {
    uint32_t startMs = millis();
    uint32_t lastMs = millis() - samplePeriodMs;
    while (millis() - startMs < ms) {
        if (abortRequested()) return false;
        if (detectHit()) return true;
        uint32_t now = millis();
        if (now - lastMs >= samplePeriodMs) {
            lastMs = now;
            logSample(phase);
        }
    }
    return true;
}

static bool patientValveTo(uint8_t angle) {
    if (angle == curPatAngle) return true;
    curPatAngle = angle;
    OCR1A = angleTicks(angle);
    return sampleFor(SERVO_SETTLE_MS, 'v');
}

static bool airValveTo(uint8_t angle) {
    if (angle == curAirAngle) return true;
    curAirAngle = angle;
    OCR1B = angleTicks(angle);
    return sampleFor(SERVO_SETTLE_MS, 'v');
}

// Pohyb vzduchove strikacky. push=true tlaci do lahvicky, false nasava
// z atmosfery. Vzorkuje a loguje po celou dobu pohybu.
static bool moveAir(float ml, bool push, char phase, uint8_t speedFactor) {
    int32_t stepsToGo = (int32_t)(ml * AIR_STEPS_PER_ML + 0.5f);
    if (stepsToGo <= 0) return true;
    if (push && airSteps - stepsToGo < 0) {
        Serial.println(F("# *** CHYBA: ve strikacce neni tolik vzduchu ***"));
        return false;
    }
    if (!push && airSteps + stepsToGo > (int32_t)(AIR_SYR_MAX_ML * AIR_STEPS_PER_ML)) {
        stepsToGo = (int32_t)(AIR_SYR_MAX_ML * AIR_STEPS_PER_ML) - airSteps;
        if (stepsToGo <= 0) return true;
    }

    uint32_t interval = pushIntervalUs();
    if (!push) interval /= speedFactor;

    airDir(push);
    int32_t done = 0;
    uint32_t lastStepUs = micros();
    uint32_t lastSampleMs = millis() - samplePeriodMs;

    while (done < stepsToGo) {
        if (abortRequested()) return false;
        if (detectHit()) return true;
        uint32_t nowUs = micros();
        if (nowUs - lastStepUs >= interval) {
            lastStepUs = nowUs;
            stepPulse();
            done++;
            if (push) { airSteps--; pushedSteps++; }
            else      { airSteps++; }
        }
        uint32_t nowMs = millis();
        if (nowMs - lastSampleMs >= samplePeriodMs) {
            lastSampleMs = nowMs;
            logSample(phase);
        }
    }
    return true;
}

// ============================================================
//  Doplneni vzduchu BEZ odvzdusneni lahvicky
// ============================================================
// Pacient ISOLATE -> S<->V -> S<->F (rameno lahvicky zaslepene, tlak
// zustava) -> nasat -> zpet S<->V -> pacient OPEN. Lahvicka je tak po
// celou dobu doplneni uzavrena z obou stran - zadny zbytkovy tlak
// nemuze mezitim tlacit kapalinu k pacientovi (viz komentar na
// zacatku souboru).
//
// Poradi zavirani/otevirani pacientskeho ventilu je schvalne stejne
// jako u planovaneho chovani pri detekci kriticke hladiny: pacient se
// zavira JAKO PRVNI (driv nez se cokoli jineho zmeni) a otevira se
// JAKO POSLEDNI (az kdyz uz je vzduchova strana zpet v poloze pro
// tlaceni).
static bool refillAir() {
    Serial.print(F("# doplneni vzduchu (pacient uzavren), vytlaceno "));
    Serial.print(pushedMl(), 2); Serial.println(F(" ml"));
    if (!patientValveTo(angPatIsolate)) return false;
    if (!airValveTo(angAirSyrFilt)) return false;
    if (!sampleFor(VALVE_SETTLE_MS, 'e')) return false;
    if (!moveAir(AIR_SYR_MAX_ML - airMl(), false, 'a', AIR_FILL_SPEED_FACTOR)) return false;
    if (!sampleFor(VALVE_SETTLE_MS, 'e')) return false;
    if (!airValveTo(angAirSyrVial)) return false;
    if (!patientValveTo(angPatOpen)) return false;
    return sampleFor(VALVE_SETTLE_MS, 'e');
}

// Predtlakovaci objem: plyn v lahvicce + vzduch ve strikacce + mrtvy
// objem, krat pomer nejhorsiho tlaku v hadicce k (nizkemu) tlaku okoli.
// Napln se v testu 5 schvalne nezadava, takze se pocita s prazdnou
// lahvickou - nejvic plynu, konzervativni smer.
static float precompMl() {
    if (precompManualMl >= 0.0f) return precompManualMl;
    float gasMl = VIAL_BRIM_ML + airMl() + AIR_DEAD_ML;
    float dpKpa = (BACKPRESS_PATIENT_CM + BACKPRESS_VENOUS_CM) * KPA_PER_CMH2O;
    return gasMl * dpKpa / P_ATM_MIN_KPA;
}

// Pacient uzavren, lahvicka na S<->V: vytlacit V_pre a nechat ustalit.
// Zapocitava se do vytlaceneho objemu (osa 'vytlaceno_ml' zacina tady).
static bool precompress(float ml) {
    Serial.print(F("# predtlakovani ")); Serial.print(ml, 2);
    Serial.println(F(" ml (pacient uzavren)"));
    if (!moveAir(ml, true, 'r', 1)) return false;
    return sampleFor(KLID_PRECOMP_MS, 'r');
}

// Otevreni pacienta a klid pred tlacenim. Vse vcetne prejezdu serva se
// loguje jako 'p' - skok hladiny pri otevreni je hlavni veliciny testu.
static bool openPatientAndHold() {
    Serial.println(F("# otevreni pacienta, klid (p)"));
    curPatAngle = angPatOpen;
    OCR1A = angleTicks(angPatOpen);
    return sampleFor(KLID_OPEN_MS, 'p');
}

// Doplneni strikacky na 10 ml na konci cyklu, az po mereni. Bez nej by
// kazdy dalsi cyklus zacinal s tim, co zbylo, a doplnovalo by se navic
// hned na zacatku i jinde nez v prvnim cyklu (prvni cyklus a ostatni by
// nebyly srovnatelne). Pacient je uz izolovan; po nasati se vzduch vrati
// do V<->F, aby se lahvicka dala znovu naplnit.
static bool topUpAir() {
    if (airMl() >= AIR_SYR_MAX_ML - 0.001f) return true;
    Serial.println(F("# doplneni strikacky na 10 ml pro dalsi cyklus"));
    if (!airValveTo(angAirSyrFilt)) return false;
    if (!moveAir(AIR_SYR_MAX_ML - airMl(), false, 'a', AIR_FILL_SPEED_FACTOR)) return false;
    return airValveTo(angAirVialFilt);
}

// ============================================================
//  Potvrzeni obsluhou
// ============================================================
// Prijme libovolny text (funguje pri kterekoli volbe zakonceni radku
// v Serial Monitoru) a zapise ho do logu - obsluha tak muze rovnou
// poslat treba naplneny objem. 'x' znamena zruseni behu.
static bool waitConfirm(const __FlashStringHelper *prompt) {
    Serial.print(F(">>> "));
    Serial.print(prompt);
    Serial.println(F("  (odeslat cokoliv = pokracovat, 'x' = konec)"));
    delay(200);                                     // dobehnuti zbytku predchoziho radku
    while (Serial.available() > 0) Serial.read();
    while (Serial.available() == 0) { /* cekani na obsluhu */ }
    delay(50);                                      // dobehnuti celeho radku
    char c = (char)Serial.peek();
    Serial.print(F("# obsluha: "));
    uint8_t len = 0;
    while (Serial.available() > 0) {
        char ch = (char)Serial.read();
        if (ch == '\n' || ch == '\r') continue;
        Serial.print(ch);
        if (len < sizeof(opText) - 1) opText[len++] = ch;
    }
    opText[len] = '\0';
    Serial.println();
    if (c == 'x' || c == 'X') {
        Serial.println(F("# ABORT - beh zastaven obsluhou"));
        return false;
    }
    return true;
}

// ============================================================

// ============================================================
//  Detekce
// ============================================================

static DetMode detMode = DET_OFF;
static DetHit  detHitKind = HIT_NONE;
static float   b1[DET_MED_N], b3[DET_MED_N];
static uint8_t bIdx = 0, bCount = 0;
static float   hist3[DET_HIST_LEN];
static uint8_t histCount = 0;
static float   histNextMl = 0.0f;
static float   c1Peak, c3Peak, slopeMax;
static float   c1Base;                 // zaklad pojistky: C1 z 'k' (uceni), F3 (dale)
static float   c1Rest, c3Rest, c1Sigma, c3Sigma;   // statistika klidu 'k'
static float   learnedF3 = 0.0f, learnedD1 = 0.0f;
static bool    learned = false;
static bool    c3Fell = false;         // uceni: C3 uz vystoupalo a spadlo (odkryto)
static float   hitM1, hitM3, hitDelta;

static bool detectHit() { return detHitKind != HIT_NONE; }

static float median(const float *src) {
    float v[DET_MED_N];
    for (uint8_t i = 0; i < DET_MED_N; i++) {
        float x = src[i];
        uint8_t j = i;
        while (j > 0 && v[j - 1] > x) { v[j] = v[j - 1]; j--; }
        v[j] = x;
    }
    return 0.5f * (v[DET_MED_N / 2 - 1] + v[DET_MED_N / 2]);
}

// Vyprazdni median i historii strmosti - po kazdem preruseni toku
// (doplneni vzduchu), aby skok ventilu nevstoupil do strmosti.
static void detectResetBuffers() {
    bIdx = 0; bCount = 0; histCount = 0;
    histNextMl = pushedMl();
}

static void detectStart(uint8_t mode) {
    detectResetBuffers();
    c1Peak = -1e9f; c3Peak = -1e9f; slopeMax = 0.0f;
    c1Base = (mode == DET_LEARN) ? c1Rest : learnedF3;
    c3Fell = false;
    detHitKind = HIT_NONE;
    detMode = (DetMode)mode;
}

// Okamzita reakce: pacient se zavira jeste uvnitr vzorkovani, motor stoji,
// protoze moveAir()/sampleFor() se po detectHit() hned vrati.
static void detectFire(uint8_t kind, float m1, float m3, float delta) {
    curPatAngle = angPatIsolate;
    OCR1A = angleTicks(angPatIsolate);
    detHitKind = (DetHit)kind;
    detMode = DET_OFF;
    hitM1 = m1; hitM3 = m3; hitDelta = delta;
}

// Pojistka C1, dve nezavisle podminky:
//  1) posledni zachrana (vzdy aktivni): C1 kleslo pod svuj klid 'k'
//     o vic nez 5 sumovych odchylek - mezi prstenci i nad nimi je C1
//     vzdy nad klidem, pod nim je jen po odkryti
//  2) C1 uz bylo v cyklu vysoko a spadlo pod polovinu vzestupu. V uceni
//     az po odkryti C3 (hladina muze projit C1 jen po C3) a se vzestupem
//     nad sumem, pozdeji se vzestupem nad polovinou D1.
static bool guardTripped(float m1) {
    if (m1 < c1Rest - DET_RISE_SIGMA * c1Sigma) return true;
    float rise = c1Peak - c1Base;
    if (detMode == DET_LEARN) {
        if (!c3Fell || rise < DET_RISE_SIGMA * c1Sigma) return false;
    } else if (rise < 0.5f * learnedD1) {
        return false;
    }
    return (m1 - c1Base) < DET_GUARD_FRAC * rise;
}

// Uceni: strmost poklesu C3 pres posledni 1 ml; konec poklesu, kdyz C3
// nejdriv vystoupalo nad klid (> 5 sumovych odchylek), pak spadlo od
// maxima o vic nez polovinu tohoto vzestupu a strmost klesla pod 0,3 x
// nejvetsi strmost tohoto poklesu. Pokles se meri vzestupem, ne sumem -
// pri vyssim sumu (test 5, 1. beh: sd 0,14 pF) by 30 sigma bylo vic nez
// cely signal.
static bool learnPlateau(float m3) {
    float ml = pushedMl();
    if (ml < histNextMl) return false;
    histNextMl = ml + DET_HIST_STEP_ML;
    for (uint8_t i = 0; i < DET_HIST_LEN - 1; i++) hist3[i] = hist3[i + 1];
    hist3[DET_HIST_LEN - 1] = m3;
    if (histCount < DET_HIST_LEN) { histCount++; return false; }
    float slope = (hist3[0] - hist3[DET_HIST_LEN - 1])
                / (DET_HIST_STEP_ML * (DET_HIST_LEN - 1));
    if (slope > slopeMax) slopeMax = slope;
    float rise = c3Peak - c3Rest;
    c3Fell = rise > DET_RISE_SIGMA * c3Sigma
          && (c3Peak - m3) > DET_FALL_FRAC * rise;
    return c3Fell && slope < DET_PLATEAU_FRAC * slopeMax;
}

static void detectFeed(float c1, float c3) {
    if (detMode == DET_OFF) return;
    b1[bIdx] = c1; b3[bIdx] = c3;
    bIdx = (bIdx + 1) % DET_MED_N;
    if (bCount < DET_MED_N) { bCount++; return; }
    float m1 = median(b1), m3 = median(b3);
    if (m1 > c1Peak) c1Peak = m1;
    if (m3 > c3Peak) c3Peak = m3;

    if (guardTripped(m1)) { detectFire(HIT_GUARD, m1, m3, 0.0f); return; }

    if (detMode == DET_LEARN) {
        if (learnPlateau(m3)) detectFire(HIT_STOP, m1, m3, 1.0f);
        return;
    }
    float denom = m1 - learnedF3;
    if (denom < DET_DENOM_FRAC * learnedD1) return;
    float delta = (m1 - m3) / denom;
    if (delta >= deltaStop) detectFire(HIT_STOP, m1, m3, delta);
}

// ============================================================
//  Klid 'k' se statistikou (zaklad a sum pro relativni prahy)
// ============================================================
static bool restWithStats() {
    float s1 = 0, q1 = 0, s3 = 0, q3 = 0;
    uint16_t n = 0;
    uint32_t startMs = millis();
    uint32_t lastMs = millis() - samplePeriodMs;
    while (millis() - startMs < KLID_PRE_MS) {
        if (abortRequested()) return false;
        uint32_t now = millis();
        if (now - lastMs < samplePeriodMs) continue;
        lastMs = now;
        logSample('k');
        float c1 = lastC1, c3 = lastC3;
        s1 += c1; q1 += c1 * c1; s3 += c3; q3 += c3 * c3; n++;
    }
    c1Rest = s1 / n; c3Rest = s3 / n;
    c1Sigma = sqrt(max(q1 / n - c1Rest * c1Rest, 0.0f));
    c3Sigma = sqrt(max(q3 / n - c3Rest * c3Rest, 0.0f));
    Serial.print(F("# klid k: C1=")); Serial.print(c1Rest, 3);
    Serial.print(F(" sd=")); Serial.print(c1Sigma, 3);
    Serial.print(F("  C3=")); Serial.print(c3Rest, 3);
    Serial.print(F(" sd=")); Serial.println(c3Sigma, 3);
    return true;
}

// ============================================================
//  Tlaceni az do detekce
// ============================================================
// Vraci false pri abortu nebo vycerpani doplneni. Detekce bezi uz od
// otevreni pacienta (faze 'p'), protoze prebytek predtlaku tece hned.
static bool pushUntilDetect() {
    if (!openPatientAndHold()) return false;
    uint8_t refills = 0;
    while (!detectHit()) {
        if (airMl() < 0.01f) {
            if (refills >= MAX_REFILLS) {
                Serial.println(F("# *** ALARM: vycerpana doplneni bez detekce ***"));
                return false;
            }
            refills++;
            DetMode m = detMode;
            detMode = DET_OFF;                 // skoky ventilu do detekce nepatri
            if (!refillAir()) return false;
            detectResetBuffers();
            detMode = m;
        }
        if (!moveAir(airMl(), true, 'w', 1)) return false;
    }
    return true;
}

static void printHit() {
    Serial.print(detHitKind == HIT_GUARD ? F("# *** POJISTKA C1") : F("# STOP"));
    Serial.print(F(" @ ")); Serial.print(pushedMl(), 2);
    Serial.print(F(" ml  C1=")); Serial.print(hitM1, 3);
    Serial.print(F(" C3=")); Serial.print(hitM3, 3);
    Serial.print(F(" delta=")); Serial.println(hitDelta, 3);
}

// ============================================================
//  Jeden cyklus
// ============================================================
static bool runCycle(uint8_t n) {
    cycleNo = n;
    Serial.print(F("# ===== CYKLUS ")); Serial.print(n);
    Serial.print('/'); Serial.print(cycleCount); Serial.println(F(" ====="));

    bool learnCycle = !learned;
    if (learnCycle) {
        if (!waitConfirm(F("naplnit lahvicku (napr. 10 ml), potvrdit"))) return false;
    } else {
        if (!waitConfirm(F("pridat roztok OD OKA (pokazde jinak), potvrdit"))) return false;
    }

    steppersEnable();
    pushedSteps = 0;
    cycleT0 = millis();

    // Lahvicka uzavrena (S<->V), pacient zustava ISOLATE.
    if (!airValveTo(angAirSyrVial)) return false;
    if (!sampleFor(VALVE_SETTLE_MS, 'v')) return false;
    if (!restWithStats()) return false;
    if (!precompress(precompMl())) return false;

    detectStart(learnCycle ? DET_LEARN : DET_STOP);
    bool ok = pushUntilDetect();
    detMode = DET_OFF;
    if (!ok) return false;
    printHit();
    if (!sampleFor(SERVO_SETTLE_MS, 's')) return false;   // pacient uz zavren
    bool guard = (detHitKind == HIT_GUARD);
    detHitKind = HIT_NONE;

    if (learnCycle && !guard) {
        learnedF3 = hitM3;
        learnedD1 = hitM1 - hitM3;
        learned = true;
        Serial.print(F("# NAUCENO F3=")); Serial.print(learnedF3, 3);
        Serial.print(F(" D1=")); Serial.println(learnedD1, 3);
    }

    Serial.println(F("# klid pod tlakem (d), pak odvzdusneni a klid (o)"));
    if (!sampleFor(KLID_POST_MS, 'd')) return false;
    if (!airValveTo(angAirVialFilt)) return false;
    if (!sampleFor(KLID_REST_MS, 'o')) return false;
    if (!topUpAir()) return false;

    Serial.print(F("# cyklus ")); Serial.print(n);
    Serial.print(F(" hotov, vytlaceno ")); Serial.print(pushedMl(), 2);
    Serial.println(F(" ml vzduchu"));

    if (!waitConfirm(F("kde je hladina? (nad C3 / mezi / pod C1), poslat"))) return false;
    if (guard) {
        Serial.println(F("# serie ukoncena pojistkou C1"));
        return false;
    }
    return true;
}

// ============================================================
//  Cela serie
// ============================================================
static void runSeries() {
    running = true;
    learned = false;
    printLogHeader();

    // Lahvicka se plni VZDY v teto poloze: pacient uzavren, V<->F otevreno.
    curPatAngle = angPatIsolate;  OCR1A = angleTicks(angPatIsolate);
    curAirAngle = angAirVialFilt; OCR1B = angleTicks(angAirVialFilt);
    delay(SERVO_SETTLE_MS);
    Serial.println(F("# ventily do polohy pro plneni (pacient ISOLATE, vzduch V-F)"));

    bool ok = true;
    for (uint8_t n = 1; ok && n <= cycleCount; n++) {
        ok = runCycle(n);
    }

    detMode = DET_OFF;
    curPatAngle = angPatIsolate;  OCR1A = angleTicks(angPatIsolate);
    curAirAngle = angAirVialFilt; OCR1B = angleTicks(angAirVialFilt);
    delay(SERVO_SETTLE_MS);
    steppersDisable();
    Serial.println(F("# ventily do bezpecnych poloh, drivery DISABLED"));
    Serial.println(ok ? F("# === SERIE HOTOVA ===") : F("# === SERIE PRERUSENA ==="));
    running = false;
}

// ============================================================
//  Diagnostika a prikazy
// ============================================================
static void printInfo() {
    Serial.print(F("# CAPDAC1=")); Serial.print(capdac[0]);
    Serial.print(F(" CAPDAC2=")); Serial.print(capdac[1]);
    Serial.print(F(" CAPDAC3=")); Serial.println(capdac[2]);
    Serial.print(F("# cykly=")); Serial.print(cycleCount);
    Serial.print(F("  rychlost=1 ml/")); Serial.print(flowSecPerMl);
    Serial.print(F(" s  delta stop=")); Serial.println(deltaStop, 2);
    Serial.print(F("# predtlak: "));
    if (precompManualMl >= 0.0f) {
        Serial.print(F("rucne ")); Serial.print(precompManualMl, 2);
    } else {
        Serial.print(F("vypocet, s plnou strikackou ")); Serial.print(
            (VIAL_BRIM_ML + AIR_SYR_MAX_ML + AIR_DEAD_ML)
            * (BACKPRESS_PATIENT_CM + BACKPRESS_VENOUS_CM) * KPA_PER_CMH2O
            / P_ATM_MIN_KPA, 2);
    }
    Serial.println(F(" ml"));
    Serial.print(F("# uhly: pacient OPEN=")); Serial.print(angPatOpen);
    Serial.print(F(" ISOLATE=")); Serial.print(angPatIsolate);
    Serial.print(F(" | vzduch S-V=")); Serial.print(angAirSyrVial);
    Serial.print(F(" S-F=")); Serial.print(angAirSyrFilt);
    Serial.print(F(" V-F=")); Serial.println(angAirVialFilt);
    Serial.print(F("# vzduch v strikacce=")); Serial.print(airMl(), 2);
    Serial.print(F(" ml  tared=")); Serial.println(tared ? F("ano") : F("ne"));
}

static void printHelp() {
    Serial.println(F("# h=napoveda i=info a=autoCAPDAC f=kontrola FDC1004 m=monitor"));
    Serial.println(F("# t=deklarace plne vzduchove strikacky (10 ml)"));
    Serial.println(F("# g=spustit serii   x=NOUZOVE ZASTAVENI"));
    Serial.println(F("# j/k=jog vzduch +-0.5 ml (odvzdusneni hadicky)"));
    Serial.println(F("# u0=pacient IZOLACE u1=pacient OTEVRENO"));
    Serial.println(F("# u2=vzduch S-V u3=vzduch S-F u4=vzduch V-F"));
    Serial.println(F("# c<n>=pocet cyklu  s<s>=sekund na 1 ml  d<x>=prah delta"));
    Serial.println(F("# r<ml>=predtlak rucne  r=predtlak vypoctem  #<text>=znacka"));
}

static void jogAir(bool push) {
    steppersEnable();
    airDir(push);
    uint32_t interval = pushIntervalUs();
    int32_t steps = (int32_t)(JOG_ML * AIR_STEPS_PER_ML);
    for (int32_t i = 0; i < steps; i++) {
        stepPulse();
        delayMicroseconds(interval - 3);
        airSteps += push ? -1 : 1;
    }
    Serial.print(F("# vzduch v strikacce=")); Serial.println(airMl(), 2);
}

static void setValveCmd(char which) {
    uint8_t ang;
    switch (which) {
        case '0': ang = angPatIsolate;  curPatAngle = ang; OCR1A = angleTicks(ang); break;
        case '1': ang = angPatOpen;     curPatAngle = ang; OCR1A = angleTicks(ang); break;
        case '2': ang = angAirSyrVial;  curAirAngle = ang; OCR1B = angleTicks(ang); break;
        case '3': ang = angAirSyrFilt;  curAirAngle = ang; OCR1B = angleTicks(ang); break;
        case '4': ang = angAirVialFilt; curAirAngle = ang; OCR1B = angleTicks(ang); break;
        default: Serial.println(F("# u0..u4")); return;
    }
    Serial.print(F("# ventil -> ")); Serial.println(ang);
}

// Prubezny vypis C1..C3 (median 10 vzorku) do stisku libovolne klavesy.
// Slouzi k identifikaci elektrod: pri pomalem plneni prazdne lahvicky
// musi zareagovat nejdriv C1 (spodni), pak C3 (horni).
static void monitor() {
    Serial.println(F("# monitor C1;C2;C3 [pF], libovolny znak = konec"));
    delay(200);
    while (Serial.available() > 0) Serial.read();
    while (Serial.available() == 0) {
        float v[N_CH][DET_MED_N];
        for (uint8_t k = 0; k < DET_MED_N; k++) {
            for (uint8_t i = 0; i < N_CH; i++) v[i][k] = readPf(i);
            delay(MONITOR_PERIOD_MS / DET_MED_N);
        }
        Serial.print(F("m;"));
        Serial.print(median(v[0]), 3); Serial.print(';');
        Serial.print(median(v[1]), 3); Serial.print(';');
        Serial.println(median(v[2]), 3);
    }
    while (Serial.available() > 0) Serial.read();
}

static void handleLine() {
    if (lineLen == 0) return;
    switch (line[0]) {
        case 'h': printHelp(); break;
        case 'i': printInfo(); break;
        case 'f': checkFdc(); break;
        case 'm': monitor(); break;
        case 'a':
            for (uint8_t i = 0; i < N_CH; i++) autoCapdac(i);
            printInfo();
            break;
        case 't':
            airSteps = (int32_t)(AIR_SYR_MAX_ML * AIR_STEPS_PER_ML);
            tared = true;
            Serial.println(F("# vychozi stav: vzduchova strikacka plna (10 ml)"));
            break;
        case 'x':
            steppersDisable();
            Serial.println(F("# drivery DISABLED"));
            break;
        case 'j': jogAir(true); break;
        case 'k': jogAir(false); break;
        case 'u': setValveCmd(line[1]); break;
        case 'c': {
            int v = atoi(&line[1]);
            if (v >= 1 && v <= 50) { cycleCount = (uint8_t)v; printInfo(); }
            break;
        }
        case 's': {
            int v = atoi(&line[1]);
            if (v >= 1 && v <= 60) { flowSecPerMl = (uint8_t)v; printInfo(); }
            break;
        }
        case 'd': {
            float v = atof(&line[1]);
            if (v > 0.3f && v < 1.0f) { deltaStop = v; printInfo(); }
            else Serial.println(F("# CHYBA: rozsah 0.3..1.0"));
            break;
        }
        case 'r': {
            if (line[1] == '\0') { precompManualMl = -1.0f; printInfo(); break; }
            float v = atof(&line[1]);
            if (v >= 0.0f && v < AIR_SYR_MAX_ML) { precompManualMl = v; printInfo(); }
            break;
        }
        case 'g':
            if (running) { Serial.println(F("# CHYBA: beh uz probiha")); break; }
            if (!tared) { Serial.println(F("# CHYBA: neprovedeno 't'")); break; }
            if (!checkFdc()) break;
            runSeries();
            break;
        case '#': break;
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

// Kontrola FDC1004: vypis konfigurace (zpetne cteni, vc. DONE bitu 3:0)
// a zda se vysledky vsech kanalu meni. Vraci false, pokud senzor nemeri.
static bool checkFdc() {
    delay(FDC_CHECK_PERIOD_MS);       // DONE bity se ctenim vysledku nuluji
    uint16_t conf = readReg(REG_FDC_CONF);
    Serial.print(F("# FDC_CONF=0x")); Serial.print(conf, HEX);
    for (uint8_t i = 0; i < N_CH; i++) {
        Serial.print(F(" CONF_MEAS")); Serial.print(i + 1);
        Serial.print(F("=0x")); Serial.print(readReg(REG_CONF_MEAS1 + i), HEX);
    }
    Serial.println();
    int32_t first[N_CH];
    bool changed[N_CH] = { false, false, false };
    for (uint8_t i = 0; i < N_CH; i++) first[i] = readRaw(i);
    for (uint8_t k = 0; k < FDC_CHECK_SAMPLES; k++) {
        delay(FDC_CHECK_PERIOD_MS);
        for (uint8_t i = 0; i < N_CH; i++) {
            if (readRaw(i) != first[i]) changed[i] = true;
        }
    }
    bool ok = true;
    for (uint8_t i = 0; i < N_CH; i++) ok = ok && changed[i];
    Serial.println(ok ? F("# FDC1004 meri OK")
                      : F("# *** CHYBA: FDC1004 nemeri (vypnout a zapnout napajeni) ***"));
    sensorFrozen = !ok;
    return ok;
}

void setup() {
    Serial.begin(BAUD);
    Wire.begin();
    Wire.setClock(100000UL);
    delay(100);

    Serial.println(F("# TEST 5 - zastaveni hladiny mezi prstenci (naslepo)"));
    Serial.println(F("# kanaly: C1 spodni prstenec, C2 svisla, C3 horni prstenec"));

    pinMode(PIN_STEPPER_EN, OUTPUT);
    digitalWrite(PIN_STEPPER_EN, STEPPER_DISABLED_LEVEL);
    pinMode(PIN_AIR_STEP, OUTPUT);
    pinMode(PIN_AIR_DIR, OUTPUT);
    digitalWrite(PIN_AIR_STEP, LOW);

    loadValveAngles();
    // Ventily ihned do pracovnich poloh - servo 0 stupnu NENI bezpecna
    // izolacni poloha (viz CLAUDE.md).
    curPatAngle = angPatIsolate;
    curAirAngle = angAirSyrFilt;
    servoTimerInit();
    servoAttach();

    uint16_t manuf = readReg(REG_MANUF_ID);
    uint16_t dev = readReg(REG_DEVICE_ID);
    Serial.print(F("# MANUFACTURER_ID=0x")); Serial.print(manuf, HEX);
    Serial.print(F(" DEVICE_ID=0x")); Serial.println(dev, HEX);
    if (manuf != MANUF_ID_VAL || dev != DEVICE_ID_VAL) {
        Serial.println(F("# CHYBA: cip neodpovida (ocekavano 0x5449 / 0x1004)"));
    }

    applyConfig();
    for (uint8_t i = 0; i < N_CH; i++) autoCapdac(i);
    checkFdc();

    printInfo();
    printHelp();
    Serial.println(F("# POSTUP: vzduchova strikacka PLNA, penicilinka ve studni,"));
    Serial.println(F("#         't' vychozi stav, pak 'g' start"));
}

void loop() {
    pollSerial();
}
