#pragma once
#include <Arduino.h>

// Sdílené heslovité logování: jedna PROGMEM tabulka zpráv,
// výstup současně na Serial; poslední zpráva se zobrazuje na OLED.

enum LogMsg : uint8_t {
    LOG_BOOT = 0,
    LOG_VALVES_SAFE,
    LOG_WAIT_READY,
    LOG_CALIBRATING,
    LOG_CALIB_DONE,
    LOG_VALVE_PATIENT_MOVE,
    LOG_VALVE_AIR_MOVE,
    LOG_AIR_PUSH,
    LOG_AIR_FILL,
    LOG_EQUALIZE,
    LOG_SALINE_PUSH,
    LOG_ITER_START,
    LOG_CRITICAL_LEVEL,
    LOG_PAUSED,
    LOG_RESUMED,
    LOG_ALARM_EXCESS_AIR,
    LOG_EMERGENCY_STOP,
    LOG_COMPLETE,
    LOG_ERROR,
    LOG_PRECOMPRESS,         // předtlak lahvičky před otevřením pacienta
    LOG_VIAL_MISSING,        // klid pod suchou hodnotou + práh: lahvička chybí / prázdná
    LOG_LEVEL_LOW,           // C3/C1 v klidu nízko: hladina už pod horním prstencem
    LOG_LEVEL_FAULT,         // skok C1 - porucha snímání hladiny
    LOG_SENSOR_FAULT,        // FDC1004 neměří (zamrzlý / neodpovídá)
    LOG_FIT_WARN,            // průběh nekopíruje šablonu (jen informace)
    LOG_MSG_COUNT
};

void logInit();
void logEvent(LogMsg msg);              // výpis na Serial + uložení pro OLED
LogMsg logLastMsg();
void logCopyMsg(LogMsg msg, char *buf, uint8_t bufSize);   // text zprávy z PROGMEM
