# Radiopharmaceutical Pump – instrukce pro Claude Code

## Přehled projektu

Zařízení pro řízenou aplikaci radiofarmaka z penicilinkové lahvičky pacientovi.
Imituje gravitační infuzi s iterativním ředěním fyziologickým roztokem.
Platforma: Arduino Uno (ATmega328P @ 16 MHz).

---

## Platforma a hardware

- **MCU**: ATmega328P @ 16 MHz
- **Flash**: 32 KB (512 B rezervováno pro bootloader → použitelných ~31.5 KB)
- **SRAM**: 2 KB – kritický zdroj, sleduj využití
- **EEPROM**: 1 KB
- **Napájení**: 5 V logika
- **Digitální piny**: 0–13 (0, 1 rezervovány pro UART)
- **Analogové vstupy**: A0–A5 (10-bit ADC, ref 5 V nebo AREF)
- **PWM piny**: 3, 5, 6, 9, 10, 11
- **I2C**: A4 (SDA), A5 (SCL)
- **Přerušení**: INT0 = pin 2, INT1 = pin 3

---

## Zapojení pinů (viz config.h)

| Pin | Funkce |
|-----|--------|
| D2  | Enkodér CLK (INT0 – přerušení) |
| D3  | Enkodér DT  (INT1 – přerušení) |
| D4  | Vzduchový stepper – STEP |
| D5  | Vzduchový stepper – DIR |
| D6  | Fyziologický stepper – STEP |
| D7  | Fyziologický stepper – DIR |
| D8  | Enkodér tlačítko |
| D9  | Servo pacientský ventil (PWM) |
| D10 | Servo vzduchový ventil (PWM) |
| D11 | Klávesnice – 10 ml |
| D12 | Klávesnice – 20 ml |
| D13 | Klávesnice – START |
| A0  | Klávesnice – NOUZOVÝ STOP |
| A1  | Klávesnice – 5. pin (common/GND řízení) |
| A2  | volný |
| A3  | volný |
| A4  | I2C SDA (OLED + FDC1004) |
| A5  | I2C SCL (OLED + FDC1004) |

---

## Mechanika a objemy

- **Vzduchová stříkačka**: 10 ml, krokový motor + trapézová tyč, stoupání 8 mm/ot.
- **Fyziologická stříkačka**: 30 ml (max ~32 ml), krokový motor + trapézová tyč, stoupání 8 mm/ot., zpětná klapka
- **Pacientská hadička**: délka 40 cm, vnitřní průměr 1 mm (~0,31 ml objem)
- **Penicilinka**: 10 ml nebo 20 ml varianta

### Výchozí stav stříkaček při startu

| Stříkačka | Výchozí poloha |
|-----------|----------------|
| Vzduchová | 10 ml (plná) |
| Fyziologická | ~30 ml (plná, naplněna ručně před startem) |

Krokové motory **nemají endstopy** – poloha se sleduje výhradně čítáním kroků od výchozího stavu.
Výchozí stav musí být vždy fyzicky zajištěn obsluhou před stiskem START.

---

## Stavový automat – stavy

```cpp
enum State {
    ST_INIT,             // zapnutí, serva do výchozí polohy (0°)
    ST_WAIT_READY,       // čekání na osazení systému a stisk START
    ST_CALIBRATING,      // kalibrace kapacitního senzoru (plná lahvička)
    ST_SET_VOLUME,       // zadávání objemu enkodérem (volitelné)

    // Fáze 1 – jednorázová
    ST_P1_PUSH_AIR,      // vytlačení vzduchem (7 ml pro 10ml, 1. část pro 20ml)
    ST_P1_REFILL_AIR,    // pouze 20ml varianta: doplnění vzduchové stříkačky
    ST_P1_PUSH_AIR2,     // pouze 20ml varianta: druhý vzduchový zdvih
    ST_P1_ADD_SALINE,    // přidání 3 ml fyziologického roztoku

    // Iterativní cyklus – opakuje se 9×
    ST_ITER_EQUALIZE,    // vyrovnání přetlaku přes vzduchový filtr
    ST_ITER_FILL_AIR,    // nasátí 3 ml vzduchu do stříkačky
    ST_ITER_PUSH_AIR,    // vytlačení 3 ml kapaliny do pacienta
    ST_ITER_ADD_SALINE,  // přidání 3 ml fyziologického roztoku

    // Konec a chyby
    ST_COMPLETE,         // procedura dokončena
    ST_ALARM_NO_FLOW,    // hladina neklesá – výzva obsluze
    ST_EMERGENCY_STOP,   // nouzové zastavení
    ST_ERROR             // obecná chyba
};
```

---

## Ventily – povolené polohy

### Pacientský ventil (Servo D9)
| Stav | Úhel |
|------|------|
| UZAVŘENO (bezpečná výchozí poloha) | 0° |
| OTEVŘENO k pacientovi | 90° |

> ⚠️ Zaslepené rameno ventilu nesmí být NIKDY propojeno s jehlou.
> Servo smí přejít POUZE mezi 0° a 90°. Nikdy nepřekračuj 90°.

### Vzduchový ventil (Servo D10)
| Stav | Úhel |
|------|------|
| UZAVŘENO (bezpečná výchozí poloha) | 0° |
| Stříkačka ↔ penicilinka | 90° |
| Stříkačka ↔ vzduchový filtr (rovnotlak / nasávání) | 180° |

---

## Bezpečnostní pravidla (projektová)

- ❌ Pacientský ventil se otevírá **výhradně** při aktivním stlačování vzduchové stříkačky
- ❌ Pacientský ventil se **nikdy** nepohybuje, dokud FDC1004 hlásí kritickou hladinu
- ❌ Vzduchová stříkačka se **nikdy** nestlačuje, pokud je pacientský ventil uzavřen a lahvička je přetlakovaná bez otevřeného filtrového ramene
- ❌ Servo D9 nikdy nesmí dostat úhel > 90° (zaslepené rameno)
- ✅ Při ST_EMERGENCY_STOP: okamžitě uzavřít obě serva (0°), zastavit oba krokové motory
- ✅ Pokud FDC1004 hlásí kritickou hladinu při stlačování vzduchu → okamžitě stop motor, zavřít pacientský ventil, přejít do ST_ALARM_NO_FLOW

---

## Absolutní zákazy (platformové)

- ❌ **Nikdy nepoužívej `String` třídu** – fragmentuje heap
- ❌ **Nikdy nepoužívej `delay()`** v `loop()` ani v ISR
- ❌ **Nikdy nedynamicky alokuj paměť** (`new`, `malloc`)
- ❌ **Nikdy nepřekračuj 80 % flash** ani **70 % SRAM**
- ❌ **Nikdy nevolej `Serial.print()` v ISR**
- ❌ **Nikdy nevolej blokovací funkce uvnitř ISR**

---

## Povinné vzory

### Časování bez blokování
```cpp
static uint32_t lastTime = 0;
const uint32_t INTERVAL = 500;

if (millis() - lastTime >= INTERVAL) {
    lastTime = millis();
    // akce
}
```

### Konstanty do flash
```cpp
Serial.print(F("Kalibruji..."));
```

### Stavový automat
```cpp
void loop() {
    switch (state) {
        case ST_INIT:           handleInit();         break;
        case ST_CALIBRATING:    handleCalibrating();  break;
        case ST_ITER_PUSH_AIR:  handleIterPushAir();  break;
        // ...
    }
}
```

### Bezpečné ISR
```cpp
volatile bool encoderMoved = false;

ISR(INT0_vect) {
    encoderMoved = true;  // jen příznak, logiku v loop()
}
```

---

## Správa paměti

- Statické buffery s pevnou maximální velikostí
- Nejmenší dostatečný datový typ: `uint8_t`, `int8_t`, `uint16_t`
- Řetězcové literály vždy přes `F()`
- Cíl: **flash < 80 %**, **SRAM < 70 %**

---

## Struktura projektu

```
radiopharmaceutical-pump/
├── radiopharmaceutical-pump.ino  # setup() a loop(), žádná logika
├── config.h                      # všechny konstanty a pin definice
├── state_machine.h / .cpp        # hlavní stavový automat
├── stepper.h / .cpp              # řízení krokových motorů
├── servo_valve.h / .cpp          # řízení ventilů servomotory
├── capacitive.h / .cpp           # FDC1004 kapacitní senzor
├── display.h / .cpp              # OLED displej
├── keyboard.h / .cpp             # 4-tlačítková klávesnice
├── encoder.h / .cpp              # rotační enkodér
└── CLAUDE.md                     # tento soubor
```

---

## Knihovny projektu

| Knihovna | Zdroj | Účel |
|----------|-------|------|
| `Wire` | Arduino standard | I2C komunikace |
| `Servo` | Arduino standard | Ovládání servomotorů |
| `EEPROM` | Arduino standard | Uložení kalibrace |
| `U8g2` | Library Manager: „U8g2" | OLED displej (nepoužívá String) |
| `FDC1004` | Library Manager: „FDC1004" | Kapacitní senzor hladiny |

> Před použitím každé knihovny ověř, že interně nepoužívá `String` ani `malloc`.

---

## config.h – šablona

```cpp
#pragma once

// === PINY – ENKODÉR ===
#define PIN_ENC_CLK      2
#define PIN_ENC_DT       3
#define PIN_ENC_BTN      8

// === PINY – KROKOVÉ MOTORY ===
#define PIN_AIR_STEP     4
#define PIN_AIR_DIR      5
#define PIN_SAL_STEP     6
#define PIN_SAL_DIR      7

// === PINY – SERVA ===
#define PIN_SERVO_PATIENT  9
#define PIN_SERVO_AIR     10

// === PINY – KLÁVESNICE ===
#define PIN_KEY_10ML     11
#define PIN_KEY_20ML     12
#define PIN_KEY_START    13
#define PIN_KEY_STOP     A0
#define PIN_KEY_COMMON   A1

// === PINY – I2C (fixní pro Uno) ===
#define PIN_SDA          A4
#define PIN_SCL          A5

// === SERVA – POVOLENÉ ÚHLY ===
#define SERVO_PATIENT_CLOSED    0
#define SERVO_PATIENT_OPEN     90   // MAX – nikdy nepřekračuj
#define SERVO_AIR_CLOSED        0
#define SERVO_AIR_SYRINGE      90
#define SERVO_AIR_FILTER      180

// === MECHANIKA ===
#define SCREW_PITCH_MM         8.0f   // mm na otáčku trapézové tyče
#define STEPS_PER_REV        200      // kroků na otáčku (1,8°/krok)
#define MICROSTEP_DIV         16      // mikrokrokování driveru
#define STEPS_PER_MM  ((STEPS_PER_REV * MICROSTEP_DIV) / SCREW_PITCH_MM)

// === OBJEMY (ml) ===
#define VOL_AIR_INITIAL_ML     10.0f
#define VOL_AIR_RESERVE_ML      3.0f
#define VOL_AIR_ITER_ML         3.0f
#define VOL_SAL_TOTAL_ML       30.0f
#define VOL_ITER_ML             3.0f
#define VOL_P1_PUSH_10ML        7.0f
#define VOL_P1_PUSH_20ML_1     10.0f
#define VOL_REMAIN_CRITICAL_ML  3.0f
#define ITER_COUNT              9

// === KAPACITNÍ SENZOR ===
#define FDC1004_ADDR          0x50
#define CAP_CRITICAL_PERCENT   15    // % pokles kapacity = kritická hladina

// === BAUD RATE ===
#define BAUD_RATE             9600
```

---

## Workflow pro každý úkol

1. **Před implementací**: Přečti `config.h` a existující `.h` soubory
2. **Plánování**: Pro změny > 1 soubor navrhni plán a čekej na schválení
3. **Implementace**: Dodržuj vzory z tohoto souboru
4. **Ověření**: Po každé změně spusť kompilaci:
   ```
   arduino-cli compile --fqbn arduino:avr:uno .
   ```
   Oprav **všechny** warningy i errory
5. **Shrnutí**: Uveď využití flash a SRAM z výstupu kompilátoru

---

## Styl kódu

- **Jazyk komentářů**: čeština
- **Jazyk identifikátorů**: angličtina, popisné názvy
- **Odsazení**: 4 mezery (ne tabulátory)
- **Závorky**: K&R styl
- Jedna funkce = jeden úkol, max ~30 řádků
- Žádná „magická čísla" – vše jako konstanty v `config.h`

---

## Ladění

```cpp
#define DEBUG 1

#if DEBUG
  Serial.print(F("Stav: "));
  Serial.println(state);
#endif
```

Před finální verzí nastav `#define DEBUG 0`.

---

## Časté chyby – kontrolní seznam

- [ ] Žádný `delay()` v `loop()`
- [ ] Žádná `String` třída
- [ ] Všechny `volatile` proměnné sdílené s ISR
- [ ] Řetězcové literály přes `F()`
- [ ] Flash < 80 %, SRAM < 70 %
- [ ] Servo D9 nikdy > 90°
- [ ] Pacientský ventil uzavřen při každém stavu kromě ST_P1_PUSH_AIR / ST_ITER_PUSH_AIR
- [ ] Kompilace bez warningů
