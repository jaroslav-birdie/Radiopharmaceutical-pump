# Test 8 – zastavení v mezeře: první ze tří kritérií

Sketch `level_stop.ino`, režim zastavení (`v0`, výchozí). Studna 3 / 3 / 3 mm.
Postup je **stejný jako v testu 7** (`TEST7.md`, oddíl Postup): 5 cyklů,
neznámá náplň 9–14 ml, po zastavení a 30 s klidu vyndat lahvičku,
změřit výšku hladiny od vnějšího dna a poslat číslo.

## Otázka

> **Zastaví se hladina v mezeře (cíl ≈ 8,5 mm, ≈ 4 ml) i při dnešním
> šumu, a nikdy níž?**

## Proč tři kritéria

Test 7: vrchol C1 − C3 leží ≈ 8 mm, ale detektor ho potvrdí až ≈ 9 s
toku po vrcholu → hladina 4,1 mm (1,3 ml), **nebezpečně nízko**. Kratší
okno zastavuje náhodně předčasně. Žádné samotné kritérium nezastaví
přesně v mezeře. Zastaví se proto na **prvním** ze tří; každé je
nastavené tak, aby ho **šum mohl jen uspíšit** (víc zbytku, bezpečný
směr), nikdy opozdit. S lepším stíněním se zastavení jen zpřesní.

| kritérium | podmínka (okno 8 košů = 4,8 s, od aktivace) | odhad výšky* |
|---|---|---|
| **podíl** | `C3/C1 ≤ 0,60` | 10,0 ± 1,1 mm |
| **koleno** | nárůst `C1 − C3` za 8 košů < 0,5 × největší nárůst, a `C1 − C3 − d₀ ≥ 1,5 ×` největší nárůst | 9,3 ± 1,5 mm |
| **vrchol** (záloha) | `C1 − C3` pokles o 10 % výšky hrbu | 4,1 mm (změřeno v testu 7) |
| **skok C1** (poslední záchrana) | koš C1 o 15 % níž než větší ze 2 předchozích | – |
| **první z podíl / koleno / vrchol** | | **10,6 ± 0,9 mm, nejníž 8,8 mm** |

Aktivace jako dřív: `C3 ≤ 0,85 × max C3` v běhu. Kontrola před tlačením
`C3/C1 ≥ 0,8` beze změny.

\* Přehráno na 9 cyklech testů 6 a 7. Výška je **odhad**: čas toku
přepočtený stálou rychlostí hladiny ≈ 0,41 mm/s, ukotvený na výšce
změřené v testu 7 (u testu 6 na jejím průměru 4,08 mm). Proto se měří.

### Proč šum zastavení jen uspíší

- **podíl:** zastaví se na **prvním** překročení prahu; zákmit ho dosáhne
  dřív, nikdy později.
- **koleno:** šum zvětší největší nárůst i rozptyl nárůstu → podmínka
  „< 0,5 × max“ nastane spíš dřív.
- **vrchol:** zákmit dolů potvrdí vrchol dřív.

Riziko je jen jedno: **plató C3/C1 v mezeře se mění** (test 6 0,54–0,57,
test 7 ≈ 0,50). Kdyby se zvedlo nad 0,60, podíl se nedosáhne a zastaví
koleno (nejníž 7,5 mm v přehrání). Vrchol zůstává jako záloha.

### Vyřazeno

- **Podíl s odečtenou suchou hodnotou** `(C3 − P3)/(C1 − P1)`: v testu 7
  8,3 mm, v testu 6 4–6 mm. Suchá hodnota se v testu 6 mezi dvěma
  měřeními posunula o 0,5 pF.
- Kratší okno / menší pokles u vrcholu: náhodná předčasná zastavení.

## Kritéria (stanovená PŘEDEM)

| výsledek | význam |
|---|---|
| všech 5 výšek ≥ 8,5 mm | cíl splněn, rozhodnout o horní mezi |
| některá výška 7–8,5 mm | ve spodním prstenci, bezpečné, posunout prahy výš |
| některá výška < 7 mm | nepřijatelné – rozhodnout jinak |
| výška > 13 mm | příliš brzy (horní prstenec), méně vytlačeno za cyklus |

## V logu

| řádek | význam |
|---|---|
| `# STOP podil C3/C1 @ t=…` | zastavil podíl |
| `# STOP koleno @ t=…` | zastavilo koleno |
| `# STOP vrchol @ t=…` | zastavila záloha (pozdě!) |
| `# *** SKOK C1 @ …` | poslední záchrana, série končí |
| `# obsluha: 9.5` | změřená výška [mm] |

V řádku je vždy `C1`, `C3`, `d` (okno) a `dmax` s časem.

Překlad (`avr-g++`): flash 25 668 B (78,3 %), SRAM 696 B (34,0 %), bez varování.
