/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "base_viewmodel.h"
#include "sub_file_parser.h"
#include "subghz_protocols.h"

#include <algorithm>
#include <cstdio>

namespace viewmodel {
namespace {

int32_t rssi_to_percent(float rssi_dbm) noexcept {
    // Map -105 dBm .. -30 dBm -> 0 .. 100%
    const float norm = (rssi_dbm + 105.0f) / 75.0f;
    return std::clamp<int32_t>(static_cast<int32_t>(norm * 100.0f), 0, 100);
}

void worker_poll_timer_cb(lv_timer_t* timer) {
    auto* vm = static_cast<BaseViewModel*>(lv_timer_get_user_data(timer));
    if (vm) {
        vm->poll_radio_events();
    }
}

}  // namespace

BaseViewModel::BaseViewModel()
    : worker_(std::make_unique<radio::SubGhzWorker>()),
      title_subject_(model_.app_title()),
      dark_mode_subject_(model_.dark_mode()),
      current_page_subject_(static_cast<int32_t>(model_.current_page())),
      home_index_subject_(model_.home_index()),
      radio_status_subject_("Initializing..."),
      frequency_text_subject_("433.92 MHz"),
      preset_text_subject_("AM650"),
      rssi_dbm_text_subject_("-96 dBm"),
      rssi_percent_subject_(8),
      hopping_subject_(false),
      ui_revision_subject_(0),
      analyzer_freq_subject_("433.92 MHz"),
      analyzer_peak_subject_("Peak: --"),
      raw_status_subject_("IDLE  Thr: -72 dBm  0 pulses"),
      info_summary_subject_("Probing CC1101..."),
      toast_message_subject_(""),
      quit_requested_subject_(false) {
    publish_all();
}

BaseViewModel::~BaseViewModel() {
    if (poll_timer_) {
        lv_timer_delete(poll_timer_);
        poll_timer_ = nullptr;
    }
    if (worker_) {
        worker_->stop();
    }
}

void BaseViewModel::init_subghz(const std::filesystem::path& bundled_subghz_root) {
    model_.init_subghz_directories(bundled_subghz_root);
    worker_->start();
    if (!poll_timer_) {
        poll_timer_ = lv_timer_create(worker_poll_timer_cb, 35, this);
    }
    publish_all();
}

lv_subject_t* BaseViewModel::title_subject() { return title_subject_.native(); }
lv_subject_t* BaseViewModel::dark_mode_subject() { return dark_mode_subject_.native(); }
lv_subject_t* BaseViewModel::current_page_subject() { return current_page_subject_.native(); }
lv_subject_t* BaseViewModel::home_index_subject() { return home_index_subject_.native(); }
lv_subject_t* BaseViewModel::radio_status_subject() { return radio_status_subject_.native(); }
lv_subject_t* BaseViewModel::frequency_text_subject() { return frequency_text_subject_.native(); }
lv_subject_t* BaseViewModel::preset_text_subject() { return preset_text_subject_.native(); }
lv_subject_t* BaseViewModel::rssi_dbm_text_subject() { return rssi_dbm_text_subject_.native(); }
lv_subject_t* BaseViewModel::rssi_percent_subject() { return rssi_percent_subject_.native(); }
lv_subject_t* BaseViewModel::hopping_subject() { return hopping_subject_.native(); }
lv_subject_t* BaseViewModel::ui_revision_subject() { return ui_revision_subject_.native(); }
lv_subject_t* BaseViewModel::analyzer_freq_subject() { return analyzer_freq_subject_.native(); }
lv_subject_t* BaseViewModel::analyzer_peak_subject() { return analyzer_peak_subject_.native(); }
lv_subject_t* BaseViewModel::raw_status_subject() { return raw_status_subject_.native(); }
lv_subject_t* BaseViewModel::info_summary_subject() { return info_summary_subject_.native(); }
lv_subject_t* BaseViewModel::toast_message_subject() { return toast_message_subject_.native(); }
lv_subject_t* BaseViewModel::quit_requested_subject() { return quit_requested_subject_.native(); }

bool BaseViewModel::is_dark_mode() const { return model_.dark_mode(); }

void BaseViewModel::set_dark_mode(bool enabled) {
    model_.set_dark_mode(enabled);
    dark_mode_subject_.set(model_.dark_mode());
}

void BaseViewModel::toggle_dark_mode() {
    model_.toggle_dark_mode();
    dark_mode_subject_.set(model_.dark_mode());
}

model::AppPage BaseViewModel::current_page() const { return model_.current_page(); }

void BaseViewModel::navigate_to(model::AppPage page) {
    if (model_.current_page() == page) {
        return;
    }
    model_.set_current_page(page);
    if (page == model::AppPage::Saved) {
        model_.refresh_file_entries();
    } else if (page == model::AppPage::ReadRaw) {
        model_.set_raw_recording(false);
    }
    title_subject_.set(model_.app_title());
    current_page_subject_.set(static_cast<int32_t>(page));
    sync_worker_mode();
    publish_all();
}

void BaseViewModel::show_home_page() {
    navigate_to(model::AppPage::Home);
}

void BaseViewModel::request_quit() {
    quit_requested_subject_.set(true);
}

void BaseViewModel::select_home_item(int32_t index) {
    model_.set_home_index(index);
    home_index_subject_.set(model_.home_index());
}

void BaseViewModel::activate_home_item() {
    switch (model_.home_index()) {
        case 0:
            navigate_to(model::AppPage::Read);
            break;
        case 1:
            navigate_to(model::AppPage::ReadRaw);
            break;
        case 2:
            navigate_to(model::AppPage::Saved);
            break;
        case 3:
            navigate_to(model::AppPage::Analyzer);
            break;
        case 4:
            navigate_to(model::AppPage::AddManual);
            break;
        case 5:
        default:
            navigate_to(model::AppPage::RadioInfo);
            break;
    }
}

void BaseViewModel::step_frequency(int delta) {
    model_.step_frequency(delta);
    sync_worker_mode();
    publish_all();
}

void BaseViewModel::cycle_preset() {
    model_.cycle_preset();
    sync_worker_mode();
    publish_all();
}

void BaseViewModel::toggle_hopping() {
    model_.toggle_hopping();
    sync_worker_mode();
    publish_all();
}

void BaseViewModel::transmit_signal(const model::SubGhzSignal& sig) {
    if (!worker_) return;
    radio::TransmitSignalCommand cmd;
    cmd.signal = sig;
    cmd.repeat_count = std::clamp<uint32_t>(sig.repeat_count, 1U, 10U);
    worker_->postCommand(std::move(cmd));
}

bool BaseViewModel::save_signal_to_library(const model::SubGhzSignal& sig, std::string& out_saved_path) {
    const auto dir = model_.writable_subghz_root();
    std::string base_name = sig.name.empty() ? sig.protocol : sig.name;
    for (char& c : base_name) {
        if (c == ' ' || c == '/' || c == '\\') c = '_';
    }
    const std::string filename = base_name + "_" + std::to_string(save_sequence_++) + ".sub";
    const auto full_path = dir / filename;
    std::string err;
    if (!model::save_sub_file(full_path, sig, err)) {
        emit_toast("Save failed: " + err);
        return false;
    }
#if !USE_DESKTOP
    model::chown_to_user_home_owner(full_path, model::resolve_user_home_dir());
#endif
    out_saved_path = full_path.string();
    model_.refresh_file_entries();
    emit_toast("Saved " + filename);
    return true;
}

void BaseViewModel::retry_radio_init() {
    if (worker_) {
        worker_->postCommand(radio::RetryInitCommand{});
        emit_toast("Re-initializing CC1101...");
    }
}

void BaseViewModel::sync_worker_mode() {
    if (!worker_) return;
    radio::SetModeCommand cmd;
    cmd.frequency_hz = model_.frequency_hz();
    cmd.preset = model_.preset();
    cmd.hopping = model_.hopping();
    cmd.rssi_threshold_dbm = model_.rssi_threshold_dbm();
    cmd.power_dbm = model_.tx_power_dbm();

    switch (model_.current_page()) {
        case model::AppPage::Read:
            cmd.mode = radio::WorkerMode::Read;
            break;
        case model::AppPage::ReadRaw:
            cmd.mode = model_.raw_recording() ? radio::WorkerMode::ReadRaw : radio::WorkerMode::Idle;
            break;
        case model::AppPage::Analyzer:
            cmd.mode = radio::WorkerMode::FrequencyAnalyzer;
            break;
        default:
            cmd.mode = radio::WorkerMode::Idle;
            break;
    }
    worker_->postCommand(cmd);
}

void BaseViewModel::poll_radio_events() {
    if (!worker_) return;
    radio::WorkerEvent ev;
    bool changed = false;
    while (worker_->tryPopEvent(ev)) {
        if (auto* state = std::get_if<radio::WorkerStateEvent>(&ev)) {
            hw_info_ = state->hw_info;
            radio_status_subject_.set(state->status_text.c_str());
            changed = true;
        } else if (auto* rssi = std::get_if<radio::RssiSampleEvent>(&ev)) {
            char buf[24]{};
            std::snprintf(buf, sizeof(buf), "%.0f dBm", static_cast<double>(rssi->rssi_dbm));
            rssi_dbm_text_subject_.set(buf);
            rssi_percent_subject_.set(rssi_to_percent(rssi->rssi_dbm));
            if (model_.hopping()) {
                frequency_text_subject_.set(model::format_frequency_mhz(rssi->frequency_hz).c_str());
            }
        } else if (auto* decoded = std::get_if<radio::DecodedSignalEvent>(&ev)) {
            model_.add_captured_signal(decoded->signal);
            changed = true;
        } else if (auto* raw_prog = std::get_if<radio::RawProgressEvent>(&ev)) {
            const int32_t pct = rssi_to_percent(raw_prog->rssi_dbm);
            model_.push_raw_waveform_sample(pct);
            rssi_percent_subject_.set(pct);
            char buf[64]{};
            std::snprintf(buf,
                          sizeof(buf),
                          "REC %.0fdBm | %zup (%.1fs)",
                          static_cast<double>(model_.rssi_threshold_dbm()),
                          raw_prog->pulse_count,
                          static_cast<double>(raw_prog->duration_us) / 1000000.0);
            raw_status_subject_.set(buf);
            changed = true;
        } else if (auto* raw_done = std::get_if<radio::RawCompleteEvent>(&ev)) {
            model_.set_last_raw_signal(raw_done->raw_signal);
            char buf[64]{};
            std::snprintf(buf,
                          sizeof(buf),
                          "READY  %zu pulses (%.2fs)",
                          model_.last_raw_signal().raw_timings.size(),
                          static_cast<double>(model_.last_raw_signal().total_duration_us()) / 1000000.0);
            raw_status_subject_.set(buf);
            changed = true;
        } else if (auto* ana = std::get_if<radio::AnalyzerReadingEvent>(&ev)) {
            analyzer_freq_subject_.set(model::format_frequency_mhz(ana->best_frequency_hz).c_str());
            rssi_percent_subject_.set(rssi_to_percent(ana->best_rssi_dbm));
            char rbuf[24]{};
            std::snprintf(rbuf, sizeof(rbuf), "%.0f dBm", static_cast<double>(ana->best_rssi_dbm));
            rssi_dbm_text_subject_.set(rbuf);
            if (ana->best_rssi_dbm > analyzer_peak_rssi_) {
                analyzer_peak_rssi_ = ana->best_rssi_dbm;
                analyzer_peak_hz_ = ana->best_frequency_hz;
                char pbuf[64]{};
                std::snprintf(pbuf,
                              sizeof(pbuf),
                              "Peak: %s (%.0f dBm)",
                              model::format_frequency_mhz(analyzer_peak_hz_).c_str(),
                              static_cast<double>(analyzer_peak_rssi_));
                analyzer_peak_subject_.set(pbuf);
            }
        } else if (auto* tx = std::get_if<radio::TxFinishedEvent>(&ev)) {
            emit_toast(tx->message);
        }
    }
    if (changed) {
        publish_all();
    }
}

bool BaseViewModel::handle_screen_key(uint32_t key) {
    const auto page = model_.current_page();

    if (page == model::AppPage::Home) {
        switch (key) {
            case LV_KEY_LEFT:
            case 'z':
            case 'Z':
                select_home_item(model_.home_index() - 1);
                return true;
            case LV_KEY_RIGHT:
            case 'c':
            case 'C':
                select_home_item(model_.home_index() + 1);
                return true;
            case LV_KEY_UP:
            case 'f':
            case 'F':
                select_home_item(model_.home_index() - 3);
                return true;
            case LV_KEY_DOWN:
            case 'x':
            case 'X':
                select_home_item(model_.home_index() + 3);
                return true;
            case LV_KEY_ENTER:
            case ' ':
                activate_home_item();
                return true;
            case '1':
                select_home_item(0);
                activate_home_item();
                return true;
            case '2':
                select_home_item(1);
                activate_home_item();
                return true;
            case '3':
                select_home_item(2);
                activate_home_item();
                return true;
            default:
                return false;
        }
    }

    if (page == model::AppPage::Read) {
        switch (key) {
            case LV_KEY_UP:
                model_.set_selected_capture_index(model_.selected_capture_index() - 1);
                notify_ui_revision();
                return true;
            case LV_KEY_DOWN:
                model_.set_selected_capture_index(model_.selected_capture_index() + 1);
                notify_ui_revision();
                return true;
            case LV_KEY_LEFT:
            case 'z':
            case 'Z':
                step_frequency(-1);
                return true;
            case LV_KEY_RIGHT:
            case 'c':
            case 'C':
            case 'f':
            case 'F':
                step_frequency(1);
                return true;
            case 'p':
            case 'P':
                cycle_preset();
                return true;
            case 'm':
            case 'M':
                toggle_hopping();
                return true;
            case LV_KEY_ENTER:
            case ' ':
                if (const auto* cap = model_.selected_capture()) {
                    transmit_signal(*cap);
                } else {
                    emit_toast("No captured signal to send");
                }
                return true;
            case 's':
            case 'S':
                if (const auto* cap = model_.selected_capture()) {
                    std::string path;
                    save_signal_to_library(*cap, path);
                } else {
                    emit_toast("No captured signal to save");
                }
                return true;
            default:
                return false;
        }
    }

    if (page == model::AppPage::ReadRaw) {
        switch (key) {
            case LV_KEY_ENTER:
            case 'r':
            case 'R':
                model_.set_raw_recording(!model_.raw_recording());
                if (model_.raw_recording()) {
                    model_.clear_raw_waveform();
                }
                sync_worker_mode();
                publish_all();
                return true;
            case LV_KEY_LEFT:
            case 'z':
            case 'Z':
                step_frequency(-1);
                return true;
            case LV_KEY_RIGHT:
            case 'c':
            case 'C':
            case 'f':
            case 'F':
                step_frequency(1);
                return true;
            case 'p':
            case 'P':
                cycle_preset();
                return true;
            case 't':
            case 'T':
                model_.cycle_rssi_threshold();
                sync_worker_mode();
                publish_all();
                return true;
            case ' ':
                if (!model_.last_raw_signal().raw_timings.empty()) {
                    transmit_signal(model_.last_raw_signal());
                } else {
                    emit_toast("Record a RAW signal first (Enter/R)");
                }
                return true;
            case 's':
            case 'S':
                if (!model_.last_raw_signal().raw_timings.empty()) {
                    std::string path;
                    save_signal_to_library(model_.last_raw_signal(), path);
                } else {
                    emit_toast("Nothing recorded yet");
                }
                return true;
            default:
                return false;
        }
    }

    if (page == model::AppPage::Saved) {
        if (model_.saved_detail_open()) {
            switch (key) {
                case LV_KEY_ENTER:
                case ' ':
                    transmit_signal(model_.active_saved_signal());
                    return true;
                case LV_KEY_BACKSPACE:
                case LV_KEY_LEFT:
                case 'z':
                case 'Z':
                    model_.set_saved_detail_open(false);
                    notify_ui_revision();
                    return true;
                default:
                    return false;
            }
        } else {
            switch (key) {
                case LV_KEY_UP:
                case 'f':
                case 'F':
                    model_.set_selected_file_index(model_.selected_file_index() - 1);
                    notify_ui_revision();
                    return true;
                case LV_KEY_DOWN:
                case 'x':
                case 'X':
                    model_.set_selected_file_index(model_.selected_file_index() + 1);
                    notify_ui_revision();
                    return true;
                case LV_KEY_RIGHT:
                case 'c':
                case 'C':
                case LV_KEY_ENTER:
                case ' ': {
                    model::SubGhzSignal loaded;
                    std::string err;
                    if (!model_.enter_selected_entry(loaded, err) && !err.empty()) {
                        emit_toast(err);
                    }
                    notify_ui_revision();
                    return true;
                }
                case LV_KEY_LEFT:
                case 'z':
                case 'Z':
                case LV_KEY_BACKSPACE:
                    if (model_.navigate_parent_directory()) {
                        notify_ui_revision();
                    }
                    return true;
                default:
                    return false;
            }
        }
    }

    if (page == model::AppPage::Analyzer) {
        switch (key) {
            case LV_KEY_ENTER:
            case ' ':
                model_.set_frequency_hz(analyzer_peak_hz_);
                model_.set_hopping(false);
                navigate_to(model::AppPage::Read);
                emit_toast("Tuned Read to " + model::format_frequency_mhz(analyzer_peak_hz_));
                return true;
            case 'c':
            case 'C':
                analyzer_peak_rssi_ = -110.0f;
                analyzer_peak_subject_.set("Peak: --");
                return true;
            default:
                return false;
        }
    }

    if (page == model::AppPage::AddManual) {
        if ((key >= '0' && key <= '9') || (key >= 'a' && key <= 'f') || (key >= 'A' && key <= 'F')) {
            uint8_t nibble = 0;
            if (key >= '0' && key <= '9') nibble = static_cast<uint8_t>(key - '0');
            else if (key >= 'a' && key <= 'f') nibble = static_cast<uint8_t>(10 + (key - 'a'));
            else nibble = static_cast<uint8_t>(10 + (key - 'A'));
            model_.append_manual_hex_nibble(nibble);
            notify_ui_revision();
            return true;
        }
        switch (key) {
            case LV_KEY_UP:
                model_.set_manual_field_index(model_.manual_field_index() - 1);
                notify_ui_revision();
                return true;
            case LV_KEY_DOWN:
                model_.set_manual_field_index(model_.manual_field_index() + 1);
                notify_ui_revision();
                return true;
            case LV_KEY_LEFT:
            case 'z':
            case 'Z':
                if (model_.manual_field_index() == 0) model_.step_manual_protocol(-1);
                else if (model_.manual_field_index() == 1) model_.step_frequency(-1);
                else if (model_.manual_field_index() == 2) model_.cycle_preset();
                else model_.set_manual_key(model_.manual_key() - 1);
                publish_all();
                return true;
            case LV_KEY_RIGHT:
            case 'c':
            case 'C':
                if (model_.manual_field_index() == 0) model_.step_manual_protocol(1);
                else if (model_.manual_field_index() == 1) model_.step_frequency(1);
                else if (model_.manual_field_index() == 2) model_.cycle_preset();
                else model_.set_manual_key(model_.manual_key() + 1);
                publish_all();
                return true;
            case 'r':
            case 'R':
                model_.randomize_manual_key();
                notify_ui_revision();
                return true;
            case ' ':
                transmit_signal(model_.build_manual_signal());
                return true;
            case LV_KEY_ENTER:
            case 's':
            case 'S': {
                const auto sig = model_.build_manual_signal();
                std::string path;
                if (save_signal_to_library(sig, path)) {
                    model_.set_active_saved_signal(sig);
                    model_.set_saved_detail_open(true);
                    navigate_to(model::AppPage::Saved);
                }
                return true;
            }
            default:
                return false;
        }
    }

    if (page == model::AppPage::RadioInfo) {
        switch (key) {
            case LV_KEY_LEFT:
            case 'z':
            case 'Z':
                model_.step_tx_power(-1);
                sync_worker_mode();
                publish_all();
                return true;
            case LV_KEY_RIGHT:
            case 'c':
            case 'C':
                model_.step_tx_power(1);
                sync_worker_mode();
                publish_all();
                return true;
            case 'r':
            case 'R':
            case LV_KEY_ENTER:
                retry_radio_init();
                return true;
            default:
                return false;
        }
    }

    return false;
}

void BaseViewModel::publish_all() {
    title_subject_.set(model_.app_title());
    dark_mode_subject_.set(model_.dark_mode());
    current_page_subject_.set(static_cast<int32_t>(model_.current_page()));
    home_index_subject_.set(model_.home_index());
    frequency_text_subject_.set(model::format_frequency_mhz(model_.frequency_hz()).c_str());
    preset_text_subject_.set(model::preset_to_short_name(model_.preset()));
    hopping_subject_.set(model_.hopping());

    if (!model_.raw_recording()) {
        char buf[64]{};
        std::snprintf(buf,
                      sizeof(buf),
                      "IDLE %.0fdBm | %zup",
                      static_cast<double>(model_.rssi_threshold_dbm()),
                      model_.last_raw_signal().raw_timings.size());
        raw_status_subject_.set(buf);
    }

    char info_buf[160]{};
    std::snprintf(info_buf,
                  sizeof(info_buf),
                  "Backend: %s\nChip: PART=0x%02X VER=0x%02X MARC=0x%02X\nBand SW: RF_SW0=%d IOCFG2=0x%02X\nTX Power: %d dBm  (Z/C adjust, R retry)",
                  hw_info_.backend_name.c_str(),
                  static_cast<unsigned>(hw_info_.partnum),
                  static_cast<unsigned>(hw_info_.version),
                  static_cast<unsigned>(hw_info_.marcstate),
                  hw_info_.rf_sw0 ? 1 : 0,
                  static_cast<unsigned>(hw_info_.iocfg2),
                  static_cast<int>(model_.tx_power_dbm()));
    info_summary_subject_.set(info_buf);

    notify_ui_revision();
}

void BaseViewModel::notify_ui_revision() {
    ui_revision_subject_.set(ui_revision_subject_.value() + 1);
}

void BaseViewModel::emit_toast(const std::string& msg) {
    toast_message_subject_.set(msg.c_str());
}

}  // namespace viewmodel
