/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "cc1101_presets.h"
#include "cc1101_registers.hpp"
#include "pinctrl_state.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}

}  // namespace

int main() {
    using namespace cc1101_driver;
    using namespace model;

    // 1. Verify OOK 650 kHz Async preset @ 433.92 MHz
    {
        const auto cfg = buildPresetConfig(SubGhzPreset::Ook650Async, 433920000U, {}, 10);
        require(cfg.is_ook, "Ook650Async must be OOK");
        require(cfg.rf_sw0_high == true && cfg.iocfg2_rf_sw1 == 0x2F, "433.92 MHz antenna switch mismatch");
        uint8_t val = 0;
        require(cfg.findRegister(0x02, val) && val == 0x0D, "IOCFG0 must be 0x0D for async GDO0");
        require(cfg.findRegister(0x08, val) && val == 0x32, "PKTCTRL0 must be 0x32 for async mode");
        require(cfg.findRegister(0x10, val) && val == 0x17, "MDMCFG4 must be 0x17 for 650kHz BW");
        require(cfg.findRegister(0x12, val) && val == 0x30, "MDMCFG2 must be 0x30 for ASK/OOK");
        require(cfg.patable[0] == 0x00 && cfg.patable[1] == 0xC0, "OOK 433.92MHz 10dBm PATABLE mismatch");
    }

    // 2. Verify OOK 270 kHz Async preset @ 315.00 MHz
    {
        const auto cfg = buildPresetConfig(SubGhzPreset::Ook270Async, 315000000U, {}, 10);
        require(cfg.rf_sw0_high == false && cfg.iocfg2_rf_sw1 == 0x6F, "315 MHz antenna switch mismatch");
        uint8_t val = 0;
        require(cfg.findRegister(0x10, val) && val == 0x67, "MDMCFG4 must be 0x67 for 270kHz BW");
    }

    // 3. Verify 2-FSK presets @ 868.35 MHz
    {
        const auto cfg238 = buildPresetConfig(SubGhzPreset::Fsk2Dev238Async, 868350000U, {}, 10);
        const auto cfg476 = buildPresetConfig(SubGhzPreset::Fsk2Dev476Async, 868350000U, {}, 10);
        require(!cfg238.is_ook && !cfg476.is_ook, "2FSK presets must not be OOK");
        require(cfg238.rf_sw0_high == true && cfg238.iocfg2_rf_sw1 == 0x6F, "868 MHz antenna switch mismatch");
        uint8_t dev238 = 0, dev476 = 0;
        require(cfg238.findRegister(0x15, dev238) && dev238 == 0x04, "DEVIATN 2.38kHz mismatch");
        require(cfg476.findRegister(0x15, dev476) && dev476 == 0x47, "DEVIATN 47.6kHz mismatch");
    }

    // 4. Verify Custom preset parsing
    {
        const std::vector<uint8_t> custom_bytes = {
            0x02, 0x0D, 0x08, 0x32, 0x12, 0x04, 0x15, 0x24, 0x00, 0x00,
            0xC2, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        };
        const auto cfg = buildPresetConfig(SubGhzPreset::Custom, 868350000U, custom_bytes, 10);
        uint8_t dev = 0;
        require(cfg.findRegister(0x15, dev) && dev == 0x24, "Custom DEVIATN register mismatch");
        require(cfg.patable[0] == 0xC2, "Custom PATABLE mismatch");
    }

    // 5. Verify pinctrl state parser
    {
        hal::PinctrlState state;
        std::string err;
        require(hal::parsePinctrlState("14: a5    pn | hi // GPIO14\n", 14, state, err), err);
        require(hal::pinctrlRestoreArguments(state) == std::vector<std::string>{"set", "14", "a5", "pn"},
                "pinctrl restore args mismatch");
    }

    std::cout << "cc1101_preset_test passed\n";
    return 0;
}
