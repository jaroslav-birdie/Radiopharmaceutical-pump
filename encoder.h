#pragma once
#include <Arduino.h>
#include "config.h"

// Rotační enkodér (CLK = INT0/D2, DT = D3, tlačítko = D8).
// ISR jen nastavuje čítač (volatile), veškerá logika v loop().
// HW zatím není připojen: pull-upy drží vstupy HIGH, žádné falešné
// události nevznikají; funkci supluje serial_input ('+'/'-').

class EncoderInput {
public:
    void begin();
    void update();                       // debounce tlačítka
    int8_t takeDelta();                  // nasbírané kroky od minula (a vynuluje)
    bool buttonPressed();                // true jednou za stisk

private:
    uint8_t  lastBtnLevel_ = HIGH;
    uint32_t lastBtnChange_ = 0;
    bool     btnEvent_ = false;
};
