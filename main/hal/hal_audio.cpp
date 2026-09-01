/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"
#include "utils/settings/settings.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <mooncake_log.h>
#include <driver/i2s_std.h>
#include <esp_dsp.h>
#include <esp_codec_dev.h>
#include <esp_codec_dev_defaults.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mutex>

static const std::string_view _tag = "HAL-Audio";

#define I2S_MCLK_PIN     (gpio_num_t)18
#define I2S_BCLK_PIN     (gpio_num_t)17
#define I2S_DADC_IN_PIN  (gpio_num_t)16
#define I2S_LRCK_PIN     (gpio_num_t)15
#define I2S_DDAC_OUT_PIN (gpio_num_t)21

struct AudioInputModeConfig {
    i2s_port_t port;
    i2s_std_slot_mask_t slot;
    bool rawRead;
    const char* name;
};

constexpr std::array<AudioInputModeConfig, static_cast<std::size_t>(Hal::AudioInputMode::Count)> _input_modes = {{
    {I2S_NUM_1, I2S_STD_SLOT_LEFT, false, "I2S1 LEFT"},
    {I2S_NUM_1, I2S_STD_SLOT_RIGHT, false, "I2S1 RIGHT"},
    {I2S_NUM_1, I2S_STD_SLOT_LEFT, true, "RAW1 LEFT"},
    {I2S_NUM_1, I2S_STD_SLOT_RIGHT, true, "RAW1 RIGHT"},
    {I2S_NUM_0, I2S_STD_SLOT_LEFT, true, "RAW0 LEFT"},
    {I2S_NUM_0, I2S_STD_SLOT_RIGHT, true, "RAW0 RIGHT"},
}};

static class AudioCodec {
public:
    static constexpr int playback_sample_rate = 44100;
    static constexpr int record_sample_rate   = 16000;
    static constexpr int spectrum_fft_size    = 512;
    static constexpr int spectrum_hop_size    = 256;

    void init(i2c_master_bus_handle_t i2c_bus)
    {
        _silence_buffer.resize(playback_sample_rate / 10);
        _silence_buffer.assign(_silence_buffer.size(), 0);
        _spectrum_init();
        const BaseType_t task_result =
            xTaskCreate([](void* obj) { static_cast<AudioCodec*>(obj)->_task_entry(); }, "audio_task", 4 * 1024, this, 5,
                        &_task_handle);
        if (task_result != pdPASS) {
            _task_handle = nullptr;
            mclog::tagError(_tag, "failed to create audio play task: {}", task_result);
        }

        audio_codec_i2c_cfg_t i2c_cfg = {};
        i2c_cfg.addr                  = ES8311_CODEC_DEFAULT_ADDR;
        i2c_cfg.bus_handle            = i2c_bus;
        _input_ctrl_if                = audio_codec_new_i2c_ctrl(&i2c_cfg);
        _output_ctrl_if               = audio_codec_new_i2c_ctrl(&i2c_cfg);
        _input_gpio_if                = audio_codec_new_gpio();
        _output_gpio_if               = audio_codec_new_gpio();

        es8311_codec_cfg_t input_codec_cfg = {};
        input_codec_cfg.ctrl_if            = _input_ctrl_if;
        input_codec_cfg.gpio_if            = _input_gpio_if;
        input_codec_cfg.codec_mode         = ESP_CODEC_DEV_WORK_MODE_ADC;
        input_codec_cfg.pa_pin             = GPIO_NUM_NC;
        input_codec_cfg.use_mclk           = true;
        _input_codec_if                    = es8311_codec_new(&input_codec_cfg);

        es8311_codec_cfg_t output_codec_cfg        = {};
        output_codec_cfg.ctrl_if                   = _output_ctrl_if;
        output_codec_cfg.gpio_if                   = _output_gpio_if;
        output_codec_cfg.codec_mode                = ESP_CODEC_DEV_WORK_MODE_DAC;
        output_codec_cfg.pa_pin                    = GPIO_NUM_NC;
        output_codec_cfg.use_mclk                  = true;
        output_codec_cfg.hw_gain.pa_voltage        = 5.0f;
        output_codec_cfg.hw_gain.codec_dac_voltage = 3.3f;
        _output_codec_if                           = es8311_codec_new(&output_codec_cfg);

        audio_codec_i2c_cfg_t duplex_i2c_cfg       = {};
        duplex_i2c_cfg.addr                        = ES8311_CODEC_DEFAULT_ADDR;
        duplex_i2c_cfg.bus_handle                  = i2c_bus;
        _duplex_ctrl_if                            = audio_codec_new_i2c_ctrl(&duplex_i2c_cfg);
        _duplex_gpio_if                            = audio_codec_new_gpio();
        es8311_codec_cfg_t duplex_codec_cfg        = {};
        duplex_codec_cfg.ctrl_if                   = _duplex_ctrl_if;
        duplex_codec_cfg.gpio_if                   = _duplex_gpio_if;
        duplex_codec_cfg.codec_mode                = ESP_CODEC_DEV_WORK_MODE_BOTH;
        duplex_codec_cfg.pa_pin                    = GPIO_NUM_NC;
        duplex_codec_cfg.use_mclk                  = true;
        duplex_codec_cfg.hw_gain.pa_voltage        = 5.0f;
        duplex_codec_cfg.hw_gain.codec_dac_voltage = 3.3f;
        _duplex_codec_if                           = es8311_codec_new(&duplex_codec_cfg);

        _select_mode_locked(Mode::Playback);
    }

    void updateSpectrum(Hal::AudioSpectrumFrame& frame)
    {
        if (_spectrum_available == false) {
            return;
        }

        if (_read_spectrum_hop() == false) {
            return;
        }

        if (_spectrum_samples_ready < spectrum_fft_size) {
            return;
        }

        _process_spectrum_frame(frame);
    }

    void setVolume(int volume)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _volume = volume;
        if (_output_dev != nullptr) {
            esp_codec_dev_set_out_vol(_output_dev, volume);
        }
        if (_duplex_dev != nullptr) {
            esp_codec_dev_set_out_vol(_duplex_dev, volume);
        }
    }

    int getVolume()
    {
        std::lock_guard<std::mutex> lock(_mutex);
        return _volume;
    }

    void setMicGain(float gain)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _mic_gain = gain;
        if (_input_dev != nullptr) {
            esp_codec_dev_set_in_gain(_input_dev, gain);
        }
        if (_duplex_dev != nullptr) {
            esp_codec_dev_set_in_gain(_duplex_dev, gain);
        }
    }

    void setInputMode(Hal::AudioInputMode mode)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (mode == Hal::AudioInputMode::Count || mode == _input_mode) {
            return;
        }
        _input_mode = mode;
        if (_mode == Mode::Record) {
            _destroy_active_path_locked();
        }
        mclog::tagInfo(_tag, "audio input mode: {}", inputModeName());
    }

    Hal::AudioInputMode inputMode() const
    {
        return _input_mode;
    }

    const char* inputModeName() const
    {
        return _input_modes[static_cast<std::size_t>(_input_mode)].name;
    }

    void play(std::vector<int16_t>& data, bool async)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (!_select_mode_locked(Mode::Playback)) {
            return;
        }
        if (async) {
            if (_task_handle == nullptr) {
                mclog::tagError(_tag, "async playback unavailable: audio play task is not running");
                return;
            }
            // Support interruption: overwrite data and notify task
            _audio_data = data;
            _is_playing = true;
            mclog::tagInfo(_tag, "async playback queued: samples={}", data.size());
            xTaskNotifyGive(_task_handle);
        } else {
            if (_is_playing) {
                mclog::tagWarn(_tag, "audio is playing");
                return;
            }
            _write(data);
        }
    }

    void record(std::vector<int16_t>& data, uint16_t durationMs, float gain)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (!_select_mode_locked(Mode::Record)) {
            data.clear();
            return;
        }

        _mic_gain = gain;
        esp_codec_dev_set_in_gain(_input_dev, gain);

        size_t sample_count = (size_t)(record_sample_rate * durationMs / 1000);
        size_t byte_size    = sample_count * sizeof(int16_t);

        data.resize(sample_count);

        esp_err_t ret = _read_input_locked(data.data(), byte_size);
        if (ret != ESP_OK) {
            mclog::tagError(_tag, "record failed: {}", ret);
            data.clear();
        }
    }

    void streamWrite(const std::vector<int16_t>& data)
    {
        if (data.empty()) {
            return;
        }
        std::lock_guard<std::mutex> lock(_mutex);
        const int result = _write_mono_locked(data);
        if (result == ESP_CODEC_DEV_OK) {
            _stream_written_frames.fetch_add(1);
        } else {
            _stream_failed_frames.fetch_add(1);
            _stream_last_error.store(result);
        }
    }

    bool startDuplex()
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_duplex_active.load()) {
            return true;
        }
        if (!_select_mode_locked(Mode::Duplex)) {
            return false;
        }
        _duplex_active.store(true);
        return true;
    }

    void stopDuplex()
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_mode == Mode::Duplex) {
            _select_mode_locked(Mode::Playback);
        }
    }

    void duplexRecord(std::vector<int16_t>& data, uint16_t durationMs, float gain)
    {
        (void)gain;
        std::lock_guard<std::mutex> lock(_input_io_mutex);
        if (!_duplex_active.load() || _duplex_dev == nullptr) {
            data.clear();
            return;
        }

        const std::size_t sample_count = static_cast<std::size_t>(record_sample_rate) * durationMs / 1000;
        _duplex_input_buffer.resize(sample_count * 2);
        const int result =
            esp_codec_dev_read(_duplex_dev, _duplex_input_buffer.data(), _duplex_input_buffer.size() * sizeof(int16_t));
        if (result != ESP_CODEC_DEV_OK) {
            data.clear();
            return;
        }
        data.resize(sample_count);
        for (std::size_t i = 0; i < sample_count; ++i) {
            data[i] = _duplex_input_buffer[i * 2];
        }
    }

    void duplexStreamWrite(const std::vector<int16_t>& data)
    {
        if (data.empty()) {
            return;
        }
        std::lock_guard<std::mutex> lock(_output_io_mutex);
        if (!_duplex_active.load() || _duplex_dev == nullptr) {
            return;
        }
        _duplex_output_buffer.resize(data.size() * 2);
        for (std::size_t i = 0; i < data.size(); ++i) {
            _duplex_output_buffer[i * 2]     = data[i];
            _duplex_output_buffer[i * 2 + 1] = data[i];
        }
        const int result = esp_codec_dev_write(_duplex_dev, _duplex_output_buffer.data(),
                                               _duplex_output_buffer.size() * sizeof(int16_t));
        if (result == ESP_CODEC_DEV_OK) {
            _stream_written_frames.fetch_add(1);
        } else {
            _stream_failed_frames.fetch_add(1);
            _stream_last_error.store(result);
        }
    }

    Hal::AudioStreamStats streamStats() const
    {
        return {
            .writtenFrames = _stream_written_frames.load(),
            .failedFrames  = _stream_failed_frames.load(),
            .lastError     = _stream_last_error.load(),
        };
    }

    bool preparePlayback()
    {
        std::lock_guard<std::mutex> lock(_mutex);
        return _select_mode_locked(Mode::Playback);
    }

    std::array<uint8_t, Hal::AudioSpectrumFrame::bandCount> analyzePacket(const std::vector<int16_t>& data)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        std::array<uint8_t, Hal::AudioSpectrumFrame::bandCount> quantized = {};
        if (data.empty()) {
            return quantized;
        }

        _spectrum_time_domain.fill(0.0f);
        const std::size_t sample_count = std::min<std::size_t>(data.size(), spectrum_fft_size);
        const std::size_t offset       = spectrum_fft_size - sample_count;
        for (std::size_t i = 0; i < sample_count; ++i) {
            _spectrum_time_domain[offset + i] = static_cast<float>(data[data.size() - sample_count + i]) / 32768.0f;
        }

        Hal::AudioSpectrumFrame frame;
        _process_spectrum_frame(frame);
        for (std::size_t i = 0; i < frame.bandCount; ++i) {
            quantized[i] = static_cast<uint8_t>(std::lround(std::clamp(frame.bands[i], 0.0f, 1.0f) * 255.0f));
        }
        return quantized;
    }

private:
    enum class Mode {
        None,
        Record,
        Playback,
        Duplex,
    };

    bool _select_mode_locked(Mode target)
    {
        if (_mode == target) {
            return true;
        }

        std::unique_lock<std::mutex> input_lock(_input_io_mutex, std::defer_lock);
        std::unique_lock<std::mutex> output_lock(_output_io_mutex, std::defer_lock);
        if (_duplex_active.exchange(false)) {
            std::lock(input_lock, output_lock);
        }
        _destroy_active_path_locked();

        int result = ESP_CODEC_DEV_OK;
        if (target == Mode::Playback) {
            if (!_create_playback_path_locked()) {
                return false;
            }
            result = esp_codec_dev_open(_output_dev, &_output_format);
            if (result == ESP_CODEC_DEV_OK) {
                esp_codec_dev_set_out_vol(_output_dev, _volume);
            }
        } else if (target == Mode::Record) {
            if (!_create_record_path_locked()) {
                return false;
            }
            result = esp_codec_dev_open(_input_dev, &_input_format);
            if (result == ESP_CODEC_DEV_OK) {
                esp_codec_dev_set_in_gain(_input_dev, _mic_gain);
            }
        } else if (target == Mode::Duplex) {
            if (!_create_duplex_path_locked()) {
                return false;
            }
            result = esp_codec_dev_open(_duplex_dev, &_duplex_format);
            if (result == ESP_CODEC_DEV_OK) {
                esp_codec_dev_set_out_vol(_duplex_dev, _volume);
                esp_codec_dev_set_in_gain(_duplex_dev, _mic_gain);
            }
        }

        if (result != ESP_CODEC_DEV_OK) {
            mclog::tagError(_tag, "audio mode switch failed: target={}, error={}", static_cast<int>(target), result);
            return false;
        }
        _mode = target;
        if (target == Mode::Playback) {
            mclog::tagInfo(_tag, "audio path: playback, i2s=0, rate={}, channels=stereo", playback_sample_rate);
        } else if (target == Mode::Record) {
            mclog::tagInfo(_tag, "audio path: record, i2s=1, rate={}, channel=right", record_sample_rate);
        } else if (target == Mode::Duplex) {
            mclog::tagInfo(_tag, "audio path: duplex, i2s=0, rate={}, channels=stereo", record_sample_rate);
        }
        return true;
    }

    void _destroy_active_path_locked()
    {
        if (_duplex_dev != nullptr) {
            esp_codec_dev_close(_duplex_dev);
            esp_codec_dev_delete(_duplex_dev);
            _duplex_dev = nullptr;
        }
        if (_output_dev != nullptr) {
            esp_codec_dev_close(_output_dev);
            esp_codec_dev_delete(_output_dev);
            _output_dev = nullptr;
        }
        if (_input_dev != nullptr) {
            esp_codec_dev_close(_input_dev);
            esp_codec_dev_delete(_input_dev);
            _input_dev = nullptr;
        }
        if (_output_data_if != nullptr) {
            audio_codec_delete_data_if(_output_data_if);
            _output_data_if = nullptr;
        }
        if (_input_data_if != nullptr) {
            audio_codec_delete_data_if(_input_data_if);
            _input_data_if = nullptr;
        }
        if (_duplex_data_if != nullptr) {
            audio_codec_delete_data_if(_duplex_data_if);
            _duplex_data_if = nullptr;
        }
        if (_tx_handle != nullptr) {
            i2s_del_channel(_tx_handle);
            _tx_handle = nullptr;
        }
        if (_rx_handle != nullptr) {
            i2s_del_channel(_rx_handle);
            _rx_handle = nullptr;
        }
        _mode = Mode::None;
    }

    bool _create_duplex_path_locked()
    {
        i2s_chan_config_t channel_config = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
        channel_config.auto_clear        = true;
        if (i2s_new_channel(&channel_config, &_tx_handle, &_rx_handle) != ESP_OK) {
            return false;
        }

        i2s_std_config_t config = {};
        config.clk_cfg          = I2S_STD_CLK_DEFAULT_CONFIG(record_sample_rate);
        config.slot_cfg         = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
        config.gpio_cfg.mclk    = I2S_MCLK_PIN;
        config.gpio_cfg.bclk    = I2S_BCLK_PIN;
        config.gpio_cfg.ws      = I2S_LRCK_PIN;
        config.gpio_cfg.dout    = I2S_DDAC_OUT_PIN;
        config.gpio_cfg.din     = I2S_DADC_IN_PIN;
        if (i2s_channel_init_std_mode(_tx_handle, &config) != ESP_OK ||
            i2s_channel_init_std_mode(_rx_handle, &config) != ESP_OK) {
            i2s_del_channel(_tx_handle);
            i2s_del_channel(_rx_handle);
            _tx_handle = nullptr;
            _rx_handle = nullptr;
            return false;
        }

        audio_codec_i2s_cfg_t data_config = {};
        data_config.port                  = I2S_NUM_0;
        data_config.tx_handle             = _tx_handle;
        data_config.rx_handle             = _rx_handle;
        _duplex_data_if                   = audio_codec_new_i2s_data(&data_config);
        esp_codec_dev_cfg_t device_config = {
            .dev_type = ESP_CODEC_DEV_TYPE_IN_OUT,
            .codec_if = _duplex_codec_if,
            .data_if  = _duplex_data_if,
        };
        _duplex_dev = esp_codec_dev_new(&device_config);
        return _duplex_data_if != nullptr && _duplex_dev != nullptr;
    }

    bool _create_playback_path_locked()
    {
        i2s_chan_config_t channel_config = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
        channel_config.auto_clear        = true;
        if (i2s_new_channel(&channel_config, &_tx_handle, nullptr) != ESP_OK) {
            return false;
        }

        i2s_std_config_t config = {};
        config.clk_cfg          = I2S_STD_CLK_DEFAULT_CONFIG(playback_sample_rate);
        config.slot_cfg         = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
        config.gpio_cfg.mclk    = I2S_MCLK_PIN;
        config.gpio_cfg.bclk    = I2S_BCLK_PIN;
        config.gpio_cfg.ws      = I2S_LRCK_PIN;
        config.gpio_cfg.dout    = I2S_DDAC_OUT_PIN;
        config.gpio_cfg.din     = GPIO_NUM_NC;
        if (i2s_channel_init_std_mode(_tx_handle, &config) != ESP_OK) {
            i2s_del_channel(_tx_handle);
            _tx_handle = nullptr;
            return false;
        }

        audio_codec_i2s_cfg_t data_config = {};
        data_config.port                  = I2S_NUM_0;
        data_config.tx_handle             = _tx_handle;
        _output_data_if                   = audio_codec_new_i2s_data(&data_config);
        esp_codec_dev_cfg_t device_config = {
            .dev_type = ESP_CODEC_DEV_TYPE_OUT,
            .codec_if = _output_codec_if,
            .data_if  = _output_data_if,
        };
        _output_dev = esp_codec_dev_new(&device_config);
        return _output_data_if != nullptr && _output_dev != nullptr;
    }

    bool _create_record_path_locked()
    {
        const auto& input_config         = _input_modes[static_cast<std::size_t>(_input_mode)];
        i2s_chan_config_t channel_config = I2S_CHANNEL_DEFAULT_CONFIG(input_config.port, I2S_ROLE_MASTER);
        if (i2s_new_channel(&channel_config, nullptr, &_rx_handle) != ESP_OK) {
            return false;
        }

        i2s_std_config_t config   = {};
        config.clk_cfg            = I2S_STD_CLK_DEFAULT_CONFIG(record_sample_rate);
        config.slot_cfg           = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
        config.slot_cfg.slot_mask = input_config.slot;
        config.gpio_cfg.mclk      = I2S_MCLK_PIN;
        config.gpio_cfg.bclk      = I2S_BCLK_PIN;
        config.gpio_cfg.ws        = I2S_LRCK_PIN;
        config.gpio_cfg.dout      = GPIO_NUM_NC;
        config.gpio_cfg.din       = I2S_DADC_IN_PIN;
        if (i2s_channel_init_std_mode(_rx_handle, &config) != ESP_OK) {
            i2s_del_channel(_rx_handle);
            _rx_handle = nullptr;
            return false;
        }

        audio_codec_i2s_cfg_t data_config = {};
        data_config.port                  = input_config.port;
        data_config.rx_handle             = _rx_handle;
        _input_data_if                    = audio_codec_new_i2s_data(&data_config);
        esp_codec_dev_cfg_t device_config = {
            .dev_type = ESP_CODEC_DEV_TYPE_IN,
            .codec_if = _input_codec_if,
            .data_if  = _input_data_if,
        };
        _input_dev                 = esp_codec_dev_new(&device_config);
        _input_format.channel_mask = input_config.slot == I2S_STD_SLOT_LEFT ? ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0)
                                                                            : ESP_CODEC_DEV_MAKE_CHANNEL_MASK(1);
        return _input_data_if != nullptr && _input_dev != nullptr;
    }

    esp_err_t _read_input_locked(int16_t* data, std::size_t byteSize)
    {
        const auto& input_config = _input_modes[static_cast<std::size_t>(_input_mode)];
        if (!input_config.rawRead) {
            return esp_codec_dev_read(_input_dev, data, byteSize);
        }

        std::size_t bytes_read = 0;
        const esp_err_t result = i2s_channel_read(_rx_handle, data, byteSize, &bytes_read, portMAX_DELAY);
        return result == ESP_OK && bytes_read == byteSize ? ESP_OK : ESP_FAIL;
    }

    int _write_mono_locked(const std::vector<int16_t>& mono)
    {
        if (!_select_mode_locked(Mode::Playback)) {
            return ESP_CODEC_DEV_WRONG_STATE;
        }
        _stereo_buffer.resize(mono.size() * 2);
        for (std::size_t i = 0; i < mono.size(); ++i) {
            _stereo_buffer[i * 2]     = mono[i];
            _stereo_buffer[i * 2 + 1] = mono[i];
        }
        return esp_codec_dev_write(_output_dev, _stereo_buffer.data(), _stereo_buffer.size() * sizeof(int16_t));
    }

    void _task_entry()
    {
        mclog::tagInfo(_tag, "start audio play task");
        std::vector<int16_t> current_data;

        while (1) {
            // Wait for play request
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

            while (true) {
                {
                    std::lock_guard<std::mutex> lock(_mutex);
                    if (!_audio_data.empty()) {
                        current_data = std::move(_audio_data);
                        _audio_data.clear();
                    } else {
                        _is_playing = false;
                        break;
                    }
                    _is_playing = true;
                }

                if (current_data.empty()) {
                    break;
                }

                size_t offset        = 0;
                size_t total_samples = current_data.size();
                bool interrupted     = false;
                int write_result     = ESP_CODEC_DEV_OK;
                mclog::tagInfo(_tag, "async playback started: samples={}", total_samples);
                // Chunk size in samples (e.g. 1024 bytes = 512 samples)
                const size_t CHUNK_SAMPLES = 512;

                while (offset < total_samples) {
                    // Check for interruption (new play request)
                    if (ulTaskNotifyTake(pdTRUE, 0) > 0) {
                        // mclog::tagInfo(_tag, "playback interrupted");
                        interrupted = true;
                        break;
                    }

                    size_t remain        = total_samples - offset;
                    size_t write_samples = (remain > CHUNK_SAMPLES) ? CHUNK_SAMPLES : remain;

                    std::vector<int16_t> chunk(current_data.begin() + offset,
                                               current_data.begin() + offset + write_samples);
                    {
                        std::lock_guard<std::mutex> lock(_mutex);
                        write_result = _write_mono_locked(chunk);
                    }
                    if (write_result != ESP_CODEC_DEV_OK) {
                        mclog::tagError(_tag, "async playback write failed: result={}, offset={}, samples={}",
                                        write_result, offset, write_samples);
                        break;
                    }
                    offset += write_samples;
                }

                if (interrupted) {
                    // Stop current playback immediately and flush DMA
                    std::lock_guard<std::mutex> lock(_mutex);
                    if (_mode == Mode::Playback) {
                        i2s_channel_disable(_tx_handle);
                        i2s_channel_enable(_tx_handle);
                    }
                    continue;
                }

                if (write_result != ESP_CODEC_DEV_OK) {
                    std::lock_guard<std::mutex> lock(_mutex);
                    _is_playing = false;
                    break;
                }

                // Normal finish, play silence to avoid pop/waiting
                {
                    std::lock_guard<std::mutex> lock(_mutex);
                    write_result = _write_mono_locked(_silence_buffer);
                }
                if (write_result != ESP_CODEC_DEV_OK) {
                    mclog::tagError(_tag, "async playback trailing silence failed: result={}", write_result);
                } else {
                    mclog::tagInfo(_tag, "async playback completed: samples={}", total_samples);
                }
            }
        }
    }

    void _write(const std::vector<int16_t>& data)
    {
        _write_mono_locked(data);
        _write_mono_locked(_silence_buffer);
    }

    void _spectrum_init()
    {
        esp_err_t ret = dsps_fft2r_init_fc32(nullptr, spectrum_fft_size);
        if (ret != ESP_OK) {
            mclog::tagError(_tag, "fft init failed: {}", ret);
            return;
        }

        dsps_wind_hann_f32(_spectrum_window.data(), spectrum_fft_size);

        constexpr int max_bin = spectrum_fft_size / 2;
        const float nyquist   = static_cast<float>(record_sample_rate) * 0.5f;
        const float min_hz    = static_cast<float>(record_sample_rate) / static_cast<float>(spectrum_fft_size);
        const float log_min   = std::log10(min_hz);
        const float log_max   = std::log10(nyquist);

        _band_bin_edges[0] = 1;
        for (std::size_t i = 1; i < Hal::AudioSpectrumFrame::bandCount; ++i) {
            float t            = static_cast<float>(i) / static_cast<float>(Hal::AudioSpectrumFrame::bandCount);
            float edge_hz      = std::pow(10.0f, log_min + (log_max - log_min) * t);
            int edge_bin       = static_cast<int>(std::lround(edge_hz * spectrum_fft_size / record_sample_rate));
            int min_edge       = _band_bin_edges[i - 1] + 1;
            int max_edge       = max_bin - static_cast<int>(Hal::AudioSpectrumFrame::bandCount - i);
            _band_bin_edges[i] = std::clamp(edge_bin, min_edge, max_edge);
        }
        _band_bin_edges[Hal::AudioSpectrumFrame::bandCount] = max_bin;
        _spectrum_available                                 = true;
    }

    bool _read_spectrum_hop()
    {
        std::unique_lock<std::mutex> lock(_mutex, std::try_to_lock);
        if (lock.owns_lock() == false) {
            return false;
        }

        if (!_select_mode_locked(Mode::Record)) {
            return false;
        }
        esp_err_t ret = _read_input_locked(_spectrum_pcm_hop.data(), sizeof(int16_t) * spectrum_hop_size);
        if (ret != ESP_OK) {
            return false;
        }

        std::move(_spectrum_time_domain.begin() + spectrum_hop_size, _spectrum_time_domain.end(),
                  _spectrum_time_domain.begin());

        for (int i = 0; i < spectrum_hop_size; ++i) {
            _spectrum_time_domain[spectrum_fft_size - spectrum_hop_size + i] =
                static_cast<float>(_spectrum_pcm_hop[i]) / 32768.0f;
        }

        _spectrum_samples_ready = std::min<std::size_t>(_spectrum_samples_ready + spectrum_hop_size, spectrum_fft_size);
        return true;
    }

    void _process_spectrum_frame(Hal::AudioSpectrumFrame& frame)
    {
        float mean = 0.0f;
        for (float sample : _spectrum_time_domain) {
            mean += sample;
        }
        mean /= static_cast<float>(spectrum_fft_size);

        for (int i = 0; i < spectrum_fft_size; ++i) {
            float sample                    = (_spectrum_time_domain[i] - mean) * _spectrum_window[i];
            _spectrum_fft_buffer[i * 2]     = sample;
            _spectrum_fft_buffer[i * 2 + 1] = 0.0f;
        }

        if (dsps_fft2r_fc32(_spectrum_fft_buffer.data(), spectrum_fft_size) != ESP_OK) {
            return;
        }
        if (dsps_bit_rev_fc32(_spectrum_fft_buffer.data(), spectrum_fft_size) != ESP_OK) {
            return;
        }

        float peak_bin_magnitude = 0.0f;
        int peak_bin_index       = 0;
        float top1               = 0.0f;
        float top2               = 0.0f;
        float top3               = 0.0f;

        for (std::size_t band = 0; band < Hal::AudioSpectrumFrame::bandCount; ++band) {
            int start_bin = _band_bin_edges[band];
            int end_bin   = _band_bin_edges[band + 1];
            float energy  = 0.0f;
            float peak    = 0.0f;
            int count     = 0;

            for (int bin = start_bin; bin < end_bin; ++bin) {
                float re  = _spectrum_fft_buffer[bin * 2];
                float im  = _spectrum_fft_buffer[bin * 2 + 1];
                float mag = std::sqrt(re * re + im * im) * (2.0f / static_cast<float>(spectrum_fft_size));
                if (mag > peak_bin_magnitude) {
                    peak_bin_magnitude = mag;
                    peak_bin_index     = bin;
                }
                peak = std::max(peak, mag);
                energy += mag * mag;
                ++count;
            }

            float rms = count > 0 ? std::sqrt(energy / static_cast<float>(count)) : 0.0f;
            float raw = rms * 0.48f + peak * 0.52f;
            float low_emphasis =
                1.12f - 0.22f * (static_cast<float>(band) / static_cast<float>(Hal::AudioSpectrumFrame::bandCount - 1));
            raw *= low_emphasis;

            float floor_alpha = raw < _spectrum_noise_floor[band] ? 0.45f : 0.004f;
            _spectrum_noise_floor[band] += (raw - _spectrum_noise_floor[band]) * floor_alpha;
            raw                       = std::max(raw - (_spectrum_noise_floor[band] * 2.20f + 0.0018f), 0.0f);
            _spectrum_raw_bands[band] = raw;

            if (raw >= top1) {
                top3 = top2;
                top2 = top1;
                top1 = raw;
            } else if (raw >= top2) {
                top3 = top2;
                top2 = raw;
            } else if (raw > top3) {
                top3 = raw;
            }
        }

        float frame_reference = std::max(top1 * 0.80f + top2 * 0.14f + top3 * 0.06f, 0.0015f);
        float norm_alpha      = frame_reference > _spectrum_normalization_level ? 0.44f : 0.16f;
        _spectrum_normalization_level += (frame_reference - _spectrum_normalization_level) * norm_alpha;
        _spectrum_normalization_level = std::clamp(_spectrum_normalization_level, 0.0015f, 1.0f);

        if (peak_bin_magnitude > 0.0f) {
            float refined_bin = static_cast<float>(peak_bin_index);
            if (peak_bin_index > 1 && peak_bin_index < (spectrum_fft_size / 2 - 1)) {
                float left_re      = _spectrum_fft_buffer[(peak_bin_index - 1) * 2];
                float left_im      = _spectrum_fft_buffer[(peak_bin_index - 1) * 2 + 1];
                float right_re     = _spectrum_fft_buffer[(peak_bin_index + 1) * 2];
                float right_im     = _spectrum_fft_buffer[(peak_bin_index + 1) * 2 + 1];
                float center_power = peak_bin_magnitude * peak_bin_magnitude;
                float left_power   = left_re * left_re + left_im * left_im;
                float right_power  = right_re * right_re + right_im * right_im;
                float denom        = left_power - 2.0f * center_power + right_power;

                if (std::fabs(denom) > 1e-9f) {
                    float offset = 0.5f * (left_power - right_power) / denom;
                    refined_bin += std::clamp(offset, -0.5f, 0.5f);
                }
            }
            frame.peakFrequencyHz =
                refined_bin * static_cast<float>(record_sample_rate) / static_cast<float>(spectrum_fft_size);
        } else {
            frame.peakFrequencyHz = 0.0f;
        }

        for (std::size_t band = 0; band < Hal::AudioSpectrumFrame::bandCount; ++band) {
            float ratio      = _spectrum_raw_bands[band] / _spectrum_normalization_level;
            float normalized = std::clamp(std::pow(ratio, 0.55f), 0.0f, 1.0f);
            if (normalized < 0.035f) {
                normalized = 0.0f;
            }

            float smooth_alpha = normalized > _spectrum_smoothed_bands[band] ? 0.82f : 0.40f;
            _spectrum_smoothed_bands[band] += (normalized - _spectrum_smoothed_bands[band]) * smooth_alpha;
            frame.bands[band] = std::clamp(_spectrum_smoothed_bands[band], 0.0f, 1.0f);
        }
    }

    i2s_chan_handle_t _tx_handle = nullptr;
    i2s_chan_handle_t _rx_handle = nullptr;

    esp_codec_dev_sample_info_t _input_format = {
        .bits_per_sample = 16,
        .channel         = 2,
        .channel_mask    = ESP_CODEC_DEV_MAKE_CHANNEL_MASK(1),
        .sample_rate     = record_sample_rate,
        .mclk_multiple   = 0,
    };
    esp_codec_dev_sample_info_t _output_format = {
        .bits_per_sample = 16,
        .channel         = 2,
        .channel_mask    = 0,
        .sample_rate     = playback_sample_rate,
        .mclk_multiple   = 0,
    };
    esp_codec_dev_sample_info_t _duplex_format = {
        .bits_per_sample = 16,
        .channel         = 2,
        .channel_mask    = 0,
        .sample_rate     = record_sample_rate,
        .mclk_multiple   = 0,
    };
    esp_codec_dev_handle_t _input_dev            = NULL;
    esp_codec_dev_handle_t _output_dev           = NULL;
    esp_codec_dev_handle_t _duplex_dev           = NULL;
    const audio_codec_data_if_t* _input_data_if  = NULL;
    const audio_codec_data_if_t* _output_data_if = NULL;
    const audio_codec_data_if_t* _duplex_data_if = NULL;
    const audio_codec_ctrl_if_t* _input_ctrl_if  = NULL;
    const audio_codec_ctrl_if_t* _output_ctrl_if = NULL;
    const audio_codec_gpio_if_t* _input_gpio_if  = NULL;
    const audio_codec_gpio_if_t* _output_gpio_if = NULL;
    const audio_codec_if_t* _input_codec_if      = NULL;
    const audio_codec_if_t* _output_codec_if     = NULL;
    const audio_codec_ctrl_if_t* _duplex_ctrl_if = NULL;
    const audio_codec_gpio_if_t* _duplex_gpio_if = NULL;
    const audio_codec_if_t* _duplex_codec_if     = NULL;

    TaskHandle_t _task_handle = nullptr;
    std::mutex _mutex;
    std::mutex _input_io_mutex;
    std::mutex _output_io_mutex;
    std::vector<int16_t> _audio_data;
    std::vector<int16_t> _stereo_buffer;
    std::vector<int16_t> _duplex_input_buffer;
    std::vector<int16_t> _duplex_output_buffer;
    std::atomic<bool> _duplex_active             = false;
    std::atomic<uint32_t> _stream_written_frames = 0;
    std::atomic<uint32_t> _stream_failed_frames  = 0;
    std::atomic<int> _stream_last_error          = ESP_CODEC_DEV_OK;
    std::vector<int16_t> _silence_buffer;
    std::array<int16_t, spectrum_hop_size> _spectrum_pcm_hop                       = {};
    std::array<float, spectrum_fft_size> _spectrum_time_domain                     = {};
    std::array<float, spectrum_fft_size> _spectrum_window                          = {};
    std::array<float, spectrum_fft_size * 2> _spectrum_fft_buffer                  = {};
    std::array<float, Hal::AudioSpectrumFrame::bandCount> _spectrum_raw_bands      = {};
    std::array<float, Hal::AudioSpectrumFrame::bandCount> _spectrum_smoothed_bands = {};
    std::array<float, Hal::AudioSpectrumFrame::bandCount> _spectrum_noise_floor    = {};
    std::array<int, Hal::AudioSpectrumFrame::bandCount + 1> _band_bin_edges        = {};
    std::size_t _spectrum_samples_ready                                            = 0;
    float _spectrum_normalization_level                                            = 0.03f;
    bool _spectrum_available                                                       = false;
    bool _is_playing                                                               = false;
    Mode _mode                                                                     = Mode::None;
    int _volume                                                                    = 80;
    float _mic_gain                                                                = 30.0f;
    Hal::AudioInputMode _input_mode                                                = Hal::AudioInputMode::I2s1LeftCodec;
} _audio_codec;

void Hal::audio_init()
{
    mclog::tagInfo(_tag, "init");

    _audio_codec.init(i2c_bus_get_internal_bus_handle(_i2c_bus));

    ioe_speaker_enable(true);

    // Load volume from settings
    setSpeakerVolume(getSpeakerVolume(true), false);
}

void Hal::setSpeakerVolume(int volume, bool saveToSettings)
{
    _spk_volume = volume;
    _spk_volume = uitk::clamp(_spk_volume, 0, 100);

    mclog::tagInfo(_tag, "set speaker volume to {}", _spk_volume);
    _audio_codec.setVolume(_spk_volume);

    if (saveToSettings) {
        Settings settings(std::string(Hal::SettingsNs), true);
        settings.SetInt("spk_vol", _spk_volume);
        mclog::tagInfo(_tag, "volume saved to settings: {}", _spk_volume);
    }
}

int Hal::getSpeakerVolume(bool loadFromSettings)
{
    _spk_volume = _audio_codec.getVolume();

    if (loadFromSettings) {
        Settings settings(std::string(Hal::SettingsNs), false);
        _spk_volume = settings.GetInt("spk_vol", 80);
        _spk_volume = uitk::clamp(_spk_volume, 0, 100);
        mclog::tagInfo(_tag, "volume loaded from settings: {}", _spk_volume);
    }

    return _spk_volume;
}

void Hal::audioRecord(std::vector<int16_t>& data, uint16_t durationMs, float gain)
{
    ioe_speaker_enable(false);
    _audio_codec.record(data, durationMs, gain);
}

void Hal::audioPlay(std::vector<int16_t>& data, bool async)
{
    _audio_codec.play(data, async);
    ioe_speaker_enable(true);
}

void Hal::audioStreamWrite(const std::vector<int16_t>& data)
{
    _audio_codec.streamWrite(data);
}

bool Hal::audioDuplexStart()
{
    const bool started = _audio_codec.startDuplex();
    if (started) {
        ioe_speaker_enable(true);
    }
    return started;
}

void Hal::audioDuplexStop()
{
    _audio_codec.stopDuplex();
    ioe_speaker_enable(true);
}

void Hal::audioDuplexRecord(std::vector<int16_t>& data, uint16_t durationMs, float gain)
{
    _audio_codec.duplexRecord(data, durationMs, gain);
}

void Hal::audioDuplexStreamWrite(const std::vector<int16_t>& data)
{
    _audio_codec.duplexStreamWrite(data);
}

Hal::AudioStreamStats Hal::getAudioStreamStats()
{
    return _audio_codec.streamStats();
}

void Hal::setSpeakerEnabled(bool enabled)
{
    if (enabled) {
        if (_audio_codec.preparePlayback()) {
            ioe_speaker_enable(true);
        }
    } else {
        ioe_speaker_enable(false);
    }
}

void Hal::setAudioInputMode(AudioInputMode mode)
{
    _audio_codec.setInputMode(mode);
}

Hal::AudioInputMode Hal::getAudioInputMode() const
{
    return _audio_codec.inputMode();
}

const char* Hal::getAudioInputModeName() const
{
    return _audio_codec.inputModeName();
}

std::array<uint8_t, Hal::AudioSpectrumFrame::bandCount> Hal::audioAnalyzePacket(const std::vector<int16_t>& data)
{
    return _audio_codec.analyzePacket(data);
}

int Hal::getAudioSampleRate()
{
    return AudioCodec::playback_sample_rate;
}

void Hal::updateAudioSpectrum()
{
    ioe_speaker_enable(false);
    _audio_codec.updateSpectrum(_audio_spectrum);
}

namespace {

extern const uint8_t _boot_sfx_start[] asm("_binary_boot_sfx_bin_start");
extern const uint8_t _boot_sfx_end[] asm("_binary_boot_sfx_bin_end");

}  // namespace

void Hal::playBootSfx()
{
    mclog::tagInfo(_tag, "play boot sfx");

    const std::size_t byte_count = _boot_sfx_end - _boot_sfx_start;
    if (byte_count == 0 || (byte_count % sizeof(int16_t)) != 0) {
        mclog::tagError(_tag, "boot sfx binary has invalid size: {}", byte_count);
        return;
    }

    const auto* samples     = reinterpret_cast<const int16_t*>(_boot_sfx_start);
    const std::size_t count = byte_count / sizeof(int16_t);
    std::vector<int16_t> pcm(samples, samples + count);

    audioPlay(pcm, true);
}
