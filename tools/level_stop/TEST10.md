# Test 10 – šablona + bezpečnostní kontroly, start jako v iteraci

Sketch `level_stop.ino`, režim zastavení (`v0`). Studna 3 / 3 / 3 mm.
Zastavení je **stejné jako v testu 9** (8,72 ± 0,27 mm). Nové jsou
bezpečnostní kontroly a cykly se startem uprostřed plató C3.

## Otázky

1. Pozná kontrola, že **lahvička chybí**?
2. Zastaví šablona stejně, když tlačení **začíná jako v iteraci**
   (≈ 7 ml ≈ 13 mm, uprostřed plató horního prstence)?
3. Nehlásí nové kontroly falešné poplachy?

## Detekce (jen porovnání hodnot signálu)

| | kritérium |
|---|---|
| aktivace | C3 ≤ 0,85 × vlastní maximum |
| **zastavení** | první z: C3 ≤ 0,78 × max C3 / C3/C1 ≤ 0,68 |
| záloha | vrchol C1 − C3 (pokles o 10 % výšky hrbu) |
| poslední záchrana | skok C1 o 15 % (závisí na rychlosti, jen záchrana) |

**Koleno vyřazeno** – měřilo strmost za pevný počet vzorků, takže
záviselo na rychlosti toku.

## Nové bezpečnostní kontroly

| kdy | podmínka | jinak | dosavadní data |
|---|---|---|---|
| před sérií | suché hodnoty C1, C3 v **prázdné** studně | – | ≈ 2,0 pF |
| před tlačením | C1 i C3 v klidu **≥ suchá + 0,5 pF** | `lahvicka chybi nebo je prazdna`, netlačí | s náplní +1,3 pF a víc |
| před tlačením | C3/C1 ≥ **0,88** (dřív 0,8) | `hladina neni nad hornim prstencem`, netlačí | 0,955–1,008 |
| při aktivaci | C3/C1 v okamžiku maxima C3 ≥ **0,88** (maximum z plató) | **okamžité zastavení, konec série** | 0,914–1,03 (23 cyklů) |

Kontrola maxima: kdyby tlačení začalo až na sestupu C3, zapamatovalo by se
nižší maximum a zastavilo by se níž. Na sestupu je horní prstenec
odkrytější než spodní, takže C3/C1 v okamžiku maxima vyjde nízko.
Rezerva proti dosavadním datům je jen 0,034 – hlavní otázka pro cykly 3–5.

Kontrola shody se šablonou zůstává jen v logu (`# SHODA ***`).

## Postup

Postup vypíše sketch po startu a na `h`.

1. Nahrát `level_stop.ino`, po startu musí být `POSTUP TESTU 10`, log 115200 Bd.
2. Vzduchová stříkačka plná, ventily a stříkačka osazené, hadička
   naplněná, **studna prázdná**. Dotykový test `m`, pak `t`.
3. `i`: `REZIM: sablona C3/max 0.78 | C3/C1 0.68 + kontroly (test 10)`, `cykly=5`.
4. `g`. **První výzva:** studna PRÁZDNÁ → suché hodnoty
   (`# suche hodnoty: C1=… C3=…`).
5. **Cyklus 1 – test chybějící lahvičky:** na výzvu k naplnění nejdřív
   potvrdit **bez lahvičky**. Sketch musí hlásit
   `# *** lahvicka chybi nebo je prazdna, netlaci se ***` a vyzvat znovu.
   Pak vložit lahvičku s **9–14 ml** a potvrdit.
6. **Cyklus 2:** 9–14 ml.
7. **Cykly 3–5:** pokaždé **přesně ≈ 7 ml** (stříkačkou), hladina ≈ 13 mm,
   jako po přidání 3 ml roztoku v iteraci.
8. Po každém zastavení a 30 s klidu změřit výšku hladiny od vnějšího dna
   a poslat číslo.

> Kdyby u 7 ml kontrola před tlačením hlásila `hladina neni nad hornim
> prstencem`, nic nedoplňovat: odeslat `x` (konec) a poslat log – ukáže,
> jak daleko je start iterace od prahu.

## V logu

| řádek | význam |
|---|---|
| `# suche hodnoty: C1=… C3=…` | prázdná studna |
| `# start C3/C1=…  nad suchou: C1 +… C3 +…` | kontrola před tlačením |
| `# aktivace @ … C3/C1 v maximu=…` | kontrola maxima z plató |
| `# STOP sablona C3` / `# STOP podil C3/C1` | zastavení |
| `# *** CHYBA maximum C3 mimo plato` | zastavení kontrolou, konec série |
| `#   C3/maxC3=… C1/maxC1=… shoda OK` | souřadnice šablony při zastavení |

## Kritéria (stanovená PŘEDEM)

| výsledek | význam |
|---|---|
| cyklus 1 bez lahvičky odmítnut | kontrola přítomnosti funguje |
| cykly 1–2: 8–9,5 mm | shoda s testem 9 |
| cykly 3–5: 8–9,5 mm, bez `CHYBA` | šablona funguje i pro start iterace |
| cykly 3–5 níž než 8 mm | maximum podceněné – řešit start iterace |
| `CHYBA maximum mimo plato` u 7 ml | práh 0,88 příliš přísný, nebo start opravdu na sestupu |

Překlad (`avr-g++`): flash 25 664 B (78,3 %), SRAM 668 B (32,6 %), bez varování.
