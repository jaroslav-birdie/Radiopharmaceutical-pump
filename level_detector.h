#pragma once
#include <stdint.h>
#include "config.h"

// Detekce kritické hladiny ze dvou prstenců (C1 spodní, C3 horní).
// Přepis ze sketche tools/level_stop (testy 9-12: hladina po zastavení
// 8,65 +- 0,23 mm, zbytek ~4,1 ml). Čistá logika bez I/O, bez času,
// bez rychlosti a bez objemu vzduchu - jen porovnání hodnot signálu.
// Stejný soubor se překládá i na PC a přehrávají se přes něj logy testů.
//
// Vzorky se předávají JEN s otevřeným pacientem (feed). Když se pacient
// zavře (doplnění vzduchu, pauza s uzavřením), volá se flowBreak():
// zahodí se rozpracovaný koš a porovnání pro skok, okno a maxima zůstávají.
//
// Zastaví PRVNÍ z:
//   podíl    C3/C1 (okno) <= LVL_STOP_Q_MAX          - od začátku tlačení
//   šablona  C3 <= LVL_STOP_C3_FRAC x vlastní maximum - po aktivaci
//   vrchol   C1 - C3 kleslo o LVL_PEAK_DROP výšky hrbu (záloha)
//   skok     koš C1 o LVL_STEP_DROP pod větším ze 2 předchozích = porucha
//            (jen po aktivaci - před ní ho spouští otevření ventilu)
// Aktivace: C3 (okno) <= LVL_ARM_FRAC x vlastní maximum.

enum LevelResult : uint8_t {
    LVL_NONE = 0,
    LVL_STOP_RATIO,          // podíl C3/C1
    LVL_STOP_TEMPLATE,       // šablona C3 / max C3
    LVL_STOP_PEAK,           // záloha: vrchol C1 - C3
    LVL_FAULT_STEP           // skok C1 - poslední záchrana, porucha
};

// Bity kontroly shody se šablonou (jen informace, nezastavují)
#define LVL_FIT_C1_EARLY     0x01   // C1 klesá dřív, než C3 dosáhlo LVL_FIT_C3_ZONE
#define LVL_FIT_NO_RISE      0x02   // C1 - C3 nevzrostlo, ač C3 kleslo
#define LVL_FIT_MAX_OFF      0x08   // maximum C3 nebylo z plató (C3/C1 v maximu nízko)

class LevelDetector {
public:
    void reset();                        // nové tlačení (Fáze 1 / iterace)
    void flowBreak();                    // pacient zavřen - přerušení toku
    LevelResult feed(float c1, float c3);   // vzorek s otevřeným pacientem

    uint8_t fitFlags() const { return fitMask_ & (LVL_FIT_C1_EARLY | LVL_FIT_NO_RISE | LVL_FIT_MAX_OFF); }
    bool  armed() const { return armed_; }
    float winC1() const { return w1_; }       // okno při posledním vyhodnocení
    float winC3() const { return w3_; }
    float maxC3() const { return c3Max_; }
    float qAtMax() const { return qAtMax_; }

private:
    LevelResult bin(float c1, float c3);
    LevelResult window();
    void checkFit(float d);

    float    accC1_ = 0.0f, accC3_ = 0.0f;
    uint8_t  accN_ = 0;
    float    binC1_[LVL_WIN_BINS];
    float    binC3_[LVL_WIN_BINS];
    uint8_t  binHead_ = 0, binCount_ = 0;
    float    prevBinC1_ = -1.0f, prev2BinC1_ = -1.0f;
    bool     armed_ = false, haveD0_ = false;
    float    d0_ = 0.0f, dMax_ = -1e9f;
    float    c1Max_ = -1e9f, c3Max_ = -1e9f, qAtMax_ = 0.0f;
    float    w1_ = 0.0f, w3_ = 0.0f;
    uint8_t  fitMask_ = 0;               // bit 0x04 = kontrola nárůstu provedena
    bool     done_ = false;              // výsledek už vydán
};
