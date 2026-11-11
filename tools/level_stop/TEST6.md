# Test 6 – profily ve studni, suché elektrody

Sketch `level_stop.ino` (stejný jako test 5, režim PROFIL). Nově:
- **studna** místo elektrod nalepených na lahvičce,
- **měření suchých elektrod** (příkaz `D`),
- **mapa kanálů nastavitelná za běhu** (příkaz `n`).

## Geometrie studny

Výšky od dosedací plochy dna (= vnější dno lahvičky). Objem podle
`h = 2,5 + 1,5·V` (`CLAUDE.md`, „Výška hladiny v penicilince").

| prvek | výška | objem v lahvičce |
|---|---|---|
| spodní prstenec | 5,5 – 8,5 mm | 2,0 – 4,0 ml |
| mezera | 8,5 – 11,5 mm | 4,0 – 6,0 ml (2,0 ml) |
| horní prstenec | 11,5 – 14,5 mm | 6,0 – 8,0 ml |
| zastavení (spodní hrana horního prstence) | 11,5 mm | **R = 6,0 ml** |
| SHLD2 | *doplnit* | |
| svislá elektroda | *doplnit* | |

Proti zkušební lahvičce (2 / 4 / 2 mm) jsou prstence vyšší a mezera užší
(3 / 3 / 3 mm). Hodnoty z testu 5 (práh C3/C1 0,45, plató 0,40) proto
neplatí a určí se z tohoto testu.

---

## Otázky

1. **Jak velké jsou suché kapacity** prstenců bez lahvičky, kolik přidá
   prázdná lahvička (sklo) a jak opakovatelně lahvička ve studni sedí?
2. **Jak vypadají průběhy** obou prstenců a svislé elektrody ve studni
   (šířka hran, plató, kontrast)?
3. **Jakou kombinací signálů** spolehlivě poznat hladinu mezi prstenci,
   každý běh zvlášť a bez učení?

Nic se nezastavuje. Detekce jen zapisuje (`# STIN …`) a výsledek určí až
analýza.

---

## Postup

### 0. Příprava

1. Nahrát `level_stop.ino`, zachytávat log (**115200 Bd**):
   ```
   tools/serial_log.py /dev/ttyUSB0 test6.csv --baud 115200
   ```
2. Po startu zkontrolovat `# FDC1004 meri OK`.
3. Vzduchová stříkačka **plná**, ventily a stříkačka osazené, pacientská
   hadička naplněná. Konec hadičky po celý test na stejném místě.

### 1. Dotykový test a mapa kanálů (prázdná studna)

1. `m` – průběžný výpis C1;C2;C3.
2. Prstem do studny postupně na **spodní prstenec, svislou elektrodu,
   horní prstenec**, vždy pár sekund.
3. Správně: spodní → **C1**, svislá → **C2**, horní → **C3** (16 pF).
4. Pokud ne, nastavit mapu `n<spodní><svislá><horní>` podle toho, který
   CIN se rozkmitá (např. `n213`), a dotykový test zopakovat.

Výchozí mapa po startu je `n123` (podle popisků).

### 2. Suché elektrody – příkaz `D`

Sketch provede obsluhu výzvami. Každý krok měří 10 s:

| krok | stav | fáze v logu |
|---|---|---|
| 1 | studna **bez lahvičky** | `b` |
| 2–4 | **prázdná suchá lahvička**, před každým krokem vyndat a znovu vložit (3×) | `l` |
| 5 | znovu **bez lahvičky** (drift) | `b` |

Po každém kroku se vypíše `# suche b: C1=… sd=… C2=… C3=…`.

**Proč obojí:** v přístroji lze změřit jen stav **bez lahvičky**.
Lahvička přichází naplněná a vkládá se až po kalibraci. Prázdná lahvička
ukáže, kolik přidá sklo. Je-li to málo a stále, stačí v provozu měření bez
lahvičky. Opakované vložení ukáže, jak přesně lahvička sedí, což je nová
nejistota proti nalepeným elektrodám.

Lahvička musí být **suchá** (bez kapek uvnitř i venku) a vkládat se vždy
stejně až na doraz.

### 3. Pět cyklů – `t`, `g`

Výchozí nastavení: 5 cyklů, profil 20 ml (`i` musí ukázat
`REZIM: PROFIL, vzduch na cyklus 20.0 ml` a `cykly=5`).

V každém cyklu:
1. Výzva: obsluha **naplní lahvičku od oka, pokaždé jinak**, a vloží ji
   do studny. Náplň volit tak, aby:
   - hladina byla na začátku **nad horním prstencem** (> 14,5 mm, tj. víc
     než ~8 ml), jinak se nezachytí celý přechod horního prstence,
   - po 20 ml vzduchu byla lahvička **prázdná** (v testu 5 se 20 ml
     vyprázdnilo ~13 ml; předtlak spotřebuje 2,7 ml).

   Doporučeno ~9–12 ml. Objem není potřeba zapisovat.
2. Vytlačí se 20 ml vzduchu, pacient se po dojezdu hned zavře, klid pod
   tlakem, odvzdušnění, 30 s klidu, doplnění stříkačky.
3. Pokud po cyklu v lahvičce zůstala kapalina, napsat při dalším potvrzení
   `zbytek`.

Logují se všechny tři elektrody zvlášť, 20 vzorků/s, bez filtrace.

---

## Co se bude vyhodnocovat

- **Suché kapacity:** bez lahvičky / s prázdnou lahvičkou, rozptyl mezi
  vloženími, drift mezi prvním a posledním `b`.
- **Průběhy** C1, C2, C3 v každém cyklu. Šířka hran (10–90 %), poloha
  přechodů a plató mezi prstenci.
- **Kombinace signálů, každý běh zvlášť:**
  - C3/C1,
  - korigovaný podíl `(C3 − P3)/(C1 − P1)` s P **bez lahvičky** i **s
    prázdnou lahvičkou**,
  - pokles C3 od vlastního maxima,
  - přínos svislé elektrody.
- **Polohy** všech příznaků vůči známým hranám prstenců. Rozestup středů
  prstenců je 6 mm (13,0 a 7,0 mm), tedy 4,0 ml roztoku. Z něj se určí
  měřítko osy vzduchu.
- **Rezerva** mezi bodem zastavení a průchodem spodním prstencem.

Log poslat jako **soubor**.

---

## Příkazy

| příkaz | význam |
|---|---|
| `m` | průběžný výpis C1;C2;C3 (dotykový test), libovolný znak = konec |
| `n<s><v><h>` | mapa kanálů, např. `n123`, `n213` |
| `D` | měření suchých elektrod (bez lahvičky / prázdná lahvička) |
| `I` | test I2C: sken adres + 200× čtení ID FDC1004 (sběrnici sdílí i jiné periferie) |
| `f` | kontrola FDC1004 po vstupech (saturace / I2C / zamrznutí) |
| `t` | vzduchová stříkačka plná |
| `g` | start 5 cyklů |
| `v<ml>` / `c<n>` | objem vzduchu na cyklus / počet cyklů |
| `x` | zastavení |

Překlad (`avr-g++`): flash 24 650 B (75,2 %), SRAM 690 B (33,7 %), bez varování.
