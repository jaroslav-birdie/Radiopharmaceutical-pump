# capacitive_protocol_test – detekce kritické hladiny při vytlačování **vzduchem**

Poslední otevřená otázka celé analýzy kapacitní detekce: **půjde pokles na
kritickou hladinu detekovat v situaci, která odpovídá reálnému přístroji?**

Všechna dosavadní data (`capacitive_cycle_test`, `capacitive_edge_detect_test`)
vznikla tak, že hladinou hýbal **stříkačkový motor přímo** – odsával kapalinu
z lahvičky. To je tvrdý, přesný pohyb: kolik kroků, tolik mililitrů. Reálný
přístroj ale kapalinu **vytlačuje vzduchem**. Mezi motorem a hladinou je
stlačitelný sloupec vzduchu (stříkačka + hadičky + headspace), takže:

- pohyb hladiny je pomalejší, měkčí a **nelineární** vůči krokům motoru,
- na začátku zdvihu se hladina nehne skoro vůbec (jen se stlačuje vzduch),
- přibývá vibrací a EMI z druhého motoru a ze servo ventilů.

Právě v tomhle režimu se musí detekce osvědčit. Tento sketch nic nepřemapovává
ani nesimuluje – jede protokol tak, jak ho jede přístroj.

```
naplnění lahvičky na 10 ml   (odvzdušněnou lahvičkou, motorem roztoku)
extrakce 0  : vytlačit vzduchem → kritická hladina
extrakce 1  : +3 ml roztoku → vytlačit vzduchem → kritická hladina
   ...
extrakce 12 : +3 ml roztoku → vytlačit vzduchem → kritická hladina
```

Běh je zcela automatický, obsluha po `g` nezasahuje. Trvá zhruba 60–90 minut.

---

## Řízení pumpy

Piny, úhly ventilů, pořadí kroků, rezerva vzduchové stříkačky i doběhy jsou
převzaté 1:1 z `Radiopharmaceutical-pump.ino` / `state_machine.cpp` / `config.h`.
Úhly ventilů se **čtou z EEPROM** (stejný magic flag i adresy jako firmware),
takže sketch používá úhly nakalibrované pro konkrétní kus; když je EEPROM
neplatná, vezmou se výchozí hodnoty z `config.h`.

| Prvek | Pin | Poznámka |
|---|---|---|
| Vzduchový stepper | D4 STEP / D5 DIR | tlačí i nasává |
| Stepper roztoku | D6 STEP / D7 DIR | **jen tlačí** |
| nENBL obou driverů | A3 | sepnut po celý běh |
| Servo pacientský ventil | D9 (OC1A) | |
| Servo vzduchový ventil | D10 (OC1B) | |

---

## Tři pravidla ze zadání a jak jsou vynucená

**1. Oba drivery zůstávají po celý běh ENABLED.**
`nENBL` se sepne jednou při `g` a během běhu se už nikdy nepustí – ani při
pauze kvůli rušení, ani mezi iteracemi, ani při přejezdu ventilů. Motory tak
drží polohu pístu proti zpětnému tlaku. DISABLE nastane jen na úplném konci
běhu nebo při nouzovém `x` (totéž co `ST_COMPLETE` / `ST_EMERGENCY_STOP`
ve firmwaru).

**2. Motor roztoku nikdy necouvne.**
Do `PIN_SAL_DIR` se zapisuje **jediným řádkem v `setup()`**, kde se nastaví na
`SAL_DIR_PUSH_LEVEL`. V celém zbytku sketche do toho pinu nikdo nepíše a
neexistuje funkce, která by uměla roztokem couvnout – ani jog (`l` jede taky
jen dopředu). Není to disciplína, je to struktura kódu.

**3. Při doplňování roztoku je vzduchový ventil otevřen do atmosféry.**
Pořadí je: vzduchový ventil → `V↔F` → vyrovnat tlak → **doplnit 3 ml roztoku**
→ vyrovnat tlak → teprve pak ventil → `S↔F` → **nasát vzduch**. Funkce
`salDispense()` navíc kontroluje polohu vzduchového ventilu a při jiné než
`V↔F` běh zastaví s chybou. Souběh obou operací se v tomhle projektu už jednou
zkoušel a selhal právě na neodvzdušněné lahvičce – viz `CLAUDE.md`,
sekce „Co bylo vyzkoušeno a NEFUNGUJE".

Totéž platí pro počáteční naplnění lahvičky na 10 ml – jede přes stejnou cestu,
tedy taky do odvzdušněné lahvičky.

---

## Průběh jedné extrakce

1. Vzduchový ventil `S↔V`, pacientský ventil `OPEN`.
2. Ustálení ~5 s + naplnění vyhlazovacího okna (25 vzorků) – motory stojí.
   Odtud se bere klidová část kalibrace ochrany CIN2.
3. Vytlačování: vzduchový motor tlačí, dokud stříkačka nedojede na trvalou
   rezervu (`VOL_AIR_RESERVE_ML` = 3 ml). Prvních 20 vzorků běhu tvoří „živé"
   kalibrační okno ochrany CIN2 (motor už běží – viz v9), teprve po něm se
   vyhodnotí práh.
4. Nestačí-li vzduch, proběhne **doplnění**: doběh kapaliny → pacient
   `ISOLATE` → lahvička `V↔F` + vyrovnání → `S↔F` + nasátí na plnou →
   vyrovnání → `S↔V` → pacient `OPEN` → obnova vyhlazovacího okna → pokračuje
   se **v téže extrakci**. Max. `MAX_AIR_REFILLS` = 5, pak alarm.
5. Detekce kritické hladiny: pokles vyhlazeného C1 o `deltaCritical` = 0,22 pF
   od neomezeného maxima od začátku extrakce, potvrzený 5 vzorky po sobě.
6. Po detekci: motor stop, doběh kapaliny 5 s s otevřeným pacientem, pacient
   `ISOLATE`, vzduchový ventil `V↔F`.

### Co přežije přerušení a co ne

Sledovaný vrchol C1 se resetuje **na začátku každé extrakce** a nikde jinde.
Přežije tedy jak doplnění vzduchu, tak pauzu kvůli rušení CIN2 – v obou
případech jde pořád o totéž vytlačování a hladina se mezitím nehýbe. Reset by
znamenal, že se sledování rozjede znovu od už poklesnuté hodnoty, takže by se
kritická hladina odhalila **později** – nebezpečný směr chyby.

Vyhlazovací okno se naopak po každém přerušení **zahodí a naplní znovu**.
Drží 25 vzorků zpětně, takže by po obnovení obsahovalo vzorky naměřené během
přerušení; kdyby rušení C1 zvedlo, nafouklo by to sledovaný vrchol – přesně
mechanismus obou selhání zdokumentovaných u ochrany CIN2 v `CLAUDE.md`.

---

## Jak se měří „hloubka" bez znalosti objemu v lahvičce

Při odsávání stříkačkou byl objem v lahvičce přesně znám z kroků motoru. Teď
už ne – část vtlačeného vzduchu se jen stlačí a kapalinu nevytlačí. Měřenou
veličinou je proto objem **vzduchu** vytlačený mezi vrcholem C1 a sepnutím
prahu (`hloubka_ml` ve výstupu).

Při modelu „komprese spotřebuje na začátku zdvihu pevný objem *C* a dál je
převod vzduch→kapalina 1:1" platí, že v ustáleném stavu vyteče právě dávka
(3 ml), takže *C* = `vzduch_celkem − 3`, a po dosazení:

```
hloubka_kapalina = vzduch_celkem − vzduch_při_vrcholu = hloubka_vzduch
```

Hloubka měřená ve vzduchu je tedy **přímo hloubka v mililitrech kapaliny**.
Sketch proto hlásí i `komprese_odh` = `vzduch_celkem − dávka`; když se ta mezi
iteracemi vyrazně mění, model neplatí a je to v datech vidět.

**Klíčová podmínka je pořád stejná:** hloubka musí vyjít **menší než 3 ml**,
jinak další extrakce začne už na sestupné větvi a vrchol se v ní nikdy nenajde.

---

## Formát logu

Hlavička per vzorek:

```
it;t_ms;faze;vzduch_ml;vytlaceno_ml;roztok_ml;servo_pac;servo_vzd;C1_raw;C1_sm;vrchol;C2_raw;c2rate
```

| Sloupec | Význam |
|---|---|
| `it` | 0 = fáze 1, 1..12 = iterace |
| `faze` | viz tabulka níže |
| `vzduch_ml` | obsah vzduchové stříkačky |
| `vytlaceno_ml` | kumulativně vytlačený vzduch **v této extrakci** (přes doplnění) |
| `roztok_ml` | kumulativně podaný roztok (jen roste) |
| `servo_pac` / `servo_vzd` | aktuální úhly obou ventilů |
| `C1_raw` / `C1_sm` | prstencová elektroda, syrová a MA25 |
| `vrchol` | sledované maximum, od kterého se počítá pokles |
| `C2_raw` / `c2rate` | svislá elektroda a rychlost její změny (ochrana) |

Znaky fáze: `F` počáteční náplň · `r` doplnění roztoku · `e` vyrovnání tlaku ·
`a` nasávání vzduchu · `v` přejezd ventilu · `s` ustálení před extrakcí ·
`q` obnova vyhlazovacího okna · `w` **vytlačování** · `d` doběh kapaliny ·
`p` pauza kvůli rušení CIN2

Řádky `#>` nesou výsledek extrakce a na konci souhrn:

```
#> it;vzduch_pri_vrcholu_ml;vzduch_celkem_ml;hloubka_ml;komprese_odh_ml;vrchol_C1;sepnuti_C1;pokles_pF;doplneni;pauzy;novy_vrchol
```

`novy_vrchol = 1` znamená, že sledované maximum přelezlo startovní hodnotu,
tedy že hladina v této iteraci vrchol křivky **znovu projela**. Kdyby vycházelo
`0` napříč iteracemi, pohybujeme se jen po sestupné větvi a algoritmus měří
pokles od náhodného startovního bodu, ne od skutečného vrcholu.

---

## Postup na hardwaru

1. Vzduchová stříkačka **plná** (10 ml), stříkačka roztoku **plná** (60 ml).
   Celková spotřeba je 10 + 12×3 = 46 ml, takže se to vejde a motor nikdy
   nemusí couvnout.
2. Osadit oba ventily a obě stříkačky; **prázdná** lahvička (10ml varianta)
   s jehlami se připojuje jako **poslední** – stejné instalační pořadí jako
   u přístroje.
3. `t` – deklarace výchozího stavu (vzduch 10 ml, roztok 0 ml podáno).
   Volitelně `u0`–`u4` na kontrolu úhlů ventilů, `n` na test šumu.
4. `g` – a dál už nic.
5. `x` kdykoli = okamžité zastavení (motory se odpojí, ventily do bezpečných
   poloh).

Je-li lahvička plněná ručně, vypnout `f0` a nastavit `v<ml>`.

Zachycení logu: `tools/serial_log.py`.

---

## Příkazy

| | |
|---|---|
| `h` `i` | nápověda, info |
| `a` `n` | autoCAPDAC, test šumu |
| `t` | deklarace výchozího stavu |
| `g` `x` | start / nouzové zastavení |
| `j` `k` | jog vzduchu ±0,5 ml |
| `l` | jog roztoku +0,5 ml (**jen dopředu**) |
| `u0`–`u4` | pacient IZOLACE / OTEVŘENO, vzduch S↔V / S↔F / V↔F |
| `r<n>` `s<ml>` `v<ml>` `f0`/`f1` | počet iterací, dávka, počáteční náplň, autofill |
| `dc<pF>` `cf<n>` `p<ms>` | práh detekce, potvrzovacích vzorků, perioda vzorku |
| `cm<x>` `cg<pF>` `ck<n>` | ochrana CIN2: násobitel (0 = vypnout), podlaha, potvrzení |
| `#<text>` | značka do logu |

---

## Kompilace

```
arduino-cli compile --fqbn arduino:avr:uno tools/capacitive_protocol_test
```

Poslední ověřený překlad: **Flash 23 572 B (73,1 %), SRAM 934 B (45,6 %)**,
bez varování.
