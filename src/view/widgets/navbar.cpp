/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "navbar.h"

#include "asset_manager.h"
#include "bindings.h"
#include "linux_input.h"
#include "theme.h"
#include "ui_const.h"

namespace view::widgets {
namespace {

struct IconSpec {
    const char* text;
    lv_event_cb_t click_cb;
};

constexpr std::array<int32_t, 5> kNavButtonXOffsets = {30, 17, 2, -15, -30};

struct ShortcutSpec {
    const char* key;
    const char* action;
};

std::array<ShortcutSpec, 3> shortcuts_for_page(model::AppPage page, bool saved_detail) {
    switch (page) {
        case model::AppPage::Read:
            return {{{"ESC", "Menu"}, {"F/P/M", "Tune"}, {"ENT/S", "TX/Save"}}};
        case model::AppPage::ReadRaw:
            return {{{"ESC", "Menu"}, {"ENT/R", "Rec"}, {"SPC/S", "TX/Save"}}};
        case model::AppPage::Saved:
            if (saved_detail) {
                return {{{"ESC/L", "List"}, {"ENT/SPC", "Transmit"}, {"", ""}}};
            }
            return {{{"ESC", "Menu"}, {"L/R", "Back/Open"}, {"UP/DN", "Scroll"}}};
        case model::AppPage::Analyzer:
            return {{{"ESC", "Menu"}, {"ENT", "Read@Peak"}, {"C", "Clear"}}};
        case model::AppPage::AddManual:
            return {{{"ESC", "Menu"}, {"R/HEX", "Key"}, {"ENT/SPC", "Save/TX"}}};
        case model::AppPage::RadioInfo:
            return {{{"ESC", "Menu"}, {"Z/C", "TX Pwr"}, {"R", "Re-init"}}};
        case model::AppPage::Home:
        default:
            return {{{"ESC", "Exit"}, {"ARROWS", "Move"}, {"ENT", "Open"}}};
    }
}

}  // namespace

NavBar::NavBar(lv_obj_t* parent, viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BaseWidgets(parent), view_model_(view_model), assets_(assets) {}

NavBar::~NavBar() {
    for (size_t i = 0; i < icon_buttons_.size(); ++i) {
        if (icon_buttons_[i]) {
            platform::unregister_nav_button(i, icon_buttons_[i]->root());
        }
    }

    if (page_observer_) {
        lv_observer_remove(page_observer_);
        page_observer_ = nullptr;
    }
    if (theme_observer_) {
        lv_observer_remove(theme_observer_);
        theme_observer_ = nullptr;
    }
    if (rev_observer_) {
        lv_observer_remove(rev_observer_);
        rev_observer_ = nullptr;
    }
}

void NavBar::build() {
    if (core_obj_) {
        return;
    }

    core_obj_ = lv_obj_create(parent_);
    lv_obj_remove_style_all(core_obj_);
    lv_obj_set_size(core_obj_, LV_PCT(100), kNavBarHeight);
    lv_obj_align(core_obj_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_clear_flag(core_obj_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(core_obj_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(core_obj_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(core_obj_, 8, 0);
    lv_obj_set_style_pad_right(core_obj_, 8, 0);
    reactive::bind_theme(core_obj_, view_model_.dark_mode_subject(), reactive::ThemeRole::Bar);

    create_icon_buttons();
    create_shortcut_hints();
    page_observer_ = reactive::observe_obj(core_obj_, view_model_.current_page_subject(), update_icons_cb, this);
    theme_observer_ = reactive::observe_obj(core_obj_, view_model_.dark_mode_subject(), update_icons_cb, this);
    rev_observer_ = reactive::observe_obj(core_obj_, view_model_.ui_revision_subject(), update_icons_cb, this);
    update_icon_buttons();
}

void NavBar::create_shortcut_hints() {
    shortcut_bar_ = lv_obj_create(core_obj_);
    lv_obj_remove_style_all(shortcut_bar_);
    lv_obj_set_size(shortcut_bar_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(shortcut_bar_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(shortcut_bar_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(shortcut_bar_, 6, 0);
    lv_obj_clear_flag(shortcut_bar_, LV_OBJ_FLAG_SCROLLABLE);

    const lv_font_t* key_font = assets_.load_standard_font(12);
    if (!key_font) {
        key_font = &lv_font_montserrat_12;
    }
    const lv_font_t* text_font = assets_.load_standard_font(12, app::StandardFontWeight::Regular);
    if (!text_font) {
        text_font = &lv_font_montserrat_12;
    }

    for (size_t index = 0; index < shortcut_key_labels_.size(); ++index) {
        auto* item = lv_obj_create(shortcut_bar_);
        lv_obj_remove_style_all(item);
        lv_obj_set_size(item, LV_SIZE_CONTENT, LV_PCT(100));
        lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(item, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(item, 3, 0);
        lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);

        auto* key = lv_label_create(item);
        lv_label_set_text(key, "");
        lv_obj_set_style_text_font(key, key_font, 0);
        shortcut_key_labels_[index] = key;

        auto* label = lv_label_create(item);
        lv_label_set_text(label, "");
        lv_obj_set_style_text_font(label, text_font, 0);
        reactive::bind_theme(label, view_model_.dark_mode_subject(), reactive::ThemeRole::Text);
        shortcut_action_labels_[index] = label;
    }
}

void NavBar::create_icon_buttons() {
    const auto light_color = view::palette(false).text;
    const auto dark_color = view::palette(true).text;
    icon_font_ = assets_.load_font("Phosphor-Fill.ttf", 20);

    for (size_t i = 0; i < icon_buttons_.size(); ++i) {
        icon_buttons_[i] = std::make_unique<IconButton>(core_obj_,
                                                        view_model_,
                                                        32,
                                                        22,
                                                        "",
                                                        icon_font_ ? icon_font_ : &lv_font_montserrat_14,
                                                        light_color,
                                                        dark_color,
                                                        nullptr,
                                                        &view_model_,
                                                        true);
        icon_buttons_[i]->build();
        lv_obj_set_style_translate_x(icon_buttons_[i]->root(), kNavButtonXOffsets[i], 0);
        platform::register_nav_button(i, icon_buttons_[i]->root());
    }
}

void NavBar::update_icon_buttons() {
    const auto page = view_model_.current_page();
    const bool is_home = page == model::AppPage::Home;
    const bool is_dark_mode = view_model_.is_dark_mode();
    const auto colors = view::palette(is_dark_mode);
    const char* theme_icon = is_dark_mode ? ICON_SUN : ICON_MOON;

    const std::array<IconSpec, 5> home_icons = {{
        {ICON_SIGN_OUT, request_quit_cb},
        {ICON_WIFI_HIGH, open_read_cb},
        {theme_icon, toggle_theme_cb},
        {ICON_INFO, show_info_cb},
        {ICON_SQUARE_ARROW_RIGHT, open_saved_cb},
    }};
    const std::array<IconSpec, 5> subpage_icons = {{
        {"", nullptr},
        {"", nullptr},
        {"", nullptr},
        {"", nullptr},
        {"", nullptr},
    }};

    platform::set_nav_shortcut_mode(!is_home);
    const auto specs = shortcuts_for_page(page, view_model_.model().saved_detail_open());
    for (size_t i = 0; i < shortcut_key_labels_.size(); ++i) {
        if (shortcut_key_labels_[i]) {
            lv_label_set_text(shortcut_key_labels_[i], specs[i].key);
            lv_obj_set_style_text_color(shortcut_key_labels_[i], colors.primary, 0);
        }
        if (shortcut_action_labels_[i]) {
            lv_label_set_text(shortcut_action_labels_[i], specs[i].action);
        }
    }

    if (shortcut_bar_) {
        if (!is_home) {
            lv_obj_remove_flag(shortcut_bar_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(shortcut_bar_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    const auto& icons = is_home ? home_icons : subpage_icons;
    for (size_t i = 0; i < icon_buttons_.size(); ++i) {
        auto& button = icon_buttons_[i];
        if (!button) continue;
        button->set_text(icons[i].text);
        if (button->root()) {
            if (!is_home) {
                lv_obj_add_flag(button->root(), LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_remove_flag(button->root(), LV_OBJ_FLAG_HIDDEN);
            }
            lv_obj_remove_event_cb(button->root(), toggle_theme_cb);
            lv_obj_remove_event_cb(button->root(), open_read_cb);
            lv_obj_remove_event_cb(button->root(), open_saved_cb);
            lv_obj_remove_event_cb(button->root(), show_info_cb);
            lv_obj_remove_event_cb(button->root(), request_quit_cb);
            if (icons[i].click_cb) {
                lv_obj_add_event_cb(button->root(), icons[i].click_cb, LV_EVENT_CLICKED, &view_model_);
                button->set_enabled(true);
            } else {
                button->set_enabled(false);
            }
        }
    }
}

void NavBar::toggle_theme_cb(lv_event_t* event) {
    auto* vm = static_cast<viewmodel::BaseViewModel*>(lv_event_get_user_data(event));
    if (vm) vm->toggle_dark_mode();
}

void NavBar::open_read_cb(lv_event_t* event) {
    auto* vm = static_cast<viewmodel::BaseViewModel*>(lv_event_get_user_data(event));
    if (vm) vm->navigate_to(model::AppPage::Read);
}

void NavBar::open_saved_cb(lv_event_t* event) {
    auto* vm = static_cast<viewmodel::BaseViewModel*>(lv_event_get_user_data(event));
    if (vm) vm->navigate_to(model::AppPage::Saved);
}

void NavBar::show_info_cb(lv_event_t* event) {
    auto* vm = static_cast<viewmodel::BaseViewModel*>(lv_event_get_user_data(event));
    if (vm) vm->navigate_to(model::AppPage::RadioInfo);
}

void NavBar::request_quit_cb(lv_event_t* event) {
    auto* vm = static_cast<viewmodel::BaseViewModel*>(lv_event_get_user_data(event));
    if (vm) vm->request_quit();
}

void NavBar::update_icons_cb(lv_observer_t* observer, lv_subject_t* subject) {
    LV_UNUSED(subject);
    auto* nav_bar = static_cast<NavBar*>(lv_observer_get_user_data(observer));
    if (nav_bar) {
        nav_bar->update_icon_buttons();
    }
}

}  // namespace view::widgets
