# Živá detekce hran s pauzou na vizuální kontrolu (FDC1004)

Na rozdíl od `capacitive_sweep_test` a `capacitive_cycle_test` (které jen
**loguji** data pro pozdější zpracování) tenhle nástroj přímo **běží
navržený detekční algoritmus** — vyhlazení 25 vzorků + pokles od
**klouzavého okna** C1 + potvrzení přes víc vzorků — a motor se podle jeho
výstupu sám zastavuje. Cíl: ověřit, že algoritmus najde obě hrany kruhové
elektrody spolehlivě, s vizuální kontrolou po každé detekci.

> **Motor jen odsává, nikdy nedávkuje zpět.** Doplnění kapaliny mezi cykly
> provádí obsluha ručně (mimo řízení systému) — to je záměr testu: ověřit,
> že algoritmus nezávisí na přesné/známé počáteční hladině.

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

---

## Postup jednoho cyklu

1. **FÁZE 1** — motor odsává, dokud algoritmus nenajde **horní hranu**
   (vrchol C1, podle klouzavého okna). Pak se **sám zastaví** a vypne driver.
2. Obsluha **vytáhne lahvičku ze studny**, vizuálně zkontroluje stav.
3. Vrátí lahvičku zpět, potvrdí `y`.
4. **FÁZE 2** — motor odsává dál, dokud algoritmus nenajde **dolní
   (kritickou) hranu** (pokles od hodnoty zamčené ve fázi 1). Zastaví se,
   vypne driver.
5. Obsluha vytáhne lahvičku, zkontroluje hladinu, **ručně doplní nějaké
   množství kapaliny** (motor nedávkuje!), vrátí lahvičku zpět.
6. Potvrdí `y` → další cyklus (znovu FÁZE 1). Celkem `cycleCountTarget`×
   (výchozí 5).

Po posledním cyklu sketch vypíše souhrnnou tabulku všech detekovaných
hran a sekvence skončí.

## Proč nezávisí na počáteční hladině

Detekční algoritmus sleduje jen **tvar** signálu (vyhlazená hodnota C1
roste → vrchol → klesá), ne absolutní `level_ml`. Funguje tedy stejně,
ať se každý cyklus startuje kdekoliv v očekávaném rozmezí ~8–20 ml.
`level_ml` ve výstupu je čistě orientační krokové počítadlo **od
posledního `t`** (přes všechny cykly), NE skutečný obsah lahvičky —
ten po ručním doplnění nikdo nezná, a ani nepotřebujeme ho znát.

## Pozor na kapacitu stříkačky

Motor mezi cykly nikdy nedávkuje zpět (doplňuje se ručně přímo do
lahvičky) — odběr ze stříkačky se tedy **kumuluje přes všech 5 cyklů**.
Sketch před každým dalším cyklem zkontroluje, jestli zbývá dost zdvihu
(`maxSyringeMl`, výchozí 55 ml pro 60ml stříkačku), a pokud ne, **zastaví
se a vyzve k ručnímu doplnění STŘÍKAČKY** (ne lahvičky) a novému `t` —
místo aby motor dojel na mechanický doraz.

**Naplň stříkačku vydatně před startem** (ideálně na 55–60 ml), ať tahle
situace nenastane uprostřed měření.

---

## Postup

1. **Nahrát sketch** do Arduina.
2. **Stříkačku naplnit vydatně** (>= `maxSyringeMl`).
3. **Lahvičku naplnit na libovolnou hladinu ~8–20 ml**, vložit do studny.
4. **Serial Monitor**, 9600 Bd, zakončení Enter (nebo `tools/serial_log.py`
   — doporučeno, výstup může být dlouhý).
5. `o` — driver ON. Volitelně `j`/`k` — odvzdušnění.
6. `t` — tare (vynuluje krokové počítadlo).
7. `g` — **spustí celou sekvenci** (5 cyklů, FÁZE 1 začne hned).
8. **Řídit se pokyny na sériové lince** — sketch vždy napíše, co má
   obsluha udělat a kdy napsat `y`.
9. `x` kdykoliv za běhu = okamžité zastavení (nouzové), vypne driver a
   sekvenci ukončí.

## Parametry (nastavit před `g`, případně upravit i za běhu)

| Příkaz | Význam | Výchozí |
|---|---|---|
| `du<pF>` | δ_upper — pokles od okenního maxima = horní hrana | `du0.10` |
| `dc<pF>` | δ_critical — pokles od vrcholu = dolní/kritická hrana | `dc0.22` |
| `cf<n>` | kolik po sobě jdoucích vzorků musí práh držet (potvrzení) | `cf5` |
| `rw<ml>` | délka klouzavého okna pro referenční maximum (fáze 1) | `rw5.0` |
| `mw<ml>` | min. odběr před tím, než fáze 1 vůbec smí detekovat (auto >= `rw`) | `mw6.0` |
| `p<ms>` | perioda vzorku | `p200` |
| `m<ml>` | bezpečnostní strop na jednu fázi (kdyby se hrana nenašla) | `m18` |
| `sm<ml>` | max. kumulativní odběr ze stříkačky od `t` | `sm55` |
| `r<n>` | počet cyklů | `r5` |

Výchozí hodnoty `du`/`dc`/`p`/`rw`/`mw` vycházejí z analýzy 5 spojitých
cyklů (`tools/capacitive_cycle_test`) a ze dvou neúspěšných pokusů s
tímhle nástrojem — `dc=0.22` má tam naměřenou marži cca 3,5× nad
nejhorším pozorovaným šumem při běžícím motoru, `rw=5` pokrývá s rezervou
pozorovaný rozsah skutečného přechodu (~1,5–3,5 ml). `mw=6` (>= `rw`,
sketch to sám vynutí) zajišťuje, že okno stihne aspoň jednou "protočit"
dřív, než detekce vůbec smí proběhnout — bez toho se okno chová jako
staré neomezené maximum. `du=0.10` je kompromis — pořád může být moc
citlivé na přechodový jev hned po startu motoru (viz `v3` výše), sleduj
to při dalším běhu.

Pokud zmenšíš `p` (kratší perioda vzorku) natolik, že se `rw` nevejde do
interního bufferu (150 vzorků), sketch při startu fáze 1 vypíše varování
a tiše použije kratší efektivní okno — sleduj `i`/hlášky, ať o tom víš.

`i` kdykoliv vypíše aktuální hodnoty všech parametrů a stav sekvence.

## Formát výstupu

```
# cycle;t_ms;level_ml;faze;C1_raw;C1_smooth;ref;C2_raw;d1;d2
# --- CYKLUS 1 / FAZE 1: hledani HORNI HRANY ---
# ustaleni (motor stoji, cca 5 s)...
1;0;0.00;p1;3.7702;3.7702;3.7702;2.4124;0.0000;0.0000
...
# *** HORNI HRANA DETEKOVANA *** cyklus=1 level(od tare)=-12.40 ml C1_vrchol(okno)=3.9560 C1_ted=3.8760
# Vytahni lahvicku ze studny, zkontroluj stav.
# Az bude lahvicka zpet ve studni, potvrd 'y' -> FAZE 2.
...
# *** DOLNI (KRITICKA) HRANA DETEKOVANA *** cyklus=1 level(od tare)=-15.10 ml C1_ted=3.7300 pokles_od_vrcholu=0.2260
# RUCNE doplň nejake mnozstvi kapaliny do lahvicky
# (motor NEDAVKUJE - doplnujes mimo system), vrat zpet.
# Az bude lahvicka zpet, potvrd 'y' -> dalsi cyklus.
...
# === SOUHRN VSECH CYKLU ===
# cyklus;horni_level_ml;horni_C1;dolni_level_ml;dolni_C1;pokles_pF
1;-12.40;3.9560;-15.10;3.7300;0.2260
...
# === SEKVENCE HOTOVA ===
```

- `level_ml` je vždy vzhledem k poslednímu `t` — pro srovnání jednotlivých
  cyklů mezi sebou je potřeba je zarovnat podle vlastního vrcholu (stejně
  jako v analýze `capacitive_cycle_test`), ne podle tohohle sloupce přímo.
- `C1_smooth` je hodnota, na které je detekce postavená — pro kontrolu
  algoritmu je užitečnější než `C1_raw`.
- `ref` je aktuální referenční hodnota (klouzavé okenní maximum ve fázi 1,
  zamčený vrchol ve fázi 2) — sleduj, jestli `C1_smooth - ref` dává smysl.
- Po zkopírování celého výstupu pošli k analýze — hlavně mě zajímá, jestli
  se okamžik `*** ... DETEKOVANA ***` shoduje s tím, co jsi v tu chvíli
  viděl/a při vizuální kontrole.
