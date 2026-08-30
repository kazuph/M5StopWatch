/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "view.h"
#include "../model/pomodoro.h"
#include <assets/assets.h>
#include <smooth_ui_toolkit.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace view;
using namespace uitk;
using namespace uitk::lvgl_cpp;

namespace {

constexpr int _panel_size = 466;

// Same palette as the stopwatch app: blue for the neutral action, green to start, pink to stop.
constexpr uint32_t _color_bg         = 0x000000;
constexpr uint32_t _color_focus      = 0xFF9EAB;
constexpr uint32_t _color_break      = 0x9CF1B6;
constexpr uint32_t _color_time       = 0xD8F2FF;
constexpr uint32_t _color_ring_track = 0x41484B;
constexpr uint32_t _color_btn_reset  = 0xB3CDFF;
constexpr uint32_t _color_btn_start  = 0x9CF1B6;
constexpr uint32_t _color_btn_pause  = 0xFF9EAB;
constexpr uint32_t _label_blend_ref  = 0x858585;

constexpr int _ring_radius = 224;
constexpr int _ring_width  = 12;

constexpr int _phase_label_y = -142;
constexpr int _time_label_y  = -14;

constexpr int _button_width  = 120;
constexpr int _button_height = 72;
constexpr int _button_y      = 124;
constexpr int _button_x      = 69;

// 0 deg is 3 o'clock in LVGL, so the countdown has to start a quarter turn earlier.
constexpr int32_t _ring_start_angle = 270;

void apply_button_style(Button& button, int x_offset)
{
    button.setSize(_button_width, _button_height);
    button.setRadius(_button_height / 2 - 2);
    button.setBorderWidth(0);
    button.setOutlineWidth(0);
    button.setShadowWidth(0);
    button.setAlign(LV_ALIGN_CENTER);
    button.align(LV_ALIGN_CENTER, x_offset, _button_y);
    button.label().setTextFont(&lv_font_maple_mono_medium_24);
}

void apply_button_colors(Button& button, uint32_t bgColor)
{
    button.setBgColor(lv_color_hex(bgColor));
    button.label().setTextColor(lv_color_hex(color::blend_in_difference(bgColor, _label_blend_ref).toHex()));
}

void draw_ring_span(lv_layer_t* layer, lv_draw_arc_dsc_t& dsc, int32_t start, int32_t span)
{
    if (span <= 0) {
        return;
    }

    if (span >= 360) {
        dsc.start_angle = 0;
        dsc.end_angle   = 360;
        lv_draw_arc(layer, &dsc);
        return;
    }

    start %= 360;
    if (start < 0) {
        start += 360;
    }

    // lv_draw_arc takes a plain start/end pair, so a span that wraps past 3 o'clock
    // is split into two draws instead of relying on angle normalisation.
    if (start + span <= 360) {
        dsc.start_angle = start;
        dsc.end_angle   = start + span;
        lv_draw_arc(layer, &dsc);
        return;
    }

    dsc.start_angle = start;
    dsc.end_angle   = 360;
    lv_draw_arc(layer, &dsc);

    dsc.start_angle = 0;
    dsc.end_angle   = start + span - 360;
    lv_draw_arc(layer, &dsc);
}

}  // namespace

void PomodoroView::init(lv_obj_t* parent)
{
    lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    _panel = std::make_unique<Container>(parent);
    _panel->align(LV_ALIGN_CENTER, 0, 0);
    _panel->setSize(_panel_size, _panel_size);
    _panel->setRadius(0);
    _panel->setBorderWidth(0);
    _panel->setOutlineWidth(0);
    _panel->setShadowWidth(0);
    _panel->setPaddingAll(0);
    _panel->setBgColor(lv_color_hex(_color_bg));
    _panel->setBgOpa(LV_OPA_COVER);
    _panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    _ring = std::make_unique<Container>(_panel->get());
    _ring->align(LV_ALIGN_CENTER, 0, 0);
    _ring->setSize(_panel_size, _panel_size);
    _ring->setRadius(0);
    _ring->setBorderWidth(0);
    _ring->setOutlineWidth(0);
    _ring->setShadowWidth(0);
    _ring->setPaddingAll(0);
    _ring->setBgOpa(LV_OPA_TRANSP);
    _ring->removeFlag(LV_OBJ_FLAG_CLICKABLE);
    _ring->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(_ring->get(), PomodoroView::drawEventCb, LV_EVENT_ALL, this);

    _phase_label = std::make_unique<Label>(_panel->get());
    _phase_label->align(LV_ALIGN_CENTER, 0, _phase_label_y);
    _phase_label->setTextFont(&lv_font_montserrat_28);
    _phase_label->setTextColor(lv_color_hex(_color_focus));
    _phase_label->setText("FOCUS");

    _time_label = std::make_unique<Label>(_panel->get());
    _time_label->align(LV_ALIGN_CENTER, 0, _time_label_y);
    _time_label->setTextFont(&CommissionerMedium108);
    _time_label->setTextColor(lv_color_hex(_color_time));
    _time_label->setText("00:00");

    _btn_reset = std::make_unique<Button>(_panel->get());
    apply_button_style(*_btn_reset, -_button_x);
    _btn_reset->label().setText("RESET");
    apply_button_colors(*_btn_reset, _color_btn_reset);
    _btn_reset->onClick().connect([this]() {
        if (onReset) {
            onReset();
        }
    });

    _btn_toggle = std::make_unique<Button>(_panel->get());
    apply_button_style(*_btn_toggle, _button_x);
    _btn_toggle->onClick().connect([this]() {
        if (onToggle) {
            onToggle();
        }
    });

    applyPhase();
    applyRunState();
    applyTimeLabel();
}

void PomodoroView::setPhase(bool isFocus)
{
    if (_is_focus == isFocus) {
        return;
    }

    _is_focus = isFocus;
    applyPhase();
    invalidateRing();
}

void PomodoroView::setRemainingSeconds(uint32_t remainingSeconds)
{
    if (_remaining_seconds == remainingSeconds) {
        return;
    }

    _remaining_seconds = remainingSeconds;
    applyTimeLabel();
    invalidateRing();
}

void PomodoroView::setRunning(bool running)
{
    if (_is_running == running) {
        return;
    }

    _is_running = running;
    applyRunState();
}

void PomodoroView::applyPhase()
{
    if (_phase_label) {
        _phase_label->setText(_is_focus ? "FOCUS" : "BREAK");
        _phase_label->setTextColor(lv_color_hex(_is_focus ? _color_focus : _color_break));
        _phase_label->align(LV_ALIGN_CENTER, 0, _phase_label_y);
    }
}

void PomodoroView::applyRunState()
{
    if (!_btn_toggle) {
        return;
    }

    _btn_toggle->label().setText(_is_running ? "PAUSE" : "START");
    apply_button_colors(*_btn_toggle, _is_running ? _color_btn_pause : _color_btn_start);
}

void PomodoroView::applyTimeLabel()
{
    if (!_time_label) {
        return;
    }

    char buffer[16] = {};
    std::snprintf(buffer, sizeof(buffer), "%02u:%02u", static_cast<unsigned>(_remaining_seconds / 60),
                  static_cast<unsigned>(_remaining_seconds % 60));
    _time_label->setText(buffer);
    _time_label->align(LV_ALIGN_CENTER, 0, _time_label_y);
}

void PomodoroView::invalidateRing()
{
    if (_ring) {
        lv_obj_invalidate(_ring->get());
    }
}

float PomodoroView::remainingRatio() const
{
    const uint32_t total = _is_focus ? model::Pomodoro::focusDurationSeconds : model::Pomodoro::breakDurationSeconds;
    if (total == 0) {
        return 0.0f;
    }

    return std::clamp(static_cast<float>(_remaining_seconds) / static_cast<float>(total), 0.0f, 1.0f);
}

void PomodoroView::drawEventCb(lv_event_t* e)
{
    auto* self = static_cast<PomodoroView*>(lv_event_get_user_data(e));
    if (self == nullptr) {
        return;
    }

    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_COVER_CHECK) {
        lv_event_set_cover_res(e, LV_COVER_RES_NOT_COVER);
        return;
    }

    if (code != LV_EVENT_DRAW_MAIN_BEGIN) {
        return;
    }

    lv_obj_t* obj     = lv_event_get_target_obj(e);
    lv_layer_t* layer = lv_event_get_layer(e);
    if (lv_obj_get_style_opa_recursive(obj, LV_PART_MAIN) <= LV_OPA_MIN) {
        return;
    }

    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);
    const lv_point_t center = {
        static_cast<int32_t>(coords.x1 + lv_obj_get_width(obj) / 2),
        static_cast<int32_t>(coords.y1 + lv_obj_get_height(obj) / 2),
    };

    lv_draw_arc_dsc_t dsc;
    lv_draw_arc_dsc_init(&dsc);
    dsc.center = center;
    dsc.radius = _ring_radius;
    dsc.width  = _ring_width;

    dsc.color       = lv_color_hex(_color_ring_track);
    dsc.opa         = LV_OPA_60;
    dsc.rounded     = 0;
    dsc.start_angle = 0;
    dsc.end_angle   = 360;
    lv_draw_arc(layer, &dsc);

    // The remaining arc keeps its tail pinned at 12 o'clock and is eaten from its head,
    // so the moving edge travels 12 -> 3 -> 6 -> 9 o'clock as the phase runs down.
    // Anchoring the start instead would make that edge crawl backwards to 12 o'clock.
    const int32_t remaining_span = static_cast<int32_t>(std::lround(self->remainingRatio() * 360.0f));
    const int32_t elapsed_span   = 360 - remaining_span;
    dsc.color                    = lv_color_hex(self->_is_focus ? _color_focus : _color_break);
    dsc.opa                      = LV_OPA_COVER;
    dsc.rounded                  = 1;
    draw_ring_span(layer, dsc, _ring_start_angle + elapsed_span, remaining_span);
}
