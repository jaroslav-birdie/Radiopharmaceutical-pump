# Test 3 – pomohl druhý prstenec?

Stejný sketch jako test 1 (`air_push_profile.ino`), nově se třemi kanály:

| kanál | elektroda |
|---|---|
| C1 | **spodní** prstenec (2 mm) |
| C2 | svislá elektroda (končí 4 mm pod spodním prstencem) |
| C3 | **horní** prstenec (2 mm), mezera 4 mm nad C1 |

Nic se nedetekuje, logují se syrová data. Rozdíl `C1 − C3` se počítá až při
vyhodnocení.

---

## Otázka

> **Dává rozdílový signál dvou prstenců ostrý a opakovatelný příznak
> průchodu hladiny, méně zatížený tlakem než samotné C1?**

Pokud ano, dá se kritická hladina hledat bez kalibrace kapacita↔objem.
Pokud ne, druhý prstenec nepomohl a je potřeba jiná cesta.

---

## Průběh (5 cyklů)

| # | krok | fáze v logu |
|---|---|---|
| 1 | obsluha **ručně naplní penicilinku nad 10 ml** (klinická situace) a pošle naplněný objem, např. `12` – zapíše se do logu jako `# obsluha: 12` | — |
| 2 | ventily: vzduch `S↔V`, pacient `OPEN` | `v` |
| 3 | klid při atmosférickém tlaku, obě elektrody pod hladinou | `k` |
| 4 | **tlačení 20 ml vzduchu**, 1 ml / 5 s; doplnění stříkačky při 5 a 15 ml (pacient při něm uzavřen) | `w`, `a`, `e`, `v` |
| 5 | klid po dojezdu, pod tlakem | `d` |
| 6 | odvzdušnění, klid při atmosférickém tlaku | `o` |
| 7 | doplnění vzduchové stříkačky na 10 ml pro další cyklus | `a`, `v` |
| 8 | obsluha potvrdí prázdnou lahvičku | — |

20 ml vzduchu spolehlivě vytlačí veškerou kapalinu. Od určitého okamžiku
v lahvičce nic není a **signál musí být stacionární** – ten úsek je
referenční „prázdno" každého cyklu.

### Proč první dávka 5 ml (`f5`)

Stříkačka má 10 ml, takže 20 ml nejde protlačit bez přerušení. Výchozí
`f5` dá doplnění při **5 a 15 ml** a mezi nimi souvislý úsek **5–15 ml**.
V testu 1 (náplň 10 ml) ležel přechod C1 na 7–14 ml; s náplní nad 10 ml
se posune později, horní prstenec ho naopak o něco předběhne. Úsek 5–15 ml
by měl pokrýt průchod hladiny **oběma** prstenci bez přerušení.

Pokud je náplň výrazně vyšší než ~12 ml, posunout první dávku o přebytek
(např. náplň 14 ml → `f7`). Pro porovnatelnost **držet náplň i `f` ve všech
pěti cyklech stejné**.

### Stříkačka se doplňuje na konci každého cyklu

Dřív se zbytek stříkačky přenášel do dalšího cyklu a u `l10` to v cyklech
2–5 vyrobilo navíc jedno doplnění hned na začátku. Teď každý cyklus začíná
s plnou stříkačkou, takže jsou všechny cykly stejné.

---

## Penicilinka není stíněná – na co dát pozor

Pro tenhle test není studna v Faradayově kleci. Návrat proudu přes vodu je
pak nedefinovaný, vede přes okolí (viz `CLAUDE.md`, varianta B), a signál
reaguje na to, **co je zrovna kolem**. Aby se to nepletlo s hladinou:

- **Během cyklu nesahat do blízkosti studny a nehýbat se u ní.** Ruka
  v řádu decimetrů změní C1/C3 o víc než celý hledaný příznak. Ruce mimo
  od potvrzení náplně až po fázi `o`.
- **Kabely a okolí neměnit mezi cykly.** Koaxy fixovat, nic nepřestavovat,
  lahvičku vkládat vždy stejně (stejné natočení).
- **Stejné prostředí po celou sérii** – kdo stojí kde, notebook, napájení.
- Pokud k rušení dojde (někdo prošel, sáhlo se na kabel), zapsat to hned
  po cyklu při potvrzení prázdné lahvičky, např. `prazdna, v 60 s ruka`.

Nestíněné uspořádání má i výhodu: vnější rušení působí na oba prstence
skoro stejně, takže je to přímo test, **jestli ho rozdíl `C1 − C3` odečte**.
Kdyby se to nepotvrdilo ani bez klece, s klecí to bude jen lepší; obráceně
to neplatí.

---

## Kritéria vyhodnocení (stanovená PŘEDEM)

Druhý prstenec **pomohl**, pokud platí 1–3. Bod 4 je doplňkový.

1. **Ostrost.** Šířka přechodu (10–90 % mezi klidem a stacionárním
   prázdnem) v ml vytlačeného vzduchu. Pro C1 a C3 zvlášť a pro příznak
   v `C1 − C3`. Rozdílový příznak musí být **znatelně užší** než samotná
   rampa C1 (v testu 1 asi 7 ml).
2. **Opakovatelnost bez kalibrace.** Vzdálenost (v ml vytlačeného vzduchu)
   mezi průchodem hladiny horním a spodním prstencem je daná geometrií
   (6 mm mezi středy) a nesmí záviset na náplni. **Rozptyl této vzdálenosti
   přes 5 cyklů** ukazuje, jak přesně se dá kritická hladina určit čistě
   z geometrie. Poloha příznaku vztažená k začátku stacionárního prázdna
   musí mít malý rozptyl i při různé náplni.
3. **Potlačení tlaku.** C1 v testu 1 během 0–6 ml stoupalo o +0,13 až
   +0,19 pF (tlakový artefakt) a rozdíl fází `d` a `o` měří tlakový offset
   přímo. V `C1 − C3` musí být oba jevy **výrazně menší** než v C1.
4. **Korelace šumu.** Korelace detrendovaných rychlých složek C1 a C3
   (v `k`, `d`, `o` a v prázdnu). U C1/C2 vycházela −0,49 až +0,24
   a měnila znaménko; u dvou stejných prstenců čekáme **stabilně vysoké
   kladné** hodnoty. Bez toho rozdíl šum nepotlačí, ale naopak sečte.

Kontrola zapojení před sérií: po `a` musí být CAPDAC1–3 nízké (jednotky).

---

## Postup

1. Vzduchová stříkačka **plná** (10 ml), ventily a stříkačka osazené.
2. Penicilinka ve studni.
3. Zachytávání logu – **pozor, 115200 Bd**:
   ```
   tools/serial_log.py /dev/ttyUSB0 test3.csv --baud 115200
   ```
4. `t` (plná stříkačka), `a` (CAPDAC), zkontrolovat `i`.
5. `g` – start. Na výzvu naplnit nad 10 ml a poslat objem, po cyklu potvrdit
   prázdnou lahvičku.

Log poslat jako **soubor**, ne vložený text.
