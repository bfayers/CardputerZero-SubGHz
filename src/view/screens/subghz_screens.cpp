/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "subghz_screens.h"

#include "asset_manager.h"
#include "bindings.h"
#include "subghz_protocols.h"
#include "theme.h"
#include "ui_const.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>

namespace screen {
namespace {

struct MenuCardSpec {
    const char* title;
    const char* subtitle;
};

constexpr std::array<MenuCardSpec, 6> kHomeCards = {{
    {"1. Read", "Decode keys"},
    {"2. Read RAW", "Raw pulses"},
    {"3. Saved", ".sub browser"},
    {"4. Analyzer", "Scan bands"},
    {"5. Manual", "Create .sub"},
    {"6. Info", "CC1101 status"},
}};

const lv_font_t* font_or_default(app::AssetManager& assets, int size, bool bold = true) {
    auto* f = bold ? assets.load_standard_font(size)
                   : assets.load_standard_font(size, app::StandardFontWeight::Regular);
    if (f) return f;
    if (size >= 18) return &lv_font_montserrat_20;
    if (size >= 14) return &lv_font_montserrat_14;
    return &lv_font_montserrat_12;
}

}  // namespace

// ============================================================================
// HomeScreen
// ============================================================================

HomeScreen::HomeScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BaseScreen(view_model, assets) {
    init();
}

void HomeScreen::build_content(lv_obj_t* content) {
    auto* font_bold = font_or_default(assets(), 12, true);
    auto* font_sub = font_or_default(assets(), 10, false);

    // Status strip at top (308x18)
    auto* status_row = lv_obj_create(content);
    lv_obj_remove_style_all(status_row);
    lv_obj_set_size(status_row, 308, 18);
    lv_obj_align(status_row, LV_ALIGN_TOP_MID, 0, 3);
    lv_obj_clear_flag(status_row, LV_OBJ_FLAG_SCROLLABLE);

    auto* status_lbl = lv_label_create(status_row);
    lv_label_bind_text(status_lbl, view_model().radio_status_subject(), "RF: %s");
    lv_obj_set_style_text_font(status_lbl, font_sub, 0);
    lv_obj_align(status_lbl, LV_ALIGN_LEFT_MID, 2, 0);
    reactive::bind_theme(status_lbl, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    auto* tune_lbl = lv_label_create(status_row);
    lv_label_bind_text(tune_lbl, view_model().frequency_text_subject(), "%s");
    lv_obj_set_style_text_font(tune_lbl, font_bold, 0);
    lv_obj_align(tune_lbl, LV_ALIGN_RIGHT_MID, -2, 0);
    reactive::bind_theme(tune_lbl, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    // 2x3 Grid of cards (308x96)
    auto* grid = lv_obj_create(content);
    lv_obj_remove_style_all(grid);
    lv_obj_set_size(grid, 308, 96);
    lv_obj_align(grid, LV_ALIGN_BOTTOM_MID, 0, -3);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_SPACE_BETWEEN);
    lv_obj_set_style_pad_row(grid, 4, 0);
    lv_obj_set_style_pad_column(grid, 4, 0);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);

    for (std::size_t i = 0; i < kHomeCards.size(); ++i) {
        auto* card = lv_obj_create(grid);
        lv_obj_remove_style_all(card);
        lv_obj_set_size(card, 99, 45);
        lv_obj_set_style_radius(card, 5, 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(card, 1, 0);
        lv_obj_set_style_pad_all(card, 5, 0);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_user_data(card, reinterpret_cast<void*>(static_cast<uintptr_t>(i)));
        lv_obj_add_event_cb(card, card_clicked_cb, LV_EVENT_CLICKED, this);

        auto* title = lv_label_create(card);
        lv_label_set_text(title, kHomeCards[i].title);
        lv_obj_set_style_text_font(title, font_bold, 0);
        lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 1);

        auto* sub = lv_label_create(card);
        lv_label_set_text(sub, kHomeCards[i].subtitle);
        lv_obj_set_style_text_font(sub, font_sub, 0);
        lv_obj_align(sub, LV_ALIGN_BOTTOM_LEFT, 0, -1);

        cards_[i] = card;
        titles_[i] = title;
        subs_[i] = sub;
    }

    reactive::observe_obj(content, view_model().home_index_subject(), update_cb, this);
    reactive::observe_obj(content, view_model().dark_mode_subject(), update_cb, this);
    refresh_selection();
}

void HomeScreen::refresh_selection() {
    const auto colors = view::palette(view_model().is_dark_mode());
    const int32_t selected = view_model().model().home_index();
    for (std::size_t i = 0; i < cards_.size(); ++i) {
        const bool active = static_cast<int32_t>(i) == selected;
        lv_obj_set_style_bg_color(cards_[i], active ? colors.primary : colors.button, 0);
        lv_obj_set_style_border_color(cards_[i], active ? colors.primary : colors.border, 0);
        lv_obj_set_style_text_color(titles_[i], active ? colors.background : colors.text, 0);
        lv_obj_set_style_text_color(subs_[i], active ? colors.background : colors.text_disabled, 0);
    }
}

void HomeScreen::update_cb(lv_observer_t* observer, lv_subject_t*) {
    auto* self = static_cast<HomeScreen*>(lv_observer_get_user_data(observer));
    if (self) self->refresh_selection();
}

void HomeScreen::card_clicked_cb(lv_event_t* event) {
    auto* self = static_cast<HomeScreen*>(lv_event_get_user_data(event));
    auto* target = static_cast<lv_obj_t*>(lv_event_get_target(event));
    if (!self || !target) return;
    const auto idx = static_cast<int32_t>(reinterpret_cast<uintptr_t>(lv_obj_get_user_data(target)));
    self->view_model().select_home_item(idx);
    self->view_model().activate_home_item();
}

// ============================================================================
// ReadScreen
// ============================================================================

ReadScreen::ReadScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BaseScreen(view_model, assets) {
    init();
}

void ReadScreen::build_content(lv_obj_t* content) {
    auto* font_bold = font_or_default(assets(), 12, true);
    auto* font_reg = font_or_default(assets(), 11, false);

    // Header bar: Freq | Preset | Hop | RSSI Bar | RSSI dBm
    auto* top = lv_obj_create(content);
    lv_obj_remove_style_all(top);
    lv_obj_set_size(top, 308, 22);
    lv_obj_align(top, LV_ALIGN_TOP_MID, 0, 2);
    lv_obj_clear_flag(top, LV_OBJ_FLAG_SCROLLABLE);

    auto* freq_lbl = lv_label_create(top);
    lv_label_bind_text(freq_lbl, view_model().frequency_text_subject(), "%s");
    lv_obj_set_style_text_font(freq_lbl, font_bold, 0);
    lv_obj_align(freq_lbl, LV_ALIGN_LEFT_MID, 0, 0);
    reactive::bind_theme(freq_lbl, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    auto* preset_lbl = lv_label_create(top);
    lv_label_bind_text(preset_lbl, view_model().preset_text_subject(), "[%s]");
    lv_obj_set_style_text_font(preset_lbl, font_bold, 0);
    lv_obj_align(preset_lbl, LV_ALIGN_LEFT_MID, 76, 0);
    reactive::bind_theme(preset_lbl, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    hop_badge_ = lv_label_create(top);
    lv_obj_set_style_text_font(hop_badge_, font_reg, 0);
    lv_obj_align(hop_badge_, LV_ALIGN_LEFT_MID, 130, 0);
    reactive::bind_bool_text(hop_badge_, view_model().hopping_subject(), "FIXED", "HOP");
    reactive::bind_theme(hop_badge_, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    auto* rssi_bar = lv_bar_create(top);
    lv_obj_set_size(rssi_bar, 68, 8);
    lv_bar_set_range(rssi_bar, 0, 100);
    lv_obj_align(rssi_bar, LV_ALIGN_RIGHT_MID, -52, 0);
    reactive::bind_bar_value(rssi_bar, view_model().rssi_percent_subject());

    auto* rssi_lbl = lv_label_create(top);
    lv_label_bind_text(rssi_lbl, view_model().rssi_dbm_text_subject(), "%s");
    lv_obj_set_style_text_font(rssi_lbl, font_reg, 0);
    lv_obj_align(rssi_lbl, LV_ALIGN_RIGHT_MID, 0, 0);
    reactive::bind_theme(rssi_lbl, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    // Captured signals list container (308x94)
    auto* list_box = lv_obj_create(content);
    lv_obj_remove_style_all(list_box);
    lv_obj_set_size(list_box, 308, 94);
    lv_obj_align(list_box, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_obj_set_flex_flow(list_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list_box, 2, 0);
    lv_obj_clear_flag(list_box, LV_OBJ_FLAG_SCROLLABLE);

    empty_label_ = lv_label_create(content);
    lv_label_set_text(empty_label_, "Listening for Sub-GHz remotes...\nPress F to change Freq, P for Preset, M for Hop");
    lv_obj_set_style_text_align(empty_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(empty_label_, font_reg, 0);
    lv_obj_align(empty_label_, LV_ALIGN_CENTER, 0, 10);
    reactive::bind_theme(empty_label_, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    for (std::size_t i = 0; i < rows_.size(); ++i) {
        auto* row = lv_obj_create(list_box);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, 308, 21);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_hor(row, 6, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        auto* lbl = lv_label_create(row);
        lv_obj_set_width(lbl, 296);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_font(lbl, font_reg, 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);

        rows_[i] = row;
        row_labels_[i] = lbl;
    }

    reactive::observe_obj(content, view_model().ui_revision_subject(), update_cb, this);
    reactive::observe_obj(content, view_model().dark_mode_subject(), update_cb, this);
    refresh_list();
}

void ReadScreen::refresh_list() {
    const auto colors = view::palette(view_model().is_dark_mode());
    const auto& captures = view_model().model().captured_signals();
    const int32_t sel = view_model().model().selected_capture_index();

    if (captures.empty()) {
        lv_obj_remove_flag(empty_label_, LV_OBJ_FLAG_HIDDEN);
        for (auto* row : rows_) lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_add_flag(empty_label_, LV_OBJ_FLAG_HIDDEN);
    const int32_t window_start = std::max<int32_t>(0, sel - 3);
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        const int32_t idx = window_start + static_cast<int32_t>(i);
        if (static_cast<std::size_t>(idx) >= captures.size()) {
            lv_obj_add_flag(rows_[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_remove_flag(rows_[i], LV_OBJ_FLAG_HIDDEN);
        const auto& sig = captures[static_cast<std::size_t>(idx)];
        const bool active = (idx == sel);
        lv_obj_set_style_bg_color(rows_[i], active ? colors.primary : colors.button, 0);
        lv_obj_set_style_text_color(row_labels_[i], active ? colors.background : colors.text, 0);

        char text[128]{};
        if (sig.protocol == "BinRAW") {
            std::snprintf(text,
                          sizeof(text),
                          "%s %ub  TE:%uus  %s (%s)",
                          sig.protocol.c_str(),
                          sig.bit_count,
                          sig.te_us,
                          model::format_frequency_mhz(sig.frequency_hz).c_str(),
                          model::preset_to_short_name(sig.preset));
        } else {
            std::snprintf(text,
                          sizeof(text),
                          "%s %ub  Key:%06llX  %s",
                          sig.protocol.c_str(),
                          sig.bit_count,
                          static_cast<unsigned long long>(sig.key & 0xFFFFFFULL),
                          model::format_frequency_mhz(sig.frequency_hz).c_str());
        }
        lv_label_set_text(row_labels_[i], text);
    }
}

void ReadScreen::update_cb(lv_observer_t* observer, lv_subject_t*) {
    auto* self = static_cast<ReadScreen*>(lv_observer_get_user_data(observer));
    if (self) self->refresh_list();
}

// ============================================================================
// ReadRawScreen
// ============================================================================

ReadRawScreen::ReadRawScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BaseScreen(view_model, assets) {
    init();
}

void ReadRawScreen::build_content(lv_obj_t* content) {
    auto* font_bold = font_or_default(assets(), 12, true);
    auto* font_reg = font_or_default(assets(), 11, false);

    auto* top = lv_obj_create(content);
    lv_obj_remove_style_all(top);
    lv_obj_set_size(top, 308, 20);
    lv_obj_align(top, LV_ALIGN_TOP_MID, 0, 2);
    lv_obj_clear_flag(top, LV_OBJ_FLAG_SCROLLABLE);

    auto* freq_lbl = lv_label_create(top);
    lv_label_bind_text(freq_lbl, view_model().frequency_text_subject(), "%s");
    lv_obj_set_style_text_font(freq_lbl, font_bold, 0);
    lv_obj_align(freq_lbl, LV_ALIGN_LEFT_MID, 0, 0);
    reactive::bind_theme(freq_lbl, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    auto* preset_lbl = lv_label_create(top);
    lv_label_bind_text(preset_lbl, view_model().preset_text_subject(), "[%s]");
    lv_obj_set_style_text_font(preset_lbl, font_bold, 0);
    lv_obj_align(preset_lbl, LV_ALIGN_LEFT_MID, 76, 0);
    reactive::bind_theme(preset_lbl, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    auto* status_lbl = lv_label_create(top);
    lv_label_bind_text(status_lbl, view_model().raw_status_subject(), "%s");
    lv_obj_set_style_text_font(status_lbl, font_reg, 0);
    lv_obj_align(status_lbl, LV_ALIGN_RIGHT_MID, 0, 0);
    reactive::bind_theme(status_lbl, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    // Waveform Scope Box (308x72)
    auto* scope = lv_obj_create(content);
    lv_obj_remove_style_all(scope);
    lv_obj_set_size(scope, 308, 72);
    lv_obj_align(scope, LV_ALIGN_CENTER, 0, 2);
    lv_obj_set_style_radius(scope, 4, 0);
    lv_obj_set_style_border_width(scope, 1, 0);
    lv_obj_set_style_pad_hor(scope, 6, 0);
    lv_obj_set_style_pad_ver(scope, 4, 0);
    lv_obj_clear_flag(scope, LV_OBJ_FLAG_SCROLLABLE);
    reactive::bind_theme(scope, view_model().dark_mode_subject(), reactive::ThemeRole::Button);

    for (std::size_t i = 0; i < bars_.size(); ++i) {
        auto* bar = lv_obj_create(scope);
        lv_obj_remove_style_all(bar);
        lv_obj_set_size(bar, 5, 6);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(bar, 1, 0);
        lv_obj_align(bar, LV_ALIGN_BOTTOM_LEFT, static_cast<int32_t>(i * 7 + 4), 0);
        bars_[i] = bar;
    }

    auto* hint = lv_label_create(content);
    lv_label_set_text(hint, "F: Step Freq   P: Preset   T: RSSI Threshold");
    lv_obj_set_style_text_font(hint, font_or_default(assets(), 10, false), 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -2);
    reactive::bind_theme(hint, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    reactive::observe_obj(content, view_model().ui_revision_subject(), update_cb, this);
    reactive::observe_obj(content, view_model().dark_mode_subject(), update_cb, this);
    refresh_waveform();
}

void ReadRawScreen::refresh_waveform() {
    const auto colors = view::palette(view_model().is_dark_mode());
    const auto& wf = view_model().model().raw_waveform();
    const bool rec = view_model().model().raw_recording();
    for (std::size_t i = 0; i < bars_.size(); ++i) {
        const int32_t h = std::clamp<int32_t>((wf[i] * 60) / 100, 3, 60);
        lv_obj_set_height(bars_[i], h);
        lv_obj_align(bars_[i], LV_ALIGN_BOTTOM_LEFT, static_cast<int32_t>(i * 7 + 4), 0);
        lv_obj_set_style_bg_color(bars_[i], rec ? colors.warning : colors.primary, 0);
    }
}

void ReadRawScreen::update_cb(lv_observer_t* observer, lv_subject_t*) {
    auto* self = static_cast<ReadRawScreen*>(lv_observer_get_user_data(observer));
    if (self) self->refresh_waveform();
}

// ============================================================================
// SavedScreen (.sub Browser & Signal Inspector)
// ============================================================================

SavedScreen::SavedScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BaseScreen(view_model, assets) {
    init();
}

void SavedScreen::build_content(lv_obj_t* content) {
    auto* font_bold = font_or_default(assets(), 12, true);
    auto* font_reg = font_or_default(assets(), 11, false);

    // 1. Browser List Container
    list_container_ = lv_obj_create(content);
    lv_obj_remove_style_all(list_container_);
    lv_obj_set_size(list_container_, 308, 116);
    lv_obj_align(list_container_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(list_container_, LV_OBJ_FLAG_SCROLLABLE);

    path_label_ = lv_label_create(list_container_);
    lv_obj_set_size(path_label_, 304, 16);
    lv_label_set_long_mode(path_label_, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(path_label_, font_bold, 0);
    lv_obj_align(path_label_, LV_ALIGN_TOP_LEFT, 2, 1);
    reactive::bind_theme(path_label_, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    for (std::size_t i = 0; i < rows_.size(); ++i) {
        auto* row = lv_obj_create(list_container_);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, 308, 22);
        lv_obj_align(row, LV_ALIGN_TOP_MID, 0, static_cast<int32_t>(20 + i * 24));
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_hor(row, 6, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        auto* badge = lv_label_create(row);
        lv_obj_set_style_text_font(badge, font_bold, 0);
        lv_obj_align(badge, LV_ALIGN_LEFT_MID, 0, 0);

        auto* name = lv_label_create(row);
        lv_obj_set_size(name, 248, 16);
        lv_label_set_long_mode(name, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_font(name, font_reg, 0);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 46, 0);

        rows_[i] = row;
        row_badges_[i] = badge;
        row_names_[i] = name;
    }

    // 2. Signal Inspector & Transmitter Card
    detail_card_ = lv_obj_create(content);
    lv_obj_remove_style_all(detail_card_);
    lv_obj_set_size(detail_card_, 308, 114);
    lv_obj_align(detail_card_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_radius(detail_card_, 5, 0);
    lv_obj_set_style_border_width(detail_card_, 1, 0);
    lv_obj_set_style_pad_all(detail_card_, 8, 0);
    lv_obj_clear_flag(detail_card_, LV_OBJ_FLAG_SCROLLABLE);
    reactive::bind_theme(detail_card_, view_model().dark_mode_subject(), reactive::ThemeRole::Button);

    detail_title_ = lv_label_create(detail_card_);
    lv_obj_set_size(detail_title_, 290, 18);
    lv_label_set_long_mode(detail_title_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_font(detail_title_, font_or_default(assets(), 14, true), 0);
    lv_obj_align(detail_title_, LV_ALIGN_TOP_LEFT, 0, 0);
    reactive::bind_theme(detail_title_, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    detail_meta1_ = lv_label_create(detail_card_);
    lv_obj_set_style_text_font(detail_meta1_, font_bold, 0);
    lv_obj_align(detail_meta1_, LV_ALIGN_TOP_LEFT, 0, 24);
    reactive::bind_theme(detail_meta1_, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    detail_meta2_ = lv_label_create(detail_card_);
    lv_obj_set_style_text_font(detail_meta2_, font_reg, 0);
    lv_obj_align(detail_meta2_, LV_ALIGN_TOP_LEFT, 0, 44);
    reactive::bind_theme(detail_meta2_, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    detail_key_ = lv_label_create(detail_card_);
    lv_obj_set_width(detail_key_, 290);
    lv_label_set_long_mode(detail_key_, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(detail_key_, font_bold, 0);
    lv_obj_align(detail_key_, LV_ALIGN_TOP_LEFT, 0, 64);

    auto* tx_prompt = lv_label_create(detail_card_);
    lv_label_set_text(tx_prompt, "Press ENTER or SPACE to Transmit via CC1101");
    lv_obj_set_style_text_font(tx_prompt, font_reg, 0);
    lv_obj_align(tx_prompt, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    reactive::bind_theme(tx_prompt, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    reactive::observe_obj(content, view_model().ui_revision_subject(), update_cb, this);
    reactive::observe_obj(content, view_model().dark_mode_subject(), update_cb, this);
    refresh_view();
}

void SavedScreen::refresh_view() {
    const auto colors = view::palette(view_model().is_dark_mode());
    const auto& m = view_model().model();

    if (m.saved_detail_open()) {
        lv_obj_add_flag(list_container_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(detail_card_, LV_OBJ_FLAG_HIDDEN);

        const auto& sig = m.active_saved_signal();
        lv_label_set_text_fmt(detail_title_,
                              "%s.sub  [%s]",
                              sig.name.c_str(),
                              sig.file_type == model::SubGhzFileType::RawFile ? "RAW" : "KEY");

        char m1[96]{};
        std::snprintf(m1,
                      sizeof(m1),
                      "Freq: %s   Preset: %s",
                      model::format_frequency_mhz(sig.frequency_hz).c_str(),
                      model::preset_to_short_name(sig.preset));
        lv_label_set_text(detail_meta1_, m1);

        char m2[96]{};
        if (sig.protocol == "RAW") {
            std::snprintf(m2,
                          sizeof(m2),
                          "Protocol: RAW   Pulses: %zu   Duration: %.1f ms",
                          sig.raw_timings.size(),
                          static_cast<double>(sig.total_duration_us()) / 1000.0);
            lv_label_set_text(detail_meta2_, m2);

            char kbuf[96]{};
            const int32_t t0 = sig.raw_timings.size() > 0 ? sig.raw_timings[0] : 0;
            const int32_t t1 = sig.raw_timings.size() > 1 ? sig.raw_timings[1] : 0;
            const int32_t t2 = sig.raw_timings.size() > 2 ? sig.raw_timings[2] : 0;
            const int32_t t3 = sig.raw_timings.size() > 3 ? sig.raw_timings[3] : 0;
            std::snprintf(kbuf, sizeof(kbuf), "RAW_Data: %d %d %d %d ...", t0, t1, t2, t3);
            lv_label_set_text(detail_key_, kbuf);
        } else if (sig.protocol == "BinRAW") {
            std::snprintf(m2,
                          sizeof(m2),
                          "Protocol: BinRAW   Bit_RAW: %u   TE: %u us",
                          sig.bit_raw > 0 ? sig.bit_raw : sig.bit_count,
                          sig.te_us);
            lv_label_set_text(detail_meta2_, m2);
            lv_label_set_text_fmt(
                detail_key_, "Synthesized Pulses: %zu (%llu us)", sig.raw_timings.size(), static_cast<unsigned long long>(sig.total_duration_us()));
        } else {
            std::snprintf(m2,
                          sizeof(m2),
                          "Protocol: %s   Bit: %u   TE: %u us",
                          sig.protocol.c_str(),
                          sig.bit_count,
                          sig.te_us > 0 ? sig.te_us : 400U);
            lv_label_set_text(detail_meta2_, m2);
            const std::string hex = model::format_key_hex(sig.key, sig.bit_count, sig.key_bytes);
            lv_label_set_text_fmt(detail_key_, "Key: %s", hex.c_str());
        }
        lv_obj_set_style_text_color(detail_key_, colors.primary, 0);
        return;
    }

    lv_obj_remove_flag(list_container_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(detail_card_, LV_OBJ_FLAG_HIDDEN);

    const auto& entries = m.file_entries();
    const int32_t sel = m.selected_file_index();
    const std::string dir_name = m.current_directory().filename().string();
    lv_label_set_text_fmt(path_label_, "Dir: /%s (%zu items)", dir_name.c_str(), entries.size());

    const int32_t start = std::max<int32_t>(0, sel - 3);
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        const int32_t idx = start + static_cast<int32_t>(i);
        if (static_cast<std::size_t>(idx) >= entries.size()) {
            lv_obj_add_flag(rows_[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_remove_flag(rows_[i], LV_OBJ_FLAG_HIDDEN);
        const auto& item = entries[static_cast<std::size_t>(idx)];
        const bool active = (idx == sel);
        lv_obj_set_style_bg_color(rows_[i], active ? colors.primary : colors.button, 0);
        lv_label_set_text(row_badges_[i], item.is_directory ? "[DIR]" : "[SUB]");
        lv_label_set_long_mode(row_names_[i], active ? LV_LABEL_LONG_SCROLL_CIRCULAR : LV_LABEL_LONG_CLIP);
        lv_label_set_text(row_names_[i], item.name.c_str());
        lv_obj_set_style_text_color(row_badges_[i], active ? colors.background : colors.primary, 0);
        lv_obj_set_style_text_color(row_names_[i], active ? colors.background : colors.text, 0);
    }
}

void SavedScreen::update_cb(lv_observer_t* observer, lv_subject_t*) {
    auto* self = static_cast<SavedScreen*>(lv_observer_get_user_data(observer));
    if (self) self->refresh_view();
}

// ============================================================================
// AnalyzerScreen
// ============================================================================

AnalyzerScreen::AnalyzerScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BaseScreen(view_model, assets) {
    init();
}

void AnalyzerScreen::build_content(lv_obj_t* content) {
    auto* font_big = font_or_default(assets(), 20, true);
    auto* font_bold = font_or_default(assets(), 13, true);
    auto* font_reg = font_or_default(assets(), 11, false);

    auto* card = lv_obj_create(content);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, 304, 110);
    lv_obj_center(card);
    lv_obj_set_style_radius(card, 6, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    reactive::bind_theme(card, view_model().dark_mode_subject(), reactive::ThemeRole::Button);

    auto* freq_lbl = lv_label_create(card);
    lv_label_bind_text(freq_lbl, view_model().analyzer_freq_subject(), "%s");
    lv_obj_set_style_text_font(freq_lbl, font_big, 0);
    lv_obj_align(freq_lbl, LV_ALIGN_TOP_MID, 0, 2);
    reactive::bind_theme(freq_lbl, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    auto* rssi_bar = lv_bar_create(card);
    lv_obj_set_size(rssi_bar, 210, 12);
    lv_bar_set_range(rssi_bar, 0, 100);
    lv_obj_align(rssi_bar, LV_ALIGN_TOP_LEFT, 6, 36);
    reactive::bind_bar_value(rssi_bar, view_model().rssi_percent_subject());

    auto* rssi_lbl = lv_label_create(card);
    lv_label_bind_text(rssi_lbl, view_model().rssi_dbm_text_subject(), "%s");
    lv_obj_set_style_text_font(rssi_lbl, font_bold, 0);
    lv_obj_align(rssi_lbl, LV_ALIGN_TOP_RIGHT, -4, 34);
    reactive::bind_theme(rssi_lbl, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    auto* peak_lbl = lv_label_create(card);
    lv_label_bind_text(peak_lbl, view_model().analyzer_peak_subject(), "%s");
    lv_obj_set_style_text_font(peak_lbl, font_bold, 0);
    lv_obj_align(peak_lbl, LV_ALIGN_TOP_MID, 0, 58);
    reactive::bind_theme(peak_lbl, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    auto* hint = lv_label_create(card);
    lv_label_set_text(hint, "Sweeping 300-928 MHz  (ENT: Tune Read)");
    lv_obj_set_style_text_font(hint, font_reg, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, 0);
    reactive::bind_theme(hint, view_model().dark_mode_subject(), reactive::ThemeRole::Text);
}

// ============================================================================
// AddManualScreen
// ============================================================================

AddManualScreen::AddManualScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BaseScreen(view_model, assets) {
    init();
}

void AddManualScreen::build_content(lv_obj_t* content) {
    auto* font_bold = font_or_default(assets(), 12, true);
    auto* font_reg = font_or_default(assets(), 12, false);

    for (std::size_t i = 0; i < rows_.size(); ++i) {
        auto* row = lv_obj_create(content);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, 308, 22);
        lv_obj_align(row, LV_ALIGN_TOP_MID, 0, static_cast<int32_t>(4 + i * 24));
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_hor(row, 8, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        auto* k = lv_label_create(row);
        lv_obj_set_style_text_font(k, font_bold, 0);
        lv_obj_align(k, LV_ALIGN_LEFT_MID, 0, 0);

        auto* v = lv_label_create(row);
        lv_obj_set_style_text_font(v, font_reg, 0);
        lv_obj_align(v, LV_ALIGN_RIGHT_MID, 0, 0);

        rows_[i] = row;
        keys_[i] = k;
        vals_[i] = v;
    }

    auto* hint = lv_label_create(content);
    lv_label_set_text(hint, "0-F: Hex Key   R: Random   SPC: Test TX");
    lv_obj_set_style_text_font(hint, font_or_default(assets(), 10, false), 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -2);
    reactive::bind_theme(hint, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    reactive::observe_obj(content, view_model().ui_revision_subject(), update_cb, this);
    reactive::observe_obj(content, view_model().dark_mode_subject(), update_cb, this);
    refresh_fields();
}

void AddManualScreen::refresh_fields() {
    const auto colors = view::palette(view_model().is_dark_mode());
    const auto& m = view_model().model();
    const auto& specs = model::manual_protocol_specs();
    const auto& spec = specs[static_cast<std::size_t>(m.manual_protocol_index())];

    const std::array<const char*, 4> labels = {"Protocol", "Frequency", "Preset", "Hex Key"};
    char proto_val[48]{};
    std::snprintf(proto_val, sizeof(proto_val), "< %s (%u-bit, TE %uus) >", spec.name, spec.bit_count, spec.default_te_us);
    const std::string freq_val = "< " + model::format_frequency_mhz(m.frequency_hz()) + " >";
    const std::string preset_val = std::string("< ") + model::preset_to_short_name(m.preset()) + " >";
    const std::string key_val = model::format_key_hex(m.manual_key(), spec.bit_count);

    const std::array<std::string, 4> values = {proto_val, freq_val, preset_val, key_val};
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        const bool active = (static_cast<int32_t>(i) == m.manual_field_index());
        lv_obj_set_style_bg_color(rows_[i], active ? colors.primary : colors.button, 0);
        lv_label_set_text(keys_[i], labels[i]);
        lv_label_set_text(vals_[i], values[i].c_str());
        lv_obj_set_style_text_color(keys_[i], active ? colors.background : colors.text, 0);
        lv_obj_set_style_text_color(vals_[i], active ? colors.background : colors.primary, 0);
    }
}

void AddManualScreen::update_cb(lv_observer_t* observer, lv_subject_t*) {
    auto* self = static_cast<AddManualScreen*>(lv_observer_get_user_data(observer));
    if (self) self->refresh_fields();
}

// ============================================================================
// RadioInfoScreen
// ============================================================================

RadioInfoScreen::RadioInfoScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BaseScreen(view_model, assets) {
    init();
}

void RadioInfoScreen::build_content(lv_obj_t* content) {
    auto* font_bold = font_or_default(assets(), 12, true);
    auto* font_reg = font_or_default(assets(), 11, false);

    auto* card = lv_obj_create(content);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, 308, 114);
    lv_obj_center(card);
    lv_obj_set_style_radius(card, 5, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    reactive::bind_theme(card, view_model().dark_mode_subject(), reactive::ThemeRole::Button);

    auto* status = lv_label_create(card);
    lv_label_bind_text(status, view_model().radio_status_subject(), "Status: %s");
    lv_obj_set_style_text_font(status, font_bold, 0);
    lv_obj_align(status, LV_ALIGN_TOP_LEFT, 0, 0);
    reactive::bind_theme(status, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    auto* details = lv_label_create(card);
    lv_label_bind_text(details, view_model().info_summary_subject(), "%s");
    lv_obj_set_style_text_font(details, font_reg, 0);
    lv_obj_align(details, LV_ALIGN_TOP_LEFT, 0, 22);
    reactive::bind_theme(details, view_model().dark_mode_subject(), reactive::ThemeRole::Text);
}

}  // namespace screen
