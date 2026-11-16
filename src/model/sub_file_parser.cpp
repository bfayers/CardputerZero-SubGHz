/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "sub_file_parser.h"
#include "subghz_protocols.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace model {
namespace {

std::string_view trim_view(std::string_view text) noexcept {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r' || text.front() == '\n')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' || text.back() == '\n')) {
        text.remove_suffix(1);
    }
    return text;
}

bool parse_hex_bytes(std::string_view text, std::vector<uint8_t>& out_bytes) {
    std::istringstream iss{std::string(text)};
    std::string token;
    while (iss >> token) {
        if (token.size() > 2 && (token.rfind("0x", 0) == 0 || token.rfind("0X", 0) == 0)) {
            token.erase(0, 2);
        }
        unsigned int value = 0;
        const auto res = std::from_chars(token.data(), token.data() + token.size(), value, 16);
        if (res.ec != std::errc{} || res.ptr != token.data() + token.size() || value > 0xFFU) {
            return false;
        }
        out_bytes.push_back(static_cast<uint8_t>(value));
    }
    return true;
}

void parse_raw_data_line(std::string_view text, std::vector<int32_t>& out_timings) {
    std::istringstream iss{std::string(text)};
    std::string token;
    while (iss >> token) {
        char* end = nullptr;
        const long val = std::strtol(token.c_str(), &end, 10);
        if (end != token.c_str() && val != 0) {
            out_timings.push_back(static_cast<int32_t>(val));
        }
    }
}

}  // namespace

bool parse_sub_string(std::string_view content, SubGhzSignal& out_signal, std::string& out_error) {
    SubGhzSignal parsed;
    bool has_filetype = false;
    bool has_frequency = false;
    bool has_preset = false;
    bool has_protocol = false;

    std::istringstream stream{std::string(content)};
    std::string raw_line;
    while (std::getline(stream, raw_line)) {
        std::string_view line = raw_line;
        if (const auto comment_pos = line.find('#'); comment_pos != std::string_view::npos) {
            line = line.substr(0, comment_pos);
        }
        line = trim_view(line);
        if (line.empty()) {
            continue;
        }

        const auto colon = line.find(':');
        if (colon == std::string_view::npos) {
            continue;
        }

        const std::string_view key = trim_view(line.substr(0, colon));
        const std::string_view val = trim_view(line.substr(colon + 1));

        if (key == "Filetype") {
            if (val == "Flipper SubGhz Key File" || val == "Flipper SubGhz") {
                parsed.file_type = SubGhzFileType::KeyFile;
                has_filetype = true;
            } else if (val == "Flipper SubGhz RAW File") {
                parsed.file_type = SubGhzFileType::RawFile;
                has_filetype = true;
            } else {
                out_error = "Unsupported Filetype: " + std::string(val);
                return false;
            }
        } else if (key == "Version") {
            uint32_t ver = 1;
            std::from_chars(val.data(), val.data() + val.size(), ver);
            parsed.version = ver;
        } else if (key == "Frequency") {
            uint32_t freq = 0;
            const auto res = std::from_chars(val.data(), val.data() + val.size(), freq);
            if (res.ec != std::errc{} || !is_valid_cc1101_frequency_hz(freq)) {
                out_error = "Invalid or out-of-band CC1101 Frequency: " + std::string(val);
                return false;
            }
            parsed.frequency_hz = freq;
            has_frequency = true;
        } else if (key == "Preset") {
            if (!parse_preset_name(val, parsed.preset)) {
                out_error = "Unknown Sub-GHz Preset: " + std::string(val);
                return false;
            }
            has_preset = true;
        } else if (key == "Custom_preset_module") {
            parsed.custom_preset_module = std::string(val);
        } else if (key == "Custom_preset_data") {
            if (!parse_hex_bytes(val, parsed.custom_preset_data)) {
                out_error = "Invalid Custom_preset_data hex sequence";
                return false;
            }
        } else if (key == "Protocol") {
            parsed.protocol = std::string(val);
            if (parsed.protocol == "RAW") {
                parsed.file_type = SubGhzFileType::RawFile;
            }
            has_protocol = true;
        } else if (key == "Bit") {
            uint32_t bits = 0;
            std::from_chars(val.data(), val.data() + val.size(), bits);
            parsed.bit_count = bits;
        } else if (key == "Key") {
            parsed.key_bytes.clear();
            if (!parse_hex_bytes(val, parsed.key_bytes) || parsed.key_bytes.empty()) {
                out_error = "Invalid Key hex sequence: " + std::string(val);
                return false;
            }
            uint64_t key_val = 0;
            const std::size_t n = std::min<std::size_t>(8U, parsed.key_bytes.size());
            for (std::size_t i = parsed.key_bytes.size() - n; i < parsed.key_bytes.size(); ++i) {
                key_val = (key_val << 8U) | parsed.key_bytes[i];
            }
            parsed.key = key_val;
        } else if (key == "TE") {
            uint32_t te = 0;
            std::from_chars(val.data(), val.data() + val.size(), te);
            parsed.te_us = te;
        } else if (key == "Repeat") {
            uint32_t rep = 5;
            std::from_chars(val.data(), val.data() + val.size(), rep);
            parsed.repeat_count = std::clamp<uint32_t>(rep, 1U, 50U);
        } else if (key == "Guard_time") {
            uint32_t gt = 0;
            std::from_chars(val.data(), val.data() + val.size(), gt);
            parsed.guard_time_us = gt;
        } else if (key == "Bit_RAW") {
            uint32_t br = 0;
            std::from_chars(val.data(), val.data() + val.size(), br);
            parsed.bit_raw += br;
        } else if (key == "Data_RAW") {
            if (!parse_hex_bytes(val, parsed.data_raw)) {
                out_error = "Invalid Data_RAW hex sequence";
                return false;
            }
        } else if (key == "RAW_Data" || key == "File_Data") {
            parse_raw_data_line(val, parsed.raw_timings);
        } else {
            parsed.extra_fields.emplace_back(std::string(key), std::string(val));
        }
    }

    if (!has_filetype) {
        out_error = "Missing Filetype header in .sub file";
        return false;
    }
    if (!has_frequency) {
        out_error = "Missing Frequency field in .sub file";
        return false;
    }
    if (!has_preset) {
        out_error = "Missing Preset field in .sub file";
        return false;
    }
    if (!has_protocol) {
        out_error = "Missing Protocol field in .sub file";
        return false;
    }

    // Ensure raw_timings are populated for transmission
    parsed.raw_timings = encode_protocol_timings(parsed);
    if (parsed.raw_timings.empty()) {
        out_error = "Signal contains no transmittable pulse data";
        return false;
    }

    out_signal = std::move(parsed);
    out_error.clear();
    return true;
}

bool parse_sub_file(const std::filesystem::path& path, SubGhzSignal& out_signal, std::string& out_error) {
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file) {
        out_error = "Failed to open file: " + path.string();
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    if (!parse_sub_string(buffer.str(), out_signal, out_error)) {
        return false;
    }
    out_signal.source_path = path.string();
    out_signal.name = path.stem().string();
    return true;
}

std::string serialize_sub_signal(const SubGhzSignal& signal) {
    std::ostringstream out;
    const bool is_raw = (signal.file_type == SubGhzFileType::RawFile || signal.protocol == "RAW");
    out << "Filetype: " << (is_raw ? "Flipper SubGhz RAW File" : "Flipper SubGhz Key File") << "\n";
    out << "Version: " << signal.version << "\n";
    out << "Frequency: " << signal.frequency_hz << "\n";
    out << "Preset: " << preset_to_flipper_name(signal.preset) << "\n";

    if (signal.preset == SubGhzPreset::Custom && !signal.custom_preset_data.empty()) {
        out << "Custom_preset_module: "
            << (signal.custom_preset_module.empty() ? "CC1101" : signal.custom_preset_module) << "\n";
        out << "Custom_preset_data:";
        for (uint8_t byte : signal.custom_preset_data) {
            char b[4]{};
            std::snprintf(b, sizeof(b), "%02X", static_cast<unsigned>(byte));
            out << ' ' << b;
        }
        out << "\n";
    }

    out << "Protocol: " << (signal.protocol.empty() ? (is_raw ? "RAW" : "Princeton") : signal.protocol) << "\n";

    if (is_raw) {
        const auto timings = encode_protocol_timings(signal);
        constexpr std::size_t kMaxPerLine = 512;
        for (std::size_t i = 0; i < timings.size(); ++i) {
            if (i % kMaxPerLine == 0) {
                if (i > 0) out << "\n";
                out << "RAW_Data:";
            }
            out << ' ' << timings[i];
        }
        out << "\n";
    } else if (signal.protocol == "BinRAW") {
        const uint32_t bits = signal.bit_raw > 0 ? signal.bit_raw : signal.bit_count;
        out << "Bit: " << bits << "\n";
        out << "TE: " << (signal.te_us > 0 ? signal.te_us : 400U) << "\n";
        out << "Bit_RAW: " << bits << "\n";
        out << "Data_RAW:";
        for (uint8_t byte : signal.data_raw) {
            char b[4]{};
            std::snprintf(b, sizeof(b), "%02X", static_cast<unsigned>(byte));
            out << ' ' << b;
        }
        out << "\n";
    } else {
        out << "Bit: " << (signal.bit_count > 0 ? signal.bit_count : 24U) << "\n";
        out << "Key: " << format_key_hex(signal.key, signal.bit_count, signal.key_bytes) << "\n";
        if (signal.te_us > 0) {
            out << "TE: " << signal.te_us << "\n";
        }
        if (signal.guard_time_us > 0) {
            out << "Guard_time: " << signal.guard_time_us << "\n";
        }
        for (const auto& [k, v] : signal.extra_fields) {
            out << k << ": " << v << "\n";
        }
    }
    return out.str();
}

bool save_sub_file(const std::filesystem::path& path, const SubGhzSignal& signal, std::string& out_error) {
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    std::ofstream file(path, std::ios::out | std::ios::trunc | std::ios::binary);
    if (!file) {
        out_error = "Failed to open file for writing: " + path.string();
        return false;
    }
    const std::string content = serialize_sub_signal(signal);
    file.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!file.good()) {
        out_error = "Failed to write .sub content to: " + path.string();
        return false;
    }
    out_error.clear();
    return true;
}

}  // namespace model
