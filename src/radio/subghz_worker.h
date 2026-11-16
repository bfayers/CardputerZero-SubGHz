/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "subghz_backend.h"
#include "subghz_types.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <variant>
#include <vector>

namespace radio {

enum class WorkerMode : int32_t {
    Idle              = 0,
    Read              = 1,
    ReadRaw           = 2,
    FrequencyAnalyzer = 3,
    Transmitting      = 4,
    Error             = 5,
};

struct SetModeCommand {
    WorkerMode mode{WorkerMode::Idle};
    uint32_t frequency_hz{433920000U};
    model::SubGhzPreset preset{model::SubGhzPreset::Ook650Async};
    bool hopping{false};
    float rssi_threshold_dbm{-72.0f};
    int8_t power_dbm{10};
};

struct TransmitSignalCommand {
    model::SubGhzSignal signal;
    uint32_t repeat_count{5};
};

struct RetryInitCommand {};

using WorkerCommand = std::variant<SetModeCommand, TransmitSignalCommand, RetryInitCommand>;

struct WorkerStateEvent {
    WorkerMode mode{WorkerMode::Idle};
    std::string status_text;
    RadioHardwareInfo hw_info;
};

struct RssiSampleEvent {
    uint32_t frequency_hz{433920000U};
    float rssi_dbm{-95.0f};
};

struct DecodedSignalEvent {
    model::SubGhzSignal signal;
};

struct RawProgressEvent {
    size_t pulse_count{0};
    uint64_t duration_us{0};
    float rssi_dbm{-95.0f};
};

struct RawCompleteEvent {
    model::SubGhzSignal raw_signal;
};

struct AnalyzerReadingEvent {
    uint32_t best_frequency_hz{433920000U};
    float best_rssi_dbm{-95.0f};
    bool locked{false};
};

struct TxFinishedEvent {
    bool success{true};
    std::string message;
};

using WorkerEvent = std::variant<WorkerStateEvent,
                                 RssiSampleEvent,
                                 DecodedSignalEvent,
                                 RawProgressEvent,
                                 RawCompleteEvent,
                                 AnalyzerReadingEvent,
                                 TxFinishedEvent>;

class SubGhzWorker {
public:
    SubGhzWorker();
    explicit SubGhzWorker(std::unique_ptr<SubGhzBackend> backend);
    ~SubGhzWorker();

    SubGhzWorker(const SubGhzWorker&)            = delete;
    SubGhzWorker& operator=(const SubGhzWorker&) = delete;

    void start();
    void stop();

    void postCommand(WorkerCommand cmd);
    bool tryPopEvent(WorkerEvent& out_event);

private:
    std::unique_ptr<SubGhzBackend> backend_;
    std::thread thread_;
    std::atomic_bool running_{false};
    std::atomic_bool stop_requested_{false};

    std::mutex cmd_mutex_;
    std::condition_variable cmd_cv_;
    std::deque<WorkerCommand> commands_;

    std::mutex event_mutex_;
    std::deque<WorkerEvent> events_;

    void runLoop();
    bool tryPopCommand(WorkerCommand& out_cmd);
    void pushEvent(WorkerEvent ev);
};

}  // namespace radio
