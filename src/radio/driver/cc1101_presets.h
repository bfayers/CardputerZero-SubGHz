/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "subghz_types.h"

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

namespace cc1101_driver {

struct Cc1101PresetConfig {
    std::vector<std::pair<uint8_t, uint8_t>> registers;
    std::array<uint8_t, 8> patable{};
    uint8_t freq2{0x10};
    uint8_t freq1{0xB0};
    uint8_t freq0{0x71};
    bool rf_sw0_high{true};
    uint8_t iocfg2_rf_sw1{0x2F};
    bool is_ook{true};

    // Helper for looking up a register value in unit tests / diagnostics
    bool findRegister(uint8_t reg, uint8_t& out_val) const noexcept {
        bool found = false;
        for (const auto& [r, v] : registers) {
            if (r == reg) {
                out_val = v;
                found = true;
            }
        }
        return found;
    }
};

uint8_t computePaTableByte(uint32_t frequency_hz, int8_t power_dbm) noexcept;

Cc1101PresetConfig buildPresetConfig(model::SubGhzPreset preset,
                                     uint32_t frequency_hz,
                                     const std::vector<uint8_t>& custom_preset_data = {},
                                     int8_t power_dbm = 10);

}  // namespace cc1101_driver
