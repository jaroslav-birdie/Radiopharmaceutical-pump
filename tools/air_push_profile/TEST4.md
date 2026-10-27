# Test 4 – hladina mezi prstenci, zpětný ventil k pacientovi

Stejný sketch (`air_push_profile.ino`), výchozí hodnoty jsou už nastavené
pro test 4 (`v10`, `f10`). Kanály beze změny: C1 spodní prstenec, C2 svislá,
C3 horní prstenec.

---

## Otázky

1. **Jak spolehlivě poznat, že je hladina právě mezi prstenci** (C3 odkrytý,
   C1 zakrytý)? Spodní hranu C1 zachytit cílem není.
2. **Je artefakt pacientské cesty z testu 3 hydraulický, nebo elektrický?**
   V testu 3 při tlačení stouply oba prstence o +2,5 až +4 pF. Při pohybu
   pacientského ventilu skákaly o ±1 pF, a to jen tehdy, když byly zakryté.
   - **Hydraulický:** neustálený tlak, kapalina se při změně tlaku vrací
     od pacienta. Zpětný ventil a okamžité zavření ho mají odstranit.
   - **Elektrický:** sloupec kapaliny v hadičce mění zpáteční cestu.
     Zpětný ventil s ním nic neudělá, pomůže jen definovaná zem.

---

## Změny proti testu 3

| | test 3 | test 4 |
|---|---|---|
| pacientská jehla | bez ventilu | **zpětný ventil** – kapalina se od pacienta nevrátí |
| vzduch na cyklus | 20 ml s doplněním při 5 a 15 ml | **10 ml najednou**, bez doplnění |
| náplň | nad 10 ml | **těsně nad horní hranou CIN3** (2–3 mm) |
| po dojezdu | 15 s klid s otevřeným pacientem, pak zavřít a odvzdušnit | pacient **ihned** `ISOLATE`, klid pod tlakem, **teprve pak** `V↔F` |
| kontrola artefaktu | — | fáze `u`: přepínání pacientského ventilu bez toku |

**Proč náplň těsně nad CIN3:** v testu 3 ležel přechod C3 na 7,9–10,2 ml
vytlačeného vzduchu a C1 až na 13,6–15,2 ml. Při tamní náplni by 10 ml
vzduchu do okna mezi prstenci ani nedošlo. Když hladina začne těsně nad
C3, 10 ml by mělo projít oběma prstenci bez přerušení. Rozestup mezi
přechody je ~5,3 ml.

Zpětný ventil se otevírá až od určitého tlaku, takže na natlakování
lahvičky padne víc vzduchu a přechody se posunou na vyšší objem. Pokud
v prvním cyklu C1 neklesne, snížit náplň o ~1 ml. Pak ji držet stejnou
ve všech cyklech.

---

## Průběh jednoho cyklu (5×)

| # | krok | fáze | doba |
|---|---|---|---|
| 1 | obsluha naplní lahvičku těsně nad CIN3 a pošle objem (např. `11.5`) | — | ruční |
| 2 | vzduch `S↔V`, pacient `OPEN` | `v` | ~3,5 s |
| 3 | klid při atmosférickém tlaku, plná lahvička (referenční „plno") | `k` | 5 s |
| 4 | **přepínání pacienta 2× OPEN/ISOLATE**, plná lahvička | `u` | 20 s |
| 5 | pacient `OPEN` | `v` | 2,5 s |
| 6 | **tlačení 10 ml**, 1 ml / 5 s | `w` | 50 s |
| 7 | motor stojí → **ihned** pacient `ISOLATE`, klid pod tlakem | `v`, `d` | 16 s |
| 8 | teprve teď vzduch `V↔F`, klid při atmosférickém tlaku | `v`, `o` | 9 s |
| 9 | **přepínání pacienta 2× OPEN/ISOLATE**, hladina po vytlačení | `u` | 20 s |
| 10 | doplnění stříkačky na 10 ml | `v`, `a` | ~30 s |
| 11 | obsluha pošle, kde je hladina, pokud je vidět (`nad C3` / `mezi` / `pod C1`) | — | ruční |

Cyklus trvá ~2,5 min čistého měření. Na začátku dalšího cyklu obsluha
dorovná lahvičku na stejnou náplň.

---

## Kritéria vyhodnocení (stanovená PŘEDEM)

### A. Artefakt (otázka 2)

1. **Skok při přepnutí ve fázi `u`** u plné lahvičky (krok 4).
   - Skok ~1 pF jako v testu 3 → artefakt je **elektrický**.
   - Skok < 0,1 pF → elektrická složka je zanedbatelná.
2. **Nárůst během tlačení** (`w` proti `k`) musí být výrazně menší než
   +2,5 až +4 pF z testu 3. Jinak zpětný ventil hydraulickou část
   nevyřešil.
3. **Tlakový offset** (konec `d` proti ustálenému `o`) při stojící
   hladině. Je to čistý vliv přetlaku, bez toku.

### B. Hladina mezi prstenci (otázka 1)

4. **Průběh při tlačení:** C3 klesne a ustálí se na své podlaze dřív, než
   C1 začne klesat. Mezi koncem poklesu C3 a začátkem poklesu C1 musí být
   rozpoznatelný úsek. Jeho šířka v ml se změří a musí se opakovat.
5. **C3 přestane reagovat.** Na podlaze C3 nereaguje na tlak ani na
   ventil (v testu 3 −0,05 pF proti −0,9 pF zakrytého). Ověřit ve fázích
   `d`, `o` a `u` po vytlačení.
6. **C3 jako měřítko pro C1.** Prstence jsou spárované (za plna
   `C1 − C3` = +0,05 pF). Pokles C3 změřený za běhu (plno → podlaha) se
   porovná s tím, o kolik je C1 v daný okamžik pod svým „plno". Cíl je
   práh pro C1 odvozený z C3, bez hodnoty „prázdno" známé předem.
7. **Rozestup přechodů** C3 → C1 (50% body) jako v testu 3: 5,3 ml,
   sd 0,27 ml. Se zpětným ventilem a okamžitým zavřením by měl být
   stejný nebo stabilnější.

---

## Příprava

- **Zpětný ventil** na pacientské jehle, propustný směrem k pacientovi.
- **Hadičku za ventilem před sérií celou naplnit.** Vzduchová bublina
  v ní by se chovala jako pružina a zkreslila by tlak i dobu toku.
- Konec hadičky po celou sérii **na stejném místě**, do stejné nádoby,
  ve stejné výšce.
- Platí všechno z [`TEST3.md`](TEST3.md) k nestíněné penicilince: nesahat,
  fixovat koaxy, zapisovat rušení.

## Postup

1. Vzduchová stříkačka **plná** (10 ml), ventily a stříkačka osazené.
2. Penicilinka ve studni.
3. Zachytávání logu, **115200 Bd**:
   ```
   tools/serial_log.py /dev/ttyUSB0 test4.csv --baud 115200
   ```
4. `t`, `a`, zkontrolovat `i`: CAPDAC1–3 nízké a `vzduch/cyklus=10.0`,
   `prvni davka=10.0`.
5. `g` – start. Na výzvu naplnit, **poslat objem**. Po cyklu poslat
   polohu hladiny.

Log poslat jako **soubor**, ne vložený text.
