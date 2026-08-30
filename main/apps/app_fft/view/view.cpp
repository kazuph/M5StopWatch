/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "view.h"
#include <assets/assets.h>
#include <algorithm>
#include <cstdio>
#include <cmath>

using namespace view;
using namespace uitk::lvgl_cpp;

namespace {

constexpr int _panel_size       = 466;
constexpr int _disc_size        = 137;
constexpr int _spectrum_size    = 250;
constexpr int _ext_draw_size    = 180;
constexpr int _bar_rest_radius  = 82;
constexpr int _bar_max_radius   = 156;
constexpr int _bar_color1_stop  = 80;
constexpr int _bar_color2_stop  = 100;
constexpr int _bar_per_band_cnt = static_cast<int>(FftView::band_count / FftView::reduced_band_count);

constexpr uint32_t _bg_color    = 0x1F1528;
constexpr uint32_t _disc_color  = 0xF8C6E7;
constexpr uint32_t _value_color = 0xAE5D92;
constexpr uint32_t _unit_color  = 0xD586BA;
constexpr uint32_t _bar_color1  = 0xC19BFF;
constexpr uint32_t _bar_color2  = 0xE97AE0;
constexpr uint32_t _bar_color3  = 0xFF66BC;
constexpr int _deg_step         = 180 / static_cast<int>(FftView::band_count);

constexpr float _view_attack_alpha  = 0.60f;
constexpr float _view_release_alpha = 0.36f;
constexpr float _disc_follow_alpha  = 0.40f;
constexpr float _peak_fall_rate     = 0.012f;

constexpr float _pi = 3.14159265358979323846f;

// Shared column geometry for the two grid style modes.
constexpr int _column_pitch = 17;
constexpr int _column_x0    = -161;

constexpr int _bars_baseline_y = 150;
constexpr int _bars_width      = 12;
constexpr int _bars_min_height = 8;
constexpr int _bars_span       = 292;
constexpr int _bars_cap_height = 4;

constexpr int _ring_inner_radius = 94;
constexpr int _ring_span         = 126;
constexpr int _ring_line_width   = 9;

constexpr int _wave_point_count = 61;
constexpr float _wave_half_span = 196.0f;
constexpr float _wave_amplitude = 120.0f;
constexpr float _wave_step      = 0.55f;

constexpr int _tunnel_inner_radius = 84;
constexpr int _tunnel_pitch        = 7;

constexpr int _matrix_rows     = 12;
constexpr int _matrix_bottom_y = 140;
constexpr int _matrix_top_y    = -140;
constexpr float _matrix_dot_r  = 5.5f;

constexpr int _mode_label_y = 196;

constexpr std::array<int, FftView::reduced_band_count> _band_widths = {20, 8, 4, 2};
constexpr std::array<int, 10> _rnd_array                            = {994, 285, 553, 11, 792, 707, 966, 641, 852, 827};

constexpr std::array<const char*, static_cast<std::size_t>(FftView::Mode::_count)> _mode_names = {
    "BLOOM", "BARS", "RING", "WAVE", "TUNNEL", "MATRIX",
};

float clamp_band(float value)
{
    return std::clamp(value, 0.0f, 1.0f);
}

int32_t get_cos(int32_t deg, int32_t amplitude)
{
    int32_t result = lv_trigo_cos(deg) * amplitude;
    result += LV_TRIGO_SIN_MAX / 2;
    return result >> LV_TRIGO_SHIFT;
}

int32_t get_sin(int32_t deg, int32_t amplitude)
{
    int32_t result = lv_trigo_sin(deg) * amplitude;
    return (result + LV_TRIGO_SIN_MAX / 2) >> LV_TRIGO_SHIFT;
}

lv_color_t mix_bar_color(int radius)
{
    if (radius < _bar_color1_stop) {
        return lv_color_hex(_bar_color1);
    }

    if (radius > _bar_max_radius) {
        return lv_color_hex(_bar_color3);
    }

    if (radius > _bar_color2_stop) {
        return lv_color_mix(
            lv_color_hex(_bar_color3), lv_color_hex(_bar_color2),
            static_cast<uint8_t>(((radius - _bar_color2_stop) * 255) / (_bar_max_radius - _bar_color2_stop)));
    }

    return lv_color_mix(
        lv_color_hex(_bar_color2), lv_color_hex(_bar_color1),
        static_cast<uint8_t>(((radius - _bar_color1_stop) * 255) / (_bar_color2_stop - _bar_color1_stop)));
}

/** @brief Same three colour stops as the petals, addressed by a 0..1 level instead of a radius. */
lv_color_t mix_level_color(float level)
{
    const float t = clamp_band(level);
    if (t < 0.5f) {
        return lv_color_mix(lv_color_hex(_bar_color2), lv_color_hex(_bar_color1),
                            static_cast<uint8_t>(std::lround(t * 2.0f * 255.0f)));
    }

    return lv_color_mix(lv_color_hex(_bar_color3), lv_color_hex(_bar_color2),
                        static_cast<uint8_t>(std::lround((t - 0.5f) * 2.0f * 255.0f)));
}

void set_line_points(lv_draw_line_dsc_t& dsc, float x1, float y1, float x2, float y2)
{
    dsc.p1.x = static_cast<lv_value_precise_t>(x1);
    dsc.p1.y = static_cast<lv_value_precise_t>(y1);
    dsc.p2.x = static_cast<lv_value_precise_t>(x2);
    dsc.p2.y = static_cast<lv_value_precise_t>(y2);
}

void fill_rect(lv_layer_t* layer, int32_t x1, int32_t y1, int32_t x2, int32_t y2, lv_color_t color, lv_opa_t opa,
               int32_t radius)
{
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.radius     = radius;
    dsc.bg_color   = color;
    dsc.bg_opa     = opa;
    lv_area_t area = {x1, y1, x2, y2};
    lv_draw_rect(layer, &dsc, &area);
}

void fill_dot(lv_layer_t* layer, float cx, float cy, float radius, lv_color_t color, lv_opa_t opa)
{
    fill_rect(layer, static_cast<int32_t>(std::lround(cx - radius)), static_cast<int32_t>(std::lround(cy - radius)),
              static_cast<int32_t>(std::lround(cx + radius)), static_cast<int32_t>(std::lround(cy + radius)), color,
              opa, LV_RADIUS_CIRCLE);
}

int column_x(std::size_t index)
{
    return _column_x0 + static_cast<int>(index) * _column_pitch;
}

}  // namespace

void FftView::init(lv_obj_t* parent)
{
    _panel = std::make_unique<Container>(parent);
    _panel->align(LV_ALIGN_CENTER, 0, 0);
    _panel->setSize(_panel_size, _panel_size);
    _panel->setRadius(0);
    _panel->setBorderWidth(0);
    _panel->setPaddingAll(0);
    _panel->setBgColor(lv_color_hex(_bg_color));
    _panel->setBgOpa(LV_OPA_COVER);
    _panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    _click_mask = std::make_unique<Container>(_panel->get());
    _click_mask->align(LV_ALIGN_CENTER, 0, 0);
    _click_mask->setSize(_panel_size, _panel_size);
    _click_mask->setBgOpa(LV_OPA_TRANSP);
    _click_mask->setBorderWidth(0);
    _click_mask->setPaddingAll(0);
    _click_mask->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _click_mask->onClick().connect([this]() { cycleMode(); });

    _spectrum_panel = std::make_unique<Container>(_panel->get());
    _spectrum_panel->align(LV_ALIGN_CENTER, 0, 0);
    _spectrum_panel->setSize(_spectrum_size, _spectrum_size);
    _spectrum_panel->setRadius(LV_RADIUS_CIRCLE);
    _spectrum_panel->setBorderWidth(0);
    _spectrum_panel->setPaddingAll(0);
    _spectrum_panel->setBgOpa(LV_OPA_TRANSP);
    _spectrum_panel->removeFlag(LV_OBJ_FLAG_CLICKABLE);
    _spectrum_panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(_spectrum_panel->get(), FftView::drawEventCb, LV_EVENT_ALL, this);
    lv_obj_refresh_ext_draw_size(_spectrum_panel->get());

    _center_disc = std::make_unique<Container>(_panel->get());
    _center_disc->align(LV_ALIGN_CENTER, 0, 0);
    _center_disc->setSize(_disc_size, _disc_size);
    _center_disc->setRadius(LV_RADIUS_CIRCLE);
    _center_disc->setBorderWidth(0);
    _center_disc->setPaddingAll(0);
    _center_disc->setBgColor(lv_color_hex(_disc_color));
    _center_disc->setBgOpa(LV_OPA_COVER);
    _center_disc->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    _peak_frequency_label = std::make_unique<Label>(_center_disc->get());
    _peak_frequency_label->align(LV_ALIGN_CENTER, 0, -4);
    _peak_frequency_label->setTextFont(&lv_font_montserrat_28);
    _peak_frequency_label->setTextColor(lv_color_hex(_value_color));
    _peak_frequency_label->setText("0");

    _peak_frequency_unit_label = std::make_unique<Label>(_center_disc->get());
    _peak_frequency_unit_label->align(LV_ALIGN_CENTER, 0, 24);
    _peak_frequency_unit_label->setTextFont(&lv_font_montserrat_18);
    _peak_frequency_unit_label->setTextColor(lv_color_hex(_unit_color));
    _peak_frequency_unit_label->setText("Hz");

    _mode_label = std::make_unique<Label>(_panel->get());
    _mode_label->align(LV_ALIGN_CENTER, 0, _mode_label_y);
    _mode_label->setTextFont(&lv_font_montserrat_20);
    _mode_label->setTextColor(lv_color_hex(_unit_color));
    _mode_label->setText(_mode_names[0]);

    _click_mask->moveForeground();
    applyMode();

    invalidateSpectrum();
}

void FftView::setSpectrum(const SpectrumBands& bands)
{
    _target_bands = bands;
}

void FftView::setPeakFrequencyHz(float frequencyHz)
{
    _peak_frequency_hz = std::max(0.0f, frequencyHz);
    applyPeakFrequencyLabel();
}

void FftView::cycleMode()
{
    _mode = static_cast<Mode>((static_cast<uint8_t>(_mode) + 1) % static_cast<uint8_t>(Mode::_count));
    applyMode();
}

void FftView::applyMode()
{
    const bool readout = showsCenterReadout();

    if (_center_disc) {
        _center_disc->setHidden(!readout);
    }
    if (_peak_frequency_label) {
        _peak_frequency_label->setHidden(!readout);
    }
    if (_peak_frequency_unit_label) {
        _peak_frequency_unit_label->setHidden(!readout);
    }
    if (_mode_label) {
        _mode_label->setText(_mode_names[static_cast<std::size_t>(_mode)]);
        _mode_label->align(LV_ALIGN_CENTER, 0, _mode_label_y);
    }

    invalidateSpectrum();
}

bool FftView::showsCenterReadout() const
{
    // Only the modes that leave the middle of the screen empty can carry the read-out.
    return _mode == Mode::Bloom || _mode == Mode::Ring || _mode == Mode::Tunnel;
}

void FftView::update()
{
    bool changed = false;

    for (std::size_t i = 0; i < band_count; ++i) {
        float target   = clamp_band(_target_bands[i]);
        float current  = _display_bands[i];
        float alpha    = target > current ? _view_attack_alpha : _view_release_alpha;
        float smoothed = current + (target - current) * alpha;

        if (std::fabs(smoothed - current) > 0.0015f) {
            changed = true;
        }

        _display_bands[i] = smoothed;
    }

    updateReducedBands();
    updatePeakBands();
    updateMotionState();
    updateCenterDisc();

    _wave_phase += 0.22f;
    if (_wave_phase > 2.0f * _pi) {
        _wave_phase -= 2.0f * _pi;
    }

    // Bloom keeps rotating and Wave keeps scrolling even while the input is quiet.
    if (changed || _mode == Mode::Bloom || _mode == Mode::Wave) {
        invalidateSpectrum();
    }
}

void FftView::updateCenterDisc()
{
    if (_center_disc == nullptr || !showsCenterReadout()) {
        return;
    }

    float pulse        = std::clamp(_reduced_bands[0] * 0.08f + _reduced_bands[1] * 0.025f, 0.0f, 0.1f);
    float target_scale = 1.0f + pulse;
    _disc_scale += (target_scale - _disc_scale) * _disc_follow_alpha;

    int size = static_cast<int>(std::lround(_disc_size * _disc_scale));
    _center_disc->setSize(size, size);
    _center_disc->align(LV_ALIGN_CENTER, 0, 0);
}

void FftView::applyPeakFrequencyLabel()
{
    if (_peak_frequency_label == nullptr) {
        return;
    }

    char buffer[16] = {};
    std::snprintf(buffer, sizeof(buffer), "%.0f", _peak_frequency_hz);
    _peak_frequency_label->setText(buffer);
}

void FftView::updateReducedBands()
{
    for (std::size_t band = 0; band < reduced_band_count; ++band) {
        std::size_t start  = band * _bar_per_band_cnt;
        std::size_t end    = start + _bar_per_band_cnt;
        float weighted_sum = 0.0f;
        float weight_sum   = 0.0f;

        for (std::size_t i = start; i < end; ++i) {
            float local    = static_cast<float>(i - start) / static_cast<float>(_bar_per_band_cnt - 1);
            float emphasis = 1.0f - local * 0.35f;
            weighted_sum += _display_bands[i] * emphasis;
            weight_sum += emphasis;
        }

        _reduced_bands[band] = weight_sum > 0.0f ? weighted_sum / weight_sum : 0.0f;
    }
}

void FftView::updatePeakBands()
{
    for (std::size_t i = 0; i < band_count; ++i) {
        if (_display_bands[i] >= _peak_bands[i]) {
            _peak_bands[i] = _display_bands[i];
        } else {
            _peak_bands[i] = std::max(0.0f, _peak_bands[i] - _peak_fall_rate);
        }
    }
}

void FftView::updateMotionState()
{
    _bar_blend += 0.035f;
    if (_bar_blend >= 1.0f) {
        _bar_blend -= 1.0f;
        _bar_ofs = (_bar_ofs + 1) % static_cast<int>(_rnd_array.size());
    }

    if (_bass_cooldown > 0) {
        --_bass_cooldown;
    }

    float bass = _reduced_bands[0];
    if (bass > 0.84f && _bass_cooldown == 0) {
        ++_bass_hit_count;
        _bass_cooldown = 14;
        if (_bass_hit_count >= 3) {
            _bass_hit_count = 0;
            _bar_ofs        = (_bar_ofs + 1) % static_cast<int>(_rnd_array.size());
        }
    }

    if (bass < 0.12f) {
        _bar_rot = (_bar_rot + _rotation_dir + static_cast<int>(band_count)) % static_cast<int>(band_count);
    }
}

void FftView::invalidateSpectrum()
{
    if (_spectrum_panel) {
        lv_obj_invalidate(_spectrum_panel->get());
    }
}

void FftView::drawBloom(lv_layer_t* layer, const lv_point_t& center) const
{
    lv_draw_triangle_dsc_t draw_dsc;
    lv_draw_triangle_dsc_init(&draw_dsc);
    draw_dsc.opa = LV_OPA_COVER;

    std::array<int, band_count> radii = {};
    radii.fill(_bar_rest_radius);

    for (std::size_t s = 0; s < reduced_band_count; ++s) {
        int band_w    = _band_widths[s];
        int amplitude = static_cast<int>((_bar_max_radius - _bar_rest_radius) * clamp_band(_reduced_bands[s]));

        for (int f = 0; f < band_w; ++f) {
            int32_t ampl_mod = get_cos(f * 360 / band_w + 180, 180) + 180;
            int t            = _bar_per_band_cnt * static_cast<int>(s) - band_w / 2 + f;
            if (t < 0) {
                t += static_cast<int>(band_count);
            }
            if (t >= static_cast<int>(band_count)) {
                t -= static_cast<int>(band_count);
            }

            radii[t] += (amplitude * ampl_mod) >> 9;
        }
    }

    // On top of the reduced envelope, every individual band pushes out its own petal
    // so all 20 values are visible instead of only the four group averages.
    for (std::size_t i = 0; i < band_count; ++i) {
        radii[i] += static_cast<int>((_bar_max_radius - _bar_rest_radius) * 0.30f * clamp_band(_display_bands[i]));
    }

    for (std::size_t i = 0; i < band_count; ++i) {
        int j = (static_cast<int>(i) + _bar_rot + _rnd_array[_bar_ofs % static_cast<int>(_rnd_array.size())]) %
                static_cast<int>(band_count);
        int k = (static_cast<int>(i) + _bar_rot + _rnd_array[(_bar_ofs + 1) % static_cast<int>(_rnd_array.size())]) %
                static_cast<int>(band_count);
        int radius     = static_cast<int>(radii[k] * _bar_blend + radii[j] * (1.0f - _bar_blend));
        draw_dsc.color = mix_bar_color(radius);

        int32_t deg_space   = 1;
        int32_t deg         = static_cast<int32_t>(i) * _deg_step + 90;
        int32_t outer_deg_a = deg + deg_space;
        int32_t outer_deg_b = deg + _deg_step - deg_space;

        int32_t x1_out = get_cos(outer_deg_a, radius);
        int32_t y1_out = get_sin(outer_deg_a, radius);
        int32_t x2_out = get_cos(outer_deg_b, radius);
        int32_t y2_out = get_sin(outer_deg_b, radius);
        int32_t x_in   = get_cos(outer_deg_b, 0);
        int32_t y_in   = get_sin(outer_deg_b, 0);

        draw_dsc.p[0].x = center.x + x1_out;
        draw_dsc.p[0].y = center.y + y1_out;
        draw_dsc.p[1].x = center.x + x2_out;
        draw_dsc.p[1].y = center.y + y2_out;
        draw_dsc.p[2].x = center.x + x_in;
        draw_dsc.p[2].y = center.y + y_in;
        lv_draw_triangle(layer, &draw_dsc);

        draw_dsc.p[0].x = center.x - x1_out;
        draw_dsc.p[1].x = center.x - x2_out;
        draw_dsc.p[2].x = center.x - x_in;
        lv_draw_triangle(layer, &draw_dsc);
    }
}

void FftView::drawBars(lv_layer_t* layer, const lv_point_t& center) const
{
    const int32_t baseline = center.y + _bars_baseline_y;

    for (std::size_t i = 0; i < band_count; ++i) {
        const float level  = clamp_band(_display_bands[i]);
        const int32_t x    = center.x + column_x(i);
        const int32_t x1   = x - _bars_width / 2;
        const int32_t x2   = x + _bars_width / 2;
        const int32_t high = static_cast<int32_t>(std::lround(_bars_min_height + level * _bars_span));

        fill_rect(layer, x1, baseline - high, x2, baseline, mix_level_color(level), LV_OPA_COVER, 3);

        // Classic analyzer peak cap, hanging above the bar as it falls back.
        const float peak = clamp_band(_peak_bands[i]);
        const int32_t cap =
            baseline - static_cast<int32_t>(std::lround(_bars_min_height + peak * _bars_span)) - _bars_cap_height;
        fill_rect(layer, x1, cap, x2, cap + _bars_cap_height, lv_color_hex(_disc_color), LV_OPA_90, 2);
    }

    fill_rect(layer, center.x - 172, baseline + 2, center.x + 172, baseline + 4, lv_color_hex(_bar_color1), LV_OPA_30,
              2);
}

void FftView::drawRing(lv_layer_t* layer, const lv_point_t& center) const
{
    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.width       = _ring_line_width;
    dsc.round_start = 1;
    dsc.round_end   = 1;
    dsc.opa         = LV_OPA_COVER;

    const float step = 360.0f / static_cast<float>(band_count);

    for (std::size_t i = 0; i < band_count; ++i) {
        const float level = clamp_band(_display_bands[i]);
        const float angle = (static_cast<float>(i) * step - 90.0f) * _pi / 180.0f;
        const float dx    = std::cos(angle);
        const float dy    = std::sin(angle);
        const float outer = _ring_inner_radius + level * _ring_span;

        dsc.color = mix_level_color(level);
        set_line_points(dsc, center.x + dx * _ring_inner_radius, center.y + dy * _ring_inner_radius,
                        center.x + dx * outer, center.y + dy * outer);
        lv_draw_line(layer, &dsc);

        const float peak = clamp_band(_peak_bands[i]);
        const float cap  = _ring_inner_radius + peak * _ring_span;
        fill_dot(layer, center.x + dx * cap, center.y + dy * cap, 4.0f, lv_color_hex(_disc_color), LV_OPA_80);
    }
}

void FftView::drawWave(lv_layer_t* layer, const lv_point_t& center) const
{
    lv_draw_line_dsc_t axis_dsc;
    lv_draw_line_dsc_init(&axis_dsc);
    axis_dsc.color = lv_color_hex(_bar_color1);
    axis_dsc.width = 2;
    axis_dsc.opa   = LV_OPA_20;
    set_line_points(axis_dsc, center.x - _wave_half_span, center.y, center.x + _wave_half_span, center.y);
    lv_draw_line(layer, &axis_dsc);

    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.width       = 5;
    dsc.round_start = 1;
    dsc.round_end   = 1;
    dsc.opa         = LV_OPA_COVER;

    const float dx = (_wave_half_span * 2.0f) / static_cast<float>(_wave_point_count - 1);

    float prev_x = 0.0f;
    float prev_y = 0.0f;

    for (int i = 0; i < _wave_point_count; ++i) {
        // Every sample along the trace is owned by one band, so the whole spectrum
        // shapes the envelope from left to right.
        const std::size_t band =
            std::min<std::size_t>(band_count - 1, static_cast<std::size_t>(i) * band_count / _wave_point_count);
        const float level = clamp_band(_display_bands[band]);
        const float x     = center.x - _wave_half_span + dx * static_cast<float>(i);
        const float y = center.y + level * _wave_amplitude * std::sin(static_cast<float>(i) * _wave_step + _wave_phase);

        if (i > 0) {
            dsc.color = mix_level_color(level);
            set_line_points(dsc, prev_x, prev_y, x, y);
            lv_draw_line(layer, &dsc);
        }

        prev_x = x;
        prev_y = y;
    }
}

void FftView::drawTunnel(lv_layer_t* layer, const lv_point_t& center) const
{
    lv_draw_arc_dsc_t dsc;
    lv_draw_arc_dsc_init(&dsc);
    dsc.center      = center;
    dsc.start_angle = 0;
    dsc.end_angle   = 360;
    dsc.rounded     = 0;

    // Bass sits innermost, treble on the outside: one ring per band, thickness is its level.
    for (std::size_t i = 0; i < band_count; ++i) {
        const float level = clamp_band(_display_bands[i]);
        dsc.radius        = static_cast<uint16_t>(_tunnel_inner_radius + static_cast<int>(i) * _tunnel_pitch);
        dsc.width         = 2 + static_cast<int32_t>(std::lround(level * 5.0f));
        dsc.color         = mix_level_color(level);
        dsc.opa           = static_cast<lv_opa_t>(std::lround(50.0f + level * 205.0f));
        lv_draw_arc(layer, &dsc);
    }
}

void FftView::drawMatrix(lv_layer_t* layer, const lv_point_t& center) const
{
    const float row_pitch = static_cast<float>(_matrix_bottom_y - _matrix_top_y) / static_cast<float>(_matrix_rows - 1);

    for (std::size_t i = 0; i < band_count; ++i) {
        const float level  = clamp_band(_display_bands[i]);
        const float peak   = clamp_band(_peak_bands[i]);
        const int lit_rows = static_cast<int>(std::lround(level * _matrix_rows));
        const int peak_row = std::max(0, static_cast<int>(std::lround(peak * _matrix_rows)) - 1);
        const float x      = static_cast<float>(center.x + column_x(i));

        for (int row = 0; row < _matrix_rows; ++row) {
            const float y     = static_cast<float>(center.y + _matrix_bottom_y) - row_pitch * static_cast<float>(row);
            const float shade = static_cast<float>(row) / static_cast<float>(_matrix_rows - 1);

            if (row < lit_rows) {
                fill_dot(layer, x, y, _matrix_dot_r, mix_level_color(shade), LV_OPA_COVER);
            } else if (row == peak_row) {
                fill_dot(layer, x, y, _matrix_dot_r, lv_color_hex(_disc_color), LV_OPA_80);
            } else {
                fill_dot(layer, x, y, _matrix_dot_r - 2.0f, lv_color_hex(_bar_color1), LV_OPA_20);
            }
        }
    }
}

void FftView::drawEventCb(lv_event_t* e)
{
    auto* self = static_cast<FftView*>(lv_event_get_user_data(e));
    if (self == nullptr) {
        return;
    }

    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_REFR_EXT_DRAW_SIZE) {
        lv_event_set_ext_draw_size(e, _ext_draw_size);
        return;
    }

    if (code == LV_EVENT_COVER_CHECK) {
        lv_event_set_cover_res(e, LV_COVER_RES_NOT_COVER);
        return;
    }

    if (code != LV_EVENT_DRAW_MAIN_BEGIN) {
        return;
    }

    lv_obj_t* obj     = lv_event_get_target_obj(e);
    lv_layer_t* layer = lv_event_get_layer(e);
    lv_opa_t opa      = lv_obj_get_style_opa_recursive(obj, LV_PART_MAIN);
    if (opa <= LV_OPA_MIN) {
        return;
    }

    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);

    lv_point_t center = {
        static_cast<int32_t>(coords.x1 + lv_obj_get_width(obj) / 2),
        static_cast<int32_t>(coords.y1 + lv_obj_get_height(obj) / 2),
    };

    switch (self->_mode) {
        case Mode::Bloom:
            self->drawBloom(layer, center);
            break;
        case Mode::Bars:
            self->drawBars(layer, center);
            break;
        case Mode::Ring:
            self->drawRing(layer, center);
            break;
        case Mode::Wave:
            self->drawWave(layer, center);
            break;
        case Mode::Tunnel:
            self->drawTunnel(layer, center);
            break;
        case Mode::Matrix:
            self->drawMatrix(layer, center);
            break;
        default:
            break;
    }
}
