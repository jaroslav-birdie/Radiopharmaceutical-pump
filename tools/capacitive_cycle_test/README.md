# Opakovaný spojitý cyklus — potvrzení detekce hran (FDC1004)

Nový nástroj, odlišný účel od `tools/capacitive_sweep_test/`:

| | `capacitive_sweep_test` | `capacitive_cycle_test` (tento) |
|---|---|---|
| Pohyb | krok 0,5 ml → počkej na ustálení → měř | **spojitý**, konstantní tempo (5 s/ml, stejné jako provoz), měří se průběžně |
| Opakování | jeden běh | **automaticky ≥5 cyklů** (odsaj → doplň → znovu) |
| Cíl | najít přibližnou polohu hrany | **potvrdit spolehlivost detekce** — opakovatelnost, marže nad šumem |
| CIN2 (svislá) | rovnocenný kanál | jen kontrolní signál „hladina se hýbe, nic není ucpané" |

> **Bezpečnostní kontext:** vizuální kontrola obsahu lahvičky není možná (neprůhledná
> studna tvořená elektrodami) — detekce spodní/kritické hladiny je **kritický
> bezpečnostní prvek** a musí zůstat robustní i při rušení signálu, navzdory
> plánovanému stínění celé sestavy. Tenhle test zatím charakterizuje chování za
> klidu — test odolnosti vůči rušení (běžící motory, servo, OLED na sdíleném I2C)
> je další krok, až bude potvrzena základní detekovatelnost.

> **Předpoklad:** zpětná klapka na fyziologické větvi byla pro tento test
> odstraněna — motor zvládá dávkovat i odsávat.

Sketch je **samostatný diagnostický nástroj**, nemá nic společného s firmware
čerpadla. Po dokončení testů se z Arduina opět nahraje ostrý firmware.

---

## Postup

1. **Nahrát sketch** do Arduina (Arduino IDE, Upload).
2. **Lahvičku naplnit NA PLNO.**
3. **Serial Monitor**, 9600 Bd, zakončení Enter.
4. `o` — driver ON.
5. Volitelně `j`/`k` — odvzdušnění hadičky (0,5 ml tam/zpět).
6. `t` — tare (aktuální poloha = `cycleMl`, výchozí 20 ml, "plná").
7. `g` — **spustí celou automatickou sérii**: odsaje `cycleMl` spojitě
   (loguje průběžně), pak stejným tempem `cycleMl` doplní zpět, a to
   `cycleCount`-krát (výchozí 5×).
   - Při výchozích parametrech trvá jeden cyklus ~200 s (100 s odsávání +
     100 s doplnění), celkem ~17 minut na 5 cyklů.
8. `x` kdykoliv za běhu = okamžité zastavení (nouzové), vypne driver.
9. Po dokončení (`# --- VSECHNY CYKLY HOTOVO ... ---`) zkopírovat celý
   výstup a předat ke zpracování.

## Volitelné parametry (nastavit PŘED `t`)

| Příkaz | Význam | Výchozí |
|---|---|---|
| `m<ml>` | kolik se odsaje/doplní za jeden cyklus | `m20` |
| `r<n>` | počet cyklů (doporučeno ≥5) | `r5` |
| `p<ms>` | perioda vzorku během pohybu | `p50` |

`i` kdykoliv vypíše aktuální hodnoty. `n` (noise test) a `a` (autoCAPDAC) jsou
stejné jako v ostatních nástrojích, pro předletovou kontrolu.

### Velikost výstupu

Výchozí parametry (20 ml, 5 cyklů, 50 ms) dají zhruba **20 000 řádků** CSV.
Arduino Serial Monitor jde označit a zkopírovat celý, ale pro tak velký objem
zvaž terminál s přímým logováním do souboru (PuTTY, CoolTerm, `screen`) místo
kopírování z okna. Případně zvyš `p` (např. `p100`) pro poloviční objem dat.

## Na co si dát pozor mezi cykly

Skutečná poloha vrcholu CIN1 (horní hrana) se mezi jednotlivými cykly může
mírně lišit — vůle gumového pístu, drobné ztráty při zpětném doplnění. **Sketch
proto NEPŘEDPOKLÁDÁ, že `level_ml` znamená totéž v cyklu 1 a cyklu 5.**
Zarovnání jednotlivých průběhů (podle polohy vlastního vrcholu CIN1 každého
cyklu, ne podle nominální hodnoty `level_ml`) je úkol až pro zpracování dat po
sběru, sketch to neřeší.

## Formát výstupu

```
# cycle;t_ms;level_ml;dir;C1_pF;C2_pF;d1;d2
# --- CYKLUS 1 ODSAVANI START ---
1;0;20.00;down;3.6932;2.4030;0.0000;0.0000
1;51;19.99;down;3.6928;2.4025;-0.0004;-0.0005
...
# --- CYKLUS 1 ODSAVANI HOTOVO, DOPLNENI START ---
1;100234;0.00;up;3.0942;2.0022;-0.5990;-0.4008
...
# --- CYKLUS 1 DOPLNENI HOTOVO ---
# --- CYKLUS 2 ODSAVANI START ---
...
# --- VSECHNY CYKLY HOTOVO, trvani 1023 s ---
```

- Sloupec `cycle` — číslo cyklu (1..`cycleCount`), pro pozdější rozdělení dat.
- Sloupec `dir` — `down` (odsávání, hlavní směr, odpovídá reálnému provozu) /
  `up` (doplnění, jen návrat pro další cyklus).
- Žádné `LEVEL_SUMMARY` řádky jako u `capacitive_sweep_test` — tohle je
  spojitý proud vzorků, ne diskrétní hladiny s ustálením. Průměrování/hledání
  hran se dělá až při zpracování.

Vše je oddělovač `;`, desetinná tečka `.` — stejně jako u ostatních nástrojů.
