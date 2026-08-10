# Plán odladění kapacitního snímání hladiny (FDC1004)

Cíl: ověřit, že elektrody spolehlivě rozliší všechny stavy, které bude
stavový automat injektoru potřebovat — **dřív**, než se detekce zapojí do
firmwaru čerpadla. Používá se samostatný sketch `capacitive_test.ino`.

---

## Fáze 0 — kontrola zapojení (PŘED prvním měřením)

Aktuální (odsouhlasené) zapojení:

| Vývod FDC1004 | Připojeno |
|---|---|
| `CIN1` | kruhová elektroda (kritická hladina) |
| `CIN2` | svislá elektroda (pohyb hladiny) |
| `SHLD1` | opletení **všech** koaxiálních kabelů |
| `SHLD2` | plošné elektrody naproti sobě (aktivní shield) |
| `CIN3`, `CIN4` | nepoužito |
| `GND` | jen napájecí zem modulu |

`SHLD1`/`SHLD2` jsou trvale buzené výstupy — nic se pro ně nekonfiguruje.

**Kontrola:** spusť sketch, příkaz `i`. CAPDAC musí vyjít **nízký**
(jednotky). Pokud vyjde 25–31, sketch to sám ohlásí varováním — znamená to
navázanou parazitní kapacitu, tedy nejspíš stínění omylem na zemi.
Uzemněné stínění naváže kapacitu kabelu (~100 pF/m) přímo na vstup a CAPDAC
umí odečíst maximálně 31 × 3,125 = **96,9 pF**.

**Test účinnosti aktivního shieldu:** spusť `s` (stream) a **hýbej kabely**
/ dotkni se jejich vnějšku. Při funkčním stínění se hodnota téměř nezmění.
Výrazné výkyvy znamenají, že shield nefunguje (špatný spoj na `SHLD1`).

---

## Fáze A — komunikace a rozsah

1. Nahrát sketch, otevřít Serial monitor (9600 Bd, zakončení **Enterem**).
2. Ověřit hlavičku: `MANUFACTURER_ID=0x5449 DEVICE_ID=0x1004`.
3. Příkaz `a` — automatická volba CAPDAC.
4. Příkaz `i` — zapsat si CAPDAC a klidovou kapacitu obou kanálů.

**Zapisované parametry:** `CAPDAC_CIN1`, `CAPDAC_CIN2`, klidová kapacita [pF]
pro prázdnou i plnou lahvičku.

---

## Fáze B — šum a stabilita (určuje, jaké prahy jsou vůbec reálné)

1. Lahvička na místě, sestavou **nehýbat**, ruce pryč.
2. Příkaz `n` — 256 vzorků, vypíše průměr, min, max, p-p a σ.
3. Zopakovat pro rychlosti `r1` (100 S/s), `r2` (200), `r3` (400).
4. Zopakovat s **rukou u lahvičky** a s **běžícími krokovými motory**.

**Zapisované parametry:** σ a p-p šumu [pF] pro každou rychlost, vliv ruky,
vliv motorů.

> Práh detekce musí být **alespoň 5× p-p šumu**, jinak bude planě spouštět.
> Hystereze pak alespoň 2× p-p.

**Drift:** nechat běžet `s` s `p1000` po dobu ~30 min bez zásahu a sledovat,
jestli hodnota neujíždí (teplota). Pokud ano, kalibrace na začátku aplikace
(`CALIBRATION_MS`) to musí pokrýt.

---

## Fáze C — statická charakteristika (kapacita vs. hladina)

Nejdůležitější fáze. Postup:

1. `t` (tare) při prázdné lahvičce, pak `s` (stream).
2. Plnit **po 0,5 ml** od 0 do plného objemu, po každém kroku:
   - počkat ~5 s na ustálení,
   - napsat značku: `#hladina 3.5 ml`.
3. Po dosažení plného objemu totéž **směrem dolů** (kvůli hysterezi).
4. `s` (stop), zkopírovat CSV do tabulky, vynést graf C = f(objem).

**Zapisované parametry:**
- citlivost CIN2 [pF/ml] a [pF/mm] — musí být přibližně lineární
- citlivost CIN1 [pF/ml] v okolí horní a spodní hrany
- hystereze (rozdíl mezi plněním a vyprazdňováním) — smáčení stěn

---

## Fáze D — prahy kruhové elektrody CIN1

Z grafu z fáze C odečíst:

| Veličina | Význam |
|---|---|
| `C_full` | kapacita při plné lahvičce (referenční, měří se při kalibraci) |
| `C_top` | hodnota, kdy hladina dosáhne **horní hrany** CIN1 |
| `C_bottom` | hodnota, kdy hladina klesne pod **spodní hranu** CIN1 |
| strmost přechodu | [pF/ml] v okolí obou hran |

**Ověřit hypotézu o dvou hladinách:** je mezi `C_top` a `C_bottom`
dostatečně strmý a jednoznačný přechod, aby šly rozlišit **oba** okraje?
Kritérium: rozdíl `C_top − C_bottom` musí být alespoň **10× p-p šumu**
a průběh mezi nimi monotónní.

Prahy se pak zapíší jako **procentuální pokles vůči `C_full`** (tak to
očekává `CAP_CRITICAL_PERCENT` v `config.h`).

---

## Fáze E — dynamika, sledování pohybu hladiny (CIN2)

Simuluje reálný běh aplikace.

1. `p100` (rychlejší výpis), `s`.
2. Ručně stříkačkou vytlačovat kapalinu rychlostí odpovídající provozu
   (~1 ml / 5 s) a sledovat průběh CIN2.
3. Zastavit uprostřed pohybu a nechat stát ~10 s — **stagnace**.
4. Opakovat pro plnění (nárůst hladiny).

**Zapisované parametry:**
- rychlost změny CIN2 při provozním průtoku [pF/s]
- minimální detekovatelná změna za `NO_FLOW_TIMEOUT_MS` (3 s)
- ověřit, že při stagnaci změna klesne pod práh a při obnovení průtoku
  zase naroste

Z toho se odvodí `CAP_FLOW_EPS_PERMILLE` a případně upraví
`NO_FLOW_TIMEOUT_MS`.

---

## Fáze F — křížové vlivy

Ověřit, že měření nerozhodí:

- ruka obsluhy v blízkosti lahvičky
- běžící krokové motory (EMI z driverů)
- pohyb serva ventilu
- zapnutý OLED (sdílí I2C sběrnici)
- vyjmutí a vrácení lahvičky (opakovatelnost usazení)

Poslední bod je zásadní: pokud se kapacita po přesazení lahvičky liší víc
než o práh, je nutná kalibrace při **každé** aplikaci (což už firmware
dělá — `ST_CALIBRATING`).

---

## Souhrn parametrů, které z testu vzejdou

| Parametr | Kam se propíše |
|---|---|
| CAPDAC pro CIN1, CIN2 | `capacitive.cpp` (konfigurace kanálů) |
| práh kritické hladiny [% poklesu] | `CAP_CRITICAL_PERCENT` |
| práh horní hrany (pokud se potvrdí) | nová konstanta |
| hystereze [pF] | nová konstanta |
| práh pohybu hladiny | `CAP_FLOW_EPS_PERMILLE` |
| timeout stagnace | `NO_FLOW_TIMEOUT_MS` |
| vzorkovací frekvence | `CAP_SAMPLE_MS` + `FDC_CONF` |
| doba kalibrace | `CALIBRATION_MS` (dnes 10 s) |

---

## Poznámka k CSV

Oddělovač je `;`, desetinná tečka `.`. Česká lokalizace Excelu očekává
desetinnou čárku — při importu buď přepnout v průvodci, nebo nahradit
`.` za `,`. Řádky začínající `#` jsou komentáře/značky.
