#include "state_machine.h"
#include <Wire.h>
#include <EEPROM.h>

// Názvy stavů pro OLED (ASCII, max 21 znaků).
static const char SN_INIT[]     PROGMEM = "Inicializace";
static const char SN_READY[]    PROGMEM = "Priprava systemu";
static const char SN_VOLUME[]   PROGMEM = "Volba objemu";
static const char SN_CALIB[]    PROGMEM = "Kalibrace";
static const char SN_P1[]       PROGMEM = "Faze 1";
static const char SN_ITER[]     PROGMEM = "Iterace";
static const char SN_DONE[]     PROGMEM = "Dokonceno";
static const char SN_PAUSED[]   PROGMEM = "POZASTAVENO";
static const char SN_ALARM[]    PROGMEM = "ALARM VZDUCH";
static const char SN_ESTOP[]    PROGMEM = "NOUZOVY STOP";
static const char SN_ERROR[]    PROGMEM = "CHYBA";

static const char *const STATE_NAMES[ST_STATE_COUNT] PROGMEM = {
    SN_INIT, SN_READY, SN_VOLUME, SN_CALIB,
    SN_P1, SN_P1, SN_P1, SN_P1,
    SN_ITER, SN_ITER, SN_ITER, SN_ITER,
    SN_DONE, SN_PAUSED, SN_ALARM, SN_ESTOP, SN_ERROR
};

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
    PP_VENT              // odtlakování lahvičky V<->F
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
        Serial.println(F("VAROVANI: FDC1004 neodpovida!"));
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
    handleGlobalKeys();
    serviceLevel();
    if (cap_.frozen() && state_ >= ST_CALIBRATING && state_ <= ST_PAUSED
            && state_ != ST_COMPLETE) {
        sensorFault();
    }

    switch (state_) {
        case ST_INIT:            handleInit();            break;
        case ST_WAIT_READY:      handleWaitReady();       break;
        case ST_SET_VOLUME:      handleSetVolume();       break;
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
    state_ = s;
    phase_ = 0;
    phaseT_ = millis();
    dispDirty_ = true;
#if DEBUG
    Serial.print(F("Stav: "));
    Serial.println((uint8_t)s);
#endif
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

int8_t PumpController::inputDelta() {
    return enc_.takeDelta() + ser_.takeDelta();
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
        logEvent(LOG_NO_FLOW);
    }
    logEvent(LOG_PAUSED);
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
    LevelResult r = lvl_.feed(cap_.lowerPf(), cap_.upperPf());
    uint8_t fit = lvl_.fitFlags();
    if (fit & ~fitLogged_) {
        fitLogged_ |= fit;
        logEvent(LOG_FIT_WARN);          // jen informace, nezastavuje
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
#if DEBUG
    Serial.print(F("Hladina: vysledek ")); Serial.print((uint8_t)r);
    Serial.print(F(" C1=")); Serial.print(lvl_.winC1(), 3);
    Serial.print(F(" C3=")); Serial.print(lvl_.winC3(), 3);
    Serial.print(F(" C3/max=")); Serial.println(lvl_.winC3() / lvl_.maxC3(), 3);
#endif
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
#if DEBUG
    Serial.print(F("Klid: C1=")); Serial.print(c1, 3);
    Serial.print(F(" C3=")); Serial.print(c3, 3);
    Serial.print(F(" C3/C1=")); Serial.println(c3 / c1, 3);
#endif
    bool vial = c1 >= cap_.dryLower() + LVL_VIAL_MIN_PF
             && c3 >= cap_.dryUpper() + LVL_VIAL_MIN_PF;
    if (!vial) {
        if (!vialMissingLogged_) {
            vialMissingLogged_ = true;
            logEvent(LOG_VIAL_MISSING);
        }
        return;                          // měřit znovu, dokud lahvička není
    }
    vialMissingLogged_ = false;
    if (c3 < LVL_START_Q_MIN * c1) {
        safeValves();
        logEvent(LOG_LEVEL_LOW);
        changeState(ST_ERROR);
        return;
    }
    logEvent(LOG_PRECOMPRESS);
    airSyr_.startMove(precompMl(), true);
    pushActive_ = true;
    phase_ = PP_PRECOMP;
}

// ============================================================
//  Obsluhy stavů – příprava
// ============================================================

void PumpController::handleInit() {
    if (valvesSettled()) {
        logEvent(LOG_WAIT_READY);
        changeState(ST_WAIT_READY);
    }
}

void PumpController::handleWaitReady() {
    if (inputPressed(KEY_10ML)) {
        is20ml_ = false;
        volumeDml_ = 100;
        logEvent(LOG_SET_VOLUME);
        changeState(ST_SET_VOLUME);
    } else if (inputPressed(KEY_20ML)) {
        is20ml_ = true;
        volumeDml_ = 200;
        logEvent(LOG_SET_VOLUME);
        changeState(ST_SET_VOLUME);
    }
}

void PumpController::handleSetVolume() {
    int8_t d = inputDelta();
    if (d != 0) {
        int16_t v = (int16_t)volumeDml_ + d * VOL_FINE_STEP_DML;
        int16_t lo = is20ml_ ? VOL_FINE_MIN_20ML_DML : VOL_FINE_MIN_10ML_DML;
        int16_t hi = is20ml_ ? VOL_FINE_MAX_20ML_DML : VOL_FINE_MAX_10ML_DML;
        if (v < lo) v = lo;
        if (v > hi) v = hi;
        volumeDml_ = (uint16_t)v;
        dispDirty_ = true;
    }
    if (inputPressed(KEY_START)) {
        enableSteppers();             // od START platí ENABLED po celý proces (i PAUSE)
        logEvent(LOG_CALIBRATING);
        changeState(ST_CALIBRATING);
    }
}

// Vzduchová stříkačka sedí v S<->F (vyvětrána do atmosféry) už od zapnutí
// (viz begin()), takže tu není potřeba žádný samostatný krok navíc -
// kalibrace přechází rovnou do Fáze 1.
void PumpController::handleCalibrating() {
    if (phase_ == 0) {
        if (!cap_.present()) {
            sensorFault();
            return;
        }
        cap_.startCalibration();
        phase_ = 1;
    } else if (!cap_.calibrating()) {
        logEvent(LOG_CALIB_DONE);
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
                phase_ = PP_REST;
            }
            break;
        case PP_REST:
            restStartCheck();
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
                    logEvent(LOG_ALARM_EXCESS_AIR);
                    changeState(ST_ALARM_EXCESS_AIR);
                } else {
                    // Pacient se zavírá JAKO PRVNÍ, lahvička zůstává pod
                    // tlakem (bez odvzdušnění), detektor si drží stav.
                    lvlFeed_ = false;
                    lvl_.flowBreak();
                    logEvent(LOG_VALVE_PATIENT_MOVE);
                    patientValve_.moveTo(angles_.patientIsolate);
                    pushResume_ = true;
                    phase_ = PP_REFILL_CLOSE;
                }
            } else if (flowStalled()) {
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
                logEvent(LOG_ERROR);
                changeState(ST_ERROR);
                break;
            }
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

    snprintf_P(buf, sizeof(buf), PSTR("Objem: %u.%u ml"),
               volumeDml_ / 10, volumeDml_ % 10);
    disp_.drawRow(4, buf);

    if (iter_ == 0) {
        snprintf_P(buf, sizeof(buf), PSTR("Faze 1  Vz:%u.%u ml"),
                   (uint8_t)airMl_, (uint8_t)(airMl_ * 10.0f) % 10);
    } else {
        // Při závěrečném vytlačení je iter_ o 1 vyšší než ITER_COUNT -
        // na displeji se ořízne, aby neukazoval např. "12/11".
        uint8_t shown = (iter_ > ITER_COUNT) ? (uint8_t)ITER_COUNT : iter_;
        snprintf_P(buf, sizeof(buf), PSTR("Iterace: %u/%u"),
                   shown, (uint8_t)ITER_COUNT);
    }
    disp_.drawRow(6, buf);
}
