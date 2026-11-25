#include "capacitive.h"
#include <Wire.h>

// Registry FDC1004
#define FDC_REG_MEAS1_MSB   0x00
#define FDC_REG_CONF_MEAS1  0x08
#define FDC_REG_FDC_CONF    0x0C
#define FDC_REG_DEVICE_ID   0xFF
#define FDC_DEVICE_ID       0x1004
#define FDC_CONF_RESET      0x8000    // FDC_CONF bit 15 = softwarový reset
// CONF_MEASx: CHA[15:13] = CINx, CHB[12:10] = 0b100 (CAPDAC), CAPDAC[9:5]
#define FDC_CHB_CAPDAC      0x4
// FDC_CONF: RATE[11:10] = 11 (400 S/s), REPEAT[8] = 1, MEAS_1..3[7:5] = 1
#define FDC_CONF_RUN        ((3 << 10) | (1 << 8) | (7 << 5))

// Index elektrody (0 = C1 spodní, 1 = C2 svislá, 2 = C3 horní) -> vstup CIN (0..2)
static const uint8_t ELECTRODE_CIN[3] = {
    CH_LOWER_CIN - 1, CH_VERT_CIN - 1, CH_UPPER_CIN - 1
};

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
int32_t CapSensor::readRaw(uint8_t cin) {
    int16_t msb = (int16_t)readReg(FDC_REG_MEAS1_MSB + cin * 2);
    uint16_t lsb = readReg(FDC_REG_MEAS1_MSB + cin * 2 + 1);
    return ((int32_t)msb << 8) | (lsb >> 8);
}

// Zápis konfigurace JEN se zastaveným měřením (jinak se čip zasekne).
// Volá se jen z begin() - blokující čekání je tu přípustné.
void CapSensor::applyConfig() {
    writeReg(FDC_REG_FDC_CONF, 0);
    delay(FDC_STOP_MS);
    for (uint8_t i = 0; i < 3; i++) {
        writeReg(FDC_REG_CONF_MEAS1 + i, ((uint16_t)i << 13)
                 | ((uint16_t)FDC_CHB_CAPDAC << 10) | ((uint16_t)capdac_[i] << 5));
    }
    writeReg(FDC_REG_FDC_CONF, FDC_CONF_RUN);
    delay(FDC_START_MS);
}

// CAPDAC zvyšovat, dokud výsledek není pod horní hranicí rozsahu.
// Správně zapojené stínění dává nízké hodnoty (jednotky).
void CapSensor::autoCapdac(uint8_t cin) {
    capdac_[cin] = 0;
    applyConfig();
    while (capdac_[cin] < CAPDAC_MAX && readRaw(cin) >= FDC_RAW_NEAR_FULL) {
        capdac_[cin]++;
        applyConfig();
    }
}

bool CapSensor::begin() {
    present_ = (readReg(FDC_REG_DEVICE_ID) == FDC_DEVICE_ID);
    if (!present_) {
        return false;
    }
    writeReg(FDC_REG_FDC_CONF, FDC_CONF_RESET);
    delay(FDC_RESET_MS);
    for (uint8_t i = 0; i < 3; i++) {
        autoCapdac(i);
    }
    uint16_t conf = readReg(FDC_REG_FDC_CONF) & 0x0DE0;
    present_ = (conf == (FDC_CONF_RUN & 0x0DE0));
    return present_;
}

// Zamrzlý převodník: N bitově stejných vzorků za sebou na C1 nebo C3
// (svislá elektroda se nehlídá - její signál se nevyhodnocuje).
void CapSensor::watchFrozen() {
    for (uint8_t e = 0; e < 3; e += 2) {
        same_[e] = (pf_[e] == prevPf_[e]) ? same_[e] + 1 : 0;
        prevPf_[e] = pf_[e];
        if (same_[e] >= FDC_FROZEN_LIMIT) {
            frozen_ = true;
        }
    }
}

void CapSensor::update() {
    uint32_t now = millis();
    if (!present_ || now - lastSample_ < CAP_SAMPLE_MS) {
        return;
    }
    lastSample_ = now;
    for (uint8_t e = 0; e < 3; e++) {
        uint8_t cin = ELECTRODE_CIN[e];
        int32_t raw = readRaw(cin);
        pf_[e] = raw / FDC_RAW_PER_PF + capdac_[cin] * CAPDAC_STEP_PF;
        if (e == 1) {
            vertRaw_ = raw;
        }
    }
    watchFrozen();
    newSample_ = true;

    if (calibrating_) {
        calSumLower_ += pf_[0];
        calSumUpper_ += pf_[2];
        calSumVert_ += (float)vertRaw_;
        calCount_++;
        if (now - calStart_ >= CALIBRATION_MS && calCount_ > 0) {
            dryLower_ = calSumLower_ / calCount_;
            dryUpper_ = calSumUpper_ / calCount_;
            flowEpsRaw_ = (int32_t)(calSumVert_ / calCount_ * CAP_FLOW_EPS_PERMILLE / 1000.0f);
            if (flowEpsRaw_ < 1) {
                flowEpsRaw_ = 1;
            }
            calibrating_ = false;
            calibrated_ = true;
        }
    }
}

bool CapSensor::takeSample() {
    bool n = newSample_;
    newSample_ = false;
    return n;
}

void CapSensor::startCalibration() {
    calSumLower_ = calSumUpper_ = calSumVert_ = 0.0f;
    calCount_ = 0;
    calStart_ = millis();
    calibrating_ = true;
    calibrated_ = false;
}
