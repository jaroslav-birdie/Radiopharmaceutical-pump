# Test 2 — opakovaná detekce kritické hladiny (FDC1004)

Bench nástroj pro ověření **spolehlivosti detekce kritické hladiny** algoritmem
odvozeným z dat testu 1 (`tools/capacitive_response_test/`). Na rozdíl od testu 1,
který jen logoval syrová data, tenhle sketch hladinu **skutečně hledá** a po každém
nálezu se ptá obsluhy, jestli trefil.

Sketch je **samostatný diagnostický nástroj**, nemá nic společného s firmware
čerpadla. Po dokončení testů se z Arduina opět nahraje ostrý firmware.

## Co se testuje

Detekční algoritmus, tak jak se plánuje nasadit do firmware:

| Prvek | Hodnota | Proč |
|---|---|---|
| Filtr | klouzavý **medián** 25 vzorků | medián zahodí krátké výkyvy, které by přes průměr nafoukly sledované maximum a posunuly detekci; na datech testu 1 rezerva 10,6× místo 4,8× |
| Reference | sledované **maximum** mediánu, monotónně neklesající | nezávislé na absolutní kapacitě, takže ho nerozhodí jiná lahvička, posazení ve studni ani teplotní drift |
| Práh | pokles o `deltaCritical` = **0,22 pF** | jediná hodnota, která na datech testu 1 drží do 4násobku okolního šumu |
| Potvrzení | **5** vzorků po sobě | |

Na 20 cyklech testu 1 tahle kombinace sepnula **20/20** s rozptylem bodu detekce
0,40 ml. Test 2 má ukázat, jestli to platí i při opakovaném hledání s různou
výchozí hladinou a v přítomnosti rušení.

> **Rozběh filtru je vyřešený:** mediánové okno se plní už během fáze `settle`,
> takže do odsávání vstupuje naplněné. Sledované maximum startuje až se začátkem
> odsávání, tedy na skutečné plošině.

## Rušení se záměrně NEdetekuje

Hlídka přes CIN2 v tomhle sketchi **není** a nic odsávání nezastaví. C2 se pouze
loguje, aby se rušivé úseky daly najít až při offline analýze. Smyslem je zjistit,
jestli (a jak) rušení dokáže detekci rozhodit, když ji nikdo nechrání — teprve
podle toho se rozhodne, jaká ochrana je potřeba.

Ze stejného důvodu se sloupec `slope10` (změna mediánu přes 10 vzorků) jen **loguje
a nepoužívá jako podmínka**. Offline se tak dá vyhodnotit, jestli by AND-podmínka
na sklon pomohla nebo uškodila, bez rizika, že teď potlačí detekci a znehodnotí
měření.

## Průběh jednoho pokusu

```
settle  (5 s, motor stojí)    → naplní mediánové okno
   ↓
pull    (odsávání)            → do detekce, nebo do vyčerpání maxPullMl
   ↓
hold    (motor stojí)         → obsluha zkontroluje hladinu, pošle 1 / 0
   ↓
refill  (doplnění 5–15 ml)    → náhodný objem, reset pro další pokus
```

Vzorkuje a loguje se ve **všech čtyřech fázích** včetně čekání na obsluhu —
klidová data mezi pohyby jsou součást měření.

Fáze `refill` je čistě **reset mezi pokusy**. V ostrém provozu se z lahvičky
jenom odsává, takže se z doplňování nic nevyhodnocuje.

## Postup

1. **Nahrát sketch** do Arduina (Arduino IDE, Upload).
2. **Stříkačku** naplnit na ~30 ml (potřebuje prostor na obě strany).
3. **Lahvičku naplnit RUČNĚ výrazně NAD kritickou hladinu**, vložit do studny.
4. **Serial Monitor**, 9600 Bd, zakončení Enter.
5. `a` — ověřit CAPDAC (po autokalibraci mají vyjít nízké hodnoty, viz `CLAUDE.md`).
   Volitelně `n` — statický test šumu (nehýbat sestavou).
6. `o` — driver ON, volitelně `j`/`k` — odvzdušnění.
7. `t` — tare (aktuální poloha = 0,00 ml, referenční).
8. `g` — **spustí všech 20 pokusů**.
9. Po každém `HOLD` očima zkontrolovat hladinu a poslat **`1`** (trefeno) nebo
   **`0`** (netrefeno). Bez odpovědi se čeká libovolně dlouho, vzorkuje se dál.
10. `x` kdykoliv za běhu = okamžité zastavení, vypne driver.
11. Po `# === TEST 2 KONEC ===` zkopírovat celý výstup a předat ke zpracování.

Pokus o `g` bez nového `t` po předchozím (dokončeném i přerušeném) běhu skončí
chybovou hláškou — vždy nejdřív `t`.

### Rušení

Rušení zaváděj podle vlastního uvážení u libovolných pokusů (dotek studny, zapnutá
zátěž vedle, pohyb kabeláží). **Neoznačuj je** — jejich identifikace z dat je
součástí zadání analýzy. Stačí, když si zapíšeš, cos dělal, pro pozdější kontrolu.

## Lahvička: použij 20ml variantu

Obsah lahvičky osciluje mezi kritickou hladinou a kritickou hladinou + doplněný
objem. Při výchozím `wh15` je potřeba, aby se do lahvičky vešlo kritické množství
+ 15 ml. Do 10ml varianty se to nevejde.

Pokud chceš jet na 10ml lahvičce, sniž horní mez, např. `wh8`.

## Parametry (nastavit před `t`/`g`)

| Příkaz | Význam | Výchozí |
|---|---|---|
| `d<pF>` | `deltaCritical` — pokles pod sledované maximum | `d0.22` |
| `c<n>` | potvrzení N vzorky po sobě | `c5` |
| `r<n>` | počet pokusů | `r20` |
| `wl<ml>` / `wh<ml>` | dolní / horní mez náhodného doplnění | `wl5` / `wh15` |
| `m<ml>` | max. zdvih odsávání bez detekce (pojistka) | `m25` |
| `p<ms>` | perioda vzorku | `p200` |
| `f<s>` | tempo pohybu v s/ml | `f5` |
| `s<n>` | seed generátoru (0 = z `micros()` při `g`) | `s0` |

`i` kdykoliv vypíše aktuální hodnoty a stav. `n` (noise test) a `a` (autoCAPDAC)
stejné jako v ostatních nástrojích.

> **Periodu vzorku a tempo neměň bez důvodu.** `p200` a `f5` jsou shodné s testem 1,
> na kterém je práh 0,22 pF ověřený. Tempo navíc posouvá bod detekce — po zastavení
> motoru klesá C1 dál o ~240 fF, takže signál za skutečnou hladinou zaostává
> a zaostávání roste s rychlostí.

Délka mediánového okna (25) je pevná — je to velikost bufferu, mění se jen
v `MED_N` a vyžaduje překompilování.

### Odhad délky běhu

Při výchozích parametrech vychází jeden pokus zhruba na 5 s ustálení + 25–75 s
odsávání + čekání na obsluhu + 25–75 s doplňování, tedy **1–3 min**. Dvacet pokusů
je odhadem **45–70 min** čistého strojového času plus tvoje reakce na 20 dotazů.
Pro pohodlné logování zvaž terminál s přímým ukládáním do souboru místo
kopírování ze Serial Monitoru.

## Formát výstupu

```
# === TEST 2: 20 pokusu o detekci kriticke hladiny ===
# seed=3948217
# att;t_ms;pos_ml;pull_ml;phase;C1_pF;C2_pF;med_pF;max_pF;drop_pF;slope10
# --- pokus 1/20
1;55;0.00;0.00;settle;8.4962;4.1576;;;;
...
1;5000;0.00;0.00;settle;8.4931;4.1564;8.4901;8.4901;0.0000;-0.0004
1;5200;-0.03;0.03;pull;8.4948;4.1552;8.4903;8.4903;0.0000;0.0002
...
1;98400;-18.18;18.18;pull;7.9012;3.4180;8.2661;8.4885;0.2224;-0.0812
# att=1 HOLD - zkontroluj hladinu, posli 1 (spravne) / 0 (spatne)
1;98600;-18.18;18.18;hold;7.8904;3.4166;8.2601;8.4885;0.2284;-0.0798
...
# VYSLEDEK att=1 detekce=ano pull=18.18 pos=-18.18 potvrzeno=1 refill=11.3
1;112000;-18.15;18.15;refill;7.9102;3.4201;8.2588;8.4885;0.2297;-0.0011
...
# --- pokus 2/20
...
# === TEST 2 KONEC ===
```

- `att` — číslo pokusu (1..`attemptCount`).
- `pos_ml` — **signovaná odchylka polohy stříkačky od `t`** (záporně = odsáto).
- `pull_ml` — kolik ml se odsálo v rámci aktuálního zdvihu (0,00 během `settle`).
- `phase` — `settle` / `pull` / `hold` / `refill`.
- `med_pF` — klouzavý medián 25 vzorků z C1, tj. to, na čem detektor rozhoduje.
- `max_pF` — sledované maximum mediánu (během `settle` se rovná mediánu).
- `drop_pF` — `max_pF − med_pF`, veličina porovnávaná s `deltaCritical`.
- `slope10` — změna mediánu přes 10 vzorků. **Jen se loguje, nic negatuje.**
- Prázdné poslední čtyři sloupce = mediánové okno ještě není plné (prvních 25 vzorků).

### Řádky se souhrnem

- `# VYSLEDEK att=n detekce=ano/ne pull=..ml pos=..ml potvrzeno=1/0 refill=..ml`
  — jeden na pokus, strojově čitelný.
- `# att=n BEZ DETEKCE - vycerpano maxPull ..` — pojistka sepnula dřív než detektor.
  Pokus pokračuje normálně dál (dotaz na obsluhu i doplnění), jen `detekce=ne`.
- `# VAROVANI att=n: C1 po doplneni stoupla jen o .. pF` — hladina se po doplnění
  nevrátila nad prstenec, takže další pokus nemá na čem detekovat. **Nic to
  nezastavuje**, jen se to označí v datech.

## Bezpečnostní meze

- **Mechanická ochrana zdvihu** je dvoustranná: −30 ml / +12 ml od tare. Při
  překročení se běh zastaví a driver vypne.
- **`maxPullMl`** (výchozí 25 ml) omezuje jediný zdvih odsávání. Bez ní by
  nedetekující cyklus dojel na doraz stříkačky.
- Během `hold` zůstává driver **ENABLED** — drží píst proti zpětnému tlaku,
  stejně jako `ST_PAUSED` v ostrém firmware.

## Co s daty dál

Analýza má odpovědět na tři věci:

1. **Opakovatelnost** — rozptyl `pull_ml` v okamžiku detekce napříč 20 pokusy,
   a jestli závisí na velikosti předchozího doplnění.
2. **Shoda s obsluhou** — kolik pokusů má `potvrzeno=0` a co se v těch datech
   liší (`drop_pF`, `slope10`, průběh C2).
3. **Rušení** — najít rušené úseky z C1/C2, zjistit, jestli způsobily chybnou
   detekci, a teprve podle toho navrhnout ochranu.
