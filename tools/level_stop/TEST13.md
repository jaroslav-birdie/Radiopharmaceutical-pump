# Test 13 – kalibrace plně kruhových prstenců

## Proč

Prstence byly 26. 11. doplněny na celý obvod (stejné výšky a šířky: spodní
5,5–8,5 mm, horní 11,5–14,5 mm), svislá elektroda odebrána. Produkční běh
13:47 zastavil s prahy z testů 9–12 hladinu ve **4,5 mm** místo ~8,7 mm.
Stará verze (hrubé pF, šablona 0,78) by zastavila ve stejném okamžiku.
Prahy je potřeba odvodit znovu z měřených výšek.

## Co sketch dělá

- **Mapa kanálů:** spodní prstenec CIN3, horní CIN1 (dotykový test 26. 11.),
  CIN2 nepřipojeno. Ověřit `m`.
- **Vše nad příspěvkem kapaliny** `L = C − suchá hodnota` (prázdná studna).
- **Šablona od začátku tlačení:** zastaví, když `L3 ≤ f × vlastní maximum`.
  Práh `f` podle cyklu: **0,90 / 0,85 / 0,80 / 0,75 / 0,70**
  (příkazem `q0.82` lze nastavit pevný práh pro všechny cykly, `q0` vrátí tabulku).
  Začíná se vysokým prahem = vysoká hladina = bezpečný směr.
- **Podíl L3/L1** se jen zapisuje (při zastavení), dává druhou kalibrační křivku.
- Záloha vrchol `L1 − L3`, poslední záchrana skok L1 o 25 % (jen po aktivaci).
- Před tlačením: L1 i L3 ≥ 0,25 pF a L3/L1 ≥ 0,60.
- Předtlak 3 ml, tlačení 1 ml / 5 s (jako produkce).

## Postup

1. Nahrát `tools/level_stop/level_stop.ino`, Serial Monitor **115200 Bd**,
   log zapisovat do souboru.
2. `m` – prst na **spodní** prstenec musí přebít **C1**, na **horní** **C3**.
   Jinak `n<spodní><svislá><horní>` (výchozí `n321`).
3. **Statický žebříček `S`** (bez tlačení, ventily v klidu):
   - prázdná studna → suché hodnoty (suchá lahvička není potřeba),
   - pak lahvička s **2, 3, 4, 5, 6, 7, 8, 10 ml**: začít 2 ml a stříkačkou
     **přidávat** do stejné lahvičky (nalévat ke dnu, ať se stěna nad
     hladinou zbytečně nesmáčí), lahvičku vždy zasunout až na dno,
   - každý krok: vložit, potvrdit, 5 s klid → řádek `# staticky V=… L1 L3 L3/L1`.
4. `t` (stříkačka plná), `i` (kontrola režimu).
5. **Dynamická série `g`:** suché hodnoty (prázdná studna), pak 5 cyklů,
   **pokaždé 10 ml** roztoku. Po zastavení a 30 s klidu **změřit výšku hladiny**
   od vnějšího dna a poslat číslo.
6. Pokud některý cyklus zastaví pod **6 mm**, další cykly nespouštět (`x`).
7. Celé zopakovat s roztokem **s inkoustem** (stejná koncentrace jako v produkčním
   testu) – zvlášť log.
8. Logy poslat jako soubory.

## Výsledek prvního běhu (26. 11. 15:07) → test 13b

Log `TEST_56_-_zastaveni_kruhove.txt` (ještě starý sketch: šablona, mapa CIN3 = spodní):

- **Mapa byla prohozená.** Při tlačení klesá nejdřív kanál CIN3, CIN1 drží
  signál až pod 4,5 mm. Kanál, který se odkryje první, musí být **horní**.
  Ve statickém žebříčku CIN3 roste mezi 5 a 7 ml (10–13 mm) a od 7–8 ml je
  plochý, což odpovídá hornímu prstenci (11,5–14,5 mm). → **horní = CIN3,
  spodní = CIN1** (sketch `n123`). Dotykový test 26. 11. tomu odporoval,
  `m` zopakovat.
- Šablona ze sketche hlídala ve skutečnosti spodní prstenec, a ten
  při poklesu hladiny ještě roste (vzdaluje se od `SHLD2`), proto
  zastavení v **5,5 mm** (f = 0,90) a **4,5 mm** (f = 0,85). Stejně
  zastavila i produkce 13:47 (4,5 mm).
- **Podíl horní/spodní** `Lh/Ls` (s opravenou mapou `L3/L1`):

| | 2 ml* | 3 ml | 4 ml | 5 ml | 6 ml | 7 ml | 8 ml | 10 ml |
|---|---|---|---|---|---|---|---|---|
| výška [mm] | 5,5 | 7,0 | 8,5 | 10,0 | 11,5 | 13,0 | 14,5 | 17,5 |
| Lh/Ls statický | 0,23 | 0,255 | 0,30 | 0,39 | 0,57 | 0,77 | 0,85 | 0,89 |

  \* „prázdná suchá“ lahvička ve statickém měření měla C3 +0,69 pF (v sérii
  `g` +0,05) → pravděpodobně nebyla suchá, 2 ml bod je nejistý.
  Dynamicky: start 0,89–0,90 (statický 10 ml: 0,89), zastavení v 5,5 mm
  0,24 a ve 4,5 mm 0,23 (statický 5,5 mm: 0,23) → **statická
  a dynamická křivka se kryjí**, i když absolutní L je s napojenou cestou
  ~2× vyšší (jiný zpětný obvod). Podíl je proto vhodné kritérium.
- Cíl 8,5–8,8 mm ⇒ podíl ≈ **0,30–0,32**. Pod 7 mm se křivka zplošťuje
  (0,23–0,26), takže práh níž nedávat.

## Test 13b

Sketch zastavuje **podílem `L3/L1 ≤ r`** od začátku tlačení. Tabulka
r = **0,45 / 0,40 / 0,35 / 0,31 / 0,28** (přibližně 10,5 / 9,8 / 9,2 /
8,7 / 8,2 mm podle statické křivky, začíná se vysoko). `q0.33` nastaví
pevný práh, `q0` vrátí tabulku. Šablona se jen zapisuje. Postup jako výše
(nejdřív `m`: prst na spodní → C1, na horní → C3), statický žebříček
`S` není nutné opakovat, stačí `g`. Přehrání logu z 15:07: na startu
podíl 0,89–0,90, prahy 0,45–0,25 padají postupně (vzduch 9,3–11,3 ml),
žádné předčasné zastavení.

## Co z toho bude

- Statický žebříček: L1, L3, L3/L1 v závislosti na výšce (h = 2,5 + 1,5·V mm)
  pro čistý roztok i inkoust.
- Dynamická série: výška hladiny pro 5 prahů šablony a odpovídající L3/L1 →
  práh pro cíl 8,5–8,8 mm a kontrola, jestli statická a dynamická křivka sedí.
- Porovnání čistý roztok × inkoust: jestli kalibrace na náplni nezávisí.
