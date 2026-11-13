# Plán převodu detekce hladiny do produkčního firmwaru

Stav: návrh ke schválení (CLAUDE.md, workflow bod 2 – změna více souborů).
Zdroj: `tools/level_stop/level_stop.ino` (testy 9–11), výsledek
8,75 ± 0,21 mm (≈ 4,2 ml) i pro start jako v iteraci.

## Co se mění

| soubor | změna |
|---|---|
| `config.h` | konstanty detekce (níže), mapa kanálů, odstranit `CAP_CRITICAL_PERCENT` |
| `capacitive.h/.cpp` | 3 kanály (C1 spodní, C2 svislá, C3 horní) v pF, mapa kanálů, zápis konfigurace jen se zastaveným měřením, hlídání zamrznutí, suché hodnoty |
| **`level_detector.h/.cpp`** (nový) | detektor: koše, okno, šablona, podíl, vrchol, skok, kontrola maxima a shody – čistá logika bez I/O |
| `state_machine.h/.cpp` | kontrola před tlačením, napájení detektoru, okamžité zavření pacienta, chybové stavy |
| `logger.h/.cpp` | nové zprávy (Lahvicka chybi, Hladina nizko, Stop hladina, Chyba hladiny, Shoda porusena) |
| `CLAUDE.md` | struktura projektu, kritérium zastavení, odstranit zmínky o 15% prahu |

## 1. `capacitive` – měření

- **Tři kanály** MEAS1–3 (dnes jen 2). Hodnoty v **pF**
  (`raw / 524288 + CAPDAC × 3,125`) – podíly C3/C1 potřebují absolutní hodnotu.
- **Mapa kanálů** jako konstanty v `config.h` (`CH_LOWER_CIN`, `CH_VERT_CIN`,
  `CH_UPPER_CIN`). Dotykový test zůstává servisní úkon.
- **Konfigurace jen se zastaveným měřením:** `FDC_CONF = 0`, ~5 ms, zápis
  `CONF_MEASx`, pak REPEAT (potvrzená příčina zamrzání, test 6).
- **Hlídání zamrznutí:** N bitově stejných vzorků za sebou na C1 nebo C3
  → chyba senzoru (`ST_ERROR`). Svislá elektroda se nehlídá.
- **Kalibrace (`ST_CALIBRATING`) = suché hodnoty** C1, C3 v prázdné studně
  (lahvička se podle CLAUDE.md vkládá až po kalibraci). Nahrazuje dnešní
  baseline s 15% prahem.
- Perioda čtení 50 ms (20 vzorků/s) – stejná jako v testech.

## 2. `LevelDetector` – logika (nový soubor)

Přímý přepis ze sketche, bez `Serial`, bez času:

```
reset()                       // nové tlačení (Fáze 1 / iterace)
feed(c1, c3)                  // jen se vzorky s OTEVŘENÝM pacientem
result()  -> NONE | STOP_TEMPLATE | STOP_RATIO | STOP_PEAK | FAULT_MAX | FAULT_STEP
fitBroken()                   // kontrola shody (jen zápis do logu)
```

- koš 12 vzorků, okno 8 košů, aktivace C3 ≤ 0,85 × max,
- zastavení: C3 ≤ 0,78 × max **nebo** C3/C1 ≤ 0,68, záloha vrchol C1 − C3,
- při aktivaci C3/C1 v maximu ≥ 0,80, jinak `FAULT_MAX`,
- skok C1 o 15 % → `FAULT_STEP` (poslední záchrana),
- přerušení toku (pacient zavřený) zahodí rozpracovaný koš a porovnání
  pro skok, okno a maxima drží (pauza, doplnění vzduchu).
- SRAM ≈ 100 B.

**Ověření shody s testy:** stejný `level_detector.cpp` se přeloží i na PC
(g++) a přehraje logy testů 6–11. Musí zastavit ve stejném okně jako
sketch. Tím se ověří převod bez zařízení.

## 3. `state_machine` – průběh tlačení (Fáze 1 i iterace)

Nová fáze **před otevřením pacienta** (vzduch S↔V, pacient ISOLATE):
1. klid ~5 s, průměr C1, C3,
2. **lahvička:** C1 i C3 ≥ suchá + 0,5 pF, jinak `ST_ERROR` „Lahvička chybí",
3. **výchozí stav:** C3/C1 ≥ 0,80, jinak `ST_ERROR` „Hladina nízko",
4. `detector.reset()` jen při **novém** tlačení, ne po doplnění vzduchu.

Během `phase_ 3` (tlačení) a vždy, když je pacient otevřený a ustálený
(i v `ST_PAUSED`): `detector.feed()`.

Při `STOP_*`: **ihned `PATIENT_VALVE_ISOLATE`** (místo dnešního čekání
`FLUID_DRAIN_MS`, podle testu 2 a CLAUDE.md), stop motoru, pak `V↔F`.
Při `FAULT_*`: zastavit, `safeValves()`, `ST_ERROR` (motory ENABLED).

Beze změny: `MAX_AIR_REFILLS` → `ST_ALARM_EXCESS_AIR`, doplňování vzduchu
na plnou stříkačku, `flowStalled()` (relativní změna svislé elektrody).

## 4. `config.h` – nové konstanty

```
LVL_BIN_SAMPLES 12, LVL_WIN_BINS 8, LVL_ARM_FRAC 0.85,
LVL_STOP_C3_FRAC 0.78, LVL_STOP_Q_MAX 0.68, LVL_PEAK_DROP 0.10,
LVL_STEP_DROP 0.15, LVL_START_Q_MIN 0.80, LVL_MAX_Q_MIN 0.80,
LVL_VIAL_MIN_PF 0.5, LVL_FIT_C1_MIN 0.95, LVL_FIT_C3_ZONE 0.65,
LVL_FIT_CHECK_C3 0.80, LVL_FIT_RISE_MIN 0.15,
CH_LOWER_CIN 1, CH_VERT_CIN 2, CH_UPPER_CIN 3
```

## Otevřené otázky (rozhodnutí obsluhy)

1. **Předtlak:** sketch před otevřením pacienta předtlakuje lahvičku
   (2,68 ml, pacient zavřený), produkce ne. Všechny testy běžely
   s předtlakem. Převést ho taky? (doporučuji ano – jinak se mění
   podmínky, za kterých bylo kritérium změřené)
2. **Dávka roztoku a zbytek:** zbytek ≈ 4,2 ml + 3 ml = 7,2 ml – ověřeno
   (test 10). Menší dávka nebo jiný zbytek ověří test 11 (6 ml).
3. **`NO_FLOW_TIMEOUT_MS`** (pauza při stagnaci) je časový – detekci
   zastavení se netýká, ponechat?

## Pořadí prací po schválení

1. `level_detector` + přehrání logů na PC (shoda se sketchem),
2. `capacitive` (3 kanály, pF, suché hodnoty, zamrznutí),
3. `state_machine` + `config.h` + `logger`,
4. překlad, flash/SRAM, CLAUDE.md.
