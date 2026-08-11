# Živá detekce kritické (dolní) hrany — dávkový test (FDC1004)

Na rozdíl od `capacitive_sweep_test` a `capacitive_cycle_test` (které jen
**loguji** data pro pozdější zpracování) tenhle nástroj přímo **běží
navržený detekční algoritmus** — vyhlazení 25 vzorků + pokles od
**neomezeného maxima od začátku cyklu** C1 + potvrzení přes víc vzorků — a
motor se podle jeho výstupu sám zastavuje. Cíl: ověřit, že algoritmus najde
**dolní (kritickou) hranu** kruhové elektrody spolehlivě, opakovaně, na
velkém počtu cyklů (výchozí 10) bez zdlouhavého ručního zásahu mezi nimi.

> **v5 — dávkový režim s automatickým doplňováním.** Po potvrzení, že
> detekce kritické hrany funguje spolehlivě (viz `v4` níže), bylo potřeba
> ověřit to na větším počtu opakování, než dovoluje ruční doplňování mezi
> každým cyklem. Tři změny:
> 1. **Odstraněn `searchSafetyMl` jako „alarm nedetekované hrany"** —
>    skutečná aplikace nic takového nemá: vytlačený objem vzduchem proti
>    neznámému odporu je právě ta neznámá veličina, kterou kapacitní
>    snímání nahrazuje, takže objemový failsafe v produkci neexistuje a
>    bench nástroj by ho neměl předstírat jako smysluplný koncept.
>    `MECH_LIMIT_ML` (viz níže) je nově vyhradně **hardwarová ochrana
>    zdvihu stříkačky**, ne detekční/alarmová logika.
> 2. **Zrušeno sledování `maxSyringeMl`/celkového odběru od `t`** —
>    odsávání i doplňování teď jde ze **stejné** stříkačky/motoru
>    v uzavřeném cyklu (viz níže), takže se pozice v průměru sama
>    vyvažuje a není potřeba obsluhu vyzývat k ručnímu doplnění stříkačky.
> 3. **Ruční doplňování lahvičky nahrazeno automatickým** — po potvrzení
>    detekce sketch sám dávkuje **náhodný objem 6–17 ml** zpět do lahvičky
>    tou samou stříkačkou/motorem (opačný směr), a rovnou pokračuje dalším
>    cyklem. Vizuální kontrola se teď potvrzuje binárně `1` (OK) / `0`
>    (chybná detekce) místo volného `y` — obojí automaticky pokračuje.

> **Proč se pozice stříkačky sama vyvažuje:** kritická hladina je fyzická
> vlastnost elektrody, ne funkce toho, kolik bylo v lahvičce na začátku —
> zbytkový objem při kritické hraně `C` je tedy mezi cykly přibližně
> konstantní. Když se po cyklu doplní náhodný objem `R` (6–17 ml), příští
> cyklus začíná na hladině `C + R` a odsaje `R` (= `C + R − C`). Odebraný
> objem tedy v průměru odpovídá objemu doplněnému v předchozím kroku —
> čistý posun pozice stříkačky přes 10 cyklů by měl být malý, jen šum
> kolem nuly (kolísání `C` mezi cykly). `MECH_LIMIT_ML=25` je konzervativní
> rezerva pro případ, že by se tohle očekávání nepotvrdilo.

> **v6 — ochrana proti vnějšímu rušení přes CIN2.** Dávkový test 10 cyklů
> ukázal 2 selhání, obě se stejným mechanismem: obsluha se dotýkala studny,
> ruka přidala ke **všem** elektrodám společnou (common-mode) kapacitu, to
> nafouklo sledované maximum C1, a když ruka odešla, následný pokles se
> vyhodnotil jako kritická hladina — o mnoho ml dřív. Klíč k obraně: skutečná
> změna hladiny a vnější rušení vypadají na **CIN2 úplně jinak**. Detaily
> a naměřené hodnoty viz „Ochrana proti rušení" níže.

> **v7 — oprava po prvním ostrém použití v6.** Dva problémy, žádný dotyk
> se přitom nekonal:
> 1. **Pevný práh (40 fF) byla chyba.** Nové sezení mělo elektricky
>    šumnější prostředí (v tomhle případě 3D tiskárna vedle aparatury) —
>    statický test ukázal CIN2 σ 6,6× a p-p 10,9× vyšší než na sezení,
>    kde byl práh měřený. Přesně ten typ chyby, kterému jsme se snažili
>    vyhnout u `deltaCritical`: **konstanta z jiného sezení se nepřenáší**.
>    Oprava: práh se teď **přepočítává před každým odsáváním** z právě
>    naměřeného klidového šumu během ustálení (`c2GuardMultiplier` ×
>    naměřený klidový strop, s podlahou `c2GuardMinFloor` a stropem
>    `C2_GUARD_MAX_CEILING=0,15 pF` proti sebe-oslepení, kdyby zrovna
>    během kalibrace někdo sahal na studnu).
> 2. **Chování po rušení bylo nekonzistentní** s principem u `ST_PAUSED`
>    v `CLAUDE.md` („stav se při pauze nesmí ztratit"). Původně se celý
>    cyklus zahodil a začalo se znovu s náhodným doplněním — to je přesně
>    ten „reset při pauze", který jsme jinde označili za nebezpečný směr
>    chyby (zpozdí detekci). Ochrana CIN2 teď funguje jako **skutečná
>    pauza**: motor stojí, sledovaný vrchol C1 zůstává beze změny, po `y`
>    pokračuje **tentýž** cyklus přesně odtud — žádné doplňování, žádný
>    nový cyklus.

---

## v2 — oprava po prvním testu (klouzavé okno místo "maxima od startu")

V prvním běhu se ukázalo, že "maximum od začátku fáze" je zranitelné vůči
**pomalému driftu** (teplotnímu/elektrickému), který nesouvisí se skutečnou
hranou: signál nafouknutý o pár tisícin jedním šumovým výkyvem hned na
začátku fáze zůstal jako referenční maximum, a pomalý pokles rozprostřený
přes ~9 ml pak i navzdory vyhlazení nastřádal dost na to, aby se to
vyhodnotilo jako "horní hrana" — mnohem dřív, než skutečná hrana fyzicky
mohla nastat. Nesprávný vrchol se pak zamkl jako reference pro fázi 2, a
dolní hranu se pak nepodařilo najít vůbec.

**Oprava:** referenční maximum se teď počítá jen z **posledních
`referenceWindowMl` ml** (výchozí 5 ml), ne od začátku fáze. Pomalý drift
rozprostřený přes víc ml než okno se v žádném jednotlivém okně nenastřádá
na `deltaUpper`, zatímco skutečný přechod (podle dat trvá cca 1,5–3,5 ml)
se do 5ml okna pohodlně vejde. Navíc přidána pojistka `minWithdrawMl`
(výchozí 2 ml) — žádná detekce dřív, než se aspoň tolik odsaje.

Výstup teď navíc loguje sloupec `ref` (aktuální referenční hodnota) — pro
kontrolu, jestli klouzavé okno dělá, co má.

## v3 — oprava po druhém testu (minWithdrawMl musí být ≥ referenceWindowMl)

Druhý test ukázal **jiný** problém: hned po rozjezdu motoru (prvních ~3 ml)
se objevil krátký "rozkolísaný" přechod — pokles a zpětný nárůst signálu
o ~0,07–0,10 pF, řádově stejně velký jako signál, co hledáme pro horní
hranu. Tenhle přechod pak vytvořil falešné referenční maximum, ze kterého
následný (taky ne úplně skutečný) pokles spustil detekci o mnoho ml dřív.

Ukázalo se, že v1 oprava (klouzavé okno) v tomhle případě vůbec nezabrala:
dokud neuplyne aspoň `referenceWindowMl` (5 ml), okno se teprve **plní** a
chová se úplně stejně jako staré neomezené maximum — nic nezapomíná.
`minWithdrawMl` byl nastavený na 2 ml, tedy MÉNĚ než `referenceWindowMl` —
detekce byla povolená dřív, než okno stihlo vůbec začít "zapomínat".

**Oprava:** `minWithdrawMl` teď musí být >= `referenceWindowMl` — sketch to
sám vynutí (a upozorní), ať se to už nedá nastavit špatně. Výchozí hodnota
zvednuta na 6 ml. `deltaUpper` zvednuto na 0,10 pF jako druhá pojistka.

**Poctivě řečeno:** tenhle přechodový jev po startu motoru se objevil už
potřetí (poprvé jako neobjasněná anomálie v `capacitive_cycle_test`, teď
dvakrát tady) — vypadá to na skutečný, opakovatelný mechanický/elektrický
jev, ne na náhodu. Jeho amplituda je bohužel podobná signálu pro horní
hranu, takže si nejsem jistý, že tahle oprava stačí napoprvé. Horní hrana
je sekundární cíl — pokud bude dál dělat problémy, dává smysl prioritizovat
spolehlivost dolní (kritické) hrany, kde je marže nad šumem podstatně
větší (deltaCritical=0,22 vs. pozorovaná amplituda přechodu ~0,07–0,10).

## v4 — živá detekce HORNÍ hrany byla odstraněna

Po 3 nezávislých testech na reálném HW (viz `v2`/`v3` výše + detailní
diskuze v `CLAUDE.md`) se ukázalo, že celá dlouhá, šumová stoupající fáze
C1 obsahuje lokální výkyvy (~0,07–0,10 pF) srovnatelně velké jako hledaný
signál horní hrany — a to **po celé délce** stoupání, ne jen na začátku.
Žádné ladění okna/prahu to spolehlivě nevyřešilo. Zkoumali jsme i kombinaci
s CIN2 (svislá elektroda): v místě skutečného vrcholu C1 je C2
hladká/monotónní bez jakékoli lokální události (ověřeno na datech z
`capacitive_cycle_test`) — C2 tenhle typ přechodu vůbec „nevidí", protože
nemá kruhovou geometrii se dvěma hranami jako CIN1. Shield elektrody
(SHLD1/SHLD2) nejsou přes registry FDC1004 vůbec čitelné, nemohou sloužit
jako další kanál. Horní hrana se tedy nedá živě spolehlivě detekovat —
nástroj se od `v4` soustředí výhradně na dolní (kritickou) hranu, kde je
marže nad šumem podstatně větší (`deltaCritical=0,22` vs. pozorovaná
amplituda šumu ~0,07–0,10 pF, nikdy falešně nespustil ve 3 terénních
testech). Poloha vrcholu C1 se dál loguje a hlásí, ale jen **informativně**
— není to zastavovací/rozhodovací bod.

---

## Postup jednoho cyklu (od v5)

1. **ODSÁVÁNÍ** — motor odsává spojitě od aktuální pozice, dokud algoritmus
   nenajde **dolní (kritickou) hranu** (pokles od průběžného, neomezeného
   maxima C1 od začátku cyklu). Pak se **sám zastaví** a vypne driver.
2. Obsluha vytáhne lahvičku, zkontroluje hladinu, vrátí zpět do studny,
   potvrdí **`1`** (detekce vypadala správně) nebo **`0`** (detekce
   vypadala chybně).
3. Sketch **sám** dávkuje náhodný objem **6–17 ml** zpět do lahvičky (ta
   stejná stříkačka/motor, opačný směr), a rovnou spustí další cyklus
   odsávání — bez dalšího ručního zásahu.
4. Opakuje se celkem `cycleCountTarget`× (výchozí **10**).

Po posledním cyklu sketch vypíše souhrnnou tabulku všech detekovaných
hran (včetně potvrzení `1`/`0` a doplněného objemu) a sekvence skončí.

**Cyklus 1 je výjimka:** počáteční hladinu v lahvičce ještě nemá z čeho
systém odvodit, takže ji obsluha naplní ručně na libovolnou hladinu
~6–17 ml (stejný rozsah jako pozdější automatické doplňování), než se
stiskne `g`. Od cyklu 2 už plní systém sám.

## Proč nezávisí na počáteční hladině

Detekční algoritmus sleduje jen **tvar** signálu (vyhlazená hodnota C1
roste → vrchol → klesá), ne absolutní `level_ml`. Funguje tedy stejně,
ať cyklus startuje kdekoliv v očekávaném rozmezí ~6–17 ml.
`level_ml` ve výstupu je čistě orientační krokové počítadlo **od
posledního `t`** (přes všechny cykly) — pozice stříkačky, ne obsah
lahvičky.

Průběžné maximum C1 (`peak_ref` ve výstupu) se nikdy „nezapomíná" —
neomezené maximum od začátku cyklu je bezpečné právě proto, že po
skutečném vrcholu C1 už jen monotónně klesá až ke kritické hraně (ověřeno
na `capacitive_cycle_test`), takže se samo zamkne na správné hodnotě, aniž
by potřebovalo klouzavé okno jako dřívější (zavržený) pokus o živou horní
hranu.

## Ochrana proti rušení (CIN2) — od v6

Dávkový test 10 cyklů selhal 2×, pokaždé při doteku studny. Mechanismus byl
v obou případech stejný a je to **jediný způsob, jak může tenhle algoritmus
selhat nebezpečně**: cokoli, co dočasně **zvedne** C1, nafoukne sledované
maximum, a následný návrat k normálu pak vypadá jako pokles hladiny.

| Cyklus | Co se stalo | Falešná detekce |
|---|---|---|
| 6 | ruka držena na studni celou dobu, pak sundána (C1 skočilo 5,92 → 4,91 pF v jednom vzorku) | −10,73 ml místo ~−19 |
| 9 | náhodné doteky, poslední zvedl C1 na 4,6 pF | −15,33 ml místo ~−19 |

**Řešení:** hlídat CIN2. Svislá elektroda sice **nevidí hranu prstence**
(proto byla ve v4 zamítnuta pro detekci horní hrany), ale rušení vidí
výborně — ruka je common-mode jev, který se naváže na obě elektrody, zatímco
změna hladiny se na C2 projeví jen extrémně pomalým monotónním poklesem.

Naměřeno na všech 10 cyklech (klouzavý průměr 5 vzorků, změna přes 3 vzorky):

| | hodnota |
|---|---|
| nejhorší **čistý** úsek (nesmí spustit) | 20 fF |
| nejslabší zachycený dotyk | 57 fF |
| dotyk, který způsobil selhání (cyklus 9) | **429 fF**, 13 vzorků po sobě |
| sundání ruky (cyklus 6) | **229 fF**, 7 vzorků po sobě |

Práh 40 fF s potvrzením 2 vzorky tehdy obě skupiny odděloval s rezervou.
**Byl to ale pevný práh z jednoho konkrétního sezení** — první ostré
použití (jiný den, elektricky šumnější prostředí) ukázalo, že to nestačí:
běžný šum tam běžně přesahoval 40 fF, aniž se čehokoli někdo dotýkal.

**Od v7 se práh počítá znovu před každým odsáváním** (`cm` × naměřený
klidový strop během ustálení, podlaha `cg`, strop 0,15 pF proti
sebe-oslepení) — stejný princip jako u trackování vrcholu C1, žádné
kouzelné číslo přenesené z jiného sezení.

**Chování při detekci rušení:** motor se okamžitě zastaví. Sledovaný
vrchol C1 **zůstává zachovaný** (žádný reset) — po `y` (jednoznakově, bez
Enteru) pokračuje **tentýž** cyklus přesně odtud, žádné doplňování ani
nový cyklus. Rušení tak nikdy nemůže způsobit **předčasnou** detekci — jen
ji odložit. To je bezpečný směr chyby, stejný princip jako `ST_PAUSED`
v produkčním návrhu (viz `CLAUDE.md`).

> **Pozn. k reálnému přístroji:** finální sestava bude mít 2 mm olova
> (stínění radiofarmaka) plus Faradayovu klec, což tenhle typ rušení
> nejspíš eliminuje úplně. Ochrana je i tak na místě: nestojí nic (CIN2 se
> stejně měří), a hlavně **mění tichou nesprávnou odpověď na hlášenou
> poruchu**. U bezpečnostně kritické funkce je to podstatný rozdíl —
> stínění chrání proti rušení, které *očekáváme*; tahle ochrana zachytí
> i to, které nečekáme (uvolněná elektroda, prasklý spoj, kondenzace).

## Mechanická ochrana stříkačky (ne detekční alarm)

`MECH_LIMIT_ML` (pevná konstanta, 25 ml) je čistě **hardwarová** pojistka
proti tomu, aby motor mlel proti mechanickému dorazu stříkačky, kdyby se
detekce chovala výrazně mimo očekávání (viz „Proč se pozice sama
vyvažuje" výše — za normálních okolností by se pozice od `t` neměla
vzdálit o víc než pár ml). **Není to totéž** co dřívější `searchSafetyMl`
alarm — ten se v `v5` odstranil úmyslně, protože reálná aplikace žádný
objemový failsafe na detekci kritické hladiny nemá (vytlačený objem
vzduchu proti neznámému odporu lahvičky je neznámá veličina — proto se
vůbec používá kapacitní snímání). Pokud `MECH_LIMIT_ML` někdy zareaguje,
je to čistě „zastav motor, ať se nic nepoškodí", ne diagnostická informace
o chybějící hraně.

**Naplň stříkačku na rozumnou střední hodnotu před startem** (doporučeno
~30 ml) — dává rezervu na obě strany (odsávání i doplňování) v rámci
25ml limitu.

---

## Postup

1. **Nahrát sketch** do Arduina.
2. **Stříkačku naplnit na ~30 ml** (viz „Mechanická ochrana" výše).
3. **Lahvičku naplnit na libovolnou hladinu ~6–17 ml** (jen pro cyklus 1),
   vložit do studny.
4. **Serial Monitor**, 9600 Bd, zakončení Enter (nebo `tools/serial_log.py`
   — doporučeno, výstup může být dlouhý).
5. `o` — driver ON. Volitelně `j`/`k` — odvzdušnění.
6. `t` — tare (vynuluje krokové počítadlo pozice).
7. `g` — **spustí celou dávkovou sekvenci** (`cycleCountTarget` cyklů,
   výchozí 10, odsávání cyklu 1 začne hned).
8. **Po každé detekci kritické hrany**: vytáhnout lahvičku, zkontrolovat,
   vrátit do studny, stisknout `1` (OK) nebo `0` (chybná detekce) — sketch
   dál pokračuje sám (doplnění + další cyklus), žádný další zásah není
   potřeba.
9. `x` kdykoliv za běhu = okamžité zastavení (nouzové), vypne driver a
   sekvenci ukončí.

## Parametry (nastavit před `g`, případně upravit i za běhu)

| Příkaz | Význam | Výchozí |
|---|---|---|
| `dc<pF>` | δ_critical — pokles od (neomezeného) maxima = dolní/kritická hrana | `dc0.22` |
| `cf<n>` | kolik po sobě jdoucích vzorků musí práh držet (potvrzení) | `cf5` |
| `cm<x>` | násobitel prahu ochrany CIN2 (× naměřený klidový šum); `cm0` = **vypnout** | `cm2.5` |
| `cg<pF>` | minimální podlaha prahu ochrany CIN2 (pro neobvykle tiché sezení) | `cg0.020` |
| `ck<n>` | kolik po sobě jdoucích vzorků musí držet práh ochrany CIN2 | `ck2` |
| `p<ms>` | perioda vzorku | `p200` |
| `r<n>` | počet cyklů (max 10) | `r10` |

Práh ochrany CIN2 (`c2GuardEffective`) se **přepočítává při každém
ustálení** (start cyklu i pokračování po rušení) — `i` kdykoliv vypíše
poslední zkalibrovanou hodnotu spolu s naměřeným klidovým stropem, ať vidíš,
jestli kalibrace dělá, co má.

Výchozí hodnoty `dc`/`p` vycházejí z analýzy 5 spojitých cyklů
(`tools/capacitive_cycle_test`) a ze 3 terénních testů s tímhle nástrojem
(viz historie `v2`/`v3`/`v4` výše) — `dc=0.22` má naměřenou marži cca 2–3×
nad nejhorším pozorovaným šumem při běžícím motoru a ve všech 3 testech
nikdy nespustil falešně.

`i` kdykoliv vypíše aktuální hodnoty všech parametrů a stav sekvence.

## Formát výstupu

```
# cycle;t_ms;level_ml;faze;C1_raw;C1_smooth;peak_ref;C2_raw;c2rate
# --- CYKLUS 1 : odsavani, hledani DOLNI (KRITICKE) HRANY ---
# ustaleni (motor stoji, cca 5 s)...
1;0;0.00;w;3.7702;3.7702;3.7702;2.4124;0.0021
...
# *** DOLNI (KRITICKA) HRANA DETEKOVANA *** cyklus=1 poloha(od tare)=-9.10 ml C1_ted=3.7300 pokles_od_vrcholu=0.2260
# (info) vrchol C1 byl pri poloze=-6.40 ml C1=3.9560
# Vytahni lahvicku, zkontroluj hladinu, vrat zpet do studny.
# '1' = detekce OK   '0' = detekce chybna  (obojí -> automaticky dalsi cyklus)
[obsluha stiskne '1' nebo '0']
# potvrzeno: OK
# --- CYKLUS 1 : automaticke doplneni lahvicky, 11.34 ml ---
1;...;...;r;3.7305;3.7305;0.0000;2.4130;0.0000
...
# --- CYKLUS 2 : odsavani, hledani DOLNI (KRITICKE) HRANY ---
...
# === SOUHRN VSECH CYKLU ===
# cyklus;vrchol_level_ml;vrchol_C1;dolni_level_ml;dolni_C1;pokles_pF;potvrzeno;ruseni_pauz;doplneno_po_cyklu_ml
1;-6.40;3.9560;-9.10;3.7300;0.2260;1;0;11.34
...
10;...;...;...;...;...;1;0;-
# potvrzeno OK=9 chybne=1 celkem pauz kvuli ruseni=2
# vrchol_* je jen INFORMATIVNI (poloha maxima C1), NENI to detekovana/pouzita hrana
# ruseni_pauz = kolikrat behem cyklu zasahla ochrana CIN2 (vrchol C1 se pri pauze neztratil)
# === SEKVENCE HOTOVA ===
```

Pokud zasáhne ochrana CIN2 (od v7 — cyklus se nezahazuje, jen pozastaví):

```
# *** RUSENI DETEKOVANO (CIN2) *** cyklus=6 poloha(od tare)=-10.41 ml  zmena_C2=0.2292 pF  prah=0.0850
# Motor pozastaven, sledovany vrchol NEZTRACEN.
# Nesahej na studnu ani kabelaz. Az bude klid, potvrd 'y'
# -> cyklus pokracuje presne odtud (bez doplneni, bez restartu). 'x' = ABORT.
[obsluha pocka, potvrdi 'y' (jednoznakove, bez Enteru)]
# ustaleni (motor stoji, cca 5 s, kalibruje se ochrana CIN2)...
# ochrana CIN2: zmereny klidovy max=0.0340 pF -> prah=0.0850 pF
# pokracuji v odsavani po ruseni (vrchol zachovan)
```

- `level_ml` je vždy vzhledem k poslednímu `t` — je to pozice **stříkačky**
  (klesá při odsávání, roste při doplňování), ne obsah lahvičky. Pro
  srovnání jednotlivých cyklů mezi sebou je potřeba je zarovnat podle
  vlastního vrcholu (stejně jako v analýze `capacitive_cycle_test`).
- `faze` je `w` (odsávání/withdraw, s aktivní detekcí) nebo `r` (automatické
  doplnění/refill, bez detekce — `peak_ref` je v těchto řádcích nevýznamný,
  vždy 0).
- `C1_smooth` je hodnota, na které je detekce postavená — pro kontrolu
  algoritmu je užitečnější než `C1_raw`.
- `peak_ref` je průběžné (neomezené) maximum vyhlazeného C1 od začátku
  **odsávací** fáze cyklu — sleduj, jestli `C1_smooth - peak_ref` dává
  smysl.
- `c2rate` je |změna vyhlazeného C2 přes 3 vzorky| — vstup ochrany proti
  rušení. Práh, proti kterému se porovnává, **není fixní** — přepočítává se
  při každém ustálení z aktuálního klidového šumu (viz `i` pro poslední
  zkalibrovanou hodnotu). Ve fázi `r` (doplňování) se nevyhodnocuje a loguje
  se jako 0.
- `vrchol_level_ml`/`vrchol_C1` v souhrnu je jen informativní poloha
  maxima C1 — NENÍ to detekovaná/rozhodovací hrana, jen se hodí pro
  pozdější analýzu (viz `CLAUDE.md`, motivace pro dělené dávkování).
- `potvrzeno` = co obsluha stiskla po vizuální kontrole (`1`/`0`).
  `doplneno_po_cyklu_ml` = náhodný objem dávkovaný po tomhle cyklu do
  dalšího (`-` u posledního cyklu, po něm se už nedoplňuje).
- Po zkopírování celého výstupu pošli k analýze — hlavně mě zajímá, jestli
  se `1`/`0` potvrzení shodují s tím, co ukazují data (např. jestli nějaké
  `0` koreluje s neobvyklým `pokles_od_vrcholu` nebo polohou vrcholu).
