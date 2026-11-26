#pragma once
#include <Arduino.h>
#include "config.h"
#include "logger.h"
#include "servo_valve.h"
#include "stepper.h"
#include "capacitive.h"
#include "level_detector.h"
#include "display.h"
#include "keyboard.h"
#include "encoder.h"
#include "serial_input.h"

// Hlavní stavový automat aplikačního procesu (viz CLAUDE.md).

enum State : uint8_t {
    ST_INIT = 0,         // zapnutí; serva okamžitě do izolačních poloh
    ST_WAIT_READY,       // osazeno, START (Enter) spustí kalibraci
    ST_CALIBRATING,      // kalibrace senzoru = suché hodnoty v PRÁZDNÉ studně (10 s)

    // Fáze 1 – jednorázová, sjednocená pro 10 ml i 20 ml
    ST_P1_PUSH_AIR,      // kontrola lahvičky, předtlak, tlačení do kritické hladiny
    ST_P1_EQUALIZE,      // vyrovnání tlaku přes filtr (před doplněním vzduchu)
    ST_P1_FILL_AIR,      // nasátí vzduchu do stříkačky z atmosféry
    ST_P1_ADD_SALINE,    // přidání 3 ml fyziologického roztoku

    // Iterativní cyklus – 9x
    ST_ITER_EQUALIZE,    // vyrovnání přetlaku přes filtr
    ST_ITER_FILL_AIR,    // doplnění vzduchové stříkačky na plný objem
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
    void pauseSystem();
    void resumeSystem();
    void startAirPush();                 // tlačení celého obsahu stříkačky
    void finishAirMove(bool push);       // aktualizace bilance objemu vzduchu
    void serviceLevel();                 // detektor hladiny - vzorky s otevřeným pacientem
    void onLevelResult(LevelResult r);   // zastavení / porucha z detektoru
    void restStartCheck();               // klid před tlačením - lahvička a výchozí stav
    float precompMl() const;             // objem předtlaku
    void sensorFault();

    // --- obsluha přes Serial Monitor ---
    bool takeConfirm();                  // Enter nebo tlačítko START
    bool confirmed(const __FlashStringHelper *prompt);   // výzva + čekání na Enter
    void sayPrefix();                    // "# [Faze 1] " / "# [Iterace i/N] "
    void announceState(State prev);      // název stavu, v krokovacím režimu výzva
    void printStatus();
    void logSample();                    // zápis vzorku pro přehrání (SAMPLE_LOG)                  // příkaz '?'
    void refreshDisplay();
    bool inputPressed(Key k);            // klávesnice NEBO Serial

    // --- EEPROM ---
    void loadValveAngles();

    // --- obsluhy stavů ---
    void handleInit();
    void handleWaitReady();
    void handleCalibrating();
    void handlePushAir(bool phase1);
    void handleEqualize(bool phase1);
    void handleFillAir(bool phase1);
    void handleAddSaline();
    void finalAirFill();
    void handlePaused();

    // --- hardware ---
    ServoValve   patientValve_;
    ServoValve   airValve_;
    StepperMotor airSyr_;
    StepperMotor salSyr_;
    CapSensor    cap_;
    LevelDetector lvl_;
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

    uint8_t  iter_ = 0;                  // 0 = Fáze 1, 1-9 = iterace
    uint8_t  refills_ = 0;               // doplnění vzduchu v aktuálním kroku
    uint8_t  logCycle_ = 0;               // číslo tlačení v zápisu vzorků
    uint32_t logT0_ = 0;
    bool     pushResume_ = false;        // tlačení pokračuje po doplnění vzduchu (bez kontrol a předtlaku)
    bool     pushActive_ = false;        // vzduchová stříkačka tlačí (bilance po dojezdu)
    bool     lvlFeed_ = false;           // pacient otevřen - vzorky jdou do detektoru
    bool     sampleFresh_ = false;       // v této smyčce je nový vzorek FDC1004
    uint8_t  fitLogged_ = 0;             // už zapsané příznaky kontroly shody
    float    restSumC1_ = 0.0f;          // klid před tlačením
    float    restSumC3_ = 0.0f;
    uint16_t restN_ = 0;

    // --- obsluha ---
    bool     promptShown_ = false;       // výzva aktuálního kroku už vypsána
    bool     stepMode_ = STEP_MODE_DEFAULT;   // krokovací režim
    bool     stepWait_ = false;          // čeká se na Enter před krokem procesu
    float    airMl_ = VOL_AIR_SYRINGE_MAX_ML;   // obsah vzduchové stříkačky (jen pro plnění)
    float    salMl_ = VOL_SAL_TOTAL_ML;         // zbývající fyziologický roztok

    // --- hlídání průtoku ---

    // --- displej ---
    uint32_t lastDisplay_ = 0;
    bool     dispDirty_ = true;
};
