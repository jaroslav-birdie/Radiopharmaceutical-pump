// ============================================================
//  Kalibrace úhlů ventilů - DOČASNÝ samostatný sketch
//
//  Nemá nic společného s hlavním firmware čerpadla - je to jen
//  pomocný nástroj pro experimentální určení úhlů PATIENT_VALVE
//  a AIR_VALVE. Po zjištění úhlů zapiš hodnoty do config.h
//  (PATIENT_VALVE_..._DEFAULT / AIR_VALVE_..._DEFAULT) a nahraj
//  zpět hlavní sketch Radiopharmaceutical-pump.ino.
//
//  Serva jsou řízena STEJNĚ jako v hlavním firmware - přímo přes
//  Timer1 HW PWM (piny D9/D10), aby zjištěné úhly odpovídaly
//  reálnému chování pumpy (žádná knihovna Servo).
//
//  Chování: obě serva se současně otáčí po 5° krocích 0 -> 180 ->
//  znovu od 0, před každým krokem 2 s prodleva, aktuální úhel se
//  vypisuje na Serial (9600 Bd).
// ============================================================
#include <Arduino.h>

#define PIN_SERVO_PATIENT   9   // OC1A - stejný pin jako v config.h
#define PIN_SERVO_AIR      10   // OC1B - stejný pin jako v config.h

#define SERVO_MIN_US      544   // pulz pro 0 stupňů
#define SERVO_MAX_US     2503   // pulz pro logických 180° - natažen o ~10°
                                 // nad nominál (stejná hodnota jako v config.h)

#define STEP_DEG            5   // krok otočení
#define STEP_DELAY_MS    2000   // prodleva mezi kroky

// Timer1: režim 14 (Fast PWM, TOP = ICR1), prescaler 8.
// 16 MHz / 8 = 2 MHz -> 1 tik = 0,5 us; ICR1 = 39999 -> perioda 20 ms (50 Hz).
static void servoTimerInit() {
    pinMode(PIN_SERVO_PATIENT, OUTPUT);
    pinMode(PIN_SERVO_AIR, OUTPUT);
    TCCR1A = _BV(COM1A1) | _BV(COM1B1) | _BV(WGM11);
    TCCR1B = _BV(WGM13) | _BV(WGM12) | _BV(CS11);
    ICR1 = 39999;
    OCR1A = 0;
    OCR1B = 0;
}

// Převod úhlu (0-180) na šířku pulzu v ticích Timeru 1 (1 tik = 0,5 us).
static uint16_t angleToTicks(uint8_t angle) {
    if (angle > 180) {
        angle = 180;
    }
    uint16_t us = SERVO_MIN_US
                + (uint16_t)(((uint32_t)(SERVO_MAX_US - SERVO_MIN_US) * angle) / 180);
    return us * 2;
}

void setup() {
    Serial.begin(9600);
    servoTimerInit();
    Serial.println(F("Kalibrace uhlu ventilu"));
    Serial.println(F("PATIENT_VALVE = D9, AIR_VALVE = D10, krok 5 st., prodleva 2 s"));
}

void loop() {
    for (uint16_t angle = 0; angle <= 180; angle += STEP_DEG) {
        uint16_t ticks = angleToTicks((uint8_t)angle);
        OCR1A = ticks;
        OCR1B = ticks;
        Serial.print(F("Uhel: "));
        Serial.print(angle);
        Serial.println(F(" stupnu"));
        delay(STEP_DELAY_MS);
    }
}
