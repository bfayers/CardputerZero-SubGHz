/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "app.h"

#include "application_config.h"
#include "asset_manager.h"
#include "base_viewmodel.h"
#include "help_popup.h"
#include "linux_input.h"
#include "logger.h"
#include "screen_manager.h"
#include "screenshot_service.h"
#include "theme.h"
#include "toast.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>

#if USE_DESKTOP
#include "desktop_simulator_frame.h"
#endif

#if !USE_DESKTOP
#if APP_USE_DRM
#include "src/drivers/display/drm/lv_linux_drm.h"
#else
#include "src/drivers/display/fb/lv_linux_fbdev.h"
#endif
#endif

#ifndef APP_FRAMEBUFFER_DEVICE
#define APP_FRAMEBUFFER_DEVICE "/dev/fb0"
#endif

#ifndef APP_DRM_DEVICE
#define APP_DRM_DEVICE "/dev/dri/card0"
#endif

#ifndef APP_DRM_CONNECTOR_ID
#define APP_DRM_CONNECTOR_ID -1
#endif

#ifndef APP_CONFIG_FILE
#define APP_CONFIG_FILE "template-app.conf"
#endif

namespace app {
namespace {

void quit_requested_observer(lv_observer_t* observer, lv_subject_t* subject) {
    auto* running = static_cast<bool*>(lv_observer_get_user_data(observer));
    if (running && lv_subject_get_int(subject)) {
        *running = false;
    }
}

struct DarkModePersistence {
    std::string config_path;
    bool last_dark_mode;
};

std::string writable_config_path() {
#if USE_DESKTOP
    return APP_CONFIG_FILE;
#else
    const auto home = model::resolve_user_home_dir();
    if (!home.empty()) {
        return (home / "subg" / "config" / "cc1101-subghz.conf").string();
    }
    return APP_CONFIG_FILE;
#endif
}

void persist_dark_mode_observer(lv_observer_t* observer, lv_subject_t* subject) {
    auto* persistence = static_cast<DarkModePersistence*>(lv_observer_get_user_data(observer));
    if (!persistence) {
        return;
    }
    const bool dark_mode = lv_subject_get_int(subject) != 0;
    if (dark_mode == persistence->last_dark_mode) {
        return;
    }
    persistence->last_dark_mode = dark_mode;

    ApplicationConfig config;
    config.dark_mode = dark_mode;
    std::string error;
    if (save_application_config(persistence->config_path, config, error)) {
#if !USE_DESKTOP
        model::chown_to_user_home_owner(std::filesystem::path(persistence->config_path).parent_path(),
                                        model::resolve_user_home_dir());
#endif
        LOG_INFO("saved config: {} (dark_mode={})", persistence->config_path, config.dark_mode ? "yes" : "no");
    } else {
        LOG_WARN("failed to save config {}: {}", persistence->config_path, error);
    }
}

class ShortcutController {
public:
    ShortcutController(viewmodel::BaseViewModel& view_model, AssetManager& assets)
        : view_model_(view_model),
          toast_(std::make_unique<view::widgets::Toast>(lv_layer_top(), view_model, assets)),
          help_popup_(std::make_unique<view::widgets::HelpPopup>(lv_layer_top(), view_model, assets)) {
        toast_->build();
        help_popup_->build();
        toast_observer_ = lv_subject_add_observer(view_model_.toast_message_subject(), toast_observer_cb, this);
        platform::set_global_key_listener(global_key_listener, this);
        platform::set_key_release_listener(key_release_listener, this);
    }

    ~ShortcutController() {
        if (toast_observer_) {
            lv_observer_remove(toast_observer_);
            toast_observer_ = nullptr;
        }
        platform::clear_key_release_listener(key_release_listener, this);
        platform::clear_global_key_listener(global_key_listener, this);
    }

private:
    static void toast_observer_cb(lv_observer_t* observer, lv_subject_t* subject) {
        auto* self = static_cast<ShortcutController*>(lv_observer_get_user_data(observer));
        const char* msg = lv_subject_get_string(subject);
        if (self && msg && msg[0] != '\0') {
            self->toast_->show(msg, view::widgets::ToastTone::Success, 2200);
        }
    }

    bool handle_key(uint32_t key, bool long_pressed) {
        if (key == platform::kKeyPrintScreen) {
            if (!long_pressed) {
                const auto result = platform::screenshot::capture_active_screen();
                if (result.success) {
                    LOG_INFO("screenshot saved: {}", result.path);
                    toast_->show_highlighted(
                        "Saved to ", result.path, "", view::widgets::ToastTone::Success, 4200, true);
                } else {
                    LOG_WARN("screenshot failed: {}", result.error);
                    toast_->show("Screenshot failed: " + result.error, view::widgets::ToastTone::Error, 3200);
                }
            }
            return true;
        }

        if (key == platform::kKeyHelp) {
            if (!long_pressed) {
                toast_->hide();
                if (help_popup_->visible()) {
                    help_popup_->hide();
                } else {
                    help_popup_->show(view_model_.current_page());
                }
            }
            return true;
        }

        if (help_popup_->visible()) {
            if (key == LV_KEY_ESC && !long_pressed) {
                help_popup_->hide();
            }
            return true;
        }

        const bool is_escape = key == LV_KEY_ESC;
        const bool is_home_exit_key =
            key == '4' && ((!long_pressed && view_model_.current_page() == model::AppPage::Home) ||
                           (long_pressed && exit_key_pressed_ == key));
        if (is_escape || is_home_exit_key) {
            if (long_pressed) {
                if (exit_key_pressed_ == key && exit_armed_) {
                    toast_->hide();
                    view_model_.request_quit();
                }
            } else {
                exit_key_pressed_ = key;
                exit_armed_ = view_model_.current_page() == model::AppPage::Home;
                if (exit_armed_) {
                    toast_->show_persistent_highlighted(
                        "Hold ", "ESC", " to exit", view::widgets::ToastTone::Warning);
                }
            }
            return true;
        }

        if (!long_pressed) {
            return view_model_.handle_screen_key(key);
        }
        return false;
    }

    void handle_release(uint32_t key) {
        if (key != exit_key_pressed_) {
            return;
        }

        if (exit_armed_) {
            toast_->hide();
        } else if (key == LV_KEY_ESC) {
            if (view_model_.current_page() == model::AppPage::Saved &&
                view_model_.model().saved_detail_open()) {
                view_model_.handle_screen_key(LV_KEY_BACKSPACE);
            } else if (view_model_.current_page() != model::AppPage::Home) {
                view_model_.show_home_page();
            }
        }
        exit_key_pressed_ = 0;
        exit_armed_ = false;
    }

    static bool global_key_listener(uint32_t key, const char*, bool long_pressed, void* user_data) {
        auto* controller = static_cast<ShortcutController*>(user_data);
        return controller && controller->handle_key(key, long_pressed);
    }

    static void key_release_listener(uint32_t key, const char*, void* user_data) {
        auto* controller = static_cast<ShortcutController*>(user_data);
        if (controller) {
            controller->handle_release(key);
        }
    }

    viewmodel::BaseViewModel& view_model_;
    std::unique_ptr<view::widgets::Toast> toast_;
    std::unique_ptr<view::widgets::HelpPopup> help_popup_;
    lv_observer_t* toast_observer_{nullptr};
    uint32_t exit_key_pressed_{0};
    bool exit_armed_{false};
};

#if !USE_DESKTOP
lv_display_t* init_device_display() {
#if APP_USE_DRM
    auto* display = lv_linux_drm_create();
    if (!display) {
        return nullptr;
    }

    if (lv_linux_drm_set_file(display, APP_DRM_DEVICE, APP_DRM_CONNECTOR_ID) != LV_RESULT_OK) {
        lv_display_delete(display);
        return nullptr;
    }

    platform::init_key_input(display);
    return display;
#else
    auto* display = lv_linux_fbdev_create();
    if (!display) {
        return nullptr;
    }

    if (lv_linux_fbdev_set_file(display, APP_FRAMEBUFFER_DEVICE) != LV_RESULT_OK) {
        lv_display_delete(display);
        return nullptr;
    }

    platform::init_key_input(display);
    return display;
#endif
}
#endif

}  // namespace

int Application::run() {
    logger::Logger::init();
    logger::Logger::set_tag("cc1101_subghz");

    lv_init();

    AssetManager assets;
    for (const auto& root : assets.roots()) {
        LOG_INFO("asset root: {}", root.string());
    }

    viewmodel::BaseViewModel view_model;
    view_model.init_subghz(assets.resolve("subghz"));

    const std::string user_config_path = writable_config_path();
    std::string loaded_config_path = APP_CONFIG_FILE;
    if (user_config_path != APP_CONFIG_FILE) {
        std::error_code filesystem_error;
        if (std::filesystem::is_regular_file(user_config_path, filesystem_error)) {
            loaded_config_path = user_config_path;
        }
    }

    ApplicationConfig config;
    std::string config_error;
    if (load_application_config(loaded_config_path, config, config_error)) {
        view_model.set_dark_mode(config.dark_mode);
        LOG_INFO("loaded config: {} (dark_mode={})", loaded_config_path, config.dark_mode ? "yes" : "no");
    } else {
        LOG_WARN("failed to load config {}: {}; using defaults", loaded_config_path, config_error);
    }

#if USE_DESKTOP
    DesktopSimulatorFrame simulator_frame(assets);
    auto* display = simulator_frame.display();
#else
    auto* display = init_device_display();
#endif
    if (!display) {
        LOG_ERROR("failed to initialize display");
        return 1;
    }

    auto* standard_font = assets.load_standard_font(14);
    view::apply_lvgl_theme(
        display, view_model.is_dark_mode(), standard_font ? standard_font : LV_FONT_DEFAULT);

#if USE_DESKTOP
    simulator_frame.bind_dark_mode(view_model.dark_mode_subject());
#endif

    ScreenManager screen_manager(view_model, assets);
    screen_manager.start();
    ShortcutController shortcuts(view_model, assets);

    DarkModePersistence dark_mode_persistence{user_config_path, view_model.is_dark_mode()};
    auto* dark_mode_observer =
        lv_subject_add_observer(view_model.dark_mode_subject(), persist_dark_mode_observer, &dark_mode_persistence);

    bool running = true;
    auto* quit_observer =
        lv_subject_add_observer(view_model.quit_requested_subject(), quit_requested_observer, &running);

    LOG_INFO("LVGL app started at {}x{}",
             lv_display_get_horizontal_resolution(display),
             lv_display_get_vertical_resolution(display));

    if (const char* auto_quit_ms_env = std::getenv("SUBGHZ_AUTO_QUIT_MS")) {
        const int auto_quit_ms = std::atoi(auto_quit_ms_env);
        if (auto_quit_ms > 0) {
            lv_timer_create(
                [](lv_timer_t* t) {
                    auto* vm = static_cast<viewmodel::BaseViewModel*>(lv_timer_get_user_data(t));
                    if (vm) vm->request_quit();
                },
                static_cast<uint32_t>(auto_quit_ms),
                &view_model);
        }
    }

    struct WalkthroughState {
        viewmodel::BaseViewModel* vm;
        std::filesystem::path dir;
        int step{0};
    } wt_state{&view_model, {}};

    if (const char* wt_dir = std::getenv("SUBGHZ_CAPTURE_WALKTHROUGH_DIR")) {
        if (wt_dir[0] != '\0') {
            wt_state.dir = wt_dir;
            lv_timer_create(
                [](lv_timer_t* t) {
                    auto* st = static_cast<WalkthroughState*>(lv_timer_get_user_data(t));
                    if (!st || !st->vm) return;
                    switch (st->step++) {
                        case 1:
                            platform::screenshot::capture_active_screen_to((st->dir / "01_home.png").string());
                            st->vm->navigate_to(model::AppPage::Read);
                            break;
                        case 4:
                            platform::screenshot::capture_active_screen_to((st->dir / "02_read.png").string());
                            st->vm->navigate_to(model::AppPage::ReadRaw);
                            st->vm->handle_screen_key('r');
                            break;
                        case 7:
                            platform::screenshot::capture_active_screen_to((st->dir / "03_read_raw.png").string());
                            st->vm->navigate_to(model::AppPage::Saved);
                            break;
                        case 9:
                            platform::screenshot::capture_active_screen_to((st->dir / "04_saved_browser.png").string());
                            st->vm->handle_screen_key(LV_KEY_ENTER);  // Enter Gates/
                            break;
                        case 11:
                            st->vm->handle_screen_key(LV_KEY_DOWN);   // Select Princeton_433.sub
                            st->vm->handle_screen_key(LV_KEY_ENTER);  // Open inspector
                            break;
                        case 13:
                            platform::screenshot::capture_active_screen_to((st->dir / "05_saved_inspector.png").string());
                            st->vm->navigate_to(model::AppPage::Analyzer);
                            break;
                        case 16:
                            platform::screenshot::capture_active_screen_to((st->dir / "06_analyzer.png").string());
                            st->vm->navigate_to(model::AppPage::AddManual);
                            break;
                        case 18:
                            platform::screenshot::capture_active_screen_to((st->dir / "07_add_manual.png").string());
                            st->vm->navigate_to(model::AppPage::RadioInfo);
                            break;
                        case 20:
                            platform::screenshot::capture_active_screen_to((st->dir / "08_radio_info.png").string());
                            st->vm->set_dark_mode(true);
                            st->vm->show_home_page();
                            break;
                        case 22:
                            platform::screenshot::capture_active_screen_to((st->dir / "09_home_dark.png").string());
                            st->vm->set_dark_mode(false);
                            st->vm->request_quit();
                            break;
                        default:
                            break;
                    }
                },
                180,
                &wt_state);
        }
    }

    while (running
#if USE_DESKTOP
           && simulator_frame.process_events()
#endif
    ) {
        lv_timer_handler();
        lv_delay_ms(5);
    }

    if (quit_observer) {
        lv_observer_remove(quit_observer);
    }
    if (dark_mode_observer) {
        lv_observer_remove(dark_mode_observer);
    }

    return 0;
}

}  // namespace app
