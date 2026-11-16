/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "cc1101.h"
#include "cc1101_registers.hpp"
#include "logger.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
constexpr uint8_t CMD_READ          = 0x80;
constexpr uint8_t CMD_BURST         = 0x40;
constexpr uint8_t CMD_ACCESS_STATUS = 0x40;
constexpr uint8_t CMD_RESET         = 0x30;
constexpr uint8_t CMD_RX            = 0x34;
constexpr uint8_t CMD_TX            = 0x35;
constexpr uint8_t CMD_IDLE          = 0x36;
constexpr uint8_t CMD_POWER_DOWN    = 0x39;
constexpr uint8_t CMD_FLUSH_RX      = 0x3A;
constexpr uint8_t CMD_FLUSH_TX      = 0x3B;

constexpr uint8_t REG_IOCFG2    = 0x00;
constexpr uint8_t REG_IOCFG0    = 0x02;
constexpr uint8_t REG_FREQ2     = 0x0D;
constexpr uint8_t REG_FREQ1     = 0x0E;
constexpr uint8_t REG_FREQ0     = 0x0F;
constexpr uint8_t REG_TEST0     = 0x2E;
constexpr uint8_t REG_PARTNUM   = 0x30;
constexpr uint8_t REG_VERSION   = 0x31;
constexpr uint8_t REG_RSSI      = 0x34;
constexpr uint8_t REG_MARCSTATE = 0x35;
constexpr uint8_t REG_PATABLE   = 0x3E;

constexpr uint8_t GDOX_ASYNC_SERIAL_DATA = 0x0D;
constexpr uint8_t GDOX_HIGH_Z            = 0x2E;

bool isStatusRegister(uint8_t reg) {
    return reg > REG_TEST0 && reg < REG_PATABLE;
}

uint8_t statusAddress(uint8_t reg) {
    return isStatusRegister(reg) ? static_cast<uint8_t>(reg | CMD_ACCESS_STATUS) : reg;
}

bool isKnownVersion(uint8_t ver) {
    return ver == 0x04 || ver == 0x14 || ver == 0x20 || ver == 0x3F || ver == 0x94 || ver == 0xBF || ver == 0xFC;
}

std::string hexByte(uint8_t value) {
    constexpr char digits[] = "0123456789ABCDEF";
    std::string result = "0x00";
    result[2] = digits[(value >> 4) & 0x0F];
    result[3] = digits[value & 0x0F];
    return result;
}

}  // namespace

CC1101Radio::CC1101Radio(SpiDevice& spi, const BoardPins& pins)
    : spi_(spi),
      pins_(pins),
      cs_(pins.cs_gpio),
      gdo0_(pins.gdo0_gpio),
      rf_sw0_(pins.rf_sw0_gpio),
      reset_(pins.reset_gpio) {}

void CC1101Radio::throwIfCancelled(const std::atomic_bool* cancel) {
    if (cancel && cancel->load(std::memory_order_acquire)) {
        throw CC1101Cancelled{};
    }
}

void CC1101Radio::interruptibleSleep(int milliseconds, const std::atomic_bool* cancel) {
    int remaining = std::max(0, milliseconds);
    while (remaining > 0) {
        throwIfCancelled(cancel);
        const int slice = std::min(remaining, 10);
        std::this_thread::sleep_for(std::chrono::milliseconds(slice));
        remaining -= slice;
    }
    throwIfCancelled(cancel);
}

void CC1101Radio::waitMicroseconds(uint32_t us, const std::atomic_bool* cancel) {
    using Clock = std::chrono::steady_clock;
    const auto target = Clock::now() + std::chrono::microseconds(us);
    if (us > 2500U) {
        while (true) {
            throwIfCancelled(cancel);
            const auto now = Clock::now();
            if (now >= target) return;
            const auto rem_us = std::chrono::duration_cast<std::chrono::microseconds>(target - now).count();
            if (rem_us <= 1200) break;
            std::this_thread::sleep_for(std::chrono::microseconds(std::min<int64_t>(rem_us - 900, 5000)));
        }
    }
    while (Clock::now() < target) {
        if (cancel && cancel->load(std::memory_order_relaxed)) {
            throw CC1101Cancelled{};
        }
    }
}

void CC1101Radio::csLow() {
    cs_.setValue(false);
}

void CC1101Radio::csHigh() {
    cs_.setValue(true);
}

uint8_t CC1101Radio::command(uint8_t cmd) {
    csLow();
    const uint8_t status = spi_.transfer(cmd);
    csHigh();
    return status;
}

uint8_t CC1101Radio::readRegRaw(uint8_t reg) {
    uint8_t tx[2] = {static_cast<uint8_t>(statusAddress(reg) | CMD_READ), 0x00};
    uint8_t rx[2] = {};
    csLow();
    spi_.transfer(tx, rx, sizeof(tx));
    csHigh();
    return rx[1];
}

void CC1101Radio::writeRegRaw(uint8_t reg, uint8_t value) {
    uint8_t tx[2] = {statusAddress(reg), value};
    csLow();
    spi_.writeBytes(tx, sizeof(tx));
    csHigh();
    if (reg < sizeof(reg_shadow_)) {
        reg_shadow_[reg] = value;
        reg_shadow_valid_[reg] = true;
    }
}

void CC1101Radio::writeBurst(uint8_t reg, const uint8_t* data, size_t len) {
    std::vector<uint8_t> tx(len + 1, 0x00);
    tx[0] = static_cast<uint8_t>(statusAddress(reg) | CMD_BURST);
    std::copy(data, data + len, tx.begin() + 1);
    csLow();
    spi_.writeBytes(tx.data(), tx.size());
    csHigh();
}

uint8_t CC1101Radio::readRegister(uint8_t reg) {
    return readRegRaw(reg);
}

void CC1101Radio::writeRegister(uint8_t reg, uint8_t value) {
    writeRegRaw(reg, value);
}

uint8_t CC1101Radio::getRegBits(uint8_t reg, uint8_t msb, uint8_t lsb) {
    const uint8_t value = readRegRaw(reg);
    const uint8_t mask = static_cast<uint8_t>(((1u << (msb - lsb + 1)) - 1u) << lsb);
    return static_cast<uint8_t>((value & mask) >> lsb);
}

void CC1101Radio::setRegBits(uint8_t reg, uint8_t value, uint8_t msb, uint8_t lsb) {
    uint8_t cur =
        (relaxed_status_ && reg < sizeof(reg_shadow_) && reg_shadow_valid_[reg]) ? reg_shadow_[reg] : readRegRaw(reg);
    if (relaxed_status_ && reg < sizeof(reg_shadow_) && !reg_shadow_valid_[reg]) cur = 0;
    const uint8_t mask = static_cast<uint8_t>(((1u << (msb - lsb + 1)) - 1u) << lsb);
    cur = static_cast<uint8_t>((cur & ~mask) | (value & mask));
    writeRegRaw(reg, cur);
}

void CC1101Radio::resetChip(const std::atomic_bool* cancel) {
    throwIfCancelled(cancel);
    if (reset_.valid()) {
        reset_.exportLine();
        reset_.setDirection(GpioLine::Direction::Out);
        reset_.setValue(false);
        interruptibleSleep(2, cancel);
        reset_.setValue(true);
        interruptibleSleep(5, cancel);
    }
    if (!skip_sres_) {
        command(CMD_RESET);
        interruptibleSleep(150, cancel);
    }
}

void CC1101Radio::beginSubGhz(model::SubGhzPreset preset,
                              uint32_t frequency_hz,
                              int8_t power_dbm,
                              const std::atomic_bool* cancel) {
    throwIfCancelled(cancel);
    preset_ = preset;
    current_freq_hz_ = frequency_hz;
    power_dbm_ = power_dbm;

    LOG_INFO("CC1101 driver: setting up control lines (CS={}, GDO0={}, RF_SW0={}, reset={})",
             pins_.cs_gpio < 0 ? "kernel" : std::to_string(pins_.cs_gpio),
             pins_.gdo0_gpio < 0 ? "disabled" : std::to_string(pins_.gdo0_gpio),
             pins_.rf_sw0_gpio < 0 ? "disabled" : std::to_string(pins_.rf_sw0_gpio),
             pins_.reset_gpio < 0 ? "SRES" : std::to_string(pins_.reset_gpio));

    cs_.exportLine();
    cs_.setDirection(GpioLine::Direction::Out);
    csHigh();
    if (rf_sw0_.valid()) {
        rf_sw0_.exportLine();
        rf_sw0_.setDirection(GpioLine::Direction::Out);
    }
    if (gdo0_.valid()) {
        gdo0_.exportLine();
        gdo0_.setDirection(GpioLine::Direction::In);
        gdo0_.setEdge("both");
    }

    resetChip(cancel);
    uint8_t part = 0;
    uint8_t ver = 0;
    for (int i = 0; i < 10; ++i) {
        throwIfCancelled(cancel);
        part = readRegRaw(REG_PARTNUM);
        ver = chipVersion();
        if (isKnownVersion(ver)) break;
        interruptibleSleep(20, cancel);
    }
    if (!isKnownVersion(ver)) {
        throw std::runtime_error("CC1101 probe failed after 10 attempts: PARTNUM=" + hexByte(part) +
                                 ", VERSION=" + hexByte(ver));
    }
    relaxed_status_ = (ver == 0x3F || ver == 0x94 || ver == 0xBF || ver == 0xFC);
    LOG_INFO("CC1101 driver: detected PARTNUM=0x{:02X}, VERSION=0x{:02X}, relaxed_status={}",
             static_cast<unsigned>(part),
             static_cast<unsigned>(ver),
             relaxed_status_);

    applySubGhzPreset(preset, frequency_hz, {}, power_dbm);
    throwIfCancelled(cancel);
}

void CC1101Radio::applySubGhzPreset(model::SubGhzPreset preset,
                                    uint32_t frequency_hz,
                                    const std::vector<uint8_t>& custom_preset_data,
                                    int8_t power_dbm) {
    if (!model::is_valid_cc1101_frequency_hz(frequency_hz)) {
        throw std::runtime_error("Invalid CC1101 frequency: " + std::to_string(frequency_hz) + " Hz");
    }
    preset_ = preset;
    current_freq_hz_ = frequency_hz;
    power_dbm_ = power_dbm;

    standby();
    const auto cfg = cc1101_driver::buildPresetConfig(preset, frequency_hz, custom_preset_data, power_dbm);
    for (const auto& [reg, val] : cfg.registers) {
        writeRegRaw(reg, val);
    }
    writeBurst(REG_PATABLE, cfg.patable.data(), cfg.patable.size());
    if (rf_sw0_.valid()) {
        rf_sw0_.setValue(cfg.rf_sw0_high);
    }
    flushRx();
    flushTx();
}

void CC1101Radio::tuneFrequencyHz(uint32_t frequency_hz) {
    if (!model::is_valid_cc1101_frequency_hz(frequency_hz)) {
        return;
    }
    current_freq_hz_ = frequency_hz;
    standby();
    const auto cfg = cc1101_driver::buildPresetConfig(preset_, frequency_hz, {}, power_dbm_);
    writeRegRaw(REG_FREQ2, cfg.freq2);
    writeRegRaw(REG_FREQ1, cfg.freq1);
    writeRegRaw(REG_FREQ0, cfg.freq0);
    selectAntenna(frequency_hz);
    command(CMD_RX);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

void CC1101Radio::selectAntenna(uint32_t frequency_hz) {
    const auto cfg = cc1101_driver::buildPresetConfig(preset_, frequency_hz, {}, power_dbm_);
    if (rf_sw0_.valid()) {
        rf_sw0_.setValue(cfg.rf_sw0_high);
    }
    writeRegRaw(REG_IOCFG2, cfg.iocfg2_rf_sw1);
}

void CC1101Radio::standby(int timeout_ms) {
    command(CMD_IDLE);
    if (relaxed_status_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        return;
    }
    auto start = std::chrono::steady_clock::now();
    while (getRegBits(REG_MARCSTATE, 4, 0) != 0x01) {
        if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count() >
            timeout_ms) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void CC1101Radio::flushRx() {
    command(CMD_IDLE);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    command(CMD_FLUSH_RX);
    command(CMD_FLUSH_RX);
}

void CC1101Radio::flushTx() {
    command(CMD_FLUSH_TX);
}

void CC1101Radio::startAsyncReceive() {
    standby();
    flushRx();
    writeRegRaw(REG_IOCFG0, GDOX_ASYNC_SERIAL_DATA);
    selectAntenna(current_freq_hz_);
    if (gdo0_.valid()) {
        gdo0_.setEdge("both");
    }
    last_edge_ts_ns_ = 0;
    command(CMD_RX);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
}

size_t CC1101Radio::readAsyncPulses(std::vector<int32_t>& out_timings, int timeout_ms) {
    if (!gdo0_.valid()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(std::max(1, timeout_ms)));
        return 0;
    }
    std::vector<GpioLine::EdgeEvent> events;
    const size_t n = gdo0_.readEdgeEvents(events, timeout_ms, 128);
    if (n == 0) {
        return 0;
    }

    size_t added = 0;
    for (const auto& ev : events) {
        if (last_edge_ts_ns_ != 0 && ev.timestamp_ns > last_edge_ts_ns_) {
            const uint64_t delta_us = (ev.timestamp_ns - last_edge_ts_ns_) / 1000ULL;
            if (delta_us >= 35ULL && delta_us <= 250000ULL) {
                // If current edge is rising, previous state was LOW (-delta_us);
                // if current edge is falling, previous state was HIGH (+delta_us).
                const int32_t signed_us = ev.rising ? -static_cast<int32_t>(delta_us) : static_cast<int32_t>(delta_us);
                if (!out_timings.empty() || signed_us > 0) {
                    out_timings.push_back(signed_us);
                    ++added;
                }
            }
        }
        last_edge_ts_ns_ = ev.timestamp_ns;
        last_edge_level_ = ev.rising;
    }
    return added;
}

void CC1101Radio::transmitAsyncTimings(const std::vector<int32_t>& timings_us,
                                       uint32_t repeat_count,
                                       const std::atomic_bool* cancel) {
    throwIfCancelled(cancel);
    if (timings_us.empty()) {
        return;
    }

    standby();
    flushTx();
    selectAntenna(current_freq_hz_);
    writeRegRaw(REG_IOCFG0, GDOX_HIGH_Z);

    if (gdo0_.valid()) {
        gdo0_.setDirection(GpioLine::Direction::Out);
        gdo0_.setValue(false);
    }

    command(CMD_TX);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    const uint32_t reps = std::clamp<uint32_t>(repeat_count, 1U, 50U);
    try {
        for (uint32_t r = 0; r < reps; ++r) {
            throwIfCancelled(cancel);
            for (int32_t t : timings_us) {
                if (t == 0) continue;
                const bool high = t > 0;
                const uint32_t duration_us = static_cast<uint32_t>(high ? t : -t);
                if (gdo0_.valid()) {
                    gdo0_.setValue(high);
                }
                waitMicroseconds(duration_us, cancel);
            }
            if (gdo0_.valid()) {
                gdo0_.setValue(false);
            }
        }
    } catch (...) {
        if (gdo0_.valid()) {
            try {
                gdo0_.setValue(false);
                gdo0_.setEdge("both");
            } catch (...) {
            }
        }
        standby();
        throw;
    }

    if (gdo0_.valid()) {
        gdo0_.setValue(false);
        gdo0_.setEdge("both");
    }
    standby();
    writeRegRaw(REG_IOCFG0, GDOX_ASYNC_SERIAL_DATA);
    selectAntenna(current_freq_hz_);
}

float CC1101Radio::readRssiDbm() {
    const uint8_t raw = readRegRaw(REG_RSSI);
    return rssiFromRaw(raw);
}

void CC1101Radio::idle() {
    standby();
    flushRx();
    flushTx();
    selectAntenna(current_freq_hz_);
}

void CC1101Radio::sleep() {
    standby();
    command(CMD_POWER_DOWN);
}

void CC1101Radio::releaseHostControlLines() noexcept {
    try {
        if (rf_sw0_.valid()) {
            rf_sw0_.setValue(false);
        }
    } catch (...) {
    }

    gdo0_.unexportLine();
    rf_sw0_.unexportLine();
    reset_.unexportLine();
    cs_.unexportLine();
}

uint8_t CC1101Radio::chipPartnum() {
    return readRegRaw(REG_PARTNUM);
}

uint8_t CC1101Radio::chipVersion() {
    return readRegRaw(REG_VERSION);
}

uint8_t CC1101Radio::marcState() {
    return getRegBits(REG_MARCSTATE, 4, 0);
}

float CC1101Radio::rssiFromRaw(uint8_t raw) noexcept {
    return raw >= 128 ? ((static_cast<float>(raw) - 256.0f) / 2.0f) - 74.0f : (static_cast<float>(raw) / 2.0f) - 74.0f;
}
