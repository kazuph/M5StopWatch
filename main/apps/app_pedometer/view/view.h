/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <smooth_lvgl.hpp>
#include <uitk/short_namespace.hpp>

namespace view {

/**
 * @brief Pedometer visualizer for the 466x466 round display.
 *
 * Tapping anywhere on the screen cycles the visualization, and there are
 * exactly four of them:
 *   Rain  -> circles dropped in from the top, falling straight down under
 *            gravity, one circle per counted step
 *   Rush  -> bullets launched in from the left/right edge and pulled into the
 *            middle by a central gravity well, one bullet per counted step
 *   Count -> the plain total step number, nothing else
 *   Graph -> the 0..23 hour step history
 *
 * The particle pool is a fixed size display budget. It never feeds back into
 * the step data: `_total_steps` and `_hourly_steps` always mirror exactly what
 * the HAL reported, no matter how many spawns had to be recycled.
 */
class PedometerView {
public:
    static constexpr std::size_t hour_slot_count = 24;
    static constexpr std::size_t particle_count  = 64;

    using HourlySteps = std::array<uint32_t, hour_slot_count>;

    enum class Mode : uint8_t {
        Rain = 0,
        Rush,
        Count,
        Graph,
        _count,
    };

    void init(lv_obj_t* parent);

    /**
     * @brief Feed the latest pedometer snapshot.
     *
     * The difference against the previous total is queued as particle spawns,
     * so while walking every single counted step becomes one falling circle in
     * Rain and one incoming bullet in Rush.
     */
    void setStepData(uint32_t totalSteps, const HourlySteps& hourlySteps);

    void update();

    Mode mode() const
    {
        return _mode;
    }
    uint32_t totalSteps() const
    {
        return _total_steps;
    }

private:
    struct Particle {
        float x      = 0.0f;  // stage local coordinates, origin at the screen center
        float y      = 0.0f;
        float vx     = 0.0f;
        float vy     = 0.0f;
        float life   = 0.0f;  // seconds left before the slot is recycled
        float radius = 9.0f;
        uint8_t tint = 0;
        bool active  = false;
    };

    static void drawEventCb(lv_event_t* e);

    void cycleMode();
    void applyMode();
    void resetParticles();
    Particle& acquireParticle();
    void drainPendingSpawns();
    void spawnRainParticle();
    void spawnRushParticle();
    void stepRainPhysics(float dt);
    void stepRushPhysics(float dt);
    void updateLabels();
    void invalidateStage();
    float nextRandom();

    void drawRain(lv_layer_t* layer, const lv_point_t& center) const;
    void drawRush(lv_layer_t* layer, const lv_point_t& center) const;
    void drawGraph(lv_layer_t* layer, const lv_point_t& center) const;

    std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
    std::unique_ptr<uitk::lvgl_cpp::Container> _stage;
    std::unique_ptr<uitk::lvgl_cpp::Container> _click_mask;
    std::unique_ptr<uitk::lvgl_cpp::Label> _step_label;
    std::unique_ptr<uitk::lvgl_cpp::Label> _step_unit_label;
    std::unique_ptr<uitk::lvgl_cpp::Label> _mode_label;
    std::unique_ptr<uitk::lvgl_cpp::Label> _graph_total_label;
    std::array<std::unique_ptr<uitk::lvgl_cpp::Label>, 4> _hour_labels = {};

    std::array<Particle, particle_count> _particles = {};
    HourlySteps _hourly_steps                       = {};

    Mode _mode              = Mode::Rain;
    uint32_t _total_steps   = 0;
    uint32_t _last_total    = 0;
    uint32_t _peak_hourly   = 1;
    uint32_t _rand_state    = 0x9E3779B9u;
    uint32_t _last_tick_ms  = 0;
    float _pending_spawns   = 0.0f;
    float _core_flash       = 0.0f;
    float _graph_reveal     = 0.0f;
    int _active_hour        = -1;  // no hour is highlighted until one is seen changing
    int _rush_side          = 0;
    bool _has_previous_data = false;
};

}  // namespace view
