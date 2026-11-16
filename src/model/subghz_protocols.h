/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "subghz_types.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace model {

// Synthesizes a single frame of microsecond pulse/space timings (+HIGH, -LOW)
// for a given SubGhzSignal (RAW, BinRAW, or parsed protocol key).
// Always begins with a positive timing and ends with a negative guard space.
std::vector<int32_t> encode_protocol_timings(const SubGhzSignal& signal);

// Attempts to decode a captured sequence of signed microsecond timings (+HIGH, -LOW)
// into a known Sub-GHz protocol (Princeton, CAME, Nice FLO, GateTX, Linear,
// Chamberlain, Holtek, KeeLoq) or BinRAW fallback.
std::optional<SubGhzSignal> decode_pulse_stream(const std::vector<int32_t>& timings,
                                                uint32_t frequency_hz,
                                                SubGhzPreset preset,
                                                float rssi_dbm = -60.0f,
                                                bool allow_binraw_fallback = false);

// List of protocols available in the "Add Manually" screen.
const std::vector<ManualProtocolSpec>& manual_protocol_specs();

}  // namespace model
