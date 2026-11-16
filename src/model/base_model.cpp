/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "base_model.h"
#include "sub_file_parser.h"
#include "subghz_protocols.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <random>

#if !defined(_WIN32)
#include <pwd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace model {

std::filesystem::path resolve_user_home_dir() {
#if !defined(_WIN32)
    if (const char* sudo_user = std::getenv("SUDO_USER"); sudo_user && sudo_user[0] != '\0') {
        if (const passwd* pw = ::getpwnam(sudo_user); pw && pw->pw_dir && pw->pw_dir[0] != '\0') {
            return std::filesystem::path(pw->pw_dir);
        }
    }
#endif
    if (const char* home = std::getenv("HOME"); home && home[0] != '\0') {
        const std::filesystem::path p(home);
        if (p != "/root") {
            return p;
        }
    }
#if !defined(_WIN32)
    std::error_code ec;
    if (std::filesystem::is_directory("/home", ec)) {
        for (const auto& entry : std::filesystem::directory_iterator("/home", ec)) {
            if (entry.is_directory(ec)) {
                return entry.path();
            }
        }
    }
    if (const passwd* pw = ::getpwuid(::getuid()); pw && pw->pw_dir && pw->pw_dir[0] != '\0') {
        return std::filesystem::path(pw->pw_dir);
    }
#endif
    return {};
}

void chown_to_user_home_owner(const std::filesystem::path& path, const std::filesystem::path& home_dir) {
#if !defined(_WIN32)
    if (home_dir.empty()) {
        return;
    }
    struct stat st{};
    if (::stat(home_dir.c_str(), &st) != 0) {
        return;
    }
    std::error_code ec;
    ::chown(path.c_str(), st.st_uid, st.st_gid);
    if (std::filesystem::is_directory(path, ec)) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(path, ec)) {
            ::chown(entry.path().c_str(), st.st_uid, st.st_gid);
        }
    }
#else
    (void)path;
    (void)home_dir;
#endif
}

BaseModel::BaseModel() {
    clear_raw_waveform();
}

const char* BaseModel::app_title() const {
    switch (current_page_) {
        case AppPage::Home:
            return "SubGHz";
        case AppPage::Read:
            return "SubGHz Read";
        case AppPage::ReadRaw:
            return "SubGHz RAW";
        case AppPage::Saved:
            return "Saved .sub";
        case AppPage::Analyzer:
            return "Freq Analyzer";
        case AppPage::AddManual:
            return "Add Manually";
        case AppPage::RadioInfo:
            return "CC1101 Info";
    }
    return "SubGHz";
}

void BaseModel::set_home_index(int32_t idx) {
    if (idx < 0) idx = 5;
    if (idx > 5) idx = 0;
    home_index_ = idx;
}

void BaseModel::set_frequency_hz(uint32_t hz) {
    if (!is_valid_cc1101_frequency_hz(hz)) {
        return;
    }
    frequency_hz_ = hz;
    for (std::size_t i = 0; i < kStandardFrequenciesHz.size(); ++i) {
        if (kStandardFrequenciesHz[i] == hz) {
            freq_index_ = i;
            break;
        }
    }
}

void BaseModel::step_frequency(int delta) {
    const int n = static_cast<int>(kStandardFrequenciesHz.size());
    int next = (static_cast<int>(freq_index_) + delta) % n;
    if (next < 0) next += n;
    freq_index_ = static_cast<std::size_t>(next);
    frequency_hz_ = kStandardFrequenciesHz[freq_index_];
}

void BaseModel::cycle_preset() {
    switch (preset_) {
        case SubGhzPreset::Ook650Async:
            preset_ = SubGhzPreset::Ook270Async;
            break;
        case SubGhzPreset::Ook270Async:
            preset_ = SubGhzPreset::Fsk2Dev238Async;
            break;
        case SubGhzPreset::Fsk2Dev238Async:
            preset_ = SubGhzPreset::Fsk2Dev476Async;
            break;
        default:
            preset_ = SubGhzPreset::Ook650Async;
            break;
    }
}

void BaseModel::cycle_rssi_threshold() {
    constexpr std::array<float, 5> kThresholds = {-85.0f, -78.0f, -72.0f, -65.0f, -55.0f};
    std::size_t idx = 0;
    for (std::size_t i = 0; i < kThresholds.size(); ++i) {
        if (std::abs(rssi_threshold_dbm_ - kThresholds[i]) < 1.5f) {
            idx = (i + 1) % kThresholds.size();
            break;
        }
    }
    rssi_threshold_dbm_ = kThresholds[idx];
}

void BaseModel::step_tx_power(int delta) {
    constexpr std::array<int8_t, 8> kPowers = {-30, -20, -15, -10, 0, 5, 7, 10};
    int idx = 7;
    for (std::size_t i = 0; i < kPowers.size(); ++i) {
        if (tx_power_dbm_ == kPowers[i]) {
            idx = static_cast<int>(i);
            break;
        }
    }
    idx = std::clamp(idx + delta, 0, static_cast<int>(kPowers.size()) - 1);
    tx_power_dbm_ = kPowers[static_cast<std::size_t>(idx)];
}

void BaseModel::add_captured_signal(SubGhzSignal sig) {
    captured_.insert(captured_.begin(), std::move(sig));
    if (captured_.size() > 32) {
        captured_.pop_back();
    }
    selected_capture_idx_ = 0;
}

void BaseModel::clear_captured_signals() {
    captured_.clear();
    selected_capture_idx_ = 0;
}

void BaseModel::set_selected_capture_index(int32_t idx) {
    if (captured_.empty()) {
        selected_capture_idx_ = 0;
        return;
    }
    selected_capture_idx_ = std::clamp<int32_t>(idx, 0, static_cast<int32_t>(captured_.size()) - 1);
}

const SubGhzSignal* BaseModel::selected_capture() const {
    if (captured_.empty() || selected_capture_idx_ < 0 ||
        static_cast<std::size_t>(selected_capture_idx_) >= captured_.size()) {
        return nullptr;
    }
    return &captured_[static_cast<std::size_t>(selected_capture_idx_)];
}

void BaseModel::push_raw_waveform_sample(int32_t level_pct) {
    std::rotate(raw_waveform_.begin(), raw_waveform_.begin() + 1, raw_waveform_.end());
    raw_waveform_.back() = std::clamp<int32_t>(level_pct, 4, 100);
}

void BaseModel::clear_raw_waveform() {
    raw_waveform_.fill(6);
}

void BaseModel::init_subghz_directories(const std::filesystem::path& bundled_root) {
    std::error_code ec;
    if (const char* env_db = std::getenv("SUBGHZ_DB_PATH")) {
        const std::filesystem::path custom_path(env_db);
        if (std::filesystem::is_directory(custom_path, ec)) {
            root_dir_ = custom_path;
            current_dir_ = custom_path;
            writable_root_ = custom_path / "Captured";
            std::filesystem::create_directories(writable_root_, ec);
            refresh_file_entries();
            return;
        }
    }

#if USE_DESKTOP
    if (!bundled_root.empty() && std::filesystem::is_directory(bundled_root, ec)) {
        root_dir_ = bundled_root;
    } else {
        root_dir_ = std::filesystem::current_path(ec) / "assets" / "subghz";
    }
    current_dir_ = root_dir_;
    writable_root_ = root_dir_ / "Captured";
    std::filesystem::create_directories(writable_root_, ec);
#else
    const auto home_dir = resolve_user_home_dir();
    const std::filesystem::path subg_base = !home_dir.empty() ? (home_dir / "subg") : std::filesystem::path("/tmp/subg");
    root_dir_ = subg_base / "files";

    std::filesystem::create_directories(subg_base / "config", ec);
    std::filesystem::create_directories(root_dir_, ec);
    writable_root_ = root_dir_ / "Captured";
    std::filesystem::create_directories(writable_root_, ec);
    (void)bundled_root;
    chown_to_user_home_owner(subg_base, home_dir);
    current_dir_ = root_dir_;
#endif
    refresh_file_entries();
}

void BaseModel::refresh_file_entries() {
    file_entries_.clear();
    std::error_code ec;
    if (current_dir_.empty() || !std::filesystem::is_directory(current_dir_, ec)) {
        selected_file_idx_ = 0;
        return;
    }

    if (current_dir_ != root_dir_ && current_dir_.has_parent_path()) {
        FileEntry parent;
        parent.name = "..";
        parent.full_path = current_dir_.parent_path().string();
        parent.is_directory = true;
        parent.is_parent_link = true;
        file_entries_.push_back(std::move(parent));
    }

    std::vector<FileEntry> dirs;
    std::vector<FileEntry> files;
    for (const auto& entry : std::filesystem::directory_iterator(current_dir_, ec)) {
        const auto path = entry.path();
        const std::string filename = path.filename().string();
        if (filename.empty() || filename[0] == '.') {
            continue;
        }
        if (entry.is_directory(ec)) {
            FileEntry item;
            item.name = filename;
            item.full_path = path.string();
            item.is_directory = true;
            dirs.push_back(std::move(item));
        } else if (entry.is_regular_file(ec) && (path.extension() == ".sub" || path.extension() == ".SUB")) {
            FileEntry item;
            item.name = filename;
            item.full_path = path.string();
            item.is_directory = false;
            files.push_back(std::move(item));
        }
    }

    auto by_name = [](const FileEntry& a, const FileEntry& b) { return a.name < b.name; };
    std::sort(dirs.begin(), dirs.end(), by_name);
    std::sort(files.begin(), files.end(), by_name);

    file_entries_.insert(file_entries_.end(), dirs.begin(), dirs.end());
    file_entries_.insert(file_entries_.end(), files.begin(), files.end());

    if (file_entries_.empty()) {
        selected_file_idx_ = 0;
    } else {
        selected_file_idx_ =
            std::clamp<int32_t>(selected_file_idx_, 0, static_cast<int32_t>(file_entries_.size()) - 1);
    }
}

void BaseModel::set_selected_file_index(int32_t idx) {
    if (file_entries_.empty()) {
        selected_file_idx_ = 0;
        return;
    }
    const int32_t n = static_cast<int32_t>(file_entries_.size());
    int32_t wrapped = idx % n;
    if (wrapped < 0) {
        wrapped += n;
    }
    selected_file_idx_ = wrapped;
}

bool BaseModel::enter_selected_entry(SubGhzSignal& out_loaded_signal, std::string& out_error) {
    if (file_entries_.empty() || selected_file_idx_ < 0 ||
        static_cast<std::size_t>(selected_file_idx_) >= file_entries_.size()) {
        out_error = "No file selected";
        return false;
    }

    const FileEntry entry = file_entries_[static_cast<std::size_t>(selected_file_idx_)];
    if (entry.is_parent_link) {
        navigate_parent_directory();
        out_error.clear();
        return false;
    }
    if (entry.is_directory) {
        current_dir_ = entry.full_path;
        selected_file_idx_ = 0;
        refresh_file_entries();
        out_error.clear();
        return false;  // Changed directory, did not open a .sub file
    }

    if (!parse_sub_file(entry.full_path, out_loaded_signal, out_error)) {
        return false;
    }
    active_saved_signal_ = out_loaded_signal;
    saved_detail_open_ = true;
    return true;
}

bool BaseModel::navigate_parent_directory() {
    if (current_dir_ == root_dir_ || !current_dir_.has_parent_path()) {
        return false;
    }
    const std::string previous_dir_name = current_dir_.filename().string();
    current_dir_ = current_dir_.parent_path();
    selected_file_idx_ = 0;
    refresh_file_entries();
    for (std::size_t i = 0; i < file_entries_.size(); ++i) {
        if (file_entries_[i].is_directory && !file_entries_[i].is_parent_link &&
            file_entries_[i].name == previous_dir_name) {
            selected_file_idx_ = static_cast<int32_t>(i);
            break;
        }
    }
    return true;
}

void BaseModel::set_manual_field_index(int32_t idx) {
    if (idx < 0) idx = 3;
    if (idx > 3) idx = 0;
    manual_field_idx_ = idx;
}

void BaseModel::step_manual_protocol(int delta) {
    const auto& specs = manual_protocol_specs();
    const int n = static_cast<int>(specs.size());
    int next = (manual_proto_idx_ + delta) % n;
    if (next < 0) next += n;
    manual_proto_idx_ = next;
    manual_key_ = specs[static_cast<std::size_t>(manual_proto_idx_)].default_key;
}

void BaseModel::randomize_manual_key() {
    const auto& specs = manual_protocol_specs();
    const auto& spec = specs[static_cast<std::size_t>(manual_proto_idx_)];
    const uint64_t seed = static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count() ^ (manual_counter_++ * 0x9E3779B97F4A7C15ULL));
    std::mt19937_64 rng(seed);
    uint64_t raw = rng();
    if (spec.bit_count < 64) {
        raw &= ((1ULL << spec.bit_count) - 1ULL);
    }
    if (raw == 0) raw = 1;
    manual_key_ = raw;
}

void BaseModel::append_manual_hex_nibble(uint8_t nibble) {
    const auto& specs = manual_protocol_specs();
    const auto& spec = specs[static_cast<std::size_t>(manual_proto_idx_)];
    uint64_t next = (manual_key_ << 4U) | (nibble & 0x0FU);
    if (spec.bit_count < 64) {
        next &= ((1ULL << spec.bit_count) - 1ULL);
    }
    manual_key_ = next;
}

SubGhzSignal BaseModel::build_manual_signal() const {
    const auto& specs = manual_protocol_specs();
    const auto& spec = specs[static_cast<std::size_t>(manual_proto_idx_)];
    SubGhzSignal sig;
    sig.file_type = SubGhzFileType::KeyFile;
    sig.frequency_hz = frequency_hz_;
    sig.preset = preset_;
    sig.protocol = spec.name;
    sig.bit_count = spec.bit_count;
    sig.te_us = spec.default_te_us;
    sig.key = manual_key_;
    sig.raw_timings = encode_protocol_timings(sig);
    sig.name = std::string(spec.name) + "_" + std::to_string(spec.bit_count) + "b";
    return sig;
}

}  // namespace model
