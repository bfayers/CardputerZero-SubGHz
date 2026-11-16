/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_viewmodel.h"
#include "base_widget.h"
#include "icon_button.h"
#include "lvgl.h"

#include <array>
#include <memory>

namespace app {
class AssetManager;
}

namespace view::widgets {

class NavBar : public BaseWidgets {
public:
    NavBar(lv_obj_t* parent, viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    ~NavBar() override;

    void build() override;

private:
    void create_icon_buttons();
    void create_shortcut_hints();
    void update_icon_buttons();

    static void toggle_theme_cb(lv_event_t* event);
    static void open_read_cb(lv_event_t* event);
    static void open_saved_cb(lv_event_t* event);
    static void show_info_cb(lv_event_t* event);
    static void request_quit_cb(lv_event_t* event);
    static void update_icons_cb(lv_observer_t* observer, lv_subject_t* subject);

    viewmodel::BaseViewModel& view_model_;
    app::AssetManager& assets_;
    const lv_font_t* icon_font_{nullptr};
    lv_obj_t* shortcut_bar_{nullptr};
    std::array<lv_obj_t*, 3> shortcut_key_labels_{};
    std::array<lv_obj_t*, 3> shortcut_action_labels_{};
    std::array<std::unique_ptr<IconButton>, 5> icon_buttons_;
    lv_observer_t* page_observer_{nullptr};
    lv_observer_t* theme_observer_{nullptr};
    lv_observer_t* rev_observer_{nullptr};
};

}  // namespace view::widgets
