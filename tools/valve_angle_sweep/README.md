# Ruční ověření geometrie vzduchového ventilu

Zjišťuje se, které úhly serva doopravdy odpovídají kterým dvěma spojeným
portům ventilu (`S` = vzduchová stříkačka, `V` = lahvička/dno, `F` = vzduchový
filtr/atmosféra) — protože hodnoty `AIR_VALVE_SYRINGE_TO_FILTER_DEFAULT` a
`AIR_VALVE_VIAL_TO_FILTER_DEFAULT` v `config.h` jsou zatím jen provizorní
odhady, ne ověřená fyzická kalibrace.

Sketch nic neměří ani nedetekuje — jen otáčí servem vzduchového ventilu
(D10) po 5° krocích a u každé polohy čeká na potvrzení obsluhou, aby byl čas
si polohu v klidu prohlédnout a zapsat.

---

## Postup

1. Vzduchová stříkačka a penicilinka nemusí být osazené — týká se to jen
   ventilu samotného. **Pacientská hadička nesmí být připojená** (viz níže).
2. `g` — spustí rozjezd: **CW 0° → 180°**, pak **CCW 180° → 0°**, 5° na krok.
3. V každé poloze se vypíše úhel a směr. Zapsat si, které 2 porty jsou
   spojené a který zaslepený, pak odeslat cokoli (Enter) pro pokračování —
   na zápis není žádný časový limit.
4. `x` kdykoli (i během čekání na potvrzení) = okamžité přerušení rozjezdu.
5. `a<úhel>` — přímý skok na konkrétní úhel 0–180, pro doladění přesné
   hranice mezi dvěma polohami, kterou jste během rozjezdu zaznamenali.

Oba směry (CW i CCW) slouží k odhalení vůle/hystereze v převodu serva —
pokud se hranice mezi polohami při najíždění zleva a zprava liší, ventil
má vůli, se kterou je potřeba počítat.

### Proč nesmí být připojená pacientská hadička

Pacientský ventil (D9) se nastaví jednou na začátku do izolační polohy
podle aktuální EEPROM (nebo výchozí hodnoty z `config.h`) a dál se v tomto
sketchi vůbec nepoužívá. Jelikož je ale otázkou právě to, jestli uložené
hodnoty odpovídají skutečné fyzické poloze, nelze se na tuto „izolaci"
zatím spolehnout — proto pacientská hadička při tomto testu nemá být
připojená vůbec.

---

## Kompilace

```
arduino-cli compile --fqbn arduino:avr:uno tools/valve_angle_sweep
```

Poslední ověřený překlad (`avr-g++`, `arduino-cli` v tomto prostředí není
dostupné): **Flash 8 538 B (26,1 %), SRAM 245 B (12,0 %)**, bez varování.
