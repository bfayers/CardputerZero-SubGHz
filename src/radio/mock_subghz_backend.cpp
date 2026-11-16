/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "subghz_backend.h"
#include "cc1101_presets.h"
#include "subghz_protocols.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>

namespace radio {
namespace {

class MockSubGhzBackend final : public SubGhzBackend {
public:
    RadioHardwareInfo open(model::SubGhzPreset preset,
                           uint32_t frequency_hz,
                           int8_t power_dbm,
                           const std::atomic_bool* cancel) override {
        if (cancel && cancel->load(std::memory_order_acquire)) {
            throw RadioCancelled{};
        }
        const auto cfg = cc1101_driver::buildPresetConfig(preset, frequency_hz, {}, power_dbm);
        info_.backend_name = "SDL Mock CC1101";
        info_.is_mock = true;
        info_.partnum = 0x00;
        info_.version = 0x14;
        info_.marcstate = 0x01;  // IDLE
        info_.frequency_hz = frequency_hz;
        info_.preset = preset;
        info_.power_dbm = power_dbm;
        info_.rf_sw0 = cfg.rf_sw0_high;
        info_.iocfg2 = cfg.iocfg2_rf_sw1;
        open_ = true;
        rx_active_ = false;
        tick_ = 0;
        return info_;
    }

    void close() noexcept override {
        open_ = false;
        rx_active_ = false;
        info_.marcstate = 0x00;
    }

    void configure(model::SubGhzPreset preset,
                   uint32_t frequency_hz,
                   const std::vector<uint8_t>& custom_preset_data,
                   int8_t power_dbm) override {
        const auto cfg = cc1101_driver::buildPresetConfig(preset, frequency_hz, custom_preset_data, power_dbm);
        info_.preset = preset;
        info_.frequency_hz = frequency_hz;
        info_.power_dbm = power_dbm;
        info_.rf_sw0 = cfg.rf_sw0_high;
        info_.iocfg2 = cfg.iocfg2_rf_sw1;
    }

    void tuneFrequency(uint32_t frequency_hz) override {
        const auto cfg = cc1101_driver::buildPresetConfig(info_.preset, frequency_hz, {}, info_.power_dbm);
        info_.frequency_hz = frequency_hz;
        info_.rf_sw0 = cfg.rf_sw0_high;
        info_.iocfg2 = cfg.iocfg2_rf_sw1;
    }

    void startAsyncRx() override {
        rx_active_ = true;
        info_.marcstate = 0x0D;  // RX
    }

    void stopRx() override {
        rx_active_ = false;
        info_.marcstate = 0x01;  // IDLE
    }

    float readRssiDbm() override {
        ++tick_;
        // Simulate a realistic noise floor around -96 dBm with periodic bursts on 433.92 / 315.00 / 868.35 MHz
        const double phase = static_cast<double>(tick_) * 0.17;
        float rssi = -96.0f + static_cast<float>(std::sin(phase) * 3.5);
        const bool burst_window = ((tick_ / 18) % 5) == 1;
        if (burst_window &&
            (info_.frequency_hz == 433920000U || info_.frequency_hz == 315000000U || info_.frequency_hz == 868350000U)) {
            rssi = -46.0f + static_cast<float>(std::cos(phase * 2.1) * 4.0);
        }
        return rssi;
    }

    size_t readPulses(std::vector<int32_t>& out_timings, int timeout_ms) override {
        std::this_thread::sleep_for(std::chrono::milliseconds(std::clamp(timeout_ms, 5, 40)));
        if (!rx_active_) {
            return 0;
        }
        ++rx_poll_count_;
        // Emit a synthetic remote burst every ~60 polls (~2.4 seconds) in the desktop simulator
        if (rx_poll_count_ % 60 == 15) {
            const auto& specs = model::manual_protocol_specs();
            const auto& spec = specs[synth_index_ % specs.size()];
            ++synth_index_;

            model::SubGhzSignal synth;
            synth.protocol = spec.name;
            synth.bit_count = spec.bit_count;
            synth.te_us = spec.default_te_us;
            synth.key = spec.default_key ^ static_cast<uint64_t>(synth_index_ * 0x11U);
            synth.frequency_hz = info_.frequency_hz;
            synth.preset = info_.preset;
            const auto frame = model::encode_protocol_timings(synth);
            out_timings.insert(out_timings.end(), frame.begin(), frame.end());
            return frame.size();
        }
        return 0;
    }

    void transmitTimings(const std::vector<int32_t>& timings_us,
                         uint32_t repeat_count,
                         const std::atomic_bool* cancel) override {
        info_.marcstate = 0x13;  // TX
        uint64_t total_us = 0;
        for (int32_t t : timings_us) {
            total_us += static_cast<uint64_t>(t < 0 ? -t : t);
        }
        total_us *= std::clamp<uint32_t>(repeat_count, 1U, 10U);
        const int sleep_ms = std::clamp<int>(static_cast<int>(total_us / 1000ULL), 20, 350);
        int elapsed = 0;
        while (elapsed < sleep_ms) {
            if (cancel && cancel->load(std::memory_order_acquire)) {
                info_.marcstate = 0x01;
                throw RadioCancelled{};
            }
            const int step = std::min(10, sleep_ms - elapsed);
            std::this_thread::sleep_for(std::chrono::milliseconds(step));
            elapsed += step;
        }
        info_.marcstate = 0x01;  // IDLE
    }

    RadioHardwareInfo hardwareInfo() override {
        return info_;
    }

private:
    RadioHardwareInfo info_;
    bool open_{false};
    bool rx_active_{false};
    uint64_t tick_{0};
    uint64_t rx_poll_count_{0};
    std::size_t synth_index_{0};
};

}  // namespace

#if USE_DESKTOP || !defined(__linux__)
std::unique_ptr<SubGhzBackend> makeDefaultSubGhzBackend() {
    return std::make_unique<MockSubGhzBackend>();
}
#endif

}  // namespace radio
