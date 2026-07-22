#include "keyboard.h"

static const uint8_t KEY_PINS[KEY_COUNT] = {
    PIN_KEY_10ML, PIN_KEY_20ML, PIN_KEY_START, PIN_KEY_PAUSE, PIN_KEY_STOP
};

void Keyboard::begin() {
    pinMode(PIN_KEY_COMMON, OUTPUT);
    digitalWrite(PIN_KEY_COMMON, LOW);   // společná zem tlačítek
    for (uint8_t i = 0; i < KEY_COUNT; i++) {
        pinMode(KEY_PINS[i], INPUT_PULLUP);
        lastLevel_[i] = HIGH;
        lastChange_[i] = 0;
        event_[i] = false;
    }
}

// Detekce sestupné hrany s debounce – událost se uloží do event_[].
void Keyboard::update() {
    uint32_t now = millis();
    for (uint8_t i = 0; i < KEY_COUNT; i++) {
        uint8_t level = digitalRead(KEY_PINS[i]);
        if (level != lastLevel_[i] && (now - lastChange_[i]) >= KEY_DEBOUNCE_MS) {
            lastChange_[i] = now;
            if (level == LOW) {
                event_[i] = true;        // stisk (aktivní LOW)
            }
            lastLevel_[i] = level;
        }
    }
}

bool Keyboard::pressed(Key k) {
    if (k >= KEY_COUNT || !event_[k]) {
        return false;
    }
    event_[k] = false;
    return true;
}
