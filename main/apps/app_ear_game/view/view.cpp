#include "view.h"

#include <array>
#include <algorithm>

LV_FONT_DECLARE(lv_font_source_han_sans_sc_16_cjk);

namespace view {
namespace {

constexpr uint32_t colorBackground = 0x000000;
constexpr uint32_t colorText = 0xD8F2FF;
constexpr uint32_t colorGreen = 0x9CF1B6;
constexpr uint32_t colorBlue = 0xB3CDFF;
constexpr uint32_t colorPink = 0xFF9EAB;
constexpr uint32_t colorDim = 0x41484B;

void styleLabel(lv_obj_t* label, uint32_t color)
{
    lv_obj_set_style_text_font(label, &lv_font_source_han_sans_sc_16_cjk, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
}

}  // namespace

EarGameView::~EarGameView()
{
    if (_root != nullptr) {
        lv_obj_delete(_root);
    }
}

void EarGameView::init(lv_obj_t* parent)
{
    _root = lv_obj_create(parent);
    lv_obj_set_size(_root, 466, 466);
    lv_obj_center(_root);
    lv_obj_set_style_bg_color(_root, lv_color_hex(colorBackground), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(_root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(_root, 0, LV_PART_MAIN);
    lv_obj_remove_flag(_root, LV_OBJ_FLAG_SCROLLABLE);
    showModeSelection();
}

void EarGameView::clearContent()
{
    if (_content != nullptr) {
        lv_obj_delete(_content);
    }
    _content = lv_obj_create(_root);
    lv_obj_set_size(_content, 466, 466);
    lv_obj_center(_content);
    lv_obj_set_style_bg_opa(_content, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(_content, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(_content, 0, LV_PART_MAIN);
    lv_obj_remove_flag(_content, LV_OBJ_FLAG_SCROLLABLE);
    _play = nullptr;
    std::fill(std::begin(_choices), std::end(_choices), nullptr);
}

lv_obj_t* EarGameView::makeButton(const char* text, int y, uint32_t color)
{
    auto* button = lv_button_create(_content);
    lv_obj_set_size(button, 310, 64);
    lv_obj_align(button, LV_ALIGN_CENTER, 0, y);
    lv_obj_set_style_radius(button, 32, LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_border_width(button, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
    auto* label = lv_label_create(button);
    styleLabel(label, colorBackground);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return button;
}

void EarGameView::showModeSelection()
{
    clearContent();
    auto* title = lv_label_create(_content);
    styleLabel(title, colorText);
    lv_label_set_text(title, "ナニノ モンダイ?");
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -145);

    const std::array<const char*, 3> labels = {"タンオン", "オンカイ", "ワオンノ コウセイオン"};
    const std::array<uint32_t, 3> colors = {colorGreen, colorBlue, colorPink};
    for (int i = 0; i < 3; ++i) {
        _choices[i] = makeButton(labels[i], -55 + i * 82, colors[i]);
        lv_obj_add_event_cb(_choices[i], modeEvent, LV_EVENT_CLICKED, this);
    }
}

void EarGameView::showQuestion(const ear_game::Question& question)
{
    clearContent();
    auto* title = lv_label_create(_content);
    styleLabel(title, colorText);
    const char* questionTitle = "ナッテイル オトハ?";
    if (question.mode == ear_game::Mode::Scale) {
        questionTitle = "ドノ オンカイ?";
    } else if (question.mode == ear_game::Mode::Chord) {
        questionTitle = "ナッテイル コウセイオンハ?";
    }
    lv_label_set_text(title, questionTitle);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -183);

    _play = makeButton("モウイチド キク", -118, colorGreen);
    lv_obj_add_event_cb(_play, playEvent, LV_EVENT_CLICKED, this);

    for (int i = 0; i < 3; ++i) {
        _choices[i] = makeButton(question.choices[i].c_str(), -25 + i * 74, i == 0 ? colorBlue : colorDim);
        lv_obj_add_event_cb(_choices[i], choiceEvent, LV_EVENT_CLICKED, this);
    }
}

void EarGameView::showAnswer(bool correct, const std::string& detail)
{
    clearContent();
    auto* result = lv_label_create(_content);
    styleLabel(result, correct ? colorGreen : colorPink);
    lv_label_set_text(result, correct ? "セイカイ!" : "オシイ!");
    lv_obj_align(result, LV_ALIGN_CENTER, 0, -85);

    auto* answer = lv_label_create(_content);
    styleLabel(answer, colorText);
    lv_obj_set_width(answer, 330);
    lv_label_set_long_mode(answer, LV_LABEL_LONG_WRAP);
    lv_label_set_text(answer, detail.c_str());
    lv_obj_align(answer, LV_ALIGN_CENTER, 0, -15);

    auto* next = makeButton("ツギノ モンダイ", 100, colorGreen);
    lv_obj_add_event_cb(next, nextEvent, LV_EVENT_CLICKED, this);
}

void EarGameView::modeEvent(lv_event_t* event)
{
    auto* self = static_cast<EarGameView*>(lv_event_get_user_data(event));
    const auto* target = lv_event_get_target_obj(event);
    for (int i = 0; i < 3; ++i) {
        if (target == self->_choices[i] && self->onModeSelected) {
            self->onModeSelected(static_cast<ear_game::Mode>(i));
        }
    }
}

void EarGameView::playEvent(lv_event_t* event)
{
    auto* self = static_cast<EarGameView*>(lv_event_get_user_data(event));
    if (self->onPlay) {
        self->onPlay();
    }
}

void EarGameView::choiceEvent(lv_event_t* event)
{
    auto* self = static_cast<EarGameView*>(lv_event_get_user_data(event));
    const auto* target = lv_event_get_target_obj(event);
    for (int i = 0; i < 3; ++i) {
        if (target == self->_choices[i] && self->onChoiceSelected) {
            self->onChoiceSelected(i);
        }
    }
}

void EarGameView::nextEvent(lv_event_t* event)
{
    auto* self = static_cast<EarGameView*>(lv_event_get_user_data(event));
    if (self->onNext) {
        self->onNext();
    }
}

}  // namespace view
