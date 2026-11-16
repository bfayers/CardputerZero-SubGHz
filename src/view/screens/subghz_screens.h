/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"

#include <array>

namespace screen {

class HomeScreen : public BaseScreen {
public:
    HomeScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);

protected:
    void build_content(lv_obj_t* content) override;

private:
    void refresh_selection();
    static void update_cb(lv_observer_t* observer, lv_subject_t* subject);
    static void card_clicked_cb(lv_event_t* event);

    std::array<lv_obj_t*, 6> cards_{};
    std::array<lv_obj_t*, 6> titles_{};
    std::array<lv_obj_t*, 6> subs_{};
};

class ReadScreen : public BaseScreen {
public:
    ReadScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);

protected:
    void build_content(lv_obj_t* content) override;

private:
    void refresh_list();
    static void update_cb(lv_observer_t* observer, lv_subject_t* subject);

    lv_obj_t* hop_badge_{nullptr};
    lv_obj_t* empty_label_{nullptr};
    std::array<lv_obj_t*, 4> rows_{};
    std::array<lv_obj_t*, 4> row_labels_{};
};

class ReadRawScreen : public BaseScreen {
public:
    ReadRawScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);

protected:
    void build_content(lv_obj_t* content) override;

private:
    void refresh_waveform();
    static void update_cb(lv_observer_t* observer, lv_subject_t* subject);

    lv_obj_t* rec_badge_{nullptr};
    std::array<lv_obj_t*, 40> bars_{};
};

class SavedScreen : public BaseScreen {
public:
    SavedScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);

protected:
    void build_content(lv_obj_t* content) override;

private:
    void refresh_view();
    static void update_cb(lv_observer_t* observer, lv_subject_t* subject);

    lv_obj_t* list_container_{nullptr};
    lv_obj_t* path_label_{nullptr};
    std::array<lv_obj_t*, 4> rows_{};
    std::array<lv_obj_t*, 4> row_badges_{};
    std::array<lv_obj_t*, 4> row_names_{};

    lv_obj_t* detail_card_{nullptr};
    lv_obj_t* detail_title_{nullptr};
    lv_obj_t* detail_meta1_{nullptr};
    lv_obj_t* detail_meta2_{nullptr};
    lv_obj_t* detail_key_{nullptr};
};

class AnalyzerScreen : public BaseScreen {
public:
    AnalyzerScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);

protected:
    void build_content(lv_obj_t* content) override;
};

class AddManualScreen : public BaseScreen {
public:
    AddManualScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);

protected:
    void build_content(lv_obj_t* content) override;

private:
    void refresh_fields();
    static void update_cb(lv_observer_t* observer, lv_subject_t* subject);

    std::array<lv_obj_t*, 4> rows_{};
    std::array<lv_obj_t*, 4> keys_{};
    std::array<lv_obj_t*, 4> vals_{};
};

class RadioInfoScreen : public BaseScreen {
public:
    RadioInfoScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);

protected:
    void build_content(lv_obj_t* content) override;
};

}  // namespace screen
