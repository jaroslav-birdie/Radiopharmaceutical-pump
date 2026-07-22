#include "encoder.h"

// Sdílený čítač kroků – plněný z ISR, vybíraný v loop().
static volatile int8_t encDelta = 0;

// ISR na sestupnou hranu CLK: směr určí úroveň DT. Jen zápis příznaku.
static void encoderIsr() {
    if (digitalRead(PIN_ENC_DT) == HIGH) {
        encDelta++;
    } else {
        encDelta--;
    }
}

void EncoderInput::begin() {
    pinMode(PIN_ENC_CLK, INPUT_PULLUP);
    pinMode(PIN_ENC_DT, INPUT_PULLUP);
    pinMode(PIN_ENC_BTN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_ENC_CLK), encoderIsr, FALLING);
}

void EncoderInput::update() {
    uint32_t now = millis();
    uint8_t level = digitalRead(PIN_ENC_BTN);
    if (level != lastBtnLevel_ && (now - lastBtnChange_) >= KEY_DEBOUNCE_MS) {
        lastBtnChange_ = now;
        if (level == LOW) {
            btnEvent_ = true;
        }
        lastBtnLevel_ = level;
    }
}

int8_t EncoderInput::takeDelta() {
    noInterrupts();
    int8_t d = encDelta;
    encDelta = 0;
    interrupts();
    return d;
}

bool EncoderInput::buttonPressed() {
    if (!btnEvent_) {
        return false;
    }
    btnEvent_ = false;
    return true;
}
