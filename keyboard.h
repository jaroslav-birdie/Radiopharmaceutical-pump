#pragma once
#include <Arduino.h>
#include "config.h"

// 5-tlačítková klávesnice (10ml, 20ml, START, PAUSE/PLAY, STOP).
// Tlačítka spínají proti společné zemi (PIN_KEY_COMMON drží LOW),
// vstupy s pull-upy -> aktivní LOW. Debounce přes millis().
// HW zatím není připojen: s pull-upy jsou vstupy trvale HIGH a modul
// je neaktivní; funkci plnohodnotně supluje serial_input.

enum Key : uint8_t {
    KEY_10ML = 0,
    KEY_20ML,
    KEY_START,
    KEY_PAUSE,
    KEY_STOP,
    KEY_COUNT
};

class Keyboard {
public:
    void begin();
    void update();
    bool pressed(Key k);                 // true jednou za stisk (event se spotřebuje)

private:
    uint8_t  lastLevel_[KEY_COUNT];
    uint32_t lastChange_[KEY_COUNT];
    bool     event_[KEY_COUNT];
};
