#pragma once
#include <Arduino.h>
#include "keyboard.h"

// Ovládání přes Serial Monitor (fyzická klávesnice a enkodér zatím nejsou).
// Čte se po ŘÁDCÍCH - Serial Monitor posílá řádek po stisku Enter:
//   prázdný řádek / Enter  = potvrzení aktuální výzvy (jako START)
//   1 / 2                  = lahvička 10 ml / 20 ml
//   + / -                  = objem +-0,1 ml (lze opakovat: +++)
//   10.4                   = objem přímo v ml
//   k                      = krokovací režim zap/vyp
//   ?                      = výpis aktuálního stavu
//   p / x                  = PAUSE/PLAY / nouzový STOP - reagují HNED
//                            na znak, nečeká se na Enter
// Modul běží souběžně s fyzickou klávesnicí a enkodérem.

class SerialInput {
public:
    void begin();
    void update();                       // čtení znaků ze Serial
    bool pressed(Key k);                 // stejné API jako Keyboard
    int8_t takeDelta();                  // stejné API jako EncoderInput
    bool takeConfirm();                  // Enter (potvrzení výzvy)
    int16_t takeVolumeDml();             // zadaný objem v desetinách ml, -1 = nic
    bool takeStepToggle();               // 'k'
    bool takeStatus();                   // '?'
    void flush();                        // zahodit nevyřízené potvrzení a zadání

private:
    void handleLine();

    char    line_[16];
    uint8_t len_ = 0;
    bool    event_[KEY_COUNT] = {false};
    int8_t  delta_ = 0;
    bool    confirm_ = false;
    int16_t volumeDml_ = -1;
    bool    stepToggle_ = false;
    bool    status_ = false;
    bool    lineHadCmd_ = false;         // na řádku byl 'p'/'x'
};
