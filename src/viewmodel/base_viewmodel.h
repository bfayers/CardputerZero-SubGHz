/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_model.h"
#include "subjects.h"
#include "subghz_worker.h"

#include "lvgl.h"

#include <filesystem>
#include <memory>
#include <string>

namespace viewmodel {

class BaseViewModel {
public:
    BaseViewModel();
    ~BaseViewModel();

    BaseViewModel(const BaseViewModel&)            = delete;
    BaseViewModel& operator=(const BaseViewModel&) = delete;

    void init_subghz(const std::filesystem::path& bundled_subghz_root);
    void poll_radio_events();

    // Subjects for reactive LVGL binding
    lv_subject_t* title_subject();
    lv_subject_t* dark_mode_subject();
    lv_subject_t* current_page_subject();
    lv_subject_t* home_index_subject();
    lv_subject_t* radio_status_subject();
    lv_subject_t* frequency_text_subject();
    lv_subject_t* preset_text_subject();
    lv_subject_t* rssi_dbm_text_subject();
    lv_subject_t* rssi_percent_subject();
    lv_subject_t* hopping_subject();
    lv_subject_t* ui_revision_subject();
    lv_subject_t* analyzer_freq_subject();
    lv_subject_t* analyzer_peak_subject();
    lv_subject_t* raw_status_subject();
    lv_subject_t* info_summary_subject();
    lv_subject_t* toast_message_subject();
    lv_subject_t* quit_requested_subject();

    // State getters
    bool is_dark_mode() const;
    void set_dark_mode(bool enabled);
    void toggle_dark_mode();

    model::AppPage current_page() const;
    void navigate_to(model::AppPage page);
    void show_home_page();
    void request_quit();

    const model::BaseModel& model() const { return model_; }
    model::BaseModel& model_mutable() { return model_; }
    const radio::RadioHardwareInfo& hardware_info() const { return hw_info_; }

    // Page-aware key input router (called for keys on active screen)
    bool handle_screen_key(uint32_t key);

    // Explicit UI actions
    void select_home_item(int32_t index);
    void activate_home_item();
    void step_frequency(int delta);
    void cycle_preset();
    void toggle_hopping();
    void transmit_signal(const model::SubGhzSignal& sig);
    bool save_signal_to_library(const model::SubGhzSignal& sig, std::string& out_saved_path);
    void retry_radio_init();

private:
    void sync_worker_mode();
    void publish_all();
    void notify_ui_revision();
    void emit_toast(const std::string& msg);

    model::BaseModel model_;
    std::unique_ptr<radio::SubGhzWorker> worker_;
    radio::RadioHardwareInfo hw_info_{};
    lv_timer_t* poll_timer_{nullptr};
    uint32_t analyzer_peak_hz_{433920000U};
    float analyzer_peak_rssi_{-110.0f};
    uint32_t save_sequence_{1};

    reactive::StringSubject<32> title_subject_;
    reactive::BoolSubject dark_mode_subject_;
    reactive::IntSubject current_page_subject_;
    reactive::IntSubject home_index_subject_;
    reactive::StringSubject<64> radio_status_subject_;
    reactive::StringSubject<24> frequency_text_subject_;
    reactive::StringSubject<16> preset_text_subject_;
    reactive::StringSubject<24> rssi_dbm_text_subject_;
    reactive::IntSubject rssi_percent_subject_;
    reactive::BoolSubject hopping_subject_;
    reactive::IntSubject ui_revision_subject_;
    reactive::StringSubject<32> analyzer_freq_subject_;
    reactive::StringSubject<64> analyzer_peak_subject_;
    reactive::StringSubject<64> raw_status_subject_;
    reactive::StringSubject<160> info_summary_subject_;
    reactive::StringSubject<96> toast_message_subject_;
    reactive::BoolSubject quit_requested_subject_;
};

}  // namespace viewmodel
