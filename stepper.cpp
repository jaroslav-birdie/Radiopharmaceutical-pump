#include "stepper.h"

void StepperMotor::begin(uint8_t stepPin, uint8_t dirPin, float stepsPerMl,
                         uint32_t stepIntervalUs, uint8_t pushLevel) {
    stepPin_ = stepPin;
    dirPin_ = dirPin;
    stepsPerMl_ = stepsPerMl;
    intervalUs_ = stepIntervalUs;
    pushLevel_ = pushLevel;
    pinMode(stepPin_, OUTPUT);
    pinMode(dirPin_, OUTPUT);
    digitalWrite(stepPin_, LOW);
}

void StepperMotor::startMove(float ml, bool push) {
    if (ml <= 0.0f) {
        remaining_ = 0;
        done_ = 0;
        return;
    }
    digitalWrite(dirPin_, push ? pushLevel_ : (pushLevel_ == HIGH ? LOW : HIGH));
    remaining_ = (uint32_t)(ml * stepsPerMl_ + 0.5f);
    done_ = 0;
    paused_ = false;
    lastStepUs_ = micros();
}

// Generování kroků: jeden pulz na průchod, řízeno micros().
// DRV8825 vyžaduje pulz >= 1,9 us – dvojice digitalWrite to bohatě splní.
void StepperMotor::update() {
    if (remaining_ == 0 || paused_) {
        return;
    }
    uint32_t now = micros();
    if (now - lastStepUs_ < intervalUs_) {
        return;
    }
    lastStepUs_ = now;
    digitalWrite(stepPin_, HIGH);
    digitalWrite(stepPin_, LOW);
    remaining_--;
    done_++;
}

void StepperMotor::pause() {
    paused_ = true;
}

void StepperMotor::resume() {
    if (remaining_ > 0) {
        paused_ = false;
        lastStepUs_ = micros();
    }
}

void StepperMotor::stop() {
    remaining_ = 0;
    paused_ = false;
}

float StepperMotor::movedMl() const {
    return (float)done_ / stepsPerMl_;
}
