#pragma once
#include <Arduino.h>
#include "config.h"

// Vlastní minimální driver FDC1004 (I2C, adresa 0x50).
// Kanál CIN1 (MEAS1) = hladinová elektroda nad kritickou hladinou.
// Kanál CIN2 (MEAS2) = svislá elektroda sledující pokles hladiny.
// Senzor běží v režimu REPEAT (100 vzorků/s), čte se každých CAP_SAMPLE_MS.
// Žádná externí knihovna – jen Wire.

class CapSensor {
public:
    bool begin();                        // true = senzor odpověděl (ID 0x1004)
    void update();                       // periodické čtení výsledků

    void startCalibration();             // průměrování po dobu CALIBRATION_MS
    bool calibrating() const { return calibrating_; }
    bool calibrated() const { return calibrated_; }

    int32_t levelRaw() const { return levelRaw_; }
    int32_t vertRaw() const { return vertRaw_; }
    int32_t flowEpsRaw() const { return flowEpsRaw_; }   // práh "hladina klesá"

    bool criticalLevel() const;          // pokles hladinové elektrody >= CAP_CRITICAL_PERCENT

private:
    void     writeReg(uint8_t reg, uint16_t value);
    uint16_t readReg(uint8_t reg);
    int32_t  readMeasurement(uint8_t msbReg);

    int32_t  levelRaw_ = 0;
    int32_t  vertRaw_ = 0;
    float    levelBaseline_ = 0.0f;
    float    vertBaseline_ = 0.0f;
    int32_t  flowEpsRaw_ = 0;
    float    calibSumLevel_ = 0.0f;
    float    calibSumVert_ = 0.0f;
    uint16_t calibCount_ = 0;
    uint32_t calibStart_ = 0;
    uint32_t lastSample_ = 0;
    bool     present_ = false;
    bool     calibrating_ = false;
    bool     calibrated_ = false;
};
