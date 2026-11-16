/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace model {

enum class SubGhzPreset : int32_t {
    Ook270Async     = 0,  // FuriHalSubGhzPresetOok270Async (AM270)
    Ook650Async     = 1,  // FuriHalSubGhzPresetOok650Async (AM650)
    Fsk2Dev238Async = 2,  // FuriHalSubGhzPreset2FSKDev238Async (FM238)
    Fsk2Dev476Async = 3,  // FuriHalSubGhzPreset2FSKDev476Async (FM476)
    Custom          = 4,  // FuriHalSubGhzPresetCustom
};

enum class SubGhzFileType : int32_t {
    KeyFile = 0,  // Flipper SubGhz Key File
    RawFile = 1,  // Flipper SubGhz RAW File
};

// Standard Flipper Zero Sub-GHz frequency table (in Hz) across CC1101 bands:
// Band 1: 300.00 - 348.00 MHz
// Band 2: 387.00 - 464.00 MHz
// Band 3: 779.00 - 928.00 MHz
inline constexpr std::array<uint32_t, 18> kStandardFrequenciesHz = {{
    300000000U,
    303875000U,
    304250000U,
    310000000U,
    315000000U,
    318000000U,
    390000000U,
    418000000U,
    433075000U,
    433420000U,
    433920000U,
    434420000U,
    434775000U,
    438900000U,
    868350000U,
    868800000U,
    915000000U,
    925000000U,
}};

inline constexpr std::array<uint32_t, 4> kHopperFrequenciesHz = {{
    315000000U,
    433920000U,
    868350000U,
    915000000U,
}};

constexpr bool is_valid_cc1101_frequency_hz(uint32_t freq_hz) noexcept {
    return (freq_hz >= 300000000U && freq_hz <= 348000000U) ||
           (freq_hz >= 387000000U && freq_hz <= 464000000U) ||
           (freq_hz >= 779000000U && freq_hz <= 928000000U);
}

constexpr const char* preset_to_flipper_name(SubGhzPreset preset) noexcept {
    switch (preset) {
        case SubGhzPreset::Ook270Async:
            return "FuriHalSubGhzPresetOok270Async";
        case SubGhzPreset::Ook650Async:
            return "FuriHalSubGhzPresetOok650Async";
        case SubGhzPreset::Fsk2Dev238Async:
            return "FuriHalSubGhzPreset2FSKDev238Async";
        case SubGhzPreset::Fsk2Dev476Async:
            return "FuriHalSubGhzPreset2FSKDev476Async";
        case SubGhzPreset::Custom:
            return "FuriHalSubGhzPresetCustom";
    }
    return "FuriHalSubGhzPresetOok650Async";
}

constexpr const char* preset_to_short_name(SubGhzPreset preset) noexcept {
    switch (preset) {
        case SubGhzPreset::Ook270Async:
            return "AM270";
        case SubGhzPreset::Ook650Async:
            return "AM650";
        case SubGhzPreset::Fsk2Dev238Async:
            return "FM238";
        case SubGhzPreset::Fsk2Dev476Async:
            return "FM476";
        case SubGhzPreset::Custom:
            return "CUSTOM";
    }
    return "AM650";
}

bool parse_preset_name(std::string_view text, SubGhzPreset& out_preset) noexcept;
std::string format_frequency_mhz(uint32_t freq_hz);
std::string format_key_hex(uint64_t key, uint32_t bit_count, const std::vector<uint8_t>& key_bytes = {});

struct SubGhzSignal {
    std::string name;
    std::string source_path;
    SubGhzFileType file_type{SubGhzFileType::KeyFile};
    uint32_t version{1};
    uint32_t frequency_hz{433920000U};
    SubGhzPreset preset{SubGhzPreset::Ook650Async};
    std::string custom_preset_module;
    std::vector<uint8_t> custom_preset_data;

    std::string protocol;  // "RAW", "BinRAW", "Princeton", "CAME", "Nice FLO", "KeeLoq", etc.
    uint32_t bit_count{0};
    uint64_t key{0};
    std::vector<uint8_t> key_bytes;
    uint32_t te_us{0};
    uint32_t repeat_count{5};
    uint32_t guard_time_us{0};

    // BinRAW fields
    uint32_t bit_raw{0};
    std::vector<uint8_t> data_raw;

    // RAW or synthesized microsecond timings (+HIGH pulse, -LOW space)
    std::vector<int32_t> raw_timings;

    // Additional protocol-specific key-value lines preserved during round-trip
    std::vector<std::pair<std::string, std::string>> extra_fields;

    // Runtime metadata
    float rssi_dbm{-95.0f};
    uint64_t timestamp_ms{0};

    uint64_t total_duration_us() const noexcept {
        uint64_t sum = 0;
        for (int32_t t : raw_timings) {
            sum += static_cast<uint64_t>(t < 0 ? -t : t);
        }
        return sum;
    }
};

struct FileEntry {
    std::string name;
    std::string full_path;
    bool is_directory{false};
    bool is_parent_link{false};
};

struct ManualProtocolSpec {
    const char* name;
    uint32_t bit_count;
    uint32_t default_te_us;
    uint64_t default_key;
};

}  // namespace model
