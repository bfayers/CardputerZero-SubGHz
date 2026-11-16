/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "subghz_backend.h"

#if !USE_DESKTOP && defined(__linux__)

#include "cardputerzero_cc1101_power.hpp"
#include "cc1101.h"
#include "cc1101_presets.h"
#include "logger.h"
#include "spi_device.h"

#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

namespace radio {
namespace {

void cancellableSleep(std::chrono::milliseconds duration, const std::atomic_bool* cancel) {
    const auto deadline = std::chrono::steady_clock::now() + duration;
    while (std::chrono::steady_clock::now() < deadline) {
        if (cancel && cancel->load(std::memory_order_acquire)) {
            throw RadioCancelled{};
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

class LinuxSubGhzBackend final : public SubGhzBackend {
public:
    RadioHardwareInfo open(model::SubGhzPreset preset,
                           uint32_t frequency_hz,
                           int8_t power_dbm,
                           const std::atomic_bool* cancel) override {
        close();
        cancellableSleep(std::chrono::milliseconds(250), cancel);

        std::string stage = "Cap power enable";
        try {
            std::string power_error;
            if (!power_.enable(power_error, cancel)) {
                if (cancel && cancel->load(std::memory_order_acquire)) {
                    throw RadioCancelled{};
                }
                throw std::runtime_error("CC1101 power enable failed: " + power_error);
            }

            stage = "power settle";
            cancellableSleep(std::chrono::milliseconds(1200), cancel);

            stage = "SPI open";
            spi_ = std::make_unique<SpiDevice>();
            spi_->openDevice("/dev/spidev0.1", 1000000, 0, 8, false);

            stage = "CC1101 probe";
            BoardPins pins;
            pins.cs_gpio = -1;
            pins.gdo0_gpio = 15;
            pins.rf_sw0_gpio = 14;
            pins.reset_gpio = -1;

            radio_ = std::make_unique<CC1101Radio>(*spi_, pins);
            radio_->beginSubGhz(preset, frequency_hz, power_dbm, cancel);

            const auto preset_cfg = cc1101_driver::buildPresetConfig(preset, frequency_hz, {}, power_dbm);
            info_.backend_name = "CC1101 /dev/spidev0.1";
            info_.is_mock = false;
            info_.partnum = radio_->chipPartnum();
            info_.version = radio_->chipVersion();
            info_.marcstate = radio_->marcState();
            info_.frequency_hz = frequency_hz;
            info_.preset = preset;
            info_.power_dbm = power_dbm;
            info_.rf_sw0 = preset_cfg.rf_sw0_high;
            info_.iocfg2 = preset_cfg.iocfg2_rf_sw1;
            open_ = true;
            return info_;
        } catch (const RadioCancelled&) {
            close();
            throw;
        } catch (const CC1101Cancelled&) {
            close();
            throw RadioCancelled{};
        } catch (const std::exception& ex) {
            const std::string msg = "CC1101 init failed at " + stage + ": " + ex.what();
            LOG_ERROR("{}", msg);
            close();
            throw std::runtime_error(msg);
        }
    }

    void close() noexcept override {
        if (radio_) {
            if (open_) {
                try {
                    radio_->sleep();
                } catch (...) {
                }
            }
            radio_->releaseHostControlLines();
            radio_.reset();
        }
        if (spi_) {
            spi_->closeDevice();
            spi_.reset();
        }
        power_.disable(true);
        open_ = false;
    }

    void configure(model::SubGhzPreset preset,
                   uint32_t frequency_hz,
                   const std::vector<uint8_t>& custom_preset_data,
                   int8_t power_dbm) override {
        requireOpen();
        radio_->applySubGhzPreset(preset, frequency_hz, custom_preset_data, power_dbm);
        const auto preset_cfg = cc1101_driver::buildPresetConfig(preset, frequency_hz, custom_preset_data, power_dbm);
        info_.preset = preset;
        info_.frequency_hz = frequency_hz;
        info_.power_dbm = power_dbm;
        info_.rf_sw0 = preset_cfg.rf_sw0_high;
        info_.iocfg2 = preset_cfg.iocfg2_rf_sw1;
        info_.marcstate = radio_->marcState();
    }

    void tuneFrequency(uint32_t frequency_hz) override {
        requireOpen();
        radio_->tuneFrequencyHz(frequency_hz);
        const auto preset_cfg = cc1101_driver::buildPresetConfig(info_.preset, frequency_hz, {}, info_.power_dbm);
        info_.frequency_hz = frequency_hz;
        info_.rf_sw0 = preset_cfg.rf_sw0_high;
        info_.iocfg2 = preset_cfg.iocfg2_rf_sw1;
    }

    void startAsyncRx() override {
        requireOpen();
        radio_->startAsyncReceive();
        info_.marcstate = radio_->marcState();
    }

    void stopRx() override {
        requireOpen();
        radio_->idle();
        info_.marcstate = radio_->marcState();
    }

    float readRssiDbm() override {
        requireOpen();
        return radio_->readRssiDbm();
    }

    size_t readPulses(std::vector<int32_t>& out_timings, int timeout_ms) override {
        requireOpen();
        return radio_->readAsyncPulses(out_timings, timeout_ms);
    }

    void transmitTimings(const std::vector<int32_t>& timings_us,
                         uint32_t repeat_count,
                         const std::atomic_bool* cancel) override {
        requireOpen();
        try {
            radio_->transmitAsyncTimings(timings_us, repeat_count, cancel);
            info_.marcstate = radio_->marcState();
        } catch (const CC1101Cancelled&) {
            throw RadioCancelled{};
        }
    }

    RadioHardwareInfo hardwareInfo() override {
        if (open_ && radio_) {
            try {
                info_.marcstate = radio_->marcState();
            } catch (...) {
            }
        }
        return info_;
    }

private:
    hal::CardputerZeroCc1101Power power_;
    std::unique_ptr<SpiDevice> spi_;
    std::unique_ptr<CC1101Radio> radio_;
    RadioHardwareInfo info_;
    bool open_{false};

    void requireOpen() const {
        if (!open_ || !radio_ || !spi_) {
            throw std::runtime_error("CC1101 hardware backend is not open");
        }
    }
};

}  // namespace

std::unique_ptr<SubGhzBackend> makeDefaultSubGhzBackend() {
    return std::make_unique<LinuxSubGhzBackend>();
}

}  // namespace radio

#endif
