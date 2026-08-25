# Test 1 – profil signálu při vytlačování vzduchem

První měření nové série. Předchozí data vznikla **odsáváním kapaliny stříkačkou** —
tvrdý, přesný pohyb hladiny bez stlačitelného členu. Ukázalo se, že na
vytlačování **vzduchem** přenositelná nejsou, protože mezi motorem a hladinou
je stlačitelný sloupec a tlak působí na signál stejným řádem jako hladina sama.

Tenhle test má odpovědět na jedinou otázku:

> **Existuje při vytlačování vzduchem na CIN1 rozpoznatelný přechod přes
> kritickou hladinu, a jak je velký proti tlakovému artefaktu?**

Nic se nedetekuje. Žádný klouzavý průměr, žádný práh, žádná ochrana proti rušení,
žádné rozhodování. Jen syrová data. Vyhlazení, práh i vzorkovací frekvence se
vyberou až nad naměřenými daty.

---

## Průběh

Pět cyklů, polo-automaticky. V každém:

| # | krok | fáze v logu | doba |
|---|---|---|---|
| 1 | obsluha naplní penicilinku na 10 ml a potvrdí | — | ruční |
| 2 | ventily: vzduch `S↔V`, pacient `OPEN` | `v` | ~3,5 s |
| 3 | **klid při atmosférickém tlaku** — referenční bod | `k` | 5 s |
| 4 | **tlačení 20 ml vzduchu**, 1 ml / 5 s | `w` | 100 s |
| 5 | mezi tím 2× doplnění vzduchu | `a`, `e`, `v` | ~2× 15 s |
| 6 | klid po dojezdu, lahvička **pod tlakem** | `d` | 15 s |
| 7 | **odvzdušnění** + klid při atmosférickém tlaku | `o` | 8 s |
| 8 | obsluha potvrdí, že je lahvička prázdná | — | ruční |

Jeden cyklus tedy trvá zhruba 2,5 minuty čistého měření.

**20 ml vzduchu je záměrně přebytek** proti ~10 ml kapaliny. Cílem je vidět
i konec — jak vypadá signál, když už lahvičkou prochází jen vzduch. Bez toho
by se nedalo poznat, která část křivky je ještě hladina a která už ne.

### Proč se lahvička při doplnění vzduchu neodvzdušňuje

Dvacet ml se do 10ml stříkačky nevejde, takže se tlačení přeruší doplněním.
Firmware při něm lahvičku odvzdušní (`V↔F`), ale tady se jde přímo
`S↔V` → `S↔F` → `S↔V`: v poloze `S↔F` je rameno lahvičky zaslepené, takže
si lahvička **drží tlak** a přerušení nedělá do dat tlakový skok. Pacientský
ventil se během celého cyklu vůbec nehne.

Je to záměrná odchylka od firmwaru — cílem je vidět tvar křivky s co nejmenším
počtem artefaktů, ne věrně imitovat přístroj.

### Fáze `d` a `o` měří tlakový offset přímo

Rozdíl mezi ustálenou hodnotou v `d` (pod tlakem) a v `o` (po odvzdušnění) je
přesně to, co během tlačení přičítá tlak k signálu hladiny. Je to jediné místo
v celém cyklu, kde se ta veličina dá odečíst samostatně — a v každém cyklu
zvlášť, takže půjde vidět, jestli je stabilní.

Odvzdušnění zároveň uvede lahvičku do stavu, ve kterém ji lze bezpečně doplnit:
pacient uzavřen, `V↔F` otevřeno do atmosféry. Plnit lahvičku v poloze `S↔V` nelze —
rameno stříkačky je zaslepené pístem, který drží motor, takže by vytlačovaný
vzduch neměl kam uniknout.

---

## Formát logu

```
cyklus;t_ms;faze;vytlaceno_ml;C1;C2
```

- `t_ms` je **relativní k začátku cyklu** — cykly jsou tak přímo porovnatelné
- `vytlaceno_ml` je kumulativně vytlačený vzduch v rámci cyklu (přes doplnění)
- `C1`, `C2` v pF, **bez jakékoli filtrace**

Znaky fáze: `v` přejezd ventilu · `k` klid před tlačením · `w` **tlačení** ·
`a` nasávání vzduchu · `e` ustálení po přepnutí · `d` klid pod tlakem ·
`o` klid po odvzdušnění

Vzorkuje se **20× za sekundu** (`samplePeriodMs` = 50). FDC1004 běží na
400 S/s, takže při dvou kanálech je každý vzorek z čerstvě dokončené konverze.
Při 9600 Bd zabere log zhruba 700 B/s ze 960 B/s, což se vejde i s rezervou.

---

## Postup na hardwaru

1. Vzduchová stříkačka **plná** (10 ml). Roztoková se nepoužívá vůbec.
2. Ventily a vzduchová stříkačka osazené, penicilinka ve studni naplněná na 10 ml.
3. `t` — deklarace, že je vzduchová stříkačka plná.
4. `a` — autokalibrace CAPDAC (proběhne i v `setup()`).
5. `g` — spustit sérii. Dál už jen potvrzování mezi cykly.
6. `x` kdykoli — okamžité zastavení.

Zachycení logu:

```
tools/serial_log.py /dev/ttyUSB0 test1.csv
```

Skript zapisuje po řádcích a zároveň propouští, co napíšeš, na sériovou linku —
potvrzení mezi cykly se dají odesílat přímo z něj.

---

## Příkazy

| | |
|---|---|
| `h` `i` | nápověda, info |
| `a` | autoCAPDAC |
| `t` | deklarace plné vzduchové stříkačky (10 ml) |
| `g` `x` | start série / nouzové zastavení |
| `j` `k` | jog vzduchu ±0,5 ml (odvzdušnění hadičky) |
| `u0`–`u4` | pacient IZOLACE / OTEVŘENO, vzduch `S↔V` / `S↔F` / `V↔F` |
| `p<ms>` | perioda vzorku (výchozí 50) |
| `c<n>` | počet cyklů (výchozí 5) |
| `v<ml>` | vzduch na cyklus (výchozí 20) |
| `l<ml>` | vzduch na jednu náplň stříkačky (výchozí 7) |
| `s<s>` | sekund na 1 ml (výchozí 5) |
| `#<text>` | značka do logu |

`l` určuje, kolikrát se tlačení přeruší doplněním: při 7 ml to jsou dvě
přerušení (7 + 7 + 6). Rezerva 3 ml odpovídá `VOL_AIR_RESERVE_ML` z firmwaru,
takže píst nedojede na doraz. Kdo chce méně přerušení, může jít na `l10`
(2 náplně) — ale na vlastní riziko, píst pak dojíždí až na dno.

`s` je tam kvůli otázce, jestli 1 ml / 5 s není moc rychle. Pokud z dat vyjde,
že se hladina za signálem opožďuje, stačí `s10` a měření zopakovat.

---

## Kompilace

```
arduino-cli compile --fqbn arduino:avr:uno tools/air_push_profile
```

Poslední ověřený překlad: **Flash 14 406 B (44,7 %), SRAM 484 B (23,6 %)**,
bez varování.
