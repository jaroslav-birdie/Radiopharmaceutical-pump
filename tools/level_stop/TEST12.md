# Test 12 – podíl od začátku, kontrola lahvičky, 20ml lahvička

Sketch `level_stop.ino` (po startu `POSTUP TESTU 12`), studna **se stíněním**.
Rychlost tlačení beze změny: **1 ml / 5 s** (platí pro všechny další testy
i produkci).

## Změny proti testu 11 (podle analýzy se stíněním)

| | test 11 | test 12 | proč |
|---|---|---|---|
| podíl C3/C1 ≤ 0,68 | až po aktivaci | **od začátku tlačení** | u 6 ml čekal na aktivaci (0,673–0,685 před ní) |
| C3/C1 v maximu ≥ 0,80 | zastaví, konec série | **jen zápis** (`# SHODA ***`) | u 6 ml 0,814 – rezerva 0,014 |
| start C3/C1 | ≥ 0,80 | **≥ 0,70** | 6 ml 0,83–0,87, mezera 0,5–0,6 |
| lahvička | suchá + 0,5 pF | **suchá + 0,25 pF** | se stíněním plná lahvička jen +0,58 pF |
| max. doplnění vzduchu | 3 | **6** | 20ml lahvička potřebuje víc vzduchu |
| před sérií | suché hodnoty | + **prázdná suchá lahvička** (jen zápis) | podle ní se určí práh kontroly lahvičky |

Přehrání podílu od začátku na 20 cyklech testů 9–11: nikde předčasné
zastavení. Před aktivací byl podíl ≥ 0,72 všude kromě startů 6 ml, kde
už zastavit měl.

## Otázky

1. Kolik přidá **prázdná lahvička** (sklo) a kolik **plná 20ml lahvička**
   (hladina vysoko nad `SHLD2`) nad suchou hodnotu? Leží práh 0,25 pF mezi?
2. Odmítne kontrola start **bez lahvičky**?
3. Zastaví detekce správně i z **20 ml**?
4. Zastaví podíl u **6 ml** dřív než v testu 11 (už ne až s aktivací)?

## Postup

1. Nahrát `level_stop.ino`, `POSTUP TESTU 12`, log 115200 Bd. `m`, `t`,
   `i` (`(test 12)`, `cykly=5`).
2. `g`:
   - 1. výzva: **studna prázdná** → `# suche hodnoty: …`
   - 2. výzva: vložit **prázdnou suchou lahvičku** → `# prazdna lahvicka nad suchou: …`
3. **Cyklus 1:** lahvičku vyndat, naplnit na **20 ml**, vložit, potvrdit.
   > Kdyby kontrola hlásila `lahvicka chybi`, odeslat `x` a poslat log –
   > práh kontroly je pro 20 ml špatně a určí se z logu.
4. **Cyklus 2:** nejdřív potvrdit **bez lahvičky** → musí přijít
   `# *** lahvicka chybi nebo je prazdna, netlaci se ***`. Pak vložit
   lahvičku s **9–14 ml** a potvrdit.
5. **Cyklus 3:** **7 ml** (start jako v iteraci).
6. **Cykly 4 a 5:** **6 ml**.
7. Po každém zastavení a 30 s klidu změřit výšku hladiny od vnějšího dna
   a poslat číslo.

## Kritéria (stanovená PŘEDEM)

| výsledek | význam |
|---|---|
| prázdná lahvička < +0,25 pF, 20 ml > +0,25 pF | práh kontroly lahvičky v pořádku |
| 20 ml pod +0,25 pF nebo prázdná nad | kontrola lahvičky jen podle hodnoty nejde – řešit jinak |
| bez lahvičky odmítnuto | kontrola funguje |
| všechny výšky 8–9,5 mm | zastavení v pořádku i z 20 ml |
| u 6 ml `STOP podil` **před** řádkem `# aktivace` | podíl od začátku funguje |
| `# SHODA *** maximum C3 mimo plato` | jen informace, zastavení řeší podíl |

Překlad (`avr-g++`): flash 25 854 B (78,9 %), SRAM 668 B (32,6 %), bez varování.
