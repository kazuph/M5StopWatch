/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_pomodoro.h"
#include <assets/assets.h>
#include <hal/hal.h>
#include <mooncake_log.h>

namespace {

// Reuse the established alarm feedback profile instead of introducing a new notification strength.
constexpr uint16_t _phase_change_vibration_duration_ms = 70;
constexpr uint8_t _phase_change_vibration_strength = 90;

}  // namespace

AppPomodoro::AppPomodoro()
{
    setAppInfo().name = "Pomodoro";
    setAppInfo().icon = (void*)&icon_clock;
}

void AppPomodoro::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
}

void AppPomodoro::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");
    _key_manager = std::make_unique<input::KeyManager>();

    LvglLockGuard lock;
    _view = std::make_unique<view::PomodoroView>();
    _view->init(lv_screen_active());
    _view->onToggle = [this]() { _toggle_requested.store(true); };
    _view->onReset = [this]() { _reset_requested.store(true); };
    syncView();
}

void AppPomodoro::onRunning()
{
    if (_key_manager && _key_manager->update() == input::KeyEvent::GoHome) {
        close();
        return;
    }

    if (_reset_requested.exchange(false)) {
        _timer.reset();
    }
    if (_toggle_requested.exchange(false)) {
        _timer.toggle(GetHAL().millis());
    }
    updateTimer();

    LvglLockGuard lock;
    syncView();
}

void AppPomodoro::onSleeping()
{
    updateTimer();
}

void AppPomodoro::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");
    _key_manager.reset();
    _toggle_requested.store(false);
    _reset_requested.store(false);

    LvglLockGuard lock;
    _view.reset();
}

void AppPomodoro::updateTimer()
{
    if (_timer.update(GetHAL().millis())) {
        GetHAL().vibrate(_phase_change_vibration_duration_ms, _phase_change_vibration_strength);
    }
}

void AppPomodoro::syncView()
{
    if (!_view) {
        return;
    }
    _view->setPhase(_timer.phase() == model::Pomodoro::Phase::Focus);
    _view->setRemainingSeconds(_timer.remainingSeconds());
    _view->setRunning(_timer.state() == model::Pomodoro::State::Running);
}
