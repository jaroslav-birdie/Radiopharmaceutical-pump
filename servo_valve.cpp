#include "servo_valve.h"

// Timer1: režim 14 (Fast PWM, TOP = ICR1), prescaler 8.
// 16 MHz / 8 = 2 MHz -> 1 tik = 0,5 us; ICR1 = 39999 -> perioda 20 ms (50 Hz).
void servoTimerInit() {
    pinMode(PIN_SERVO_PATIENT, OUTPUT);
    pinMode(PIN_SERVO_AIR, OUTPUT);
    TCCR1A = _BV(WGM11);                             // zatím bez připojených výstupů
    TCCR1B = _BV(WGM13) | _BV(WGM12) | _BV(CS11);    // prescaler 8
    ICR1 = 39999;
    OCR1A = 0;
    OCR1B = 0;
}

void ServoValve::begin(uint8_t pin, uint8_t angle) {
    pin_ = pin;
    angle_ = angle;
    writePulse(angle);
    // Připojení PWM výstupu až po nastavení OCR – servo nedostane náhodný pulz
    if (pin_ == PIN_SERVO_PATIENT) {
        TCCR1A |= _BV(COM1A1);
    } else {
        TCCR1A |= _BV(COM1B1);
    }
    moveStart_ = millis();
}

// Převod úhlu (0-180) na šířku pulzu v ticích Timeru 1 (1 tik = 0,5 us).
void ServoValve::writePulse(uint8_t angle) {
    if (angle > 180) {
        angle = 180;
    }
    uint16_t us = SERVO_MIN_US
                + (uint16_t)(((uint32_t)(SERVO_MAX_US - SERVO_MIN_US) * angle) / 180);
    uint16_t ticks = us * 2;
    if (pin_ == PIN_SERVO_PATIENT) {
        OCR1A = ticks;
    } else {
        OCR1B = ticks;
    }
}

void ServoValve::moveTo(uint8_t angle) {
    if (angle == angle_) {
        return;                          // žádný pohyb – nespouštět čekání
    }
    angle_ = angle;
    writePulse(angle);
    moveStart_ = millis();
}

bool ServoValve::settled() const {
    return (millis() - moveStart_) >= SERVO_SETTLE_MS;
}
