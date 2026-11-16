/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "cc1101_presets.h"
#include "gpio_line.h"
#include "spi_device.h"
#include "subghz_types.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <string>
#include <vector>

struct BoardPins {
    int cs_gpio     = -1;
    int gdo0_gpio   = 15;
    int rf_sw0_gpio = 14;
    int reset_gpio  = -1;
};

struct RadioConfig {
    float freq_mhz         = 433.92f;
    float bit_rate_kbps    = 3.79f;
    float freq_dev_khz     = 25.4f;
    float rx_bw_khz        = 650.0f;
    int8_t power_dbm       = 10;
    uint8_t preamble_bytes = 16;
};

class CC1101Cancelled final : public std::exception {
public:
    const char* what() const noexcept override {
        return "CC1101 operation cancelled";
    }
};

class CC1101Radio {
public:
    CC1101Radio(SpiDevice& spi, const BoardPins& pins);

    CC1101Radio(const CC1101Radio&)            = delete;
    CC1101Radio& operator=(const CC1101Radio&) = delete;
    CC1101Radio(CC1101Radio&&)                 = delete;
    CC1101Radio& operator=(CC1101Radio&&)      = delete;

    void setSkipSres(bool skip) {
        skip_sres_ = skip;
    }

    // Initializes and probes the CC1101 chip on SPI, applying the initial Sub-GHz preset.
    void beginSubGhz(model::SubGhzPreset preset,
                     uint32_t frequency_hz,
                     int8_t power_dbm = 10,
                     const std::atomic_bool* cancel = nullptr);

    // Reconfigures the CC1101 registers, PATABLE, frequency, and RF_SW0/RF_SW1 antenna switch.
    void applySubGhzPreset(model::SubGhzPreset preset,
                           uint32_t frequency_hz,
                           const std::vector<uint8_t>& custom_preset_data = {},
                           int8_t power_dbm = 10);

    // Fast frequency retune (used by Frequency Analyzer and Hopping Read).
    void tuneFrequencyHz(uint32_t frequency_hz);

    void startAsyncReceive();
    size_t readAsyncPulses(std::vector<int32_t>& out_timings, int timeout_ms);
    void transmitAsyncTimings(const std::vector<int32_t>& timings_us,
                              uint32_t repeat_count,
                              const std::atomic_bool* cancel = nullptr);

    float readRssiDbm();
    void idle();
    void sleep();
    void releaseHostControlLines() noexcept;

    uint8_t readRegister(uint8_t reg);
    void writeRegister(uint8_t reg, uint8_t value);
    uint8_t chipPartnum();
    uint8_t chipVersion();
    uint8_t marcState();
    void selectAntenna(uint32_t frequency_hz);

    static float rssiFromRaw(uint8_t raw) noexcept;

private:
    SpiDevice& spi_;
    BoardPins pins_;
    GpioLine cs_;
    GpioLine gdo0_;
    GpioLine rf_sw0_;
    GpioLine reset_;

    uint32_t current_freq_hz_      = 433920000U;
    model::SubGhzPreset preset_    = model::SubGhzPreset::Ook650Async;
    int8_t power_dbm_              = 10;
    bool relaxed_status_           = false;
    bool skip_sres_                = false;
    uint64_t last_edge_ts_ns_      = 0;
    bool last_edge_level_          = false;
    uint8_t reg_shadow_[0x40]      = {};
    bool reg_shadow_valid_[0x40]   = {};

    void csLow();
    void csHigh();
    uint8_t command(uint8_t cmd);
    uint8_t readRegRaw(uint8_t reg);
    void writeRegRaw(uint8_t reg, uint8_t value);
    void writeBurst(uint8_t reg, const uint8_t* data, size_t len);
    uint8_t getRegBits(uint8_t reg, uint8_t msb, uint8_t lsb);
    void setRegBits(uint8_t reg, uint8_t value, uint8_t msb, uint8_t lsb);

    void resetChip(const std::atomic_bool* cancel);
    void standby(int timeout_ms = 100);
    void flushRx();
    void flushTx();

    static void throwIfCancelled(const std::atomic_bool* cancel);
    static void interruptibleSleep(int milliseconds, const std::atomic_bool* cancel);
    static void waitMicroseconds(uint32_t us, const std::atomic_bool* cancel);
};
