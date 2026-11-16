/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "subghz_protocols.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace model {
namespace {

bool iequals(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        char ca = a[i];
        char cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb - 'A' + 'a');
        if (ca != cb) {
            return false;
        }
    }
    return true;
}

bool near_duration(int32_t measured, int32_t expected, int32_t tolerance_pct = 32) noexcept {
    const int32_t diff = std::abs(measured - expected);
    const int32_t max_diff = std::max<int32_t>(70, (expected * tolerance_pct) / 100);
    return diff <= max_diff;
}

bool get_signal_bit(const SubGhzSignal& signal, uint32_t bit_index_from_msb, uint32_t total_bits) noexcept {
    if (total_bits == 0 || bit_index_from_msb >= total_bits) {
        return false;
    }
    const uint32_t bit_from_lsb = total_bits - 1U - bit_index_from_msb;
    if (total_bits <= 64 && (signal.key != 0 || signal.key_bytes.empty())) {
        return ((signal.key >> bit_from_lsb) & 1ULL) != 0;
    }
    if (!signal.key_bytes.empty()) {
        const std::size_t total_byte_bits = static_cast<std::size_t>(signal.key_bytes.size() * 8U);
        if (bit_from_lsb >= total_byte_bits) {
            return false;
        }
        const std::size_t byte_idx = signal.key_bytes.size() - 1U - (bit_from_lsb / 8U);
        const uint32_t bit_in_byte = bit_from_lsb % 8U;
        return ((signal.key_bytes[byte_idx] >> bit_in_byte) & 1U) != 0;
    }
    return false;
}

void append_coalesced(std::vector<int32_t>& timings, int32_t duration) {
    if (duration == 0) {
        return;
    }
    if (!timings.empty()) {
        if ((timings.back() > 0 && duration > 0) || (timings.back() < 0 && duration < 0)) {
            timings.back() += duration;
            return;
        }
    } else if (duration < 0) {
        // Flipper RAW format requires sequences to start with a positive pulse.
        return;
    }
    timings.push_back(duration);
}

std::vector<int32_t> encode_binraw(const SubGhzSignal& signal) {
    std::vector<int32_t> out;
    const int32_t te = static_cast<int32_t>(signal.te_us > 0 ? signal.te_us : 400U);
    const uint32_t total_bits =
        signal.bit_raw > 0 ? signal.bit_raw
                           : (signal.bit_count > 0 ? signal.bit_count
                                                   : static_cast<uint32_t>(signal.data_raw.size() * 8U));
    const uint32_t max_bits = static_cast<uint32_t>(signal.data_raw.size() * 8U);
    const uint32_t count = std::min(total_bits, max_bits);
    out.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t byte = signal.data_raw[i / 8U];
        const bool high = ((byte >> (7U - (i % 8U))) & 1U) != 0;
        append_coalesced(out, high ? te : -te);
    }
    if (!out.empty() && out.back() > 0) {
        append_coalesced(out, -te * 10);
    }
    return out;
}

std::vector<int32_t> encode_princeton(const SubGhzSignal& signal) {
    std::vector<int32_t> out;
    const int32_t te = static_cast<int32_t>(signal.te_us > 0 ? signal.te_us : 400U);
    const uint32_t bits = signal.bit_count > 0 ? signal.bit_count : 24U;
    out.reserve(bits * 2U + 2U);
    for (uint32_t i = 0; i < bits; ++i) {
        if (get_signal_bit(signal, i, bits)) {
            out.push_back(3 * te);
            out.push_back(-1 * te);
        } else {
            out.push_back(1 * te);
            out.push_back(-3 * te);
        }
    }
    // Sync pulse: 1 TE high, 31 TE low (or custom guard_time)
    const int32_t guard_multiple =
        signal.guard_time_us >= 10 && signal.guard_time_us <= 100 ? static_cast<int32_t>(signal.guard_time_us) : 31;
    out.push_back(1 * te);
    out.push_back(-guard_multiple * te);
    return out;
}

// Pilot-first encoder used by CAME, Nice FLO, and GateTX:
// Starts with +1*TE pilot pulse, then each bit 0 is (-1*TE, +2*TE), bit 1 is (-2*TE, +1*TE),
// followed by -guard_mult*TE space.
std::vector<int32_t> encode_pilot_pwm(const SubGhzSignal& signal,
                                      uint32_t default_te_us,
                                      uint32_t default_bits,
                                      int32_t guard_mult) {
    std::vector<int32_t> out;
    const int32_t te = static_cast<int32_t>(signal.te_us > 0 ? signal.te_us : default_te_us);
    const uint32_t bits = signal.bit_count > 0 ? signal.bit_count : default_bits;
    out.reserve(bits * 2U + 2U);
    out.push_back(1 * te);  // Pilot pulse
    for (uint32_t i = 0; i < bits; ++i) {
        if (get_signal_bit(signal, i, bits)) {
            out.push_back(-2 * te);
            out.push_back(1 * te);
        } else {
            out.push_back(-1 * te);
            out.push_back(2 * te);
        }
    }
    out.push_back(-guard_mult * te);
    return out;
}

std::vector<int32_t> encode_linear(const SubGhzSignal& signal) {
    std::vector<int32_t> out;
    const int32_t te = static_cast<int32_t>(signal.te_us > 0 ? signal.te_us : 500U);
    const uint32_t bits = signal.bit_count > 0 ? signal.bit_count : 10U;
    out.reserve(bits * 2U);
    for (uint32_t i = 0; i < bits; ++i) {
        if (get_signal_bit(signal, i, bits)) {
            out.push_back(3 * te);
            out.push_back(-1 * te);
        } else {
            out.push_back(1 * te);
            out.push_back(-3 * te);
        }
    }
    if (!out.empty() && out.back() < 0) {
        out.back() -= 38 * te;
    }
    return out;
}

std::vector<int32_t> encode_chamberlain(const SubGhzSignal& signal) {
    std::vector<int32_t> out;
    const int32_t te = static_cast<int32_t>(signal.te_us > 0 ? signal.te_us : 1000U);
    const uint32_t bits = signal.bit_count > 0 ? signal.bit_count : 9U;
    out.reserve(bits * 2U + 2U);
    // Sync symbol: +3*TE, -1*TE
    out.push_back(3 * te);
    out.push_back(-1 * te);
    for (uint32_t i = 0; i < bits; ++i) {
        if (get_signal_bit(signal, i, bits)) {
            out.push_back(2 * te);
            out.push_back(-2 * te);
        } else {
            out.push_back(1 * te);
            out.push_back(-3 * te);
        }
    }
    out.back() -= 36 * te;
    return out;
}

std::vector<int32_t> encode_holtek(const SubGhzSignal& signal) {
    std::vector<int32_t> out;
    const int32_t te = static_cast<int32_t>(signal.te_us > 0 ? signal.te_us : 430U);
    const uint32_t bits = signal.bit_count > 0 ? signal.bit_count : 12U;
    out.reserve(bits * 2U + 2U);
    out.push_back(1 * te);  // Pilot pulse
    for (uint32_t i = 0; i < bits; ++i) {
        if (get_signal_bit(signal, i, bits)) {
            out.push_back(-1 * te);
            out.push_back(2 * te);
        } else {
            out.push_back(-2 * te);
            out.push_back(1 * te);
        }
    }
    out.push_back(-36 * te);
    return out;
}

std::vector<int32_t> encode_keeloq(const SubGhzSignal& signal) {
    std::vector<int32_t> out;
    const int32_t te = static_cast<int32_t>(signal.te_us > 0 ? signal.te_us : 400U);
    const uint32_t bits = signal.bit_count > 0 ? signal.bit_count : 64U;
    out.reserve(24U + bits * 2U);
    // 11x (1 TE high, 1 TE low) + 1x (1 TE high, 10 TE low header)
    for (int i = 0; i < 11; ++i) {
        out.push_back(te);
        out.push_back(-te);
    }
    out.push_back(te);
    out.push_back(-10 * te);
    // Data bits: 0 = (+2*TE, -1*TE), 1 = (+1*TE, -2*TE)
    for (uint32_t i = 0; i < bits; ++i) {
        if (get_signal_bit(signal, i, bits)) {
            out.push_back(1 * te);
            out.push_back(-2 * te);
        } else {
            out.push_back(2 * te);
            out.push_back(-1 * te);
        }
    }
    out.back() -= 38 * te;
    return out;
}

std::optional<SubGhzSignal> try_decode_princeton(const std::vector<int32_t>& timings) {
    // Look for N pairs of (1:3 or 3:1) followed by (1:31) sync
    for (std::size_t start = 0; start + 48 < timings.size(); ++start) {
        if (timings[start] <= 0) {
            continue;
        }
        // Estimate TE from first pair: total period of a bit is 4*TE
        const int32_t p0 = timings[start];
        const int32_t s0 = -timings[start + 1];
        if (s0 <= 0) {
            continue;
        }
        const int32_t te = (p0 + s0) / 4;
        if (te < 150 || te > 1100) {
            continue;
        }

        uint64_t key = 0;
        uint32_t bits = 0;
        std::size_t idx = start;
        while (idx + 1 < timings.size() && bits < 64) {
            const int32_t p = timings[idx];
            const int32_t s = -timings[idx + 1];
            if (p <= 0 || s <= 0) {
                break;
            }
            // Check sync: 1*TE high, >= 20*TE low
            if (near_duration(p, te, 38) && s >= 20 * te && s <= 48 * te) {
                if (bits >= 16) {
                    SubGhzSignal sig;
                    sig.protocol = "Princeton";
                    sig.bit_count = bits;
                    sig.key = key;
                    sig.te_us = static_cast<uint32_t>(te);
                    sig.guard_time_us = static_cast<uint32_t>((s + te / 2) / te);
                    return sig;
                }
                break;
            }
            if (near_duration(p, te, 35) && near_duration(s, 3 * te, 35)) {
                key = (key << 1U);
                ++bits;
                idx += 2;
            } else if (near_duration(p, 3 * te, 35) && near_duration(s, te, 35)) {
                key = (key << 1U) | 1ULL;
                ++bits;
                idx += 2;
            } else {
                break;
            }
        }
    }
    return std::nullopt;
}

std::optional<SubGhzSignal> try_decode_pilot_pwm(const std::vector<int32_t>& timings) {
    // CAME (TE~320, 12/24 bits), GateTX (TE~350, 24 bits), Nice FLO (TE~700, 12/24 bits)
    // Pattern: +1*TE pilot, then N pairs of (-1*TE, +2*TE => 0) or (-2*TE, +1*TE => 1), then -guard (<= -25*TE)
    for (std::size_t start = 0; start + 24 < timings.size(); ++start) {
        const int32_t pilot = timings[start];
        if (pilot < 200 || pilot > 1000) {
            continue;
        }
        // Estimate TE from first bit after pilot
        const int32_t s0 = -timings[start + 1];
        const int32_t p0 = timings[start + 2];
        if (s0 <= 0 || p0 <= 0) {
            continue;
        }
        const int32_t te = (s0 + p0) / 3;
        if (te < 220 || te > 950 || !near_duration(pilot, te, 40)) {
            continue;
        }

        uint64_t key = 0;
        uint32_t bits = 0;
        std::size_t idx = start + 1;
        while (idx < timings.size() && bits < 64) {
            const int32_t s = -timings[idx];
            if (s >= 22 * te) {
                break;  // Reached trailing guard space
            }
            if (idx + 1 >= timings.size()) {
                break;
            }
            const int32_t p = timings[idx + 1];
            if (s <= 0 || p <= 0) {
                break;
            }
            if (near_duration(s, te, 35) && near_duration(p, 2 * te, 35)) {
                key = (key << 1U);
                ++bits;
                idx += 2;
            } else if (near_duration(s, 2 * te, 35) && near_duration(p, te, 35)) {
                key = (key << 1U) | 1ULL;
                ++bits;
                idx += 2;
            } else {
                bits = 0;
                break;
            }
        }

        if (bits == 12 || bits == 24 || bits == 32) {
            SubGhzSignal sig;
            sig.bit_count = bits;
            sig.key = key;
            sig.te_us = static_cast<uint32_t>(te);
            if (te >= 520) {
                sig.protocol = "Nice FLO";
            } else if (bits == 24 && te >= 335 && te <= 390) {
                sig.protocol = "GateTX";
            } else {
                sig.protocol = "CAME";
            }
            return sig;
        }
    }
    return std::nullopt;
}

std::optional<SubGhzSignal> try_decode_holtek(const std::vector<int32_t>& timings) {
    // Holtek: +1*TE pilot, then bit 0 = (-2*TE, +1*TE), bit 1 = (-1*TE, +2*TE), guard <= -25*TE, TE ~ 430 us
    for (std::size_t start = 0; start + 24 < timings.size(); ++start) {
        const int32_t pilot = timings[start];
        if (pilot < 300 || pilot > 600) {
            continue;
        }
        const int32_t s0 = -timings[start + 1];
        const int32_t p0 = timings[start + 2];
        if (s0 <= 0 || p0 <= 0) {
            continue;
        }
        const int32_t te = (s0 + p0) / 3;
        if (te < 380 || te > 500 || !near_duration(pilot, te, 35)) {
            continue;
        }

        uint64_t key = 0;
        uint32_t bits = 0;
        std::size_t idx = start + 1;
        while (idx < timings.size() && bits < 64) {
            const int32_t s = -timings[idx];
            if (s >= 22 * te) {
                break;
            }
            if (idx + 1 >= timings.size()) {
                break;
            }
            const int32_t p = timings[idx + 1];
            if (s <= 0 || p <= 0) {
                break;
            }
            if (near_duration(s, 2 * te, 32) && near_duration(p, te, 32)) {
                key = (key << 1U);
                ++bits;
                idx += 2;
            } else if (near_duration(s, te, 32) && near_duration(p, 2 * te, 32)) {
                key = (key << 1U) | 1ULL;
                ++bits;
                idx += 2;
            } else {
                bits = 0;
                break;
            }
        }
        if (bits == 12 || bits == 32 || bits == 40) {
            SubGhzSignal sig;
            sig.protocol = "Holtek";
            sig.bit_count = bits;
            sig.key = key;
            sig.te_us = static_cast<uint32_t>(te);
            return sig;
        }
    }
    return std::nullopt;
}

std::optional<SubGhzSignal> try_decode_keeloq(const std::vector<int32_t>& timings) {
    // KeeLoq: preamble of >= 8 (1*TE, -1*TE) + (1*TE, -10*TE) header + 64..66 bits of (2*TE,-1*TE => 0) / (1*TE,-2*TE => 1)
    for (std::size_t i = 0; i + 130 <= timings.size(); ++i) {
        const int32_t p_hdr = timings[i];
        const int32_t s_hdr = -timings[i + 1];
        if (p_hdr < 250 || p_hdr > 600 || s_hdr < 7 * p_hdr || s_hdr > 14 * p_hdr) {
            continue;
        }
        const int32_t te = p_hdr;
        std::size_t idx = i + 2;
        uint64_t key = 0;
        uint32_t bits = 0;
        while (idx + 1 < timings.size() && bits < 64) {
            const int32_t p = timings[idx];
            const int32_t s = -timings[idx + 1];
            if (p <= 0 || s <= 0) {
                break;
            }
            const bool last_bit = (bits == 63) && s >= 10 * te;
            if (near_duration(p, 2 * te, 35) && (near_duration(s, te, 35) || last_bit)) {
                key = (key << 1U);
                ++bits;
                idx += 2;
            } else if (near_duration(p, te, 35) && (near_duration(s, 2 * te, 35) || last_bit)) {
                key = (key << 1U) | 1ULL;
                ++bits;
                idx += 2;
            } else {
                break;
            }
        }
        if (bits == 64) {
            SubGhzSignal sig;
            sig.protocol = "KeeLoq";
            sig.bit_count = 64;
            sig.key = key;
            sig.te_us = static_cast<uint32_t>(te);
            return sig;
        }
    }
    return std::nullopt;
}

std::optional<SubGhzSignal> try_decode_chamberlain(const std::vector<int32_t>& timings) {
    // Chamberlain: sync (+3*TE, -1*TE), then 7..10 bits of (+1*TE, -3*TE => 0) or (+2*TE, -2*TE => 1), TE ~ 1000 us
    for (std::size_t start = 0; start + 16 <= timings.size(); ++start) {
        const int32_t p_sync = timings[start];
        const int32_t s_sync = -timings[start + 1];
        if (p_sync < 2000 || p_sync > 4000 || s_sync < 650 || s_sync > 1400) {
            continue;
        }
        const int32_t te = s_sync;
        if (!near_duration(p_sync, 3 * te, 30)) {
            continue;
        }
        uint64_t key = 0;
        uint32_t bits = 0;
        std::size_t idx = start + 2;
        while (idx + 1 < timings.size() && bits < 12) {
            const int32_t p = timings[idx];
            const int32_t s = -timings[idx + 1];
            if (p <= 0 || s <= 0) {
                break;
            }
            const bool trailing_guard = s >= 20 * te;
            if (near_duration(p, te, 32) && (near_duration(s, 3 * te, 32) || trailing_guard)) {
                key = (key << 1U);
                ++bits;
                idx += 2;
                if (trailing_guard) break;
            } else if (near_duration(p, 2 * te, 32) && (near_duration(s, 2 * te, 32) || trailing_guard)) {
                key = (key << 1U) | 1ULL;
                ++bits;
                idx += 2;
                if (trailing_guard) break;
            } else {
                break;
            }
        }
        if (bits >= 7 && bits <= 10) {
            SubGhzSignal sig;
            sig.protocol = "Chamberlain";
            sig.bit_count = bits;
            sig.key = key;
            sig.te_us = static_cast<uint32_t>(te);
            return sig;
        }
    }
    return std::nullopt;
}

std::optional<SubGhzSignal> try_decode_linear(const std::vector<int32_t>& timings) {
    // Linear 10-bit: 10 bits of (+1*TE, -3*TE => 0) or (+3*TE, -1*TE => 1), last low has guard (~39*TE), TE ~ 500 us
    for (std::size_t start = 0; start + 20 <= timings.size(); ++start) {
        if (timings[start] <= 0) {
            continue;
        }
        const int32_t p0 = timings[start];
        const int32_t s0 = -timings[start + 1];
        if (s0 <= 0) {
            continue;
        }
        const int32_t te = (p0 + s0) / 4;
        if (te < 380 || te > 650) {
            continue;
        }
        uint64_t key = 0;
        uint32_t bits = 0;
        std::size_t idx = start;
        while (idx + 1 < timings.size() && bits < 12) {
            const int32_t p = timings[idx];
            const int32_t s = -timings[idx + 1];
            if (p <= 0 || s <= 0) {
                break;
            }
            const bool trailing_guard = s >= 20 * te;
            if (near_duration(p, te, 32) && (near_duration(s, 3 * te, 32) || trailing_guard)) {
                key = (key << 1U);
                ++bits;
                idx += 2;
                if (trailing_guard) break;
            } else if (near_duration(p, 3 * te, 32) && (near_duration(s, te, 32) || trailing_guard)) {
                key = (key << 1U) | 1ULL;
                ++bits;
                idx += 2;
                if (trailing_guard) break;
            } else {
                break;
            }
        }
        if (bits == 10 || bits == 8) {
            SubGhzSignal sig;
            sig.protocol = "Linear";
            sig.bit_count = bits;
            sig.key = key;
            sig.te_us = static_cast<uint32_t>(te);
            return sig;
        }
    }
    return std::nullopt;
}

}  // namespace

bool parse_preset_name(std::string_view text, SubGhzPreset& out_preset) noexcept {
    if (iequals(text, "FuriHalSubGhzPresetOok270Async") || iequals(text, "AM270") || iequals(text, "OOK270")) {
        out_preset = SubGhzPreset::Ook270Async;
        return true;
    }
    if (iequals(text, "FuriHalSubGhzPresetOok650Async") || iequals(text, "FuriHalSubGhzPresetOok65Async") ||
        iequals(text, "AM650") || iequals(text, "OOK650")) {
        out_preset = SubGhzPreset::Ook650Async;
        return true;
    }
    if (iequals(text, "FuriHalSubGhzPreset2FSKDev238Async") || iequals(text, "FM238") || iequals(text, "2FSK238")) {
        out_preset = SubGhzPreset::Fsk2Dev238Async;
        return true;
    }
    if (iequals(text, "FuriHalSubGhzPreset2FSKDev476Async") || iequals(text, "FM476") || iequals(text, "2FSK476")) {
        out_preset = SubGhzPreset::Fsk2Dev476Async;
        return true;
    }
    if (iequals(text, "FuriHalSubGhzPresetCustom") || iequals(text, "CUSTOM")) {
        out_preset = SubGhzPreset::Custom;
        return true;
    }
    return false;
}

std::string format_frequency_mhz(uint32_t freq_hz) {
    char buf[32]{};
    const double mhz = static_cast<double>(freq_hz) / 1000000.0;
    std::snprintf(buf, sizeof(buf), "%.2f MHz", mhz);
    return buf;
}

std::string format_key_hex(uint64_t key, uint32_t bit_count, const std::vector<uint8_t>& key_bytes) {
    std::ostringstream out;
    if (!key_bytes.empty() && bit_count > 64) {
        for (std::size_t i = 0; i < key_bytes.size(); ++i) {
            if (i > 0) out << ' ';
            char b[4]{};
            std::snprintf(b, sizeof(b), "%02X", static_cast<unsigned>(key_bytes[i]));
            out << b;
        }
        return out.str();
    }
    // Flipper SubGhz Key File always formats standard keys as 8 space-separated hex bytes
    for (int i = 7; i >= 0; --i) {
        if (i < 7) out << ' ';
        const uint8_t byte = static_cast<uint8_t>((key >> (i * 8)) & 0xFFU);
        char b[4]{};
        std::snprintf(b, sizeof(b), "%02X", static_cast<unsigned>(byte));
        out << b;
    }
    return out.str();
}

std::vector<int32_t> encode_protocol_timings(const SubGhzSignal& signal) {
    if (iequals(signal.protocol, "RAW") || !signal.raw_timings.empty()) {
        std::vector<int32_t> cleaned;
        cleaned.reserve(signal.raw_timings.size());
        for (int32_t t : signal.raw_timings) {
            append_coalesced(cleaned, t);
        }
        if (!cleaned.empty() && cleaned.back() > 0) {
            cleaned.push_back(-1000);
        }
        return cleaned;
    }

    if (iequals(signal.protocol, "BinRAW") && !signal.data_raw.empty()) {
        return encode_binraw(signal);
    }
    if (iequals(signal.protocol, "Princeton")) {
        return encode_princeton(signal);
    }
    if (iequals(signal.protocol, "CAME") || iequals(signal.protocol, "Came")) {
        return encode_pilot_pwm(signal, 320U, 12U, 36);
    }
    if (iequals(signal.protocol, "Nice FLO") || iequals(signal.protocol, "Nice_FLO")) {
        return encode_pilot_pwm(signal, 700U, 12U, 36);
    }
    if (iequals(signal.protocol, "GateTX") || iequals(signal.protocol, "Gate_TX")) {
        return encode_pilot_pwm(signal, 350U, 24U, 49);
    }
    if (iequals(signal.protocol, "Linear") || iequals(signal.protocol, "Linear Delta3")) {
        return encode_linear(signal);
    }
    if (iequals(signal.protocol, "Chamberlain")) {
        return encode_chamberlain(signal);
    }
    if (iequals(signal.protocol, "Holtek") || iequals(signal.protocol, "Holtek_HT12X")) {
        return encode_holtek(signal);
    }
    if (iequals(signal.protocol, "KeeLoq")) {
        return encode_keeloq(signal);
    }

    // Generic fallback for any other Flipper Sub-GHz protocol with Key + Bit
    return encode_princeton(signal);
}

std::optional<SubGhzSignal> decode_pulse_stream(const std::vector<int32_t>& timings,
                                                uint32_t frequency_hz,
                                                SubGhzPreset preset,
                                                float rssi_dbm,
                                                bool allow_binraw_fallback) {
    if (timings.size() < 16) {
        return std::nullopt;
    }

    std::optional<SubGhzSignal> result;
    if (!result) result = try_decode_keeloq(timings);
    if (!result) result = try_decode_princeton(timings);
    if (!result) result = try_decode_holtek(timings);
    if (!result) result = try_decode_pilot_pwm(timings);
    if (!result) result = try_decode_chamberlain(timings);
    if (!result) result = try_decode_linear(timings);

    if (!result && allow_binraw_fallback && timings.size() >= 24) {
        // Estimate shortest pulse as TE and quantize into BinRAW
        int32_t min_pulse = 100000;
        for (int32_t t : timings) {
            const int32_t mag = std::abs(t);
            if (mag >= 150 && mag < min_pulse) {
                min_pulse = mag;
            }
        }
        if (min_pulse >= 150 && min_pulse <= 1500) {
            SubGhzSignal bin;
            bin.protocol = "BinRAW";
            bin.te_us = static_cast<uint32_t>(min_pulse);
            std::vector<bool> bits;
            for (int32_t t : timings) {
                const bool high = t > 0;
                const uint32_t count = std::min<uint32_t>(64U, static_cast<uint32_t>((std::abs(t) + min_pulse / 2) / min_pulse));
                for (uint32_t k = 0; k < count && bits.size() < 512; ++k) {
                    bits.push_back(high);
                }
            }
            bin.bit_count = static_cast<uint32_t>(bits.size());
            bin.bit_raw = bin.bit_count;
            bin.data_raw.assign((bits.size() + 7U) / 8U, 0U);
            for (std::size_t i = 0; i < bits.size(); ++i) {
                if (bits[i]) {
                    bin.data_raw[i / 8U] |= static_cast<uint8_t>(1U << (7U - (i % 8U)));
                }
            }
            result = std::move(bin);
        }
    }

    if (result) {
        result->file_type = SubGhzFileType::KeyFile;
        result->frequency_hz = frequency_hz;
        result->preset = preset;
        result->rssi_dbm = rssi_dbm;
        result->raw_timings = encode_protocol_timings(*result);
        result->name = result->protocol + "_" + std::to_string(result->bit_count) + "b";
    }
    return result;
}

const std::vector<ManualProtocolSpec>& manual_protocol_specs() {
    static const std::vector<ManualProtocolSpec> kSpecs = {
        {"Princeton", 24, 400, 0x000000000055AA3CU},
        {"CAME", 12, 320, 0x0000000000000A5BU},
        {"CAME", 24, 320, 0x0000000000A5B4C3U},
        {"Nice FLO", 12, 700, 0x00000000000005C3U},
        {"Nice FLO", 24, 700, 0x00000000003C5A96U},
        {"GateTX", 24, 350, 0x00000000007F2A18U},
        {"Linear", 10, 500, 0x00000000000002B5U},
        {"Chamberlain", 9, 1000, 0x000000000000015AU},
        {"Holtek", 12, 430, 0x00000000000006E1U},
        {"KeeLoq", 64, 400, 0xA5C3965A3C691E87ULL},
    };
    return kSpecs;
}

}  // namespace model
