#pragma once
#include <Arduino.h>
#include "keyboard.h"

// Náhrada klávesnice a enkodéru příkazy přes sériovou linku
// (fyzický HW zatím není k dispozici). Jednoznakové příkazy:
//   '1' = tlačítko 10 ml        '2' = tlačítko 20 ml
//   's' = START                 'p' = PAUSE / PLAY
//   'x' = nouzový STOP          '+' / '-' = enkodér +-0,1 ml
// Po připojení skutečné klávesnice/enkodéru zůstává modul aktivní
// souběžně – controller čte události z obou zdrojů.

class SerialInput {
public:
    void begin();                        // vypíše nápovědu příkazů
    void update();                       // čtení znaků ze Serial
    bool pressed(Key k);                 // stejné API jako Keyboard
    int8_t takeDelta();                  // stejné API jako EncoderInput

private:
    bool   event_[KEY_COUNT] = {false};
    int8_t delta_ = 0;
};
