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
   - prázdná studna → suché hodnoty, prázdná suchá lahvička → zápis,
   - pak postupně lahvička s **2, 3, 4, 5, 6, 7, 8, 10 ml** (objem odměřit
     stříkačkou do suché lahvičky, lahvičku vždy zasunout až na dno),
   - každý krok: vložit, potvrdit, 5 s klid → řádek `# staticky V=… L1 L3 L3/L1`.
4. `t` (stříkačka plná), `i` (kontrola režimu).
5. **Dynamická série `g`:** suché hodnoty, prázdná lahvička, pak 5 cyklů,
   **pokaždé 10 ml** roztoku. Po zastavení a 30 s klidu **změřit výšku hladiny**
   od vnějšího dna a poslat číslo.
6. Pokud některý cyklus zastaví pod **6 mm**, další cykly nespouštět (`x`).
7. Celé zopakovat s roztokem **s inkoustem** (stejná koncentrace jako v produkčním
   testu) – zvlášť log.
8. Logy poslat jako soubory.

## Co z toho bude

- Statický žebříček: L1, L3, L3/L1 v závislosti na výšce (h = 2,5 + 1,5·V mm)
  pro čistý roztok i inkoust.
- Dynamická série: výška hladiny pro 5 prahů šablony a odpovídající L3/L1 →
  práh pro cíl 8,5–8,8 mm a kontrola, jestli statická a dynamická křivka sedí.
- Porovnání čistý roztok × inkoust: jestli kalibrace na náplni nezávisí.
