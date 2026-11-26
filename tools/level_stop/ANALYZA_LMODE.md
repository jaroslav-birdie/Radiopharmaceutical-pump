# Detekce nad příspěvkem kapaliny (26. 11. 2026)

## Proč

Zařízení 26. 11. 13:21 (roztok s inkoustem): signál kapaliny v klidu asi
poloviční (+0,78 / +0,68 pF nad suchou místo +1,72 / +1,57), Fáze 1
nezastavila a vytlačila všechno. Kritéria pracovala s hrubými pF
`C = suchá + L`. Suchý základ kabelu a modulu se se ziskem nemění, takže
při slabším L se podíl C3/C1 posouvá k poměru suchých hodnot (nové
prstence 1,95 / 1,48 = 1,32) a šablona C3/max klesá pomaleji.

## Přehrání testů 10–12 (20 cyklů se suchou hodnotou a změřenou výškou)

Skripty `analyza/lmode/` (`det.py` = port `level_detector.cpp`).
Test 9 suchou hodnotu v logu nemá, nepoužit.

| detektor | zisk L ×1 | ×0,7 | ×0,5 | ×2 |
|---|---|---|---|---|
| hrubé pF (0,68 / 0,78) | 20/20, 8,63 mm | **0/20 nezastaví** | **0/20** | – |
| L (0,52 / 0,65, skok 25 %) | 20/20 | – | 20/20, stejný vzorek | 20/20, stejný vzorek |

- L3/L1 v okamžiku ověřeného zastavení: **0,459–0,515**; před aktivací
  ≥ 0,57; v klidu na startu 0,70–1,07; v maximu L3 0,68–0,95.
- Zastavení L-detektorem 0–2,4 s toku dřív než ověřené → odhad hladiny
  **8,88 ± 0,26 mm** (odhad z rychlosti 0,22 mm/s, jen pro posouzení).
- Skok 15 % na L dal 1 falešný zásah (test 11 c2) → 25 %.
- Kontroly průběhu (C1 předčasně, nárůst rozdílu ≥ 0,15) bez falešného
  varování; nárůst 0,24–0,43.

## Otevřené

Inkoustový běh, iterace 1: po 4,25 ml (hladina by měla být v mezeře)
L3/L1 = 0,865 → start by prošel i v L. To není změna zisku: horní
prstenec viděl kapaliny víc, než odpovídá objemu (usazení lahvičky?
vliv inkoustu na rozložení pole?). Bez surových vzorků z produkce se
to přehrát nedá.
