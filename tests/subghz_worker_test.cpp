/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "subghz_protocols.h"
#include "subghz_worker.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}

}  // namespace

int main() {
    using namespace radio;
    using namespace model;

    SubGhzWorker worker;
    worker.start();

    // 1. Wait for initial Ready state event
    bool got_ready = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline && !got_ready) {
        WorkerEvent ev;
        while (worker.tryPopEvent(ev)) {
            if (auto* st = std::get_if<WorkerStateEvent>(&ev)) {
                if (st->status_text == "Ready") {
                    got_ready = true;
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    require(got_ready, "SubGhzWorker did not emit Ready state");

    // 2. Test transmitting a Princeton signal
    SubGhzSignal sig;
    sig.protocol = "Princeton";
    sig.bit_count = 24;
    sig.te_us = 400;
    sig.key = 0x55AA3CU;
    sig.frequency_hz = 433920000U;
    sig.preset = SubGhzPreset::Ook650Async;
    sig.raw_timings = encode_protocol_timings(sig);

    TransmitSignalCommand tx_cmd;
    tx_cmd.signal = sig;
    tx_cmd.repeat_count = 2;
    worker.postCommand(tx_cmd);

    bool got_tx_done = false;
    const auto tx_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < tx_deadline && !got_tx_done) {
        WorkerEvent ev;
        while (worker.tryPopEvent(ev)) {
            if (auto* tx = std::get_if<TxFinishedEvent>(&ev)) {
                require(tx->success, "Transmit failed: " + tx->message);
                got_tx_done = true;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    require(got_tx_done, "SubGhzWorker did not finish transmission");

    // 3. Test Frequency Analyzer mode
    SetModeCommand ana_cmd;
    ana_cmd.mode = WorkerMode::FrequencyAnalyzer;
    worker.postCommand(ana_cmd);

    bool got_ana = false;
    const auto ana_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < ana_deadline && !got_ana) {
        WorkerEvent ev;
        while (worker.tryPopEvent(ev)) {
            if (std::holds_alternative<AnalyzerReadingEvent>(ev)) {
                got_ana = true;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    require(got_ana, "SubGhzWorker did not emit AnalyzerReadingEvent");

    worker.stop();
    std::cout << "subghz_worker_test passed\n";
    return 0;
}
