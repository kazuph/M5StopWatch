/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"
#include <mooncake_log.h>
#include <bmi270_bmm150.h>
#include "utils/settings/settings.h"

#ifndef BMI2_I2C_PRIM_ADDR
#define BMI2_I2C_PRIM_ADDR 0x68
#endif

static const std::string_view _tag        = "HAL-IMU";
static bmi270_bmm150_handle_t _imu_sensor = NULL;

void Hal::imu_init()
{
    mclog::tagInfo(_tag, "init");

    bmi270_bmm150_config_t sensor_conf = {
        .i2c_addr        = BMI2_I2C_PRIM_ADDR,
        .config_file_ptr = NULL,
        // .mode            = BOSCH_ACCEL_AND_MAGN,
        .mode = BOSCH_ACCELEROMETER_ONLY,
    };

    if (bmi270_bmm150_sensor_create(_i2c_bus, &_imu_sensor, &sensor_conf) != ESP_OK) {
        mclog::tagError(_tag, "init failed");
        _imu_sensor = NULL;
        return;
    }

    uint8_t step_counter = BMI2_STEP_COUNTER;
    if (bmi270_sensor_enable(&step_counter, 1, &_imu_sensor->bmi2) != BMI2_OK) {
        mclog::tagError(_tag, "step counter enable failed");
    }
}

namespace {

constexpr uint32_t _pedometer_poll_period_ms = 1000;
constexpr const char* _pedometer_namespace = "pedometer";
constexpr const char* _pedometer_storage_key = "daily";

int current_date_key()
{
    const auto date = GetHAL().getDateYmd();
    return static_cast<int>(date.year) * 10000 + static_cast<int>(date.month) * 100 + date.day;
}

}  // namespace

void Hal::pedometer_init()
{
    const int today = current_date_key();
    PedometerStorageSnapshot stored;
    size_t stored_size = sizeof(stored);
    Settings settings(_pedometer_namespace, false);
    const bool loaded = settings.GetBlob(_pedometer_storage_key, &stored, &stored_size) == ESP_OK &&
                        stored_size == sizeof(stored) && stored.isValid() && stored.dateKey == today;

    _pedometer_data = loaded ? stored : PedometerStorageSnapshot{};
    _pedometer_data.dateKey = today;
    _pedometer_last_hour = GetHAL().getTimeHms().hour;
    _pedometer_dirty = !loaded;
    mclog::tagInfo(_tag, "pedometer initialized, loaded: {}, total: {}", loaded, _pedometer_data.totalSteps);
}

void Hal::updatePedometer()
{
    const uint32_t now = millis();
    if (_imu_sensor == nullptr || now - _pedometer_last_poll_ms < _pedometer_poll_period_ms) {
        return;
    }
    _pedometer_last_poll_ms = now;

    const int today = current_date_key();
    const int hour = getTimeHms().hour;
    if (_pedometer_data.dateKey != today) {
        flushPedometerData();
        _pedometer_data = PedometerStorageSnapshot{};
        _pedometer_data.dateKey = today;
        _pedometer_dirty = true;
    } else if (_pedometer_last_hour >= 0 && _pedometer_last_hour != hour) {
        flushPedometerData();
    }
    _pedometer_last_hour = hour;

    bmi2_feat_sensor_data sensor_data = {};
    sensor_data.type = BMI2_STEP_COUNTER;
    if (bmi270_get_feature_data(&sensor_data, 1, &_imu_sensor->bmi2) != BMI2_OK) {
        return;
    }

    const uint32_t raw = sensor_data.sens_data.step_counter_output;
    if (!_pedometer_raw_initialized) {
        _pedometer_last_raw = raw;
        _pedometer_raw_initialized = true;
        return;
    }

    const uint32_t delta = raw >= _pedometer_last_raw ? raw - _pedometer_last_raw : raw;
    _pedometer_last_raw = raw;
    if (delta == 0 || hour < 0 || hour >= static_cast<int>(_pedometer_data.hourlySteps.size())) {
        return;
    }

    _pedometer_data.totalSteps += delta;
    _pedometer_data.hourlySteps[hour] += delta;
    _pedometer_dirty = true;
}

bool Hal::flushPedometerData()
{
    if (!_pedometer_dirty || !_pedometer_data.isValid()) {
        return true;
    }
    Settings settings(_pedometer_namespace, true);
    const bool ok = settings.SetBlob(_pedometer_storage_key, &_pedometer_data, sizeof(_pedometer_data)) == ESP_OK;
    if (ok) {
        _pedometer_dirty = false;
    }
    return ok;
}

void Hal::updateImuData()
{
    if (_imu_sensor != NULL) {
        int available = 0;
        if (bmi270_bmm150_sensor_acceleration_available(_imu_sensor, &available) == ESP_OK && available > 0) {
            bmi270_bmm150_sensor_read_acceleration(_imu_sensor, &_imu_data.accelY, &_imu_data.accelX,
                                                   &_imu_data.accelZ);
        }
        if (bmi270_bmm150_sensor_gyroscope_available(_imu_sensor, &available) == ESP_OK && available > 0) {
            bmi270_bmm150_sensor_read_gyroscope(_imu_sensor, &_imu_data.gyroY, &_imu_data.gyroX, &_imu_data.gyroZ);
        }
    } else {
        mclog::tagError(_tag, "imu invalid");
    }
}
