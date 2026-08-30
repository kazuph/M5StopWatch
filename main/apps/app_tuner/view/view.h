#pragma once

#include "../model/pitch_detector.h"
#include <functional>
#include <lvgl.h>

namespace view {

class TunerView {
public:
    ~TunerView();
    void init(lv_obj_t* parent);
    void setPitch(const tuner::PitchResult& pitch);
    void setCalibration(int hz);

    std::function<void(int)> onCalibrationChange;

private:
    static void calibrationEvent(lv_event_t* event);
    lv_obj_t* _root = nullptr;
    lv_obj_t* _note = nullptr;
    lv_obj_t* _frequency = nullptr;
    lv_obj_t* _cents = nullptr;
    lv_obj_t* _meter = nullptr;
    lv_obj_t* _calibration = nullptr;
    lv_obj_t* _minus = nullptr;
    lv_obj_t* _plus = nullptr;
    int _calibrationHz = tuner::calibrationDefaultHz;
};

}  // namespace view
