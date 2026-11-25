#include "state_machine.h"
#include <Wire.h>
#include <EEPROM.h>

// Názvy stavů pro OLED (ASCII, max 21 znaků).
static const char SN_INIT[]     PROGMEM = "Inicializace";
static const char SN_READY[]    PROGMEM = "Priprava systemu";
static const char SN_CALIB[]    PROGMEM = "Kalibrace";
static const char SN_P1[]       PROGMEM = "Faze 1";
static const char SN_ITER[]     PROGMEM = "Iterace";
static const char SN_DONE[]     PROGMEM = "Dokonceno";
static const char SN_PAUSED[]   PROGMEM = "POZASTAVENO";
static const char SN_ALARM[]    PROGMEM = "ALARM VZDUCH";
static const char SN_ESTOP[]    PROGMEM = "NOUZOVY STOP";
static const char SN_ERROR[]    PROGMEM = "CHYBA";

static const char *const STATE_NAMES[ST_STATE_COUNT] PROGMEM = {
    SN_INIT, SN_READY, SN_CALIB,
    SN_P1, SN_P1, SN_P1, SN_P1,
    SN_ITER, SN_ITER, SN_ITER, SN_ITER,
    SN_DONE, SN_PAUSED, SN_ALARM, SN_ESTOP, SN_ERROR
};

// Popisy kroků procesu pro výpis (krokovací režim, změna stavu).
static const char SD_PUSH[]     PROGMEM = "TLACENI: kontrola lahvicky, predtlak, tlaceni do kriticke hladiny";
static const char SD_RESUME[]   PROGMEM = "TLACENI pokracuje po doplneni vzduchu";
static const char SD_EQUALIZE[] PROGMEM = "VYROVNANI tlaku lahvicky pres filtr";
static const char SD_FILL[]     PROGMEM = "DOPLNENI vzduchove strikacky na plny objem";
static const char SD_SALINE[]   PROGMEM = "PRIDANI fyziologickeho roztoku do lahvicky";

static bool isProcessState(State s) {
    return s >= ST_P1_PUSH_AIR && s <= ST_ITER_ADD_SALINE;
}

// Skládání řádku displeje bez snprintf. Vrací ukazatel na koncovou nulu.
static char *appendText(char *dst, const char *pgm) {
    strcpy_P(dst, pgm);
    return dst + strlen(dst);
}

static char *appendUint(char *dst, uint16_t v) {
    utoa(v, dst, 10);
    return dst + strlen(dst);
}

static char *appendTenths(char *dst, uint16_t tenths) {
    dst = appendUint(dst, tenths / 10);
    *dst++ = '.';
    *dst++ = (char)('0' + tenths % 10);
    *dst = '\0';
    return dst;
}

// Výpis "<text><hodnota>" na Serial - společná funkce šetří flash.
static void sayVal(const __FlashStringHelper *label, float v, uint8_t digits) {
    Serial.print(label);
    Serial.print(v, digits);
}

// Pod-kroky ST_P1_PUSH_AIR / ST_ITER_PUSH_AIR (phase_).
enum PushPhase : uint8_t {
    PP_AIR_VALVE = 0,    // vzduchový ventil S<->V (lahvička uzavřena, pacient izolován)
    PP_AIR_SETTLE,       // dojezd ventilu
    PP_VALVE_WAIT,       // ustálení po ventilu
    PP_REST,             // klid: kontrola lahvičky a výchozího stavu
    PP_PRECOMP,          // předtlak (pacient izolován)
    PP_PRECOMP_SETTLE,   // klid po předtlaku
    PP_OPEN_WAIT,        // pacient otevřen, detektor běží, klid před tlačením
    PP_RESUME_OPEN,      // po doplnění vzduchu: otevírání pacienta
    PP_RESUME_WAIT,      // ustálení po otevření
    PP_PUSH,             // tlačení, detektor běží
    PP_REFILL_CLOSE,     // stříkačka prázdná: pacient se zavírá, pak doplnění
    PP_STOPPED,          // kritická hladina: pacient se zavírá (povel už dán)
    PP_VENT,             // odtlakování lahvičky V<->F
    PP_VIAL_WAIT         // lahvička chybí: čeká se na obsluhu (Enter)
};

// ============================================================
//  Inicializace
// ============================================================

void PumpController::begin() {
    Serial.begin(BAUD_RATE);
    logInit();
    logEvent(LOG_BOOT);
    Wire.begin();
    // 100 kHz (standardní režim) - spolehlivější než 400 kHz na nepájeném
    // propojení (drátové propojky, prototyp). Až bude I2C sběrnice
    // pevně spojená (pájka/kvalitní konektory), lze zkusit zpět 400000UL.
    Wire.setClock(100000UL);

    // Motory zůstávají DISABLED až do stisku START (viz handleSetVolume) –
    // před tím se lahvička a stříkačky teprve osazují ručně.
    pinMode(PIN_STEPPER_EN, OUTPUT);
    digitalWrite(PIN_STEPPER_EN, STEPPER_DISABLED_LEVEL);

    loadValveAngles();
    servoTimerInit();
    // KRITICKÉ: ventily ihned do pracovních poloh – NE servo 0 stupňů!
    // Instalační pořadí: obsluha nejprve osadí OBA ventily a OBĚ stříkačky,
    // lahvička s jehlami přichází na řadu až jako úplně poslední krok. V
    // lahvičce proto nikdy nemůže vzniknout přetlak ani podtlak - připojuje
    // se jehlami do systému, který je už mechanicky hotový a otevřený do
    // atmosféry. Pacientský ventil zůstává v izolační poloze (bezpečnost
    // vůči pacientovi platí bez ohledu na pořadí instalace); vzduchový
    // ventil může rovnou sedět v poloze, kterou stejně potřebuje jako
    // první (S<->F, vyvětrání stříkačky) - žádný samostatný "purge" krok
    // navíc už není potřeba.
    patientValve_.begin(PIN_SERVO_PATIENT, angles_.patientIsolate);
    airValve_.begin(PIN_SERVO_AIR, angles_.airSyrToFilter);
    logEvent(LOG_VALVES_SAFE);

    float airStepsPerMl = STEPS_PER_MM / AIR_SYR_ML_PER_MM;
    float salStepsPerMl = STEPS_PER_MM / SAL_SYR_ML_PER_MM;
    airSyr_.begin(PIN_AIR_STEP, PIN_AIR_DIR, airStepsPerMl,
                  (uint32_t)(FLOW_S_PER_ML * 1000000.0f / airStepsPerMl),
                  AIR_DIR_PUSH_LEVEL);
    salSyr_.begin(PIN_SAL_STEP, PIN_SAL_DIR, salStepsPerMl,
                  (uint32_t)(FLOW_S_PER_ML * 1000000.0f / salStepsPerMl),
                  SAL_DIR_PUSH_LEVEL);

    if (!cap_.begin()) {
        Serial.println(F("# *** FDC1004 neodpovida"));
    }
    disp_.begin();
    kb_.begin();
    enc_.begin();
    ser_.begin();
    changeState(ST_INIT);
}

// Načtení úhlů ventilů z EEPROM; při prvním spuštění se uloží výchozí.
void PumpController::loadValveAngles() {
    if (EEPROM.read(EEPROM_ADDR_VALID_FLAG) == EEPROM_MAGIC_VALUE) {
        angles_.patientOpen     = EEPROM.read(EEPROM_ADDR_PATIENT_OPEN);
        angles_.patientIsolate  = EEPROM.read(EEPROM_ADDR_PATIENT_ISOLATE);
        angles_.airSyrToVial    = EEPROM.read(EEPROM_ADDR_AIR_SYR_TO_VIAL);
        angles_.airSyrToFilter  = EEPROM.read(EEPROM_ADDR_AIR_SYR_TO_FILT);
        angles_.airVialToFilter = EEPROM.read(EEPROM_ADDR_AIR_VIAL_TO_FILT);
    } else {
        angles_.patientOpen     = PATIENT_VALVE_OPEN_DEFAULT;
        angles_.patientIsolate  = PATIENT_VALVE_ISOLATE_DEFAULT;
        angles_.airSyrToVial    = AIR_VALVE_SYRINGE_TO_VIAL_DEFAULT;
        angles_.airSyrToFilter  = AIR_VALVE_SYRINGE_TO_FILTER_DEFAULT;
        angles_.airVialToFilter = AIR_VALVE_VIAL_TO_FILTER_DEFAULT;
        EEPROM.update(EEPROM_ADDR_PATIENT_OPEN, angles_.patientOpen);
        EEPROM.update(EEPROM_ADDR_PATIENT_ISOLATE, angles_.patientIsolate);
        EEPROM.update(EEPROM_ADDR_AIR_SYR_TO_VIAL, angles_.airSyrToVial);
        EEPROM.update(EEPROM_ADDR_AIR_SYR_TO_FILT, angles_.airSyrToFilter);
        EEPROM.update(EEPROM_ADDR_AIR_VIAL_TO_FILT, angles_.airVialToFilter);
        EEPROM.update(EEPROM_ADDR_VALID_FLAG, EEPROM_MAGIC_VALUE);
    }
}

// ============================================================
//  Hlavní smyčka
// ============================================================

void PumpController::update() {
    airSyr_.update();
    salSyr_.update();
    cap_.update();
    sampleFresh_ = cap_.takeSample();
    kb_.update();
    enc_.update();
    ser_.update();
    if (ser_.takeStepToggle()) {
        stepMode_ = !stepMode_;
        Serial.println(stepMode_ ? F("# Krokovani ZAP")
                                 : F("# Krokovani VYP"));
    }
    if (ser_.takeStatus()) {
        printStatus();
    }
    handleGlobalKeys();
    serviceLevel();
    if (cap_.frozen() && state_ >= ST_CALIBRATING && state_ <= ST_PAUSED
            && state_ != ST_COMPLETE) {
        sensorFault();
    }

    if (stepWait_ && state_ != ST_PAUSED) {
        // Krokovací režim: krok procesu začne až po Enteru. Čeká se jen
        // mezi kroky (pacient izolován, motory stojí).
        if (!takeConfirm()) {
            refreshDisplay();
            return;
        }
        stepWait_ = false;
    }

    switch (state_) {
        case ST_INIT:            handleInit();            break;
        case ST_WAIT_READY:      handleWaitReady();       break;
        case ST_CALIBRATING:     handleCalibrating();     break;
        case ST_P1_PUSH_AIR:     handlePushAir(true);     break;
        case ST_P1_EQUALIZE:     handleEqualize(true);    break;
        case ST_P1_FILL_AIR:     handleFillAir(true);     break;
        case ST_P1_ADD_SALINE:   handleAddSaline();       break;
        case ST_ITER_EQUALIZE:   handleEqualize(false);   break;
        case ST_ITER_FILL_AIR:   handleFillAir(false);    break;
        case ST_ITER_PUSH_AIR:   handlePushAir(false);    break;
        case ST_ITER_ADD_SALINE: handleAddSaline();       break;
        case ST_PAUSED:          handlePaused();          break;
        default:                 break;   // terminální stavy: nic
    }
    refreshDisplay();
}

// ============================================================
//  Pomocné funkce
// ============================================================

void PumpController::changeState(State s) {
    State prev = state_;
    state_ = s;
    phase_ = 0;
    phaseT_ = millis();
    dispDirty_ = true;
    promptShown_ = false;
    stepWait_ = false;
    announceState(prev);
}

// Vypíše, do jakého kroku systém přechází. V krokovacím režimu se mezi
// dvěma kroky procesu čeká na Enter (stepWait_, viz update()).
void PumpController::announceState(State prev) {
    const char *desc = NULL;
    switch (state_) {
        case ST_P1_PUSH_AIR:
        case ST_ITER_PUSH_AIR:   desc = pushResume_ ? SD_RESUME : SD_PUSH; break;
        case ST_P1_EQUALIZE:
        case ST_ITER_EQUALIZE:   desc = SD_EQUALIZE; break;
        case ST_P1_FILL_AIR:
        case ST_ITER_FILL_AIR:   desc = SD_FILL; break;
        case ST_P1_ADD_SALINE:
        case ST_ITER_ADD_SALINE: desc = SD_SALINE; break;
        default: break;
    }
    if (desc == NULL) {
        char buf[22];
        strncpy_P(buf, (const char *)pgm_read_ptr(&STATE_NAMES[state_]), sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        Serial.print(F("# ===== "));
        Serial.println(buf);
        return;
    }
    if (stepMode_ && isProcessState(prev)) {
        stepWait_ = true;
        ser_.flush();
        Serial.print(F(">>> "));
        Serial.print(iter_ == 0 ? F("[Faze 1] ") : F("[Iterace] "));
        Serial.print(F("Dalsi krok: "));
        Serial.print((const __FlashStringHelper *)desc);
        Serial.println(F(". Enter = pokracovat"));
    } else {
        sayPrefix();
        Serial.print(F("===== "));
        Serial.println((const __FlashStringHelper *)desc);
    }
}

void PumpController::sayPrefix() {
    if (iter_ == 0) {
        Serial.print(F("# [Faze 1] "));
    } else {
        uint8_t shown = (iter_ > ITER_COUNT) ? (uint8_t)ITER_COUNT : iter_;
        Serial.print(F("# [Iterace "));
        Serial.print(shown);
        Serial.print('/');
        Serial.print((uint8_t)ITER_COUNT);
        Serial.print(F("] "));
    }
}

bool PumpController::takeConfirm() {
    bool a = ser_.takeConfirm();
    bool b = kb_.pressed(KEY_START);
    return a || b;
}

// Výzva obsluze: vypíše se jednou, pak se čeká na Enter (nebo START).
bool PumpController::confirmed(const __FlashStringHelper *prompt) {
    if (!promptShown_) {
        ser_.flush();
        Serial.print(F(">>> "));
        Serial.print(prompt);
        Serial.println(F(" Enter = pokracovat"));
        promptShown_ = true;
    }
    if (takeConfirm()) {
        promptShown_ = false;
        return true;
    }
    return false;
}

void PumpController::printStatus() {
    char buf[22];
    strncpy_P(buf, (const char *)pgm_read_ptr(&STATE_NAMES[state_]), sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    Serial.print(F("# STAV: ")); Serial.print(buf);
    Serial.print(F(" (stav ")); Serial.print((uint8_t)state_);
    Serial.print(F(", krok ")); Serial.print(phase_);
    Serial.print(F(")  iterace ")); Serial.print(iter_);
    Serial.print('/'); Serial.println((uint8_t)ITER_COUNT);
    sayVal(F("#   vzduch ve strikacce "), airMl_, 1);
    sayVal(F(" ml, roztok "), salMl_, 1);
    Serial.println(F(" ml"));
    sayVal(F("#   C1="), cap_.lowerPf(), 3);
    sayVal(F(" C3="), cap_.upperPf(), 3);
    sayVal(F(" pF  suche C1="), cap_.dryLower(), 3);
    sayVal(F(" C3="), cap_.dryUpper(), 3);
    Serial.print(F("  detekce ")); Serial.print(lvlFeed_ ? F("BEZI") : F("stoji"));
    Serial.print(F("  krokovani ")); Serial.println(stepMode_ ? F("ZAP") : F("VYP"));
}

void PumpController::safeValves() {
    lvlFeed_ = false;                    // pacient izolován - konec vzorků detektoru
    patientValve_.moveTo(angles_.patientIsolate);
    airValve_.moveTo(angles_.airVialToFilter);
}

// Volá se právě jednou při stisku START (viz handleSetVolume) – od tohoto
// okamžiku musí motory držet i za PAUSE, povoleny zůstávají po celý proces.
void PumpController::enableSteppers() {
    digitalWrite(PIN_STEPPER_EN, STEPPER_ENABLED_LEVEL);
}

// Doplňková HW pojistka: fyzicky odpojí výstupy obou driverů (vysoká
// impedance), i kdyby v kroku motoru zůstala chyba v generování pulzů.
// Platí jen pro dokončení procesu nebo STOP/ALARM/ERROR (viz volání).
void PumpController::disableSteppers() {
    digitalWrite(PIN_STEPPER_EN, STEPPER_DISABLED_LEVEL);
}

bool PumpController::valvesSettled() const {
    return patientValve_.settled() && airValve_.settled();
}

bool PumpController::inputPressed(Key k) {
    bool a = kb_.pressed(k);
    bool b = ser_.pressed(k);
    return a || b;
}

// STOP a PAUSE/PLAY fungují globálně nezávisle na stavu.
void PumpController::handleGlobalKeys() {
    if (inputPressed(KEY_STOP)) {
        if (state_ != ST_EMERGENCY_STOP && state_ != ST_ALARM_EXCESS_AIR) {
            airSyr_.stop();
            salSyr_.stop();
            safeValves();
            disableSteppers();
            logEvent(LOG_EMERGENCY_STOP);
            changeState(ST_EMERGENCY_STOP);
        }
        return;
    }
    if (inputPressed(KEY_PAUSE)) {
        if (state_ == ST_PAUSED) {
            resumeSystem();
        } else if (state_ >= ST_P1_PUSH_AIR && state_ <= ST_ITER_ADD_SALINE) {
            pauseSystem(false);
        }
    }
}

void PumpController::pauseSystem(bool byNoFlow) {
    savedState_ = state_;
    savedPhase_ = phase_;
    pausedByNoFlow_ = byNoFlow;
    pauseRefVert_ = cap_.vertRaw();
    airSyr_.pause();
    salSyr_.pause();
    // Ventily zůstávají v aktuální poloze – PAUSE pokračuje ve stejném kroku
    if (byNoFlow) {
        Serial.println(F("# Hladina 3 s neklesa (svisla el.) - pauza, pokracuje sama"));
        logEvent(LOG_NO_FLOW);
    }
    logEvent(LOG_PAUSED);
    Serial.println(F(">>> PAUZA: p = pokracovat, x = STOP"));
    state_ = ST_PAUSED;
    dispDirty_ = true;
}

void PumpController::resumeSystem() {
    state_ = savedState_;
    phase_ = savedPhase_;
    phaseT_ = millis();
    airSyr_.resume();
    salSyr_.resume();
    armFlowWatch();
    logEvent(LOG_RESUMED);
    dispDirty_ = true;
}

// Spustí tlačení vzduchu – celý obsah stříkačky. Tlačení ukončí detekce
// kapacity; dojde-li vzduch dřív, stříkačka se doplní. Objem vzduchu
// o hladině nic neříká (stlačitelnost, tlak v systému a u pacienta).
void PumpController::startAirPush() {
    float pushable = airMl_;
    sayPrefix();
    Serial.print(F("Tlaceni, ve strikacce "));
    Serial.print(pushable, 1);
    Serial.println(F(" ml"));
    logEvent(LOG_AIR_PUSH);
    dispDirty_ = true;
    refreshDisplay();                    // překreslit, dokud motor stojí
    airSyr_.startMove(pushable, true);
    pushActive_ = true;
    armFlowWatch();
}

// Po skončení pohybu vzduchové stříkačky aktualizuje obsah stříkačky –
// slouží jen k tomu, aby nasátí nikdy nepřekročilo její kapacitu.
void PumpController::finishAirMove(bool push) {
    float moved = airSyr_.movedMl();
    if (push) {
        pushActive_ = false;
        airMl_ -= moved;
    } else {
        airMl_ += moved;
    }
    if (airMl_ < 0.0f) {
        airMl_ = 0.0f;
    }
    if (airMl_ > VOL_AIR_SYRINGE_MAX_ML) {
        airMl_ = VOL_AIR_SYRINGE_MAX_ML;
    }
}

void PumpController::armFlowWatch() {
    flowRefRaw_ = cap_.vertRaw();
    flowT_ = millis();
}

// Hlídání poklesu hladiny: svislá elektroda musí klesat, jinak stagnace.
bool PumpController::flowStalled() {
    if (!cap_.calibrated()) {
        return false;
    }
    if (cap_.vertRaw() < flowRefRaw_ - cap_.flowEpsRaw()) {
        armFlowWatch();                  // hladina klesá – posun reference
        return false;
    }
    return (millis() - flowT_) > NO_FLOW_TIMEOUT_MS;
}

// Detektor dostává vzorky jen s otevřeným pacientem - i v pauze, protože
// hladina se může hýbat i se stojícím motorem (zbytkový přetlak).
void PumpController::serviceLevel() {
    if (!lvlFeed_ || !sampleFresh_) {
        return;
    }
    LevelResult r = lvl_.feed(cap_.lowerPf(), cap_.upperPf(), cap_.vertPf());
    uint8_t fit = lvl_.fitFlags();
    if (fit & ~fitLogged_) {
        fitLogged_ |= fit;
        logEvent(LOG_FIT_WARN);          // jen informace, nezastavuje
        sayPrefix();
        Serial.print(F("Mimo sablonu (info):"));
        if (fit & LVL_FIT_C1_EARLY) Serial.print(F(" C1 klesa predcasne"));
        if (fit & LVL_FIT_NO_RISE)  Serial.print(F(" C1-C3 neroste"));
        if (fit & LVL_FIT_MAX_OFF)  Serial.print(F(" max mimo plato"));
        if (fit & LVL_FIT_C2_FLAT)  Serial.print(F(" *** C2 neklesla"));
        Serial.println();
    }
    if (r != LVL_NONE) {
        onLevelResult(r);
    }
}

// Zastavení: motor stojí a pacient se zavírá OKAMŽITĚ (bez čekání na
// dotečení - test 2), teprve potom se lahvička odtlakuje (PP_STOPPED).
void PumpController::onLevelResult(LevelResult r) {
    airSyr_.stop();
    if (pushActive_) {
        finishAirMove(true);
    }
    lvlFeed_ = false;
    pushResume_ = false;
    patientValve_.moveTo(angles_.patientIsolate);
    if (state_ == ST_PAUSED) {           // zastavení přišlo v pauze
        state_ = savedState_;
        salSyr_.resume();
    }
    sayPrefix();
    switch (r) {
        case LVL_STOP_RATIO:    Serial.print(F("KRITICKA HLADINA (podil)")); break;
        case LVL_STOP_TEMPLATE: Serial.print(F("KRITICKA HLADINA (sablona)")); break;
        case LVL_STOP_PEAK:     Serial.print(F("KRITICKA HLADINA (vrchol)")); break;
        default:                Serial.print(F("*** SKOK C1 - porucha")); break;
    }
    Serial.println(F(" - pacient UZAVREN"));
    sayVal(F("#   C1="), lvl_.winC1(), 3);
    sayVal(F(" C3="), lvl_.winC3(), 3);
    sayVal(F(" pF  C3/C1="), lvl_.winC3() / lvl_.winC1(), 3);
    if (lvl_.maxC3() > 0.0f) {           // maximum existuje až s plným oknem
        sayVal(F("  C3/max="), lvl_.winC3() / lvl_.maxC3(), 3);
    }
    sayVal(F("  vzduch ve strikacce "), airMl_, 1);
    Serial.println(F(" ml"));
    sayVal(F("#   kontrola C2: pokles pod maximum "), lvl_.maxC2() - lvl_.winC2(), 3);
    Serial.println((lvl_.fitFlags() & LVL_FIT_C2_FLAT) ? F(" pF *** HLADINA NEKLESLA?") : F(" pF OK"));
    if (r == LVL_FAULT_STEP) {
        safeValves();
        logEvent(LOG_LEVEL_FAULT);
        changeState(ST_ERROR);
        return;
    }
    logEvent(LOG_CRITICAL_LEVEL);
    phase_ = PP_STOPPED;
    dispDirty_ = true;
}

void PumpController::sensorFault() {
    airSyr_.stop();
    salSyr_.stop();
    safeValves();
    Serial.println(cap_.present() ? F("# *** FDC1004 nemeri - zkontroluj konektory elektrod")
                                  : F("# *** FDC1004 neodpovida (I2C)"));
    logEvent(LOG_SENSOR_FAULT);
    changeState(ST_ERROR);
}

// Předtlak: plyn v systému (prázdná lahvička = nejvíc plynu, konzervativní
// směr) krát poměr nejhoršího tlaku v hadičce k nízkému tlaku okolí.
float PumpController::precompMl() const {
    float gasMl = PRECOMP_VIAL_ML + airMl_ + PRECOMP_DEAD_ML;
    float dpKpa = (PRECOMP_PATIENT_CM + PRECOMP_VENOUS_CM) * PRECOMP_KPA_PER_CMH2O;
    float ml = gasMl * dpKpa / PRECOMP_P_ATM_KPA;
    return (ml < airMl_) ? ml : airMl_;
}

// Klid před tlačením (lahvička uzavřená, pacient izolován): lahvička musí
// být na místě (prstence nad suchou hodnotou) a hladina nad horním
// prstencem (C3/C1). Chybějící lahvička se čeká - obsluha ji teprve
// vkládá (vkládá se až po kalibraci).
void PumpController::restStartCheck() {
    if (sampleFresh_) {
        restSumC1_ += cap_.lowerPf();
        restSumC3_ += cap_.upperPf();
        restN_++;
    }
    if (millis() - phaseT_ < LVL_REST_MS || restN_ == 0) {
        return;
    }
    float c1 = restSumC1_ / restN_;
    float c3 = restSumC3_ / restN_;
    restSumC1_ = restSumC3_ = 0.0f;
    restN_ = 0;
    phaseT_ = millis();
    sayPrefix();
    sayVal(F("Klid: C1="), c1, 3);
    sayVal(F(" C3="), c3, 3);
    sayVal(F(" pF (nad suchou +"), c1 - cap_.dryLower(), 2);
    sayVal(F(" / +"), c3 - cap_.dryUpper(), 2);
    Serial.print(F("), C3/C1=")); Serial.println(c3 / c1, 3);
    bool vial = c1 >= cap_.dryLower() + LVL_VIAL_MIN_PF
             && c3 >= cap_.dryUpper() + LVL_VIAL_MIN_PF;
    if (!vial) {
        logEvent(LOG_VIAL_MISSING);
        phase_ = PP_VIAL_WAIT;           // obsluha vloží lahvičku, pak znovu klid
        return;
    }
    if (c3 < LVL_START_Q_MIN * c1) {
        sayPrefix();
        Serial.print(F("Hladina POD hornim prstencem (C3/C1 < "));
        Serial.print(LVL_START_Q_MIN, 2);
        Serial.println(F(") - STOP"));
        safeValves();
        logEvent(LOG_LEVEL_LOW);
        changeState(ST_ERROR);
        return;
    }
    float pre = precompMl();
    sayPrefix();
    Serial.print(F("Lahvicka OK. Predtlak "));
    Serial.print(pre, 2);
    Serial.println(F(" ml (pacient uzavren)"));
    logEvent(LOG_PRECOMPRESS);
    airSyr_.startMove(pre, true);
    pushActive_ = true;
    phase_ = PP_PRECOMP;
}

// ============================================================
//  Obsluhy stavů – příprava
// ============================================================

void PumpController::handleInit() {
    if (!valvesSettled()) {
        return;
    }
    if (confirmed(F("PRIPRAVA: ventily, strikacky (vzduch PLNA 10 ml). Studna PRAZDNA, bez lahvicky."))) {
        logEvent(LOG_WAIT_READY);
        changeState(ST_WAIT_READY);
    }
}

// START (Enter) spustí kalibraci. Objem lahvičky se nezadává - na řízení
// nemá vliv (tlačení ukončuje detekce hladiny, dávky roztoku jsou pevné).
void PumpController::handleWaitReady() {
    if (confirmed(F("KALIBRACE - studna PRAZDNA (bez lahvicky)."))) {
        enableSteppers();             // od START platí ENABLED po celý proces (i PAUSE)
        logEvent(LOG_CALIBRATING);
        changeState(ST_CALIBRATING);
    }
}

// Kalibrace = suché hodnoty prstenců v prázdné studně. Lahvička se vkládá
// až potom (vzduchová stříkačka sedí v S<->F už od zapnutí).
void PumpController::handleCalibrating() {
    if (phase_ == 0) {
        if (!cap_.present()) {
            sensorFault();
            return;
        }
        Serial.print(F("# Kalibrace "));
        Serial.print(CALIBRATION_MS / 1000);
        Serial.println(F(" s, nesahat"));
        cap_.startCalibration();
        phase_ = 1;
    } else if (phase_ == 1) {
        if (!cap_.calibrating()) {
            logEvent(LOG_CALIB_DONE);
            sayVal(F("# Suche hodnoty: C1="), cap_.dryLower(), 3);
            sayVal(F(" C3="), cap_.dryUpper(), 3);
            Serial.println(F(" pF"));
            phase_ = 2;
        }
    } else if (confirmed(F("VLOZ LAHVICKU s jehlami do studny."))) {
        changeState(ST_P1_PUSH_AIR);
    }
}

// ============================================================
//  Obsluhy stavů – aplikační proces
// ============================================================

// Tlačení vzduchu do lahvičky (Fáze 1 i iterace – shodná logika).
// Postup převzatý ze sketche tools/level_stop (testy 9-12): klid s
// kontrolou -> předtlak -> otevření pacienta -> tlačení do zastavení
// detektorem. Stříkačka se doplňuje BEZ odvzdušnění lahvičky (vzduchový
// ventil S<->F zaslepí rameno lahvičky) a detektor si drží stav.
void PumpController::handlePushAir(bool phase1) {
    switch (phase_) {
        case PP_AIR_VALVE:
            logEvent(LOG_VALVE_AIR_MOVE);
            airValve_.moveTo(angles_.airSyrToVial);
            phase_ = PP_AIR_SETTLE;
            break;
        case PP_AIR_SETTLE:
            if (airValve_.settled()) {
                phaseT_ = millis();
                phase_ = PP_VALVE_WAIT;
            }
            break;
        case PP_VALVE_WAIT:
            if (millis() - phaseT_ < LVL_VALVE_SETTLE_MS) {
                break;
            }
            phaseT_ = millis();
            if (pushResume_) {           // po doplnění vzduchu: rovnou otevřít
                logEvent(LOG_VALVE_PATIENT_MOVE);
                patientValve_.moveTo(angles_.patientOpen);
                phase_ = PP_RESUME_OPEN;
            } else {
                restSumC1_ = restSumC3_ = 0.0f;
                restN_ = 0;
                sayPrefix();
                Serial.println(F("Klid 5 s (vzduch S-V): kontrola lahvicky"));
                phase_ = PP_REST;
            }
            break;
        case PP_REST:
            restStartCheck();
            break;
        case PP_VIAL_WAIT:
            if (confirmed(F("Lahvicka CHYBI / prazdna - vloz ji."))) {
                restSumC1_ = restSumC3_ = 0.0f;
                restN_ = 0;
                phaseT_ = millis();
                phase_ = PP_REST;
            }
            break;
        case PP_PRECOMP:
            if (airSyr_.idle()) {
                finishAirMove(true);
                phaseT_ = millis();
                phase_ = PP_PRECOMP_SETTLE;
            }
            break;
        case PP_PRECOMP_SETTLE:
            if (millis() - phaseT_ >= PRECOMP_SETTLE_MS) {
                // Detektor běží od povelu k otevření (jako ve sketchi) -
                // přebytek předtlaku teče k pacientovi hned.
                lvl_.reset();
                fitLogged_ = 0;
                refills_ = 0;
                logEvent(LOG_VALVE_PATIENT_MOVE);
                patientValve_.moveTo(angles_.patientOpen);
                lvlFeed_ = true;
                sayPrefix();
                Serial.println(F("Pacient OTEVREN, detekce bezi, klid 5 s"));
                phaseT_ = millis();
                phase_ = PP_OPEN_WAIT;
            }
            break;
        case PP_OPEN_WAIT:
            if (millis() - phaseT_ >= LVL_OPEN_SETTLE_MS) {
                startAirPush();
                phase_ = PP_PUSH;
            }
            break;
        case PP_RESUME_OPEN:
            if (patientValve_.settled()) {
                phaseT_ = millis();
                phase_ = PP_RESUME_WAIT;
            }
            break;
        case PP_RESUME_WAIT:
            if (millis() - phaseT_ >= LVL_VALVE_SETTLE_MS) {
                pushResume_ = false;
                lvl_.flowBreak();
                lvlFeed_ = true;
                sayPrefix();
                Serial.println(F("Pacient znovu OTEVREN"));
                startAirPush();
                phase_ = PP_PUSH;
            }
            break;
        case PP_PUSH:                    // zastavení řeší serviceLevel()
            if (airSyr_.idle()) {        // stříkačka prázdná – doplnit
                finishAirMove(true);
                refills_++;
                if (refills_ > MAX_AIR_REFILLS) {
                    // Motory zůstávají ENABLED (drží polohu) - DISABLE je
                    // vyhrazeno jen pro ST_COMPLETE a ST_EMERGENCY_STOP.
                    safeValves();
                    Serial.println(F("# *** Limit doplneni vzduchu - mozna netesnost"));
                    logEvent(LOG_ALARM_EXCESS_AIR);
                    changeState(ST_ALARM_EXCESS_AIR);
                } else {
                    // Pacient se zavírá JAKO PRVNÍ, lahvička zůstává pod
                    // tlakem (bez odvzdušnění), detektor si drží stav.
                    lvlFeed_ = false;
                    lvl_.flowBreak();
                    sayPrefix();
                    Serial.print(F("Strikacka prazdna - pacient UZAVREN, doplneni "));
                    Serial.print(refills_); Serial.print('/'); Serial.print(MAX_AIR_REFILLS);
                    Serial.println(F(" (lahvicka pod tlakem)"));
                    logEvent(LOG_VALVE_PATIENT_MOVE);
                    patientValve_.moveTo(angles_.patientIsolate);
                    pushResume_ = true;
                    phase_ = PP_REFILL_CLOSE;
                }
            } else if (NO_FLOW_WATCH && flowStalled()) {
                pauseSystem(true);
            }
            break;
        case PP_REFILL_CLOSE:
            if (patientValve_.settled()) {
                changeState(phase1 ? ST_P1_FILL_AIR : ST_ITER_FILL_AIR);
            }
            break;
        case PP_STOPPED:                 // pacient už zavřen - odtlakovat lahvičku
            if (patientValve_.settled()) {
                sayPrefix();
                Serial.println(F("Odtlakovani lahvicky (V-F)"));
                logEvent(LOG_VALVE_AIR_MOVE);
                airValve_.moveTo(angles_.airVialToFilter);
                phase_ = PP_VENT;
            }
            break;
        case PP_VENT:
            if (airValve_.settled()) {
                if (!phase1 && iter_ > ITER_COUNT) {
                    // Závěrečné vytlačení: po POSLEDNÍM doplnění roztoku se
                    // ještě jednou projde EQUALIZE -> FILL_AIR -> PUSH_AIR a
                    // tento objem se vytlačí pacientovi. Další roztok už se
                    // nepřidává - procedura tímto končí.
                    safeValves();
                    disableSteppers();
                    logEvent(LOG_COMPLETE);
                    changeState(ST_COMPLETE);
                } else {
                    changeState(phase1 ? ST_P1_ADD_SALINE : ST_ITER_ADD_SALINE);
                }
            }
            break;
    }
}

// Vyrovnání tlaku lahvičky s atmosférou přes filtr (V<->F).
void PumpController::handleEqualize(bool phase1) {
    switch (phase_) {
        case 0:
            sayPrefix();
            Serial.println(F("Vyrovnani tlaku (V-F, 3 s)"));
            logEvent(LOG_EQUALIZE);
            airValve_.moveTo(angles_.airVialToFilter);
            phase_ = 1;
            break;
        case 1:
            if (airValve_.settled()) {
                phaseT_ = millis();
                phase_ = 2;
            }
            break;
        case 2:
            if (millis() - phaseT_ >= EQUALIZE_TIME_MS) {
                changeState(phase1 ? ST_P1_FILL_AIR : ST_ITER_FILL_AIR);
            }
            break;
    }
}

// Nasátí vzduchu z atmosféry do stříkačky (S<->F).
void PumpController::handleFillAir(bool phase1) {
    switch (phase_) {
        case 0:
            logEvent(LOG_VALVE_AIR_MOVE);
            airValve_.moveTo(angles_.airSyrToFilter);
            phase_ = 1;
            break;
        case 1: {
            if (!airValve_.settled()) {
                break;
            }
            // Vždy doplnit na plnou stříkačku, nikdy víc, než pojme
            float target = VOL_AIR_SYRINGE_MAX_ML - airMl_;
            if (target < 0.0f) {
                target = 0.0f;
            }
            sayPrefix();
            sayVal(F("Nasati "), target, 1);
            Serial.println(F(" ml vzduchu (S-F)"));
            logEvent(LOG_AIR_FILL);
            dispDirty_ = true;
            refreshDisplay();            // překreslit, dokud motor stojí
            // Nasávání jde rychleji - nic se netlačí do pacienta
            airSyr_.startMove(target, false, AIR_FILL_SPEED_FACTOR);
            phase_ = 2;
            break;
        }
        case 2:
            if (airSyr_.idle()) {
                finishAirMove(false);
                // Filtr klade odpor - tlak ve stříkačce/hadičce se po
                // nasátí ještě chvíli vyrovnává, stejná prodleva jako
                // u ST_ITER_EQUALIZE.
                phaseT_ = millis();
                phase_ = 3;
            }
            break;
        case 3:
            if (millis() - phaseT_ >= EQUALIZE_TIME_MS) {
                changeState(phase1 ? ST_P1_PUSH_AIR : ST_ITER_PUSH_AIR);
            }
            break;
    }
}

// Přidání dávky fyziologického roztoku do lahvičky.
void PumpController::handleAddSaline() {
    switch (phase_) {
        case 0:
            // Tolerance 0,1 ml kryje zaokrouhlení kroků; stříkačka se plní
            // reálně o něco víc, takže skutečná zásoba je vyšší než bilance.
            if (salMl_ + 0.1f < VOL_SAL_ITER_ML) {
                // Motory zůstávají ENABLED (drží polohu) - DISABLE je
                // vyhrazeno jen pro ST_COMPLETE a ST_EMERGENCY_STOP.
                safeValves();
                Serial.println(F("# *** Malo roztoku"));
                logEvent(LOG_ERROR);
                changeState(ST_ERROR);
                break;
            }
            sayPrefix();
            sayVal(F("Pridani "), VOL_SAL_ITER_ML, 2);
            sayVal(F(" ml roztoku, zbyva "), salMl_, 1);
            Serial.println(F(" ml"));
            logEvent(LOG_SALINE_PUSH);
            dispDirty_ = true;
            refreshDisplay();
            salSyr_.startMove(VOL_SAL_ITER_ML, true);
            phase_ = 1;
            break;
        case 1:
            if (salSyr_.idle()) {
                salMl_ -= salSyr_.movedMl();
                refills_ = 0;
                iter_++;
                // I po POSLEDNÍM doplnění roztoku se pokračuje na
                // ST_ITER_EQUALIZE - i tento objem se má ještě vytlačit
                // pacientovi. Procedura se ukončí až v handlePushAir
                // (case 6), kde se pozná, že iter_ > ITER_COUNT, a další
                // roztok se už nepřidává.
                logEvent(LOG_ITER_START);
                if (state_ == ST_P1_ADD_SALINE && stepMode_) {
                    // Fáze 1 potvrzena krok po kroku, iterace běží samy
                    stepMode_ = false;
                    Serial.println(F("# Iterace bezi automaticky (k = krokovani)"));
                }
                changeState(ST_ITER_EQUALIZE);
            }
            break;
    }
}

// Pozastaveno: PLAY řeší handleGlobalKeys; při zástavě průtoku se
// pokračuje automaticky, jakmile hladina znovu začne klesat.
void PumpController::handlePaused() {
    if (pausedByNoFlow_
            && cap_.vertRaw() < pauseRefVert_ - cap_.flowEpsRaw()) {
        resumeSystem();
    }
}

// ============================================================
//  Displej
// ============================================================

// Překresluje jen při stojících motorech – kreslení řádku trvá ~3 ms
// a nesmí rozhodit časování kroků.
void PumpController::refreshDisplay() {
    bool motorsBusy = (!airSyr_.idle() && !airSyr_.paused())
                   || (!salSyr_.idle() && !salSyr_.paused());
    if (motorsBusy) {
        return;
    }
    uint32_t now = millis();
    if (!dispDirty_ && (now - lastDisplay_) < DISPLAY_REFRESH_MS) {
        return;
    }
    lastDisplay_ = now;
    dispDirty_ = false;

    char buf[22];
    strncpy_P(buf, (const char *)pgm_read_ptr(&STATE_NAMES[state_]), sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    disp_.drawRow(0, buf);

    logCopyMsg(logLastMsg(), buf, sizeof(buf));
    disp_.drawRow(2, buf);

    // Bez snprintf (šetří ~1 kB flash); obsah displeje se ještě doladí.
    char *e = appendText(buf, PSTR("Roztok: "));
    e = appendTenths(e, (uint16_t)(salMl_ * 10.0f + 0.5f));
    appendText(e, PSTR(" ml"));
    disp_.drawRow(4, buf);

    if (iter_ == 0) {
        e = appendText(buf, PSTR("Faze 1  Vz:"));
        e = appendTenths(e, (uint16_t)(airMl_ * 10.0f + 0.5f));
        appendText(e, PSTR(" ml"));
    } else {
        // Při závěrečném vytlačení je iter_ o 1 vyšší než ITER_COUNT -
        // na displeji se ořízne, aby neukazoval např. "12/11".
        uint8_t shown = (iter_ > ITER_COUNT) ? (uint8_t)ITER_COUNT : iter_;
        e = appendText(buf, PSTR("Iterace: "));
        e = appendUint(e, shown);
        *e++ = '/';
        appendUint(e, (uint8_t)ITER_COUNT);
    }
    disp_.drawRow(6, buf);
}
