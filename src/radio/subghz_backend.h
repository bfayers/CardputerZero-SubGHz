/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "subghz_types.h"

#include <atomic>
#include <cstdint>
#include <exception>
#include <memory>
#include <string>
#include <vector>

namespace radio {

class RadioCancelled final : public std::exception {
public:
    const char* what() const noexcept override {
        return "Radio operation cancelled";
    }
};

struct RadioHardwareInfo {
    std::string backend_name{"Uninitialized"};
    bool is_mock{false};
    uint8_t partnum{0x00};
    uint8_t version{0x00};
    uint8_t marcstate{0x01};
    uint32_t frequency_hz{433920000U};
    model::SubGhzPreset preset{model::SubGhzPreset::Ook650Async};
    int8_t power_dbm{10};
    bool rf_sw0{true};
    uint8_t iocfg2{0x2F};
};

class SubGhzBackend {
public:
    virtual ~SubGhzBackend() = default;

    virtual RadioHardwareInfo open(model::SubGhzPreset preset,
                                   uint32_t frequency_hz,
                                   int8_t power_dbm,
                                   const std::atomic_bool* cancel) = 0;
    virtual void close() noexcept = 0;

    virtual void configure(model::SubGhzPreset preset,
                           uint32_t frequency_hz,
                           const std::vector<uint8_t>& custom_preset_data,
                           int8_t power_dbm) = 0;
    virtual void tuneFrequency(uint32_t frequency_hz) = 0;
    virtual void startAsyncRx() = 0;
    virtual void stopRx() = 0;
    virtual float readRssiDbm() = 0;
    virtual size_t readPulses(std::vector<int32_t>& out_timings, int timeout_ms) = 0;
    virtual void transmitTimings(const std::vector<int32_t>& timings_us,
                                 uint32_t repeat_count,
                                 const std::atomic_bool* cancel) = 0;
    virtual RadioHardwareInfo hardwareInfo() = 0;
};

std::unique_ptr<SubGhzBackend> makeDefaultSubGhzBackend();

}  // namespace radio
