#pragma once
#include <Arduino.h>
#include "config.h"

// Neblokující řízení krokového motoru (DRV8825: STEP + DIR).
// Kroky se generují z hlavní smyčky podle micros() – žádné přerušení,
// žádný delay. Poloha se sleduje čítáním kroků (stříkačky nemají endstopy).

class StepperMotor {
public:
    // stepsPerMl: kroků na 1 ml; stepIntervalUs: perioda kroků (rychlost);
    // pushLevel: úroveň DIR pinu pro směr stlačování stříkačky
    void begin(uint8_t stepPin, uint8_t dirPin, float stepsPerMl,
               uint32_t stepIntervalUs, uint8_t pushLevel);

    // Spustí pohyb o daný objem. speedFactor zrychluje pohyb dělením
    // periody kroků (1 = normální rychlost, 2 = dvojnásobná, ...).
    void startMove(float ml, bool push, uint8_t speedFactor = 1);
    void update();                         // volat co nejčastěji z loop()
    void pause();                          // pozastavení (zbytek pohybu zůstává)
    void resume();                         // pokračování po pause()
    void stop();                           // okamžité ukončení pohybu (zbytek se ruší)

    bool  idle() const { return remaining_ == 0; }
    bool  paused() const { return paused_; }
    float movedMl() const;                 // skutečně ujetý objem od startMove()

private:
    uint8_t  stepPin_ = 0;
    uint8_t  dirPin_ = 0;
    uint8_t  pushLevel_ = HIGH;
    float    stepsPerMl_ = 1.0f;
    uint32_t intervalUs_ = 1000;         // základní perioda (normální rychlost)
    uint32_t curIntervalUs_ = 1000;      // perioda aktuálního pohybu
    uint32_t remaining_ = 0;
    uint32_t done_ = 0;
    uint32_t lastStepUs_ = 0;
    bool     paused_ = false;
};
