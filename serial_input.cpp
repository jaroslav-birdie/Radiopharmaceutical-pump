#include "serial_input.h"

void SerialInput::begin() {
    Serial.println(F("# Ovladani: Enter = potvrdit | k = krokovani | ? = stav |"));
    Serial.println(F("#   p = PAUSE/PLAY | x = nouzovy STOP"));
}

void SerialInput::update() {
    while (Serial.available() > 0) {
        char c = (char)Serial.read();
        // PAUSE a STOP reagují hned na znak - nečeká se na konec řádku
        if (c == 'x' || c == 'X') {
            event_[KEY_STOP] = true;
            lineHadCmd_ = true;
            continue;
        }
        if (c == 'p' || c == 'P') {
            event_[KEY_PAUSE] = true;
            lineHadCmd_ = true;
            continue;
        }
        if (c == '\n' || c == '\r') {
            // CR+LF z jednoho Enteru: druhý znak s prázdným řádkem ignorovat
            static char lastEnd = 0;
            if (len_ == 0 && lastEnd != 0 && lastEnd != c) {
                lastEnd = 0;
                continue;
            }
            lastEnd = c;
            line_[len_] = '\0';
            if (len_ > 0 || !lineHadCmd_) {
                handleLine();            // 'p'/'x' + Enter není potvrzení
            }
            len_ = 0;
            lineHadCmd_ = false;
            continue;
        }
        if (len_ < sizeof(line_) - 1) {
            line_[len_++] = c;
        }
    }
}

// Jeden řádek. Cokoli, co není příkaz, je potvrzení výzvy.
void SerialInput::handleLine() {
    char c = line_[0];
    if (len_ == 1 && (c == 'k' || c == 'K')) {
        stepToggle_ = true;
    } else if (len_ == 1 && c == '?') {
        status_ = true;
    } else {
        confirm_ = true;                 // Enter nebo jakýkoli text
        if (len_ > 0) {                  // text (např. změřená výška) do logu
            Serial.print(F("# obsluha: "));
            Serial.println(line_);
        }
    }
}

bool SerialInput::pressed(Key k) {
    if (k >= KEY_COUNT || !event_[k]) {
        return false;
    }
    event_[k] = false;
    return true;
}

bool SerialInput::takeConfirm() {
    bool c = confirm_;
    confirm_ = false;
    return c;
}

bool SerialInput::takeStepToggle() {
    bool t = stepToggle_;
    stepToggle_ = false;
    return t;
}

bool SerialInput::takeStatus() {
    bool s = status_;
    status_ = false;
    return s;
}

void SerialInput::flush() {
    confirm_ = false;
}
