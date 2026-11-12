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

Detektor neběží na čase, ale na **ose vytlačeného vzduchu**:

- vzorky se sbírají **jen během tlačení** (fáze `w`) do košů po
  **0,125 ml** vzduchu (průměr košíku),
- klouzavé okno je **8 košů = 1 ml**: `C1ₒ`, `C3ₒ`, `d = C1ₒ − C3ₒ`,
- `d₀` = první plné okno běhu (výchozí výška hrbu).

| krok | podmínka |
|---|---|
| **aktivace** | `C3ₒ ≤ 0,85 × max C3ₒ` v tomto běhu (horní prstenec se začal odkrývat) |
| **vrchol** | od aktivace se sleduje `dmax`; zastaví se, když `d < dmax − 0,10·(dmax − d₀)` |
| **skok C1** (poslední záchrana) | koš C1 o víc než **15 %** níž než předchozí koš → zastavení, konec série |

Vlastnosti:
- **Pauza, doplnění vzduchu, klid:** nic se nevytlačuje, takže se stav
  detektoru nemění. Okno i maxima zůstávají, zahodí se jen rozpracovaný
  koš a porovnání pro skok (přepnutí ventilu dělá skoky).
- **Na zesílení ani posunu nezávisí:** poloha maxima rozdílu se nezmění,
  když se oba prstence vynásobí nebo posunou stejně (vliv samolepky,
  vodivosti, okolí).
- Žádný práh v pF ani v ml roztoku.

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

Na PC přeložený stejný algoritmus, puštěný na pět profilů testu 6:

| cyklus | aktivace [ml] | vrchol dmax [ml] | **zastavení** [ml] | skok C1 15 % [ml] | zastavení před skokem [ml vzduchu] |
|---|---|---|---|---|---|
| 1 | 12,50 | 15,25 | **16,50** | 18,26 | 1,76 |
| 2 | 10,25 | 13,00 | **14,25** | – (max. pokles 12 %) | – |
| 3 | 10,63 | 14,00 | **15,38** | 16,88 | 1,50 |
| 4 | 10,38 | 12,75 | **14,63** | 16,63 | 2,00 |
| 5 | 13,25 | 16,26 | **17,26** | 19,63 | 2,37 |

- Zastavení přichází vždy **před skokem**, pojistka skoku nikde
  nezasáhla falešně.
- Výška hrbu `dmax − d₀` = 2,2–2,4 pF ve všech cyklech.

### Očekávání (stanovené PŘEDEM)

Z testu 6: zastavení ≈ 2,1 ± 0,2 ml vzduchu před skokem, zpoždění za
skutečným vrcholem ≈ 0,9 ml vzduchu. Převodem vzduch → roztok ≈ 1,5 : 1
(test 5) to dává hladinu **≈ 8,8 mm** (8,0–9,0 mm), tedy těsně nad horní
hranou spodního prstence (8,5 mm), **zbytek ≈ 4,2 ml** (3,3–4,3 ml).

| výsledek | význam |
|---|---|
| výška 8–9,5 mm ve všech cyklech, rozptyl ≤ ±0,5 mm | převod platí, detektor použitelný |
| výška opakovatelná, ale jinde | převod vzduch → roztok neplatí, posun se dá korigovat |
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
| `# aktivace @ x ml  C3=… max C3=…` | horní prstenec se začal odkrývat |
| `# STOP vrchol @ x ml  C1=… C3=… d=… d0=… dmax=… @ y` | zastavení (okno 1 ml), `y` = poloha maxima |
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

Překlad (`avr-g++`): flash 25 064 B (76,5 %), SRAM 654 B (31,9 %), bez varování.
