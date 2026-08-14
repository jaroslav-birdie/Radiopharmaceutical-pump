// ============================================================
//  Simulace SKUTECNEHO aplikacniho protokolu na kapacitnim snimani
//
//  Ucel: zavrit posledni otevrenou diru v analyze detekce kriticke
//  hladiny. Dosavadni data (tools/capacitive_cycle_test,
//  tools/capacitive_edge_detect_test) vznikla pri PLNYCH zdvizich
//  (0 -> -20 ml a zpet na plno). Realny protokol ale projede prstencem
//  jen ~3 ml na iteraci a hlubokou "patku" krivky, kde sedi namereny
//  creep, nikdy nenavstivi. Analyza v CLAUDE.md ("Chovani detekce
//  v iterativnim protokolu") proto stoji na PREMAPOVANI plnych zdvihu
//  na iterace - obhajitelnem v oblasti vrcholu, ale neoverenem.
//  Tenhle sketch mericky protokol imituje primo, takze uz nic
//  premapovavat netreba:
//
//     krok 0 (faze 1) : lahvicka 10 ml -> vytlacit na kritickou hladinu
//     krok 1..N       : doplnit 3,0 ml -> vytlacit na kritickou hladinu
//
//  Cely beh je ZCELA AUTOMATICKY, bez zasahu obsluhy - vcetne
//  pocatecniho naplneni lahvicky (viz autoFill nize). Obsluha jen
//  vlozi PRAZDNOU lahvicku do studny a spusti 'g'.
//
//  SAMOSTATNY diagnosticky sketch - nema nic spolecneho s firmware
//  cerpadla. Ovlada fyziologicky krokovy motor (D6/D7, DRV8825 na
//  sdilenem nENBL A3) - odsava z lahvicky I davkuje zpet, ze stejne
//  strikacky (zpetna klapka byla pro tenhle test odstranena).
//  Vzduchova vetev ani ventily se nepouzivaji - meri se chovani
//  SENZORU, ne pneumatika.
//
//  ---------------------------------------------------------------
//  CO PREBIRA z tools/capacitive_edge_detect_test (v9) beze zmeny
//  ---------------------------------------------------------------
//   - detekce kriticke hladiny: klouzavy prumer 25 vzorku, pokles
//     o deltaCritical od NEOMEZENEHO maxima od zacatku vytlacovani,
//     potvrzeni pres confirmSamples po sobe jdoucich vzorku
//   - sledovane maximum se resetuje na zacatku KAZDEHO vytlacovani
//     (to je prave ta vlastnost, ktera dela z creepu patky neskodny
//     jev - viz bezpecnostni pravidla v CLAUDE.md)
//   - samo-kalibrujici se ochrana proti ruseni pres CIN2 vcetne
//     "ziveho" kalibracniho okna s bezicim motorem (v9) a kontroly
//     znecistene kalibrace proti zdrave zakladne (v8)
//
//  ---------------------------------------------------------------
//  V CEM SE LISI (a proc)
//  ---------------------------------------------------------------
//   1) DOPLNUJE SE PEVNYCH refillMl (3,0 ml), ne nahodnych 6-17 ml.
//      Nahodne doplnovani melo smysl, dokud se testovala robustnost
//      detekce vuci neznamemu pocatecnimu stavu. Ted se meri prave
//      to, co protokol opravdu dela, takze je davka pevna.
//   2) ZADNE POTVRZOVANI OBSLUHOU mezi iteracemi ('1'/'0' v predchozim
//      nastroji). Beh je nepretrzity, aby se elektrody mezi iteracemi
//      nehybaly - pohyb lahvicky by byl presne ten common-mode zasah,
//      ktery se snazime vyloucit, a navic by zamaskoval creep, ktery
//      hledame.
//   3) OCHRANA CIN2 POKRACUJE SAMA. V predchozim nastroji cekala na
//      'y'. Tady se motor pozastavi, vrchol C1 i vyhlazovaci okno
//      ZUSTAVAJI (viz ST_PAUSED v CLAUDE.md - reset by detekci
//      zpozdil, coz je nebezpecny smer chyby) a beh sam pokracuje,
//      jakmile je rychlost zmeny C2 pod prahem c2QuietSamples vzorku
//      po sobe. Vyhlazovaci okno se behem pauzy dal plni (hladina se
//      nehybe, takze prumer zustava platny) - na rozdil od v7/v8/v9
//      se tedy pri pauze NEresetuje vubec nic.
//   4) LOGUJE SE i odhadovany OBSAH LAHVICKY v ml (`vial_ml`), ne jen
//      poloha strikacky. Diky automatickemu pocatecnimu naplneni je
//      pocatecni stav znamy, takze absolutni objem v lahvicce dava
//      poprve smysl. POZOR: je to dopocet z kroku motoru, ne mereni -
//      predpoklada prazdnou lahvicku pred startem a nulove ztraty.
//   5) KLICOVA VYSTUPNI VELICINA je `hloubka` = vrchol_ml - sepnuti_ml,
//      tedy o kolik ml pod vrcholem krivky prah sepnul. Musi vyjit
//      MENSI nez refillMl, jinak dalsi vytlacovani zacne na sestupne
//      vetvi misto nad vrcholem. Sketch to hlida a hlasi primo za behu.
//
//  ---------------------------------------------------------------
//  POSTUP
//  ---------------------------------------------------------------
//    1. Naplnit strikacku na rozumnou stredni hodnotu (~30 ml) -
//       viz MECH_LIMIT_ML nize, proc.
//    2. Do studny vlozit PRAZDNOU lahvicku (10ml varianta).
//    3. 'o' driver ON, volitelne 'j'/'k' odvzdusneni hadicky.
//    4. 't' tare (referencni bod krokoveho pocitadla).
//    5. 'g' - a dal uz nic. Beh trva zhruba 40-60 minut.
//    6. 'x' kdykoliv = okamzite zastaveni.
//
//  Pokud je lahvicka naplnena rucne, vypnout autoFill prikazem 'f0'
//  a nastavit skutecny objem 'v<ml>'.
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
#define SMOOTH_WINDOW      25      // ~5 s pri 200 ms/vzorek
#define SETTLE_MS          5000UL  // klid pred kazdym vytlacovanim (motor stoji)

// Kolik vytlacovani se vejde do pameti: faze 1 + MAX_ITER iteraci.
#define MAX_ITER           12
#define MAX_PUSH           (MAX_ITER + 1)

// Vyhradne HARDWAROVA ochrana zdvihu strikacky - NENI to detekcni ani
// "alarm" koncept (viz capacitive_edge_detect_test v5). Bilance behu:
// s autoFill se nejdriv davkuje +10 ml (naplneni lahvicky), faze 1 pak
// odebere ~9 ml a kazda iterace doplni 3,0 a odebere ~3,1 ml - poloha se
// tedy pohybuje zhruba v pasmu -1 az +10 ml od tare. Bez autoFill je to
// stejne siroke pasmo posunute o -10 ml. Strop 25 ml je v obou pripadech
// pohodlna rezerva, ktera se pri ocekavanem chovani nepriblizi.
#define MECH_LIMIT_ML      25.0f

// ---------- ochrana proti vnejsimu ruseni pres CIN2 ----------
#define C2_MA_WINDOW       5    // kratke vyhlazeni C2
#define C2_LAG             3    // pres kolik vzorku se meri zmena (0,6 s pri 200 ms)

// ---------- parametry (2-znakove prikazy: dc/cf/cg/ck/cm) ----------
static float    deltaCritical   = 0.22f;   // pF - pokles od (neomezeneho) maxima = kriticka hladina
static uint8_t  confirmSamples  = 5;       // kolik po sobe jdoucich vzorku musi prah drzet
static uint16_t samplePeriodMs  = 200;
static uint8_t  iterCountTarget = MAX_ITER;// pocet iteraci PO fazi 1
static float    refillMl        = 3.0f;    // davka roztoku na iteraci (= VOL_SAL_ITER_ML)
static float    vialStartMl     = 10.0f;   // pocatecni objem v lahvicce (10ml varianta)
static bool     autoFill        = true;    // true = sketch lahvicku naplni sam z prazdne

// Prah ochrany CIN2 se pocita znovu pred kazdym vytlacovanim z prave
// namereneho sumu (klidoveho i s bezicim motorem) - viz v7-v9
// v capacitive_edge_detect_test, proc pevna konstanta nestaci.
static float    c2GuardMultiplier = 2.5f;
static float    c2GuardMinFloor = 0.020f;  // pF
static uint8_t  c2GuardConfirm  = 2;
static float    c2GuardEffective = 0.0f;
static float    c2CalibMax      = 0.0f;
static float    c2BaselineQuiet = 0.0f;
#define C2_BASELINE_OUTLIER_MULT 4.0f
#define C2_RUNNING_CALIB_SAMPLES 20
#define C2_GUARD_MAX_CEILING 0.150f

// Automaticke pokracovani po ruseni (nahrada za 'y' v predchozim nastroji).
#define C2_QUIET_SAMPLES     15      // kolik vzorku po sobe musi byt klid, aby se pokracovalo
#define C2_QUIET_TIMEOUT_MS  180000UL // po teto dobe bez klidu se beh vzda (rusi trvale)
#define MAX_PAUSES_PER_PUSH  20      // pojistka proti nekonecnemu ping-pongu

static uint8_t  capdac[N_CH]   = { 0, 0 };
static bool     tared          = false;
static bool     driverEnabled  = false;
static int32_t  posSteps       = 0;      // 0 = pri tare; zaporne = odsato
static int32_t  runStartSteps  = 0;      // poloha pri 'g' - odtud se pocita obsah lahvicky
static float    vialBaseMl     = 0.0f;   // obsah lahvicky v okamziku 'g'
static char     line[24];
static uint8_t  lineLen        = 0;
static uint32_t runStartMs     = 0;
static bool     running        = false;
static uint8_t  pushIndex      = 0;      // 0 = faze 1, 1..N = iterace

// ---------- vysledky jednotlivych vytlacovani ----------
static float   resStartVial[MAX_PUSH];   // obsah lahvicky na zacatku vytlacovani
static float   resPeakVial[MAX_PUSH];    // obsah pri vrcholu C1
static float   resPeakC1[MAX_PUSH];
static float   resTrigVial[MAX_PUSH];    // obsah pri sepnuti prahu
static float   resTrigC1[MAX_PUSH];
static uint8_t resPauses[MAX_PUSH];
static bool    resNewPeak[MAX_PUSH];     // dosahlo sledovane maximum nad startovni hodnotu?
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

// Poloha strikacky od posledniho 'tare' - slouzi jen pro mechanickou ochranu.
static float levelMl() {
    return (float)posSteps / SAL_STEPS_PER_ML;
}

// Odhad obsahu lahvicky. Dopocet z kroku motoru, NE mereni - predpoklada
// znamy pocatecni stav a nulove ztraty.
static float vialMl() {
    return vialBaseMl + (float)(posSteps - runStartSteps) / SAL_STEPS_PER_ML;
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

static void motorStart(bool push) {
    digitalWrite(PIN_STEPPER_EN, STEPPER_ENABLED_LEVEL);
    driverEnabled = true;
    digitalWrite(PIN_SAL_DIR, push ? SAL_DIR_PUSH_LEVEL
                                    : (SAL_DIR_PUSH_LEVEL == HIGH ? LOW : HIGH));
    delayMicroseconds(10);
}

static void jogOnce(bool push) {
    int32_t steps = stepsFor(JOG_ML);
    motorStart(push);
    for (int32_t i = 0; i < steps; i++) {
        digitalWrite(PIN_SAL_STEP, HIGH);
        delayMicroseconds(3);
        digitalWrite(PIN_SAL_STEP, LOW);
        delayMicroseconds(SAL_STEP_INTERVAL_US - 3);
        posSteps += push ? 1 : -1;
    }
}

// ---------- log ----------
// faze: F=pocatecni plneni  s=ustaleni (motor stoji)  w=vytlacovani
//       p=pauza kvuli ruseni  r=doplneni roztoku
static void logSample(char phase, float raw1, float sm1, float raw2, float ref, float c2rate) {
    Serial.print(pushIndex);
    Serial.print(';'); Serial.print(millis() - runStartMs);
    Serial.print(';'); Serial.print(vialMl(), 3);
    Serial.print(';'); Serial.print(levelMl(), 3);
    Serial.print(';'); Serial.print(phase);
    Serial.print(';'); Serial.print(raw1, 4);
    Serial.print(';'); Serial.print(sm1, 4);
    Serial.print(';'); Serial.print(ref, 4);
    Serial.print(';'); Serial.print(raw2, 4);
    Serial.print(';'); Serial.println(c2rate, 4);
}

static void printLogHeader() {
    Serial.println(F("# push;t_ms;vial_ml;level_ml;faze;C1_raw;C1_sm;peak_ref;C2_raw;c2rate"));
}

// ---------- diagnostika ----------
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
    Serial.print(F("# c2GuardMultiplier=")); Serial.print(c2GuardMultiplier, 2);
    Serial.print(F(" c2GuardMinFloor=")); Serial.print(c2GuardMinFloor, 4);
    Serial.print(F(" c2GuardConfirm=")); Serial.print(c2GuardConfirm);
    if (c2GuardMultiplier <= 0.0f) Serial.print(F("  *** OCHRANA CIN2 VYPNUTA ***"));
    Serial.println();
    Serial.print(F("# c2GuardEffective=")); Serial.print(c2GuardEffective, 4);
    Serial.print(F(" pF (posledni kalibrace max=")); Serial.print(c2CalibMax, 4);
    Serial.print(F(" pF, zdrava zakladna=")); Serial.print(c2BaselineQuiet, 4);
    Serial.println(F(" pF)"));
    Serial.print(F("# MECH_LIMIT_ML=")); Serial.print(MECH_LIMIT_ML, 1);
    Serial.println(F(" (hardwarova ochrana zdvihu, ne detekcni alarm)"));
    Serial.print(F("# driver=")); Serial.print(driverEnabled ? F("ON") : F("OFF"));
    Serial.print(F(" tared=")); Serial.print(tared ? F("ano") : F("ne"));
    Serial.print(F(" bezi=")); Serial.print(running ? F("ano") : F("ne"));
    Serial.print(F(" poloha strikacky(od tare)=")); Serial.print(levelMl(), 3);
    Serial.println(F(" ml"));
}

static void printHelp() {
    Serial.println(F("# h=napoveda i=info a=autoCAPDAC n=sum"));
    Serial.println(F("# o=driver ON  x=driver OFF / STOP behem behu"));
    Serial.println(F("# t=tare (reset pocitadla, po naplneni strikacky)"));
    Serial.println(F("# j/k=jog +-0.5ml (odvzdusneni)"));
    Serial.println(F("# g=spustit CELY protokol (faze 1 + iterCountTarget iteraci)"));
    Serial.println(F("#   beh je zcela automaticky, obsluha uz nic nedela"));
    Serial.println(F("# r<n>=pocet iteraci po fazi 1  s<ml>=davka roztoku na iteraci"));
    Serial.println(F("# v<ml>=pocatecni objem v lahvicce  f1/f0=automaticke plneni ano/ne"));
    Serial.println(F("# dc<pF>=delta kriticka hladina  cf<n>=potvrzovacich vzorku"));
    Serial.println(F("# p<ms>=perioda vzorku"));
    Serial.println(F("# cm<x>=nasobitel prahu CIN2 (0=vypnout)  cg<pF>=min. podlaha prahu"));
    Serial.println(F("# ck<n>=potvrzovacich vzorku ochrany CIN2"));
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

// ---------- ustaleni pred vytlacovanim ----------
// Motor stoji, sbira se klidova cast kalibrace ochrany CIN2 a plni se
// vyhlazovaci okno, aby vytlacovani zacinalo s cerstvym plnym prumerem.
// Prah se JESTE nevyhodnocuje - to dela finalizeC2GuardThreshold() az po
// "zivem" okne s bezicim motorem (viz v9 v capacitive_edge_detect_test).
static bool settleBeforePush() {
    resetSmooth();
    resetC2Guard();
    Serial.print(F("# ustaleni pred vytlacovanim ")); Serial.print(pushIndex);
    Serial.println(F(" (motor stoji, cca 5 s)..."));
    uint32_t startMs = millis();
    uint32_t lastSampleMs = millis() - samplePeriodMs;
    while (millis() - startMs < SETTLE_MS || smoothCount < SMOOTH_WINDOW) {
        if (abortRequested()) {
            stopMotorDisable();
            Serial.println(F("# ABORT - beh zastaven"));
            return false;
        }
        uint32_t now = millis();
        if (now - lastSampleMs >= samplePeriodMs) {
            lastSampleMs = now;
            float raw1 = readPf(0);
            float raw2 = readPf(1);
            float sm = pushSmooth(raw1);
            float rate = 0.0f;
            pushC2AndCheck(raw2, &rate);
            logSample('s', raw1, sm, raw2, 0.0f, rate);
        }
    }
    c2AlarmCount = 0;
    return true;
}

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

// Pauza kvuli ruseni: motor stoji, vrchol C1 i vyhlazovaci okno se
// NERESETUJI (viz ST_PAUSED v CLAUDE.md - reset by detekci zpozdil).
// Vyhlazovaci okno se dal plni, hladina se nehybe, takze prumer zustava
// platny. Vraci true, kdyz je klid a lze pokracovat.
static bool waitForQuiet(float peakRef) {
    uint32_t startMs = millis();
    uint32_t lastSampleMs = millis() - samplePeriodMs;
    uint8_t quiet = 0;
    while (millis() - startMs < C2_QUIET_TIMEOUT_MS) {
        if (abortRequested()) {
            stopMotorDisable();
            Serial.println(F("# ABORT - beh zastaven"));
            return false;
        }
        uint32_t now = millis();
        if (now - lastSampleMs >= samplePeriodMs) {
            lastSampleMs = now;
            float raw1 = readPf(0);
            float raw2 = readPf(1);
            float sm = pushSmooth(raw1);
            float rate = 0.0f;
            pushC2AndCheck(raw2, &rate);
            logSample('p', raw1, sm, raw2, peakRef, rate);
            if (rate < c2GuardEffective) {
                if (++quiet >= C2_QUIET_SAMPLES) {
                    c2AlarmCount = 0;
                    Serial.println(F("# klid obnoven, pokracuji v TEMZE vytlacovani (vrchol zachovan)"));
                    return true;
                }
            } else {
                quiet = 0;
            }
        }
    }
    stopMotorDisable();
    Serial.println(F("# *** RUSENI NEUSTALO v casovem limitu - beh ukoncen ***"));
    return false;
}

// ---------- davkovani do lahvicky (pocatecni plneni i doplneni roztoku) ----------
static bool dispense(float ml, char phase) {
    motorStart(true);
    int32_t stepsToGo = stepsFor(ml);
    int32_t stepsDone = 0;
    uint32_t lastStepUs = micros();
    uint32_t lastSampleMs = millis();

    while (stepsDone < stepsToGo) {
        if (abortRequested()) {
            stopMotorDisable();
            Serial.println(F("# ABORT - beh zastaven"));
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
                Serial.println(F("# *** MECHANICKY DORAZ STRIKACKY (davkovani) *** beh zastaven ***"));
                return false;
            }
        }
        uint32_t nowMs = millis();
        if (nowMs - lastSampleMs >= samplePeriodMs) {
            lastSampleMs = nowMs;
            float raw1 = readPf(0);
            float raw2 = readPf(1);
            // Pri davkovani hladina stoupa, takze C2 legitimne roste rychle -
            // ochrana se tu nevyhodnocuje a filtr se stejne resetuje
            // v settleBeforePush() pred dalsim vytlacovanim.
            logSample(phase, raw1, raw1, raw2, 0.0f, 0.0f);
        }
    }
    stopMotorDisable();
    return true;
}

// ---------- jedno vytlacovani na kritickou hladinu ----------
static bool runPush() {
    Serial.print(F("# --- VYTLACOVANI "));
    Serial.print(pushIndex);
    if (pushIndex == 0) Serial.println(F(" (faze 1, z plne lahvicky) ---"));
    else { Serial.print(F(" (iterace ")); Serial.print(pushIndex); Serial.println(F(") ---")); }

    if (!settleBeforePush()) return false;

    float startVial = vialMl();
    float startSm = smoothSum / smoothCount;   // vyhlazena hodnota na startu (okno je plne)

    motorStart(false);   // smer odsavani

    // Neomezene maximum od zacatku TOHOTO vytlacovani - reset je zamerny
    // a je to nutna podminka toho, aby creep patky detekci neovlivnil
    // (viz bezpecnostni pravidla v CLAUDE.md).
    float peakMax = -1e9f;
    float peakVial = startVial;
    uint8_t belowCount = 0;
    uint8_t pauses = 0;
    uint32_t lastStepUs = micros();
    uint32_t lastSampleMs = millis();
    uint16_t liveCalibRemaining = C2_RUNNING_CALIB_SAMPLES;

    while (true) {
        if (abortRequested()) {
            stopMotorDisable();
            Serial.println(F("# ABORT - beh zastaven"));
            return false;
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
                Serial.print(F("# *** MECHANICKY DORAZ STRIKACKY (vytlacovani "));
                Serial.print(pushIndex);
                Serial.println(F(") *** kriticka hladina NEBYLA nalezena, beh zastaven ***"));
                return false;
            }
        }
        uint32_t nowMs = millis();
        if (nowMs - lastSampleMs >= samplePeriodMs) {
            lastSampleMs = nowMs;
            float raw1 = readPf(0);
            float raw2 = readPf(1);
            float sm = pushSmooth(raw1);
            float c2rate = 0.0f;
            bool interference = pushC2AndCheck(raw2, &c2rate);

            if (liveCalibRemaining > 0) {
                interference = false;   // jeste se meri "zivy" sum, prah zatim neznamy
                liveCalibRemaining--;
                if (liveCalibRemaining == 0) finalizeC2GuardThreshold();
            }

            // Pri ruseni se NESMI aktualizovat peakMax ani vyhodnotit detekce -
            // nafouknuty peakMax byl pricinou obou selhani v davkovem testu.
            if (interference) {
                stopMotorDisable();
                logSample('w', raw1, sm, raw2, peakMax, c2rate);
                pauses++;
                Serial.print(F("# *** RUSENI (CIN2) *** vytlacovani=")); Serial.print(pushIndex);
                Serial.print(F(" vial=")); Serial.print(vialMl(), 2);
                Serial.print(F(" ml zmena_C2=")); Serial.print(c2rate, 4);
                Serial.print(F(" pF prah=")); Serial.print(c2GuardEffective, 4);
                Serial.print(F(" pF (pauza c. ")); Serial.print(pauses);
                Serial.println(F(") - motor stoji, vrchol NEZTRACEN"));
                if (pauses > MAX_PAUSES_PER_PUSH) {
                    Serial.println(F("# *** PRILIS MNOHO PAUZ v jednom vytlacovani - beh ukoncen ***"));
                    return false;
                }
                if (!waitForQuiet(peakMax)) return false;
                motorStart(false);
                lastStepUs = micros();
                lastSampleMs = millis();
                continue;   // peakMax i belowCount zustavaji nezmenene
            }

            if (sm > peakMax) {
                peakMax = sm;
                peakVial = vialMl();
            }
            logSample('w', raw1, sm, raw2, peakMax, c2rate);

            float drop = peakMax - sm;
            if (drop >= deltaCritical) {
                belowCount++;
                if (belowCount >= confirmSamples) {
                    stopMotorDisable();
                    float trigVial = vialMl();
                    resStartVial[pushIndex] = startVial;
                    resPeakVial[pushIndex]  = peakVial;
                    resPeakC1[pushIndex]    = peakMax;
                    resTrigVial[pushIndex]  = trigVial;
                    resTrigC1[pushIndex]    = sm;
                    resPauses[pushIndex]    = pauses;
                    resNewPeak[pushIndex]   = (peakMax > startSm + 0.005f);
                    pushDone = pushIndex + 1;

                    float depth = peakVial - trigVial;
                    Serial.print(F("#> ")); Serial.print(pushIndex);
                    Serial.print(';'); Serial.print(startVial, 3);
                    Serial.print(';'); Serial.print(peakVial, 3);
                    Serial.print(';'); Serial.print(peakMax, 4);
                    Serial.print(';'); Serial.print(trigVial, 3);
                    Serial.print(';'); Serial.print(sm, 4);
                    Serial.print(';'); Serial.print(drop, 4);
                    Serial.print(';'); Serial.print(depth, 3);
                    Serial.print(';'); Serial.print(startVial - trigVial, 3);
                    Serial.print(';'); Serial.print(resNewPeak[pushIndex] ? '1' : '0');
                    Serial.print(';'); Serial.println(pauses);

                    if (depth >= refillMl) {
                        Serial.print(F("# !!! HLOUBKA SEPNUTI "));
                        Serial.print(depth, 2);
                        Serial.print(F(" ml >= davka "));
                        Serial.print(refillMl, 2);
                        Serial.println(F(" ml - dalsi vytlacovani zacne POD vrcholem !!!"));
                    }
                    if (!resNewPeak[pushIndex]) {
                        Serial.println(F("# pozn.: sledovane maximum nepreslo nad startovni hodnotu"));
                        Serial.println(F("#        (pohybujeme se jen po sestupne vetvi)"));
                    }
                    return true;
                }
            } else {
                belowCount = 0;
            }
        }
    }
}

// ---------- souhrn ----------
static void printSummary() {
    Serial.println(F("# === SOUHRN ==="));
    Serial.println(F("#> push;start_vial_ml;vrchol_vial_ml;vrchol_C1;sepnuti_vial_ml;sepnuti_C1;pokles_pF;hloubka_ml;drah_ml;novy_vrchol;pauzy"));
    float dSum = 0.0f, dMin = 1e9f, dMax = -1e9f;
    uint8_t overWindow = 0, noPeak = 0;
    uint16_t pauseTotal = 0;
    for (uint8_t i = 0; i < pushDone; i++) {
        float depth = resPeakVial[i] - resTrigVial[i];
        Serial.print(F("#> ")); Serial.print(i);
        Serial.print(';'); Serial.print(resStartVial[i], 3);
        Serial.print(';'); Serial.print(resPeakVial[i], 3);
        Serial.print(';'); Serial.print(resPeakC1[i], 4);
        Serial.print(';'); Serial.print(resTrigVial[i], 3);
        Serial.print(';'); Serial.print(resTrigC1[i], 4);
        Serial.print(';'); Serial.print(resPeakC1[i] - resTrigC1[i], 4);
        Serial.print(';'); Serial.print(depth, 3);
        Serial.print(';'); Serial.print(resStartVial[i] - resTrigVial[i], 3);
        Serial.print(';'); Serial.print(resNewPeak[i] ? '1' : '0');
        Serial.print(';'); Serial.println(resPauses[i]);
        if (i > 0) {   // faze 1 startuje z plne lahvicky, do statistiky okna nepatri
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
        Serial.print(F("# hloubka sepnuti pod vrcholem (iterace 1..")); Serial.print(n);
        Serial.print(F("): prumer=")); Serial.print(dSum / n, 3);
        Serial.print(F(" min=")); Serial.print(dMin, 3);
        Serial.print(F(" max=")); Serial.print(dMax, 3);
        Serial.println(F(" ml"));
        Serial.print(F("# mimo okno (hloubka >= ")); Serial.print(refillMl, 2);
        Serial.print(F(" ml): ")); Serial.print(overWindow);
        Serial.print('/'); Serial.println(n);
        Serial.print(F("# bez noveho vrcholu (jen sestupna vetev): ")); Serial.print(noPeak);
        Serial.print('/'); Serial.println(n);
    }
    Serial.print(F("# celkem pauz kvuli ruseni: ")); Serial.println(pauseTotal);
    Serial.println(F("# vial_ml je DOPOCET z kroku motoru, ne mereni"));
    Serial.println(F("# === BEH HOTOV ==="));
}

// ---------- cely protokol ----------
static void runProtocol() {
    running = true;
    runStartMs = millis();
    runStartSteps = posSteps;
    vialBaseMl = autoFill ? 0.0f : vialStartMl;
    pushDone = 0;
    pushIndex = 0;
    c2BaselineQuiet = 0.0f;

    printLogHeader();

    if (autoFill) {
        Serial.print(F("# --- pocatecni naplneni lahvicky ")); Serial.print(vialStartMl, 2);
        Serial.println(F(" ml ---"));
        if (!dispense(vialStartMl, 'F')) { running = false; return; }
    } else {
        Serial.print(F("# lahvicka naplnena rucne, predpokladany obsah "));
        Serial.print(vialStartMl, 2); Serial.println(F(" ml"));
    }

    // faze 1 + iterCountTarget iteraci
    for (pushIndex = 0; pushIndex <= iterCountTarget; pushIndex++) {
        if (pushIndex > 0) {
            Serial.print(F("# --- doplneni roztoku ")); Serial.print(refillMl, 2);
            Serial.print(F(" ml (pred iteraci ")); Serial.print(pushIndex);
            Serial.println(F(") ---"));
            if (!dispense(refillMl, 'r')) break;
        }
        if (!runPush()) break;
    }

    stopMotorDisable();
    printSummary();
    running = false;
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
        Serial.print(F("# c2GuardMultiplier=")); Serial.print(c2GuardMultiplier, 2);
        Serial.println(c2GuardMultiplier > 0.0f ? F("") : F("  (OCHRANA VYPNUTA!)"));
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
            stopMotorDisable();
            Serial.println(F("# driver OFF"));
            break;
        case 't':
            posSteps = 0;
            tared = true;
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
            if (!tared) { Serial.println(F("# CHYBA: neprovedeno tare, nejdriv 't'")); break; }
            runProtocol();
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
    Serial.begin(9600);
    Wire.begin();
    Wire.setClock(100000UL);
    delay(100);

    Serial.println(F("# FDC1004 - simulace realneho aplikacniho protokolu"));
    Serial.println(F("# faze 1 (10 ml -> kriticka hladina) + 12x (+3 ml -> kriticka hladina)"));
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
    Serial.println(F("#         2) PRAZDNA lahvicka do studny (sketch ji naplni sam)"));
    Serial.println(F("#         3) 'o' driver ON, 't' tare, 'g' start"));
    Serial.println(F("#         4) dal uz nic - beh je zcela automaticky (~40-60 min)"));
}

void loop() {
    pollSerial();
}
