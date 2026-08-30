#include "app_tuner.h"

#include <assets/assets.h>
#include <algorithm>
#include <hal/hal.h>
#include <hal/utils/settings/settings.h>
#include <mooncake_log.h>
#include <smooth_lvgl.hpp>

namespace {
constexpr char settingsNamespace[] = "tuner";
constexpr char calibrationKey[] = "a4_hz";
}

AppTuner::AppTuner()
{
    setAppInfo().name = "Tuner";
    setAppInfo().icon = (void*)&icon_fft;
}

void AppTuner::onCreate()
{
    if (xTaskCreate(captureTaskEntry, "tuner_capture", 4 * 1024, this, 3, &_captureTask) != pdPASS) {
        _captureTask = nullptr;
        mclog::tagError(getAppInfo().name, "failed to create capture task");
    }
}

void AppTuner::onOpen()
{
    Settings settings(settingsNamespace, false);
    setCalibration(settings.GetInt(calibrationKey, tuner::calibrationDefaultHz));
    {
        std::lock_guard<std::mutex> lock(_pitchMutex);
        _latestPitch = {};
    }
    _keyManager = std::make_unique<input::KeyManager>();
    {
        LvglLockGuard lock;
        _view = std::make_unique<view::TunerView>();
        _view->init(lv_screen_active());
        _view->setCalibration(_calibrationHz.load());
        _view->onCalibrationChange = [this](int hz) { setCalibration(hz); };
    }
    _active.store(true);
    if (_captureTask != nullptr) {
        xTaskNotifyGive(_captureTask);
    }
}

void AppTuner::onRunning()
{
    if (_keyManager && _keyManager->update() == input::KeyEvent::GoHome) {
        close();
        return;
    }
    tuner::PitchResult pitch;
    {
        std::lock_guard<std::mutex> lock(_pitchMutex);
        pitch = _latestPitch;
    }
    LvglLockGuard lock;
    if (_view) {
        _view->setPitch(pitch);
    }
}

void AppTuner::onClose()
{
    _active.store(false);
    {
        Settings settings(settingsNamespace, true);
        settings.SetInt(calibrationKey, _calibrationHz.load());
    }
    _keyManager.reset();
    LvglLockGuard lock;
    _view.reset();
}

void AppTuner::captureTaskEntry(void* context)
{
    static_cast<AppTuner*>(context)->captureTask();
}

void AppTuner::captureTask()
{
    constexpr uint16_t frameDurationMs = tuner::PitchDetector::frameSize * 1000 / tuner::PitchDetector::sampleRate;
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        while (_active.load()) {
            std::vector<int16_t> samples;
            GetHAL().audioRecord(samples, frameDurationMs);
            const auto pitch = _detector.analyze(samples, static_cast<float>(_calibrationHz.load()));
            std::lock_guard<std::mutex> lock(_pitchMutex);
            _latestPitch = pitch;
        }
        GetHAL().setSpeakerEnabled(true);
    }
}

void AppTuner::setCalibration(int hz)
{
    const int bounded = std::clamp(hz, tuner::calibrationMinHz, tuner::calibrationMaxHz);
    _calibrationHz.store(bounded);
    if (_view) {
        _view->setCalibration(bounded);
    }
}
