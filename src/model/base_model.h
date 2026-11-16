/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "subghz_types.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace model {

enum class AppPage : int32_t {
    Home      = 0,
    Read      = 1,
    ReadRaw   = 2,
    Saved     = 3,
    Analyzer  = 4,
    AddManual = 5,
    RadioInfo = 6,
};

class BaseModel {
public:
    BaseModel();

    const char* app_title() const;

    bool dark_mode() const { return dark_mode_; }
    void set_dark_mode(bool enabled) { dark_mode_ = enabled; }
    void toggle_dark_mode() { dark_mode_ = !dark_mode_; }

    AppPage current_page() const { return current_page_; }
    void set_current_page(AppPage page) { current_page_ = page; }

    // Home menu selection (0..5)
    int32_t home_index() const { return home_index_; }
    void set_home_index(int32_t idx);

    // Radio tuning settings
    uint32_t frequency_hz() const { return frequency_hz_; }
    void set_frequency_hz(uint32_t hz);
    void step_frequency(int delta);

    SubGhzPreset preset() const { return preset_; }
    void set_preset(SubGhzPreset p) { preset_ = p; }
    void cycle_preset();

    bool hopping() const { return hopping_; }
    void set_hopping(bool h) { hopping_ = h; }
    void toggle_hopping() { hopping_ = !hopping_; }

    float rssi_threshold_dbm() const { return rssi_threshold_dbm_; }
    void cycle_rssi_threshold();

    int8_t tx_power_dbm() const { return tx_power_dbm_; }
    void step_tx_power(int delta);

    // Captured signals in Read mode
    const std::vector<SubGhzSignal>& captured_signals() const { return captured_; }
    void add_captured_signal(SubGhzSignal sig);
    void clear_captured_signals();
    int32_t selected_capture_index() const { return selected_capture_idx_; }
    void set_selected_capture_index(int32_t idx);
    const SubGhzSignal* selected_capture() const;

    // Read RAW state
    bool raw_recording() const { return raw_recording_; }
    void set_raw_recording(bool rec) { raw_recording_ = rec; }
    const SubGhzSignal& last_raw_signal() const { return last_raw_signal_; }
    void set_last_raw_signal(SubGhzSignal sig) { last_raw_signal_ = std::move(sig); }
    const std::array<int32_t, 40>& raw_waveform() const { return raw_waveform_; }
    void push_raw_waveform_sample(int32_t level_pct);
    void clear_raw_waveform();

    // Saved .sub file browser
    void init_subghz_directories(const std::filesystem::path& bundled_root);
    const std::filesystem::path& current_directory() const { return current_dir_; }
    const std::filesystem::path& writable_subghz_root() const { return writable_root_; }
    const std::vector<FileEntry>& file_entries() const { return file_entries_; }
    void refresh_file_entries();
    int32_t selected_file_index() const { return selected_file_idx_; }
    void set_selected_file_index(int32_t idx);
    bool enter_selected_entry(SubGhzSignal& out_loaded_signal, std::string& out_error);
    bool navigate_parent_directory();

    bool saved_detail_open() const { return saved_detail_open_; }
    void set_saved_detail_open(bool open) { saved_detail_open_ = open; }
    const SubGhzSignal& active_saved_signal() const { return active_saved_signal_; }
    void set_active_saved_signal(SubGhzSignal sig) { active_saved_signal_ = std::move(sig); }

    // Add Manually state
    int32_t manual_field_index() const { return manual_field_idx_; }
    void set_manual_field_index(int32_t idx);
    int32_t manual_protocol_index() const { return manual_proto_idx_; }
    void step_manual_protocol(int delta);
    uint64_t manual_key() const { return manual_key_; }
    void set_manual_key(uint64_t key) { manual_key_ = key; }
    void randomize_manual_key();
    void append_manual_hex_nibble(uint8_t nibble);
    SubGhzSignal build_manual_signal() const;

private:
    bool dark_mode_{true};
    AppPage current_page_{AppPage::Home};
    int32_t home_index_{0};

    std::size_t freq_index_{10};  // Default 433.92 MHz
    uint32_t frequency_hz_{433920000U};
    SubGhzPreset preset_{SubGhzPreset::Ook650Async};
    bool hopping_{false};
    float rssi_threshold_dbm_{-72.0f};
    int8_t tx_power_dbm_{10};

    std::vector<SubGhzSignal> captured_;
    int32_t selected_capture_idx_{0};

    bool raw_recording_{false};
    SubGhzSignal last_raw_signal_;
    std::array<int32_t, 40> raw_waveform_{};

    std::filesystem::path root_dir_;
    std::filesystem::path current_dir_;
    std::filesystem::path writable_root_;
    std::vector<FileEntry> file_entries_;
    int32_t selected_file_idx_{0};
    bool saved_detail_open_{false};
    SubGhzSignal active_saved_signal_;

    int32_t manual_field_idx_{0};
    int32_t manual_proto_idx_{0};
    uint64_t manual_key_{0x000000000055AA3CU};
    uint32_t manual_counter_{1};
};

std::filesystem::path resolve_user_home_dir();
void chown_to_user_home_owner(const std::filesystem::path& path, const std::filesystem::path& home_dir);

}  // namespace model
