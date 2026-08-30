/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <vector>
#include <smooth_lvgl.hpp>
#include <uitk/short_namespace.hpp>

namespace view {

/**
 * @brief Winamp style spectrum visualizer for the 466x466 round display.
 *
 * Tapping the screen cycles the render mode. Every mode is driven by all
 * `band_count` bands, so nothing is thrown away between them:
 *   Bloom  -> the mirrored radial petals, reduced envelope plus per band detail
 *   Bars   -> the classic vertical analyzer with falling peak caps
 *   Ring   -> one radial spoke per band around the full circle
 *   Wave   -> an oscilloscope trace whose envelope is the spectrum
 *   Tunnel -> one concentric ring per band, bass innermost
 *   Matrix -> an LED column grid, one column per band, with peak markers
 *
 * The peak frequency read-out only appears in the modes that leave the middle
 * of the screen free.
 */
class FftView {
public:
    static constexpr std::size_t band_count         = 20;
    static constexpr std::size_t reduced_band_count = 4;
    using SpectrumBands                             = std::array<float, band_count>;
    using ReducedSpectrumBands                      = std::array<float, reduced_band_count>;

    enum class Mode : uint8_t {
        Bloom = 0,
        Bars,
        Ring,
        Wave,
        Tunnel,
        Matrix,
        _count,
    };

    void init(lv_obj_t* parent);
    void setSpectrum(const SpectrumBands& bands);
    void setPeakFrequencyHz(float frequencyHz);
    void update();

    Mode mode() const
    {
        return _mode;
    }
    const SpectrumBands& displayBands() const
    {
        return _display_bands;
    }
    const ReducedSpectrumBands& reducedBands() const
    {
        return _reduced_bands;
    }
    int barRotation() const
    {
        return _bar_rot;
    }
    int barOffset() const
    {
        return _bar_ofs;
    }
    float barBlend() const
    {
        return _bar_blend;
    }

private:
    static void drawEventCb(lv_event_t* e);

    std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
    std::unique_ptr<uitk::lvgl_cpp::Container> _click_mask;
    std::unique_ptr<uitk::lvgl_cpp::Container> _center_disc;
    std::unique_ptr<uitk::lvgl_cpp::Container> _spectrum_panel;
    std::unique_ptr<uitk::lvgl_cpp::Label> _peak_frequency_label;
    std::unique_ptr<uitk::lvgl_cpp::Label> _peak_frequency_unit_label;
    std::unique_ptr<uitk::lvgl_cpp::Label> _mode_label;
    SpectrumBands _target_bands         = {};
    SpectrumBands _display_bands        = {};
    SpectrumBands _peak_bands           = {};
    ReducedSpectrumBands _reduced_bands = {};
    Mode _mode                          = Mode::Bloom;
    int _bar_rot                        = 0;
    int _bar_ofs                        = 0;
    int _bass_hit_count                 = 0;
    int _bass_cooldown                  = 0;
    int _rotation_dir                   = 1;
    float _bar_blend                    = 0.0f;
    float _disc_scale                   = 1.0f;
    float _wave_phase                   = 0.0f;
    float _peak_frequency_hz            = 0.0f;

    void cycleMode();
    void applyMode();
    bool showsCenterReadout() const;
    void updateCenterDisc();
    void applyPeakFrequencyLabel();
    void updateReducedBands();
    void updatePeakBands();
    void updateMotionState();
    void invalidateSpectrum();

    void drawBloom(lv_layer_t* layer, const lv_point_t& center) const;
    void drawBars(lv_layer_t* layer, const lv_point_t& center) const;
    void drawRing(lv_layer_t* layer, const lv_point_t& center) const;
    void drawWave(lv_layer_t* layer, const lv_point_t& center) const;
    void drawTunnel(lv_layer_t* layer, const lv_point_t& center) const;
    void drawMatrix(lv_layer_t* layer, const lv_point_t& center) const;
};

}  // namespace view
