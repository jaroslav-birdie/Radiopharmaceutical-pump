// ============================================================
//  VALVE ANGLE SWEEP - rucni overeni fyzicke geometrie
//  vzduchoveho ventilu (servo D10)
//
//  Duvod: v config.h jsou uhly S<->F a V<->F jen provizorni
//  odhady, zatimco skutecna geometrie ventilu (ktere 2 ze 3 ramen
//  jsou spojene v ktere poloze) se dosud overovala jen nepremo -
//  z toho vznikla otazka, jestli nejsou S<->F a V<->F prohozene.
//  Tenhle sketch nic nepocita ani nedetekuje - jen otoci servo
//  vzduchoveho ventilu po 5° krocich a u kazde polohy pocka na
//  obsluhu, aby mela cas prohlednout si a zapsat, ktere dva porty
//  jsou skutecne spojene.
//
//  Zadny krokovy motor, zadny senzor FDC1004 - jen servo pres
//  Timer1 HW PWM, stejny prevod uhlu na pulz jako v ostrem
//  firmwaru (servo_valve.cpp) a v air_push_profile.ino, aby uhly
//  ve stupnich, ktere si zapisete, odpovidaly presne tomu, co se
//  pak ulozi do EEPROM/config.h.
//
//  ---------------------------------------------------------------
//  POSTUP
//  ---------------------------------------------------------------
//    1. Vzduchova strikacka i penicilinka NEMUSI byt osazene -
//       tenhle test se tyka jen ventilu samotneho.
//       Pacientska hadicka NESMI byt pripojena (viz nize).
//    2. 'g' = spustit rozjezd: CW 0° -> 180°, pak CCW 180° -> 0°,
//       5° na krok.
//    3. V kazde poloze se vypise uhel a smer. Zapsat si, ktere 2
//       porty (S, V, F) jsou prave spojene a ktery je zaslepeny,
//       pak odeslat cokoli pro pokracovani (Serial Monitor: Enter).
//    4. 'x' kdykoliv (i behem cekani na potvrzeni) = okamzite
//       preruseni rozjezdu.
//    5. 'a<uhel>' = primy skok na konkretni uhel 0-180, pro
//       dolareni presne hranice mezi dvema polohami.
//
//  Piny S/V/F na ramenech ventilu dle CLAUDE.md:
//    S = vzduchova strikacka, V = lahvicka/dno, F = vzduchovy filtr/atmosfera
//
//  Pacientsky ventil (D9) se nastavi jednou na zacatku do bezpecne
//  izolacni polohy (dle aktualni EEPROM, nebo vychozi hodnoty
//  z config.h) a dal se v tomto sketchi vubec nepouziva - proto
//  pacientska hadicka behem tohoto testu nema byt pripojena
//  (nezname jeste jistotu, ze ulozena "izolacni" hodnota fyzicky
//  odpovida skutecne izolaci).
// ============================================================
#include <Arduino.h>
#include <EEPROM.h>

// ---------- piny (shodne s config.h hlavniho firmware) ----------
#define PIN_SERVO_PATIENT  9    // OC1A
#define PIN_SERVO_AIR     10    // OC1B

// ---------- serva (Timer1 HW PWM, prevzato ze servo_valve.cpp) ----------
#define SERVO_MIN_US       544
#define SERVO_MAX_US      2503
#define SERVO_STEP_SETTLE_MS  300UL   // kratke usazeni pred vypisem; dalsi
                                      // cas na prohlednuti dava az cekani
                                      // na potvrzeni obsluhou (bez limitu)

#define PATIENT_VALVE_ISOLATE_DEFAULT  90

#define EEPROM_MAGIC_VALUE           0xA6
#define EEPROM_ADDR_VALID_FLAG        0
#define EEPROM_ADDR_PATIENT_ISOLATE   2

#define STEP_DEG   5

static uint8_t curPatAngle = 255, curAirAngle = 255;
static bool    running     = false;
static char    line[16];
static uint8_t lineLen     = 0;

// ============================================================
//  Servo - Timer1 fast PWM 50 Hz (prevzato ze servo_valve.cpp)
// ============================================================
static void servoTimerInit() {
    pinMode(PIN_SERVO_PATIENT, OUTPUT);
    pinMode(PIN_SERVO_AIR, OUTPUT);
    TCCR1A = _BV(WGM11);
    TCCR1B = _BV(WGM13) | _BV(WGM12) | _BV(CS11);   // prescaler 8 -> 0,5 us/tik
    ICR1 = 39999;                                    // 20 ms
    OCR1A = 0;
    OCR1B = 0;
}

static uint16_t angleTicks(uint8_t angle) {
    if (angle > 180) angle = 180;
    uint16_t us = SERVO_MIN_US
                + (uint16_t)(((uint32_t)(SERVO_MAX_US - SERVO_MIN_US) * angle) / 180);
    return us * 2;
}

static void servoAttach() {
    OCR1A = angleTicks(curPatAngle);
    OCR1B = angleTicks(curAirAngle);
    TCCR1A |= _BV(COM1A1) | _BV(COM1B1);   // vystupy az po nastaveni OCR
}

static void airValveTo(uint8_t angle) {
    curAirAngle = angle;
    OCR1B = angleTicks(angle);
}

static uint8_t loadPatientIsolateAngle() {
    if (EEPROM.read(EEPROM_ADDR_VALID_FLAG) == EEPROM_MAGIC_VALUE) {
        return EEPROM.read(EEPROM_ADDR_PATIENT_ISOLATE);
    }
    return PATIENT_VALVE_ISOLATE_DEFAULT;
}

// ============================================================
//  Potvrzeni obsluhou - ceka bez limitu, 'x' prerusi rozjezd
// ============================================================
static bool waitConfirm() {
    while (Serial.available() > 0) Serial.read();
    while (Serial.available() == 0) { /* neomezene cekani na obsluhu */ }
    char c = (char)Serial.read();
    delay(30);
    while (Serial.available() > 0) Serial.read();
    return !(c == 'x' || c == 'X');
}

static void reportAngle(const __FlashStringHelper *dir, uint8_t angle) {
    Serial.print(F("# ")); Serial.print(dir);
    Serial.print(F(" uhel=")); Serial.print(angle);
    Serial.print(F("  (pulz=")); Serial.print(angleTicks(angle) / 2);
    Serial.println(F(" us) -- zapis, ktere 2 porty (S/V/F) jsou spojene, pak potvrd"));
}

static void sweep() {
    running = true;
    Serial.println(F("# rozjezd: CW 0 -> 180 (5 stupnu/krok)"));
    for (int16_t a = 0; a <= 180; a += STEP_DEG) {
        airValveTo((uint8_t)a);
        delay(SERVO_STEP_SETTLE_MS);
        reportAngle(F("CW "), (uint8_t)a);
        if (!waitConfirm()) {
            Serial.println(F("# ABORT - rozjezd zastaven obsluhou"));
            running = false;
            return;
        }
    }
    Serial.println(F("# rozjezd: CCW 180 -> 0 (5 stupnu/krok)"));
    for (int16_t a = 180 - STEP_DEG; a >= 0; a -= STEP_DEG) {
        airValveTo((uint8_t)a);
        delay(SERVO_STEP_SETTLE_MS);
        reportAngle(F("CCW"), (uint8_t)a);
        if (!waitConfirm()) {
            Serial.println(F("# ABORT - rozjezd zastaven obsluhou"));
            running = false;
            return;
        }
    }
    Serial.println(F("# HOTOVO - oba smery projety"));
    running = false;
}

static void printHelp() {
    Serial.println(F("# g = spustit rozjezd (CW 0->180, pak CCW 180->0), 5 st./krok"));
    Serial.println(F("# a<uhel> = primy skok na uhel 0..180 (napr. a90)"));
    Serial.println(F("# x = okamzite zastaveni behem rozjezdu"));
    Serial.println(F("# h = tato napoveda"));
    Serial.println(F("# porty ventilu: S=strikacka V=lahvicka/dno F=filtr/atmosfera"));
}

static void handleLine() {
    if (lineLen == 0) return;
    switch (line[0]) {
        case 'g':
            if (running) { Serial.println(F("# CHYBA: rozjezd uz probiha")); break; }
            sweep();
            break;
        case 'a': {
            int v = atoi(&line[1]);
            if (v >= 0 && v <= 180) {
                airValveTo((uint8_t)v);
                reportAngle(F("---"), (uint8_t)v);
            } else {
                Serial.println(F("# CHYBA: rozsah 0..180"));
            }
            break;
        }
        case 'h':
            printHelp();
            break;
        default:
            Serial.println(F("# neznamy prikaz, h=napoveda"));
            break;
    }
}

static void pollSerial() {
    while (Serial.available() > 0) {
        char c = (char)Serial.read();
        if (c == '\n' || c == '\r') {
            if (lineLen > 0) {
                line[lineLen] = '\0';
                handleLine();
                lineLen = 0;
            }
        } else if (lineLen < sizeof(line) - 1) {
            line[lineLen++] = c;
        }
    }
}

void setup() {
    Serial.begin(9600);
    delay(100);

    Serial.println(F("# VALVE ANGLE SWEEP - overeni geometrie vzduchoveho ventilu"));
    Serial.println(F("# POZOR: pacientska hadicka nema byt pripojena"));

    servoTimerInit();
    curPatAngle = loadPatientIsolateAngle();
    curAirAngle = 0;
    servoAttach();
    delay(1000);   // cas na dojezd serv do vychozich poloh pred prvnim prikazem

    printHelp();
    Serial.println(F("# 'g' spusti rozjezd"));
}

void loop() {
    pollSerial();
}
