/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_pedometer.h"
#include <assets/assets.h>
#include <hal/hal.h>
#include <mooncake_log.h>

AppPedometer::AppPedometer()
{
    setAppInfo().name = "Pedometer";
    setAppInfo().icon = (void*)&icon_imu;
}

void AppPedometer::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
}

void AppPedometer::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");
    _key_manager = std::make_unique<input::KeyManager>();

    LvglLockGuard lock;
    _view = std::make_unique<view::PedometerView>();
    _view->init(lv_screen_active());
    const auto& data = GetHAL().getPedometerData();
    _view->setStepData(data.totalSteps, data.hourlySteps);
}

void AppPedometer::onRunning()
{
    if (_key_manager && _key_manager->update() == input::KeyEvent::GoHome) {
        close();
        return;
    }

    LvglLockGuard lock;
    if (_view) {
        const auto& data = GetHAL().getPedometerData();
        _view->setStepData(data.totalSteps, data.hourlySteps);
        _view->update();
    }
}

void AppPedometer::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");
    _key_manager.reset();
    GetHAL().flushPedometerData();

    LvglLockGuard lock;
    _view.reset();
}
