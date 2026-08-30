/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "model/protocol.h"
#include "transport/esp_now_radio.h"
#include "transport/packets.h"
#include "view/view.h"
#include <apps/common/key_manager/key_manager.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <memory>
#include <mutex>
#include <mooncake.h>
#include <optional>
#include <vector>

class AppTransceiver : public mooncake::AppAbility {
public:
    AppTransceiver();

    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    struct PendingControl {
        transceiver::ControlPacket packet;
        transceiver::MacAddress destination = {};
        bool broadcast                      = false;
    };

    transceiver::Protocol _protocol;
    transceiver::EspNowRadio _radio;
    std::unique_ptr<input::KeyManager> _key_manager;
    std::unique_ptr<view::TransceiverView> _view;
    std::optional<PendingControl> _pending_control;
    std::atomic<bool> _power_toggle_requested                     = false;
    std::atomic<bool> _conversation_mode_toggle_requested         = false;
    std::array<uint8_t, transceiver::spectrumBandCount> _spectrum = {};
    transceiver::MacAddress _call_target                          = {};
    transceiver::Role _talker_role                                = transceiver::Role::None;
    std::atomic<uint32_t> _sequence                               = 0;
    uint32_t _last_audio_sequence                                 = 0;
    uint32_t _tx_audio_frames                                     = 0;
    std::atomic<uint32_t> _rx_audio_frames                        = 0;
    uint32_t _tx_audio_peak                                       = 0;
    std::atomic<uint32_t> _rx_audio_peak                          = 0;
    uint32_t _send_failures_at_talk_start                         = 0;
    std::atomic<uint32_t> _playback_writes_at_talk_start          = 0;
    std::atomic<uint32_t> _playback_failures_at_talk_start        = 0;
    uint64_t _tx_sample_count                                     = 0;
    uint64_t _tx_clipped_samples                                  = 0;
    int64_t _tx_sample_sum                                        = 0;
    uint64_t _tx_sample_square_sum                                = 0;
    int16_t _tx_sample_min                                        = 0;
    int16_t _tx_sample_max                                        = 0;
    bool _has_audio_sequence                                      = false;
    bool _answer_release_required                                 = false;
    bool _close_after_send                                        = false;
    bool _speaker_enabled                                         = true;
    TaskHandle_t _capture_task                                    = nullptr;
    TaskHandle_t _playback_task                                   = nullptr;
    QueueHandle_t _playback_queue                                 = nullptr;
    std::atomic<bool> _capture_enabled                            = false;
    std::atomic<bool> _open_mic_active                            = false;
    std::atomic<bool> _playback_talk_ended                        = true;
    std::atomic<bool> _playback_report_pending                    = false;
    std::atomic<uint32_t> _captured_audio_frames                  = 0;
    std::atomic<uint32_t> _playback_queue_drops                   = 0;
    std::mutex _capture_mutex;
    transceiver::MacAddress _capture_peer                                  = {};
    uint32_t _capture_session                                              = 0;
    transceiver::Role _capture_role                                        = transceiver::Role::None;
    std::array<uint8_t, transceiver::spectrumBandCount> _captured_spectrum = {};

    void handlePowerToggle();
    void startCall(const transceiver::MacAddress& peer);
    void answerCall(const char* input);
    void handleButtons();
    void cycleSpeakerVolume();
    void handleConversationModeToggle();
    bool applyConversationMode(transceiver::ConversationMode mode, bool notifyPeer);
    void startCapture(bool announceTalkStart);
    void stopCapture(bool announceTalkStop);
    void handleReceivedPackets();
    void handlePacket(const transceiver::EspNowRadio::ReceivedPacket& received);
    void handleControl(const transceiver::EspNowRadio::ReceivedPacket& received,
                       const transceiver::ControlPacket& packet);
    void handleAudio(const transceiver::EspNowRadio::ReceivedPacket& received, const transceiver::AudioPacket& packet);
    void beginRemoteTalk(transceiver::Role talker);
    void captureAudioTask();
    void playbackAudioTask();
    void sendControl(transceiver::PacketType type, bool broadcast = false);
    void flushPendingControl();
    void syncView();
    void resetStreamState();
    void setSpeakerEnabled(bool enabled);
};
