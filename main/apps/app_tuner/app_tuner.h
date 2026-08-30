#pragma once

#include "model/pitch_detector.h"
#include "view/view.h"
#include <apps/common/key_manager/key_manager.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <memory>
#include <mooncake.h>
#include <mutex>

class AppTuner : public mooncake::AppAbility {
public:
    AppTuner();
    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    static void captureTaskEntry(void* context);
    void captureTask();
    void setCalibration(int hz);

    std::unique_ptr<input::KeyManager> _keyManager;
    std::unique_ptr<view::TunerView> _view;
    tuner::PitchDetector _detector;
    tuner::PitchResult _latestPitch;
    std::mutex _pitchMutex;
    std::atomic<bool> _active{false};
    std::atomic<int> _calibrationHz{tuner::calibrationDefaultHz};
    TaskHandle_t _captureTask = nullptr;
};
