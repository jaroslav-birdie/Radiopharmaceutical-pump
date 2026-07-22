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
    // KRITICKÉ: ventily ihned do izolačních poloh – NE servo 0 stupňů!
    patientValve_.begin(PIN_SERVO_PATIENT, angles_.patientIsolate);
    airValve_.begin(PIN_SERVO_AIR, angles_.airVialToFilter);
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
#if TEST_MODE_NO_SENSOR
    Serial.println(F("POZOR: TEST REZIM - kriticka hladina je simulovana"));
    Serial.println(F("pevnym objemem vzduchu, senzor se NEPOUZIVA!"));
#endif
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
    kb_.update();
    enc_.update();
    ser_.update();
    handleGlobalKeys();

    switch (state_) {
        case ST_INIT:            handleInit();            break;
        case ST_WAIT_READY:      handleWaitReady();       break;
        case ST_SET_VOLUME:      handleSetVolume();       break;
        case ST_CALIBRATING:     handleCalibrating();     break;
        case ST_P1_PUSH_AIR:     handlePushAir(true);     break;
        case ST_P1_EQUALIZE:     handleEqualize(true);    break;
        case ST_P1_FILL_AIR:     handleFillAir(true);     break;
        case ST_P1_ADD_SALINE:   handleAddSaline(true);   break;
        case ST_ITER_EQUALIZE:   handleEqualize(false);   break;
        case ST_ITER_FILL_AIR:   handleFillAir(false);    break;
        case ST_ITER_PUSH_AIR:   handlePushAir(false);    break;
        case ST_ITER_ADD_SALINE: handleAddSaline(false);  break;
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

// Spustí tlačení vzduchu – vždy jen po trvalou rezervu stříkačky.
void PumpController::startAirPush() {
    float pushable = airMl_ - VOL_AIR_RESERVE_ML;
    if (pushable < 0.0f) {
        pushable = 0.0f;
    }
    logEvent(LOG_AIR_PUSH);
    dispDirty_ = true;
    refreshDisplay();                    // překreslit, dokud motor stojí
    airSyr_.startMove(pushable, true);
    armFlowWatch();
}

// Po skončení pohybu vzduchové stříkačky aktualizuje bilanci objemu.
void PumpController::finishAirMove(bool push) {
    float moved = airSyr_.movedMl();
    if (push) {
        airMl_ -= moved;
        airUsedMl_ += moved;
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

void PumpController::handleCalibrating() {
    if (phase_ == 0) {
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
void PumpController::handlePushAir(bool phase1) {
    switch (phase_) {
        case 0:                          // vzduchový ventil: S<->V
            logEvent(LOG_VALVE_AIR_MOVE);
            airValve_.moveTo(angles_.airSyrToVial);
            phase_ = 1;
            break;
        case 1:                          // pacientský ventil: otevřít k pacientovi
            if (airValve_.settled()) {
                logEvent(LOG_VALVE_PATIENT_MOVE);
                patientValve_.moveTo(angles_.patientOpen);
                phase_ = 2;
            }
            break;
        case 2:                          // start tlačení
            if (patientValve_.settled()) {
                startAirPush();
                phase_ = 3;
            }
            break;
        case 3: {                        // monitorování průběhu
#if TEST_MODE_NO_SENSOR
            // PROVIZORNÍ: bez kapacitního senzoru se kritická hladina
            // nahrazuje pevně daným cílovým objemem vzduchu (viz config.h).
            float target = phase1 ? (is20ml_ ? TEST_VOL_P1_PUSH_20ML_ML
                                              : TEST_VOL_P1_PUSH_10ML_ML)
                                   : TEST_VOL_ITER_PUSH_ML;
            bool targetReached = (airUsedMl_ + airSyr_.movedMl())
                                  >= (target - TEST_VOL_MARGIN_ML);
#else
            bool targetReached = cap_.criticalLevel();
#endif
            if (targetReached) {
                airSyr_.stop();
                finishAirMove(true);
                refillMode_ = false;
                logEvent(LOG_CRITICAL_LEVEL);
                logEvent(LOG_VALVE_PATIENT_MOVE);
                patientValve_.moveTo(angles_.patientIsolate);
                phase_ = 4;
            } else if (airSyr_.idle()) { // stříkačka na rezervě – nutné doplnění
                finishAirMove(true);
                refills_++;
                if (refills_ > MAX_AIR_REFILLS) {
                    // Motory zůstávají ENABLED (drží polohu) - DISABLE je
                    // vyhrazeno jen pro ST_COMPLETE a ST_EMERGENCY_STOP.
                    safeValves();
                    logEvent(LOG_ALARM_EXCESS_AIR);
                    changeState(ST_ALARM_EXCESS_AIR);
                } else {
                    logEvent(LOG_VALVE_PATIENT_MOVE);
                    patientValve_.moveTo(angles_.patientIsolate);
                    phase_ = 5;
                }
#if !TEST_MODE_NO_SENSOR
            } else if (flowStalled()) {
                pauseSystem(true);
#endif
            }
            break;
        }
        case 4:                          // kritická hladina: uzavřít i vzduch
            if (patientValve_.settled()) {
                logEvent(LOG_VALVE_AIR_MOVE);
                airValve_.moveTo(angles_.airVialToFilter);
                phase_ = 6;
            }
            break;
        case 5:                          // k doplnění vzduchu přes vyrovnání tlaku
            if (patientValve_.settled()) {
                refillMode_ = true;
                changeState(phase1 ? ST_P1_EQUALIZE : ST_ITER_EQUALIZE);
            }
            break;
        case 6:                          // hotovo -> fyziologický roztok
            if (airValve_.settled()) {
                changeState(phase1 ? ST_P1_ADD_SALINE : ST_ITER_ADD_SALINE);
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
            // Fáze 1 a doplňování: plná stříkačka; jinak naučený objem
#if TEST_MODE_NO_SENSOR
            // PROVIZORNÍ: pevný objem 3 ml místo adaptivně naučeného
            float target = (phase1 || refillMode_)
                         ? (VOL_AIR_SYRINGE_MAX_ML - airMl_)
                         : TEST_VOL_ITER_PUSH_ML;
#else
            float target = (phase1 || refillMode_)
                         ? (VOL_AIR_SYRINGE_MAX_ML - airMl_)
                         : intakeMl_;
#endif
            float room = VOL_AIR_SYRINGE_MAX_ML - airMl_;
            if (target > room) {
                target = room;
            }
            logEvent(LOG_AIR_FILL);
            dispDirty_ = true;
            refreshDisplay();            // překreslit, dokud motor stojí
            airSyr_.startMove(target, false);
            phase_ = 2;
            break;
        }
        case 2:
            if (airSyr_.idle()) {
                finishAirMove(false);
                changeState(phase1 ? ST_P1_PUSH_AIR : ST_ITER_PUSH_AIR);
            }
            break;
    }
}

// Přidání dávky fyziologického roztoku do lahvičky.
void PumpController::handleAddSaline(bool phase1) {
#if TEST_MODE_NO_SENSOR
    (void)phase1;                    // nepoužito – učení objemu je vypnuté
#endif
    switch (phase_) {
        case 0:
            // Tolerance 0,1 ml kryje zaokrouhlení kroků; stříkačka se plní
            // reálně na ~30-32 ml, takže skutečná zásoba je vyšší než bilance.
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
#if !TEST_MODE_NO_SENSOR
                if (!phase1) {
                    // Učení: příští nasátí = skutečná spotřeba + přirážka
                    float learned = airUsedMl_ + VOL_AIR_INTAKE_MARGIN_ML;
                    float maxIntake = VOL_AIR_SYRINGE_MAX_ML - VOL_AIR_RESERVE_ML;
                    if (learned < 1.0f) learned = 1.0f;
                    if (learned > maxIntake) learned = maxIntake;
                    intakeMl_ = learned;
                }
#endif
                airUsedMl_ = 0.0f;
                refills_ = 0;
                refillMode_ = false;
                iter_++;
                if (iter_ > ITER_COUNT) {
                    safeValves();
                    disableSteppers();
                    logEvent(LOG_COMPLETE);
                    changeState(ST_COMPLETE);
                } else {
                    logEvent(LOG_ITER_START);
                    changeState(ST_ITER_EQUALIZE);
                }
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
        snprintf_P(buf, sizeof(buf), PSTR("Iterace: %u/%u"),
                   iter_, (uint8_t)ITER_COUNT);
    }
    disp_.drawRow(6, buf);
}
