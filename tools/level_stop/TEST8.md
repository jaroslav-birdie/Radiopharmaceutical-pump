# Test 8 – zastavení v mezeře na koleni rozdílu C1 − C3

Sketch `level_stop.ino`, režim zastavení (`v0`, výchozí). Studna 3 / 3 / 3 mm
(geometrie beze změny). Postup je **stejný jako v testu 7** (`TEST7.md`,
oddíl Postup): 5 cyklů, neznámá náplň 9–14 ml, po zastavení a 30 s klidu
vyndat lahvičku, změřit výšku hladiny od vnějšího dna a poslat číslo.

## Otázka

> **Zastaví koleno rozdílu hladinu v mezeře (cíl ≈ 8,5 mm, ≈ 4 ml) a je
> rozptyl přijatelný?**

## Proč koleno

Test 7: vrchol C1 − C3 leží ≈ 8 mm, ale detektor ho potvrdí až ≈ 9 s
toku po vrcholu → hladina 4,1 mm (1,3 ml), **nebezpečně nízko**. Koleno je
chvíle, kdy rozdíl **přestává růst** (horní prstenec už je odkrytý) – ještě
před vrcholem, takže nečeká na jeho potvrzení.

| kritérium | podmínka (okno 8 košů = 4,8 s, od aktivace) | odhad výšky* |
|---|---|---|
| **koleno** (zastavuje) | nárůst `C1 − C3` za 8 košů < 0,5 × největší nárůst v běhu, a zároveň `C1 − C3 − d₀ ≥ 1,5 ×` největší nárůst | **9,3 ± 1,5 mm (7,5–11,5)** |
| **vrchol** (záloha) | `C1 − C3` pokles o 10 % výšky hrbu | 4,1 mm (změřeno v testu 7) |
| **skok C1** (poslední záchrana) | koš C1 o 15 % níž než větší ze 2 předchozích | – |

Aktivace jako dřív: `C3 ≤ 0,85 × max C3` v běhu. Kontrola před tlačením
`C3/C1 ≥ 0,8` beze změny. Druhá podmínka kolena (`≥ 1,5 ×`) brání
zastavení na začátku růstu, kdy je nárůst ještě malý.

\* Přehráno na 9 cyklech testů 6 a 7, koleno se spustilo ve všech.
Výška je **odhad**: čas toku přepočtený stálou rychlostí hladiny
≈ 0,41 mm/s, ukotvený na výšce změřené v testu 7 (u testu 6 na jejím
průměru 4,08 mm). Proto se měří.

**Šum** zvětší největší nárůst i kolísání nárůstu, takže podmínka
„< 0,5 × max“ nastane spíš **dřív** (výš, víc zbytku) – bezpečný směr.

### Jen pro vyhodnocení (nezastavují)

Log obsahuje surová data, takže se dají přehrát i další kritéria a
porovnat se změřenou výškou: podíl `C3/C1 ≤ 0,60` (odhad 10,0 ± 1,1 mm),
pokles horního prstence `C3 ≤ 0,7 ×` vlastního maxima (10,5 ± 0,7 mm).

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
| `# STOP koleno @ t=…` | zastavilo koleno |
| `# STOP vrchol @ t=…` | zastavila záloha (pozdě!) |
| `# *** SKOK C1 @ …` | poslední záchrana, série končí |
| `# obsluha: 9.5` | změřená výška [mm] |

V řádku je vždy `C1`, `C3`, `d` (okno) a `dmax` s časem.

Překlad (`avr-g++`): flash 25 588 B (78,1 %), SRAM 696 B (34,0 %), bez varování.
