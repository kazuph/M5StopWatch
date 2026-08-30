#include "view.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

LV_FONT_DECLARE(lv_font_source_han_sans_sc_16_cjk);

namespace view {
namespace {

constexpr uint32_t colorBackground = 0x000000;
constexpr uint32_t colorText = 0xD8F2FF;
constexpr uint32_t colorAccent = 0x9CF1B6;
constexpr uint32_t colorFlat = 0xB3CDFF;
constexpr uint32_t colorSharp = 0xFF9EAB;
constexpr uint32_t colorTrack = 0x41484B;

lv_obj_t* makeLabel(lv_obj_t* parent, const lv_font_t* font, uint32_t color, int y)
{
    auto* label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, y);
    return label;
}

void styleButton(lv_obj_t* button, uint32_t color)
{
    lv_obj_set_size(button, 78, 58);
    lv_obj_set_style_radius(button, 29, LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_border_width(button, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
}

}  // namespace

TunerView::~TunerView()
{
    if (_root != nullptr) {
        lv_obj_delete(_root);
    }
}

void TunerView::init(lv_obj_t* parent)
{
    _root = lv_obj_create(parent);
    lv_obj_set_size(_root, 466, 466);
    lv_obj_center(_root);
    lv_obj_set_style_bg_color(_root, lv_color_hex(colorBackground), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(_root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(_root, 0, LV_PART_MAIN);
    lv_obj_remove_flag(_root, LV_OBJ_FLAG_SCROLLABLE);

    auto* title = makeLabel(_root, &lv_font_montserrat_20, colorAccent, -181);
    lv_label_set_text(title, "CHROMATIC TUNER");

    _note = makeLabel(_root, &lv_font_source_han_sans_sc_16_cjk, colorText, -105);
    lv_obj_set_style_transform_scale(_note, 512, LV_PART_MAIN);
    lv_label_set_text(_note, "--");

    _frequency = makeLabel(_root, &lv_font_montserrat_28, colorText, -58);
    lv_label_set_text(_frequency, "-- Hz");
    _cents = makeLabel(_root, &lv_font_montserrat_20, colorText, -18);
    lv_label_set_text(_cents, "-- cent");

    _meter = lv_slider_create(_root);
    lv_obj_set_size(_meter, 300, 20);
    lv_obj_align(_meter, LV_ALIGN_CENTER, 0, 38);
    lv_slider_set_range(_meter, -50, 50);
    lv_slider_set_value(_meter, 0, LV_ANIM_OFF);
    lv_obj_remove_flag(_meter, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(_meter, lv_color_hex(colorTrack), LV_PART_MAIN);
    lv_obj_set_style_bg_color(_meter, lv_color_hex(colorAccent), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(_meter, lv_color_hex(colorText), LV_PART_KNOB);

    auto* low = makeLabel(_root, &lv_font_montserrat_16, colorFlat, 71);
    lv_label_set_text(low, "-50");
    lv_obj_align(low, LV_ALIGN_CENTER, -139, 71);
    auto* center = makeLabel(_root, &lv_font_montserrat_16, colorAccent, 71);
    lv_label_set_text(center, "0");
    auto* high = makeLabel(_root, &lv_font_montserrat_16, colorSharp, 71);
    lv_label_set_text(high, "+50");
    lv_obj_align(high, LV_ALIGN_CENTER, 139, 71);

    _calibration = makeLabel(_root, &lv_font_montserrat_28, colorText, 119);

    _minus = lv_button_create(_root);
    styleButton(_minus, colorFlat);
    lv_obj_align(_minus, LV_ALIGN_CENTER, -75, 170);
    auto* minusLabel = lv_label_create(_minus);
    lv_obj_set_style_text_font(minusLabel, &lv_font_montserrat_28, LV_PART_MAIN);
    lv_label_set_text(minusLabel, "-");
    lv_obj_center(minusLabel);
    lv_obj_add_event_cb(_minus, calibrationEvent, LV_EVENT_CLICKED, this);

    _plus = lv_button_create(_root);
    styleButton(_plus, colorAccent);
    lv_obj_align(_plus, LV_ALIGN_CENTER, 75, 170);
    auto* plusLabel = lv_label_create(_plus);
    lv_obj_set_style_text_font(plusLabel, &lv_font_montserrat_28, LV_PART_MAIN);
    lv_label_set_text(plusLabel, "+");
    lv_obj_center(plusLabel);
    lv_obj_add_event_cb(_plus, calibrationEvent, LV_EVENT_CLICKED, this);
    setCalibration(_calibrationHz);
}

void TunerView::setPitch(const tuner::PitchResult& pitch)
{
    if (!pitch.valid) {
        lv_label_set_text(_note, "--");
        lv_label_set_text(_frequency, "-- Hz");
        lv_label_set_text(_cents, "-- cent");
        lv_slider_set_value(_meter, 0, LV_ANIM_OFF);
        return;
    }

    const std::string note = std::string(tuner::katakanaNoteName(pitch.pitchClass)) + " " + std::to_string(pitch.octave);
    lv_label_set_text(_note, note.c_str());
    char buffer[32] = {};
    std::snprintf(buffer, sizeof(buffer), "%.2f Hz", static_cast<double>(pitch.frequencyHz));
    lv_label_set_text(_frequency, buffer);
    std::snprintf(buffer, sizeof(buffer), "%+.1f cent", static_cast<double>(pitch.cents));
    lv_label_set_text(_cents, buffer);
    lv_slider_set_value(_meter, static_cast<int32_t>(std::lround(std::clamp(pitch.cents, -50.0f, 50.0f))), LV_ANIM_OFF);
}

void TunerView::setCalibration(int hz)
{
    _calibrationHz = std::clamp(hz, tuner::calibrationMinHz, tuner::calibrationMaxHz);
    char buffer[24] = {};
    std::snprintf(buffer, sizeof(buffer), "A4 = %d Hz", _calibrationHz);
    lv_label_set_text(_calibration, buffer);
}

void TunerView::calibrationEvent(lv_event_t* event)
{
    auto* self = static_cast<TunerView*>(lv_event_get_user_data(event));
    const int delta = lv_event_get_target_obj(event) == self->_minus ? -1 : 1;
    const int next = std::clamp(self->_calibrationHz + delta, tuner::calibrationMinHz, tuner::calibrationMaxHz);
    if (next != self->_calibrationHz && self->onCalibrationChange) {
        self->onCalibrationChange(next);
    }
}

}  // namespace view
