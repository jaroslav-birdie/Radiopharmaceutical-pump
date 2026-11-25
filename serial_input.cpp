#include "serial_input.h"

void SerialInput::begin() {
    Serial.println(F("# Ovladani: Enter = potvrdit vyzvu | 1/2 = lahvicka 10/20 ml |"));
    Serial.println(F("#   +/- nebo cislo (10.4) = objem | k = krokovani zap/vyp |"));
    Serial.println(F("#   ? = stav | p = PAUSE/PLAY | x = nouzovy STOP"));
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
    if (len_ == 1 && c == '1') {
        event_[KEY_10ML] = true;
    } else if (len_ == 1 && c == '2') {
        event_[KEY_20ML] = true;
    } else if (len_ == 1 && (c == 'k' || c == 'K')) {
        stepToggle_ = true;
    } else if (len_ == 1 && c == '?') {
        status_ = true;
    } else if (c == '+' || c == '-') {
        for (uint8_t i = 0; i < len_; i++) {
            if (line_[i] == '+' && delta_ < 100) delta_++;
            if (line_[i] == '-' && delta_ > -100) delta_--;
        }
    } else if (c >= '0' && c <= '9' && len_ > 1) {
        // "10.4" -> 104 desetin ml (bez atof, šetří flash)
        int16_t v = 0, frac = -1;
        for (uint8_t i = 0; i < len_; i++) {
            char d = line_[i];
            if (d == '.' || d == ',') {
                frac = 0;
            } else if (d >= '0' && d <= '9') {
                if (frac < 0) {
                    v = v * 10 + (d - '0');
                } else if (frac == 0) {
                    frac = d - '0';      // jen první desetinné místo
                }
            }
        }
        volumeDml_ = v * 10 + (frac > 0 ? frac : 0);
    } else {
        confirm_ = true;                 // Enter, 's' nebo jakýkoli text
    }
}

bool SerialInput::pressed(Key k) {
    if (k >= KEY_COUNT || !event_[k]) {
        return false;
    }
    event_[k] = false;
    return true;
}

int8_t SerialInput::takeDelta() {
    int8_t d = delta_;
    delta_ = 0;
    return d;
}

bool SerialInput::takeConfirm() {
    bool c = confirm_;
    confirm_ = false;
    return c;
}

int16_t SerialInput::takeVolumeDml() {
    int16_t v = volumeDml_;
    volumeDml_ = -1;
    return v;
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
    volumeDml_ = -1;
    delta_ = 0;
    event_[KEY_10ML] = event_[KEY_20ML] = false;
}
