# Test 3 – vyhodnocení (dva prstence, nestíněná penicilinka)

> **Oprava po testu 4:** závěr o „artefaktu vázaném na kapalinovou cestu
> k pacientovi" níže neplatí. Nárůst prstenců zůstává i v klidu s uzavřeným
> pacientem a odvzdušněnou lahvičkou – je to stav hladiny pod spodní hranou
> `SHLD2` (viz `CLAUDE.md`, „Výsledek testu 3"). Stejná data ukazují
> `C1 − C3` = +4,4 až +4,8 pF mezi prstenci ve všech pěti cyklech; na tom
> stojí detekce v `tools/level_stop/`.

Data: 5 cyklů, 22 112 řádků, 20 Hz, `f5`, `l10`, 20 ml vzduchu, CAPDAC1–3 = 0.
Náplněný objem do logu nezapsán (potvrzení prázdná) – absolutní polohy
přechodů proto nejde vztáhnout k objemu, jen mezi sebou.

## Shrnutí podle předem stanovených kritérií

| # | kritérium | výsledek |
|---|---|---|
| 1 | rozdílový příznak ostřejší než rampa | **ne** – hrany `C1 − C3` jsou jen hrany jednotlivých prstenců, šířka 10–90 % je 4,2–5,6 ml |
| 2 | stabilní rozestup přechodů C3 → C1 | **ano** – 5,34 ml, sd 0,27 ml, rozpětí 0,75 ml, i když náplň mezi cykly kolísala o ~2,3 ml |
| 3 | potlačení tlaku/artefaktu v `C1 − C3` | **jen částečně** – funguje, dokud jsou zakryté oba prstence; v kritickém okně ne (viz níže) |
| 4 | kladná korelace rychlého šumu C1/C3 | **ne** – r = −0,26 až +0,24, rozdíl šum sčítá |

## Hlavní nález: velký artefakt vázaný na kapalinovou cestu k pacientovi

Klid před tlačením (`k`, plná lahvička): C1 ≈ C3 ≈ 3,0 pF, `C1 − C3` = +0,05 pF
(prstence jsou dobře spárované). Prázdná lahvička (`o`): C1 2,5, C3 2,03 pF.
**Užitečný signál hladiny v klidu je tedy jen ~0,5 pF (C1) a ~1 pF (C3).**

Během tlačení ale oba prstence od ~1,5 ml stoupnou na 5,5 (C3) až 7 pF (C1),
tj. **+2,5 až +4 pF – pětkrát víc než samotný signál hladiny.** V testu 1 byl
nárůst jen +0,15 pF.

Artefakt je jednoznačně spjatý s pacientským ventilem a kapalinou u elektrody:

| událost | C1 | C2 | C3 |
|---|---|---|---|
| zavření pacientského ventilu při 5 ml (oba prstence zakryté) | −1,0 | −0,4 | −0,9 |
| otevření | +1,0 až +1,3 | +0,4 | +0,8 až +1,2 |
| zavření při 15 ml (C3 už odkrytý) | −0,3 až −1,0 | −0,2 | **−0,05** |

C3 bez kapaliny nereaguje – **artefakt je multiplikativní**: zesiluje jen tu
část kapacity, kterou zprostředkuje kapalina. To odpovídá hypotéze
nedefinované zpáteční cesty (CLAUDE.md, varianta B): sloupec kapaliny
v pacientské hadičce dává vodě v lahvičce návrat přes okolí a vazba elektrody
na vodu tím mnohonásobně vzroste. Mechanismus zatím **není ověřen**.

Důsledek pro kritérium 3: dokud jsou zakryté oba prstence, `C1 − C3` artefakt
odečte (nárůst do 5 ml jen +0,05 až +0,5 místo +2,5; skok ventilu −0,08 místo
−1,0). Jakmile je hladina mezi prstenci – tedy přesně v kritickém okně –
artefakt nese už jen C1 a v rozdílu zůstává celý.

## Profil (medián po 0,5 ml, fáze `w`)

- C3 klesá z ~5,5 na podlahu ~2,2 pF; 50 % při 7,9–10,2 ml (podle náplně)
- C1 klesá ze ~7 na ~3,5 pF a pak pomalu na 2,6; 50 % při 13,6–15,2 ml
- C2 kopíruje artefakt (2,2 → 3,1 → 2,0), jako signál hladiny je slabý

## Nedostatek měření

Přechod C1 (12–19 ml) **ve všech cyklech přetíná doplnění při 15 ml** včetně
skoku pacientského ventilu −0,8 pF. Poloha 50 % bodu C1 je tím zatížená.
Oba přechody dohromady zabírají 6–19 ml, což se do 10 ml souvislého úseku
nevejde – další měření je potřeba rozdělit nebo `f` volit podle toho, který
přechod se měří.

## Šum

| úsek | C1 | C3 | `C1 − C3` |
|---|---|---|---|
| 0–5 ml | 0,14–0,33 | 0,14–0,29 | 0,20–0,49 |
| 5–15 ml | 0,35–0,61 | 0,17–0,24 | 0,39–0,66 |
| 17–20 ml (prázdno) | 0,11–0,13 | 0,02–0,03 | 0,11–0,13 |

(sd rezidua od klouzavého mediánu 2 s, pF). Za tlačení je šum 5–10× větší
než v klidu (0,03 pF) a roste s artefaktem. C1 je i v prázdnu 4× hlučnější než
C3 – nejspíš zbytky kapaliny/bubliny u spodního prstence blízko jehel.

## Závěr

Druhý prstenec **nepřinesl ostřejší hranu**, ale přinesl **vnitřní měřítko**:
přechod C3 předchází přechodu C1 o stálých 5,3 ± 0,3 ml vytlačeného vzduchu
nezávisle na náplni. To je použitelné jako předpověď blížící se kritické
hladiny bez externí kalibrace.

Dominantní problém ale není geometrie, nýbrž **multiplikativní artefakt
vázaný na kapalinovou cestu k pacientovi** (+2,5 až +4 pF, skoky ±1 pF při
pohybu ventilu). Ten je potřeba nejdřív vysvětlit a odstranit (definovaná
zem/zpáteční cesta), jinak žádná kombinace elektrod v kritickém okně
spolehlivě nefunguje.
