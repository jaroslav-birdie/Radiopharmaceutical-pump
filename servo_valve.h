#pragma once
#include <Arduino.h>
#include "config.h"

// Řízení servo ventilu přímo přes Timer1 hardwarové PWM (50 Hz).
// Piny 9 (OC1A) a 10 (OC1B) – žádná knihovna, nulový jitter, nulová SRAM navíc.
// Timer1 je plně vyhrazen servům (millis() běží na Timeru 0).

// Jednorázová inicializace Timeru 1 – volat před ServoValve::begin().
void servoTimerInit();

class ServoValve {
public:
    // pin: PIN_SERVO_PATIENT nebo PIN_SERVO_AIR; angle: výchozí úhel
    void begin(uint8_t pin, uint8_t angle);
    void moveTo(uint8_t angle);          // okamžitý povel, servo jede max. rychlostí
    bool settled() const;                // uplynula doba přejezdu?
    uint8_t angle() const { return angle_; }

private:
    void writePulse(uint8_t angle);
    uint8_t  pin_ = 0;
    uint8_t  angle_ = 0;
    uint32_t moveStart_ = 0;
};
