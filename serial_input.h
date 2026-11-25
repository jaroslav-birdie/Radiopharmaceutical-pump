#pragma once
#include <Arduino.h>
#include "keyboard.h"

// Ovládání přes Serial Monitor (fyzická klávesnice a enkodér zatím nejsou).
// Čte se po ŘÁDCÍCH - Serial Monitor posílá řádek po stisku Enter:
//   prázdný řádek / Enter  = potvrzení aktuální výzvy (jako START)
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
    bool takeConfirm();                  // Enter (potvrzení výzvy)
    bool takeStepToggle();               // 'k'
    bool takeStatus();                   // '?'
    void flush();                        // zahodit nevyřízené potvrzení a zadání

private:
    void handleLine();

    char    line_[16];
    uint8_t len_ = 0;
    bool    event_[KEY_COUNT] = {false};
    bool    confirm_ = false;
    bool    stepToggle_ = false;
    bool    status_ = false;
    bool    lineHadCmd_ = false;         // na řádku byl 'p'/'x'
};
