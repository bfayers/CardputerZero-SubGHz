/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "subghz_protocols.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}

}  // namespace

int main() {
    using namespace model;

    for (const auto& spec : manual_protocol_specs()) {
        SubGhzSignal input;
        input.protocol = spec.name;
        input.bit_count = spec.bit_count;
        input.te_us = spec.default_te_us;
        input.key = spec.default_key;
        input.frequency_hz = 433920000U;
        input.preset = SubGhzPreset::Ook650Async;

        const auto timings = encode_protocol_timings(input);
        require(!timings.empty(), std::string("Empty timings for ") + spec.name);
        require(timings.front() > 0, std::string("Timings must start positive for ") + spec.name);
        require(timings.back() < 0, std::string("Timings must end negative for ") + spec.name);

        const auto decoded = decode_pulse_stream(timings, input.frequency_hz, input.preset, -55.0f, false);
        require(decoded.has_value(), std::string("Failed to decode synthesized ") + spec.name);
        require(decoded->protocol == spec.name,
                std::string("Decoded protocol mismatch: expected ") + spec.name + ", got " + decoded->protocol);
        require(decoded->bit_count == spec.bit_count,
                std::string("Decoded bit count mismatch for ") + spec.name);
        require(decoded->key == spec.default_key,
                std::string("Decoded key mismatch for ") + spec.name);
    }

    std::cout << "subghz_protocols_test passed\n";
    return 0;
}
