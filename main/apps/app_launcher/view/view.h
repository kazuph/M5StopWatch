/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <apps/common/arc_top_clock/arc_top_clock.h>
#include <apps/common/key_manager/key_manager.h>
#include <mooncake.h>
#include <smooth_ui_toolkit.hpp>
#include <uitk/short_namespace.hpp>
#include <smooth_lvgl.hpp>
#include <functional>
#include <vector>
#include <memory>

namespace view {

/**
 * @brief
 *
 */
class LauncherView {
public:
    ~LauncherView();

    enum State_t {
        STATE_STARTUP,
        STATE_NORMAL,
    };

    std::function<void(int appID)> onAppClicked;

    void init(std::vector<mooncake::AppProps_t> appPorps);
    void update();

    /**
     * @brief Park the launcher while another app owns the screen.
     *
     * The whole icon tree stays allocated so coming back costs nothing, but the
     * root panel is hidden and stripped of input. LVGL skips a hidden subtree
     * when it hit tests a touch, so a tap meant for the running app can never be
     * latched by an icon underneath and fired on the way back.
     */
    void hide();

    /** @brief Bring the parked launcher back, with its input state reset. */
    void show();

    bool isInitialized() const
    {
        return _panel != nullptr;
    }

private:
    std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
    std::vector<std::unique_ptr<uitk::lvgl_cpp::Container>> _icon_panels;
    std::vector<std::unique_ptr<uitk::lvgl_cpp::Image>> _icon_images;
    std::vector<std::unique_ptr<uitk::lvgl_cpp::Container>> _lr_indicator_panels;
    std::vector<std::unique_ptr<uitk::lvgl_cpp::Image>> _lr_indicators_images;
    std::unique_ptr<view::ArcTopClock> _clock;
    std::unique_ptr<input::KeyManager> _key_manager;

    int _clicked_app_id = -1;
    State_t _state      = STATE_STARTUP;

    void scroll_to_nearby_icon(int direction);
    void handle_state_startup();
    void handle_state_normal();
    void handle_scroll_in_loop();
};

/**
 * @brief
 *
 */
class GuidePage {
public:
    GuidePage();

private:
    std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
    std::unique_ptr<uitk::lvgl_cpp::Image> _img;
};

}  // namespace view
