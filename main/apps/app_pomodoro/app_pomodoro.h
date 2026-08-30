/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "model/pomodoro.h"
#include "view/view.h"
#include <apps/common/key_manager/key_manager.h>
#include <atomic>
#include <memory>
#include <mooncake.h>

class AppPomodoro : public mooncake::AppAbility {
public:
    AppPomodoro();

    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onSleeping() override;
    void onClose() override;

private:
    model::Pomodoro _timer;
    std::unique_ptr<input::KeyManager> _key_manager;
    std::unique_ptr<view::PomodoroView> _view;
    std::atomic<bool> _toggle_requested = false;
    std::atomic<bool> _reset_requested = false;

    void updateTimer();
    void syncView();
};
