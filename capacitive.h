#pragma once
#include <Arduino.h>
#include "config.h"

// Vlastní minimální driver FDC1004 (I2C, adresa 0x50), jen Wire.
// Dva kanály: MEAS1 = spodní prstenec (C1), MEAS2 = horní prstenec (C3),
// vstupy CIN podle CH_*_CIN. Svislá elektroda je od 26. 11. odebrána.
// Hodnoty v pF (raw / 2^19 + CAPDAC x 3,125) – podíly C3/C1 v detekci
// potřebují absolutní hodnotu. REPEAT 400 S/s, čte se každých CAP_SAMPLE_MS.
//
// Konfigurace se zapisuje JEN se zastaveným měřením (FDC_CONF = 0, ~5 ms,
// zápis CONF_MEASx, pak REPEAT) – zápis do běžícího převodu čip zasekne
// (potvrzeno testem 6, viz CLAUDE.md).

class CapSensor {
public:
    bool begin();                        // reset, autokalibrace CAPDAC, start měření
    void update();                       // periodické čtení výsledků

    // Kalibrace = suché hodnoty prstenců v PRÁZDNÉ studně (lahvička se
    // vkládá až po kalibraci). Slouží ke kontrole, že lahvička je na místě.
    void startCalibration();
    bool calibrating() const { return calibrating_; }
    bool calibrated() const { return calibrated_; }
    float dryLower() const { return dryLower_; }
    float dryUpper() const { return dryUpper_; }

    bool  takeSample();                  // true = od posledního volání nový vzorek
    float lowerPf() const { return pf_[0]; }     // C1 spodní prstenec
    float upperPf() const { return pf_[1]; }     // C3 horní prstenec
    bool  frozen() const { return frozen_; }     // C1 nebo C3 se přestal měnit
    bool  present() const { return present_; }

private:
    void     writeReg(uint8_t reg, uint16_t value);
    uint16_t readReg(uint8_t reg);
    int32_t  readRaw(uint8_t meas);      // meas = 0..1 (MEAS1..2)
    void     applyConfig();
    void     autoCapdac(uint8_t meas);
    void     watchFrozen();

    uint8_t  capdac_[2] = {0, 0};
    float    pf_[2] = {0.0f, 0.0f};    // [0] = C1 spodní, [1] = C3 horní
    float    prevPf_[2] = {0.0f, 0.0f};
    uint8_t  same_[2] = {0, 0};
    float    dryLower_ = 0.0f;
    float    dryUpper_ = 0.0f;
    float    calSumLower_ = 0.0f;
    float    calSumUpper_ = 0.0f;
    uint16_t calCount_ = 0;
    uint32_t calStart_ = 0;
    uint32_t lastSample_ = 0;
    bool     newSample_ = false;
    bool     present_ = false;
    bool     frozen_ = false;
    bool     calibrating_ = false;
    bool     calibrated_ = false;
};
