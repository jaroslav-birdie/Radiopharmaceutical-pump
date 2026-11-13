// ============================================================
//  TEST 11 – SABLONA C3 / max C3 + KONTROLY S PRAHEM 0,80 (studna 3/3/3 mm)
//
//  Tlaci se vzduch, dokud nenastane PRVNI z kriterii nize; pak se pacient
//  OKAMZITE zavre a tlaceni konci. Obsluha pak zmeri vysku hladiny od
//  vnejsiho dna lahvicky. Cil: hladina ~8,5 mm (~4 ml), testy 9+10: 8,75 mm.
//
//  Detekce (viz TEST11.md) - jen porovnani hodnot signalu, zadny cas ani
//  rychlost. Vzorky jen s otevrenym pacientem ('p', 'w'), filtr: kose po
//  12 vzorcich, klouzave okno 8 kosu.
//    aktivace  C3 (okno) klesne na 0,85 x sve maximum v behu
//  pak zastavi PRVNI z:
//    sablona   C3 <= 0,78 x vlastni maximum v behu
//    podil     C3/C1 <= 0,68
//    vrchol    C1 - C3 kleslo o 10 % vysky hrbu (zaloha, test 7: 4,1 mm)
//    skok      C1 (kos) o 15 % niz nez vetsi ze 2 predchozich = posledni zachrana
//  Bezpecnostni kontroly:
//    pred serii  suche hodnoty C1, C3 v PRAZDNE studne
//    pred tlacenim  C1 i C3 aspon o 0,5 pF nad suchou hodnotou (lahvicka
//                tam je a ma kapalinu u prstencu), C3/C1 >= 0,80
//    pri aktivaci  C3/C1 v okamziku maxima C3 >= 0,80 (maximum z plato);
//                jinak okamzite zastaveni a konec serie
//  Kontrola shody se sablonou (jen zapis '# SHODA ***', nezastavuje):
//    C1 < 0,95 x max C1, dokud C3 > 0,65 x max C3 (C1 klesa predcasne)
//    pri C3 = 0,80 x max C3 musi byt (C1 - C3) - d0 >= 0,15 x max C3
//  Sum muze kazde kriterium jen USPISIT (vic zbytku), nikdy opozdit.
//  Pred tlacenim: C3/C1 v klidu musi byt >= 0,8 (horni prstenec zakryt),
//  jinak se netlaci a obsluha doplni kapalinu.
//
//  Rezim PROFIL (v<ml>) zustava: vytlaci pevny objem, detekce jen zapisuje.
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

#define N_CH              3   // pocet merenych vstupu FDC1004 (CIN1..CIN3)

// ---------- mapa kanalu ----------
// Ktery vstup FDC1004 (CIN1..CIN3) cte kterou elektrodu. Overuje se
// DOTYKOVYM TESTEM (prikaz 'm', prst na elektrode = kanal na 16 pF), ne
// popisky. Tady jsou vychozi hodnoty po startu (studna z testu 6 podle
// popisku); za behu se meni prikazem n<spodni><svisla><horni>, napr. n213
// pro zkusebni lahvicku z testu 5 (spodni CIN2, svisla CIN3, horni CIN1).
// Sloupce C1/C2/C3 v logu i detekce znamenaji vzdy spodni/svislou/horni
// elektrodu.
#define CH_LOWER_CIN      1   // spodni prstenec
#define CH_VERT_CIN       2   // svisla elektroda
#define CH_UPPER_CIN      3   // horni prstenec
static_assert(((1 << CH_LOWER_CIN) | (1 << CH_VERT_CIN) | (1 << CH_UPPER_CIN)) == 0x0E,
              "mapa kanalu musi byt permutace CIN1..CIN3");
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

// ---------- detekce vrcholu (vse relativni, zadne pF) ----------
#define DET_MED_N           10      // median pro monitor 'm'
#define PEAK_BIN_SAMPLES    12      // kos = 12 vzorku (0,6 s pri 20 vzorcich/s)
#define PEAK_WIN_BINS        8      // klouzave okno = 8 kosu (4,8 s)
#define PEAK_ARM_FRAC     0.85f     // aktivace: C3 <= 0,85 x vlastni maximum
#define PEAK_DROP_FRAC    0.10f     // vrchol: pokles o 10 % vysky hrbu
#define STOP_C3_FRAC      0.78f     // sablona: C3 <= 0,78 x vlastni maximum
#define STOP_Q_MAX        0.68f     // podil: C3/C1 <= 0,68
#define FIT_C1_MIN        0.95f     // shoda: C1 >= 0,95 x max C1 ...
#define FIT_C3_ZONE       0.65f     // ... dokud C3 > 0,65 x max C3
#define FIT_CHECK_C3      0.80f     // shoda: pri C3 = 0,80 x max C3 ...
#define FIT_RISE_MIN      0.15f     // ... musi byt (C1 - C3) - d0 >= 0,15 x max C3
#define STEP_DROP_FRAC    0.15f     // skok: C1 v kosu o 15 % niz nez vetsi ze 2 predchozich
#define START_Q_MIN       0.80f     // pred tlacenim C3/C1 >= 0,80 (test 10: start iterace 0,90)
#define MAX_Q_MIN         0.80f     // C3/C1 v okamziku maxima C3 >= 0,80 (test 10: 0,893-0,927)
#define VIAL_MIN_PF       0.50f     // lahvicka: C1 i C3 v klidu >= sucha hodnota + 0,5 pF
#define MAX_REFILLS          3      // max. doplneni vzduchu v jednom cyklu

// ---------- hlidani senzoru ----------
// Ve skutecnych datech se stejna hodnota opakuje nejvys 2x za sebou (sum
// ~2000 LSB). 10x po sobe bitove stejna hodnota = FDC1004 nemeri.
#define FROZEN_LIMIT        10
#define FDC_CHECK_SAMPLES   20
#define FDC_REINIT_TRIES     3       // pokusy o reinicializaci, kdyz kontrola selze
#define FDC_RESET_MS        10       // po softwarovem resetu FDC1004
#define FDC_STOP_MS          5       // po zastaveni mereni, nez se prepise konfigurace
#define FDC_CONF_RST    0x8000       // FDC_CONF bit 15 = reset
#define FDC_CONF_CHECK  0x0DF0       // RATE, REPEAT a MEAS_1..4 pro zpetnou kontrolu
#define FDC_CHECK_PERIOD_MS 10
#define MONITOR_PERIOD_MS  500       // prikaz 'm': vypis C1..C3 kazdych 0,5 s

// ---------- parametry behu (menitelne pres Serial) ----------
static const uint16_t samplePeriodMs = 50;   // 20 vzorku/s (DET_MED_N je na to naladen)
static uint8_t  cycleCount     = 5;     // pocet cyklu
static uint8_t  flowSecPerMl   = 5;     // rychlost tlaceni
static float    precompManualMl = -1.0f; // <0 = vypocet, jinak rucne (r<ml>)
static float    profileAirMl   = 0.0f;  // >0 = PROFIL: vytlacit tolik vzduchu, detekce
                                        // jen zapisuje (stin); 0 = zastaveni detekci (v<ml>)

// ---------- typy detekce ----------
// Pred prvni funkci a v parametrech funkci jen jako uint8_t: Arduino IDE
// vklada automaticky generovane prototypy pred prvni funkci a vyctovy typ
// v nich by jeste nemusel byt znamy.
enum DetMode : uint8_t { DET_OFF, DET_ON };
enum DetHit  : uint8_t { HIT_NONE, HIT_C3, HIT_Q, HIT_STOP, HIT_BADMAX, HIT_STEP };

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
static bool fdcEnsure();

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
//
// Pred zapisem CONF_MEASx se mereni ZASTAVI (FDC_CONF = 0) a pocka se na
// dobeh rozjeteho prevodu. Prepis konfigurace behem bezici REPEAT sekvence
// obcas prevodnik zasekne (konfigurace sedi, DONE=0, hodnoty stoji) -
// test 6: nahodne uspesne/neuspesne reinicializace.
static void applyConfig() {
    writeReg(REG_FDC_CONF, 0);
    delay(FDC_STOP_MS);
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

// Cteni podle elektrody: 0 = spodni, 1 = svisla, 2 = horni (viz mapa kanalu).
static uint8_t chMap[N_CH] = { CH_LOWER_CIN - 1, CH_VERT_CIN - 1, CH_UPPER_CIN - 1 };

static float readPf(uint8_t ch);
static float readElectrode(uint8_t e) { return readPf(chMap[e]); }

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
static void detectFeed(float c1, float c3, char phase);

// Hlidani zamrzleho senzoru: bitove stejna hodnota FROZEN_LIMIT x po sobe
// na kteremkoli kanalu ukonci beh (pres abortRequested()).
// Svisla elektroda (C2) se jen meri a loguje, nehlida se - zastaveni
// ani kontrola senzoru na ni nezavisi.
#define VERT_ELECTRODE  1

static void watchFrozen(float c1, float c2, float c3) {
    static float prev[N_CH];
    static uint8_t same[N_CH];
    float v[N_CH] = { c1, c2, c3 };
    for (uint8_t i = 0; i < N_CH; i++) {
        if (i == VERT_ELECTRODE) continue;
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
static float lastC1, lastC2, lastC3;    // posledni zalogovany vzorek

static void logSample(char phase) {
    float c1 = readElectrode(0);
    float c2 = readElectrode(1);
    float c3 = readElectrode(2);
    watchFrozen(c1, c2, c3);
    lastC1 = c1; lastC2 = c2; lastC3 = c3;
    detectFeed(c1, c3, phase);
    Serial.print(cycleNo);
    Serial.print(';'); Serial.print(millis() - cycleT0);
    Serial.print(';'); Serial.print(phase);
    Serial.print(';'); Serial.print(pushedMl(), 3);
    Serial.print(';'); Serial.print(c1, 4);
    Serial.print(';'); Serial.print(c2, 4);
    Serial.print(';'); Serial.println(c3, 4);
}

static void printChannelMap() {
    Serial.print(F("# C1 = spodni prstenec (CIN")); Serial.print(chMap[0] + 1);
    Serial.print(F("), C2 = svisla (CIN")); Serial.print(chMap[1] + 1);
    Serial.print(F("), C3 = horni prstenec (CIN")); Serial.print(chMap[2] + 1);
    Serial.println(F(")"));
}

static void printLogHeader() {
    Serial.println(F("# cyklus;t_ms;faze;vytlaceno_ml;C1;C2;C3"));
    Serial.println(F("# t_ms je relativni k zacatku cyklu; C1..C3 v pF, BEZ filtrace"));
    printChannelMap();
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
static float   c1Rest, c3Rest, c1Sigma, c3Sigma;   // klid 'k'
// kosy po PEAK_BIN_SAMPLES vzorcich: rozpracovany kos + kruhova historie hotovych
static float   accC1, accC3;
static uint16_t accN;
static float   binC1[PEAK_WIN_BINS], binC3[PEAK_WIN_BINS];
static uint8_t binHead, binCount;
static float   prevBinC1, prev2BinC1;   // dva predchozi kose pro skok; < 0 = neplatny
static char    prevPhase;
// stav hledani vrcholu
static bool    armed, haveD0;
static uint8_t firedMask;               // bit = druh udalosti uz zapsan/proveden
static float   d0, dMax, c3Max, c1Max;
static float   qAtMax;                   // C3/C1 v okamziku maxima C3
static float   dryC1 = -1.0f, dryC3 = -1.0f;   // suche hodnoty (prazdna studna)
static uint8_t fitMask;                  // bit 0 = C1 predcasne, 1 = rozdil neroste, 2 = kontrola provedena
static uint32_t dMaxMs;                 // cas maxima rozdilu od zacatku cyklu
static float   hitD, hitC1, hitC3;

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

static void detectStart() {
    accC1 = accC3 = 0; accN = 0;
    binHead = binCount = 0; prevBinC1 = prev2BinC1 = -1.0f; prevPhase = 0;
    armed = haveD0 = false;
    firedMask = 0; qAtMax = 0;
    dMax = -1e9f; dMaxMs = 0; c3Max = c1Max = -1e9f; d0 = 0; fitMask = 0;
    detHitKind = HIT_NONE;
    detMode = DET_ON;
}

static void printEvent(const __FlashStringHelper *prefix, uint8_t kind) {
    Serial.print(prefix);
    switch (kind) {
        case HIT_C3:   Serial.print(F("STOP sablona C3")); break;
        case HIT_Q:    Serial.print(F("STOP podil C3/C1")); break;
        case HIT_BADMAX: Serial.print(F("CHYBA maximum C3 mimo plato")); break;
        case HIT_STOP: Serial.print(F("STOP vrchol")); break;
        default:       Serial.print(F("SKOK C1")); break;
    }
    Serial.print(F(" @ t=")); Serial.print(millis() - cycleT0);
    Serial.print(F(" ms  C1=")); Serial.print(hitC1, 3);
    Serial.print(F(" C3=")); Serial.print(hitC3, 3);
    Serial.print(F(" d=")); Serial.print(hitD, 3);
    Serial.print(F(" d0=")); Serial.print(d0, 3);
    Serial.print(F(" dmax=")); Serial.print(dMax, 3);
    Serial.print(F(" @ t=")); Serial.println(dMaxMs);
    Serial.print(F("#   C3/maxC3=")); Serial.print(hitC3 / c3Max, 3);
    Serial.print(F(" C1/maxC1=")); Serial.print(hitC1 / c1Max, 3);
    Serial.print(F(" (d-d0)/maxC3=")); Serial.print((hitD - d0) / c3Max, 3);
    Serial.println((fitMask & 3) ? F("  SHODA PORUSENA") : F("  shoda OK"));
}

// Kontrola shody se sablonou: jen zapis do logu, nic nezastavuje.
static void fitWarn(uint8_t bit, const __FlashStringHelper *msg) {
    if (fitMask & bit) return;
    fitMask |= bit;
    Serial.print(F("# SHODA *** ")); Serial.print(msg);
    Serial.print(F(" @ t=")); Serial.println(millis() - cycleT0);
}

static void checkFit(float w1, float w3, float d) {
    if (w3 > FIT_C3_ZONE * c3Max && w1 < FIT_C1_MIN * c1Max) {
        fitWarn(1, F("C1 klesa predcasne"));
    }
    if (!(fitMask & 4) && w3 <= FIT_CHECK_C3 * c3Max) {
        fitMask |= 4;
        if (d - d0 < FIT_RISE_MIN * c3Max) fitWarn(2, F("C1-C3 neroste"));
    }
}

// Zastaveni: pacient se zavira jeste uvnitr vzorkovani, motor stoji,
// protoze moveAir()/sampleFor() se po detectHit() hned vrati. V profilu
// se udalost jen zapise (kazdy druh jednou za cyklus).
static void detectFire(uint8_t kind) {
    uint8_t bit = (uint8_t)(1 << kind);
    if (firedMask & bit) return;
    firedMask |= bit;
    if (profileAirMl > 0.0f) { printEvent(F("# STIN "), kind); return; }
    curPatAngle = angPatIsolate;
    OCR1A = angleTicks(angPatIsolate);
    detHitKind = (DetHit)kind;
    detMode = DET_OFF;
}

// Okno pres poslednich 8 kosu: aktivace po poklesu C3, pak sablona, podil
// a vrchol jako zaloha. Vse pomerove - nezavisi na spolecnem zisku prstencu.
static void detectWindow() {
    float w1 = 0, w3 = 0;
    for (uint8_t i = 0; i < PEAK_WIN_BINS; i++) { w1 += binC1[i]; w3 += binC3[i]; }
    w1 /= PEAK_WIN_BINS; w3 /= PEAK_WIN_BINS;
    float d = w1 - w3;
    if (!haveD0) { d0 = d; haveD0 = true; }
    if (w3 > c3Max) { c3Max = w3; qAtMax = w3 / w1; }
    if (w1 > c1Max) c1Max = w1;
    hitD = d; hitC1 = w1; hitC3 = w3;
    if (!armed && w3 <= PEAK_ARM_FRAC * c3Max) {
        armed = true;
        Serial.print(F("# aktivace @ t=")); Serial.print(millis() - cycleT0);
        Serial.print(F(" ms  C3=")); Serial.print(w3, 3);
        Serial.print(F(" max C3=")); Serial.print(c3Max, 3);
        Serial.print(F(" C3/C1 v maximu=")); Serial.println(qAtMax, 3);
        // Maximum uz je za nami (C3 kleslo o 15 %). Pochazi z plato, jen kdyz
        // byl v tu chvili horni prstenec zakryty stejne jako spodni.
        if (qAtMax < MAX_Q_MIN) { detectFire(HIT_BADMAX); return; }
    }
    if (!armed) return;
    if (d > dMax) { dMax = d; dMaxMs = millis() - cycleT0; }
    checkFit(w1, w3, d);
    if (w3 <= STOP_C3_FRAC * c3Max) detectFire(HIT_C3);
    if (detMode == DET_OFF) return;
    if (w3 <= STOP_Q_MAX * w1) detectFire(HIT_Q);
    if (detMode == DET_OFF) return;
    if (d < dMax - PEAK_DROP_FRAC * (dMax - d0)) detectFire(HIT_STOP);
}

// Hotovy kos: nejdriv skok C1 proti vetsimu ze dvou predchozich kosu
// (skok se casto rozdeli do dvou kosu), pak okno.
static void detectBin(float c1, float c3) {
    float ref = max(prevBinC1, prev2BinC1);
    if (prev2BinC1 > 0 && c1 < (1.0f - STEP_DROP_FRAC) * ref) {
        hitC1 = c1; hitC3 = c3; hitD = c1 - c3;
        detectFire(HIT_STEP);
        if (detMode == DET_OFF) return;
    }
    prev2BinC1 = prevBinC1;
    prevBinC1 = c1;
    binC1[binHead] = c1; binC3[binHead] = c3;
    binHead = (binHead + 1) % PEAK_WIN_BINS;
    if (binCount < PEAK_WIN_BINS) binCount++;
    if (binCount == PEAK_WIN_BINS) detectWindow();
}

// Detekce bezi na CASE, vzdy kdyz je pacientska cesta otevrena a ustalena:
// 'p' (otevreno, motor stoji) a 'w' (tlaceni). Hladina se muze hybat
// i bez motoru (zbytkovy pretlak), proto se nezastavuje se strikackou.
// Spotrebovany vzduch detekce vubec nepouziva - neni vazany na hladinu.
// S pacientem zavrenym (doplneni vzduchu, prejezdy ventilu) hladina
// stoji; vzorky se ignoruji, okno a maxima zustavaji, zahodi se jen
// rozpracovany kos a porovnani pro skok (prepnuti ventilu dela skoky).
static bool patientFlowPhase(char phase) {
    return phase == 'p' || phase == 'w';
}

static void detectFeed(float c1, float c3, char phase) {
    if (detMode == DET_OFF) return;
    bool flowing = patientFlowPhase(phase);
    if (flowing && !patientFlowPhase(prevPhase)) {
        accC1 = accC3 = 0; accN = 0; prevBinC1 = prev2BinC1 = -1.0f;
    }
    prevPhase = phase;
    if (!flowing) return;
    accC1 += c1; accC3 += c3; accN++;
    if (accN < PEAK_BIN_SAMPLES) return;
    detectBin(accC1 / accN, accC3 / accN);
    accC1 = accC3 = 0; accN = 0;
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
            if (!refillAir()) return false;    // faze 'e'/'a'/'v' detekce ignoruje
        }
        if (!moveAir(airMl(), true, 'w', 1)) return false;
    }
    return true;
}

static void printHit() {
    printEvent(detHitKind >= HIT_BADMAX ? F("# *** ") : F("# "), detHitKind);
}

// Profil: vytlaci se profileAirMl (s doplnovanim), detekce bezi jen jako
// stin. Pacient se zavira hned po dojezdu, stejne jako pri detekci.
static bool pushProfile() {
    if (!openPatientAndHold()) return false;
    while (pushedMl() < profileAirMl - 0.001f) {
        if (airMl() < 0.01f) {
            if (!refillAir()) return false;    // faze 'e'/'a'/'v' detekce ignoruje
        }
        float rest = profileAirMl - pushedMl();
        if (!moveAir(rest < airMl() ? rest : airMl(), true, 'w', 1)) return false;
    }
    return patientValveTo(angPatIsolate);
}

// ============================================================
//  Jeden cyklus
// ============================================================
// Klid 'k' s uzavrenou lahvickou (S<->V) a kontrola vychoziho stavu:
// horni prstenec musi byt zakryty (C3/C1 >= 0,8). Jinak by hladina uz
// byla v mezere nebo niz a tlacit se nesmi - obsluha doplni kapalinu.
static bool restAndCheckStart() {
    for (;;) {
        if (!airValveTo(angAirSyrVial)) return false;
        if (!sampleFor(VALVE_SETTLE_MS, 'v')) return false;
        if (!restWithStats()) return false;
        float q = c3Rest / c1Rest;
        Serial.print(F("# start C3/C1=")); Serial.print(q, 3);
        Serial.print(F("  nad suchou: C1 +")); Serial.print(c1Rest - dryC1, 3);
        Serial.print(F(" C3 +")); Serial.println(c3Rest - dryC3, 3);
        bool vial = c1Rest >= dryC1 + VIAL_MIN_PF && c3Rest >= dryC3 + VIAL_MIN_PF;
        if (profileAirMl > 0.0f || (vial && q >= START_Q_MIN)) return true;
        Serial.println(vial ? F("# *** hladina neni nad hornim prstencem, netlaci se ***")
                            : F("# *** lahvicka chybi nebo je prazdna, netlaci se ***"));
        if (!airValveTo(angAirVialFilt)) return false;
        if (!waitConfirm(F("VLOZIT lahvicku / DOPLNIT kapalinu, potvrdit"))) return false;
    }
}

static bool runCycle(uint8_t n) {
    cycleNo = n;
    Serial.print(F("# ===== CYKLUS ")); Serial.print(n);
    Serial.print('/'); Serial.print(cycleCount); Serial.println(F(" ====="));

    bool profile = profileAirMl > 0.0f;
    if (!waitConfirm(profile
            ? F("PROFIL: doplnit kapalinu OD OKA (pokazde jinak), potvrdit")
            : F("naplnit lahvicku (1: 9-14 ml, 2-3: 7 ml, 4-5: 6 ml), vlozit, potvrdit"))) return false;
    if (!fdcEnsure()) return false;      // senzor mohl mezi cykly zamrznout

    steppersEnable();
    pushedSteps = 0;
    cycleT0 = millis();

    // Lahvicka uzavrena (S<->V), pacient zustava ISOLATE.
    if (!restAndCheckStart()) return false;
    if (!precompress(precompMl())) return false;

    detectStart();
    bool ok = profile ? pushProfile() : pushUntilDetect();
    detMode = DET_OFF;
    if (!ok) return false;
    bool step = false;
    if (!profile) {
        printHit();
        step = (detHitKind == HIT_STEP || detHitKind == HIT_BADMAX);
        // Nulovat PRED 's' - jinak se sampleFor() po detectHit() hned vrati
        // a ustaleni serva se preskoci (test 7).
        detHitKind = HIT_NONE;
        if (!sampleFor(SERVO_SETTLE_MS, 's')) return false;   // pacient uz zavren
    }
    detHitKind = HIT_NONE;

    Serial.println(F("# klid pod tlakem (d), pak odvzdusneni a klid (o)"));
    if (!sampleFor(KLID_POST_MS, 'd')) return false;
    if (!airValveTo(angAirVialFilt)) return false;
    if (!sampleFor(KLID_REST_MS, 'o')) return false;
    if (!topUpAir()) return false;

    Serial.print(F("# cyklus ")); Serial.print(n);
    Serial.println(F(" hotov"));

    if (!profile && !waitConfirm(F("ZMERIT vysku hladiny od vnejsiho dna [mm], poslat cislo"))) return false;
    if (step) {
        Serial.println(F("# serie ukoncena (skok C1 / maximum mimo plato)"));
        return false;
    }
    return true;
}

// ============================================================
//  Cela serie
// ============================================================
// Suche hodnoty prstencu v PRAZDNE studne (v pristroji pri kalibraci, pred
// vlozenim lahvicky). Slouzi jen ke kontrole, ze lahvicka s kapalinou je
// na miste - do detekce nevstupuji.
static bool measureDry() {
    if (!waitConfirm(F("studna PRAZDNA (bez lahvicky) - suche hodnoty, potvrdit"))) return false;
    if (!fdcEnsure()) return false;
    cycleNo = 0;
    cycleT0 = millis();
    if (!restWithStats()) return false;
    dryC1 = c1Rest; dryC3 = c3Rest;
    Serial.print(F("# suche hodnoty: C1=")); Serial.print(dryC1, 3);
    Serial.print(F(" C3=")); Serial.println(dryC3, 3);
    return true;
}

static void runSeries() {
    running = true;
    printLogHeader();

    // Lahvicka se plni VZDY v teto poloze: pacient uzavren, V<->F otevreno.
    curPatAngle = angPatIsolate;  OCR1A = angleTicks(angPatIsolate);
    curAirAngle = angAirVialFilt; OCR1B = angleTicks(angAirVialFilt);
    delay(SERVO_SETTLE_MS);
    Serial.println(F("# ventily do polohy pro plneni (pacient ISOLATE, vzduch V-F)"));

    bool ok = measureDry();
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
    Serial.print(F("# CAPDAC CIN1=")); Serial.print(capdac[0]);
    Serial.print(F(" CIN2=")); Serial.print(capdac[1]);
    Serial.print(F(" CIN3=")); Serial.println(capdac[2]);
    printChannelMap();
    Serial.print(F("# cykly=")); Serial.print(cycleCount);
    Serial.print(F("  rychlost=1 ml/")); Serial.print(flowSecPerMl);
    Serial.println(F(" s"));
    if (profileAirMl > 0.0f) {
        Serial.print(F("# REZIM: PROFIL, vzduch na cyklus ")); Serial.print(profileAirMl, 1);
        Serial.println(F(" ml, detekce jen zapisuje (STIN)"));
    } else {
        Serial.println(F("# REZIM: sablona C3/max 0.78 | C3/C1 0.68 + kontroly 0.80 (test 11)"));
    }
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

// Napoveda: postup testu 11 krok za krokem, pak prehled prikazu.
static void printHelp() {
    Serial.println(F("# ================ POSTUP TESTU 11 ================"));
    Serial.println(F("# 1) PRIPRAVA: vzduch. strikacka PLNA, ventily+strikacka"));
    Serial.println(F("#    osazene, hadicka naplnena, studna PRAZDNA, log bezi"));
    Serial.println(F("# 2) MAPA: 'm', prst postupne na spodni / svislou / horni"));
    Serial.println(F("#    elektrodu -> 16 pF musi ukazat C1 / C2 / C3."));
    Serial.println(F("#    Jinak 'n<s><v><h>' (napr. n213) a znovu 'm'."));
    Serial.println(F("# 3) 't' = vzduchova strikacka je plna"));
    Serial.println(F("# 4) 'i' = REZIM ... (test 11), cykly=5, FDC OK"));
    Serial.println(F("# 5) 'g', 1. vyzva: studna PRAZDNA -> suche hodnoty"));
    Serial.println(F("# 6) cyklus 1: NEJDRIV potvrdit BEZ lahvicky - musi"));
    Serial.println(F("#    hlasit 'lahvicka chybi'; pak vlozit 9-14 ml"));
    Serial.println(F("# 7) cyklus 2: 7 ml, 3: 7 ml, 4-5: 6 ml (strikackou)"));
    Serial.println(F("# 8) po zastaveni a 30 s klidu ZMERIT vysku hladiny"));
    Serial.println(F("#    od vnejsiho dna v mm a poslat cislo"));
    Serial.println(F("# 9) log poslat jako soubor"));
    Serial.println(F("# ------------------- prikazy -------------------"));
    Serial.println(F("# h napoveda  i info  f kontrola FDC  a CAPDAC  x STOP"));
    Serial.println(F("# m monitor  n<s><v><h> mapa  t plna  g start"));
    Serial.println(F("# j/k jog vzduch +-0.5 ml  u0/u1 pacient ISOL/OTEV"));
    Serial.println(F("# u2/u3/u4 vzduch S-V/S-F/V-F  #<text> znacka do logu"));
    Serial.println(F("# v<ml> profil (v0=zastaveni)  c<n> cykly  s<s> s/ml"));
    Serial.println(F("# r<ml>/r predtlak rucne/vypoctem"));
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

// Prubezny vypis C1..C3 (median 10 vzorku, uz podle mapy kanalu) do
// stisku libovolne klavesy. Kontrola mapy: prst na spodnim prstenci musi
// prebit C1, na svisle elektrode C2, na hornim prstenci C3 (16 pF).
static void monitor() {
    Serial.println(F("# monitor C1;C2;C3 [pF], libovolny znak = konec"));
    printChannelMap();
    delay(200);
    while (Serial.available() > 0) Serial.read();
    while (Serial.available() == 0) {
        float v[N_CH][DET_MED_N];
        for (uint8_t k = 0; k < DET_MED_N; k++) {
            for (uint8_t i = 0; i < N_CH; i++) v[i][k] = readElectrode(i);
            delay(MONITOR_PERIOD_MS / DET_MED_N);
        }
        Serial.print(F("m;"));
        Serial.print(median(v[0]), 3); Serial.print(';');
        Serial.print(median(v[1]), 3); Serial.print(';');
        Serial.println(median(v[2]), 3);
    }
    while (Serial.available() > 0) Serial.read();
}

// Nastaveni mapy kanalu: n<spodni><svisla><horni>, cislice 1..3, kazda
// jednou (napr. n123, n213). Overit dotykovym testem 'm'.
static void setChannelMap(const char *arg) {
    uint8_t seen = 0, v[N_CH];
    for (uint8_t i = 0; i < N_CH; i++) {
        v[i] = (uint8_t)(arg[i] - '1');
        if (v[i] >= N_CH || (seen & (1 << v[i]))) {
            Serial.println(F("# CHYBA: n<spodni><svisla><horni>, napr. n123"));
            return;
        }
        seen |= 1 << v[i];
    }
    for (uint8_t i = 0; i < N_CH; i++) chMap[i] = v[i];
    printChannelMap();
}

static void handleLine() {
    if (lineLen == 0) return;
    switch (line[0]) {
        case 'h': printHelp(); break;
        case 'i': printInfo(); break;
        case 'f': fdcEnsure(); break;
        case 'm': monitor(); break;
        case 'n': setChannelMap(&line[1]); break;
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
        case 'v': {
            float v = atof(&line[1]);
            if (v == 0.0f || (v >= 5.0f && v <= 60.0f)) { profileAirMl = v; printInfo(); }
            else Serial.println(F("# CHYBA: v0 (zastaveni) nebo v5..v60 (profil)"));
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
            if (!fdcEnsure()) break;
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
// a pro kazdy vstup CIN1..CIN3, zda se vysledek meni. Konstantni hodnota
// ma tri mozne priciny, ktere se tu rozlisi:
//   SATURACE  - kapacita mimo rozsah i s CAPDAC 31 (> ~113 pF, typicky
//               vstup navazany na zem, napr. stineni na GND tesne u elektrody)
//   I2C       - cteni vraci 0 (chyba komunikace)
//   ZAMRZLY   - hodnota uprostred rozsahu, cip neprevadi (vypnout napajeni)
// Vraci false, pokud kterykoli vstup nemeri.
#define RAW_SAT_LIMIT  8388000L   // |raw| nad timto = plny rozsah (2^23 - 1)

static bool checkFdcChannel(uint8_t ch) {
    int32_t first = readRaw(ch), lo = first, hi = first;
    for (uint8_t k = 0; k < FDC_CHECK_SAMPLES; k++) {
        delay(FDC_CHECK_PERIOD_MS);
        int32_t v = readRaw(ch);
        if (v < lo) lo = v;
        if (v > hi) hi = v;
    }
    Serial.print(F("#   CIN")); Serial.print(ch + 1);
    Serial.print(F(" CAPDAC=")); Serial.print(capdac[ch]);
    Serial.print(F(" C=")); Serial.print(readPf(ch), 3);
    Serial.print(F(" pF raw ")); Serial.print(lo); Serial.print(F(" .. ")); Serial.print(hi);
    bool ok = (hi != lo);
    if (ok)                                    Serial.println(F("  OK"));
    else if (lo >= RAW_SAT_LIMIT)              Serial.println(F("  *** SATURACE + (kapacita nad rozsahem)"));
    else if (lo <= -RAW_SAT_LIMIT)             Serial.println(F("  *** SATURACE - (kapacita pod CAPDAC)"));
    else if (lo == 0)                          Serial.println(F("  *** cte 0 - chyba I2C?"));
    else                                       Serial.println(F("  *** ZAMRZLY (cip neprevadi)"));
    return ok;
}

static bool checkFdc() {
    delay(FDC_CHECK_PERIOD_MS);       // DONE bity se ctenim vysledku nuluji
    uint16_t conf = readReg(REG_FDC_CONF);
    Serial.print(F("# FDC_CONF=0x")); Serial.print(conf, HEX);
    Serial.print(F(" (REPEAT=")); Serial.print((conf >> 8) & 1);
    Serial.print(F(" MEAS1-3=")); Serial.print((conf >> 5) & 7, BIN);
    Serial.print(F(" DONE=")); Serial.print(conf & 0x0F, BIN);
    Serial.print(')');
    for (uint8_t i = 0; i < N_CH; i++) {
        Serial.print(F(" CONF_MEAS")); Serial.print(i + 1);
        Serial.print(F("=0x")); Serial.print(readReg(REG_CONF_MEAS1 + i), HEX);
    }
    Serial.println();
    bool ok = true;
    for (uint8_t i = 0; i < N_CH; i++) {
        bool chOk = checkFdcChannel(i);
        if (i == chMap[VERT_ELECTRODE]) continue;    // svisla: jen informace
        ok = chOk && ok;
    }
    Serial.println(ok ? F("# FDC1004 meri OK")
                      : F("# *** CHYBA: nektery vstup FDC1004 nemeri, viz radky vyse ***"));
    sensorFrozen = !ok;
    return ok;
}

// Uvolneni zaseknute I2C sbernice. Kdyz Arduino resetuje uprostred cteni
// (napr. otevrenim Serial Monitoru), FDC1004 zustane v pulce bajtu a drzi
// SDA dole. Az 9 hodinovych pulzu ho dovede na konec bajtu, START+STOP
// pak sbernici uvolni. Vola se pred Wire.begin().
static void i2cBusRecover() {
    pinMode(SDA, INPUT_PULLUP);
    pinMode(SCL, INPUT_PULLUP);
    delayMicroseconds(10);
    for (uint8_t i = 0; i < 9 && digitalRead(SDA) == LOW; i++) {
        pinMode(SCL, OUTPUT); digitalWrite(SCL, LOW);
        delayMicroseconds(5);
        pinMode(SCL, INPUT_PULLUP);
        delayMicroseconds(5);
    }
    pinMode(SDA, OUTPUT); digitalWrite(SDA, LOW);   // START
    delayMicroseconds(5);
    pinMode(SDA, INPUT_PULLUP);                     // STOP
    delayMicroseconds(5);
}

static void i2cStart() {
    i2cBusRecover();
    Wire.begin();
    Wire.setClock(100000UL);
}

// Softwarovy reset FDC1004, konfigurace, CAPDAC a zpetna kontrola, ze cip
// konfiguraci prijal. FDC1004 si nastaveni drzi jen dokud ma napajeni;
// nepovedeny zapis pri startu = cip nemeri, dokud se znovu nenastavi.
static bool fdcInit() {
    writeReg(REG_FDC_CONF, FDC_CONF_RST);
    delay(FDC_RESET_MS);
    for (uint8_t i = 0; i < N_CH; i++) autoCapdac(i);   // kazdy vola applyConfig()
    uint16_t expect = (3 << 10) | (1 << 8);
    for (uint8_t i = 0; i < N_CH; i++) expect |= (uint16_t)1 << (7 - i);
    uint16_t conf = readReg(REG_FDC_CONF);
    bool ok = (conf & FDC_CONF_CHECK) == expect;
    if (!ok) {
        Serial.print(F("# *** FDC_CONF neprijat: 0x")); Serial.print(conf, HEX);
        Serial.print(F(" (ocekavano 0x")); Serial.print(expect, HEX); Serial.println(F(")"));
    }
    return ok;
}

// Kontrola FDC1004; kdyz selze, uvolneni sbernice + reinicializace a znovu.
static bool fdcEnsure() {
    for (uint8_t t = 0; t <= FDC_REINIT_TRIES; t++) {
        if (t > 0) {
            Serial.print(F("# reinicializace FDC1004, pokus ")); Serial.println(t);
            Wire.end();
            i2cStart();
            fdcInit();
        }
        if (checkFdc()) return true;
    }
    Serial.println(F("# *** FDC1004 se nepodarilo rozbehnout - vypnout napajeni, pak 'f'"));
    return false;
}

void setup() {
    Serial.begin(BAUD);
    i2cStart();
    delay(100);

    Serial.println(F("# TEST 5/6 - zastaveni hladiny mezi prstenci / profily ve studni"));
    printChannelMap();

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

    // FDC1004 az po dojezdu serv: rozjezd obou serv do vychozich poloh
    // bere velky proud a pokles napajeni FDC1004 zasekne (konfigurace
    // zustane, prevod stoji, DONE=0) - test 6, prvni nastaveni po startu.
    delay(SERVO_SETTLE_MS);
    fdcInit();
    fdcEnsure();

    printInfo();
    printHelp();
}

void loop() {
    pollSerial();
}
