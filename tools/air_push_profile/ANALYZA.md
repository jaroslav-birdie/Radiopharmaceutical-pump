# Test 1 – vyhodnocení

Data: 5 cyklů, 20 903 vzorků, 20 Hz, bez filtrace. Log z `air_push_profile.ino`
v konfiguraci `cykly=5 vzduch/cyklus=20.0 ml na naplnu=7.0 ml rychlost=1 ml/5 s`,
CAPDAC1 = CAPDAC2 = 0.

Otázka experimentu: **existuje při vytlačování vzduchem na CIN1 rozpoznatelný
přechod přes kritickou hladinu?**

---

## Odpověď: ano, ale ne jako zlom

C1 má ve všech pěti cyklech shodný třífázový průběh proti vytlačenému vzduchu:

| úsek | co se děje | chování C1 |
|------|-----------|------------|
| 0–6 ml | hladina je nad prstencem | **stoupá** o +0,13 až +0,19 pF (tlakový artefakt) |
| 7–14 ml | hladina prochází citlivou zónou prstence | monotónní rampa **−0,045 pF/ml** |
| nad 14 ml | lahvička prázdná | podlaha, šum spadne 4–5× |

Sklon rampy je napříč cykly stabilní na ±5 % (−0,0439 až −0,0481 pF/ml) – je to
zdaleka nejopakovatelnější veličina celého měření. Absolutní hodnoty naopak
opakovatelné nejsou.

### Čísla po cyklech

| cyklus | klid C1 | vrchol | podlaha | pokles | sklon rampy | šum (rampa) | nejistota polohy |
|--------|---------|--------|---------|--------|-------------|-------------|------------------|
| 1 | 3,7880 | 3,9248 | 3,2839 | 13,3 % | −0,0481 | 0,035 | 0,31 ml |
| 2 | 3,7408 | 4,0345 | 3,2931 | 12,0 % | −0,0439 | 0,044 | 0,45 ml |
| 3 | 3,8827 | 4,1315 | 3,3491 | 13,7 % | −0,0466 | 0,070 | 0,77 ml |
| 4 | 3,7565 | 4,0532 | 3,3425 | 11,0 % | −0,0451 | 0,047 | 0,63 ml |
| 5 | 3,7127 | 4,0159 | 3,3432 | 10,0 % | −0,0447 | 0,040 | 0,42 ml |

Hodnoty v pF, sklon v pF/ml, šum jako detrendovaná sd jednotlivého vzorku,
nejistota polohy po mediánovém filtru délky 1 s. Cyklus 3 je mechanický odlehlík.

---

## Co je vyvráceno

### Práh na % poklesu od kalibrované základny

Úplné vyprázdnění lahvičky odpovídá poklesu jen **10,0–13,7 %** proti klidové
základně. `CAP_CRITICAL_PERCENT 15` se tedy nespustí ani u prázdné lahvičky,
natož u kritických 3 ml.

Navíc klidová základna kolísá mezi cykly 3,71–3,88 pF (rozptyl 0,17 pF), zatímco
celý užitečný zdvih je 0,37–0,54 pF – rozptyl základny je třetina až polovina
signálu. Podlaha během 18 min běhu monotónně ujela o +0,055 pF (teplotní drift).

### „Zprůměrujeme to a šum zmizí"

Šum **neklesá jako 1/√N**. Mediánový filtr délky 40 (2 s) srazí sd z 0,035 jen na
0,013 pF místo teoretických 0,0055. Šum je nízkofrekvenční (1/f, mechanického
původu), takže delší okno prakticky nepomáhá. Nejistota polohy na rampě zůstane
0,3–0,8 ml.

### Zpomalení tlačení

Nejistota polohy = šum / sklon. Sklon v pF/ml je dán geometrií a na rychlosti
nezávisí; šum v pevném *časovém* okně na rychlosti také nezávisí (je 1/f).
Zpomalení na 1 ml/10 s tedy rozlišení nezlepší, jen prodlouží proceduru.
Zlepší ho jedině snížení samotného šumu – a ten je mechanický.

### Fáze `d` → `o` tlakový offset nezměřila

Rozdíl vyšel 0,000–0,014 pF, tedy nic. Konstrukční chyba testu: v okamžiku měření
byla lahvička už prázdná *a* celou dobu otevřená do pacientské hadičky, takže tlak
dávno odezněl. Tlakový artefakt je v datech vidět jinde – jako vzestup C1 v úseku
0–6 ml a jako propad během pauz.

---

## Co je nově změřeno

### Šum nepochází z krokového motoru

| stav | sd C1 |
|------|-------|
| prázdná lahvička, motor **běží** | 0,008 pF |
| plná lahvička, motor **stojí** | 0,016–0,10 pF |

Šum je multiplikativní a váže se na kapalinu, ne na krokování. Cyklus 3 je
4× hlučnější všude včetně podlahy – rozdíl mezi nejtišším a nejhlučnějším cyklem
je faktor 2 až 4 a leží v mechanice sestavy.

### Doběh po zastavení motoru je větší než chyba detekce

Po zastavení tlačení kapalina teče dál z uložené stlačitelnosti. Do 90 % dotečení
uplyne **7–15 s** (cyklus 3 se neustálil vůbec) a C1 mezitím spadne o dalších
0,08–0,11 pF – na stupnici rampy **zhruba 2 ml**, tedy víc než nejistota detekce
(0,3–0,8 ml).

**Přesah je dominantní chyba celé smyčky, ne šum senzoru.**

`FLUID_DRAIN_MS 5000` je podle těchto dat krátká: v 5. sekundě je dotečeno teprve
20–80 %.

### Stlačitelnost spolkne ~4 ml vzduchu

Deset ml kapaliny odešlo až po ~14 ml vytlačeného vzduchu, a část i to během 19s
pauzy bez tlačení. Rozdíl je stlačení headspace a poddajnost sestavy.

### C2 není šum

C2 nese stejnou informaci jako C1 (r = +0,55 až +0,80) s jen o málo horším poměrem
signál/šum (4,2–5,3 proti 5,5–7,2 u C1). Zdvih 1,6–2,1 pF, vrchol o ~1 ml později
než C1. Poměr C1/C2 tlakový artefakt **neodstraní** – obě elektrody na tlak reagují
souhlasně, takže podíl jen přidá šum (SNR spadne na 2,0–3,4).

---

## Návrh algoritmu, který data podpírají

Signál nemá v místě kritické hladiny žádnou událost – je tam uprostřed rampy.
Zato má dvě rozpoznatelné události: začátek rampy (obrat znaménka sklonu) a
podlahu (propad šumu). Kritická hladina leží mezi nimi.

1. **Nulovat na vrcholu, ne na kalibraci.** Sledovat běžící maximum C1 během
   tlačení a teprve od něj měřit pokles. Odpadne rozptyl klidové základny
   i teplotní drift podlahy.
2. **Detekovat vstup na rampu** jako trvale záporný sklon přes okno ~1 ml (5 s).
   Prahovat na sklonu, ne na hodnotě.
3. **Od vstupu na rampu odjet předem daný pokles v pF**, ne v %. Sklon je stabilní
   na ±5 %, takže pokles je dobrým měřítkem objemu.
4. **Odečíst přesah doběhu** – zastavovat o ~2 ml dřív, než cíl vypadá dosažený.
5. **Propad šumu použít jako záchranu, ne jako trigger.** sd < 0,015 pF přes 2 s =
   lahvička je prázdná, tedy stav, do kterého se procedura nikdy neměla dostat →
   alarm.

### Konstanty v `config.h`, které data zpochybňují

| konstanta | teď | podle dat | proč |
|-----------|-----|-----------|------|
| `CAP_CRITICAL_PERCENT` | 15 | nepoužitelná | prázdná lahvička = 10–14 % |
| `FLUID_DRAIN_MS` | 5 000 | 10 000–20 000 | v 5. s je dotečeno 20–80 % |
| `CAP_FLOW_EPS_PERMILLE` | 3 | ověřit | 3 ‰ z 3,7 pF = 0,011 pF, pod šumem jednoho vzorku (0,04) |
| `CAP_SAMPLE_MS` | 50 | 50 stačí | 20 Hz je s rezervou dost, užitečná jednotka je 1s okno |
| `VOL_AIR_ITER1_ML` | 5,0 | ověřit | jen stlačitelnost spolkne ~4 ml |

---

## Co musí změřit test 2

Tenhle test mapuje C1 na *vytlačený vzduch*. Chybí druhá polovina převodu:
**C1 na skutečně zbývající objem v lahvičce**. Bez ní nelze říct, kde na rampě
leží kritické 3 ml – a tedy ani nastavit jakoukoli konstantu.

1. **Referenční měření objemu.** Vytlačovat po 1 ml, po každém kroku pauza a odečet
   skutečně vyteklého objemu (odměrka na výstupu, značka do logu příkazem `#`).
   Vznikne kalibrační křivka C1 ↔ zbývající ml.
2. **Bez přerušení uprostřed rampy.** Doplnění vzduchu spadlo přesně na 7 a 14 ml
   a rozbilo obě zajímavá místa. Použít `l10` (dvě náplně) nebo pauzy naplánovat
   mimo oblast 8–15 ml.
3. **Změřit tlakový offset správně.** Zastavit s *plnou* lahvičkou, uzavřít
   pacientský ventil, chvíli měřit pod tlakem, pak odvzdušnit přes filtr a měřit
   znovu.
4. **Zopakovat po mechanickém zpevnění.** Faktor 2–4 v šumu leží v mechanice
   sestavy a je to jediná páka, která rozlišení reálně zlepší.
