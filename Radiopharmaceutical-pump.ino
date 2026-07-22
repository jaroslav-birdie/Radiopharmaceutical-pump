// ============================================================
//  Radiopharmaceutical Pump – hlavní soubor
//  Veškerá logika je ve state_machine.cpp; zde jen setup/loop.
// ============================================================
#include <Arduino.h>
#include "config.h"
#include "state_machine.h"

static PumpController pump;

void setup() {
    pump.begin();
}

void loop() {
    pump.update();
}
