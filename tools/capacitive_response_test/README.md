# Test 1 — odezva komory a elektrod po kompletním stínění (FDC1004)

Bench nástroj pro **první krok** revalidace kapacitního snímání po dokončení
celého stínění (olovo uzemněné na GND FDC1004 + Cu kolem celé studny, viz
`CLAUDE.md` a diskuze v tomhle repu). Na rozdíl od
`tools/capacitive_edge_detect_test/` tenhle sketch **žádnou hranu nehledá** —
jen opakovaně pohybuje strříkačkou o pevný objem tam a zpět a loguje syrová
C1/C2 data. Cílem je získat čistý přehled, jak se komora a elektrody chovají
TEĎ, než se z téhle znalosti navrhne nový práh/algoritmus pro test 2
(opakované hledání kritické hladiny).

> Vychází z `tools/capacitive_cycle_test/` (spojitý pohyb, opakované cykly),
> rozšířeno o dvě po sobě jdoucí varianty počátečního objemu a o logované
> ustálení před každým směrem pohybu.

> **Tohle NENÍ test odolnosti proti dotyku/rušení.** Studny se během běhu
> záměrně nesahá — to je součástí navazujícího testu hledání hladiny (test 2),
> který zahrne i záměrné doteky (viz diskuze v repu, proč je to potřeba).

Sketch je **samostatný diagnostický nástroj**, nemá nic společného s firmware
čerpadla. Po dokončení testů se z Arduina opět nahraje ostrý firmware.

---

## Postup

1. **Nahrát sketch** do Arduina (Arduino IDE, Upload).
2. **Stříkačku** naplnit na rozumnou střední hodnotu (~30 ml, mechanická rezerva).
3. **Lahvičku naplnit RUČNĚ na ~20 ml**, vložit do studny.
4. **Serial Monitor**, 9600 Bd, zakončení Enter.
5. `a` — ověřit CAPDAC (po změně stínění se mohl posunout, viz `CLAUDE.md`
   „rychlá kontrola správnosti zapojení" — po autokalibraci by měly vyjít
   nízké hodnoty). Volitelně `n` — statický test šumu (nehýbat sestavou).
6. `o` — driver ON, volitelně `j`/`k` — odvzdušnění.
7. `t` — tare (aktuální poloha = 20 ml, "plná" pro Fázi A).
8. `g` — **spustí Fázi A**: `cycleCount` (výchozí 10) cyklů, každý:
   ustálení 5 s (motor stojí) → odsaje 18 ml spojitě → ustálení 5 s → doplní
   18 ml zpět. Loguje se průběžně po celou dobu, včetně ustálení.
9. Po `# === FAZE 20 ml HOTOVA ===`: **vyprázdni/uprav lahvičku na ~10 ml**,
   vrať do studny, znovu `t` (tare, poloha=10 ml), pak `g` — **spustí Fázi B**
   (stejný postup, 8 ml odběr/doplnění).
   - Pokus o `g` bez nového `t` po Fázi A skončí chybovou hláškou.
10. `x` kdykoliv za běhu = okamžité zastavení (nouzové), vypne driver.
11. Po `# Obe faze dokonceny` zkopírovat celý výstup a předat ke zpracování.

## Volitelné parametry (nastavit před `t`/`g`)

| Příkaz | Význam | Výchozí |
|---|---|---|
| `wa<ml>` | odběr/doplnění za cyklus ve Fázi A | `wa18` |
| `wb<ml>` | odběr/doplnění za cyklus ve Fázi B | `wb8` |
| `r<n>` | počet cyklů na fázi | `r10` |
| `p<ms>` | perioda vzorku | `p200` |

`i` kdykoliv vypíše aktuální hodnoty a stav (která fáze je další, čeká-li se
na re-tare). `n` (noise test) a `a` (autoCAPDAC) stejné jako v ostatních
nástrojích.

> **Pozor na periodu vzorku.** FDC1004 v REPEAT módu běží na 100 S/s (~10 ms
> na kanál) — `p` pod ~20 ms nemá smysl, sketch to ani nepovolí. Výchozí
> `200` je stejná perioda jako v `capacitive_edge_detect_test`, aby byla data
> mezi nástroji přímo srovnatelná.

### Odhad velikosti výstupu

Při výchozích parametrech (18/8 ml, 10 cyklů na fázi, 200 ms):
Fáze A ~10 × (5 s + 90 s + 5 s + 90 s) ≈ 32 min, Fáze B ~10 × (5 s + 40 s + 5 s
+ 40 s) ≈ 15 min → dohromady odhadem ~14 000 řádků CSV. Pro pohodlné logování
zvaž terminál s přímým ukládáním do souboru místo kopírování ze Serial
Monitoru.

## Formát výstupu

```
# === FAZE 20 ml: 10 cyklu, odber/doplneni 18.00 ml ===
# variant;cycle;t_ms;level_ml;dir;C1_pF;C2_pF;d1;d2
# --- cyklus 1/10
1;1;0;20.00;settle;3.5745;2.3723;0.0000;0.0000
1;1;200;20.00;settle;3.5729;2.3741;-0.0016;0.0018
...
1;1;5000;20.00;down;3.5677;2.3717;-0.0068;-0.0006
1;1;5200;19.97;down;3.5683;2.3710;-0.0062;-0.0013
...
1;1;95300;2.00;down;3.4200;2.1500;-0.1545;-0.2223
1;1;95300;2.00;settle;3.4198;2.1502;-0.1547;-0.2221
...
1;1;100300;2.00;up;3.5670;2.3690;-0.0075;-0.0033
...
1;1;190600;20.00;up;3.5751;2.3729;0.0006;0.0006
# --- cyklus 2/10
...
# === FAZE 20 ml HOTOVA ===
# Vyprazdni/uprav lahvicku na ~10 ml, vrat do studny,
# znovu 't' (tare), pak 'g' pro Fazi B.
```

- Sloupec `variant` — `20` nebo `10`, počáteční objem dané fáze.
- Sloupec `cycle` — číslo cyklu (1..`cycleCount`) **v rámci dané fáze**
  (resetuje se pro Fázi B).
- Sloupec `dir` — `settle` (motor stojí, 5 s před každým směrem) / `down`
  (odsávání) / `up` (doplnění zpět).
- `level_ml` — orientační poloha stříkačky od posledního `t`, s nabídkou
  počátku dané fáze (20 nebo 10) — **není** to skutečný obsah lahvičky (ten
  po prvním cyklu systém nezná přesně, stejně jako v ostatních nástrojích).
- `d1`/`d2` — rozdíl proti jednorázové baseline z okamžiku `t` (jen orientační
  rychlý pohled, offline analýza počítá s `C1_pF`/`C2_pF` přímo).

## Co s daty dál (test 2 z plánu)

Tahle data slouží k charakterizaci — tvar odezvy, opakovatelnost mezi cykly,
šumová podlaha v klidu (`settle`) i za chodu motoru (`down`/`up`), rozdíl
mezi 18 ml a 8 ml variantou. Teprve na základě téhle analýzy se navrhne
sketch pro test 2 (opakované hledání kritické hladiny) — ten se zatím
nepřipravuje, viz diskuze v repu proč („s aktuálním nastavením není hledání
hladiny úplně spolehlivé").
