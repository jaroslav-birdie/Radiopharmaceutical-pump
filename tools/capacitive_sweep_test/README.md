# Automatizovaný sweep test kapacitního senzoru (FDC1004)

Nahrazuje ruční plnění/vyprazdňování z `tools/capacitive_test/` (Fáze C v jeho
`README.md`) plně automatizovaným průběhem. Obsluha už neplní stříkačkou po
kapkách a nepíše ruční značky objemu — sketch sám ovládá fyziologický krokový
motor (D6/D7, DRV8825 na sdíleném `nENBL` A3) a přesně dávkuje/odsává po
0,5 ml, čeká na ustálení signálu a zaznamenává >= 50 rychlých vzorků na
každou polohu hladiny.

> **Předpoklad:** zpětná klapka na fyziologické větvi byla pro tento test
> odstraněna. Bez ní by šlo tekutinu jen dávkovat, ne odsávat zpět — sketch
> počítá s tím, že motor teď zvládá oba směry.

Sketch je **samostatný diagnostický nástroj**, nemá nic společného s
firmware čerpadla (`state_machine.cpp` atd.). Po dokončení testů se z
Arduina opět nahraje ostrý firmware.

---

## Postup

1. **Nahrát sketch** do Arduina (Arduino IDE, Upload).
2. **Fyzicky připravit:**
   - fyziologická stříkačka naplněná alespoň ~30 ml roztoku (dost prostoru
     na plný rozsah sweepu oběma směry),
   - prázdná lahvička na měřicím místě,
   - FDC1004 připojen na A4/A5 jako dosud.
3. **Serial Monitor**, 9600 Bd, zakončení Enter. Po startu sketch sám
   vypíše stav (CAPDAC, aktuální parametry) a nápovědu.
4. **Zapnout driver:** `o`
5. **Odvzdušnit hadičku** (pokud je v ní vzduchová bublina po sestavení):
   `j` / `k` pro poposunutí o 0,5 ml tam a zpět, dokud neteče plynule.
6. **Tare** — teprve TEĎ, po odvzdušnění: `t` (aktuální poloha = 0 ml,
   zaznamená se i referenční kapacita).
7. **Spustit sweep:** `g`
   - sketch automaticky: 0 → `maxVol` (výchozí 20 ml) po 0,5 ml, pak stejnou
     cestou zpět na 0 ml,
   - na každé poloze počká na ustálení (klouzavé okno kapacity pod prahem
     `settleEps`, max. `settleTimeout`), pak zapíše >= `samples` (výchozí 60)
     rychlých vzorků,
   - trvá to cca 5–15 minut podle rychlosti ustálení.
8. **Nouzové zastavení kdykoliv za běhu:** napsat `x` (přeruší pohyb i
   měření, vypne driver).
9. Po dokončení (`# --- SWEEP HOTOVO ... ---`) **zkopírovat celý výstup**
   ze Serial Monitoru (od úvodních `#` řádků až po konec) a předat ke
   zpracování.

## Volitelné doladění parametrů před `g`

| Příkaz | Význam | Výchozí |
|---|---|---|
| `m<ml>` | maximální objem sweepu | `m20` |
| `e<pF>` | práh ustálení (p-p klouzavého okna) | `e0.01` |
| `w<ms>` | max. čekání na ustálení, pak pokračuje i tak (timeout se zaznamená) | `w6000` |
| `c<n>` | počet rychlých vzorků na hladinu (min. 50) | `c60` |
| `p<ms>` | perioda rychlých vzorků | `p20` |

`i` kdykoliv vypíše aktuální hodnoty všech parametrů.

Hodnota `e` (settle epsilon) je odhad — pokud sweep na první pokus vypisuje
hodně `timeout=1`, buď se nikdy nestihne ustálit v `w`, nebo je práh moc
přísný; zvyš `e`, případně `w`, a spusť znovu.

## Formát výstupu

```
# t_ms;level_ml;dir;C1_pF;C2_pF;d1;d2
# --- SWEEP START ---
# LEVEL 0.00 ml dir=up settle_ms=840 timeout=0
0;0.00;up;2.9401;1.9312;0.0000;0.0000
...
# LEVEL_SUMMARY 0.00 ml dir=up mean1=2.9398 pp1=0.0041 sigma1=0.00112 mean2=1.9310 pp2=0.0038 sigma2=0.00098
# LEVEL 0.50 ml dir=up settle_ms=1120 timeout=0
...
# --- SWEEP OBRAT (dolu) ---
...
# --- SWEEP HOTOVO, trvani 612 s ---
```

- Řádky bez `#` = jednotlivé CSV vzorky (>= `samples` na hladinu).
- `# LEVEL ...` = začátek nové hladiny, obsahuje **dobu ustálení**
  (`settle_ms`) — přímo použitelné pro doladění `NO_FLOW_TIMEOUT_MS` a
  budoucí kalibrační doby v `config.h`.
- `# LEVEL_SUMMARY ...` = průměr/p-p/sigma za všech N vzorků té hladiny —
  hotová statistika bez nutnosti ruční redukce v tabulce.
- `dir=up` / `dir=down` rozlišuje plnící a vyprazdňovací větev (hystereze).

Vše je oddělovač `;`, desetinná tečka `.` — stejně jako u `capacitive_test.ino`.
