# Automatizovaný sweep test kapacitního senzoru (FDC1004)

Nahrazuje ruční plnění/vyprazdňování z `tools/capacitive_test/` (Fáze C v jeho
`README.md`) automatizovaným průběhem. Sketch ovládá fyziologický krokový
motor (D6/D7, DRV8825 na sdíleném `nENBL` A3), čeká na skutečné ustálení
signálu a zaznamenává >= 50 rychlých vzorků na každou polohu.

> **Cíl testu NENÍ přesný objem.** Píst je gumový a má vůli, takže dávkovaný
> objem přesně neodpovídá `level_ml` na displeji — to je jen orientační
> relativní poloha odvozená z počtu kroků. Jde o **spolehlivou detekci dvou
> hran kruhové elektrody**: spodní/kritickou hladinu (pod elektrodou) a
> případně horní hladinu (těsně nad elektrodou), pokud je dost strmá na
> spolehlivé odlišení.

> **Předpoklad:** zpětná klapka na fyziologické větvi byla pro tento test
> odstraněna. Bez ní by šlo tekutinu jen dávkovat, ne odsávat zpět.

Sketch je **samostatný diagnostický nástroj**, nemá nic společného s
firmware čerpadla. Po dokončení testů se z Arduina opět nahraje ostrý
firmware.

---

## Proč sweep dolů (odsávání), ne nahoru

Reálná kalibrace probíhá s **plnou** lahvičkou a systém pak kapalinu
**odčerpává** — směr testu tomu teď odpovídá. `g` proto vždy jede
z aktuální polohy **dolů** (odsávání), ne nahoru. `u` je volitelný návrat
nahoru (jen pro kontrolu hystereze, není to hlavní směr).

## Proč hrubý sweep, pak zaostřený

Nás zajímá přesná poloha a strmost dvou hran, ne celá křivka po 0,5 ml
od 0 do 20 ml. Postup je dvoufázový:

1. **Hrubý sweep** celým rozsahem (výchozí krok 0,5 ml) → najít přibližně,
   kde hrana leží.
2. **Zaostřený sweep** jen kolem té hrany, s jemným krokem (např. 0,1 ml)
   a omezeným rozsahem (`z<ml>`) — mnohem rychlejší než honit jemný krok
   přes celých 20 ml a dá přesnější polohu i strmost hrany.

---

## Postup — hrubý sweep

1. **Nahrát sketch** do Arduina (Arduino IDE, Upload).
2. **Fyzicky připravit:**
   - fyziologická stříkačka naplněná alespoň ~30 ml roztoku,
   - lahvička **naplněná na plno** (odpovídá reálné kalibraci),
   - FDC1004 připojen na A4/A5 jako dosud.
3. **Serial Monitor**, 9600 Bd, zakončení Enter. Po startu sketch vypíše
   stav (CAPDAC, aktuální parametry) a nápovědu.
4. **Zapnout driver:** `o`
5. **Tare:** `t` — aktuální poloha (plná lahvička) = `maxVol` (výchozí
   20 ml, uprav `m<ml>` PŘED tare podle varianty 10/20 ml).
6. **Spustit hrubý sweep:** `g`
   - jede z aktuální polohy dolů až po `sweepEndMl` (výchozí 0) po
     `sweepStepMl` (výchozí 0,5 ml),
   - na každé poloze počká na ustálení (klouzavé okno kapacity pod prahem
     `settleEps`, min. `settleMin`, max. `settleTimeout`), pak zapíše
     >= `samples` (výchozí 60) rychlých vzorků,
   - trvá to řádově desítky minut podle rychlosti ustálení (viz níže).
7. **Nouzové zastavení kdykoliv za běhu:** `x`.
8. Po dokončení (`# --- SWEEP DOLU HOTOVO ... ---`) zkopírovat výstup a
   z `# LEVEL_SUMMARY` řádků odhadnout, kolem jaké hladiny leží hrana.

## Postup — zaostřený sweep kolem nalezené hrany

1. Lahvičku znovu **naplnit na plno**.
2. `o`, pak `t` (nová tare, poloha = `maxVol`).
3. `J`/`K` — rychlé skoky po 1 ml, přiblížit se pár ml nad odhadovanou
   hranu.
4. `s0.1` — jemný krok 0,1 ml (nebo jiná hodnota podle potřeby).
5. `z<ml>` — nastavit konec sweepu pár ml pod hranu (aby se sweep zbytečně
   netáhl dál, než je potřeba).
6. `g` — jemný sweep jen v okolí hrany.
7. Volitelně `u` — vrátit zpět nahoru na `maxVol`, pro kontrolu hystereze
   na tomtéž úseku.

Pokud se to týká i horní hrany (nad elektrodou), stejný postup zopakovat
s `z`/pozicí posunutou o kus výš.

## Parametry (příkaz `i` vypíše aktuální hodnoty)

| Příkaz | Význam | Výchozí |
|---|---|---|
| `m<ml>` | maxVol — nastavit **před** `t` (tare) | `m20` |
| `s<ml>` | krok sweepu | `s0.5` |
| `z<ml>` | kde sweep dolů (`g`) skončí | `z0` |
| `e<pF>` | práh ustálení (p-p klouzavého okna) | `e0.05` |
| `w<ms>` | max. čekání na ustálení | `w15000` |
| `c<n>` | počet rychlých vzorků na hladinu (min. 50) | `c60` |
| `p<ms>` | perioda rychlých vzorků | `p20` |
| `j`/`k` | jog +/- `sweepStepMl` | — |
| `J`/`K` | jog +/- 1 ml (rychlé přiblížení) | — |
| `g` | sweep dolů: aktuální poloha → `sweepEndMl` | — |
| `u` | sweep nahoru: aktuální poloha → `maxVol` (hystereze) | — |

### Proč se výchozí `settleEps`/`settleTimeout` změnily

První běh s `e0.01`/`w6000` **nikdy skutečně nedetekoval ustálení** — všech
81 hladin skončilo přesně na hranici timeoutu (~6010 ms). Efektivně to byla
pevná 6s prodleva, ne adaptivní čekání, a to se ukázalo jako nedostatečné —
gumový píst má vůli a reakce systému je pomalejší, než jsme čekali. Nové
výchozí hodnoty (`e0.05`, `w15000`, plus nová `settleMin=3000` — minimální
čekání, i kdyby okno vypadalo stabilně dřív) lépe odpovídají tomu, jak dlouho
se ručně čekalo v prvním manuálním testu. Pokud pořád vidíš hodně
`timeout=1`, zkus `e` ještě zvýšit nebo `w` prodloužit.

## Formát výstupu

```
# t_ms;level_ml;dir;C1_pF;C2_pF;d1;d2
# --- SWEEP DOLU START ---
# LEVEL 20.00 ml dir=down settle_ms=3040 timeout=0
0;20.00;down;2.7281;3.4335;0.0000;0.0000
...
# LEVEL_SUMMARY 20.00 ml dir=down mean1=2.7278 pp1=0.0210 sigma1=0.00512 mean2=3.4340 pp2=0.0980 sigma2=0.02341
# LEVEL 19.50 ml dir=down settle_ms=4120 timeout=0
...
# --- SWEEP DOLU HOTOVO, trvani 612 s ---
```

- Řádky bez `#` = jednotlivé CSV vzorky (>= `samples` na hladinu).
- `# LEVEL ...` = začátek nové hladiny, obsahuje **dobu ustálení**
  (`settle_ms`) — přímo použitelné pro doladění `NO_FLOW_TIMEOUT_MS` a
  budoucí kalibrační doby v `config.h`.
- `# LEVEL_SUMMARY ...` = průměr/p-p/sigma za všech N vzorků té hladiny —
  hotová statistika bez nutnosti ruční redukce v tabulce.
- `dir=down` / `dir=up` rozlišuje odsávací (hlavní) a vratnou (hystereze)
  větev.

Vše je oddělovač `;`, desetinná tečka `.` — stejně jako u `capacitive_test.ino`.
