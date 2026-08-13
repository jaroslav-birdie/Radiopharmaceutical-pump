# Test 1 — odezva komory a elektrod po kompletním stínění (FDC1004)

Bench nástroj pro **první krok** revalidace kapacitního snímání po dokončení
celého stínění (olovo uzemněné na GND FDC1004 + Cu kolem celé studny, viz
`CLAUDE.md` a diskuze v tomhle repu). Na rozdíl od
`tools/capacitive_edge_detect_test/` tenhle sketch **žádnou hranu nehledá** —
jen opakovaně pohybuje strříkačkou o pevný objem tam a zpět a loguje syrová
C1/C2 data. Cílem je získat čistý přehled, jak se komora a elektrody chovají
TEĎ, než se z téhle znalosti navrhne nový práh/algoritmus pro test 2
(opakované hledání kritické hladiny).

> Vychází z `tools/capacitive_cycle_test/` (spojitý pohyb, opakované cykly).

> **Tohle NENÍ test odolnosti proti dotyku/rušení.** Studny se během běhu
> záměrně nesahá — to je součástí navazujícího testu hledání hladiny (test 2),
> který zahrne i záměrné doteky (viz diskuze v repu, proč je to potřeba).

Sketch je **samostatný diagnostický nástroj**, nemá nic společného s firmware
čerpadla. Po dokončení testů se z Arduina opět nahraje ostrý firmware.

## Proč jeden souvislý běh (a ne dvě oddělené fáze)

První verze tohoto testu dělala dvě **oddělené** fáze — Fázi A (odsátí 18 ml
z ručně, od oka naplněné „~20 ml" lahvičky) a Fázi B (odsátí 8 ml z ručně
naplněné „~10 ml" lahvičky), s ručním přeplněním lahvičky a novým `t` mezi
nimi.

Ukázalo se, že to je vadný návrh testu: **Fáze A hranu prstence vůbec
nenašla.** Napříč všemi deseti cykly C1 klesla nejvýš o 0,23 pF, zatímco
skutečná hrana je hluboká přes 1 pF — odsátí prostě skončilo dřív, než
hladina v lahvičce klesla ke kroužku. Dvě nezávisle, od oka odhadnuté
zálivky („~20 ml" a „~10 ml") do fyzicky stejné lahvičky nejsou vzájemně
porovnatelné v mililitrech, takže 18 ml zdvihu z jedné zálivky vůbec nemuselo
odpovídat 18 ml skutečně odebrané kapaliny.

Tahle verze proto dělá **jeden souvislý běh** s **jedním** ručním naplněním
a **jedním** `t` na začátku:

1. **Velká fáze** — `cycleCount` cyklů `{odsej bigMoveMl, doplň zpět
   bigMoveMl}` (výchozí 20 ml). Žádný ruční zásah mezi cykly.
2. **Přechod** — na **posledním** cyklu velké fáze se po odsátí `bigMoveMl`
   doplní zpět jen `smallMoveMl` (výchozí 10 ml) — poloha se tím trvale
   posune o `bigMoveMl - smallMoveMl` hlouběji.
3. **Malá fáze** — `cycleCount` cyklů `{odsej smallMoveMl, doplň zpět
   smallMoveMl}` (výchozí 10 ml) kolem nové, hlubší základny.

Celé se to spustí **jedním** `g` po **jednom** `t`.

> **Tohle neřeší, kolik ml je skutečně v lahvičce** — to se ukázalo jako
> nespolehlivý předpoklad (viz výše) a tenhle test se ho záměrně vzdává.
> `level_ml` je teď čistě **signovaná odchylka polohy stříkačky od tare**
> (0,00 při `t`, záporně = odsáto) — ne odhad obsahu lahvičky.

---

## Postup

1. **Nahrát sketch** do Arduina (Arduino IDE, Upload).
2. **Stříkačku** naplnit na rozumnou střední hodnotu (~30 ml, mechanická rezerva).
3. **Lahvičku naplnit RUČNĚ výrazně NAD kritickou hladinu** — očima ověř, že
   hladina je jasně nad prstencem, ne jen „odhadem 20 ml". Vlož do studny.
4. **Serial Monitor**, 9600 Bd, zakončení Enter.
5. `a` — ověřit CAPDAC (po změně stínění se mohl posunout, viz `CLAUDE.md`
   „rychlá kontrola správnosti zapojení" — po autokalibraci by měly vyjít
   nízké hodnoty). Volitelně `n` — statický test šumu (nehýbat sestavou).
6. `o` — driver ON, volitelně `j`/`k` — odvzdušnění.
7. `t` — tare (aktuální poloha = 0,00 ml, referenční).
8. `g` — **spustí celý test**: velká fáze (20 ml) → přechod → malá fáze
   (10 ml), bez dalšího zásahu.
9. **Sleduj živě první `down` úsek.** Pokud C1 zjevně neklesá (řádově desetiny
   pF), zálivka nejspíš zase nestačí na dosažení hrany — `x` (abort), dolij
   víc, znovu `t`, pak zas `g`. Tohle je jediná pojistka proti opakování
   chyby z první verze testu.
10. `x` kdykoliv za během = okamžité zastavení (nouzové), vypne driver.
11. Po `# === MALA FAZE HOTOVA - test 1 kompletni ===` zkopírovat celý
    výstup a předat ke zpracování.

Pokus o `g` bez nového `t` po předchozím (dokončeném i přerušeném) běhu
skončí chybovou hláškou — vždy nejdřív `t`.

## Volitelné parametry (nastavit před `t`/`g`)

| Příkaz | Význam | Výchozí |
|---|---|---|
| `wa<ml>` | odběr/doplnění za cyklus ve velké fázi | `wa20` |
| `wb<ml>` | odběr/doplnění za cyklus v malé fázi (i přechodový doplněk) | `wb10` |
| `r<n>` | počet cyklů na fázi | `r10` |
| `p<ms>` | perioda vzorku | `p200` |

`i` kdykoliv vypíše aktuální hodnoty a stav (testStarted, poloha). `n`
(noise test) a `a` (autoCAPDAC) stejné jako v ostatních nástrojích.

> **Pozor na periodu vzorku.** FDC1004 v REPEAT módu běží na 100 S/s (~10 ms
> na kanál) — `p` pod ~20 ms nemá smysl, sketch to ani nepovolí. Výchozí
> `200` je stejná perioda jako v `capacitive_edge_detect_test`, aby byla data
> mezi nástroji přímo srovnatelná.

> **Pokud ani `wa20` hranu nenajde**, zvyš `wa` (např. `wa25` — mechanický
> strop `MECH_LIMIT_ML` je 25 ml) a hlavně dolij lahvičku výrazně výš. Živé
> sledování prvního `down` úseku (bod 9 výše) tohle odhalí okamžitě, není
> nutné čekat na konec celého běhu.

### Odhad velikosti výstupu

Při výchozích parametrech (20/10 ml, 10 cyklů na fázi, 200 ms):
velká fáze ~10 × (5 s + 100 s + 5 s + 100 s) ≈ 35 min, malá fáze ~10 × (5 s +
50 s + 5 s + 50 s) ≈ 18 min → dohromady odhadem ~16 000 řádků CSV. Pro
pohodlné logování zvaž terminál s přímým ukládáním do souboru místo
kopírování ze Serial Monitoru.

## Formát výstupu

```
# === VELKA FAZE 20.0 ml: 10 cyklu ===
# variant;cycle;t_ms;level_ml;dir;C1_pF;C2_pF;d1;d2
# --- cyklus 1/10
20;1;0;0.00;settle;8.4987;3.5678;0.0000;0.0000
20;1;200;0.00;settle;8.4971;3.5661;-0.0016;-0.0017
...
20;1;5000;0.00;down;8.4955;3.5670;-0.0032;-0.0008
20;1;5200;-0.03;down;8.4948;3.5652;-0.0039;-0.0026
...
20;1;105000;-20.00;down;7.3800;3.2100;-1.1187;-0.3578
20;1;105000;-20.00;settle;7.3795;3.2098;-1.1192;-0.3580
...
20;1;110000;0.00;up;8.4990;3.5675;0.0003;-0.0003
# --- cyklus 2/10
...
# === VELKA FAZE 20.0 ml HOTOVA ===
# === MALA FAZE 10.0 ml: 10 cyklu ===
# --- cyklus 1/10
10;1;...
...
# === MALA FAZE HOTOVA - test 1 kompletni ===
```

Na posledním cyklu velké fáze, mezi `down` a `up`, se navíc vypíše
komentářový řádek `# --- prechod: doplni se jen 10.0 ml ... ---` — dole v
datech je vidět, že ten konkrétní `up` úsek nedojde zpátky na `level_ml`
0,00, ale zastaví se výš (na `bigMoveMl - smallMoveMl` pod nulou), a odtud
rovnou pokračuje malá fáze.

- Sloupec `variant` — `20` nebo `10`, celočíselně zaokrouhlený `bigMoveMl`/
  `smallMoveMl` (jen štítek pro rozdělení dat, ne obsah lahvičky).
- Sloupec `cycle` — číslo cyklu (1..`cycleCount`) **v rámci dané fáze**
  (resetuje se pro malou fázi).
- Sloupec `dir` — `settle` (motor stojí, 5 s před každým směrem) / `down`
  (odsávání) / `up` (doplnění zpět — na posledním cyklu velké fáze jen
  částečné, viz výše).
- `level_ml` — **signovaná odchylka polohy stříkačky od `t`** (0,00 při
  tare, záporně = odsáto). Není to obsah lahvičky — viz „Proč jeden souvislý
  běh" výše.
- `d1`/`d2` — rozdíl proti jednorázové baseline z okamžiku `t` (jen orientační
  rychlý pohled, offline analýza počítá s `C1_pF`/`C2_pF` přímo).

## Co s daty dál (test 2 z plánu)

Tahle data slouží k charakterizaci — tvar odezvy, opakovatelnost mezi cykly,
šumová podlaha v klidu (`settle`) i za chodu motoru (`down`/`up`), rozdíl
mezi 20ml a 10ml amplitudou pohybu. Teprve na základě téhle analýzy se
navrhne sketch pro test 2 (opakované hledání kritické hladiny) — ten se
zatím nepřipravuje, viz diskuze v repu proč („s aktuálním nastavením není
hledání hladiny úplně spolehlivé").
