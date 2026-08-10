# Živá detekce hran s pauzou na vizuální kontrolu (FDC1004)

Na rozdíl od `capacitive_sweep_test` a `capacitive_cycle_test` (které jen
**loguji** data pro pozdější zpracování) tenhle nástroj přímo **běží
navržený detekční algoritmus** — vyhlazení 25 vzorků + pokles od
průběžného maxima C1 + potvrzení přes víc vzorků — a motor se podle jeho
výstupu sám zastavuje. Cíl: ověřit, že algoritmus najde obě hrany kruhové
elektrody spolehlivě, s vizuální kontrolou po každé detekci.

> **Motor jen odsává, nikdy nedávkuje zpět.** Doplnění kapaliny mezi cykly
> provádí obsluha ručně (mimo řízení systému) — to je záměr testu: ověřit,
> že algoritmus nezávisí na přesné/známé počáteční hladině.

---

## Postup jednoho cyklu

1. **FÁZE 1** — motor odsává, dokud algoritmus nenajde **horní hranu**
   (vrchol C1). Pak se **sám zastaví** a vypne driver.
2. Obsluha **vytáhne lahvičku ze studny**, vizuálně zkontroluje stav.
3. Vrátí lahvičku zpět, potvrdí `y`.
4. **FÁZE 2** — motor odsává dál, dokud algoritmus nenajde **dolní
   (kritickou) hranu**. Zastaví se, vypne driver.
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
| `du<pF>` | δ_upper — pokles od maxima = horní hrana | `du0.08` |
| `dc<pF>` | δ_critical — pokles od vrcholu = dolní/kritická hrana | `dc0.22` |
| `cf<n>` | kolik po sobě jdoucích vzorků musí práh držet (potvrzení) | `cf5` |
| `p<ms>` | perioda vzorku | `p200` |
| `m<ml>` | bezpečnostní strop na jednu fázi (kdyby se hrana nenašla) | `m18` |
| `sm<ml>` | max. kumulativní odběr ze stříkačky od `t` | `sm55` |
| `r<n>` | počet cyklů | `r5` |

Výchozí hodnoty `du`/`dc`/`p` vycházejí přímo z analýzy 5 spojitých cyklů
(`tools/capacitive_cycle_test`) — `dc=0.22` má tam naměřenou marži cca
3,5× nad nejhorším pozorovaným šumem při běžícím motoru s tímhle
vyhlazením. `du=0.08` je opatrnější odhad (menší marže, horní hrana je
sekundární cíl) — pokud se při testu ukáže, že spouští moc brzy/pozdě,
doladit a spustit znovu.

`i` kdykoliv vypíše aktuální hodnoty všech parametrů a stav sekvence.

## Formát výstupu

```
# cycle;t_ms;level_ml;faze;C1_raw;C1_smooth;C2_raw;d1;d2
# --- CYKLUS 1 / FAZE 1: hledani HORNI HRANY ---
# ustaleni (motor stoji, cca 5 s)...
1;0;0.00;p1;3.7702;3.7702;2.4124;0.0000;0.0000
...
# *** HORNI HRANA DETEKOVANA *** cyklus=1 level(od tare)=-12.40 ml C1_vrchol=3.9560 C1_ted=3.8760
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
- Po zkopírování celého výstupu pošli k analýze — hlavně mě zajímá, jestli
  se okamžik `*** ... DETEKOVANA ***` shoduje s tím, co jsi v tu chvíli
  viděl/a při vizuální kontrole.
