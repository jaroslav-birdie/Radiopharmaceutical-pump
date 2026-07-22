#pragma once
#include <Arduino.h>
#include "config.h"

// Vlastní minimální textový driver OLED SSD1306 128x64 (I2C).
// Bez framebufferu – text se zapisuje přímo po stránkách (8 řádků po 8 px),
// SRAM náklady ~0 B, font 5x7 v PROGMEM. Žádná externí knihovna.
// Kreslení jednoho řádku trvá ~3 ms – volat JEN když stojí krokové motory!

class PumpDisplay {
public:
    void begin();
    void clear();
    // Vypíše text (max 21 znaků) na řádek 0-7; zbytek řádku se smaže.
    void drawRow(uint8_t row, const char *text);

private:
    void setWindow(uint8_t row);
    bool present_ = false;
};
