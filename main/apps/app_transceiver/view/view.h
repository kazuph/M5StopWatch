/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <smooth_lvgl.hpp>
#include <string>
#include <uitk/short_namespace.hpp>

namespace view {

/**
 * @brief Transceiver face for the 466x466 round display.
 *
 * The incoming voice is drawn as a messy Winamp style trace looped around the
 * centre of the screen. The view never runs an FFT of its own: the sender does
 * the analysis and hands both ends the same 20 quantized band values, and the
 * geometry here is a pure function of those values plus the talker role. Two
 * radios fed the same `setTalkerRole` and `setSpectrum` therefore paint the
 * identical shape in the identical colour, with no time, no random state and no
 * frame to frame smoothing anywhere in the drawing path.
 *
 * Taps are confined to the states where a call is being set up or torn down: Off,
 * Searching, Calling, Incoming and Connecting all raise `onPowerToggle` and the app
 * decides what that means, with Incoming answering by tap or by the physical blue
 * button, either works. Once the call is up, Connected, Talking and Listening ignore
 * the screen entirely - a stray touch during a conversation used to hang it up - so
 * talking is driven from the blue button and the hints say so. Error ignores taps too.
 *
 * Setting up a call takes three steps - the caller sends CALL, the callee answers,
 * and the parent confirms - so Connecting covers the gap between the answer and
 * the confirmation.
 */
class TransceiverView {
public:
    static constexpr std::size_t band_count = 20;
    using SpectrumBands                     = std::array<uint8_t, band_count>;

    enum class State : uint8_t {
        Off = 0,
        /** @brief Powered up and looking for the other radio, no approval needed. */
        Searching,
        Calling,
        Incoming,
        /** @brief Answered, waiting for the parent to confirm. */
        Connecting,
        Connected,
        Talking,
        Listening,
        Error,
    };

    enum class Role : uint8_t {
        None = 0,
        Parent,
        Child,
    };

    enum class ConversationMode : uint8_t {
        Ptt,
        OpenMic,
    };

    void init(lv_obj_t* parent);

    void setState(State state);

    /** @brief Role of this radio, used for its own colour and label. */
    void setRole(Role role);

    /** @brief Role of whoever produced the current spectrum, used for the trace colour. */
    void setTalkerRole(Role talkerRole);

    /** @brief The 20 band values the sender already quantized. Never re-analysed here. */
    void setSpectrum(const SpectrumBands& bands);

    void setVolumePercent(int volume);
    void setConversationMode(ConversationMode mode);

    /** @brief Raised by a tap while discovery or connection setup can still be cancelled or answered. */
    std::function<void()> onPowerToggle;
    std::function<void()> onConversationModeToggle;

    State state() const
    {
        return _state;
    }
    Role role() const
    {
        return _role;
    }
    Role talkerRole() const
    {
        return _talker_role;
    }

private:
    static void drawEventCb(lv_event_t* e);

    void handleTap();
    void applyState();
    void invalidateStage();
    void applyConversationMode();
    bool showsWaveform() const;
    bool acceptsTap() const;

    void drawBaseline(lv_layer_t* layer, const lv_point_t& center) const;
    void drawWaveform(lv_layer_t* layer, const lv_point_t& center) const;

    std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
    std::unique_ptr<uitk::lvgl_cpp::Container> _stage;
    std::unique_ptr<uitk::lvgl_cpp::Container> _click_mask;
    std::unique_ptr<uitk::lvgl_cpp::Label> _role_label;
    std::unique_ptr<uitk::lvgl_cpp::Label> _state_label;
    std::unique_ptr<uitk::lvgl_cpp::Label> _hint_label;
    std::unique_ptr<uitk::lvgl_cpp::Label> _volume_label;
    std::unique_ptr<uitk::lvgl_cpp::Button> _ptt_button;
    std::unique_ptr<uitk::lvgl_cpp::Button> _open_mic_button;
    std::string _volume_text;

    SpectrumBands _bands = {};

    State _state                        = State::Off;
    Role _role                          = Role::None;
    Role _talker_role                   = Role::None;
    ConversationMode _conversation_mode = ConversationMode::Ptt;
};

}  // namespace view
