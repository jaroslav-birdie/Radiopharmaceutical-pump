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
| A0  | Klávesnice – STOP (nouzové zastavení, 2. stupeň) |
| A1  | Klávesnice – PAUSE / PLAY (nouzové pozastavení a obnovení, 1. stupeň) |
| A2  | Klávesnice – 6. pin (společná/řídicí linka, upřesnit dle návrhu klávesnice) |
| A3  | nENBL obou DRV8825 (sdíleno, aktivní LOW) |
| A4  | I2C SDA (OLED + FDC1004) |
| A5  | I2C SCL (OLED + FDC1004) |

> **Piny jsou nyní beze zbytku obsazené** (18/18 použitelných pinů D2-D13 + A0-A5).
> Žádná další periferie se už bez uvolnění stávajícího pinu nevejde.

> **I2C sdílení**: SDA/SCL je sběrnice, ne bod-bod spojení – OLED (`0x3C`)
> a FDC1004 (`0x50`) se připojují **paralelně na stejné A4/A5** a rozlišují se
> adresou, ne piny. To není konflikt, je to standardní použití I2C. Ověřit jen
> kombinovaný odpor pull-up rezistorů, pokud je má osazený každý modul zvlášť.

> **nENBL DRV8825**: oba drivery sdílí jeden společný pin (nikdy není potřeba
> povolit jen jeden motor nezávisle na druhém). Aktivní LOW = výstupy povoleny.
>
> - **DISABLED**: od zapnutí (`ST_INIT`) až do stisku START (`ST_WAIT_READY`,
>   `ST_SET_VOLUME`) – obsluha v této době ručně osazuje lahvičku a stříkačky
> - **ENABLED**: od stisku START po celý zbytek aplikačního procesu –
>   `ST_PAUSED` (nouzové **pozastavení**), `ST_ALARM_EXCESS_AIR` i `ST_ERROR`
>   motory ponechávají ENABLED (drží polohu pístu proti zpětnému tlaku)
> - **DISABLED**: pouze při dokončení (`ST_COMPLETE`) nebo při nouzovém
>   **zastavení** (`ST_EMERGENCY_STOP`) – to jsou jediné dvě situace
>
> Toto je dodatečná HW pojistka nezávislá na generování kroků – i kdyby
> zůstala chyba v logice `StepperMotor`, disablovaný driver motor nepohne.

---

## Mechanika a objemy

- **Vzduchová stříkačka**: 10 ml, krokový motor + trapézová tyč, stoupání 8 mm/ot.
- **Fyziologická stříkačka**: 60 ml, krokový motor + trapézová tyč, stoupání 8 mm/ot., zpětná klapka
- **Pacientská hadička**: délka 40 cm, vnitřní průměr 1 mm (~0,31 ml objem)
- **Penicilinka**: 10 ml nebo 20 ml varianta

### Výchozí stav stříkaček při startu

| Stříkačka | Výchozí poloha |
|-----------|----------------|
| Vzduchová | 10 ml (plná) |
| Fyziologická | ~60 ml (plná, naplněna ručně před startem) |

Krokové motory **nemají endstopy** – poloha se sleduje výhradně čítáním kroků od výchozího stavu.
Výchozí stav musí být vždy fyzicky zajištěn obsluhou před stiskem START.

**Penicilinka**: 10 ml i 20 ml varianta jsou fyzicky **totožná lahvička** – liší se pouze
množstvím náplně, ne geometrií. Poloha snímací elektrody (kritická hladina) i kalibrační
postup jsou tedy pro obě varianty identické.

---

## Krokové motory – drivery (DRV8825)

Mikrokrokování se nastavuje **čistě hardwarově** piny MS1/MS2/MS3 (interní pull-down,
takže musí být explicitně připojeny) – žádný MCU pin není potřeba:

| MS1 | MS2 | MS3 | Rozlišení |
|-----|-----|-----|-----------|
| 0   | 0   | 1   | **1/16 kroku** (použito v projektu) |

MS1, MS2 → GND, MS3 → logické VCC driveru (trvale zapájeno na desce).
`MICROSTEP_DIV = 16` v `config.h` odpovídá tomuto zapojení.

---

## Stavový automat – stavy

```cpp
enum State {
    ST_INIT,             // zapnutí; serva okamžitě do definovaných PRACOVNÍCH
                         // poloh – NIKOLI do syrového 0° servo-defaultu
    ST_WAIT_READY,       // čekání na osazení systému a stisk START
    ST_SET_VOLUME,       // zadávání objemu enkodérem, potvrzení stiskem
    ST_CALIBRATING,      // kalibrace kapacitního senzoru (plná lahvička)

    // Fáze 1 – jednorázová, sjednocená pro 10 ml i 20 ml
    ST_P1_PUSH_AIR,      // tlačení vzduchu do lahvičky, dokud FDC1004 nehlásí
                         // kritickou hladinu (3 ml)
    ST_P1_EQUALIZE,      // vyrovnání tlaku přes filtr (nutné doplnění vzduchu)
    ST_P1_FILL_AIR,      // nasátí dalšího vzduchu do stříkačky z atmosféry
    ST_P1_ADD_SALINE,    // přidání 3 ml fyziologického roztoku

    // Iterativní cyklus – opakuje se 9×
    ST_ITER_EQUALIZE,    // vyrovnání přetlaku přes vzduchový filtr
    ST_ITER_FILL_AIR,    // nasátí vzduchu do stříkačky (5 ml v 1. iteraci,
                         // dále dle naučeného objemu z předchozí iterace)
    ST_ITER_PUSH_AIR,    // vytlačení kapaliny do pacienta až na kritickou hladinu;
                         // pokud nestačí nasátý objem, opakuje se
                         // ST_ITER_EQUALIZE → ST_ITER_FILL_AIR stejně jako ve Fázi 1
    ST_ITER_ADD_SALINE,  // přidání 3 ml fyziologického roztoku

    // Konec a chyby
    ST_COMPLETE,         // procedura dokončena
    ST_PAUSED,           // 1. stupeň nouzového zastavení – vše pozastaveno,
                         // obnovení tlačítkem PLAY, pokračuje automaticky
    ST_ALARM_EXCESS_AIR, // vyčerpán bezpečnostní limit počtu doplnění vzduchu
                         // bez dosažení kritické hladiny (Fáze 1 i iterace) –
                         // možná netěsnost
    ST_EMERGENCY_STOP,   // 2. stupeň – trvalé zastavení, nutný ruční zásah
    ST_ERROR             // obecná chyba
};
```

> `ST_PAUSED` se používá jak pro ruční stisk **PAUSE**, tak pro automatickou
> reakci na chybějící pokles hladiny během `ST_P1_PUSH_AIR` / `ST_ITER_PUSH_AIR`.
> Po zmáčknutí **PLAY** se automat vrací do stavu, ze kterého byl pozastaven,
> a pokračuje bez zásahu obsluhy.

---

## Ventily – povolené polohy

> ⚠️ **Zásadní korekce:** trojcestný ventil NIKDY nemá polohu „uzavřeno vůči
> všem třem ramenům". V každé rotační poloze jsou spojena vždy právě 2 ze 3
> ramen a třetí je zaslepené vnitřní geometrií ventilu. Servo pozice **0°
> (výchozí poloha po zapnutí) proto NENÍ bezpečně izolační** – u obou ventilů
> odpovídá reálně otevřené průtokové cestě. Konstanty proto nepojmenováváme
> OPEN/CLOSED, ale podle toho, **které dvě větve jsou spojeny**. Přesné úhly
> se určí experimentálně; níže je pouze sémantika stavů.

### Pacientský ventil (Servo D9)
Tři ramena: **V** (lahvička/dno), **P** (pacient), **C** (zaslepená slepá větev – nikdy nepoužívat)

| Stav | Spojení | Volný/zaslepený port | Použití |
|------|---------|----------------------|---------|
| `PATIENT_VALVE_OPEN` | V ↔ P | C zaslepen | aktivní vytlačování k pacientovi |
| `PATIENT_VALVE_ISOLATE` | V ↔ C | P zaslepen | **bezpečný klidový stav** – pacient plně izolován |

> ⚠️ Poloha spojující **P ↔ C** (pacient ↔ slepá větev) se v kódu **nikdy
> nesmí objevit** – ani jako mezipoloha při přejezdu mezi definovanými stavy,
> pokud by procházela touto kombinací nekontrolovaně (u L-portového ventilu
> s pouze 2 používanými polohami k tomu při přímém pohybu nedochází, ale
> ověř to při návrhu skutečných úhlů).
> Klidový stav systému (ihned po zapnutí i po celou dobu instalace) =
> `PATIENT_VALVE_ISOLATE`, NIKOLI servo 0°. Bezpečnost pacienta na pořadí
> instalace nezávisí – ventil zůstává izolovaný bez ohledu na to, co se
> zrovna osazuje.

### Vzduchový ventil (Servo D10)
Tři ramena: **S** (vzduchová stříkačka), **V** (lahvička/dno), **F** (vzduchový filtr/atmosféra)

| Stav | Spojení | Volný/zaslepený port | Použití |
|------|---------|----------------------|---------|
| `AIR_VALVE_SYRINGE_TO_VIAL` | S ↔ V | F zaslepen | tlačení vzduchu do lahvičky |
| `AIR_VALVE_SYRINGE_TO_FILTER` | S ↔ F | V zaslepen | nasátí vzduchu z atmosféry do stříkačky |
| `AIR_VALVE_VIAL_TO_FILTER` | V ↔ F | S zaslepen | vyrovnání tlaku lahvičky s atmosférou; **provozní klidový stav** během aplikace – stříkačka izolována |

> **Instalační stav (ihned po zapnutí) = `AIR_VALVE_SYRINGE_TO_FILTER`, NIKOLI servo 0°.**
> Liší se od provozního klidového stavu výše – viz „Pořadí instalace" níže,
> proč lahvička žádnou zvláštní ochranu při instalaci nepotřebuje a ventil
> může rovnou sedět v poloze, kterou stejně potřebuje jako první (vyvětrání
> stříkačky). `AIR_VALVE_VIAL_TO_FILTER` zůstává v provozu nadále používaný
> (vyrovnání tlaku po přidání fyz. roztoku, klidový stav po každém tlačení).

### Pořadí instalace

Obsluha nejprve osadí **oba ventily a obě stříkačky**. Lahvička s jehlami
přichází na řadu **až jako úplně poslední krok**, po stisku START a
kalibraci. Díky tomu v lahvičce **nikdy nemůže vzniknout přetlak ani
podtlak** způsobený instalací – připojuje se jehlami do systému, který je
už mechanicky hotový a otevřený do atmosféry. Proto odpadá potřeba
chránit lahvičku volbou konkrétní polohy vzduchového ventilu během
instalace (na rozdíl od dřívějšího návrhu) – jediné, co je potřeba ošetřit,
je neznámý tlakový stav samotné **stříkačky** po ručním osazení pístu (viz
`AIR_VALVE_SYRINGE_TO_FILTER` výše).

---

## Bezpečnostní pravidla (projektová)

- ❌ Pacientský ventil je v `PATIENT_VALVE_OPEN` **výhradně** při aktivním stlačování vzduchové stříkačky (`ST_P1_PUSH_AIR` / `ST_ITER_PUSH_AIR`) – ve všech ostatních stavech musí být v `PATIENT_VALVE_ISOLATE`
- ❌ Vzduchový ventil nikdy nesmí zůstat v `AIR_VALVE_SYRINGE_TO_VIAL` mimo aktivní tlačení – po dokončení kroku okamžitě přejít do `AIR_VALVE_VIAL_TO_FILTER`
- ❌ Poloha spojující pacientskou jehlu se zaslepenou větví ventilu (P↔C) se v kódu **nikdy nepoužívá** – v `config.h` pro ni neexistuje konstanta
- ❌ Ihned po `ST_INIT` (ještě před instalací obsluhou) musí být oba ventily explicitně nastaveny do `PATIENT_VALVE_ISOLATE` a `AIR_VALVE_SYRINGE_TO_FILTER` – **spoléhat na mechanický 0° default serva je nebezpečné**, protože ten odpovídá otevřené průtokové cestě
- ✅ Instalační pořadí je závazné: obsluha nejprve osadí oba ventily a obě stříkačky, lahvička s jehlami přichází na řadu jako poslední – viz „Pořadí instalace" výše
- ✅ Při `ST_EMERGENCY_STOP`: okamžitě `PATIENT_VALVE_ISOLATE`, `AIR_VALVE_VIAL_TO_FILTER`, zastavit oba krokové motory
- ✅ Při `ST_PAUSED`: zastavit oba krokové motory, ventily ponechat v aktuální poloze (na rozdíl od STOP se nemusí uzavírat, protože PAUSE má pokračovat automaticky ve stejném kroku)
- ✅ Pokud FDC1004 hlásí kritickou hladinu při stlačování vzduchu → okamžitě stop motor, `PATIENT_VALVE_ISOLATE`
- ✅ Pokud hladina neklesá při `ST_P1_PUSH_AIR` / `ST_ITER_PUSH_AIR` déle než `NO_FLOW_TIMEOUT_MS` → přejít do `ST_PAUSED`, výzva obsluze; po obnovení poklesu (nebo stisku PLAY) pokračovat automaticky
- ✅ Počet cyklů doplnění vzduchu (`ST_P1_EQUALIZE`/`ST_ITER_EQUALIZE` → `ST_P1_FILL_AIR`/`ST_ITER_FILL_AIR`) se počítá **jak ve Fázi 1, tak v každé jednotlivé iteraci zvlášť**; po překročení `MAX_AIR_REFILLS` bez dosažení kritické hladiny → `ST_ALARM_EXCESS_AIR` (chování jako `ST_EMERGENCY_STOP`, indikuje možnou netěsnost systému)
- ✅ Systém si po každé iteraci ukládá **celkový** skutečně spotřebovaný objem vzduchu potřebný k dosažení kritické hladiny (součet počátečního nasátí i všech případných doplnění v rámci téže iterace) – tato hodnota určuje nasávaný objem pro **následující** iteraci (1. iterace používá pevnou počáteční hodnotu `VOL_AIR_ITER1_ML`)

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
├── keyboard.h / .cpp             # 5-tlačítková klávesnice (10ml, 20ml, START, PAUSE, STOP)
├── encoder.h / .cpp              # rotační enkodér
├── logger.h / .cpp               # sdílené heslovité logování (Serial + OLED)
└── CLAUDE.md                     # tento soubor
```

---

## Knihovny projektu

| Knihovna | Zdroj | Účel |
|----------|-------|------|
| `Wire` | Arduino standard | I2C komunikace |
| `EEPROM` | Arduino standard | Uložení úhlů ventilů |
| — (vlastní driver) | `servo_valve.cpp` | Serva přímo přes Timer1 HW PWM (OC1A/OC1B na D9/D10) – nulový jitter, bez knihovny Servo |
| — (vlastní driver) | `display.cpp` | SH1106 128×64 textově po stránkách, bez framebufferu (~0 B SRAM), font 5×7 v PROGMEM |
| — (vlastní driver) | `capacitive.cpp` | FDC1004 přímo přes Wire (REPEAT režim 100 S/s) |

> Externí knihovny (U8g2, FDC1004, Servo) se **nepoužívají** – nahrazeny
> vlastními minimálními drivery bez `String`/`malloc` a bez závislostí.
> Serva na D9/D10 jsou vázána na Timer1 – **piny nelze přesunout**.

---

## Logování – Serial + OLED současně

Každá významná akce se vypisuje **heslovitě** (krátce) na obě výstupní zařízení
najednou – sériovou linku (ladění) i OLED (obsluha). Aby se řetězcové literály
nezdvojovaly ve flash, používá se **jedna tabulka zpráv v PROGMEM** a společná
funkce, ne oddělené `Serial.print` / OLED volání s duplicitním textem.

Příklady zpráv (heslovitě, čeština, max ~20 znaků kvůli šířce OLED řádku):
`Pohyb ventilu 1`, `Aplikace vzduchu`, `Pohyb ventilu 2`, `Nasáti vzduchu`,
`Aplikace H2O`, `Aplikace pacientovi`, `Kalibrace...`, `PAUSE - kontrola`, `STOP`.

```cpp
enum LogMsg : uint8_t { LOG_VALVE1_MOVE, LOG_AIR_PUSH, LOG_VALVE2_MOVE, /* ... */ };

void logEvent(LogMsg msg) {
    // interně: jeden PROGMEM řetězec na zprávu, vypsat na Serial i OLED
}
```

OLED (128×64, U8g2 v **page buffer módu** – viz sekce SRAM) zobrazuje aktuální
akci, případně několik posledních řádků jako jednoduchý log. Obsah konkrétních
obrazovek dořešíme později – zatím stačí, že mechanismus logování je sdílený
a nezávislý na tom, zda je klávesnice/enkodér fyzicky připojen.

> Klávesnice a enkodér zatím nejsou fyzicky dostupné. Kód se proto píše
> výhradně proti pinům z `config.h` (třídy `Keyboard`/`Encoder`) – po zapojení
> reálného hardwaru se neupravuje logika, jen se ověří, že fyzické zapojení
> odpovídá pinům v `config.h`.

---

## EEPROM – co se ukládá

Jediná hodnota, která musí přežít vypnutí přístroje, jsou **úhly servo ventilů**
(`PATIENT_VALVE_OPEN`, `PATIENT_VALVE_ISOLATE`, `AIR_VALVE_SYRINGE_TO_VIAL`,
`AIR_VALVE_SYRINGE_TO_FILTER`, `AIR_VALVE_VIAL_TO_FILTER`) – každý kus zařízení
má mírně jinou mechanickou nulu serva.

- V `config.h` jsou definovány **výchozí hodnoty** (fallback, použité při první inicializaci)
- Po sestavení konkrétního kusu lze úhly doladit v servisním režimu (enkodér) a uložit do EEPROM
- Při startu se hodnoty načtou z EEPROM; pokud EEPROM neobsahuje platná data (první spuštění), použijí se výchozí hodnoty z `config.h` a rovnou se do EEPROM zapíší
- Žádná jiná provozní data (kalibrace kapacity, naučené objemy vzduchu) se mezi jednotlivými aplikacemi neukládají – každá aplikace začíná vlastní kalibrací od nuly

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
#define PIN_STEPPER_EN   A3   // nENBL obou DRV8825, sdíleno, aktivní LOW

// === PINY – SERVA ===
#define PIN_SERVO_PATIENT  9
#define PIN_SERVO_AIR     10

// === PINY – KLÁVESNICE (5 tlačítek, 6 pinů celkem – HW zatím nedodán) ===
#define PIN_KEY_10ML     11
#define PIN_KEY_20ML     12
#define PIN_KEY_START    13
#define PIN_KEY_STOP     A0   // nouzový STOP – 2. stupeň
#define PIN_KEY_PAUSE    A1   // PAUSE/PLAY – 1. stupeň
#define PIN_KEY_COMMON   A2   // 6. pin dle návrhu klávesnice – upřesnit po dodání HW

// === PINY – I2C (fixní pro Uno) ===
#define PIN_SDA          A4
#define PIN_SCL          A5

// === PACIENTSKÝ VENTIL – SÉMANTIKA STAVŮ (výchozí hodnoty, přepsatelné z EEPROM) ===
// Pozor: servo 0° NENÍ bezpečná izolační poloha (viz sekce Ventily výše)
#define PATIENT_VALVE_OPEN_DEFAULT        XX   // V<->P spojeno (tlačení k pacientovi)
#define PATIENT_VALVE_ISOLATE_DEFAULT     YY   // V<->C spojeno, pacient izolován (KLIDOVÝ STAV)
// Poloha P<->C se v kódu nesmí nikdy definovat ani použít.

// === VZDUCHOVÝ VENTIL – SÉMANTIKA STAVŮ (výchozí hodnoty, přepsatelné z EEPROM) ===
#define AIR_VALVE_SYRINGE_TO_VIAL_DEFAULT    XX   // S<->V spojeno (tlačení vzduchu do lahvičky)
#define AIR_VALVE_SYRINGE_TO_FILTER_DEFAULT  YY   // S<->F spojeno (nasátí vzduchu z atmosféry)
#define AIR_VALVE_VIAL_TO_FILTER_DEFAULT     ZZ   // V<->F spojeno, stříkačka izolována (KLIDOVÝ STAV)

// === EEPROM – ADRESY (uint8_t úhel na hodnotu) ===
#define EEPROM_ADDR_VALID_FLAG        0   // magická hodnota – rozlišuje "první spuštění"
#define EEPROM_ADDR_PATIENT_OPEN      1
#define EEPROM_ADDR_PATIENT_ISOLATE   2
#define EEPROM_ADDR_AIR_SYR_TO_VIAL   3
#define EEPROM_ADDR_AIR_SYR_TO_FILT   4
#define EEPROM_ADDR_AIR_VIAL_TO_FILT  5

// === MECHANIKA ===
#define SCREW_PITCH_MM         8.0f   // mm na otáčku trapézové tyče
#define STEPS_PER_REV        200      // kroků na otáčku (1,8°/krok)
#define MICROSTEP_DIV         16      // mikrokrokování driveru
#define STEPS_PER_MM  ((STEPS_PER_REV * MICROSTEP_DIV) / SCREW_PITCH_MM)

// === OBJEMY (ml) ===
#define VOL_AIR_SYRINGE_MAX_ML  10.0f
#define VOL_AIR_RESERVE_ML      3.0f   // trvalá rezerva ve vzduchové stříkačce
#define VOL_AIR_ITER1_ML        5.0f   // počáteční objem nasávaný v 1. iteraci (upravitelné)
#define VOL_SAL_TOTAL_ML       30.0f
#define VOL_SAL_ITER_ML         3.0f   // dávka fyziologického roztoku na 1 iteraci
#define VOL_REMAIN_CRITICAL_ML  3.0f   // kritický zbytkový objem v lahvičce
#define ITER_COUNT               9

// === ZADÁVÁNÍ PŘESNÉHO OBJEMU (enkodér, krok 0,1 ml) ===
#define VOL_FINE_STEP_ML         0.1f
#define VOL_FINE_MIN_10ML        8.0f
#define VOL_FINE_MAX_10ML       12.0f
#define VOL_FINE_MIN_20ML       18.0f
#define VOL_FINE_MAX_20ML       22.0f

// === BEZPEČNOSTNÍ LIMITY ===
#define MAX_AIR_REFILLS          5    // max. počet doplnění vzduchu (Fáze 1 i každá
                                       // iterace zvlášť) před vyhlášením ST_ALARM_EXCESS_AIR
#define NO_FLOW_TIMEOUT_MS     3000    // bez poklesu kapacity déle než toto -> ST_PAUSED
                                       // (orientační, doladit na reálném prototypu)

// === KALIBRACE ===
#define CALIBRATION_DURATION_MS 10000  // celková doba kalibrace kapacitního senzoru

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
   Pokud arduino-cli není dostupné (offline prostředí), použij:
   ```
   tools/build.sh
   ```
   (vyžaduje balíčky `gcc-avr avr-libc arduino-core-avr`)
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

## Provizorní testovací režim bez kapacitního senzoru

Dokud není fyzicky osazen FDC1004, `TEST_MODE_NO_SENSOR` v `config.h` (`1`)
nahrazuje detekci kritické hladiny **pevně danými objemy vzduchu**:

| Krok | Požadovaný objem kapaliny (`config.h`) |
|---|---|
| Fáze 1, 10 ml varianta | `TEST_VOL_P1_PUSH_10ML_ML` |
| Fáze 1, 20 ml varianta | `TEST_VOL_P1_PUSH_20ML_ML` (přes refill smyčku, beze změny logiky) |
| Každá iterace – vytlačení | `TEST_VOL_ITER_PUSH_ML` |
| Každá iterace – nasátí | `TEST_VOL_ITER_FILL_ML` (bez adaptivního učení) |

### Kompenzace stlačení vzduchu

Ke **každé** operaci se vzduchem (tlačení i nasátí) se v kódu přičítá
`AIR_PUSH_COMPENSATION_ML`. Důvod: část vtlačeného objemu se jen schová do
stlačení celého vzduchového sloupce (stříkačka + hadičky + headspace) a do
poddajnosti mechaniky, takže kapalinu nevytlačí. Experimentálně zjištěno,
že na 7 ml požadované kapaliny chybí ~2 ml vzduchu.

Konstanty v tabulce výše tedy udávají **kolik kapaliny má vytéct**, ne kolik
vzduchu se skutečně vtlačí – přirážku doplní firmware sám. Fyziologického
roztoku se to **netýká** (je nestlačitelný a doteče vždy celý).

> Prodlužování `FLUID_DRAIN_MS` se jako řešení tohoto deficitu
> **neosvědčilo** – ani 30 s nepřidalo víc než pár kapek.

Zároveň se v tomto režimu vypíná hlídání stagnace hladiny (`flowStalled`),
protože bez svislé elektrody nemá vstupní data. `MAX_AIR_REFILLS` a
nouzové STOP/PAUSE zůstávají aktivní beze změny – týkají se jen fyzické
bilance vzduchové stříkačky, ne senzoru.

> Po osazení a odzkoušení FDC1004 nastav `TEST_MODE_NO_SENSOR` zpět na `0` –
> vrátí se plnohodnotná detekce kritické hladiny i hlídání stagnace.

---

## Časté chyby – kontrolní seznam

- [ ] Žádný `delay()` v `loop()`
- [ ] Žádná `String` třída
- [ ] Všechny `volatile` proměnné sdílené s ISR
- [ ] Řetězcové literály přes `F()`
- [ ] Flash < 80 %, SRAM < 70 %
- [ ] Poloha P<->C (pacient <-> zaslepená větev) se v kódu nikde nevyskytuje
- [ ] Ihned po ST_INIT nastaveny oba ventily do pracovních poloh (PATIENT_VALVE_ISOLATE, AIR_VALVE_SYRINGE_TO_FILTER) – ne spoléhat na 0° default
- [ ] Pacientský ventil v PATIENT_VALVE_ISOLATE při každém stavu kromě ST_P1_PUSH_AIR / ST_ITER_PUSH_AIR
- [ ] MAX_AIR_REFILLS ošetřen ve Fázi 1 i v každé iteraci → ST_ALARM_EXCESS_AIR
- [ ] Naučený objem vzduchu pro další iteraci = součet nasátí + všech doplnění v aktuální iteraci
- [ ] Zadaný objem enkodérem omezen na rozsah VOL_FINE_MIN/MAX_10ML resp. _20ML
- [ ] Kompilace bez warningů
