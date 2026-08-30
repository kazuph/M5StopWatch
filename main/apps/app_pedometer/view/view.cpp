/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "view.h"
#include <assets/assets.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace view;
using namespace uitk::lvgl_cpp;

namespace {

constexpr int _panel_size = 466;

// Palette borrowed from the stopwatch app so the whole product stays on one set of colors.
constexpr uint32_t _color_bg      = 0x000000;
constexpr uint32_t _color_blue    = 0xB3CDFF;
constexpr uint32_t _color_green   = 0x9CF1B6;
constexpr uint32_t _color_pink    = 0xFF9EAB;
constexpr uint32_t _color_text    = 0xD8F2FF;
constexpr uint32_t _color_muted   = 0x738086;
constexpr uint32_t _color_divider = 0x58646A;

constexpr std::array<uint32_t, 4> _particle_tints = {_color_blue, _color_green, _color_pink, _color_text};

// Round bowl the particles live in, in stage local coordinates (origin at the screen center).
constexpr float _bowl_radius = 214.0f;

constexpr float _rain_gravity     = 860.0f;
constexpr float _rain_restitution = 0.40f;
constexpr float _rain_friction    = 0.90f;
constexpr float _rain_life        = 6.5f;

constexpr float _rush_pull      = 1600.0f;
constexpr float _rush_drag      = 0.55f;
constexpr float _rush_life      = 4.0f;
constexpr float _rush_absorb_r  = 24.0f;
constexpr float _rush_spawn_r   = 244.0f;
constexpr float _rush_max_speed = 900.0f;

constexpr float _fade_seconds = 1.0f;

// Display side budget only. A burst larger than this is trimmed from the animation queue,
// never from the step totals, which are always taken verbatim from the HAL snapshot.
constexpr float _max_pending_spawns  = 240.0f;
constexpr int _max_spawns_per_update = 3;

constexpr int _graph_inner_radius = 104;
constexpr int _graph_outer_radius = 200;
constexpr int _graph_bar_width    = 14;
constexpr int _graph_label_radius = 218;

constexpr int _core_base_radius = 20;

constexpr float _pi = 3.14159265358979323846f;

constexpr std::array<const char*, 4> _hour_label_texts = {"0", "6", "12", "18"};

float clamp01(float value)
{
    return std::clamp(value, 0.0f, 1.0f);
}

lv_opa_t life_to_opa(float life)
{
    return static_cast<lv_opa_t>(std::lround(clamp01(life / _fade_seconds) * 255.0f));
}

float hour_angle_rad(int hour)
{
    // Hour 0 sits at 12 o'clock and the day runs clockwise.
    return (static_cast<float>(hour) * 15.0f - 90.0f) * _pi / 180.0f;
}

void set_line_points(lv_draw_line_dsc_t& dsc, float x1, float y1, float x2, float y2)
{
    dsc.p1.x = static_cast<lv_value_precise_t>(x1);
    dsc.p1.y = static_cast<lv_value_precise_t>(y1);
    dsc.p2.x = static_cast<lv_value_precise_t>(x2);
    dsc.p2.y = static_cast<lv_value_precise_t>(y2);
}

void draw_disc(lv_layer_t* layer, float cx, float cy, float radius, lv_color_t color, lv_opa_t opa)
{
    if (radius <= 0.0f || opa == LV_OPA_TRANSP) {
        return;
    }

    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.radius   = LV_RADIUS_CIRCLE;
    dsc.bg_color = color;
    dsc.bg_opa   = opa;

    lv_area_t area = {
        static_cast<int32_t>(std::lround(cx - radius)),
        static_cast<int32_t>(std::lround(cy - radius)),
        static_cast<int32_t>(std::lround(cx + radius)),
        static_cast<int32_t>(std::lround(cy + radius)),
    };
    lv_draw_rect(layer, &dsc, &area);
}

}  // namespace

void PedometerView::init(lv_obj_t* parent)
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

    _stage = std::make_unique<Container>(_panel->get());
    _stage->align(LV_ALIGN_CENTER, 0, 0);
    _stage->setSize(_panel_size, _panel_size);
    _stage->setRadius(0);
    _stage->setBorderWidth(0);
    _stage->setOutlineWidth(0);
    _stage->setShadowWidth(0);
    _stage->setPaddingAll(0);
    _stage->setBgOpa(LV_OPA_TRANSP);
    _stage->removeFlag(LV_OBJ_FLAG_CLICKABLE);
    _stage->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(_stage->get(), PedometerView::drawEventCb, LV_EVENT_ALL, this);

    _step_label = std::make_unique<Label>(_panel->get());
    _step_label->align(LV_ALIGN_CENTER, 0, -12);
    _step_label->setTextFont(&CommissionerMedium108);
    _step_label->setTextColor(lv_color_hex(_color_text));
    _step_label->setText("0");

    _step_unit_label = std::make_unique<Label>(_panel->get());
    _step_unit_label->align(LV_ALIGN_CENTER, 0, 76);
    _step_unit_label->setTextFont(&lv_font_montserrat_24);
    _step_unit_label->setTextColor(lv_color_hex(_color_muted));
    _step_unit_label->setText("STEPS");

    _graph_total_label = std::make_unique<Label>(_panel->get());
    _graph_total_label->align(LV_ALIGN_CENTER, 0, 0);
    _graph_total_label->setTextFont(&lv_font_montserrat_36);
    _graph_total_label->setTextColor(lv_color_hex(_color_text));
    _graph_total_label->setText("0");

    for (std::size_t i = 0; i < _hour_labels.size(); ++i) {
        const float angle = hour_angle_rad(static_cast<int>(i) * 6);
        auto& label       = _hour_labels[i];
        label             = std::make_unique<Label>(_panel->get());
        label->setTextFont(&lv_font_montserrat_16);
        label->setTextColor(lv_color_hex(_color_muted));
        label->setText(_hour_label_texts[i]);
        label->align(LV_ALIGN_CENTER, static_cast<int32_t>(std::lround(std::cos(angle) * _graph_label_radius)),
                     static_cast<int32_t>(std::lround(std::sin(angle) * _graph_label_radius)));
    }

    _mode_label = std::make_unique<Label>(_panel->get());
    _mode_label->align(LV_ALIGN_CENTER, 0, -186);
    _mode_label->setTextFont(&lv_font_montserrat_20);
    _mode_label->setTextColor(lv_color_hex(_color_divider));
    _mode_label->setText("RAIN");

    _click_mask = std::make_unique<Container>(_panel->get());
    _click_mask->align(LV_ALIGN_CENTER, 0, 0);
    _click_mask->setSize(_panel_size, _panel_size);
    _click_mask->setRadius(0);
    _click_mask->setBgOpa(LV_OPA_TRANSP);
    _click_mask->setBorderWidth(0);
    _click_mask->setOutlineWidth(0);
    _click_mask->setShadowWidth(0);
    _click_mask->setPaddingAll(0);
    _click_mask->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _click_mask->onClick().connect([this]() { cycleMode(); });
    _click_mask->moveForeground();

    _last_tick_ms = lv_tick_get();
    applyMode();
    updateLabels();
}

void PedometerView::setStepData(uint32_t totalSteps, const HourlySteps& hourlySteps)
{
    // The step data is always mirrored verbatim, independently of what the particle pool can show.
    if (_has_previous_data && totalSteps > _last_total) {
        _pending_spawns += static_cast<float>(totalSteps - _last_total);
        _pending_spawns = std::min(_pending_spawns, _max_pending_spawns);
    }

    for (std::size_t hour = 0; hour < hour_slot_count; ++hour) {
        if (_has_previous_data && hourlySteps[hour] != _hourly_steps[hour]) {
            _active_hour = static_cast<int>(hour);
        }
    }

    _last_total   = totalSteps;
    _total_steps  = totalSteps;
    _hourly_steps = hourlySteps;

    uint32_t peak = 1;
    for (uint32_t value : _hourly_steps) {
        peak = std::max(peak, value);
    }
    _peak_hourly = peak;

    _has_previous_data = true;
    updateLabels();
}

void PedometerView::update()
{
    const uint32_t now = lv_tick_get();
    float dt           = static_cast<float>(now - _last_tick_ms) / 1000.0f;
    _last_tick_ms      = now;
    dt                 = std::clamp(dt, 0.0f, 0.10f);

    if (_core_flash > 0.0f) {
        _core_flash = std::max(0.0f, _core_flash - dt * 3.2f);
    }

    switch (_mode) {
        case Mode::Rain:
            drainPendingSpawns();
            stepRainPhysics(dt);
            invalidateStage();
            break;

        case Mode::Rush:
            drainPendingSpawns();
            stepRushPhysics(dt);
            invalidateStage();
            break;

        case Mode::Count:
            // Nothing to animate beyond the number itself, so the queued spawns are
            // dropped here rather than piling up for the next visual mode.
            _pending_spawns = 0.0f;
            break;

        case Mode::Graph:
            _pending_spawns = 0.0f;
            if (_graph_reveal < 1.0f) {
                _graph_reveal = std::min(1.0f, _graph_reveal + dt * 2.4f);
                invalidateStage();
            }
            break;

        default:
            break;
    }
}

void PedometerView::cycleMode()
{
    _mode = static_cast<Mode>((static_cast<uint8_t>(_mode) + 1) % static_cast<uint8_t>(Mode::_count));
    applyMode();
    updateLabels();
}

void PedometerView::applyMode()
{
    resetParticles();
    _pending_spawns = 0.0f;
    _core_flash     = 0.0f;
    _graph_reveal   = _mode == Mode::Graph ? 0.0f : 1.0f;

    const bool count_mode = _mode == Mode::Count;
    const bool graph_mode = _mode == Mode::Graph;

    if (_step_label) {
        _step_label->setHidden(!count_mode);
    }
    if (_step_unit_label) {
        _step_unit_label->setHidden(!count_mode);
    }
    if (_graph_total_label) {
        _graph_total_label->setHidden(!graph_mode);
    }
    for (auto& label : _hour_labels) {
        if (label) {
            label->setHidden(!graph_mode);
        }
    }

    invalidateStage();
}

void PedometerView::resetParticles()
{
    for (auto& particle : _particles) {
        particle.active = false;
    }
}

PedometerView::Particle& PedometerView::acquireParticle()
{
    // Prefer a free slot, otherwise recycle the one closest to expiring so a fast
    // walker keeps getting a fresh visual for every step.
    Particle* oldest = &_particles[0];
    for (auto& particle : _particles) {
        if (!particle.active) {
            return particle;
        }
        if (particle.life < oldest->life) {
            oldest = &particle;
        }
    }
    return *oldest;
}

void PedometerView::drainPendingSpawns()
{
    int budget = _max_spawns_per_update;
    while (_pending_spawns >= 1.0f && budget > 0) {
        _pending_spawns -= 1.0f;
        --budget;

        if (_mode == Mode::Rain) {
            spawnRainParticle();
        } else {
            spawnRushParticle();
        }
    }
}

void PedometerView::spawnRainParticle()
{
    Particle& particle = acquireParticle();

    particle.x      = (nextRandom() * 2.0f - 1.0f) * 170.0f;
    particle.y      = -252.0f - nextRandom() * 60.0f;
    particle.vx     = (nextRandom() * 2.0f - 1.0f) * 25.0f;
    particle.vy     = 40.0f + nextRandom() * 90.0f;
    particle.radius = 8.0f + nextRandom() * 5.0f;
    particle.tint   = static_cast<uint8_t>(static_cast<int>(nextRandom() * 4.0f) & 0x03);
    particle.life   = _rain_life;
    particle.active = true;
}

void PedometerView::spawnRushParticle()
{
    Particle& particle = acquireParticle();

    _rush_side       = _rush_side == 0 ? 1 : 0;
    const float side = _rush_side == 0 ? -1.0f : 1.0f;
    const float y    = (nextRandom() * 2.0f - 1.0f) * 140.0f;

    particle.x      = side * _rush_spawn_r;
    particle.y      = y;
    particle.vx     = -side * (300.0f + nextRandom() * 120.0f);
    particle.vy     = -y * 0.9f + (nextRandom() * 2.0f - 1.0f) * 60.0f;
    particle.radius = 6.0f + nextRandom() * 4.0f;
    particle.tint   = static_cast<uint8_t>(static_cast<int>(nextRandom() * 4.0f) & 0x03);
    particle.life   = _rush_life;
    particle.active = true;
}

void PedometerView::stepRainPhysics(float dt)
{
    for (auto& particle : _particles) {
        if (!particle.active) {
            continue;
        }

        particle.life -= dt;
        if (particle.life <= 0.0f) {
            particle.active = false;
            continue;
        }

        particle.vy += _rain_gravity * dt;
        particle.x += particle.vx * dt;
        particle.y += particle.vy * dt;

        const float dist  = std::sqrt(particle.x * particle.x + particle.y * particle.y);
        const float limit = _bowl_radius - particle.radius;
        if (dist <= limit || dist < 0.001f) {
            continue;
        }

        const float nx = particle.x / dist;
        const float ny = particle.y / dist;
        const float vn = particle.vx * nx + particle.vy * ny;
        if (vn <= 0.0f) {
            // Still on its way in from above the screen, let it fall.
            continue;
        }

        particle.x  = nx * limit;
        particle.y  = ny * limit;
        particle.vx = (particle.vx - (1.0f + _rain_restitution) * vn * nx) * _rain_friction;
        particle.vy = (particle.vy - (1.0f + _rain_restitution) * vn * ny) * _rain_friction;
    }
}

void PedometerView::stepRushPhysics(float dt)
{
    for (auto& particle : _particles) {
        if (!particle.active) {
            continue;
        }

        particle.life -= dt;
        if (particle.life <= 0.0f) {
            particle.active = false;
            continue;
        }

        const float dist = std::sqrt(particle.x * particle.x + particle.y * particle.y);
        if (dist < _rush_absorb_r) {
            particle.active = false;
            _core_flash     = 1.0f;
            continue;
        }

        const float nx = -particle.x / dist;
        const float ny = -particle.y / dist;
        particle.vx += nx * _rush_pull * dt;
        particle.vy += ny * _rush_pull * dt;
        particle.vx -= particle.vx * _rush_drag * dt;
        particle.vy -= particle.vy * _rush_drag * dt;

        const float speed = std::sqrt(particle.vx * particle.vx + particle.vy * particle.vy);
        if (speed > _rush_max_speed) {
            const float scale = _rush_max_speed / speed;
            particle.vx *= scale;
            particle.vy *= scale;
        }

        particle.x += particle.vx * dt;
        particle.y += particle.vy * dt;
    }
}

void PedometerView::updateLabels()
{
    if (_mode_label) {
        switch (_mode) {
            case Mode::Rain:
                _mode_label->setText("RAIN");
                break;
            case Mode::Rush:
                _mode_label->setText("RUSH");
                break;
            case Mode::Count:
                _mode_label->setText("COUNT");
                break;
            case Mode::Graph:
                _mode_label->setText("24H");
                break;
            default:
                break;
        }
    }

    if (_step_label) {
        char buffer[16] = {};
        std::snprintf(buffer, sizeof(buffer), "%u", static_cast<unsigned>(_total_steps));
        _step_label->setText(buffer);
        _step_label->align(LV_ALIGN_CENTER, 0, -12);
    }

    if (_graph_total_label) {
        char buffer[16] = {};
        std::snprintf(buffer, sizeof(buffer), "%u", static_cast<unsigned>(_total_steps));
        _graph_total_label->setText(buffer);
        _graph_total_label->align(LV_ALIGN_CENTER, 0, 0);
    }
}

void PedometerView::invalidateStage()
{
    if (_stage) {
        lv_obj_invalidate(_stage->get());
    }
}

float PedometerView::nextRandom()
{
    // xorshift32, cheap and good enough for scatter.
    _rand_state ^= _rand_state << 13;
    _rand_state ^= _rand_state >> 17;
    _rand_state ^= _rand_state << 5;
    return static_cast<float>(_rand_state & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
}

void PedometerView::drawRain(lv_layer_t* layer, const lv_point_t& center) const
{
    for (const auto& particle : _particles) {
        if (!particle.active) {
            continue;
        }

        draw_disc(layer, static_cast<float>(center.x) + particle.x, static_cast<float>(center.y) + particle.y,
                  particle.radius, lv_color_hex(_particle_tints[particle.tint]), life_to_opa(particle.life));
    }
}

void PedometerView::drawRush(lv_layer_t* layer, const lv_point_t& center) const
{
    lv_draw_line_dsc_t trail_dsc;
    lv_draw_line_dsc_init(&trail_dsc);
    trail_dsc.round_start = 1;
    trail_dsc.round_end   = 1;

    for (const auto& particle : _particles) {
        if (!particle.active) {
            continue;
        }

        const float head_x     = static_cast<float>(center.x) + particle.x;
        const float head_y     = static_cast<float>(center.y) + particle.y;
        const lv_opa_t opa     = life_to_opa(particle.life);
        const lv_color_t color = lv_color_hex(_particle_tints[particle.tint]);

        trail_dsc.color = color;
        trail_dsc.width = static_cast<int32_t>(std::lround(particle.radius));
        trail_dsc.opa   = static_cast<lv_opa_t>(opa / 3);
        set_line_points(trail_dsc, head_x - particle.vx * 0.055f, head_y - particle.vy * 0.055f, head_x, head_y);
        lv_draw_line(layer, &trail_dsc);

        draw_disc(layer, head_x, head_y, particle.radius, color, opa);
    }

    // The gravity well itself, flashing on every absorbed step.
    const float core_radius = _core_base_radius + 26.0f * _core_flash;
    draw_disc(layer, static_cast<float>(center.x), static_cast<float>(center.y), core_radius, lv_color_hex(_color_text),
              static_cast<lv_opa_t>(std::lround(120.0f + 135.0f * _core_flash)));

    lv_draw_arc_dsc_t ring_dsc;
    lv_draw_arc_dsc_init(&ring_dsc);
    ring_dsc.color       = lv_color_hex(_color_blue);
    ring_dsc.width       = 3;
    ring_dsc.radius      = static_cast<uint16_t>(std::lround(core_radius + 16.0f));
    ring_dsc.center      = center;
    ring_dsc.start_angle = 0;
    ring_dsc.end_angle   = 360;
    ring_dsc.opa         = static_cast<lv_opa_t>(std::lround(60.0f + 160.0f * _core_flash));
    lv_draw_arc(layer, &ring_dsc);
}

void PedometerView::drawGraph(lv_layer_t* layer, const lv_point_t& center) const
{
    lv_draw_arc_dsc_t base_dsc;
    lv_draw_arc_dsc_init(&base_dsc);
    base_dsc.color       = lv_color_hex(_color_divider);
    base_dsc.width       = 2;
    base_dsc.radius      = _graph_inner_radius;
    base_dsc.center      = center;
    base_dsc.start_angle = 0;
    base_dsc.end_angle   = 360;
    base_dsc.opa         = LV_OPA_50;
    lv_draw_arc(layer, &base_dsc);

    lv_draw_line_dsc_t bar_dsc;
    lv_draw_line_dsc_init(&bar_dsc);
    bar_dsc.width       = _graph_bar_width;
    bar_dsc.round_start = 1;
    bar_dsc.round_end   = 1;

    const float span = static_cast<float>(_graph_outer_radius - _graph_inner_radius);

    for (std::size_t hour = 0; hour < hour_slot_count; ++hour) {
        const float ratio = clamp01(static_cast<float>(_hourly_steps[hour]) / static_cast<float>(_peak_hourly));
        const float angle = hour_angle_rad(static_cast<int>(hour));
        const float dx    = std::cos(angle);
        const float dy    = std::sin(angle);

        // A stub is always drawn so the empty hours still read as part of the day.
        const float length = 4.0f + ratio * span * _graph_reveal;
        const bool active  = static_cast<int>(hour) == _active_hour;

        bar_dsc.color =
            active ? lv_color_hex(_color_pink) : lv_color_hex(_hourly_steps[hour] > 0 ? _color_blue : _color_divider);
        bar_dsc.opa = _hourly_steps[hour] > 0 ? LV_OPA_COVER : LV_OPA_40;
        set_line_points(bar_dsc, static_cast<float>(center.x) + dx * _graph_inner_radius,
                        static_cast<float>(center.y) + dy * _graph_inner_radius,
                        static_cast<float>(center.x) + dx * (_graph_inner_radius + length),
                        static_cast<float>(center.y) + dy * (_graph_inner_radius + length));
        lv_draw_line(layer, &bar_dsc);
    }
}

void PedometerView::drawEventCb(lv_event_t* e)
{
    auto* self = static_cast<PedometerView*>(lv_event_get_user_data(e));
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

    switch (self->_mode) {
        case Mode::Rain:
            self->drawRain(layer, center);
            break;
        case Mode::Rush:
            self->drawRush(layer, center);
            break;
        case Mode::Graph:
            self->drawGraph(layer, center);
            break;
        case Mode::Count:
        default:
            break;
    }
}
