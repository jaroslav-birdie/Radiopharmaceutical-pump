#pragma once
#include <Arduino.h>

// ============================================================
//  Radiopharmaceutical Pump – centrální konfigurace
//  Všechny konstanty a piny na jednom místě.
// ============================================================

// === LADĚNÍ ===
#define DEBUG 1

// === PINY – ENKODÉR (HW zatím nepřipojen; funkci supluje Serial) ===
#define PIN_ENC_CLK      2    // INT0
#define PIN_ENC_DT       3    // INT1
#define PIN_ENC_BTN      8

// === PINY – KROKOVÉ MOTORY (DRV8825, 1/16 mikrokrok pevně HW zapojením MS pinů) ===
#define PIN_AIR_STEP     4
#define PIN_AIR_DIR      5
#define PIN_SAL_STEP     6
#define PIN_SAL_DIR      7

// Úroveň DIR pinu pro směr "stlačování stříkačky" (ověřit dle skutečného zapojení)
#define AIR_DIR_PUSH_LEVEL   HIGH
#define SAL_DIR_PUSH_LEVEL   HIGH

// === PINY – SERVA (Timer1 HW PWM: OC1A/OC1B – piny NELZE přesunout) ===
#define PIN_SERVO_PATIENT  9    // OC1A
#define PIN_SERVO_AIR     10    // OC1B

// === PINY – KLÁVESNICE (5 tlačítek, HW zatím nepřipojen; funkci supluje Serial) ===
#define PIN_KEY_10ML     11
#define PIN_KEY_20ML     12
#define PIN_KEY_START    13
#define PIN_KEY_STOP     A0   // nouzový STOP – 2. stupeň
#define PIN_KEY_PAUSE    A1   // PAUSE/PLAY – 1. stupeň
#define PIN_KEY_COMMON   A2   // společná zem tlačítek (výstup LOW)

// === PINY – I2C (fixní pro Uno): A4 = SDA, A5 = SCL (OLED + FDC1004) ===

// === SERVA – PŘEVOD ÚHLU NA PULZ ===
#define SERVO_MIN_US       544    // pulz pro 0 stupňů
#define SERVO_MAX_US      2400    // pulz pro 180 stupňů
#define SERVO_SETTLE_MS   1000    // doba přejezdu ventilu (1 s dle zadání)

// === PACIENTSKÝ VENTIL – VÝCHOZÍ ÚHLY (doladit experimentálně, ukládá se do EEPROM) ===
// Pozor: servo 0 stupňů NENÍ bezpečná izolační poloha (viz CLAUDE.md)!
#define PATIENT_VALVE_OPEN_DEFAULT        0   // V<->P spojeno (tlačení k pacientovi)
#define PATIENT_VALVE_ISOLATE_DEFAULT    90   // V<->C spojeno, pacient izolován (KLIDOVY STAV)
// Poloha P<->C se v kódu nesmí nikdy definovat ani použít.

// === VZDUCHOVÝ VENTIL – VÝCHOZÍ ÚHLY (doladit experimentálně, ukládá se do EEPROM) ===
#define AIR_VALVE_SYRINGE_TO_VIAL_DEFAULT     0   // S<->V (tlačení vzduchu do lahvičky)
#define AIR_VALVE_SYRINGE_TO_FILTER_DEFAULT  90   // S<->F (nasátí vzduchu z atmosféry)
#define AIR_VALVE_VIAL_TO_FILTER_DEFAULT    180   // V<->F, stříkačka izolována (KLIDOVY STAV)

// === EEPROM – ADRESY ===
#define EEPROM_MAGIC_VALUE           0xA5
#define EEPROM_ADDR_VALID_FLAG        0
#define EEPROM_ADDR_PATIENT_OPEN      1
#define EEPROM_ADDR_PATIENT_ISOLATE   2
#define EEPROM_ADDR_AIR_SYR_TO_VIAL   3
#define EEPROM_ADDR_AIR_SYR_TO_FILT   4
#define EEPROM_ADDR_AIR_VIAL_TO_FILT  5

// === MECHANIKA ===
#define SCREW_PITCH_MM         8.0f   // mm na otáčku trapézové tyče
#define STEPS_PER_REV        200      // celých kroků na otáčku (1,8 stupně)
#define MICROSTEP_DIV         16      // mikrokrokování DRV8825 (HW: MS1=0 MS2=0 MS3=1)
#define STEPS_PER_MM  ((STEPS_PER_REV * MICROSTEP_DIV) / SCREW_PITCH_MM)   // = 400

// Průřez stříkaček – ml na mm zdvihu (KALIBROVAT na konkrétních stříkačkách!)
#define AIR_SYR_ML_PER_MM      0.199f   // 10ml stříkačka, vnitřní průměr ~15,9 mm
#define SAL_SYR_ML_PER_MM      0.430f   // 30ml stříkačka, vnitřní průměr ~23,4 mm

// === RYCHLOST APLIKACE ===
#define FLOW_S_PER_ML          5        // 1 ml za 5 sekund (obě stříkačky)

// === OBJEMY (ml) ===
#define VOL_AIR_SYRINGE_MAX_ML  10.0f
#define VOL_AIR_RESERVE_ML       3.0f   // trvalá rezerva ve vzduchové stříkačce
#define VOL_AIR_ITER1_ML         5.0f   // objem nasávaný v 1. iteraci (upravitelné)
#define VOL_AIR_INTAKE_MARGIN_ML 0.5f   // přirážka k naučenému objemu pro další iteraci
#define VOL_SAL_TOTAL_ML        30.0f
#define VOL_SAL_ITER_ML          3.0f   // dávka fyziologického roztoku na 1 krok
#define ITER_COUNT               9      // počet iterativních cyklů po Fázi 1

// === ZADÁVÁNÍ PŘESNÉHO OBJEMU (krok 0,1 ml; hodnoty v desetinách ml) ===
#define VOL_FINE_STEP_DML        1      // 0,1 ml
#define VOL_FINE_MIN_10ML_DML   80      // 8,0 ml
#define VOL_FINE_MAX_10ML_DML  120      // 12,0 ml
#define VOL_FINE_MIN_20ML_DML  180      // 18,0 ml
#define VOL_FINE_MAX_20ML_DML  220      // 22,0 ml

// === BEZPEČNOSTNÍ LIMITY ===
#define MAX_AIR_REFILLS          5      // max. doplnění vzduchu (Fáze 1 i každá iterace zvlášť)
#define NO_FLOW_TIMEOUT_MS    3000UL    // bez poklesu hladiny déle -> ST_PAUSED (doladit!)
#define CAP_FLOW_EPS_PERMILLE    3      // promile poklesu kapacity = "hladina klesá"

// === ČASOVÁNÍ PROCESU ===
#define EQUALIZE_TIME_MS      3000UL    // doba vyrovnávání tlaku přes filtr
#define CALIBRATION_MS       10000UL    // celková doba kalibrace kapacitního senzoru
#define CAP_SAMPLE_MS           50UL    // perioda čtení FDC1004
#define DISPLAY_REFRESH_MS     500UL    // perioda překreslení OLED (jen při stojících motorech)
#define KEY_DEBOUNCE_MS         30UL    // odskok tlačítek

// === KAPACITNÍ SENZOR FDC1004 ===
#define FDC1004_ADDR          0x50
#define CAP_CRITICAL_PERCENT    15      // % pokles kapacity hladinové elektrody = kritická hladina

// === OLED SSD1306 128x64 ===
#define OLED_ADDR             0x3C

// === SÉRIOVÁ LINKA ===
#define BAUD_RATE             9600
