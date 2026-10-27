# Test 4 – hladina mezi prstenci, předtlakování před otevřením pacienta

Stejný sketch (`air_push_profile.ino`), výchozí hodnoty jsou už nastavené
pro test 4 (`v10`, `f10`). Kanály beze změny: C1 spodní prstenec, C2 svislá,
C3 horní prstenec. **Bez zpětného ventilu.**

---

## Otázky

1. **Jak spolehlivě poznat, že je hladina právě mezi prstenci** (C3 odkrytý,
   C1 zakrytý)? Spodní hranu C1 zachytit cílem není.
2. **Zabrání předtlakování návratu kapaliny od pacienta?**
3. **Je artefakt pacientské cesty z testu 3 hydraulický, nebo elektrický?**
   V testu 3 při otevření pacienta stouply C1/C3 o +1,0 až +1,3 pF.

---

## Předtlakování

Po zavření pacientského ventilu a vyrovnání lahvičky s okolím může být
v hadičce k pacientovi přetlak proti lahvičce. Příčinou je výška pacienta
nad ventilem a žilní tlak. Otevření pacienta při atmosférickém tlaku
v lahvičce by kapalinu vrátilo. Proto se před otevřením, s pacientem
uzavřeným a lahvičkou na `S↔V`, vytlačí:

**V_pre = V_plyn × ΔP / p_atm**

| veličina | hodnota | poznámka |
|---|---|---|
| ΔP | 40 + 20 = 60 cm H₂O ≈ 5,9 kPa | pacient max. o délku hadičky výš + horní odhad žilního tlaku |
| p_atm | 90 kPa | konzervativně nízký (~1000 m n. m.) |
| V_plyn | 30 − náplň + vzduch ve stříkačce + 1 ml | lahvička po okraj 30 ml a mrtvý objem 1 ml jsou **odhady, neměřeno** |

| náplň | V_pre |
|---|---|
| 10 ml | 2,0 ml |
| 12 ml | 1,9 ml |
| 20 ml | 1,4 ml |

Sketch počítá V_pre z objemu, který obsluha pošle na začátku cyklu. Když
objem nepošle, počítá s náplní 0 ml, tedy s největším předtlakem.
`r<ml>` nastaví předtlak ručně, samotné `r` vrátí výpočet.

**1. cyklus je vždy bez předtlakování.** Ukáže, jestli zpětný tok
v tomhle uspořádání vůbec nastává.

Předtlak se započítává do vytlačeného objemu: osa `vytlaceno_ml` začíná
předtlakováním a celkem je to pořád 10 ml, bez doplnění.

**Cena konzervativnosti:** přebytek předtlaku po otevření vyteče
k pacientovi. Je to bezpečný směr, ale v přístroji to znamená, že
**hlídání hladiny musí běžet od otevření pacientského ventilu**, ne až od
rozjezdu motoru.

---

## Průběh jednoho cyklu (5×)

| # | krok | fáze | doba |
|---|---|---|---|
| 1 | obsluha naplní lahvičku **těsně nad horní hranu CIN3** (2–3 mm) a **pošle objem** (např. `11.5`) | — | ruční |
| 2 | vzduch `S↔V` (lahvička uzavřená), pacient zůstává `ISOLATE` | `v` | ~2,5 s |
| 3 | klid při atmosférickém tlaku (referenční „plno") | `k` | 5 s |
| 4 | předtlakování V_pre, pacient uzavřen (**ne v 1. cyklu**) | `r` | ~10 s + 3 s |
| 5 | **otevření pacienta**, klid | `p` | 5 s |
| 6 | tlačení zbytku do 10 ml, 1 ml / 5 s | `w` | ~40–50 s |
| 7 | motor stojí → **ihned** pacient `ISOLATE`, klid pod tlakem | `v`, `d` | 16 s |
| 8 | teprve teď vzduch `V↔F`, klid při atmosférickém tlaku | `v`, `o` | 9 s |
| 9 | doplnění stříkačky na 10 ml | `v`, `a` | ~30 s |
| 10 | obsluha pošle, kde je hladina, pokud je vidět (`nad C3` / `mezi` / `pod C1`) | — | ruční |

**Proč náplň těsně nad CIN3:** v testu 3 ležel přechod C3 na 7,9–10,2 ml
vytlačeného vzduchu a C1 na 13,6–15,2 ml. Při tamní náplni by 10 ml
do okna mezi prstenci nedošlo. Pokud v 1. cyklu C1 vůbec neklesne,
snížit náplň o ~1 ml a pak ji držet stejnou ve všech cyklech.

---

## Kritéria vyhodnocení (stanovená PŘEDEM)

### A. Zpětný tok a artefakt (otázky 2 a 3)

Hlavní veličina je **skok C1/C3 ve fázi `p` proti konci fáze `r`**
(v 1. cyklu proti konci `k`).

1. **Cyklus 1 (bez předtlaku):** skok nahoru při otevření, hladina se
   zvedá, znamená zpětný tok. Je to referenční velikost problému.
2. **Cykly 2–5 (s předtlakem):** hladina smí při otevření jen klesnout
   (dopředný tok přebytku), nebo zůstat.
   - Skok **dolů** nebo žádný → předtlak funguje.
   - Skok **nahoru** i s předtlakem → buď je ΔP podhodnocené, nebo je
     artefakt **elektrický**. Rozliší to velikost: hydraulický tok o
     desetiny ml dá ~0,01–0,05 pF (rampa ~0,045 pF/ml), elektrický skok
     z testu 3 ~1 pF.
3. **Nárůst během tlačení** (`w` proti `k`) proti +2,5 až +4 pF z testu 3.
4. **Tlakový offset** (konec `d` proti ustálenému `o`) při stojící hladině.
   K tomu skok mezi `k` a koncem `r`: vliv předtlaku na signál bez toku.

### B. Hladina mezi prstenci (otázka 1)

5. **Průběh při tlačení:** C3 klesne a ustálí se na své podlaze dřív, než
   C1 začne klesat. Mezi koncem poklesu C3 a začátkem poklesu C1 musí být
   rozpoznatelný úsek. Jeho šířka v ml se změří a musí se opakovat.
6. **C3 přestane reagovat.** Na podlaze C3 nereaguje na tlak ani na
   ventil (v testu 3 −0,05 pF proti −0,9 pF zakrytého). Ověřit ve fázích
   `d` a `o`.
7. **C3 jako měřítko pro C1.** Prstence jsou spárované (za plna
   `C1 − C3` = +0,05 pF). Pokles C3 změřený za běhu (plno → podlaha) se
   porovná s tím, o kolik je C1 v daný okamžik pod svým „plno". Cíl je
   práh pro C1 odvozený z C3, bez hodnoty „prázdno" známé předem.
8. **Rozestup přechodů** C3 → C1 (50% body) proti testu 3: 5,3 ml,
   sd 0,27 ml.

---

## Příprava

- Konec pacientské hadičky po celou sérii **na stejném místě**, do stejné
  nádoby, ve stejné výšce. Výšku konce vůči lahvičce a to, jestli je
  ponořený, zapsat při prvním potvrzení (např. `11.5 konec 10 cm pod, ponoren`;
  objem musí být na začátku).
- Platí všechno z [`TEST3.md`](TEST3.md) k nestíněné penicilince: nesahat,
  fixovat koaxy, zapisovat rušení.

## Postup

1. Vzduchová stříkačka **plná** (10 ml), ventily a stříkačka osazené.
2. Penicilinka ve studni.
3. Zachytávání logu, **115200 Bd**:
   ```
   tools/serial_log.py /dev/ttyUSB0 test4.csv --baud 115200
   ```
4. `t`, `a`, zkontrolovat `i`: CAPDAC1–3 nízké, `vzduch/cyklus=10.0`,
   `prvni davka=10.0`, `predtlak: vypocet`.
5. `g` – start. Na výzvu naplnit, **poslat objem**. Po cyklu poslat
   polohu hladiny.

Log poslat jako **soubor**, ne vložený text.
