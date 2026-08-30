/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <smooth_lvgl.hpp>
#include <uitk/short_namespace.hpp>

namespace view {

/**
 * @brief Pomodoro timer face for the 466x466 round display.
 *
 * The layout is stacked vertically so nothing ever overlaps: the countdown ring
 * hugs the bezel, the phase name sits above the clock, and the two controls
 * share the bottom row side by side.
 */
class PomodoroView {
public:
    void init(lv_obj_t* parent);

    /** @brief true while the focus phase is active, false during the break. */
    void setPhase(bool isFocus);
    void setRemainingSeconds(uint32_t remainingSeconds);
    void setRunning(bool running);

    /** @brief Raised by the start / pause button. */
    std::function<void()> onToggle;
    /** @brief Raised by the reset button. */
    std::function<void()> onReset;

private:
    static void drawEventCb(lv_event_t* e);

    void applyPhase();
    void applyRunState();
    void applyTimeLabel();
    void invalidateRing();
    float remainingRatio() const;

    std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
    std::unique_ptr<uitk::lvgl_cpp::Container> _ring;
    std::unique_ptr<uitk::lvgl_cpp::Label> _phase_label;
    std::unique_ptr<uitk::lvgl_cpp::Label> _time_label;
    std::unique_ptr<uitk::lvgl_cpp::Button> _btn_reset;
    std::unique_ptr<uitk::lvgl_cpp::Button> _btn_toggle;

    uint32_t _remaining_seconds = 0;
    bool _is_focus              = true;
    bool _is_running            = false;
};

}  // namespace view
