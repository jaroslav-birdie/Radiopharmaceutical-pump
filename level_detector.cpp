#include "level_detector.h"

#define LVL_FIT_CHECKED      0x04   // interní: kontrola nárůstu už proběhla

void LevelDetector::reset() {
    accC1_ = accC3_ = 0.0f;
    accN_ = 0;
    binHead_ = binCount_ = 0;
    prevBinC1_ = prev2BinC1_ = -1.0f;
    armed_ = haveD0_ = false;
    d0_ = 0.0f;
    dMax_ = c1Max_ = c3Max_ = -1e9f;
    qAtMax_ = w1_ = w3_ = 0.0f;
    fitMask_ = 0;
    done_ = false;
}

// Přerušení toku (pacient zavřen): rozpracovaný koš a porovnání pro skok
// se zahodí - přepnutí ventilu dělá skoky. Okno a maxima zůstávají,
// pauza ani doplnění vzduchu stav hledání nezmění.
void LevelDetector::flowBreak() {
    accC1_ = accC3_ = 0.0f;
    accN_ = 0;
    prevBinC1_ = prev2BinC1_ = -1.0f;
}

LevelResult LevelDetector::feed(float c1, float c3) {
    if (done_) {
        return LVL_NONE;
    }
    accC1_ += c1;
    accC3_ += c3;
    accN_++;
    if (accN_ < LVL_BIN_SAMPLES) {
        return LVL_NONE;
    }
    LevelResult r = bin(accC1_ / accN_, accC3_ / accN_);
    accC1_ = accC3_ = 0.0f;
    accN_ = 0;
    if (r != LVL_NONE) {
        done_ = true;
    }
    return r;
}

// Hotový koš: nejdřív skok C1 proti většímu ze dvou předchozích košů
// (skok se často rozdělí do dvou košů), pak klouzavé okno.
LevelResult LevelDetector::bin(float c1, float c3) {
    float ref = (prevBinC1_ > prev2BinC1_) ? prevBinC1_ : prev2BinC1_;
    if (prev2BinC1_ > 0.0f && c1 < (1.0f - LVL_STEP_DROP) * ref) {
        w1_ = c1;
        w3_ = c3;
        return LVL_FAULT_STEP;
    }
    prev2BinC1_ = prevBinC1_;
    prevBinC1_ = c1;
    binC1_[binHead_] = c1;
    binC3_[binHead_] = c3;
    binHead_ = (binHead_ + 1) % LVL_WIN_BINS;
    if (binCount_ < LVL_WIN_BINS) {
        binCount_++;
    }
    return (binCount_ == LVL_WIN_BINS) ? window() : LVL_NONE;
}

// Okno přes posledních LVL_WIN_BINS košů. Všechna kritéria jsou poměrová -
// nezávisí na společném zisku prstenců (samolepka, okolí, stínění).
LevelResult LevelDetector::window() {
    float s1 = 0.0f, s3 = 0.0f;
    for (uint8_t i = 0; i < LVL_WIN_BINS; i++) {
        s1 += binC1_[i];
        s3 += binC3_[i];
    }
    w1_ = s1 / LVL_WIN_BINS;
    w3_ = s3 / LVL_WIN_BINS;
    float d = w1_ - w3_;
    if (!haveD0_) {
        d0_ = d;
        haveD0_ = true;
    }
    if (w3_ > c3Max_) {
        c3Max_ = w3_;
        qAtMax_ = w3_ / w1_;
    }
    if (w1_ > c1Max_) {
        c1Max_ = w1_;
    }
    if (!armed_ && w3_ <= LVL_ARM_FRAC * c3Max_) {
        // Maximum už je za námi (C3 kleslo o 15 %). Z plató je, jen když
        // byl horní prstenec zakrytý stejně jako spodní - jen zápis.
        armed_ = true;
        if (qAtMax_ < LVL_MAX_Q_MIN) {
            fitMask_ |= LVL_FIT_MAX_OFF;
        }
    }
    // Podíl na maximu nezávisí - hlídá se od začátku tlačení (test 11/12).
    if (w3_ <= LVL_STOP_Q_MAX * w1_) {
        return LVL_STOP_RATIO;
    }
    if (!armed_) {
        return LVL_NONE;
    }
    if (d > dMax_) {
        dMax_ = d;
    }
    checkFit(d);
    if (w3_ <= LVL_STOP_C3_FRAC * c3Max_) {
        return LVL_STOP_TEMPLATE;
    }
    if (d < dMax_ - LVL_PEAK_DROP * (dMax_ - d0_)) {
        return LVL_STOP_PEAK;
    }
    return LVL_NONE;
}

// Kontrola shody se šablonou (jen zápis): C1 nesmí klesat, dokud je C3
// vysoko, a rozdíl musí růst, když C3 klesá.
void LevelDetector::checkFit(float d) {
    if (w3_ > LVL_FIT_C3_ZONE * c3Max_ && w1_ < LVL_FIT_C1_MIN * c1Max_) {
        fitMask_ |= LVL_FIT_C1_EARLY;
    }
    if (!(fitMask_ & LVL_FIT_CHECKED) && w3_ <= LVL_FIT_CHECK_C3 * c3Max_) {
        fitMask_ |= LVL_FIT_CHECKED;
        if (d - d0_ < LVL_FIT_RISE_MIN * c3Max_) {
            fitMask_ |= LVL_FIT_NO_RISE;
        }
    }
}
