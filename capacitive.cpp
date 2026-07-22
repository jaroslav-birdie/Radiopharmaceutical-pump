#include "capacitive.h"
#include <Wire.h>

// Registry FDC1004
#define FDC_REG_MEAS1_MSB   0x00
#define FDC_REG_MEAS2_MSB   0x02
#define FDC_REG_CONF_MEAS1  0x08
#define FDC_REG_CONF_MEAS2  0x09
#define FDC_REG_FDC_CONF    0x0C
#define FDC_REG_DEVICE_ID   0xFF
#define FDC_DEVICE_ID       0x1004

// CONF_MEAS: CHA[15:13], CHB[12:10]; CHB=0b100 = CAPDAC (single-ended), CAPDAC=0
#define FDC_MEAS1_CIN1      0x1000    // CHA=CIN1
#define FDC_MEAS2_CIN2      0x3000    // CHA=CIN2
// FDC_CONF: RATE[11:10]=01 (100 S/s), REPEAT[8]=1, INIT_MEAS1[7]=1, INIT_MEAS2[6]=1
#define FDC_CONF_RUN        0x05C0

void CapSensor::writeReg(uint8_t reg, uint16_t value) {
    Wire.beginTransmission(FDC1004_ADDR);
    Wire.write(reg);
    Wire.write((uint8_t)(value >> 8));
    Wire.write((uint8_t)(value & 0xFF));
    Wire.endTransmission();
}

uint16_t CapSensor::readReg(uint8_t reg) {
    Wire.beginTransmission(FDC1004_ADDR);
    Wire.write(reg);
    Wire.endTransmission(false);
    if (Wire.requestFrom((uint8_t)FDC1004_ADDR, (uint8_t)2) != 2) {
        return 0;
    }
    uint16_t hi = Wire.read();
    return (hi << 8) | Wire.read();
}

// 24bitový výsledek: MSB registr (16 bitů) + horních 8 bitů LSB registru.
int32_t CapSensor::readMeasurement(uint8_t msbReg) {
    int16_t msb = (int16_t)readReg(msbReg);
    uint16_t lsb = readReg(msbReg + 1);
    return ((int32_t)msb << 8) | (lsb >> 8);
}

bool CapSensor::begin() {
    present_ = (readReg(FDC_REG_DEVICE_ID) == FDC_DEVICE_ID);
    writeReg(FDC_REG_CONF_MEAS1, FDC_MEAS1_CIN1);
    writeReg(FDC_REG_CONF_MEAS2, FDC_MEAS2_CIN2);
    writeReg(FDC_REG_FDC_CONF, FDC_CONF_RUN);
    return present_;
}

void CapSensor::update() {
    uint32_t now = millis();
    if (now - lastSample_ < CAP_SAMPLE_MS) {
        return;
    }
    lastSample_ = now;
    levelRaw_ = readMeasurement(FDC_REG_MEAS1_MSB);
    vertRaw_ = readMeasurement(FDC_REG_MEAS2_MSB);

    if (calibrating_) {
        calibSumLevel_ += (float)levelRaw_;
        calibSumVert_ += (float)vertRaw_;
        calibCount_++;
        if (now - calibStart_ >= CALIBRATION_MS && calibCount_ > 0) {
            levelBaseline_ = calibSumLevel_ / calibCount_;
            vertBaseline_ = calibSumVert_ / calibCount_;
            flowEpsRaw_ = (int32_t)(vertBaseline_ * CAP_FLOW_EPS_PERMILLE / 1000.0f);
            if (flowEpsRaw_ < 1) {
                flowEpsRaw_ = 1;
            }
            calibrating_ = false;
            calibrated_ = true;
        }
    }
}

void CapSensor::startCalibration() {
    calibSumLevel_ = 0.0f;
    calibSumVert_ = 0.0f;
    calibCount_ = 0;
    calibStart_ = millis();
    calibrating_ = true;
    calibrated_ = false;
}

// Kritická hladina: kapacita hladinové elektrody klesla o CAP_CRITICAL_PERCENT
// procent proti kalibrované plné lahvičce.
bool CapSensor::criticalLevel() const {
    if (!calibrated_) {
        return false;
    }
    float threshold = levelBaseline_ * (1.0f - (float)CAP_CRITICAL_PERCENT / 100.0f);
    return (float)levelRaw_ < threshold;
}
