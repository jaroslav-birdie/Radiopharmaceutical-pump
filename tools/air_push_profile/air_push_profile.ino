// ============================================================
//  TEST 1 (nova serie) – PROFIL SIGNALU PRI VYTLACOVANI VZDUCHEM
//  TEST 3 – totez se DVEMA PRSTENCI (CIN1 spodni, CIN3 horni)
//  TEST 4 – 10 ml bez doplneni, PREDTLAKOVANI pred otevrenim pacienta,
//           pacient se zavira IHNED po dojezdu (viz TEST4.md)
//
//  Otazka testu 1: existuje pri vytlacovani VZDUCHEM na CIN1
//  rozpoznatelny prechod pres kritickou hladinu, a jak je velky
//  proti tlakovemu artefaktu?
//
//  Otazka testu 3: pomohl druhy prstenec? Tj. je rozdil C1 - C3
//  ostrejsi a mene zatizeny tlakem nez samotne C1 (kriteria viz
//  README.md). Loguji se vsechny tri kanaly, rozdil se pocita az
//  pri vyhodnoceni.
//
//  Otazka testu 4: jak spolehlive poznat, ze je hladina PRAVE MEZI
//  prstenci (C3 odkryty, C1 zakryty)? A je artefakt pacientske cesty
//  hydraulicky (zpetny tok, neustaleny tlak), nebo elektricky
//  (zpatecni cesta pres sloupec kapaliny)? To rozlisi skok pri otevreni
//  pacienta po predtlakovani (faze 'p'): hladina smi jen klesnout,
//  skok nahoru je elektricky.
//
//  Tenhle sketch NIC NEDETEKUJE. Zadny klouzavy prumer, zadny prah,
//  zadna ochrana proti ruseni, zadne rozhodovani. Jen syrova data.
//  Vyhlazeni, prah i vzorkovaci frekvence se vyberou az nad namerenymi
//  daty - proto se vzorkuje hustotou, ktera je nad predpokladanou
//  potrebou, a loguje se bez jakekoli filtrace.
//
//  ---------------------------------------------------------------
//  PRUBEH JEDNOHO CYKLU (5x dokola, polo-automaticky)
//  ---------------------------------------------------------------
//    1. obsluha naplni penicilinku NAD 10 ml a potvrdi (odeslany
//       text, napr. naplneny objem, se zapise do logu)
//    2. ventily do polohy pro vytlacovani (vzduch S<->V, pacient OPEN)
//    3. KLID_PRE_MS klidu pri atmosferickem tlaku – referencni bod
//    4. protlaci se TOTAL_AIR_ML vzduchu rychlosti 1 ml / FLOW_S_PER_ML
//    5. KLID_POST_MS klidu – jak se signal usadi po dojezdu
//    6. vzduchova strikacka se doplni na 10 ml (pripravena na dalsi cyklus)
//    7. obsluha potvrdi, ze je lahvicka prazdna
//
//  20 ml vzduchu je zamerne PREBYTEK proti ~10 ml kapaliny. Cilem je
//  videt i konec: jak vypada signal, kdyz uz lahvickou prochazi jen
//  vzduch. Bez toho by se nedalo poznat, ktera cast krivky je jeste
//  hladina a ktera uz ne.
//
//  ---------------------------------------------------------------
//  PROC SE LAHVICKA PRI DOPLNENI VZDUCHU NEODVZDUSNUJE
//  ---------------------------------------------------------------
//  20 ml se do 10ml strikacky nevejde, takze se tlaceni prerusi
//  doplnenim. Firmware pri nem lahvicku odvzdusni (V<->F), ale tady
//  se jde primo S<->V -> S<->F -> S<->V: pri poloze S<->F je rameno
//  lahvicky zaslepene, takze lahvicka si tlak DRZI a preruseni nedela
//  do dat tlakovy skok.
//
//  Pacientsky ventil se pri doplneni ZAVIRA (na dobu celeho refillAir())
//  a zase OTVIRA az pred navratem k tlaceni - lahvicka je tak behem
//  doplneni uzavrena z OBOU stran (vzduch i pacient), zadny zbytkovy
//  tlak nema kudy dal tlacit kapalinu k pacientovi. Bez toho by
//  kapalina i behem doplnovani dal dotekava - presne to byl efekt
//  namereny v prvnim behu testu 1 (pokles C1 ve fazi 'a').
//
//  Je to zamerna odchylka od firmwaru jen v tom, ze se doplneni resi
//  primo S<->V -> S<->F -> S<->V misto pres V<->F: cilem experimentu
//  je videt tvar krivky s co nejmene artefakty, ne verne imitovat
//  pristroj. Chovani pacientskeho ventilu (zavreno mimo aktivni
//  tlaceni) uz s firmwarem shodne je.
//
//  ---------------------------------------------------------------
//  POSTUP
//  ---------------------------------------------------------------
//    1. Vzduchova strikacka PLNA (10 ml), ventily i strikacka osazene.
//    2. Penicilinka do studny.
//    3. 't' = deklarace, ze je vzduchova strikacka plna.
//    4. 'a' = autokalibrace CAPDAC (volitelne, dela se i v setup()).
//    5. 'g' = spustit serii. Dal uz jen potvrzovani mezi cykly.
//    6. 'x' kdykoliv = okamzite zastaveni.
//
//  Serial Monitor: 115200 Bd (tri kanaly se do 9600 Bd nevejdou).
//  Log si zachyt do souboru.
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
#define KLID_POST_MS    15000UL   // klid po dojezdu, jeste pod tlakem
#define KLID_VENT_MS     8000UL   // klid po odvzdusneni - zmeri tlakovy offset
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

// ---------- parametry behu (menitelne pres Serial) ----------
static uint16_t samplePeriodMs = 50;    // 20 vzorku/s
static uint8_t  cycleCount     = 5;     // pocet opakovani
static float    totalAirMl     = 10.0f; // vzduch na jeden cyklus (test 4: cela
                                        // strikacka, bez doplneni)
static float    firstAirMl     = 10.0f; // prvni davka cyklu - posouva doplneni
                                        // (test 3: f5; test 4: f10 = bez doplneni)
static float    loadAirMl      = 10.0f; // dalsi davky (cela strikacka, pist dojizdi
                                        // az na dno)
static uint8_t  flowSecPerMl   = 5;     // rychlost tlaceni
static float    precompManualMl = -1.0f; // <0 = vypocet z naplne, jinak rucne (r<ml>)

// ---------- uhly ventilu ----------
static uint8_t angPatOpen, angPatIsolate;
static uint8_t angAirSyrVial, angAirSyrFilt, angAirVialFilt;
static uint8_t curPatAngle = 255, curAirAngle = 255;

// ---------- stav ----------
static uint8_t  capdac[N_CH] = { 0, 0, 0 };
static bool     tared        = false;
static bool     running      = false;
static char     line[20];
static char     opText[12];          // posledni odpoved obsluhy (naplneny objem)
static uint8_t  lineLen      = 0;
static int32_t  airSteps     = 0;   // obsah vzduchove strikacky v krocich
static int32_t  pushedSteps  = 0;   // kumulativne vytlaceno v tomto cyklu
static uint32_t cycleT0      = 0;   // zacatek cyklu (t_ms je relativni)
static uint8_t  cycleNo      = 0;

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
static bool abortRequested() {
    bool abort = false;
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
static void logSample(char phase) {
    float c1 = readPf(0);
    float c2 = readPf(1);
    float c3 = readPf(2);
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

// Predtlakovaci objem pro aktualni stav: plyn v lahvicce (po okraj minus
// zadana napln) + vzduch ve strikacce + mrtvy objem, krat pomer nejhorsiho
// tlaku v hadicce k (nizkemu) tlaku okoli. Nezadana napln = 0 ml, tj.
// nejvic plynu - konzervativni smer.
static float precompMl(float fillMl) {
    if (precompManualMl >= 0.0f) return precompManualMl;
    if (fillMl < 0.0f || fillMl > VIAL_BRIM_ML) fillMl = 0.0f;
    float gasMl = VIAL_BRIM_ML - fillMl + airMl() + AIR_DEAD_ML;
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
//  Jeden cyklus
// ============================================================
static bool runCycle(uint8_t n) {
    cycleNo = n;
    Serial.print(F("# ===== CYKLUS ")); Serial.print(n);
    Serial.print('/'); Serial.print(cycleCount); Serial.println(F(" ====="));

    if (!waitConfirm(F("naplnit tesne NAD horni hranu CIN3, poslat objem v ml"))) return false;
    float fillMl = (opText[0] != '\0') ? (float)atof(opText) : -1.0f;

    steppersEnable();
    pushedSteps = 0;
    cycleT0 = millis();

    // Lahvicka se uzavre (S<->V), pacient zustava ISOLATE. Klid 'k' je
    // tak referencni bod pri atm. tlaku BEZ spojeni s hadickou.
    if (!airValveTo(angAirSyrVial)) return false;
    if (!sampleFor(VALVE_SETTLE_MS, 'v')) return false;

    Serial.println(F("# klid pred tlacenim (referencni bod, atm. tlak)"));
    if (!sampleFor(KLID_PRE_MS, 'k')) return false;

    // 1. cyklus bez predtlakovani (ukaze, jestli zpetny tok v tomhle
    // usporadani vubec nastava), dalsi s nim.
    if (n == 1) {
        Serial.println(F("# 1. cyklus: BEZ predtlakovani"));
    } else if (!precompress(precompMl(fillMl))) {
        return false;
    }
    if (!openPatientAndHold()) return false;

    Serial.print(F("# tlaceni ")); Serial.print(totalAirMl, 1);
    Serial.print(F(" ml vzduchu, 1 ml / ")); Serial.print(flowSecPerMl);
    Serial.println(F(" s"));

    while (pushedMl() < totalAirMl - 0.001f) {
        float zbyva = totalAirMl - pushedMl();
        float davka = (pushedSteps == 0) ? firstAirMl : loadAirMl;
        float chunk = (zbyva < davka) ? zbyva : davka;
        if (chunk > airMl()) {                      // ve strikacce uz neni dost
            if (!refillAir()) return false;
        }
        if (!moveAir(chunk, true, 'w', 1)) return false;
    }

    // Pacient se zavira IHNED po zastaveni motoru, jeste pod tlakem -
    // stejne poradi jako pri detekci kriticke hladiny v pristroji. Zbytkovy
    // pretlak tak nema kudy tlacit kapalinu dal (zadny dobeh) a faze 'd'
    // ukazuje cisty vliv tlaku na signal pri stojici hladine.
    Serial.println(F("# pacient uzavren, klid pod tlakem"));
    if (!patientValveTo(angPatIsolate)) return false;
    if (!sampleFor(KLID_POST_MS, 'd')) return false;

    // Odvzdusneni AZ po uzavreni pacienta. Rozdil mezi poslednim vzorkem
    // 'd' (pod tlakem) a ustalenym 'o' (atmosfericky) je TLAKOVY OFFSET -
    // to, co behem tlaceni pricita tlak k signalu hladiny.
    Serial.println(F("# odvzdusneni + klid (mereni tlakoveho offsetu)"));
    if (!airValveTo(angAirVialFilt)) return false;
    if (!sampleFor(KLID_VENT_MS, 'o')) return false;

    if (!topUpAir()) return false;

    Serial.print(F("# cyklus ")); Serial.print(n);
    Serial.print(F(" hotov, vytlaceno ")); Serial.print(pushedMl(), 2);
    Serial.println(F(" ml vzduchu"));

    return waitConfirm(F("kde je hladina? (nad C3 / mezi / pod C1), poslat"));
}

// ============================================================
//  Cela serie
// ============================================================
static void runSeries() {
    running = true;
    printLogHeader();

    // Lahvicka se plni VZDY v teto poloze: pacient uzavren, V<->F otevreno
    // do atmosfery. Jinak by vytlacovany vzduch nemel kam unikat - rameno
    // strikacky je pri S<->V zaslepene pistem, ktery drzi motor.
    curPatAngle = angPatIsolate;  OCR1A = angleTicks(angPatIsolate);
    curAirAngle = angAirVialFilt; OCR1B = angleTicks(angAirVialFilt);
    delay(SERVO_SETTLE_MS);
    Serial.println(F("# ventily do polohy pro plneni (pacient ISOLATE, vzduch V-F)"));

    bool ok = true;
    for (uint8_t n = 1; ok && n <= cycleCount; n++) {
        ok = runCycle(n);
    }

    // Bezpecne polohy: pacient izolovan, lahvicka odvzdusnena.
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
    Serial.print(F("# samplePeriodMs=")); Serial.print(samplePeriodMs);
    Serial.print(F(" (")); Serial.print(1000 / samplePeriodMs);
    Serial.println(F(" vzorku/s)"));
    Serial.print(F("# cykly=")); Serial.print(cycleCount);
    Serial.print(F(" vzduch/cyklus=")); Serial.print(totalAirMl, 1);
    Serial.print(F(" ml  prvni davka=")); Serial.print(firstAirMl, 1);
    Serial.print(F(" ml  na naplnu=")); Serial.print(loadAirMl, 1);
    Serial.print(F(" ml  rychlost=1 ml/")); Serial.print(flowSecPerMl);
    Serial.println(F(" s"));
    Serial.print(F("# predtlak: "));
    if (precompManualMl >= 0.0f) {
        Serial.print(F("rucne ")); Serial.print(precompManualMl, 2);
    } else {
        Serial.print(F("vypocet, napr. napln 10 ml -> ")); Serial.print(precompMl(10.0f), 2);
    }
    Serial.println(F(" ml (1. cyklus vzdy bez)"));
    Serial.print(F("# uhly: pacient OPEN=")); Serial.print(angPatOpen);
    Serial.print(F(" ISOLATE=")); Serial.print(angPatIsolate);
    Serial.print(F(" | vzduch S-V=")); Serial.print(angAirSyrVial);
    Serial.print(F(" S-F=")); Serial.print(angAirSyrFilt);
    Serial.print(F(" V-F=")); Serial.println(angAirVialFilt);
    Serial.print(F("# vzduch v strikacce=")); Serial.print(airMl(), 2);
    Serial.print(F(" ml  tared=")); Serial.println(tared ? F("ano") : F("ne"));
}

static void printHelp() {
    Serial.println(F("# h=napoveda i=info a=autoCAPDAC"));
    Serial.println(F("# t=deklarace plne vzduchove strikacky (10 ml)"));
    Serial.println(F("# g=spustit serii   x=NOUZOVE ZASTAVENI"));
    Serial.println(F("# j/k=jog vzduch +-0.5 ml (odvzdusneni hadicky)"));
    Serial.println(F("# u0=pacient IZOLACE u1=pacient OTEVRENO"));
    Serial.println(F("# u2=vzduch S-V u3=vzduch S-F u4=vzduch V-F"));
    Serial.println(F("# p<ms>=perioda vzorku  c<n>=pocet cyklu"));
    Serial.println(F("# v<ml>=vzduch na cyklus  l<ml>=vzduch na jednu naplnu"));
    Serial.println(F("# f<ml>=prvni davka cyklu (posouva doplneni)"));
    Serial.println(F("# r<ml>=predtlak rucne  r=predtlak vypoctem"));
    Serial.println(F("# s<s>=sekund na 1 ml  #<text>=znacka do logu"));
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

static void handleLine() {
    if (lineLen == 0) return;
    switch (line[0]) {
        case 'h': printHelp(); break;
        case 'i': printInfo(); break;
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
        case 'u': {
            uint8_t ang;
            switch (line[1]) {
                case '0': ang = angPatIsolate;  curPatAngle = ang; OCR1A = angleTicks(ang); break;
                case '1': ang = angPatOpen;     curPatAngle = ang; OCR1A = angleTicks(ang); break;
                case '2': ang = angAirSyrVial;  curAirAngle = ang; OCR1B = angleTicks(ang); break;
                case '3': ang = angAirSyrFilt;  curAirAngle = ang; OCR1B = angleTicks(ang); break;
                case '4': ang = angAirVialFilt; curAirAngle = ang; OCR1B = angleTicks(ang); break;
                default: Serial.println(F("# u0..u4")); return;
            }
            Serial.print(F("# ventil -> ")); Serial.println(ang);
            break;
        }
        case 'p': {
            int v = atoi(&line[1]);
            if (v >= 20 && v <= 2000) { samplePeriodMs = (uint16_t)v; printInfo(); }
            else Serial.println(F("# CHYBA: rozsah 20..2000 ms"));
            break;
        }
        case 'c': {
            int v = atoi(&line[1]);
            if (v >= 1 && v <= 50) { cycleCount = (uint8_t)v; printInfo(); }
            break;
        }
        case 'v': {
            float v = atof(&line[1]);
            if (v > 0.0f && v <= 60.0f) { totalAirMl = v; printInfo(); }
            break;
        }
        case 'l': {
            float v = atof(&line[1]);
            if (v > 0.0f && v <= AIR_SYR_MAX_ML) { loadAirMl = v; printInfo(); }
            break;
        }
        case 'f': {
            float v = atof(&line[1]);
            if (v > 0.0f && v <= AIR_SYR_MAX_ML) { firstAirMl = v; printInfo(); }
            break;
        }
        case 'r': {
            if (line[1] == '\0') { precompManualMl = -1.0f; printInfo(); break; }
            float v = atof(&line[1]);
            if (v >= 0.0f && v < AIR_SYR_MAX_ML) { precompManualMl = v; printInfo(); }
            break;
        }
        case 's': {
            int v = atoi(&line[1]);
            if (v >= 1 && v <= 60) { flowSecPerMl = (uint8_t)v; printInfo(); }
            break;
        }
        case 'g':
            if (running) { Serial.println(F("# CHYBA: beh uz probiha")); break; }
            if (!tared) { Serial.println(F("# CHYBA: neprovedeno 't'")); break; }
            runSeries();
            break;
        case '#': break;   // znacka do logu
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
    Serial.begin(BAUD);
    Wire.begin();
    Wire.setClock(100000UL);
    delay(100);

    Serial.println(F("# TEST 1/3/4 - profil signalu pri vytlacovani VZDUCHEM"));
    Serial.println(F("# kanaly: C1 spodni prstenec, C2 svisla, C3 horni prstenec"));
    Serial.println(F("# syrova data, zadna filtrace, zadna detekce"));

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

    printInfo();
    printHelp();
    Serial.println(F("# POSTUP: 1) vzduchova strikacka PLNA, penicilinka ve studni"));
    Serial.println(F("#         2) 't' vychozi stav, pak 'g' start"));
}

void loop() {
    pollSerial();
}
