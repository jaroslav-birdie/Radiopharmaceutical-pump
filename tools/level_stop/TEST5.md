# Test 5 – zastavení hladiny mezi prstenci (naslepo)

Sketch `level_stop.ino`. Kanály jako v testech 3–4: C1 spodní prstenec,
C2 svislá, C3 horní prstenec. Ovladače (FDC1004, serva, motor) jsou
převzaté z `tools/air_push_profile/`.

## Otázka

> **Zastaví se hladina vždy mezi prstenci, ať se před cyklem přidá
> jakýkoli objem?**

Sketch o přidaném objemu **nic neví**. Obsluha přidává od oka a pokaždé
jinak. Detekce nesmí záviset na náplni ani na absolutních hodnotách
kapacity.

---

## Princip detekce

Pod spodní hranou `SHLD2` se zakrytý prstenec zvedne na vysokou hodnotu,
odkrytý spadne na podlahu (viz vyhodnocení 1. cyklu testu 4). Oba
prstence jsou stejného typu, takže vliv kapaliny (vodivost, zemnění,
okolí) je násobí stejně. Podíl ho vykrátí:

**δ = (C1 − C3) / (C1 − F3)**

| hladina | δ |
|---|---|
| nad `SHLD2` | ≈ 0 (jmenovatel malý, nevyhodnocuje se) |
| pod `SHLD2`, oba prstence zakryté | ≈ 0,1 |
| mezi prstenci | → 1 |

### Cyklus 1 – učení

Tlačí se, dokud C3 nepřestane klesat:
- pokles C3 od maxima v cyklu je větší než **30 ×** jeho šum v klidu `k`,
- strmost poklesu (přes poslední 1 ml) je menší než **0,3 ×** největší
  strmost téhož poklesu.

V tu chvíli se zastaví a uloží **F3** (C3 na podlaze, suchý prstenec)
a **D1** = C1 − F3.

### Cykly 2–10

Zastaví se, jakmile **δ ≥ 0,7**. δ se vyhodnocuje, až když C1 − F3 > 0,3 × D1.

### Pojistka C1 (ve všech cyklech)

Pokud C1 v cyklu vystoupalo a pak spadlo pod **polovinu** svého vzestupu,
hladina prochází C1. Pacient se okamžitě zavře a série končí.
- vzestup se v učení počítá proti C1 z `k`, později proti F3,
- vzestup musí být v učení větší než 30 × šum, později větší než D1 / 2.

### Zastavení

Pacientský ventil se zavírá **okamžitě, uvnitř vzorkování**: motor stojí,
protože se smyčka tlačení hned ukončí. Teprve po 15 s klidu pod tlakem se
lahvička odvzdušní.

Všechny prahy jsou poměry: 30 × šum, 0,3 × strmost, 0,7, 0,3 × D1, 0,5.
Žádné pF ani ml.

### Zpětné ověření na datech testů 3 a 4

Kód detektoru byl přeložený na PC a přehraný přes data:

| | T3 c1 | c2 | c3 | c4 | c5 | T4 c1 |
|---|---|---|---|---|---|---|
| učení: plató C3 [ml] | 10,1 | 12,2 | 12,4 | 11,9 | 12,4 | po 10 ml (konec dat) |
| přechod C1 (50 %) [ml] | 13,9 | 14,9 | 15,1 | 14,6 | 15,6 | – |
| δ ≥ 0,7 [ml] | 8,0 | 9,6 | 10,0 | 9,1 | 10,5 | 7,6 |

- Zastavení podle δ padá **5,0 ± 0,2 ml vzduchu nad přechodem C1**.
- Učení končí 2,5–3,8 ml nad přechodem C1.
- Pojistka nezasáhla falešně: nejnižší poměr C1 byl 0,63 proti prahu 0,5.

---

## Průběh

| # | krok | fáze |
|---|---|---|
| 1 | cyklus 1: obsluha naplní lahvičku (např. 10 ml); další cykly: **přidá roztok od oka, pokaždé jinak** | — |
| 2 | vzduch `S↔V` (lahvička uzavřená), pacient `ISOLATE` | `v` |
| 3 | klid 5 s + statistika (základ a šum) | `k` |
| 4 | předtlakování, pacient zavřen | `r` |
| 5 | otevření pacienta, klid 5 s – **detekce už běží** | `p` |
| 6 | tlačení až do detekce, podle potřeby doplnění vzduchu (max. 3×) | `w`, `a`, `e`, `v` |
| 7 | **detekce → pacient ihned `ISOLATE`** | `s` |
| 8 | klid pod tlakem 15 s | `d` |
| 9 | vzduch `V↔F`, klid **30 s** (měří se poloha zastavení) | `v`, `o` |
| 10 | doplnění stříkačky na 10 ml | `v`, `a` |
| 11 | obsluha pošle, kde je hladina (`nad C3` / `mezi` / `pod C1`) | — |

V logu jsou řádky `# klid k: ...`, `# STOP @ ... delta=...`,
`# NAUCENO F3=... D1=...` a případně `# *** POJISTKA C1`.

**Předtlak:** objem náplně se nezadává, takže se počítá s prázdnou
lahvičkou: 30 + 10 + 1 = 41 ml plynu → **2,7 ml**. Je to nejkonzervativnější
varianta. Přebytek po otevření vyteče k pacientovi, proto detekce běží
od otevření.

---

## Co zkusit (naslepo)

Aby test pokryl i krajní případy:
- **hodně** roztoku (hladina vysoko nad `SHLD2`),
- **málo** roztoku – hladina po přidání zůstane mezi prstenci; správně
  je okamžité zastavení hned po otevření pacienta,
- **velmi málo** – skoro nic,
- střední objemy, různě.

Do potvrzení stačí napsat poznámku (např. `hodne`), zapíše se do logu.
Skutečný objem není potřeba.

---

## Kritéria (stanovená PŘEDEM)

1. **Zastavení mezi prstenci v každém cyklu 2–10.** Obsluha potvrdí
   `mezi` a pojistka C1 nezasáhne.
2. **Opakovatelnost polohy:** klidová hodnota C3 ve fázi `o` po zastavení
   (šum v klidu ~0,03 pF). Malý rozptyl přes cykly 2–10 znamená stejnou
   hladinu.
3. **Nezávislost na přidaném objemu:** δ při zastavení a klidové C3 nesmí
   souviset s tím, kolik vzduchu bylo potřeba.
4. **Učení:** plató C3 nalezeno v cyklu 1 a C1 v tu chvíli ještě vysoko.

---

## Postup

1. Vzduchová stříkačka **plná** (10 ml), ventily a stříkačka osazené,
   pacientská hadička naplněná.
2. Penicilinka ve studni.
3. Zachytávání logu, **115200 Bd**:
   ```
   tools/serial_log.py /dev/ttyUSB0 test5.csv --baud 115200
   ```
4. `t`, `a`, zkontrolovat `i`: CAPDAC1–3 nízké, `cykly=10`, `delta stop=0.70`.
5. `g` – start, dál jen potvrzování.

`x` kdykoli = zastavení. Log poslat jako **soubor**.

### Parametry

| příkaz | význam |
|---|---|
| `c<n>` | počet cyklů (výchozí 10) |
| `d<x>` | práh δ (výchozí 0,7) |
| `s<s>` | sekund na 1 ml (výchozí 5) |
| `r<ml>` / `r` | předtlak ručně / výpočtem |

Překlad (`avr-g++`): flash 19 270 B (58,8 %), SRAM 659 B (32,2 %), bez varování.
