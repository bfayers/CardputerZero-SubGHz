/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "cc1101_presets.h"

#include <algorithm>
#include <cmath>

namespace cc1101_driver {
namespace {

constexpr double kXoscHz = 26000000.0;

void fillFrequencyAndAntenna(uint32_t frequency_hz, Cc1101PresetConfig& cfg) noexcept {
    const uint64_t frf = (static_cast<uint64_t>(frequency_hz) * 65536ULL) / static_cast<uint64_t>(kXoscHz);
    cfg.freq2 = static_cast<uint8_t>((frf >> 16U) & 0xFFU);
    cfg.freq1 = static_cast<uint8_t>((frf >> 8U) & 0xFFU);
    cfg.freq0 = static_cast<uint8_t>(frf & 0xFFU);

    // Cap CC1101 antenna switch table:
    // < 374.0 MHz:  RF_SW0 (GPIO14) = 0, RF_SW1 (GDO2) = 1 (IOCFG2 = 0x6F)
    // < 650.5 MHz:  RF_SW0 (GPIO14) = 1, RF_SW1 (GDO2) = 0 (IOCFG2 = 0x2F)
    // >= 650.5 MHz: RF_SW0 (GPIO14) = 1, RF_SW1 (GDO2) = 1 (IOCFG2 = 0x6F)
    if (frequency_hz < 374000000U) {
        cfg.rf_sw0_high = false;
        cfg.iocfg2_rf_sw1 = 0x6F;
    } else if (frequency_hz < 650500000U) {
        cfg.rf_sw0_high = true;
        cfg.iocfg2_rf_sw1 = 0x2F;
    } else {
        cfg.rf_sw0_high = true;
        cfg.iocfg2_rf_sw1 = 0x6F;
    }
}

}  // namespace

uint8_t computePaTableByte(uint32_t frequency_hz, int8_t power_dbm) noexcept {
    constexpr int8_t kAllowed[8] = {-30, -20, -15, -10, 0, 5, 7, 10};
    constexpr uint8_t kTable[8][4] = {
        {0x12, 0x12, 0x03, 0x03},
        {0x0D, 0x0E, 0x0F, 0x0E},
        {0x1C, 0x1D, 0x1E, 0x1E},
        {0x34, 0x34, 0x27, 0x27},
        {0x51, 0x60, 0x50, 0x8E},
        {0x85, 0x84, 0x81, 0xCD},
        {0xCB, 0xC8, 0xCB, 0xC7},
        {0xC2, 0xC0, 0xC2, 0xC0},
    };
    const int band = frequency_hz < 374000000U
                         ? 0
                         : (frequency_hz < 650500000U ? 1 : (frequency_hz < 891500000U ? 2 : 3));
    std::size_t best_idx = 7;
    int best_diff = 1000;
    for (std::size_t i = 0; i < 8; ++i) {
        const int diff = std::abs(static_cast<int>(power_dbm) - static_cast<int>(kAllowed[i]));
        if (diff < best_diff) {
            best_diff = diff;
            best_idx = i;
        }
    }
    return kTable[best_idx][band];
}

Cc1101PresetConfig buildPresetConfig(model::SubGhzPreset preset,
                                     uint32_t frequency_hz,
                                     const std::vector<uint8_t>& custom_preset_data,
                                     int8_t power_dbm) {
    Cc1101PresetConfig cfg;
    fillFrequencyAndAntenna(frequency_hz, cfg);
    const uint8_t pa_byte = computePaTableByte(frequency_hz, power_dbm);

    if (preset == model::SubGhzPreset::Custom && !custom_preset_data.empty()) {
        std::size_t idx = 0;
        uint8_t mdmcfg2 = 0x30;
        uint8_t frend0 = 0x11;
        while (idx + 1 < custom_preset_data.size()) {
            const uint8_t reg = custom_preset_data[idx];
            const uint8_t val = custom_preset_data[idx + 1];
            idx += 2;
            if (reg == 0x00 && val == 0x00) {
                break;
            }
            if (reg == 0x12) mdmcfg2 = val;
            if (reg == 0x22) frend0 = val;
            cfg.registers.emplace_back(reg, val);
        }
        cfg.is_ook = ((mdmcfg2 >> 4U) & 0x07U) == 0x03U || (frend0 & 0x07U) == 0x01U;
        for (std::size_t p = 0; p < 8 && idx + p < custom_preset_data.size(); ++p) {
            cfg.patable[p] = custom_preset_data[idx + p];
        }
        if (cfg.patable == std::array<uint8_t, 8>{}) {
            if (cfg.is_ook) {
                cfg.patable[0] = 0x00;
                cfg.patable[1] = pa_byte;
            } else {
                cfg.patable[0] = pa_byte;
            }
        }
    } else if (preset == model::SubGhzPreset::Ook270Async || preset == model::SubGhzPreset::Ook650Async) {
        const bool wide = (preset == model::SubGhzPreset::Ook650Async);
        cfg.is_ook = true;
        cfg.registers = {
            {0x02, 0x0D},                  // IOCFG0: GDO0 asynchronous serial data out/in
            {0x03, wide ? 0x07 : 0x47},    // FIFOTHR
            {0x08, 0x32},                  // PKTCTRL0: Asynchronous serial mode, infinite packet length
            {0x0B, 0x06},                  // FSCTRL1
            {0x10, wide ? 0x17 : 0x67},    // MDMCFG4: RX filter BW (650 kHz vs 270 kHz)
            {0x11, 0x32},                  // MDMCFG3: Data rate mantissa
            {0x12, 0x30},                  // MDMCFG2: ASK/OOK modulation, no preamble/sync
            {0x13, 0x00},                  // MDMCFG1
            {0x14, 0x00},                  // MDMCFG0
            {0x18, 0x18},                  // MCSM0: Auto-calibrate on IDLE -> RX/TX
            {0x19, 0x18},                  // FOCCFG
            {0x1B, wide ? 0x07 : 0x03},    // AGCCTRL2
            {0x1C, 0x00},                  // AGCCTRL1
            {0x1D, wide ? 0x92 : 0x91},    // AGCCTRL0
            {0x20, 0xFB},                  // WORCTRL
            {0x21, 0xB6},                  // FREND1
            {0x22, 0x11},                  // FREND0: PA_POWER = 1 for OOK
            {0x23, 0xE9},                  // FSCAL3
            {0x24, 0x2A},                  // FSCAL2
            {0x25, 0x00},                  // FSCAL1
            {0x26, 0x1F},                  // FSCAL0
            {0x2C, wide ? 0x88 : 0x81},    // TEST2
            {0x2D, wide ? 0x31 : 0x35},    // TEST1
            {0x2E, 0x09},                  // TEST0
        };
        cfg.patable = {0x00, pa_byte, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    } else {
        // 2-FSK Async presets: Fsk2Dev238Async (DEVIATN=0x04) or Fsk2Dev476Async (DEVIATN=0x47)
        const bool wide_dev = (preset == model::SubGhzPreset::Fsk2Dev476Async);
        cfg.is_ook = false;
        cfg.registers = {
            {0x02, 0x0D},                  // IOCFG0: GDO0 asynchronous serial data out/in
            {0x03, 0x07},                  // FIFOTHR
            {0x08, 0x32},                  // PKTCTRL0: Asynchronous serial mode
            {0x0B, 0x06},                  // FSCTRL1
            {0x10, 0x67},                  // MDMCFG4: ~270.8 kHz RX filter BW
            {0x11, 0x83},                  // MDMCFG3
            {0x12, 0x04},                  // MDMCFG2: 2-FSK modulation, no sync/preamble
            {0x13, 0x00},                  // MDMCFG1
            {0x14, 0x00},                  // MDMCFG0
            {0x15, wide_dev ? 0x47 : 0x04},// DEVIATN: 47.6 kHz vs 2.38 kHz
            {0x18, 0x18},                  // MCSM0
            {0x19, 0x16},                  // FOCCFG
            {0x1A, 0x6C},                  // BSCFG
            {0x1B, 0x07},                  // AGCCTRL2
            {0x1C, 0x00},                  // AGCCTRL1
            {0x1D, 0x91},                  // AGCCTRL0
            {0x20, 0xFB},                  // WORCTRL
            {0x21, 0x56},                  // FREND1
            {0x22, 0x10},                  // FREND0: PA_POWER = 0 for FSK
            {0x23, 0xE9},                  // FSCAL3
            {0x24, 0x2A},                  // FSCAL2
            {0x25, 0x00},                  // FSCAL1
            {0x26, 0x1F},                  // FSCAL0
            {0x2C, 0x81},                  // TEST2
            {0x2D, 0x35},                  // TEST1
            {0x2E, 0x09},                  // TEST0
        };
        cfg.patable = {pa_byte, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    }

    // Always append frequency registers and Cap CC1101 GDO2 RF_SW1 antenna switch
    cfg.registers.emplace_back(0x0D, cfg.freq2);
    cfg.registers.emplace_back(0x0E, cfg.freq1);
    cfg.registers.emplace_back(0x0F, cfg.freq0);
    cfg.registers.emplace_back(0x00, cfg.iocfg2_rf_sw1);
    return cfg;
}

}  // namespace cc1101_driver
