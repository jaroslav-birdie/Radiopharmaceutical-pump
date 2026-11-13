# Test 11 – kontroly s prahem 0,80, zkouška bez lahvičky, nižší start

Sketch `level_stop.ino` (po startu `POSTUP TESTU 11`). Zastavení jako
v testech 9–10 (8,75 ± 0,21 mm). Změna: obě kontroly C3/C1 (před tlačením
a v okamžiku maxima C3) mají práh **0,80** místo 0,88. V testu 10 byly
u startu jako v iteraci jen o 0,01–0,02 nad starým prahem.

## Otázky

1. Odmítne kontrola start **bez lahvičky**? (v testu 10 se nezkoušelo)
2. Zastaví šablona / podíl stejně i při **nižším startu** (6 ml ≈ 11,5 mm,
   konec plató C3)?
3. Nehlásí kontroly s prahem 0,80 zbytečné chyby?

## Postup

1. Nahrát `level_stop.ino`, po startu `POSTUP TESTU 11`, log 115200 Bd.
   `i` musí ukázat `kontroly 0.80 (test 11)`.
2. Vzduchová stříkačka plná, ventily osazené, hadička naplněná,
   **studna prázdná**. `m` (dotykový test), `t`.
3. `g` → 1. výzva: studna **PRÁZDNÁ** → suché hodnoty.
4. **Cyklus 1 – bez lahvičky:** na výzvu k naplnění potvrdit **bez
   lahvičky**. Musí přijít `# *** lahvicka chybi nebo je prazdna, netlaci se ***`
   a nová výzva. Teprve pak vložit lahvičku s **9–14 ml** a potvrdit.
5. **Cykly 2 a 3:** **7 ml** (stříkačkou) – start jako v iteraci.
6. **Cykly 4 a 5:** **6 ml** (stříkačkou) – start na konci plató C3.
   Pokud kontrola start odmítne (`hladina neni nad hornim prstencem`),
   přidat **1 ml** (na 7 ml), napsat do potvrzení `doplneno 1 ml`
   a pokračovat.
7. Po každém zastavení a 30 s klidu změřit výšku hladiny od vnějšího dna
   a poslat číslo.

## Kritéria (stanovená PŘEDEM)

| výsledek | význam |
|---|---|
| cyklus 1 bez lahvičky odmítnut | kontrola přítomnosti lahvičky funguje |
| všechny výšky 8–9,5 mm | zastavení nezávisí na náplni ani na startu |
| 6 ml odmítnuto kontrolou | start 6 ml je pod prahem – v iteraci hlídat zbytek + dávku |
| 6 ml přijato, výška < 8 mm | start za plató podcení maximum, podíl to nezachránil |
| `CHYBA maximum C3 mimo plato` | práh 0,80 pořád moc přísný / start na sestupu |

## V logu

Jako v `TEST10.md`: `# suche hodnoty`, `# start C3/C1=… nad suchou: …`,
`# aktivace … C3/C1 v maximu=…`, `# STOP …`, `#   C3/maxC3=… shoda OK`.

Překlad (`avr-g++`): flash 25 672 B (78,3 %), SRAM 668 B (32,6 %), bez varování.
