/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "view.h"
#include <assets/assets.h>
#include <algorithm>
#include <cmath>

using namespace view;
using namespace uitk::lvgl_cpp;

namespace {

constexpr int _panel_size = 466;

// Palette taken from the stopwatch app. Parent and child keep the two colours the
// product already uses for them, so a trace is identified by colour alone.
constexpr uint32_t _color_bg       = 0x000000;
constexpr uint32_t _color_parent   = 0xFF9EAB;
constexpr uint32_t _color_child    = 0xB3CDFF;
constexpr uint32_t _color_state    = 0xD8F2FF;
constexpr uint32_t _color_muted    = 0x738086;
constexpr uint32_t _color_baseline = 0x41484B;

// The trace is an annulus, which leaves the middle of the screen free for the labels.
constexpr float _radius_base = 168.0f;
constexpr float _radius_span = 56.0f;
constexpr int _sample_count  = 120;
constexpr int _trace_width   = 3;
constexpr float _center_dot  = 5.0f;

constexpr int _role_label_y   = -56;
constexpr int _state_label_y  = -8;
constexpr int _hint_label_y   = 52;
constexpr int _volume_label_y = 90;
constexpr int _mode_button_y  = 142;
constexpr int _mode_button_x  = 61;
constexpr int _mode_button_w  = 116;
constexpr int _mode_button_h  = 48;

constexpr float _pi = 3.14159265358979323846f;

/**
 * @brief Integer avalanche mix. Every bit of the drawing seed comes out of here, so the
 *        trace is reproducible from the transmitted values alone with no float rounding
 *        anywhere in the seed path.
 */
uint32_t mix32(uint32_t value)
{
    value ^= value >> 16;
    value *= 0x7FEB352Du;
    value ^= value >> 15;
    value *= 0x846CA68Bu;
    value ^= value >> 16;
    return value;
}

uint32_t role_seed(TransceiverView::Role role)
{
    switch (role) {
        case TransceiverView::Role::Parent:
            return 1u;
        case TransceiverView::Role::Child:
            return 2u;
        default:
            return 0u;
    }
}

uint32_t role_color(TransceiverView::Role role)
{
    switch (role) {
        case TransceiverView::Role::Parent:
            return _color_parent;
        case TransceiverView::Role::Child:
            return _color_child;
        default:
            return _color_muted;
    }
}

const char* role_text(TransceiverView::Role role)
{
    switch (role) {
        case TransceiverView::Role::Parent:
            return "PARENT";
        case TransceiverView::Role::Child:
            return "CHILD";
        default:
            return "-";
    }
}

const char* state_text(TransceiverView::State state)
{
    switch (state) {
        case TransceiverView::State::Off:
            return "OFF";
        case TransceiverView::State::Searching:
            return "SEARCHING";
        case TransceiverView::State::Calling:
            return "CALLING";
        case TransceiverView::State::Incoming:
            return "INCOMING";
        case TransceiverView::State::Connecting:
            return "CONNECTING";
        case TransceiverView::State::Connected:
            return "CONNECTED";
        case TransceiverView::State::Talking:
            return "TALKING";
        case TransceiverView::State::Listening:
            return "LISTENING";
        default:
            return "ERROR";
    }
}

/**
 * @brief What the user should do next in this state.
 *
 * An incoming call can be answered either way, so that row names both routes rather
 * than sending the user to the button for something the screen also accepts. Once the
 * call is up the screen stops taking taps, so those rows describe the blue button
 * instead of offering a touch action that is deliberately ignored.
 */
const char* hint_text(TransceiverView::State state, TransceiverView::ConversationMode mode)
{
    switch (state) {
        case TransceiverView::State::Off:
            return "TAP TO CALL";
        case TransceiverView::State::Searching:
        case TransceiverView::State::Calling:
        case TransceiverView::State::Connecting:
            return "TAP TO CANCEL";
        case TransceiverView::State::Incoming:
            return "TAP OR BLUE BUTTON";
        case TransceiverView::State::Connected:
            return mode == TransceiverView::ConversationMode::OpenMic ? "BOTH SIDES LIVE" : "HOLD BLUE TO TALK";
        case TransceiverView::State::Talking:
            return "RELEASE TO LISTEN";
        case TransceiverView::State::Listening:
            return "LISTENING";
        default:
            return "";
    }
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

void TransceiverView::init(lv_obj_t* parent)
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
    lv_obj_add_event_cb(_stage->get(), TransceiverView::drawEventCb, LV_EVENT_ALL, this);

    _role_label = std::make_unique<Label>(_panel->get());
    _role_label->align(LV_ALIGN_CENTER, 0, _role_label_y);
    _role_label->setTextFont(&lv_font_montserrat_20);
    _role_label->setTextColor(lv_color_hex(_color_muted));
    _role_label->setText(role_text(Role::None));

    _state_label = std::make_unique<Label>(_panel->get());
    _state_label->align(LV_ALIGN_CENTER, 0, _state_label_y);
    _state_label->setTextFont(&lv_font_montserrat_28);
    _state_label->setTextColor(lv_color_hex(_color_state));
    _state_label->setText(state_text(State::Off));

    _hint_label = std::make_unique<Label>(_panel->get());
    _hint_label->align(LV_ALIGN_CENTER, 0, _hint_label_y);
    _hint_label->setTextFont(&lv_font_montserrat_20);
    _hint_label->setTextColor(lv_color_hex(_color_muted));
    _hint_label->setText(hint_text(State::Off, ConversationMode::Ptt));

    _volume_label = std::make_unique<Label>(_panel->get());
    _volume_label->align(LV_ALIGN_CENTER, 0, _volume_label_y);
    _volume_label->setTextFont(&lv_font_montserrat_16);
    _volume_label->setTextColor(lv_color_hex(_color_muted));
    setVolumePercent(0);

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
    _click_mask->onClick().connect([this]() { handleTap(); });
    _click_mask->moveForeground();

    auto configure_mode_button = [](Button& button, int x, const char* text) {
        button.setSize(_mode_button_w, _mode_button_h);
        button.align(LV_ALIGN_CENTER, x, _mode_button_y);
        button.setRadius(_mode_button_h / 2);
        button.setBorderWidth(2);
        button.setShadowWidth(0);
        button.label().setText(text);
        button.label().setTextFont(&lv_font_montserrat_16);
        button.label().align(LV_ALIGN_CENTER, 0, 0);
    };

    _ptt_button = std::make_unique<Button>(_panel->get());
    configure_mode_button(*_ptt_button, -_mode_button_x, "PTT");
    _ptt_button->onClick().connect([this]() {
        if (_conversation_mode != ConversationMode::Ptt && onConversationModeToggle) {
            onConversationModeToggle();
        }
    });

    _open_mic_button = std::make_unique<Button>(_panel->get());
    configure_mode_button(*_open_mic_button, _mode_button_x, "OPEN MIC");
    _open_mic_button->onClick().connect([this]() {
        if (_conversation_mode != ConversationMode::OpenMic && onConversationModeToggle) {
            onConversationModeToggle();
        }
    });

    applyState();
}

void TransceiverView::setState(State state)
{
    if (_state == state) {
        return;
    }

    _state = state;
    applyState();
}

void TransceiverView::setRole(Role role)
{
    if (_role == role) {
        return;
    }

    _role = role;
    applyState();
}

void TransceiverView::setTalkerRole(Role talkerRole)
{
    if (_talker_role == talkerRole) {
        return;
    }

    _talker_role = talkerRole;
    invalidateStage();
}

void TransceiverView::setSpectrum(const SpectrumBands& bands)
{
    if (_bands == bands) {
        return;
    }

    _bands = bands;
    invalidateStage();
}

void TransceiverView::setVolumePercent(int volume)
{
    std::string next = volume == 0 ? "Y: MUTE" : "Y: VOL " + std::to_string(volume) + "%";
    if (next == _volume_text) {
        return;
    }
    _volume_text = next;
    if (_volume_label) {
        _volume_label->setText(_volume_text.c_str());
        _volume_label->align(LV_ALIGN_CENTER, 0, _volume_label_y);
    }
}

void TransceiverView::setConversationMode(ConversationMode mode)
{
    if (_conversation_mode == mode) {
        return;
    }
    _conversation_mode = mode;
    applyState();
}

void TransceiverView::handleTap()
{
    // Taps only reach the app while a call is being set up or torn down. Incoming is
    // included, because the physical blue button is a second way to answer rather than the
    // only one. A live conversation ignores the screen so it cannot be dropped by accident.
    if (!acceptsTap()) {
        return;
    }

    if (onPowerToggle) {
        onPowerToggle();
    }
}

bool TransceiverView::acceptsTap() const
{
    // Only while a call is being set up or torn down. Connected, Talking and Listening are
    // excluded on purpose: a stray touch mid conversation was hanging real calls up, and
    // talking is a blue button gesture anyway. Error has nothing to toggle.
    return _state == State::Off || _state == State::Searching || _state == State::Calling ||
           _state == State::Incoming || _state == State::Connecting;
}

bool TransceiverView::showsWaveform() const
{
    return _state == State::Connected || _state == State::Talking || _state == State::Listening;
}

void TransceiverView::applyState()
{
    if (_role_label) {
        _role_label->setText(role_text(_role));
        _role_label->setTextColor(lv_color_hex(role_color(_role)));
        _role_label->align(LV_ALIGN_CENTER, 0, _role_label_y);
    }

    if (_state_label) {
        _state_label->setText(state_text(_state));
        _state_label->setTextColor(lv_color_hex(_state == State::Error ? _color_parent : _color_state));
        _state_label->align(LV_ALIGN_CENTER, 0, _state_label_y);
    }

    if (_hint_label) {
        const char* hint = hint_text(_state, _conversation_mode);
        _hint_label->setText(hint);
        _hint_label->setHidden(hint[0] == '\0');
        _hint_label->align(LV_ALIGN_CENTER, 0, _hint_label_y);
    }

    applyConversationMode();

    invalidateStage();
}

void TransceiverView::applyConversationMode()
{
    if (!_ptt_button || !_open_mic_button) {
        return;
    }
    const bool connected = showsWaveform();
    _ptt_button->setHidden(!connected);
    _open_mic_button->setHidden(!connected);

    const lv_color_t selected = lv_color_hex(role_color(_role));
    const lv_color_t idle     = lv_color_hex(_color_baseline);
    const lv_color_t text     = lv_color_hex(_color_state);
    _ptt_button->setBgColor(_conversation_mode == ConversationMode::Ptt ? selected : idle);
    _open_mic_button->setBgColor(_conversation_mode == ConversationMode::OpenMic ? selected : idle);
    _ptt_button->setBorderColor(_conversation_mode == ConversationMode::Ptt ? selected : lv_color_hex(_color_muted));
    _open_mic_button->setBorderColor(_conversation_mode == ConversationMode::OpenMic ? selected
                                                                                     : lv_color_hex(_color_muted));
    _ptt_button->label().setTextColor(text);
    _open_mic_button->label().setTextColor(text);
}

void TransceiverView::invalidateStage()
{
    if (_stage) {
        lv_obj_invalidate(_stage->get());
    }
}

void TransceiverView::drawBaseline(lv_layer_t* layer, const lv_point_t& center) const
{
    lv_draw_arc_dsc_t dsc;
    lv_draw_arc_dsc_init(&dsc);
    dsc.center      = center;
    dsc.radius      = static_cast<uint16_t>(std::lround(_radius_base));
    dsc.width       = 2;
    dsc.start_angle = 0;
    dsc.end_angle   = 360;
    dsc.rounded     = 0;

    // Off and Error keep a plain grey rest ring; while a call is being set up the ring
    // already carries the colour of the radio the audio will come from.
    if (_state == State::Searching || _state == State::Calling || _state == State::Connecting) {
        dsc.color = lv_color_hex(role_color(_role));
        dsc.opa   = LV_OPA_50;
    } else if (_state == State::Incoming) {
        dsc.color = lv_color_hex(role_color(_talker_role));
        dsc.opa   = LV_OPA_70;
    } else if (showsWaveform()) {
        dsc.color = lv_color_hex(_color_baseline);
        dsc.opa   = LV_OPA_60;
    } else {
        dsc.color = lv_color_hex(_color_baseline);
        dsc.opa   = LV_OPA_40;
    }

    lv_draw_arc(layer, &dsc);

    draw_disc(layer, static_cast<float>(center.x), static_cast<float>(center.y), _center_dot,
              lv_color_hex(_color_baseline), LV_OPA_70);
}

void TransceiverView::drawWaveform(lv_layer_t* layer, const lv_point_t& center) const
{
    // ---------------------------------------------------------------------------
    // Deterministic geometry. For sample i of _sample_count, with the transmitted
    // band values b[0..19] and the talker role R:
    //
    //   theta  = 2*pi*i / _sample_count
    //   pos    = i * 20 / _sample_count                      (integer band index)
    //   frac   = (i * 20 % _sample_count) / _sample_count    (weight to the next band)
    //   level  = ((1-frac)*b[pos] + frac*b[(pos+1) % 20]) / 255
    //   h      = mix32(i*0x9E3779B9 ^ mix32(b[pos]*0x85EBCA6B ^ seed(R)*0x165667B1))
    //   swing  = ((h & 0xFFFF) - 32768) / 32768              in [-1, 1)
    //   r      = _radius_base + level * _radius_span * swing
    //   point  = centre + r * (cos theta, sin theta)
    //
    // The band values supply the envelope, so the ring is loud where the sound is
    // loud, and the hash supplies the mess. Both terms are functions of the received
    // values only - no clock, no persistent random state, no smoothing - so the two
    // radios draw the same messy shape from the same packet.
    // ---------------------------------------------------------------------------
    const uint32_t seed    = role_seed(_talker_role) * 0x165667B1u;
    const lv_color_t color = lv_color_hex(role_color(_talker_role));

    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.width       = _trace_width;
    dsc.round_start = 1;
    dsc.round_end   = 1;
    dsc.color       = color;

    float first_x    = 0.0f;
    float first_y    = 0.0f;
    float prev_x     = 0.0f;
    float prev_y     = 0.0f;
    float prev_level = 0.0f;

    for (int i = 0; i <= _sample_count; ++i) {
        const int sample = i % _sample_count;

        const int pos    = sample * static_cast<int>(band_count) / _sample_count;
        const int next   = (pos + 1) % static_cast<int>(band_count);
        const float frac = static_cast<float>(sample * static_cast<int>(band_count) % _sample_count) /
                           static_cast<float>(_sample_count);
        const float level =
            ((1.0f - frac) * static_cast<float>(_bands[pos]) + frac * static_cast<float>(_bands[next])) / 255.0f;

        const uint32_t hash = mix32(static_cast<uint32_t>(sample) * 0x9E3779B9u ^
                                    mix32(static_cast<uint32_t>(_bands[pos]) * 0x85EBCA6Bu ^ seed));
        const float swing   = (static_cast<float>(hash & 0xFFFFu) - 32768.0f) / 32768.0f;

        const float theta = 2.0f * _pi * static_cast<float>(sample) / static_cast<float>(_sample_count);
        const float r     = _radius_base + level * _radius_span * swing;
        const float x     = static_cast<float>(center.x) + r * std::cos(theta);
        const float y     = static_cast<float>(center.y) + r * std::sin(theta);

        if (i == 0) {
            first_x = x;
            first_y = y;
        } else {
            // Louder segments are drawn more solidly, which is again only a function of
            // the transmitted values.
            const float loudest = std::max(prev_level, level);
            dsc.opa             = static_cast<lv_opa_t>(std::lround(110.0f + 145.0f * std::clamp(loudest, 0.0f, 1.0f)));
            set_line_points(dsc, prev_x, prev_y, i == _sample_count ? first_x : x, i == _sample_count ? first_y : y);
            lv_draw_line(layer, &dsc);
        }

        prev_x     = x;
        prev_y     = y;
        prev_level = level;
    }
}

void TransceiverView::drawEventCb(lv_event_t* e)
{
    auto* self = static_cast<TransceiverView*>(lv_event_get_user_data(e));
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

    self->drawBaseline(layer, center);

    if (self->showsWaveform()) {
        self->drawWaveform(layer, center);
    }
}
