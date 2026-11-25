#pragma once
#include <Arduino.h>

// ============================================================
//  Radiopharmaceutical Pump – centrální konfigurace
//  Všechny konstanty a piny na jednom místě.
// ============================================================

// === LADĚNÍ ===
#define DEBUG 1
// Krokovací režim: před každým krokem procesu (tlačení, vyrovnání tlaku,
// doplnění vzduchu, přidání roztoku) se vypíše, co přijde, a čeká se na
// Enter. Za běhu přepíná příkaz 'k'. Pro zkoušky na zařízení zapnuto.
#define STEP_MODE_DEFAULT 1

// === PINY – ENKODÉR (HW zatím nepřipojen; funkci supluje Serial) ===
#define PIN_ENC_CLK      2    // INT0
#define PIN_ENC_DT       3    // INT1
#define PIN_ENC_BTN      8

// === PINY – KROKOVÉ MOTORY (DRV8825, 1/16 mikrokrok pevně HW zapojením MS pinů) ===
#define PIN_AIR_STEP     4
#define PIN_AIR_DIR      5
#define PIN_SAL_STEP     6
#define PIN_SAL_DIR      7
#define PIN_STEPPER_EN   A3   // nENBL obou driverů spojen na 1 pin (aktivní LOW)

// Úroveň DIR pinu pro směr "stlačování stříkačky" (ověřit dle skutečného zapojení)
#define AIR_DIR_PUSH_LEVEL   HIGH
#define SAL_DIR_PUSH_LEVEL   HIGH

// nENBL DRV8825: LOW = výstupy povoleny, HIGH = výstupy odpojeny (vysoká impedance)
#define STEPPER_ENABLED_LEVEL   LOW
#define STEPPER_DISABLED_LEVEL  HIGH

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
#define SERVO_MAX_US      2503    // pulz pro logických 180° - natažen o ~10°
                                   // nad nominál (MG996R obvykle snese, ověřit
                                   // při kalibraci, že servo u dorazu nevrčí/
                                   // netáhne nadměrný proud)
#define SERVO_SETTLE_MS   1000    // doba přejezdu ventilu (1 s dle zadání)

// === PACIENTSKÝ VENTIL – VÝCHOZÍ ÚHLY (doladit experimentálně, ukládá se do EEPROM) ===
// Pozor: servo 0 stupňů NENÍ bezpečná izolační poloha (viz CLAUDE.md)!
#define PATIENT_VALVE_OPEN_DEFAULT        0   // V<->P spojeno (tlačení k pacientovi)
#define PATIENT_VALVE_ISOLATE_DEFAULT    90   // V<->C spojeno, pacient izolován (KLIDOVY STAV)
// Poloha P<->C se v kódu nesmí nikdy definovat ani použít.

// === VZDUCHOVÝ VENTIL – VÝCHOZÍ ÚHLY (doladit experimentálně, ukládá se do EEPROM) ===
#define AIR_VALVE_SYRINGE_TO_VIAL_DEFAULT    90   // S<->V (tlačení vzduchu do lahvičky)
#define AIR_VALVE_SYRINGE_TO_FILTER_DEFAULT  0   // S<->F (nasátí vzduchu z atmosféry)
#define AIR_VALVE_VIAL_TO_FILTER_DEFAULT    180   // V<->F, stříkačka izolována (KLIDOVY STAV)

// === EEPROM – ADRESY ===
#define EEPROM_MAGIC_VALUE           0xA6
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
#define AIR_SYR_ML_PER_MM      0.2f   // 10ml stříkačka
#define SAL_SYR_ML_PER_MM      0.625f   // 60ml stříkačka

// === RYCHLOST APLIKACE ===
#define FLOW_S_PER_ML          5        // 1 ml za 5 sekund (obě stříkačky)
#define AIR_FILL_SPEED_FACTOR  2        // nasávání vzduchu zpět do stříkačky je
                                        // rychlejší (nic se netlačí do pacienta);
                                        // 2 = dvojnásobná rychlost proti FLOW_S_PER_ML

// === OBJEMY (ml) ===
#define VOL_AIR_SYRINGE_MAX_ML  10.0f
#define VOL_SAL_TOTAL_ML        60.0f   // 60ml stříkačka (viz SAL_SYR_ML_PER_MM)
#define VOL_SAL_ITER_ML          3.0f   // dávka fyziologického roztoku na 1 krok
#define ITER_COUNT               11      // počet iterativních cyklů po Fázi 1

// === ZADÁVÁNÍ PŘESNÉHO OBJEMU (krok 0,1 ml; hodnoty v desetinách ml) ===
#define VOL_FINE_STEP_DML        1      // 0,1 ml
#define VOL_FINE_MIN_10ML_DML   80      // 8,0 ml
#define VOL_FINE_MAX_10ML_DML  120      // 12,0 ml
#define VOL_FINE_MIN_20ML_DML  180      // 18,0 ml
#define VOL_FINE_MAX_20ML_DML  220      // 22,0 ml

// === BEZPEČNOSTNÍ LIMITY ===
#define MAX_AIR_REFILLS          3      // max. doplnění vzduchu (Fáze 1 i každá iterace zvlášť); testy 9-12 max. 2
#define NO_FLOW_WATCH            0      // hlídání stagnace svislou elektrodou: 0 = vypnuto
                                        // (falešné pauzy 1-15x za tlačení, test 12 / zařízení 25. 11.)
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
#define FDC_RESET_MS            10      // po softwarovém resetu čipu
#define FDC_STOP_MS              5      // po zastavení měření, než se zapíše konfigurace
#define FDC_START_MS            30      // po spuštění REPEAT, než jsou první výsledky
#define FDC_FROZEN_LIMIT        10      // tolik bitově stejných vzorků za sebou = čip neměří
#define CAPDAC_STEP_PF       3.125f     // krok CAPDAC
#define CAPDAC_MAX              31
#define FDC_RAW_PER_PF   524288.0f      // 2^19 LSB na pF
#define FDC_RAW_NEAR_FULL 7000000L      // nad tímto se zvýší CAPDAC (autokalibrace)

// Mapa kanálů: který vstup FDC1004 (1-3) čte kterou elektrodu. Ověřuje se
// DOTYKOVÝM TESTEM (prst na elektrodě = kanál ~16 pF), ne popisky.
// Testy 6-12 (studna 3/3/3 mm): spodní CIN1, svislá CIN2, horní CIN3.
#define CH_LOWER_CIN             1      // spodní prstenec (C1)
#define CH_VERT_CIN              2      // svislá elektroda (C2, jen hlídání průtoku)
#define CH_UPPER_CIN             3      // horní prstenec (C3)

// === DETEKCE HLADINY (tools/level_stop, testy 9-12: 8,65 +- 0,23 mm) ===
// Jen porovnání hodnot signálu – žádný čas, rychlost ani objem vzduchu.
// Vzorky jen s otevřeným pacientem; filtr: koš LVL_BIN_SAMPLES vzorků,
// klouzavé okno LVL_WIN_BINS košů. Naladěno na rychlost FLOW_S_PER_ML = 5.
#define LVL_BIN_SAMPLES         12      // koš = 12 vzorků (0,6 s při 20 vz./s)
#define LVL_WIN_BINS             8      // okno = 8 košů
#define LVL_STOP_Q_MAX       0.68f      // stop: C3/C1 <= 0,68 (od začátku tlačení)
#define LVL_ARM_FRAC         0.85f      // aktivace: C3 <= 0,85 x vlastní maximum
#define LVL_STOP_C3_FRAC     0.78f      // stop (šablona): C3 <= 0,78 x vlastní maximum
#define LVL_PEAK_DROP        0.10f      // záloha: C1 - C3 pokles o 10 % výšky hrbu
#define LVL_STEP_DROP        0.15f      // poslední záchrana: koš C1 o 15 % pod větším ze 2 předchozích
#define LVL_MAX_Q_MIN        0.80f      // C3/C1 v okamžiku maxima C3 (jen zápis do logu)
#define LVL_FIT_C1_MIN       0.95f      // shoda: C1 >= 0,95 x max C1 ...
#define LVL_FIT_C3_ZONE      0.65f      // ... dokud C3 > 0,65 x max C3
#define LVL_FIT_CHECK_C3     0.80f      // shoda: při C3 = 0,80 x max C3 ...
#define LVL_C2_DROP_MIN      0.01f      // nezávislá kontrola: při zastavení C2 <= (1 - 0,01) x max C2 (jen zápis)
#define LVL_FIT_RISE_MIN     0.15f      // ... musí (C1 - C3) - d0 >= 0,15 x max C3

// Kontrola před každým tlačením (lahvička uzavřená, pacient izolován)
#define LVL_START_Q_MIN      0.70f      // C3/C1 >= 0,70 (v mezeře 0,5-0,6)
#define LVL_VIAL_MIN_PF      0.25f      // C1 i C3 >= suchá hodnota + 0,25 pF (prázdná lahvička +0,05)
#define LVL_REST_MS          5000UL     // klid pro kontrolu před tlačením
#define LVL_VALVE_SETTLE_MS  1500UL     // po přejezdu ventilu navíc, než se měří / tlačí
#define LVL_OPEN_SETTLE_MS   5000UL     // klid po otevření pacienta, před tlačením

// === PŘEDTLAK (před otevřením pacienta, pacient izolován) ===
// Objem = plyn v systému x (tlak hadičky + žilní tlak) / nízký tlak okolí.
// Počítá se s prázdnou lahvičkou (nejvíc plynu = konzervativní směr).
#define PRECOMP_VIAL_ML       30.0f     // objem lahvičky po okraj (odhad)
#define PRECOMP_DEAD_ML        1.0f     // mrtvý objem vzduchové cesty (odhad)
#define PRECOMP_PATIENT_CM    40.0f     // pacient max. o délku hadičky výš
#define PRECOMP_VENOUS_CM     20.0f     // horní odhad periferního žilního tlaku
#define PRECOMP_KPA_PER_CMH2O 0.0981f
#define PRECOMP_P_ATM_KPA     90.0f     // konzervativně nízký tlak okolí (~1000 m n. m.)
#define PRECOMP_SETTLE_MS     3000UL    // klid po předtlaku, pacient ještě izolován

// === OLED SH1106 128x64 ===
#define OLED_ADDR             0x3C
#define OLED_COL_OFFSET          2   // interní RAM SH1106 je 132 px široká,
                                     // viditelných je jen prostředních 128
                                     // (pokud je obraz vodorovně posunutý,
                                     // zkus 0 nebo 4)

// === SÉRIOVÁ LINKA ===
#define BAUD_RATE             9600
