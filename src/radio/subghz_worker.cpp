/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "subghz_worker.h"
#include "subghz_protocols.h"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <utility>

namespace radio {
namespace {

uint64_t monotonicMilliseconds() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

}  // namespace

SubGhzWorker::SubGhzWorker() : SubGhzWorker(makeDefaultSubGhzBackend()) {}

SubGhzWorker::SubGhzWorker(std::unique_ptr<SubGhzBackend> backend) : backend_(std::move(backend)) {}

SubGhzWorker::~SubGhzWorker() {
    stop();
}

void SubGhzWorker::start() {
    if (running_.exchange(true)) {
        return;
    }
    stop_requested_.store(false, std::memory_order_release);
    thread_ = std::thread(&SubGhzWorker::runLoop, this);
}

void SubGhzWorker::stop() {
    stop_requested_.store(true, std::memory_order_release);
    cmd_cv_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
    running_.store(false, std::memory_order_release);
}

void SubGhzWorker::postCommand(WorkerCommand cmd) {
    {
        std::lock_guard<std::mutex> lock(cmd_mutex_);
        if (commands_.size() >= 32) {
            commands_.pop_front();
        }
        commands_.push_back(std::move(cmd));
    }
    cmd_cv_.notify_one();
}

bool SubGhzWorker::tryPopEvent(WorkerEvent& out_event) {
    std::lock_guard<std::mutex> lock(event_mutex_);
    if (events_.empty()) {
        return false;
    }
    out_event = std::move(events_.front());
    events_.pop_front();
    return true;
}

bool SubGhzWorker::tryPopCommand(WorkerCommand& out_cmd) {
    std::lock_guard<std::mutex> lock(cmd_mutex_);
    if (commands_.empty()) {
        return false;
    }
    out_cmd = std::move(commands_.front());
    commands_.pop_front();
    return true;
}

void SubGhzWorker::pushEvent(WorkerEvent ev) {
    std::lock_guard<std::mutex> lock(event_mutex_);
    if (events_.size() >= 128) {
        events_.pop_front();
    }
    events_.push_back(std::move(ev));
}

void SubGhzWorker::runLoop() {
    bool initialized = false;
    SetModeCommand current_cfg{};
    std::vector<int32_t> raw_buffer;
    std::size_t hop_index = 0;

    auto init_hardware = [&]() -> bool {
        try {
            pushEvent(WorkerStateEvent{WorkerMode::Idle, "Initializing CC1101...", backend_->hardwareInfo()});
            auto info = backend_->open(
                current_cfg.preset, current_cfg.frequency_hz, current_cfg.power_dbm, &stop_requested_);
            pushEvent(WorkerStateEvent{WorkerMode::Idle, "Ready", std::move(info)});
            return true;
        } catch (const RadioCancelled&) {
            return false;
        } catch (const std::exception& ex) {
            pushEvent(WorkerStateEvent{WorkerMode::Error, ex.what(), backend_->hardwareInfo()});
            return false;
        }
    };

    initialized = init_hardware();

    while (!stop_requested_.load(std::memory_order_acquire)) {
        WorkerCommand cmd;
        if (tryPopCommand(cmd)) {
            if (std::holds_alternative<RetryInitCommand>(cmd)) {
                initialized = init_hardware();
                continue;
            }
            if (auto* mode_cmd = std::get_if<SetModeCommand>(&cmd)) {
                const WorkerMode prev_mode = current_cfg.mode;
                const auto prev_cfg = current_cfg;
                current_cfg = *mode_cmd;

                if (!initialized) {
                    continue;
                }

                if (prev_mode == WorkerMode::ReadRaw && current_cfg.mode != WorkerMode::ReadRaw &&
                    !raw_buffer.empty()) {
                    model::SubGhzSignal raw_sig;
                    raw_sig.file_type = model::SubGhzFileType::RawFile;
                    raw_sig.protocol = "RAW";
                    raw_sig.frequency_hz = prev_cfg.frequency_hz;
                    raw_sig.preset = prev_cfg.preset;
                    raw_sig.raw_timings = std::move(raw_buffer);
                    raw_sig.timestamp_ms = monotonicMilliseconds();
                    raw_sig.name = "RAW_" + std::to_string(raw_sig.frequency_hz / 1000000U);
                    raw_buffer.clear();
                    pushEvent(RawCompleteEvent{std::move(raw_sig)});
                }

                try {
                    if (current_cfg.mode == WorkerMode::Idle) {
                        backend_->stopRx();
                        pushEvent(WorkerStateEvent{WorkerMode::Idle, "Radio Idle", backend_->hardwareInfo()});
                    } else if (current_cfg.mode == WorkerMode::Read) {
                        backend_->configure(current_cfg.preset, current_cfg.frequency_hz, {}, current_cfg.power_dbm);
                        backend_->startAsyncRx();
                        pushEvent(WorkerStateEvent{WorkerMode::Read,
                                                   current_cfg.hopping ? "Listening (Hopping)" : "Listening",
                                                   backend_->hardwareInfo()});
                    } else if (current_cfg.mode == WorkerMode::ReadRaw) {
                        raw_buffer.clear();
                        backend_->configure(current_cfg.preset, current_cfg.frequency_hz, {}, current_cfg.power_dbm);
                        backend_->startAsyncRx();
                        pushEvent(
                            WorkerStateEvent{WorkerMode::ReadRaw, "Recording RAW...", backend_->hardwareInfo()});
                    } else if (current_cfg.mode == WorkerMode::FrequencyAnalyzer) {
                        backend_->configure(
                            model::SubGhzPreset::Ook650Async, current_cfg.frequency_hz, {}, current_cfg.power_dbm);
                        backend_->startAsyncRx();
                        pushEvent(WorkerStateEvent{
                            WorkerMode::FrequencyAnalyzer, "Scanning bands...", backend_->hardwareInfo()});
                    }
                } catch (const std::exception& ex) {
                    initialized = false;
                    pushEvent(WorkerStateEvent{WorkerMode::Error, ex.what(), backend_->hardwareInfo()});
                }
                continue;
            }
            if (auto* tx_cmd = std::get_if<TransmitSignalCommand>(&cmd)) {
                if (!initialized) {
                    pushEvent(TxFinishedEvent{false, "CC1101 not initialized"});
                    continue;
                }
                try {
                    const auto& sig = tx_cmd->signal;
                    pushEvent(WorkerStateEvent{WorkerMode::Transmitting,
                                               "TX " + model::format_frequency_mhz(sig.frequency_hz) + " (" +
                                                   model::preset_to_short_name(sig.preset) + ")",
                                               backend_->hardwareInfo()});
                    backend_->configure(
                        sig.preset, sig.frequency_hz, sig.custom_preset_data, current_cfg.power_dbm);
                    const auto timings =
                        sig.raw_timings.empty() ? model::encode_protocol_timings(sig) : sig.raw_timings;
                    backend_->transmitTimings(timings, tx_cmd->repeat_count, &stop_requested_);
                    pushEvent(TxFinishedEvent{
                        true, "Sent " + sig.protocol + " @ " + model::format_frequency_mhz(sig.frequency_hz)});

                    // Restore previous RX mode if active
                    if (current_cfg.mode == WorkerMode::Read || current_cfg.mode == WorkerMode::ReadRaw) {
                        backend_->configure(
                            current_cfg.preset, current_cfg.frequency_hz, {}, current_cfg.power_dbm);
                        backend_->startAsyncRx();
                        pushEvent(WorkerStateEvent{current_cfg.mode, "Listening", backend_->hardwareInfo()});
                    } else {
                        backend_->stopRx();
                        current_cfg.mode = WorkerMode::Idle;
                        pushEvent(WorkerStateEvent{WorkerMode::Idle, "Ready", backend_->hardwareInfo()});
                    }
                } catch (const RadioCancelled&) {
                    break;
                } catch (const std::exception& ex) {
                    pushEvent(TxFinishedEvent{false, std::string("TX failed: ") + ex.what()});
                    pushEvent(WorkerStateEvent{WorkerMode::Idle, "Ready", backend_->hardwareInfo()});
                }
                continue;
            }
        }

        if (!initialized || current_cfg.mode == WorkerMode::Idle || current_cfg.mode == WorkerMode::Error) {
            std::unique_lock<std::mutex> lock(cmd_mutex_);
            cmd_cv_.wait_for(lock, std::chrono::milliseconds(60), [this]() {
                return stop_requested_.load(std::memory_order_acquire) || !commands_.empty();
            });
            continue;
        }

        try {
            if (current_cfg.mode == WorkerMode::Read) {
                if (current_cfg.hopping) {
                    current_cfg.frequency_hz =
                        model::kHopperFrequenciesHz[hop_index % model::kHopperFrequenciesHz.size()];
                    ++hop_index;
                    backend_->tuneFrequency(current_cfg.frequency_hz);
                }
                const float rssi = backend_->readRssiDbm();
                pushEvent(RssiSampleEvent{current_cfg.frequency_hz, rssi});

                std::vector<int32_t> pulses;
                if (backend_->readPulses(pulses, 35) > 0) {
                    if (auto decoded = model::decode_pulse_stream(
                            pulses, current_cfg.frequency_hz, current_cfg.preset, rssi, true)) {
                        decoded->timestamp_ms = monotonicMilliseconds();
                        pushEvent(DecodedSignalEvent{std::move(*decoded)});
                    }
                }
            } else if (current_cfg.mode == WorkerMode::ReadRaw) {
                const float rssi = backend_->readRssiDbm();
                std::vector<int32_t> pulses;
                backend_->readPulses(pulses, 30);
                if (rssi >= current_cfg.rssi_threshold_dbm && !pulses.empty()) {
                    for (int32_t t : pulses) {
                        if (raw_buffer.size() < 16384) {
                            if (!raw_buffer.empty() || t > 0) {
                                raw_buffer.push_back(t);
                            }
                        }
                    }
                }
                uint64_t duration_us = 0;
                for (int32_t t : raw_buffer) {
                    duration_us += static_cast<uint64_t>(t < 0 ? -t : t);
                }
                pushEvent(RawProgressEvent{raw_buffer.size(), duration_us, rssi});
            } else if (current_cfg.mode == WorkerMode::FrequencyAnalyzer) {
                uint32_t best_freq = model::kStandardFrequenciesHz[0];
                float best_rssi = -120.0f;
                for (uint32_t freq : model::kStandardFrequenciesHz) {
                    if (stop_requested_.load(std::memory_order_acquire)) break;
                    backend_->tuneFrequency(freq);
                    const float rssi = backend_->readRssiDbm();
                    if (rssi > best_rssi) {
                        best_rssi = rssi;
                        best_freq = freq;
                    }
                }
                const bool locked = best_rssi >= -68.0f;
                pushEvent(AnalyzerReadingEvent{best_freq, best_rssi, locked});
                std::this_thread::sleep_for(std::chrono::milliseconds(45));
            }
        } catch (const RadioCancelled&) {
            break;
        } catch (const std::exception& ex) {
            initialized = false;
            pushEvent(WorkerStateEvent{WorkerMode::Error, ex.what(), backend_->hardwareInfo()});
        }
    }

    backend_->close();
}

}  // namespace radio
