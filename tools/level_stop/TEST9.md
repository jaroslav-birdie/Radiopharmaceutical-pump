# Test 9 – zastavení podle šablony C3 / max C3

Sketch `level_stop.ino`, režim zastavení (`v0`, výchozí). Studna 3 / 3 / 3 mm
beze změny. Postup je **stejný jako v testech 7 a 8** (`TEST7.md`, oddíl
Postup): 5 cyklů, neznámá náplň 9–14 ml, po zastavení a 30 s klidu vyndat
lahvičku, změřit výšku hladiny od vnějšího dna a poslat číslo.

## Otázka

> **Zastaví šablona hladinu u cíle ≈ 8,5 mm (≈ 4 ml), a kopíruje signál
> šablonu (kontrola shody bez falešných poplachů)?**

## Princip (viz `ANALYZA_TEST8.md`, oddíl Šablona)

Bez časové osy: postup hladiny udává pokles horního prstence proti jeho
vlastnímu maximu v běhu, `C3 / max C3`. V těchto souřadnicích se všech
15 cyklů testů 6–8 kryje. Rychlost hladiny ani čas se nepoužívají.

| kritérium (okno 8 košů = 4,8 s, od aktivace C3 ≤ 0,85 × max) | bod šablony* |
|---|---|
| **šablona:** `C3 ≤ 0,78 × max C3` | ≈ 8,8 mm |
| **podíl:** `C3/C1 ≤ 0,68` | ≈ 8,9 mm |
| koleno (záloha) | test 8: 6,7 mm |
| vrchol (záloha) | test 7: 4,1 mm |
| skok C1 (poslední záchrana) | – |

Zastaví **první** z nich. Šum je může jen uspíšit (víc zbytku).

\* Polohy příznaků z 10 změřených výšek testů 7 a 8 (proložení, jen
analýza). Přehrání na 15 cyklech: **8,7 ± 0,45 mm (8,1–9,6 mm)**,
zastavuje vždy šablona při C3/max 0,77–0,79.

## Kontrola shody se šablonou (jen zápis, nezastavuje)

| kontrola | podmínka poruchy | testy 6–8 (15 cyklů) |
|---|---|---|
| C1 klesá předčasně | `C1 < 0,95 × max C1`, dokud `C3 > 0,65 × max C3` | nejníž 0,966 |
| rozdíl neroste | při `C3 = 0,80 × max C3` je `(C1 − C3) − d₀ < 0,15 × max C3` | 0,30–0,36 |

Porucha se zapíše jednou: `# SHODA *** C1 klesa predcasne @ t=…`.
Při zastavení se vždy vypíše řádek se souřadnicemi šablony:

```
# STOP sablona C3 @ t=… ms  C1=… C3=… d=… d0=… dmax=… @ t=…
#   C3/maxC3=0.779 C1/maxC1=1.000 (d-d0)/maxC3=0.33  shoda OK
```

Kontrola bude moci zastavovat až po ověření, že nehlásí falešné poplachy.

## Kritéria (stanovená PŘEDEM)

| výsledek | význam |
|---|---|
| všech 5 výšek 8–10 mm, žádná `SHODA ***` | cíl splněn |
| výšky opakovatelné, ale posunuté | posunout práh šablony (0,05 ≈ 0,7 mm) |
| výška < 7 mm | nepřijatelné |
| `SHODA ***` bez zjevné příčiny | upravit práh kontroly |

## Změny ve sketchi

- nová kritéria šablona a podíl, kontrola shody,
- **odebráno měření suchých elektrod `D`** (test 6 hotový), aby se vešla
  flash.

Překlad (`avr-g++`): flash 25 228 B (77,0 %), SRAM 697 B (34,0 %), bez varování.
