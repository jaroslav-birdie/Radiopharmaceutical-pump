# Test 7 – zastavení na vrcholu rozdílu C1 − C3

Sketch `level_stop.ino`, výchozí režim **zastavení** (`v0`). Geometrie
studny zůstává 3 / 3 / 3 mm (viz `TEST6.md`).

## Otázka

> **Kde přesně stojí hladina, když systém zastaví na vrcholu C1 − C3,
> a jak je to opakovatelné při neznámé náplni?**

Obsluha naplní lahvičku neznámým objemem, systém tlačí až do detekce
vrcholu a zastaví. Obsluha pak **změří výšku hladiny od vnějšího dna**.
Podle výšky se z tabulky `h = 2,5 + 1,5·V` určí zbytkový objem a pak se
rozhodne, jak dál.

---

## Detektor (bez učení, každý běh zvlášť)

Vzduch je **jen hnací médium**. Jeho spotřeba s hladinou nijak nesouvisí
a detekce ji **nepoužívá**. Sleduje se jen, jestli ve stříkačce ještě
vzduch je; když dojde, stříkačka se doplní. Sloupec `vytlaceno_ml` v logu
je jen diagnostika polohy motoru.

Detektor běží **na čase**, vždy když je pacientská cesta otevřená
a ustálená: fáze `p` (otevřeno, motor stojí) a `w` (tlačení). Hladina se
může hýbat i se stojícím motorem (zbytkový přetlak), proto se detekce
nevypíná se stříkačkou.

- koš = **12 vzorků** (0,6 s při 20 vzorcích/s), průměr,
- klouzavé okno = **8 košů** (4,8 s): `C1ₒ`, `C3ₒ`, `d = C1ₒ − C3ₒ`,
- `d₀` = první plné okno běhu.

| krok | podmínka |
|---|---|
| **aktivace** | `C3ₒ ≤ 0,85 × max C3ₒ` v tomto běhu (horní prstenec se začal odkrývat) |
| **vrchol** | od aktivace se sleduje `dmax`; zastaví se, když `d < dmax − 0,10·(dmax − d₀)` |
| **skok C1** (poslední záchrana) | koš C1 o víc než **15 %** níž než větší ze dvou předchozích košů → zastavení, konec série |

Vlastnosti:
- **Pacient zavřený** (doplnění vzduchu, přejezdy ventilů): hladina stojí,
  vzorky se ignorují. Okno i maxima zůstávají, zahodí se jen rozpracovaný
  koš a porovnání pro skok (přepnutí ventilu dělá skoky).
- **Na zesílení ani posunu nezávisí:** poloha maxima rozdílu se nezmění,
  když se oba prstence vynásobí nebo posunou stejně (vliv samolepky,
  vodivosti, okolí).
- Žádný práh v pF ani v ml.

Při detekci se pacient zavírá **okamžitě, uvnitř vzorkování**. Motor
stojí, tlačení skončí. Teprve po 15 s klidu pod tlakem se lahvička
odvzdušní.

### Kontrola před tlačením

V klidu `k` (lahvička uzavřená) musí být **C3/C1 ≥ 0,8**, tedy horní
prstenec zakrytý. V testu 6 bylo C3/C1 = 1,03–1,06 nad prstencem
a ≈ 0,55 v mezeře. Když podmínka neplatí, systém **netlačí**
(`# *** hladina neni nad hornim prstencem`) a vyzve obsluhu, ať doplní
kapalinu.

### Přehrání na datech testu 6

Na PC přeložený stejný algoritmus, puštěný na pět profilů testu 6
(časy od začátku cyklu, včetně doplňování vzduchu):

| cyklus | aktivace [s] | **zastavení** [s] | C1 / C3 při zastavení [pF] | skok C1 [s] | zastavení před skokem [s] |
|---|---|---|---|---|---|
| 1 | 120,7 | **143,5** | 4,69 / 2,69 | 153,7 | 10,2 |
| 2 | 108,2 | **130,4** | 4,74 / 2,71 | 141,8 | 11,4 |
| 3 | 110,6 | **137,6** | 4,69 / 2,68 | 146,0 | 8,4 |
| 4 | 109,4 | **132,8** | 4,77 / 2,71 | 144,2 | 11,4 |
| 5 | 125,0 | **148,4** | 4,64 / 2,73 | 161,0 | 12,6 |

- Zastavení přichází vždy **před skokem**, pojistka skoku zachytila skok
  ve všech cyklech (pokles 17–29 %) a nikde nezasáhla falešně (jinde
  nejvýš 11 %).
- **Signál při zastavení je velmi stálý:** C1 4,64–4,77 pF, C3
  2,68–2,73 pF při náplních, které se lišily. Hladina by tedy měla stát
  pokaždé na stejném místě.
- Časy závisí na rychlosti motoru (`s5`), polohu hladiny neurčují.

### Očekávání (stanovené PŘEDEM)

Kde přesně hladina stojí, z dosavadních dat **určit nejde** (poloha šla
jen přes osu vzduchu, která s hladinou nesouvisí). Proto se měří.
Z geometrie: vrchol rozdílu nastává, když je horní prstenec odkrytý
a spodní ještě zakrytý, tedy hladina **v mezeře nebo na horní hraně
spodního prstence**, 8,5–11,5 mm.

| výsledek | význam |
|---|---|
| výška v 8,5–11,5 mm ve všech cyklech, rozptyl ≤ ±0,5 mm | detektor použitelný |
| výška opakovatelná, ale v horním prstenci (> 11,5 mm) | stop příliš brzy – posunout práh poklesu |
| výška 5,5–8,5 mm (ve spodním prstenci) | stop pozdě, ale jehla sahá na dno – posoudit rezervu |
| velký rozptyl (> ±1 mm) | detektor závisí na náplni – rozhodnout jinak |
| výška < 5,5 mm | hladina prošla spodním prstencem – nepoužitelné |

---

## Postup

Postup vypíše sketch po startu a na `h`.

1. Nahrát `level_stop.ino`, zachytávat log (**115200 Bd**):
   ```
   tools/serial_log.py /dev/ttyUSB0 test7.csv --baud 115200
   ```
   Zkontrolovat `# FDC1004 meri OK`.
2. Vzduchová stříkačka **plná**, ventily a stříkačka osazené, pacientská
   hadička naplněná, studna prázdná.
3. **Dotykový test** `m` (spodní → C1, svislá → C2, horní → C3), jinak
   `n<s><v><h>`.
4. `t`, `i`: musí ukázat `REZIM: zastaveni na vrcholu C1-C3`, `cykly=5`.
5. `g` – 5 cyklů. V každém:
   1. Výzva: naplnit lahvičku **neznámým objemem 9–14 ml**, pokaždé jinak
      (hladina nad horním prstencem, > 14,5 mm), vložit, potvrdit. Objem
      nezapisovat.
   2. Klid `k`, kontrola C3/C1, předtlak, otevření pacienta, tlačení
      (podle potřeby doplnění vzduchu, max. 3×) až do `# STOP vrchol`.
   3. Klid pod tlakem 15 s, odvzdušnění, 30 s klidu, doplnění stříkačky.
   4. Výzva: **vyndat lahvičku, změřit výšku hladiny od vnějšího dna**
      (dosedací plochy) v mm, napsat číslo (např. `8.5`) a odeslat. Jde do
      logu jako `# obsluha: 8.5`.
6. Log poslat jako **soubor**.

Měřit vždy stejně: lahvička svisle na rovné podložce, oči v úrovni
hladiny, číst **spodní okraj menisku**.

`x` kdykoli = zastavení.

## V logu

| řádek | význam |
|---|---|
| `# start C3/C1=…` | kontrola před tlačením |
| `# aktivace @ t=… ms  C3=… max C3=…` | horní prstenec se začal odkrývat |
| `# STOP vrchol @ t=… ms  C1=… C3=… d=… d0=… dmax=… @ t=…` | zastavení (okno 4,8 s), druhý čas = maximum rozdílu |
| `# *** SKOK C1 @ …` | poslední záchrana, série končí |
| `# obsluha: 8.5` | změřená výška hladiny [mm] |

## Příkazy

| příkaz | význam |
|---|---|
| `m` / `n<s><v><h>` | monitor / mapa kanálů |
| `f` | kontrola FDC1004 |
| `t` / `g` / `x` | stříkačka plná / start / zastavení |
| `c<n>` | počet cyklů (výchozí 5) |
| `v<ml>` / `v0` | profil (detekce jen zapisuje `# STIN …`) / zastavení (výchozí) |
| `s<s>` | sekund na 1 ml (výchozí 5) |
| `r<ml>` / `r` | předtlak ručně / výpočtem |
| `D` | suché elektrody |

Příkaz `d` (práh δ) z testu 5 je zrušený, učení F3 taky.

Překlad (`avr-g++`): flash 25 208 B (76,9 %), SRAM 656 B (32,0 %), bez varování.
