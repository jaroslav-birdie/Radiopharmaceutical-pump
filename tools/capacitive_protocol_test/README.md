# Simulace skutečného aplikačního protokolu (FDC1004)

Předchozí nástroje (`capacitive_cycle_test`, `capacitive_edge_detect_test`)
pracovaly s **plnými zdvihy**: lahvička se vyprázdnila o 20 ml a pak zase
doplnila na plno. Skutečný přístroj ale dělá něco jiného — jedno vytlačení
na kritickou hladinu a pak stále dokola „doplnit 3 ml → vytlačit zpátky".
Rozdíl není kosmetický:

- V plném zdvihu projede hladina **celou** křivku C1 včetně hluboké patky.
- V reálné iteraci se hladina pohybuje jen v **~3 ml pásmu kolem vrcholu**
  a do patky se nikdy nedostane.

A právě v patce sedí naměřený **creep** (růst signálu při úplném odčerpání,
−41,5 fF/cyklus, r = −0,91). Analýza v `CLAUDE.md` („Chování detekce
v iterativním protokolu") proto musela plné zdvihy na iterace
**přemapovat** — v oblasti vrcholu je to obhájitelné, ale je to
předpoklad, ne měření. Tenhle nástroj protokol imituje přímo, takže se
už nic přemapovávat nemusí.

```
krok 0 (fáze 1) : lahvička 10 ml  ->  vytlačit na kritickou hladinu
krok 1..12      : doplnit 3,0 ml  ->  vytlačit na kritickou hladinu
```

Celý běh je **zcela automatický** — včetně počátečního naplnění lahvičky.
Obsluha vloží **prázdnou** lahvičku do studny a spustí `g`; dál už nesahá
na nic. To je záměr, ne pohodlí: každý dotyk studny mezi iteracemi je
přesně ten common-mode zásah, kvůli kterému vznikla ochrana CIN2, a
zároveň by zamaskoval creep, který hledáme.

---

## Co nástroj měří

Klíčová veličina je **hloubka sepnutí pod vrcholem**:

```
hloubka = vrchol_vial_ml − sepnutí_vial_ml
```

tedy o kolik mililitrů pod vrcholem křivky C1 práh sepnul. Musí vyjít
**menší než doplňovaná dávka** (3,0 ml), jinak další vytlačování začne
na sestupné větvi místo nad vrcholem a algoritmus ztratí referenci.

Z přemapovaných dat vyšlo `2,53 ml` (rozsah 1,93–3,28; 1 z 10 cyklů mimo
okno). Tenhle běh to buď potvrdí, nebo ne. Sketch hlásí překročení
**přímo za běhu**, takže se na výsledek nemusí čekat do konce:

```
# !!! HLOUBKA SEPNUTI 3,12 ml >= davka 3,00 ml - dalsi vytlacovani zacne POD vrcholem !!!
```

Druhá sledovaná věc je sloupec `novy_vrchol`: `1` = sledované maximum
během vytlačování přešlo nad startovní hodnotu (vrchol byl v okně
znovu zachycen), `0` = pohybovali jsme se jen po sestupné větvi.

---

## Co přebírá beze změny

Detekční jádro je **totožné** s `capacitive_edge_detect_test` (v9), aby
byly výsledky srovnatelné:

- klouzavý průměr 25 vzorků (~5 s při 200 ms/vzorek)
- pokles o `deltaCritical` (0,22 pF) od **neomezeného maxima** od začátku
  vytlačování
- potvrzení přes 5 po sobě jdoucích vzorků
- **reset sledovaného maxima na začátku každého vytlačování** — to je
  nutná podmínka toho, aby creep patky detekci neovlivnil (viz
  bezpečnostní pravidla v `CLAUDE.md`)
- samo-kalibrující se ochrana proti rušení přes CIN2 včetně „živého"
  kalibračního okna s běžícím motorem (v9) a kontroly znečištěné
  kalibrace proti zdravé základně (v8)

---

## V čem se liší (a proč)

| | `capacitive_edge_detect_test` | tenhle nástroj |
|---|---|---|
| Doplňovaný objem | náhodných 6–17 ml | **pevných 3,0 ml** |
| Potvrzení mezi cykly | obsluha `1`/`0` | **žádné, běh je nepřetržitý** |
| Po rušení CIN2 | čeká na `y` | **pokračuje sama, až je klid** |
| Počáteční naplnění | ručně obsluhou | **sketch sám z prázdné lahvičky** |
| Osa v logu | poloha stříkačky od `tare` | **+ odhad obsahu lahvičky v ml** |

**Náhodné doplňování** mělo smysl, dokud se testovala robustnost detekce
vůči neznámému počátečnímu stavu. Teď se měří přesně to, co protokol dělá,
takže je dávka pevná.

**Automatické pokračování po rušení** nahrazuje ruční `y`. Motor se
pozastaví a běh sám pokračuje, jakmile je rychlost změny C2 pod prahem
15 vzorků po sobě. Pokud rušení neustane do 3 minut nebo se v jednom
vytlačování nasčítá víc než 20 pauz, běh se ukončí a vypíše souhrn
(nemá smysl pokračovat s podezřelými daty).

Co se při pauze zachová a co zahodí, je nejchoulostivější místo celého
sketche:

- **Sledovaný vrchol C1 a čítač potvrzení zůstávají.** To je pravidlo
  `ST_PAUSED` z `CLAUDE.md` — reset by sledování vrcholu spustil znovu
  od už poklesnuté hodnoty, takže by se kritická hladina odhalila
  **později**, ne dřív. To je nebezpečný směr chyby.
- **Vyhlazovací okno se naopak zahodí a naplní znovu** (proto pauza trvá
  minimálně ~8 s). Pravidlo výše mluví o **vrcholu**; okno je něco jiného
  a drží 25 vzorků zpětně. Kdyby se v něm pokračovalo, byly by v něm po
  obnovení běžného pořadí vzorky naměřené **během rušení**, takže první
  `sm` po rozjezdu by z nich bylo poskládané. Když rušení C1 zvedne,
  nafoukne to sledovaný vrchol — a to je přesně mechanismus obou selhání
  zdokumentovaných u ochrany CIN2. Když ho sníží, nafoukne to zase
  okamžitý pokles. Hladina se během pauzy nehýbe, takže čerstvé okno měří
  tutéž hladinu a zahozením se nic neztrácí.

**Odhad obsahu lahvičky** (`vial_ml`) dává smysl teprve teď, kdy je
počáteční stav známý, protože si ho nastavil sketch sám. Je to ale
**dopočet z kroků motoru, ne měření** — předpokládá prázdnou lahvičku
před startem a nulové ztráty.

---

## Postup

1. Naplnit stříkačku na rozumnou střední hodnotu (~30 ml) — viz
   `MECH_LIMIT_ML` níže.
2. Do studny vložit **prázdnou** lahvičku (10ml varianta).
3. `o` — driver ON, volitelně `j`/`k` — odvzdušnění hadičky.
4. `t` — tare (referenční bod krokového počítadla).
5. `g` — start. Běh trvá zhruba 40–60 minut.
6. `x` kdykoliv = okamžité zastavení.

Pokud je lahvička naplněná ručně, vypnout automatické plnění (`f0`)
a nastavit skutečný objem (`v10`).

### Příkazy

| Příkaz | Význam |
|---|---|
| `h` / `i` | nápověda / info |
| `a` | autokalibrace CAPDAC |
| `n` | statický test šumu (256 vzorků na kanál) |
| `o` / `x` | driver ON / OFF (za běhu `x` = okamžitý STOP) |
| `t` | tare |
| `j` / `k` | jog ±0,5 ml |
| `g` | spustit celý protokol |
| `r<n>` | počet iterací po fázi 1 (1–12, výchozí 12) |
| `s<ml>` | dávka roztoku na iteraci (výchozí 3,0) |
| `v<ml>` | počáteční objem v lahvičce (výchozí 10,0) |
| `f1` / `f0` | automatické počáteční naplnění ano / ne |
| `dc<pF>` | `deltaCritical` (výchozí 0,22) |
| `cf<n>` | potvrzovacích vzorků (výchozí 5) |
| `p<ms>` | perioda vzorků (výchozí 200) |
| `cm<x>` | násobitel prahu CIN2 (0 = vypnout) |
| `cg<pF>` | minimální podlaha prahu CIN2 |
| `ck<n>` | potvrzovacích vzorků ochrany CIN2 |
| `#<text>` | značka do logu |

---

## Formát logu

Řádky začínající `#` jsou komentáře a hlášky. Data mají dva druhy řádků.

### Vzorky (jeden řádek na vzorek)

```
push;t_ms;vial_ml;level_ml;faze;C1_raw;C1_sm;peak_ref;C2_raw;c2rate
```

| Sloupec | Význam |
|---|---|
| `push` | 0 = fáze 1, 1..12 = iterace |
| `t_ms` | ms od stisku `g` |
| `vial_ml` | odhad obsahu lahvičky (dopočet z kroků) |
| `level_ml` | poloha stříkačky od `tare` (pro mechanickou ochranu) |
| `faze` | `F` počáteční plnění · `s` ustálení · `w` vytlačování · `p` pauza kvůli rušení · `r` doplnění roztoku |
| `C1_raw` | CIN1, syrová hodnota [pF] |
| `C1_sm` | CIN1 po klouzavém průměru 25 vzorků [pF] |
| `peak_ref` | sledované maximum C1 v tomto vytlačování [pF] |
| `C2_raw` | CIN2, syrová hodnota [pF] |
| `c2rate` | rychlost změny vyhlazeného C2 přes 3 vzorky [pF] |

### Výsledek vytlačování (řádky `#>`)

Vypíše se hned po každém sepnutí a znovu celý blok v závěrečném souhrnu:

```
#> push;start_vial_ml;vrchol_vial_ml;vrchol_C1;sepnuti_vial_ml;sepnuti_C1;pokles_pF;hloubka_ml;drah_ml;novy_vrchol;pauzy
```

`hloubka_ml` je ta veličina, kvůli které tenhle test vznikl.
`drah_ml` = kolik se v tomhle vytlačování skutečně odčerpalo
(v ustáleném stavu by mělo být blízko 3,0 ml — pokud je systematicky
větší, hladina se s iteracemi propadá).

Souhrn na konci navíc uvádí průměr / min / max hloubky, kolik iterací
padlo mimo okno a kolik jich proběhlo bez zachycení nového vrcholu.
**Fáze 1 se do těchhle statistik nezapočítává** — startuje z plné
lahvičky, takže její hloubka není srovnatelná s iteracemi.

---

## Bezpečnostní meze

`MECH_LIMIT_ML = 25` je **výhradně hardwarová ochrana zdvihu stříkačky**,
ne detekční ani alarmový koncept (stejně jako v `capacitive_edge_detect_test`
v5 — skutečná aplikace žádný objemový failsafe nemá, protože objem vytlačený
vzduchem proti neznámému odporu je právě ta neznámá veličina, kterou
kapacitní snímání nahrazuje).

Bilance běhu: s automatickým plněním se nejdřív dávkuje +10 ml (naplnění
lahvičky), fáze 1 pak odebere ~9 ml a každá iterace doplní 3,0 a odebere
~3,1 ml. Poloha stříkačky se tedy pohybuje zhruba v pásmu −1 až +10 ml od
`tare`; bez automatického plnění je to stejně široké pásmo posunuté o
−10 ml. K mezi 25 ml se to za normálních okolností nepřiblíží. Pokud na ni
sketch narazí, něco je jinak, než se čeká — zastaví se a vypíše souhrn.

---

## Vyhodnocení

Co z běhu potřebujeme vědět:

1. **Vejde se vrchol do okna 3 ml?** — sloupec `hloubka_ml` musí být
   pod `refillMl` a sloupec `novy_vrchol` má být `1`.
2. **Má hloubka trend?** — proložit `hloubka_ml` přes iterace. Analýza
   z přemapovaných dat říká, že trend být nemá (+0,004 ml/iteraci,
   r = 0,03). Kdyby se objevil, je celý závěr o creepu k přepsání.
3. **Chová se creep v úzkém pásmu jinak?** — sledovat `vrchol_C1`
   (má být stabilní) a `pokles_pF` (dostupná hloubka poklesu).
4. **Propadá se hladina?** — `drah_ml` systematicky nad 3,0 ml znamená,
   že každá iterace odebere víc, než se doplní.

Body 1 a 2 rozhodují, jestli zůstává v platnosti to, co je dnes
zapsané v `CLAUDE.md`. Pokud ano, poznámka o neověřeném mapování
iterace na cyklus se může škrtnout.
