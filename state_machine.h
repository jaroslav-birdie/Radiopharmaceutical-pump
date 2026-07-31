#pragma once
#include <Arduino.h>
#include "config.h"
#include "logger.h"
#include "servo_valve.h"
#include "stepper.h"
#include "capacitive.h"
#include "display.h"
#include "keyboard.h"
#include "encoder.h"
#include "serial_input.h"

// Hlavní stavový automat aplikačního procesu (viz CLAUDE.md).

enum State : uint8_t {
    ST_INIT = 0,         // zapnutí; serva okamžitě do izolačních poloh
    ST_WAIT_READY,       // čekání na osazení systému a volbu 10/20 ml
    ST_SET_VOLUME,       // jemné doladění objemu (enkodér/Serial), START spustí
    ST_CALIBRATING,      // kalibrace kapacitního senzoru (plná lahvička, 10 s)

    // Fáze 1 – jednorázová, sjednocená pro 10 ml i 20 ml
    ST_P1_PUSH_AIR,      // tlačení vzduchu, dokud senzor nehlásí kritickou hladinu
    ST_P1_EQUALIZE,      // vyrovnání tlaku přes filtr (před doplněním vzduchu)
    ST_P1_FILL_AIR,      // nasátí vzduchu do stříkačky z atmosféry
    ST_P1_ADD_SALINE,    // přidání 3 ml fyziologického roztoku

    // Iterativní cyklus – 9x
    ST_ITER_EQUALIZE,    // vyrovnání přetlaku přes filtr
    ST_ITER_FILL_AIR,    // nasátí vzduchu (1. iterace 5 ml, dále naučený objem)
    ST_ITER_PUSH_AIR,    // vytlačení kapaliny k pacientovi na kritickou hladinu
    ST_ITER_ADD_SALINE,  // přidání 3 ml fyziologického roztoku

    // Konec a chyby
    ST_COMPLETE,         // procedura dokončena
    ST_PAUSED,           // 1. stupeň – pozastaveno, PLAY pokračuje automaticky
    ST_ALARM_EXCESS_AIR, // limit doplnění vzduchu vyčerpán (možná netěsnost)
    ST_EMERGENCY_STOP,   // 2. stupeň – trvalé zastavení, ruční dokončení
    ST_ERROR,            // obecná chyba
    ST_STATE_COUNT
};

// Kalibrovatelné úhly ventilů (výchozí z config.h, trvale v EEPROM).
struct ValveAngles {
    uint8_t patientOpen;
    uint8_t patientIsolate;
    uint8_t airSyrToVial;
    uint8_t airSyrToFilter;
    uint8_t airVialToFilter;
};

class PumpController {
public:
    void begin();
    void update();                       // volat z loop(), neblokuje

private:
    // --- pomocné akce ---
    void changeState(State s);
    void safeValves();                   // oba ventily do izolačních poloh
    void enableSteppers();               // povolit oba drivery (nENBL LOW) – při START
    void disableSteppers();              // fyzicky odpojit oba drivery (nENBL HIGH)
    bool valvesSettled() const;
    void handleGlobalKeys();
    void pauseSystem(bool byNoFlow);
    void resumeSystem();
    void startAirPush();                 // tlačení až k rezervě stříkačky
    void finishAirMove(bool push);       // aktualizace bilance objemu vzduchu
    void armFlowWatch();
    bool flowStalled();
    void refreshDisplay();
    bool inputPressed(Key k);            // klávesnice NEBO Serial
    int8_t inputDelta();                 // enkodér NEBO Serial

    // --- EEPROM ---
    void loadValveAngles();

    // --- obsluhy stavů ---
    void handleInit();
    void handleWaitReady();
    void handleSetVolume();
    void handleCalibrating();
    void handlePushAir(bool phase1);
    void handleEqualize(bool phase1);
    void handleFillAir(bool phase1);
    void handleAddSaline(bool phase1);
    void handlePaused();

    // --- hardware ---
    ServoValve   patientValve_;
    ServoValve   airValve_;
    StepperMotor airSyr_;
    StepperMotor salSyr_;
    CapSensor    cap_;
    PumpDisplay  disp_;
    Keyboard     kb_;
    EncoderInput enc_;
    SerialInput  ser_;
    ValveAngles  angles_;

    // --- stav procesu ---
    State    state_ = ST_INIT;
    uint8_t  phase_ = 0;                 // pod-krok uvnitř stavu
    uint32_t phaseT_ = 0;                // časovač pod-kroku
    State    savedState_ = ST_INIT;      // pro návrat z PAUSED
    uint8_t  savedPhase_ = 0;
    bool     pausedByNoFlow_ = false;
    int32_t  pauseRefVert_ = 0;

    uint8_t  iter_ = 0;                  // 0 = Fáze 1, 1-9 = iterace
    uint8_t  refills_ = 0;               // doplnění vzduchu v aktuálním kroku
    bool     refillMode_ = false;        // FILL_AIR doplňuje po vyčerpání stříkačky
    bool     pushDone_ = false;          // po prodlevě na dotečení: true=hotovo, false=doplnit vzduch
    float    airMl_ = VOL_AIR_SYRINGE_MAX_ML;   // obsah vzduchové stříkačky
    float    salMl_ = VOL_SAL_TOTAL_ML;         // zbývající fyziologický roztok
    float    airUsedMl_ = 0.0f;          // spotřeba vzduchu v aktuální iteraci
    float    intakeMl_ = VOL_AIR_ITER1_ML;      // naučený objem pro nasátí
    uint16_t volumeDml_ = 100;           // zadaný objem lahvičky (desetiny ml)
    bool     is20ml_ = false;

    // --- hlídání průtoku ---
    int32_t  flowRefRaw_ = 0;
    uint32_t flowT_ = 0;

    // --- displej ---
    uint32_t lastDisplay_ = 0;
    bool     dispDirty_ = true;
};
