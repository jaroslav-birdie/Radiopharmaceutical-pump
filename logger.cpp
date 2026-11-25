#include "logger.h"

// Texty zpráv – ASCII bez diakritiky (OLED font i úspora flash).
// Max ~21 znaků kvůli šířce řádku OLED (128 px / 6 px na znak).
static const char MSG_BOOT[]          PROGMEM = "Start systemu";
static const char MSG_VALVES_SAFE[]   PROGMEM = "Ventily do bezpecna";
static const char MSG_WAIT_READY[]    PROGMEM = "Osazeno, START";
static const char MSG_CALIBRATING[]   PROGMEM = "Kalibrace senzoru";
static const char MSG_CALIB_DONE[]    PROGMEM = "Kalibrace hotova";
static const char MSG_VALVE_PAT[]     PROGMEM = "Pohyb ventilu 1";
static const char MSG_VALVE_AIR[]     PROGMEM = "Pohyb ventilu 2";
static const char MSG_AIR_PUSH[]      PROGMEM = "Aplikace vzduchu";
static const char MSG_AIR_FILL[]      PROGMEM = "Nasati vzduchu";
static const char MSG_EQUALIZE[]      PROGMEM = "Vyrovnani tlaku";
static const char MSG_SALINE[]        PROGMEM = "Aplikace H2O";
static const char MSG_ITER[]          PROGMEM = "Nova iterace";
static const char MSG_CRITICAL[]      PROGMEM = "Kriticka hladina";
static const char MSG_PAUSED[]        PROGMEM = "PAUSE - kontrola";
static const char MSG_RESUMED[]       PROGMEM = "Pokracovani";
static const char MSG_NO_FLOW[]       PROGMEM = "Hladina neklesa!";
static const char MSG_EXCESS_AIR[]    PROGMEM = "ALARM: unik vzduchu";
static const char MSG_EMERGENCY[]     PROGMEM = "NOUZOVY STOP";
static const char MSG_COMPLETE[]      PROGMEM = "HOTOVO";
static const char MSG_ERROR[]         PROGMEM = "CHYBA SYSTEMU";
static const char MSG_PRECOMP[]       PROGMEM = "Predtlak lahvicky";
static const char MSG_VIAL_MISSING[]  PROGMEM = "Lahvicka chybi!";
static const char MSG_LEVEL_LOW[]     PROGMEM = "Hladina nizko!";
static const char MSG_LEVEL_FAULT[]   PROGMEM = "Chyba hladiny!";
static const char MSG_SENSOR_FAULT[]  PROGMEM = "Chyba senzoru!";
static const char MSG_FIT_WARN[]      PROGMEM = "Prubeh mimo sablonu";

static const char *const MSG_TABLE[LOG_MSG_COUNT] PROGMEM = {
    MSG_BOOT, MSG_VALVES_SAFE, MSG_WAIT_READY,
    MSG_CALIBRATING, MSG_CALIB_DONE, MSG_VALVE_PAT, MSG_VALVE_AIR,
    MSG_AIR_PUSH, MSG_AIR_FILL, MSG_EQUALIZE, MSG_SALINE,
    MSG_ITER, MSG_CRITICAL, MSG_PAUSED, MSG_RESUMED,
    MSG_NO_FLOW, MSG_EXCESS_AIR, MSG_EMERGENCY, MSG_COMPLETE, MSG_ERROR,
    MSG_PRECOMP, MSG_VIAL_MISSING, MSG_LEVEL_LOW, MSG_LEVEL_FAULT,
    MSG_SENSOR_FAULT, MSG_FIT_WARN
};

static LogMsg lastMsg = LOG_BOOT;

void logInit() {
    lastMsg = LOG_BOOT;
}

// Zkopíruje text zprávy z PROGMEM do RAM bufferu.
void logCopyMsg(LogMsg msg, char *buf, uint8_t bufSize) {
    if (msg >= LOG_MSG_COUNT || bufSize == 0) {
        if (bufSize) buf[0] = '\0';
        return;
    }
    const char *p = (const char *)pgm_read_ptr(&MSG_TABLE[msg]);
    strncpy_P(buf, p, bufSize - 1);
    buf[bufSize - 1] = '\0';
}

void logEvent(LogMsg msg) {
    lastMsg = msg;
    char buf[24];
    logCopyMsg(msg, buf, sizeof(buf));
    Serial.print(F("# "));
    Serial.println(buf);
}

LogMsg logLastMsg() {
    return lastMsg;
}
