#pragma once

#include "../model/ear_game.h"
#include <functional>
#include <lvgl.h>

namespace view {

class EarGameView {
public:
    ~EarGameView();
    void init(lv_obj_t* parent);
    void showModeSelection();
    void showQuestion(const ear_game::Question& question);
    void showAnswer(bool correct, const std::string& detail);
    void setVolumePercent(int volume);

    std::function<void(ear_game::Mode)> onModeSelected;
    std::function<void()> onPlay;
    std::function<void(int)> onChoiceSelected;
    std::function<void()> onNext;

private:
    static void modeEvent(lv_event_t* event);
    static void playEvent(lv_event_t* event);
    static void choiceEvent(lv_event_t* event);
    static void nextEvent(lv_event_t* event);
    void clearContent();
    lv_obj_t* makeButton(const char* text, int y, uint32_t color);

    lv_obj_t* _root = nullptr;
    lv_obj_t* _content = nullptr;
    lv_obj_t* _volume = nullptr;
    lv_obj_t* _play = nullptr;
    lv_obj_t* _choices[3] = {};
};

}  // namespace view
