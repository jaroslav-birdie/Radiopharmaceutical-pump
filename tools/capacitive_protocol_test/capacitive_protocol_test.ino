// ============================================================
//  Simulace SKUTECNEHO aplikacniho protokolu - vytlacovani VZDUCHEM
//
//  Ucel: overit, jestli je pokles na kritickou hladinu detekovatelny
//  v situaci, ktera odpovida realnemu pristroji. Predchozi nastroje
//  (capacitive_cycle_test, capacitive_edge_detect_test) menily hladinu
//  ODSAVANIM kapaliny primo strikackou - to je cisty, tvrdy pohyb
//  hladiny bez stlacitelneho clenu. Realny pristroj ale kapalinu
//  VYTLACUJE VZDUCHEM: mezi motorem a hladinou je stlacitelny vzduchovy
//  sloupec, pohyb hladiny je proto pomalejsi, mekci a nelinearni vuci
//  krokum motoru. Prave v tomhle rezimu se musi detekce osvedcit.
//
//     krok 0 (faze 1) : lahvicka 10 ml -> vytlacit vzduchem na kritickou
//     krok 1..N       : doplnit 3,0 ml roztoku -> vytlacit na kritickou
//
//  Beh je ZCELA AUTOMATICKY. Rizeni pumpy (piny, uhly ventilu, poradi
//  kroku, rezerva vzduchove strikacky, dobehy) je prevzate 1:1
//  z Radiopharmaceutical-pump.ino / state_machine.cpp.
//
//  ---------------------------------------------------------------
//  ZAVAZNA PRAVIDLA ZADANI (jsou vynucena strukturou kodu, ne kazni)
//  ---------------------------------------------------------------
//   1) OBA DRIVERY ZUSTAVAJI PO CELY BEH ENABLED. nENBL (A3) se sepne
//      jednou pri 'g' a uz se behem behu nikdy nepusti - ani pri pauze
//      kvuli ruseni, ani mezi iteracemi. Motor tak drzi polohu pistu
//      proti zpetnemu tlaku. DISABLE nastane jen na uplnem konci behu
//      nebo pri nouzovem 'x' (stejne jako ST_COMPLETE / ST_EMERGENCY_STOP
//      ve firmwaru).
//   2) MOTOR ROZTOKU NIKDY NECOUVNE. Pin PIN_SAL_DIR se nastavi jednou
//      v setup() na SAL_DIR_PUSH_LEVEL a v celem sketchi uz do nej nikdo
//      nezapise - neexistuje funkce, ktera by to umela. Strikacka
//      roztoku se tedy muze pohybovat vyhradne smerem "tlacit kapalinu".
//   3) PRI DOPLNOVANI ROZTOKU JE VZDUCHOVY VENTIL OTEVREN DO ATMOSFERY
//      (V<->F). Lahvicka je pri davkovani odvzdusnena, takze v ni
//      nevznika pretlak. Vzduch se do strikacky nasava az POTOM
//      (prepnuti na S<->F). Souběh obou operaci se v tomhle projektu uz
//      jednou zkousel a selhal prave na neodvzdusnene lahvicce -
//      viz CLAUDE.md, "Co bylo vyzkouseno a NEFUNGUJE".
//
//  ---------------------------------------------------------------
//  CO PREBIRA z tools/capacitive_edge_detect_test (v9) beze zmeny
//  ---------------------------------------------------------------
//   - detekce kriticke hladiny: klouzavy prumer 25 vzorku, pokles
//     o deltaCritical od NEOMEZENEHO maxima od zacatku vytlacovani,
//     potvrzeni pres confirmSamples po sobe jdoucich vzorku
//   - sledovane maximum se resetuje na zacatku KAZDE extrakce, ale NE
//     pri jejim preruseni (doplneni vzduchu, pauza kvuli ruseni) -
//     viz bezpecnostni pravidla v CLAUDE.md
//   - samo-kalibrujici se ochrana proti ruseni pres CIN2 vcetne "ziveho"
//     kalibracniho okna s bezicim motorem (v9) a kontroly znecistene
//     kalibrace proti zdrave zakladne (v8)
//
//  ---------------------------------------------------------------
//  JAK SE MERI "HLOUBKA" BEZ ZNALOSTI OBJEMU V LAHVICCE
//  ---------------------------------------------------------------
//  Pri odsavani strikackou byl objem v lahvicce presne znamy z kroku
//  motoru. Ted uz ne - cast vtlaceneho vzduchu se jen stlaci a kapalinu
//  nevytlaci. Merenou velicinou je proto objem VZDUCHU vytlaceny mezi
//  vrcholem C1 a sepnutim prahu (`hloubka_vzduch_ml`).
//
//  Pri modelu "komprese spotrebuje na zacatku zdvihu pevny objem C a
//  dal uz je prevod vzduch->kapalina 1:1" plati:
//      kapalina vytlacena do vrcholu = vzduch_pri_vrcholu - C
//      C = vzduch_celkem - davka                (v ustalenem stavu)
//   => hloubka_kapalina = davka - (vzduch_pri_vrcholu - C)
//                       = vzduch_celkem - vzduch_pri_vrcholu
//                       = hloubka_vzduch
//  Hloubka mereny ve vzduchu je tedy za tohoto predpokladu primo
//  hloubka v mililitrech kapaliny. Sketch navic hlasi odhad komprese
//  (`komprese_odh` = vzduch_celkem - davka), aby se dal predpoklad
//  z dat zpetne overit - kdyby se komprese mezi iteracemi vyrazne
//  menila, model neplati a je to v logu videt.
//
//  Klicova podminka zustava stejna: hloubka musi vyjit MENSI nez davka
//  (3 ml), jinak dalsi extrakce zacne uz na sestupne vetvi a vrchol
//  se v ni nikdy nenajde.
//
//  ---------------------------------------------------------------
//  POSTUP
//  ---------------------------------------------------------------
//    1. Vzduchova strikacka PLNA (10 ml), roztokova PLNA (60 ml).
//    2. Oba ventily a obe strikacky osazeny; lahvicka (PRAZDNA, 10ml
//       varianta) s jehlami se pripojuje jako POSLEDNI - stejne
//       instalacni poradi jako u pristroje.
//    3. 't' = deklarace vychoziho stavu (vzduch 10 ml, roztok 0 ml
//       podano). Volitelne 'u<n>' na kontrolu uhlu ventilu.
//    4. 'g' - a dal uz nic. Beh trva zhruba 60-90 minut.
//    5. 'x' kdykoliv = okamzite zastaveni (motory se odpoji, ventily
//       do bezpecnych poloh).
//
//  Pokud je lahvicka naplnena rucne, vypnout 'f0' a nastavit 'v<ml>'.
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

#define N_CH              2   // CIN1 + CIN2

// ---------- piny (shodne s config.h hlavniho firmware) ----------
#define PIN_AIR_STEP      4
#define PIN_AIR_DIR       5
#define PIN_SAL_STEP      6
#define PIN_SAL_DIR       7
#define PIN_STEPPER_EN    A3
#define PIN_SERVO_PATIENT 9    // OC1A
#define PIN_SERVO_AIR    10    // OC1B

#define AIR_DIR_PUSH_LEVEL      HIGH
#define SAL_DIR_PUSH_LEVEL      HIGH
#define STEPPER_ENABLED_LEVEL   LOW
#define STEPPER_DISABLED_LEVEL  HIGH

// ---------- serva (Timer1 HW PWM, prevzato ze servo_valve.cpp) ----------
#define SERVO_MIN_US      544
#define SERVO_MAX_US     2503
#define SERVO_SETTLE_MS  1000UL

// vychozi uhly (prepsane z EEPROM, pokud je platna)
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
#define SAL_SYR_ML_PER_MM  0.625f
#define AIR_STEPS_PER_ML   (STEPS_PER_MM / AIR_SYR_ML_PER_MM)                   // 2000
#define SAL_STEPS_PER_ML   (STEPS_PER_MM / SAL_SYR_ML_PER_MM)                   // 640

#define FLOW_S_PER_ML          5UL
#define AIR_FILL_SPEED_FACTOR  2
#define AIR_STEP_INTERVAL_US   ((uint32_t)(1000000.0f * FLOW_S_PER_ML / AIR_STEPS_PER_ML))  // 2500
#define SAL_STEP_INTERVAL_US   ((uint32_t)(1000000.0f * FLOW_S_PER_ML / SAL_STEPS_PER_ML))  // ~7812

// ---------- objemy a casovani (shodne s config.h) ----------
#define VOL_AIR_SYRINGE_MAX_ML  10.0f
#define VOL_AIR_RESERVE_ML       3.0f   // trvala rezerva - pod ni se netlaci
#define VOL_SAL_TOTAL_ML        60.0f
#define FLUID_DRAIN_MS        5000UL    // dobeh kapaliny hadickou k pacientovi
#define EQUALIZE_TIME_MS      3000UL    // vyrovnani tlaku pres filtr
#define MAX_AIR_REFILLS          5

#define JOG_ML             0.5f

// ---------- vyhlazeni signalu ----------
#define SMOOTH_WINDOW      25      // ~5 s pri 200 ms/vzorek
#define SETTLE_MS          5000UL  // klid pred kazdou extrakci (motory stoji)

#define MAX_ITER           12
#define MAX_PUSH           (MAX_ITER + 1)

// ---------- ochrana proti vnejsimu ruseni pres CIN2 ----------
#define C2_MA_WINDOW       5
#define C2_LAG             3
#define C2_BASELINE_OUTLIER_MULT 4.0f
#define C2_RUNNING_CALIB_SAMPLES 20
#define C2_GUARD_MAX_CEILING 0.150f
#define C2_QUIET_SAMPLES     15
#define C2_QUIET_TIMEOUT_MS  180000UL
#define MAX_PAUSES_PER_PUSH  20

// ---------- parametry ----------
static float    deltaCritical   = 0.22f;
static uint8_t  confirmSamples  = 5;
static uint16_t samplePeriodMs  = 200;
static uint8_t  iterCountTarget = MAX_ITER;
static float    refillMl        = 3.0f;    // davka roztoku na iteraci
static float    vialStartMl     = 10.0f;   // pocatecni napln lahvicky
static bool     autoFill        = true;

static float    c2GuardMultiplier = 2.5f;
static float    c2GuardMinFloor = 0.020f;
static uint8_t  c2GuardConfirm  = 2;
static float    c2GuardEffective = 0.0f;
static float    c2CalibMax      = 0.0f;
static float    c2BaselineQuiet = 0.0f;

// ---------- uhly ventilu ----------
static uint8_t angPatOpen, angPatIsolate;
static uint8_t angAirSyrVial, angAirSyrFilt, angAirVialFilt;
static uint8_t curPatAngle = 255, curAirAngle = 255;

// ---------- stav ----------
static uint8_t  capdac[N_CH]   = { 0, 0 };
static bool     tared          = false;
static bool     running        = false;
static char     line[24];
static uint8_t  lineLen        = 0;
static uint32_t runStartMs     = 0;
static uint8_t  pushIndex      = 0;      // 0 = faze 1, 1..N = iterace

static int32_t  airSteps       = 0;      // obsah vzduchove strikacky v krocich
static int32_t  salSteps       = 0;      // kumulativne podano roztoku (jen roste!)

// ---------- prubeh aktualni extrakce (prezije doplneni vzduchu i pauzu) ----------
static float    pkMax;        // sledovane maximum vyhlazeneho C1
static float    pkAirMl;      // kolik vzduchu bylo vytlaceno v okamziku vrcholu
static float    pkStartSm;    // vyhlazena hodnota na startu extrakce
static int32_t  pushStepsTot; // kumulativne vytlaceny vzduch v teto extrakci
static uint8_t  belowCnt;
static uint8_t  refillCnt;
static uint8_t  pauseCnt;
static uint16_t liveCalib;

// ---------- vysledky ----------
static float   resPeakAir[MAX_PUSH];
static float   resTotAir[MAX_PUSH];
static float   resPeakC1[MAX_PUSH];
static float   resTrigC1[MAX_PUSH];
static uint8_t resRefills[MAX_PUSH];
static uint8_t resPauses[MAX_PUSH];
static bool    resNewPeak[MAX_PUSH];
static uint8_t pushDone        = 0;

// ---------- klouzavy prumer C1 ----------
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

// ---------- ochrana proti ruseni: rychlost zmeny C2 ----------
static float   c2Buf[C2_MA_WINDOW];
static uint8_t c2Idx = 0, c2Count = 0;
static float   c2Sum = 0.0f;
static float   c2Hist[C2_LAG + 1];
static uint8_t c2HistIdx = 0, c2HistCount = 0;
static uint8_t c2AlarmCount = 0;

static void resetC2Guard() {
    c2Idx = 0; c2Count = 0; c2Sum = 0.0f;
    c2HistIdx = 0; c2HistCount = 0; c2AlarmCount = 0;
    c2CalibMax = 0.0f;
}

// Prida vzorek C2, do *outRate vrati |zmenu vyhlazeneho C2 pres C2_LAG vzorku|.
// Vraci true, jakmile prah drzi c2GuardConfirm vzorku po sobe = RUSENI.
static bool pushC2AndCheck(float v, float *outRate) {
    if (c2Count == C2_MA_WINDOW) {
        c2Sum -= c2Buf[c2Idx];
    } else {
        c2Count++;
    }
    c2Buf[c2Idx] = v;
    c2Sum += v;
    c2Idx = (c2Idx + 1) % C2_MA_WINDOW;
    float mean = c2Sum / c2Count;

    bool full = (c2HistCount == C2_LAG + 1);
    float rate = 0.0f;
    if (full) {
        uint8_t lagPos = (uint8_t)((c2HistIdx + 1) % (C2_LAG + 1));
        rate = mean - c2Hist[lagPos];
        if (rate < 0.0f) rate = -rate;
    }
    c2Hist[c2HistIdx] = mean;
    c2HistIdx = (uint8_t)((c2HistIdx + 1) % (C2_LAG + 1));
    if (!full) c2HistCount++;

    *outRate = rate;
    if (full && rate > c2CalibMax) c2CalibMax = rate;

    if (c2GuardEffective <= 0.0f) return false;
    if (full && rate >= c2GuardEffective) {
        c2AlarmCount++;
        if (c2AlarmCount >= c2GuardConfirm) return true;
    } else {
        c2AlarmCount = 0;
    }
    return false;
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

// ============================================================
//  Serva - Timer1 fast PWM 50 Hz (prevzato ze servo_valve.cpp)
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
//  Motory
// ============================================================
// nENBL se sepne pri 'g' a behem celeho behu se uz NIKDY nepousti -
// motory drzi polohu pistu proti zpetnemu tlaku (pravidlo 1 v hlavicce).
static void steppersEnable() {
    digitalWrite(PIN_STEPPER_EN, STEPPER_ENABLED_LEVEL);
}

static void steppersDisable() {
    digitalWrite(PIN_STEPPER_EN, STEPPER_DISABLED_LEVEL);
}

static void stepPulse(uint8_t pin) {
    digitalWrite(pin, HIGH);
    delayMicroseconds(3);      // DRV8825 potrebuje >= 1,9 us
    digitalWrite(pin, LOW);
}

// Smer vzduchove strikacky - jedina strikacka, ktera smi couvat (nasavani
// vzduchu z atmosfery). Roztokova strikacka zadnou takovou funkci nema.
static void airDir(bool push) {
    digitalWrite(PIN_AIR_DIR, push ? AIR_DIR_PUSH_LEVEL
                                   : (AIR_DIR_PUSH_LEVEL == HIGH ? LOW : HIGH));
    delayMicroseconds(10);
}

static float airMl() { return (float)airSteps / AIR_STEPS_PER_ML; }
static float salMl() { return (float)salSteps / SAL_STEPS_PER_ML; }
static float pushedMl() { return (float)pushStepsTot / AIR_STEPS_PER_ML; }

// POZOR: musi ODEBRAT i znaky, ktere 'x' nejsou. Pouhy peek() by se zaseknul
// na prvnim cizim znaku v bufferu a nouzove zastaveni by bylo po zbytek behu
// mrtve - typicky na '\n', ktery zbyde v bufferu, kdyz Serial Monitor posila
// "Both NL & CR" (prikaz 'g' ukonci uz '\r', '\n' zustane).
static bool abortRequested() {
    bool abort = false;
    while (Serial.available() > 0) {
        if ((char)Serial.read() == 'x') abort = true;
    }
    return abort;
}

// ============================================================
//  Log
// ============================================================
// faze: F=pocatecni napln  r=doplneni roztoku  e=vyrovnani tlaku (V<->F)
//       a=nasavani vzduchu (S<->F)  v=prejezd ventilu  s=ustaleni pred extrakci
//       q=obnova vyhlazovaciho okna  w=vytlacovani vzduchem  d=dobeh kapaliny
//       p=pauza kvuli ruseni CIN2
static void logSample(char phase, float raw1, float sm1, float ref,
                      float raw2, float c2rate) {
    Serial.print(pushIndex);
    Serial.print(';'); Serial.print(millis() - runStartMs);
    Serial.print(';'); Serial.print(phase);
    Serial.print(';'); Serial.print(airMl(), 3);
    Serial.print(';'); Serial.print(pushedMl(), 3);
    Serial.print(';'); Serial.print(salMl(), 3);
    Serial.print(';'); Serial.print(curPatAngle);
    Serial.print(';'); Serial.print(curAirAngle);
    Serial.print(';'); Serial.print(raw1, 4);
    Serial.print(';'); Serial.print(sm1, 4);
    Serial.print(';'); Serial.print(ref, 4);
    Serial.print(';'); Serial.print(raw2, 4);
    Serial.print(';'); Serial.println(c2rate, 4);
}

static void printLogHeader() {
    Serial.println(F("# it;t_ms;faze;vzduch_ml;vytlaceno_ml;roztok_ml;"
                     "servo_pac;servo_vzd;C1_raw;C1_sm;vrchol;C2_raw;c2rate"));
}

// ============================================================
//  Vzorkovani behem cekani (prejezd ventilu, dobeh, vyrovnani tlaku)
// ============================================================
// feedSmooth = plnit i vyhlazovaci okno (pouziva se jen tam, kde ma navazovat
// na nasledujici extrakci). Ochrana CIN2 se tu jen SBIRA, nevyhodnocuje -
// pohyb ventilu i stoupajici hladina jsou legitimni zmeny C2.
static bool sampleFor(uint32_t ms, char phase, float ref, bool feedSmooth) {
    uint32_t startMs = millis();
    uint32_t lastSampleMs = millis() - samplePeriodMs;
    while (millis() - startMs < ms) {
        if (abortRequested()) return false;
        uint32_t now = millis();
        if (now - lastSampleMs >= samplePeriodMs) {
            lastSampleMs = now;
            float raw1 = readPf(0);
            float raw2 = readPf(1);
            float sm = feedSmooth ? pushSmooth(raw1) : raw1;
            float rate = 0.0f;
            pushC2AndCheck(raw2, &rate);
            logSample(phase, raw1, sm, ref, raw2, rate);
        }
    }
    return true;
}

static bool patientValveTo(uint8_t angle, float ref) {
    if (angle == curPatAngle) return true;
    curPatAngle = angle;
    OCR1A = angleTicks(angle);
    return sampleFor(SERVO_SETTLE_MS, 'v', ref, false);
}

static bool airValveTo(uint8_t angle, float ref) {
    if (angle == curAirAngle) return true;
    curAirAngle = angle;
    OCR1B = angleTicks(angle);
    return sampleFor(SERVO_SETTLE_MS, 'v', ref, false);
}

// ============================================================
//  Roztokova strikacka - VYHRADNE smerem tlaceni
// ============================================================
// Nikde v tomhle sketchi neexistuje zapis do PIN_SAL_DIR mimo setup().
// Roztokova strikacka se proto nemuze rozjet zpet ani omylem.
// Volat SMI se jen tehdy, kdyz je vzduchovy ventil v V<->F (lahvicka
// odvzdusnena) - viz volajici mista.
static bool salDispense(float ml, char phase) {
    int32_t stepsToGo = (int32_t)(ml * SAL_STEPS_PER_ML + 0.5f);
    if (salSteps + stepsToGo > (int32_t)(VOL_SAL_TOTAL_ML * SAL_STEPS_PER_ML)) {
        Serial.println(F("# *** DOSLA ZASOBA ROZTOKU - beh ukoncen ***"));
        return false;
    }
    if (curAirAngle != angAirVialFilt) {
        // Pojistka proti chybe v poradi kroku: davkovat do neodvzdusnene
        // lahvicky je presne to, co v tomhle projektu uz jednou selhalo.
        Serial.println(F("# *** CHYBA: davkovani roztoku pri neodvzdusnene lahvicce ***"));
        return false;
    }
    int32_t done = 0;
    uint32_t lastStepUs = micros();
    uint32_t lastSampleMs = millis();
    while (done < stepsToGo) {
        if (abortRequested()) return false;
        uint32_t nowUs = micros();
        if (nowUs - lastStepUs >= SAL_STEP_INTERVAL_US) {
            lastStepUs = nowUs;
            stepPulse(PIN_SAL_STEP);
            done++;
            salSteps++;
        }
        uint32_t nowMs = millis();
        if (nowMs - lastSampleMs >= samplePeriodMs) {
            lastSampleMs = nowMs;
            float raw1 = readPf(0);
            float raw2 = readPf(1);
            float rate = 0.0f;
            pushC2AndCheck(raw2, &rate);
            logSample(phase, raw1, raw1, 0.0f, raw2, rate);
        }
    }
    return true;
}

// ============================================================
//  Nasavani vzduchu z atmosfery (S<->F)
// ============================================================
static bool airAspirate(float ml, float ref) {
    int32_t room = (int32_t)(VOL_AIR_SYRINGE_MAX_ML * AIR_STEPS_PER_ML) - airSteps;
    int32_t stepsToGo = (int32_t)(ml * AIR_STEPS_PER_ML + 0.5f);
    if (stepsToGo > room) stepsToGo = room;
    if (stepsToGo <= 0) return true;

    airDir(false);
    int32_t done = 0;
    uint32_t lastStepUs = micros();
    uint32_t lastSampleMs = millis();
    uint32_t interval = AIR_STEP_INTERVAL_US / AIR_FILL_SPEED_FACTOR;
    while (done < stepsToGo) {
        if (abortRequested()) return false;
        uint32_t nowUs = micros();
        if (nowUs - lastStepUs >= interval) {
            lastStepUs = nowUs;
            stepPulse(PIN_AIR_STEP);
            done++;
            airSteps++;
        }
        uint32_t nowMs = millis();
        if (nowMs - lastSampleMs >= samplePeriodMs) {
            lastSampleMs = nowMs;
            float raw1 = readPf(0);
            float raw2 = readPf(1);
            float rate = 0.0f;
            pushC2AndCheck(raw2, &rate);
            logSample('a', raw1, raw1, ref, raw2, rate);
        }
    }
    return true;
}

// ============================================================
//  Kalibrace prahu ochrany CIN2
// ============================================================
static void finalizeC2GuardThreshold() {
    float calibSource = c2CalibMax;
    bool calibOutlier = false;
    if (c2BaselineQuiet > 0.0f && c2CalibMax > c2BaselineQuiet * C2_BASELINE_OUTLIER_MULT) {
        calibOutlier = true;
        calibSource = c2BaselineQuiet;
    } else {
        c2BaselineQuiet = (c2BaselineQuiet <= 0.0f) ? c2CalibMax
                                                     : (0.7f * c2BaselineQuiet + 0.3f * c2CalibMax);
    }

    if (c2GuardMultiplier <= 0.0f) {
        c2GuardEffective = 0.0f;
    } else {
        c2GuardEffective = calibSource * c2GuardMultiplier;
        if (c2GuardEffective < c2GuardMinFloor) c2GuardEffective = c2GuardMinFloor;
        if (c2GuardEffective > C2_GUARD_MAX_CEILING) c2GuardEffective = C2_GUARD_MAX_CEILING;
    }

    if (calibOutlier) {
        Serial.print(F("# VAROVANI: kalibrace CIN2 vypada znecistena (zmereno "));
        Serial.print(c2CalibMax, 4);
        Serial.print(F(" pF, zdrava zakladna "));
        Serial.print(c2BaselineQuiet, 4);
        Serial.println(F(" pF) - pouzita zakladna misto teto kalibrace."));
    }
    Serial.print(F("# ochrana CIN2: klidovy+zivy max=")); Serial.print(c2CalibMax, 4);
    Serial.print(F(" pF -> prah=")); Serial.print(c2GuardEffective, 4);
    Serial.println(c2GuardMultiplier <= 0.0f ? F(" pF (VYPNUTA)") : F(" pF"));
    c2AlarmCount = 0;
}

// ============================================================
//  Naplneni vyhlazovaciho okna cerstvymi vzorky (motory stoji)
// ============================================================
// Pouziva se na startu extrakce i po kazdem jejim preruseni. Sledovany
// vrchol se pritom NIKDY nemeni - to resi volajici.
static bool fillSmoothWindow(char phase) {
    resetSmooth();
    uint32_t lastSampleMs = millis() - samplePeriodMs;
    while (smoothCount < SMOOTH_WINDOW) {
        if (abortRequested()) return false;
        uint32_t now = millis();
        if (now - lastSampleMs >= samplePeriodMs) {
            lastSampleMs = now;
            float raw1 = readPf(0);
            float raw2 = readPf(1);
            float sm = pushSmooth(raw1);
            float rate = 0.0f;
            pushC2AndCheck(raw2, &rate);
            logSample(phase, raw1, sm, pkMax, raw2, rate);
        }
    }
    c2AlarmCount = 0;
    return true;
}

// ============================================================
//  Pauza kvuli ruseni CIN2
// ============================================================
// Motor stoji (ale ZUSTAVA ENABLED). Ceka se, az rychlost zmeny C2 klesne
// pod prah na C2_QUIET_SAMPLES vzorku po sobe, a pak se JESTE dojede plne
// vyhlazovaci okno z cistych vzorku.
//
// Co se ZACHOVA a co ZAHODI:
//  - sledovany vrchol C1 (pkMax) a citac potvrzeni ZUSTAVAJI. To je
//    pravidlo ST_PAUSED z CLAUDE.md: reset by sledovani vrcholu spustil
//    znovu od uz pokleslé hodnoty, takze by se kriticka hladina odhalila
//    POZDEJI - nebezpecny smer chyby.
//  - vyhlazovaci okno se ZAHODI a naplni znovu. Drzi 25 vzorku ZPETNE,
//    takze by po obnoveni obsahovalo vzorky namerene BEHEM ruseni; kdyz
//    ruseni C1 zvedne, nafoukne to pkMax (= mechanismus obou selhani
//    zdokumentovanych u ochrany CIN2 v CLAUDE.md). Hladina se pri pauze
//    nehybe, takze cerstve okno meri tutez hladinu - nic se neztraci.
static bool waitForQuiet() {
    uint32_t startMs = millis();
    uint32_t lastSampleMs = millis() - samplePeriodMs;
    uint8_t quiet = 0;
    bool flushing = false;
    while (millis() - startMs < C2_QUIET_TIMEOUT_MS) {
        if (abortRequested()) return false;
        uint32_t now = millis();
        if (now - lastSampleMs >= samplePeriodMs) {
            lastSampleMs = now;
            float raw1 = readPf(0);
            float raw2 = readPf(1);
            float sm = pushSmooth(raw1);
            float rate = 0.0f;
            pushC2AndCheck(raw2, &rate);
            logSample('p', raw1, sm, pkMax, raw2, rate);

            if (rate < c2GuardEffective) {
                quiet++;
                if (!flushing && quiet >= C2_QUIET_SAMPLES) {
                    flushing = true;
                    resetSmooth();
                    Serial.println(F("# klid, plnim cerstve vyhlazovaci okno..."));
                } else if (flushing && smoothCount >= SMOOTH_WINDOW) {
                    c2AlarmCount = 0;
                    Serial.println(F("# pokracuji v TEZE extrakci (vrchol zachovan)"));
                    return true;
                }
            } else {
                quiet = 0;
                if (flushing) {
                    flushing = false;
                    resetSmooth();
                }
            }
        }
    }
    Serial.println(F("# *** RUSENI NEUSTALO v casovem limitu - beh ukoncen ***"));
    return false;
}

// ============================================================
//  Jeden segment vytlacovani vzduchem (po rezervu strikacky)
// ============================================================
// Vraci: 0 = kriticka hladina detekovana, 1 = dosla zasoba vzduchu,
//        2 = abort / chyba.
static uint8_t airPushSegment() {
    int32_t reserveSteps = (int32_t)(VOL_AIR_RESERVE_ML * AIR_STEPS_PER_ML);
    int32_t stepsToGo = airSteps - reserveSteps;
    if (stepsToGo <= 0) return 1;

    airDir(true);
    int32_t done = 0;
    uint32_t lastStepUs = micros();
    uint32_t lastSampleMs = millis();

    while (done < stepsToGo) {
        if (abortRequested()) return 2;
        uint32_t nowUs = micros();
        if (nowUs - lastStepUs >= AIR_STEP_INTERVAL_US) {
            lastStepUs = nowUs;
            stepPulse(PIN_AIR_STEP);
            done++;
            airSteps--;
            pushStepsTot++;
        }
        uint32_t nowMs = millis();
        if (nowMs - lastSampleMs < samplePeriodMs) continue;
        lastSampleMs = nowMs;

        float raw1 = readPf(0);
        float raw2 = readPf(1);
        float sm = pushSmooth(raw1);
        float c2rate = 0.0f;
        bool interference = pushC2AndCheck(raw2, &c2rate);

        if (liveCalib > 0) {
            interference = false;      // jeste se meri "zivy" sum, prah neznamy
            liveCalib--;
            if (liveCalib == 0) finalizeC2GuardThreshold();
        }

        // Pri ruseni se NESMI aktualizovat pkMax ani vyhodnotit detekce -
        // nafouknuty vrchol byl pricinou obou selhani v davkovem testu.
        if (interference) {
            logSample('w', raw1, sm, pkMax, raw2, c2rate);
            pauseCnt++;
            Serial.print(F("# *** RUSENI (CIN2) *** it=")); Serial.print(pushIndex);
            Serial.print(F(" vytlaceno=")); Serial.print(pushedMl(), 2);
            Serial.print(F(" ml zmena_C2=")); Serial.print(c2rate, 4);
            Serial.print(F(" pF prah=")); Serial.print(c2GuardEffective, 4);
            Serial.print(F(" pF (pauza c. ")); Serial.print(pauseCnt);
            Serial.println(F(") - motor stoji (ENABLED), vrchol NEZTRACEN"));
            if (pauseCnt > MAX_PAUSES_PER_PUSH) {
                Serial.println(F("# *** PRILIS MNOHO PAUZ - beh ukoncen ***"));
                return 2;
            }
            if (!waitForQuiet()) return 2;
            airDir(true);
            lastStepUs = micros();
            lastSampleMs = millis();
            continue;                  // pkMax i belowCnt zustavaji nezmenene
        }

        if (sm > pkMax) {
            pkMax = sm;
            pkAirMl = pushedMl();
        }
        logSample('w', raw1, sm, pkMax, raw2, c2rate);

        if (pkMax - sm >= deltaCritical) {
            belowCnt++;
            if (belowCnt >= confirmSamples) {
                resTrigC1[pushIndex] = sm;
                return 0;
            }
        } else {
            belowCnt = 0;
        }
    }
    return 1;
}

// ============================================================
//  Doplneni vzduchu do strikacky (uvnitr probihajici extrakce)
// ============================================================
// Poradi je shodne s firmwarem: dobeh kapaliny -> izolovat pacienta ->
// odvzdusnit lahvicku (V<->F) -> nasat vzduch (S<->F) -> zpet na S<->V.
static bool airRefillCycle() {
    if (!sampleFor(FLUID_DRAIN_MS, 'd', pkMax, false)) return false;
    if (!patientValveTo(angPatIsolate, pkMax)) return false;
    if (!airValveTo(angAirVialFilt, pkMax)) return false;
    if (!sampleFor(EQUALIZE_TIME_MS, 'e', pkMax, false)) return false;
    if (!airValveTo(angAirSyrFilt, pkMax)) return false;
    if (!airAspirate(VOL_AIR_SYRINGE_MAX_ML, pkMax)) return false;
    if (!sampleFor(EQUALIZE_TIME_MS, 'e', pkMax, false)) return false;
    if (!airValveTo(angAirSyrVial, pkMax)) return false;
    if (!patientValveTo(angPatOpen, pkMax)) return false;
    // Vrchol se NEresetuje - je to porad TATAZ extrakce, hladina se behem
    // doplnovani nehybala. Obnovi se jen vyhlazovaci okno.
    return fillSmoothWindow('q');
}

// ============================================================
//  Jedna kompletni extrakce na kritickou hladinu
// ============================================================
static bool runExtraction() {
    Serial.print(F("# --- EXTRAKCE "));
    Serial.print(pushIndex);
    if (pushIndex == 0) Serial.println(F(" (faze 1, z plne lahvicky) ---"));
    else { Serial.print(F(" (iterace ")); Serial.print(pushIndex); Serial.println(F(") ---")); }

    pkMax = 0.0f;                  // jen pro sloupec "vrchol" v logu pred startem
    pkAirMl = 0.0f;
    pushStepsTot = 0;
    belowCnt = 0;
    refillCnt = 0;
    pauseCnt = 0;
    liveCalib = C2_RUNNING_CALIB_SAMPLES;
    resetC2Guard();
    c2GuardEffective = 0.0f;       // prah plati az po "zivem" okne

    // Ventily do polohy pro vytlacovani, pak klid a naplneni okna.
    if (!airValveTo(angAirSyrVial, 0.0f)) return false;
    if (!patientValveTo(angPatOpen, 0.0f)) return false;
    Serial.println(F("# ustaleni pred extrakci (motory stoji, ~5-10 s)..."));
    if (!sampleFor(SETTLE_MS, 's', 0.0f, false)) return false;
    if (!fillSmoothWindow('s')) return false;
    pkStartSm = smoothSum / smoothCount;

    // Reset sledovaneho vrcholu na zacatku KAZDE extrakce - az tady, kdyz uz
    // se nic neloguje s prazdnou referenci. Uvnitr extrakce (doplneni vzduchu,
    // pauza kvuli ruseni) se vrchol NIKDY neresetuje - viz CLAUDE.md.
    pkMax = -1e9f;

    while (true) {
        uint8_t r = airPushSegment();
        if (r == 2) return false;
        if (r == 0) break;                       // kriticka hladina

        refillCnt++;
        Serial.print(F("# zasoba vzduchu vycerpana, doplneni c. "));
        Serial.println(refillCnt);
        if (refillCnt > MAX_AIR_REFILLS) {
            Serial.println(F("# *** ALARM: prekrocen MAX_AIR_REFILLS - mozna netesnost ***"));
            return false;
        }
        if (!airRefillCycle()) return false;
    }

    // Kriticka hladina: motor stoji, pacient jeste dobiha, pak se uzavre.
    Serial.println(F("# KRITICKA HLADINA"));
    if (!sampleFor(FLUID_DRAIN_MS, 'd', pkMax, false)) return false;
    if (!patientValveTo(angPatIsolate, pkMax)) return false;
    if (!airValveTo(angAirVialFilt, pkMax)) return false;

    resPeakAir[pushIndex]  = pkAirMl;
    resTotAir[pushIndex]   = pushedMl();
    resPeakC1[pushIndex]   = pkMax;
    resRefills[pushIndex]  = refillCnt;
    resPauses[pushIndex]   = pauseCnt;
    resNewPeak[pushIndex]  = (pkMax > pkStartSm + 0.005f);
    pushDone = pushIndex + 1;

    float depth = pushedMl() - pkAirMl;
    Serial.print(F("#> ")); Serial.print(pushIndex);
    Serial.print(';'); Serial.print(pkAirMl, 3);
    Serial.print(';'); Serial.print(pushedMl(), 3);
    Serial.print(';'); Serial.print(depth, 3);
    Serial.print(';'); Serial.print(pkMax, 4);
    Serial.print(';'); Serial.print(resTrigC1[pushIndex], 4);
    Serial.print(';'); Serial.print(pkMax - resTrigC1[pushIndex], 4);
    Serial.print(';'); Serial.print(refillCnt);
    Serial.print(';'); Serial.print(pauseCnt);
    Serial.print(';'); Serial.println(resNewPeak[pushIndex] ? '1' : '0');

    if (pushIndex > 0 && depth >= refillMl) {
        Serial.print(F("# !!! HLOUBKA "));
        Serial.print(depth, 2);
        Serial.print(F(" ml >= davka "));
        Serial.print(refillMl, 2);
        Serial.println(F(" ml - dalsi extrakce zacne POD vrcholem !!!"));
    }
    if (!resNewPeak[pushIndex]) {
        Serial.println(F("# pozn.: vrchol nepresel nad startovni hodnotu"));
        Serial.println(F("#        (pohybujeme se jen po sestupne vetvi)"));
    }
    return true;
}

// ============================================================
//  Doplneni roztoku - lahvicka MUSI byt odvzdusnena (V<->F)
// ============================================================
// Poradi podle zadani: nejdriv odvzdusnit lahvicku a doplnit roztok,
// TEPRVE POTOM nasat vzduch pro dalsi tlaceni.
static bool addSalineThenAir() {
    Serial.print(F("# --- doplneni roztoku ")); Serial.print(refillMl, 2);
    Serial.println(F(" ml (lahvicka odvzdusnena V<->F) ---"));
    if (!airValveTo(angAirVialFilt, 0.0f)) return false;
    if (!sampleFor(EQUALIZE_TIME_MS, 'e', 0.0f, false)) return false;
    if (!salDispense(refillMl, 'r')) return false;
    if (!sampleFor(EQUALIZE_TIME_MS, 'e', 0.0f, false)) return false;

    Serial.println(F("# --- nasati vzduchu do strikacky (S<->F) ---"));
    if (!airValveTo(angAirSyrFilt, 0.0f)) return false;
    if (!airAspirate(VOL_AIR_SYRINGE_MAX_ML, 0.0f)) return false;
    return sampleFor(EQUALIZE_TIME_MS, 'e', 0.0f, false);
}

// ============================================================
//  Souhrn
// ============================================================
static void printSummary() {
    Serial.println(F("# === SOUHRN ==="));
    Serial.println(F("#> it;vzduch_pri_vrcholu_ml;vzduch_celkem_ml;hloubka_ml;komprese_odh_ml;"
                     "vrchol_C1;sepnuti_C1;pokles_pF;doplneni;pauzy;novy_vrchol"));
    float dSum = 0.0f, dMin = 1e9f, dMax = -1e9f;
    uint8_t overWindow = 0, noPeak = 0;
    uint16_t pauseTotal = 0;
    for (uint8_t i = 0; i < pushDone; i++) {
        float depth = resTotAir[i] - resPeakAir[i];
        Serial.print(F("#> ")); Serial.print(i);
        Serial.print(';'); Serial.print(resPeakAir[i], 3);
        Serial.print(';'); Serial.print(resTotAir[i], 3);
        Serial.print(';'); Serial.print(depth, 3);
        // komprese_odh plati jen pro iterace (v ustalenem stavu vytece prave
        // davka); u faze 1 je vytecena kapalina jina, proto se neuvadi
        Serial.print(';');
        if (i > 0) Serial.print(resTotAir[i] - refillMl, 3); else Serial.print('-');
        Serial.print(';'); Serial.print(resPeakC1[i], 4);
        Serial.print(';'); Serial.print(resTrigC1[i], 4);
        Serial.print(';'); Serial.print(resPeakC1[i] - resTrigC1[i], 4);
        Serial.print(';'); Serial.print(resRefills[i]);
        Serial.print(';'); Serial.print(resPauses[i]);
        Serial.print(';'); Serial.println(resNewPeak[i] ? '1' : '0');
        if (i > 0) {   // faze 1 startuje z plne lahvicky, do statistiky nepatri
            dSum += depth;
            if (depth < dMin) dMin = depth;
            if (depth > dMax) dMax = depth;
            if (depth >= refillMl) overWindow++;
            if (!resNewPeak[i]) noPeak++;
        }
        pauseTotal += resPauses[i];
    }
    uint8_t n = (pushDone > 1) ? (uint8_t)(pushDone - 1) : 0;
    if (n > 0) {
        Serial.print(F("# hloubka pod vrcholem (iterace 1..")); Serial.print(n);
        Serial.print(F("): prumer=")); Serial.print(dSum / n, 3);
        Serial.print(F(" min=")); Serial.print(dMin, 3);
        Serial.print(F(" max=")); Serial.print(dMax, 3);
        Serial.println(F(" ml"));
        Serial.print(F("# mimo okno (hloubka >= ")); Serial.print(refillMl, 2);
        Serial.print(F(" ml): ")); Serial.print(overWindow);
        Serial.print('/'); Serial.println(n);
        Serial.print(F("# bez noveho vrcholu (jen sestupna vetev): ")); Serial.print(noPeak);
        Serial.print('/'); Serial.println(n);
        Serial.println(F("# komprese_odh = vzduch_celkem - davka; ma-li byt hloubka"));
        Serial.println(F("# ve vzduchu = hloubka v kapaline, musi byt stabilni"));
    }
    Serial.print(F("# celkem pauz kvuli ruseni: ")); Serial.println(pauseTotal);
    Serial.print(F("# celkem podano roztoku: ")); Serial.print(salMl(), 2);
    Serial.println(F(" ml"));
    Serial.println(F("# === BEH HOTOV ==="));
}

// ============================================================
//  Cely protokol
// ============================================================
static void runProtocol() {
    running = true;
    runStartMs = millis();
    pushDone = 0;
    pushIndex = 0;
    pkMax = 0.0f;
    pushStepsTot = 0;
    c2BaselineQuiet = 0.0f;
    c2GuardEffective = 0.0f;

    printLogHeader();
    steppersEnable();                 // ENABLED po CELY beh, viz pravidlo 1
    Serial.println(F("# drivery ENABLED (zustanou po cely beh)"));

    bool ok = true;

    // Pocatecni napln lahvicky - i tady musi byt lahvicka odvzdusnena.
    if (autoFill) {
        Serial.print(F("# --- pocatecni napln lahvicky ")); Serial.print(vialStartMl, 2);
        Serial.println(F(" ml (V<->F) ---"));
        ok = patientValveTo(angPatIsolate, 0.0f)
          && airValveTo(angAirVialFilt, 0.0f)
          && salDispense(vialStartMl, 'F')
          && sampleFor(EQUALIZE_TIME_MS, 'e', 0.0f, false);
    } else {
        Serial.print(F("# lahvicka naplnena rucne, predpokladany obsah "));
        Serial.print(vialStartMl, 2); Serial.println(F(" ml"));
    }

    for (pushIndex = 0; ok && pushIndex <= iterCountTarget; pushIndex++) {
        if (pushIndex > 0) {
            ok = addSalineThenAir();
            if (!ok) break;
        }
        ok = runExtraction();
    }

    // Bezpecne polohy a teprve ted DISABLE (konec behu = ST_COMPLETE).
    curPatAngle = angPatIsolate; OCR1A = angleTicks(angPatIsolate);
    curAirAngle = angAirVialFilt; OCR1B = angleTicks(angAirVialFilt);
    delay(SERVO_SETTLE_MS);
    steppersDisable();
    Serial.println(F("# ventily do bezpecnych poloh, drivery DISABLED"));
    printSummary();
    running = false;
}

// ============================================================
//  Diagnostika a prikazy
// ============================================================
static void printInfo() {
    Serial.print(F("# CAPDAC1=")); Serial.print(capdac[0]);
    Serial.print(F(" CAPDAC2=")); Serial.println(capdac[1]);
    Serial.print(F("# deltaCritical=")); Serial.print(deltaCritical, 4);
    Serial.print(F(" confirmSamples=")); Serial.print(confirmSamples);
    Serial.print(F(" samplePeriod=")); Serial.println(samplePeriodMs);
    Serial.print(F("# vialStartMl=")); Serial.print(vialStartMl, 2);
    Serial.print(F(" autoFill=")); Serial.print(autoFill ? F("ano") : F("ne"));
    Serial.print(F(" refillMl=")); Serial.print(refillMl, 2);
    Serial.print(F(" iterCountTarget=")); Serial.println(iterCountTarget);
    Serial.print(F("# uhly: pacient OPEN=")); Serial.print(angPatOpen);
    Serial.print(F(" ISOLATE=")); Serial.print(angPatIsolate);
    Serial.print(F(" | vzduch S-V=")); Serial.print(angAirSyrVial);
    Serial.print(F(" S-F=")); Serial.print(angAirSyrFilt);
    Serial.print(F(" V-F=")); Serial.println(angAirVialFilt);
    Serial.print(F("# c2GuardMultiplier=")); Serial.print(c2GuardMultiplier, 2);
    Serial.print(F(" c2GuardMinFloor=")); Serial.print(c2GuardMinFloor, 4);
    Serial.print(F(" c2GuardConfirm=")); Serial.print(c2GuardConfirm);
    if (c2GuardMultiplier <= 0.0f) Serial.print(F("  *** OCHRANA CIN2 VYPNUTA ***"));
    Serial.println();
    Serial.print(F("# vzduch v strikacce=")); Serial.print(airMl(), 2);
    Serial.print(F(" ml  podano roztoku=")); Serial.print(salMl(), 2);
    Serial.print(F(" ml  tared=")); Serial.print(tared ? F("ano") : F("ne"));
    Serial.print(F(" bezi=")); Serial.println(running ? F("ano") : F("ne"));
}

static void printHelp() {
    Serial.println(F("# h=napoveda i=info a=autoCAPDAC n=sum"));
    Serial.println(F("# t=deklarace vychoziho stavu (vzduch PLNY 10 ml, roztok 0 ml)"));
    Serial.println(F("# g=spustit CELY protokol   x=NOUZOVE ZASTAVENI"));
    Serial.println(F("# j/k=jog vzduch +-0.5ml   l=jog roztok +0.5ml (jen tlaceni!)"));
    Serial.println(F("# u0=pacient IZOLACE u1=pacient OTEVRENO"));
    Serial.println(F("# u2=vzduch S-V u3=vzduch S-F u4=vzduch V-F"));
    Serial.println(F("# r<n>=pocet iteraci  s<ml>=davka roztoku  v<ml>=pocatecni napln"));
    Serial.println(F("# f1/f0=automaticka pocatecni napln ano/ne"));
    Serial.println(F("# dc<pF>=delta kriticka hladina  cf<n>=potvrzovacich vzorku"));
    Serial.println(F("# p<ms>=perioda vzorku"));
    Serial.println(F("# cm<x>=nasobitel prahu CIN2 (0=vypnout)  cg<pF>=min. podlaha"));
    Serial.println(F("# ck<n>=potvrzovacich vzorku ochrany CIN2   #<text>=znacka"));
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

static void jogAir(bool push) {
    steppersEnable();
    airDir(push);
    int32_t steps = (int32_t)(JOG_ML * AIR_STEPS_PER_ML);
    for (int32_t i = 0; i < steps; i++) {
        stepPulse(PIN_AIR_STEP);
        delayMicroseconds(AIR_STEP_INTERVAL_US - 3);
        airSteps += push ? -1 : 1;
    }
    Serial.print(F("# vzduch v strikacce=")); Serial.println(airMl(), 2);
}

// Jog roztoku - i tady VYHRADNE smerem tlaceni (zadny parametr smeru).
static void jogSaline() {
    steppersEnable();
    int32_t steps = (int32_t)(JOG_ML * SAL_STEPS_PER_ML);
    for (int32_t i = 0; i < steps; i++) {
        stepPulse(PIN_SAL_STEP);
        delayMicroseconds(SAL_STEP_INTERVAL_US - 3);
        salSteps++;
    }
    Serial.print(F("# podano roztoku=")); Serial.println(salMl(), 2);
}

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
    if (lineLen >= 2 && line[0] == 'c' && line[1] == 'g') {
        float v = atof(&line[2]);
        if (v >= 0.0f) { c2GuardMinFloor = v; Serial.print(F("# c2GuardMinFloor=")); Serial.println(c2GuardMinFloor, 4); }
        return;
    }
    if (lineLen >= 2 && line[0] == 'c' && line[1] == 'k') {
        int v = atoi(&line[2]);
        if (v >= 1 && v <= 100) { c2GuardConfirm = (uint8_t)v; Serial.print(F("# c2GuardConfirm=")); Serial.println(c2GuardConfirm); }
        return;
    }
    if (lineLen >= 2 && line[0] == 'c' && line[1] == 'm') {
        c2GuardMultiplier = atof(&line[2]);
        Serial.print(F("# c2GuardMultiplier=")); Serial.println(c2GuardMultiplier, 2);
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
        case 't':
            airSteps = (int32_t)(VOL_AIR_SYRINGE_MAX_ML * AIR_STEPS_PER_ML);
            salSteps = 0;
            tared = true;
            Serial.println(F("# vychozi stav: vzduch 10 ml, roztok 0 ml podano"));
            break;
        case 'x':
            steppersDisable();
            Serial.println(F("# drivery DISABLED"));
            break;
        case 'j': jogAir(true); break;
        case 'k': jogAir(false); break;
        case 'l': jogSaline(); break;
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
            if (v >= 20 && v <= 5000) { samplePeriodMs = (uint16_t)v; Serial.print(F("# samplePeriodMs=")); Serial.println(samplePeriodMs); }
            break;
        }
        case 'r': {
            int v = atoi(&line[1]);
            if (v >= 1 && v <= MAX_ITER) { iterCountTarget = (uint8_t)v; Serial.print(F("# iterCountTarget=")); Serial.println(iterCountTarget); }
            else { Serial.print(F("# CHYBA: rozsah 1..")); Serial.println(MAX_ITER); }
            break;
        }
        case 's': {
            float v = atof(&line[1]);
            if (v > 0.0f && v <= 10.0f) { refillMl = v; Serial.print(F("# refillMl=")); Serial.println(refillMl, 2); }
            break;
        }
        case 'v': {
            float v = atof(&line[1]);
            if (v > 0.0f && v <= 20.0f) { vialStartMl = v; Serial.print(F("# vialStartMl=")); Serial.println(vialStartMl, 2); }
            break;
        }
        case 'f':
            autoFill = (line[1] != '0');
            Serial.print(F("# autoFill=")); Serial.println(autoFill ? F("ano") : F("ne"));
            break;
        case 'g':
            if (running) { Serial.println(F("# CHYBA: beh uz probiha")); break; }
            if (!tared) { Serial.println(F("# CHYBA: neprovedeno 't'")); break; }
            runProtocol();
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

void setup() {
    Serial.begin(9600);
    Wire.begin();
    Wire.setClock(100000UL);
    delay(100);

    Serial.println(F("# FDC1004 - realny protokol, vytlacovani VZDUCHEM"));
    Serial.println(F("# faze 1 (10 ml -> kriticka) + 12x (+3 ml -> kriticka)"));

    // Drivery zatim odpojeny - obsluha jeste osazuje strikacky a lahvicku.
    pinMode(PIN_STEPPER_EN, OUTPUT);
    digitalWrite(PIN_STEPPER_EN, STEPPER_DISABLED_LEVEL);
    pinMode(PIN_AIR_STEP, OUTPUT);
    pinMode(PIN_AIR_DIR, OUTPUT);
    pinMode(PIN_SAL_STEP, OUTPUT);
    pinMode(PIN_SAL_DIR, OUTPUT);
    digitalWrite(PIN_AIR_STEP, LOW);
    digitalWrite(PIN_SAL_STEP, LOW);
    // JEDINY zapis do PIN_SAL_DIR v celem sketchi - strikacka roztoku
    // se od ted muze pohybovat vyhradne smerem tlaceni kapaliny.
    digitalWrite(PIN_SAL_DIR, SAL_DIR_PUSH_LEVEL);

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
    Serial.println(F("# POSTUP: 1) vzduchova strikacka PLNA (10 ml), roztokova PLNA"));
    Serial.println(F("#         2) ventily a strikacky osazene, lahvicka az POSLEDNI"));
    Serial.println(F("#         3) 't' vychozi stav, 'g' start"));
    Serial.println(F("#         4) dal uz nic - beh je automaticky (~60-90 min)"));
}

void loop() {
    pollSerial();
}
