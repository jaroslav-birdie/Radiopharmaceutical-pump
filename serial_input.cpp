#include "serial_input.h"

void SerialInput::begin() {
    Serial.println(F("Prikazy: 1=10ml 2=20ml +=+0.1ml -=-0.1ml s=START p=PAUSE/PLAY x=STOP"));
}

void SerialInput::update() {
    while (Serial.available() > 0) {
        char c = (char)Serial.read();
        switch (c) {
            case '1': event_[KEY_10ML] = true;  break;
            case '2': event_[KEY_20ML] = true;  break;
            case 's': event_[KEY_START] = true; break;
            case 'p': event_[KEY_PAUSE] = true; break;
            case 'x': event_[KEY_STOP] = true;  break;
            case '+': if (delta_ < 100) delta_++; break;
            case '-': if (delta_ > -100) delta_--; break;
            default:  break;             // CR/LF a neznámé znaky se ignorují
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

int8_t SerialInput::takeDelta() {
    int8_t d = delta_;
    delta_ = 0;
    return d;
}
