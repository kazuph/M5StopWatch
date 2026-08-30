/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_transceiver.h"
#include "model/volume.h"
#include <algorithm>
#include <assets/assets.h>
#include <cmath>
#include <cstring>
#include <esp_system.h>
#include <hal/hal.h>
#include <limits>
#include <mooncake_log.h>

namespace {

uint32_t peakAmplitude(const int16_t* samples, std::size_t count)
{
    uint32_t peak = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const int32_t sample     = samples[i];
        const uint32_t amplitude = static_cast<uint32_t>(sample < 0 ? -sample : sample);
        peak                     = std::max(peak, amplitude);
    }
    return peak;
}

view::TransceiverView::State toViewState(const transceiver::Protocol& protocol)
{
    using ViewState = view::TransceiverView::State;
    switch (protocol.phase()) {
        case transceiver::Phase::Off:
            return ViewState::Off;
        case transceiver::Phase::Discovering:
            return ViewState::Searching;
        case transceiver::Phase::Calling:
            return ViewState::Calling;
        case transceiver::Phase::Incoming:
            return ViewState::Incoming;
        case transceiver::Phase::Answering:
            return ViewState::Connecting;
        case transceiver::Phase::Connected:
            if (protocol.localTalking()) {
                return ViewState::Talking;
            }
            if (protocol.remoteTalking()) {
                return ViewState::Listening;
            }
            return ViewState::Connected;
        case transceiver::Phase::Error:
            return ViewState::Error;
    }
    return ViewState::Error;
}

view::TransceiverView::Role toViewRole(transceiver::Role role)
{
    switch (role) {
        case transceiver::Role::Parent:
            return view::TransceiverView::Role::Parent;
        case transceiver::Role::Child:
            return view::TransceiverView::Role::Child;
        case transceiver::Role::None:
            return view::TransceiverView::Role::None;
    }
    return view::TransceiverView::Role::None;
}

view::TransceiverView::ConversationMode toViewConversationMode(transceiver::ConversationMode mode)
{
    return mode == transceiver::ConversationMode::OpenMic ? view::TransceiverView::ConversationMode::OpenMic
                                                          : view::TransceiverView::ConversationMode::Ptt;
}

}  // namespace

AppTransceiver::AppTransceiver() : _protocol(GetHAL().getFactoryMac())
{
    setAppInfo().name = "Transceiver";
    setAppInfo().icon = (void*)&icon_fft;
}

void AppTransceiver::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
    _playback_queue = xQueueCreate(transceiver::transportBufferFrames, sizeof(transceiver::AudioPacket));
    if (xTaskCreate([](void* app) { static_cast<AppTransceiver*>(app)->captureAudioTask(); }, "transceiver_capture",
                    4 * 1024, this, 5, &_capture_task) != pdPASS) {
        _capture_task = nullptr;
        mclog::tagError(getAppInfo().name, "failed to create capture task");
    }
    if (_playback_queue == nullptr ||
        xTaskCreate([](void* app) { static_cast<AppTransceiver*>(app)->playbackAudioTask(); }, "transceiver_playback",
                    4 * 1024, this, 5, &_playback_task) != pdPASS) {
        _playback_task = nullptr;
        mclog::tagError(getAppInfo().name, "failed to create playback task");
    }
}

void AppTransceiver::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open, reset_reason={}", static_cast<int>(esp_reset_reason()));
    _key_manager = std::make_unique<input::KeyManager>();
    GetHAL().setAudioInputMode(Hal::AudioInputMode::I2s1LeftCodec);
    if (_radio.start()) {
        _protocol.startDiscovery();
    } else {
        _protocol.setError();
    }

    LvglLockGuard lock;
    _view = std::make_unique<view::TransceiverView>();
    _view->init(lv_screen_active());
    _view->onPowerToggle            = [this]() { _power_toggle_requested.store(true); };
    _view->onConversationModeToggle = [this]() { _conversation_mode_toggle_requested.store(true); };
    if (_protocol.phase() == transceiver::Phase::Discovering) {
        mclog::tagInfo(getAppInfo().name, "discovering peer");
        sendControl(transceiver::PacketType::Discover, true);
    }
    syncView();
}

void AppTransceiver::onRunning()
{
    GetHAL().updateButtonStates(false);
    const input::KeyEvent key = _key_manager ? _key_manager->update(false) : input::KeyEvent::None;

    flushPendingControl();
    handleReceivedPackets();

    if (_close_after_send && _radio.isSendReady() && !_pending_control) {
        close();
        return;
    }
    if (key == input::KeyEvent::GoHome) {
        if (_protocol.phase() != transceiver::Phase::Off && _protocol.phase() != transceiver::Phase::Error) {
            sendControl(transceiver::PacketType::Hangup, !_protocol.hasPeer());
            _protocol.hangup();
            _close_after_send = true;
        } else {
            close();
        }
        return;
    }

    if (_power_toggle_requested.exchange(false)) {
        handlePowerToggle();
    }
    if (_conversation_mode_toggle_requested.exchange(false)) {
        handleConversationModeToggle();
    }
    handleButtons();

    LvglLockGuard lock;
    syncView();
}

void AppTransceiver::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");
    _radio.stop();
    _protocol.hangup();
    _key_manager.reset();
    _pending_control.reset();
    _power_toggle_requested.store(false);
    _conversation_mode_toggle_requested.store(false);
    _close_after_send = false;
    _capture_enabled.store(false);
    resetStreamState();

    LvglLockGuard lock;
    _view.reset();
}

void AppTransceiver::handlePowerToggle()
{
    if (_protocol.phase() == transceiver::Phase::Off) {
        _protocol.startDiscovery();
        mclog::tagInfo(getAppInfo().name, "discovering peer");
        sendControl(transceiver::PacketType::Discover, true);
        return;
    }

    if (_protocol.phase() == transceiver::Phase::Discovering) {
        _protocol.hangup();
        return;
    }

    if (_protocol.phase() == transceiver::Phase::Incoming) {
        answerCall("tap");
        return;
    }

    if (_protocol.phase() == transceiver::Phase::Calling || _protocol.phase() == transceiver::Phase::Answering) {
        mclog::tagInfo(getAppInfo().name, "hangup, session={}", _protocol.session());
        sendControl(transceiver::PacketType::Hangup, !_protocol.hasPeer());
        _protocol.hangup();
        resetStreamState();
    }
}

void AppTransceiver::startCall(const transceiver::MacAddress& peer)
{
    uint32_t session = 0;
    while (session == 0) {
        session = esp_random();
    }
    _call_target = peer;
    _protocol.startCall(session);
    resetStreamState();
    mclog::tagInfo(getAppInfo().name, "calling as parent, session={}", session);
    sendControl(transceiver::PacketType::Call);
}

void AppTransceiver::answerCall(const char* input)
{
    if (_protocol.phase() == transceiver::Phase::Incoming && _protocol.answer()) {
        mclog::tagInfo(getAppInfo().name, "answered, input={}, session={}", input, _protocol.session());
        sendControl(transceiver::PacketType::Answer);
        _answer_release_required = true;
    }
}

void AppTransceiver::handleButtons()
{
    auto& yellow = GetHAL().btnA;
    auto& blue   = GetHAL().btnB;
    if (yellow.wasPressed() && !_protocol.localTalking()) {
        cycleSpeakerVolume();
    }
    if (_protocol.phase() == transceiver::Phase::Incoming && blue.wasPressed()) {
        answerCall("blue button");
        return;
    }

    if (_answer_release_required) {
        if (blue.isReleased()) {
            _answer_release_required = false;
        }
        return;
    }

    if (_protocol.phase() != transceiver::Phase::Connected) {
        return;
    }

    if (_protocol.conversationMode() == transceiver::ConversationMode::OpenMic) {
        return;
    }

    if (blue.wasPressed() && _protocol.beginLocalTalk()) {
        startCapture(true);
        mclog::tagInfo(getAppInfo().name, "push to talk start");
    }
    if (blue.wasReleased() && _protocol.localTalking()) {
        stopCapture(true);
    }
}

void AppTransceiver::startCapture(bool announceTalkStart)
{
    _playback_talk_ended.store(true);
    _playback_report_pending.store(false);
    if (_playback_queue != nullptr) {
        xQueueReset(_playback_queue);
    }
    _talker_role                 = _protocol.role();
    _send_failures_at_talk_start = _radio.sendFailureCount();
    _captured_audio_frames.store(0);
    {
        std::lock_guard<std::mutex> lock(_capture_mutex);
        _capture_peer    = _protocol.peer();
        _capture_session = _protocol.session();
        _capture_role    = _protocol.role();
        _captured_spectrum.fill(0);
        _tx_audio_frames      = 0;
        _tx_audio_peak        = 0;
        _tx_sample_count      = 0;
        _tx_clipped_samples   = 0;
        _tx_sample_sum        = 0;
        _tx_sample_square_sum = 0;
        _tx_sample_min        = std::numeric_limits<int16_t>::max();
        _tx_sample_max        = std::numeric_limits<int16_t>::min();
    }
    if (announceTalkStart) {
        setSpeakerEnabled(false);
        sendControl(transceiver::PacketType::TalkStart);
    }
    _capture_enabled.store(true);
    if (_capture_task != nullptr) {
        xTaskNotifyGive(_capture_task);
    }
}

void AppTransceiver::stopCapture(bool announceTalkStop)
{
    _capture_enabled.store(false);
    if (announceTalkStop) {
        _protocol.endLocalTalk();
    }
    setSpeakerEnabled(true);
    _spectrum.fill(0);
    _talker_role = transceiver::Role::None;
    {
        std::lock_guard<std::mutex> lock(_capture_mutex);
        const double mean =
            _tx_sample_count == 0 ? 0.0 : static_cast<double>(_tx_sample_sum) / static_cast<double>(_tx_sample_count);
        const double rms =
            _tx_sample_count == 0
                ? 0.0
                : std::sqrt(static_cast<double>(_tx_sample_square_sum) / static_cast<double>(_tx_sample_count));
        mclog::tagInfo(getAppInfo().name,
                       "push to talk stop, captured_frames={}, tx_frames={}, peak={}, samples={}, min={}, "
                       "max={}, mean={}, rms={}, clipped={}, send_failures={}",
                       _captured_audio_frames.load(), _tx_audio_frames, _tx_audio_peak, _tx_sample_count,
                       _tx_sample_min, _tx_sample_max, mean, rms, _tx_clipped_samples,
                       _radio.sendFailureCount() - _send_failures_at_talk_start);
    }
    if (announceTalkStop) {
        sendControl(transceiver::PacketType::TalkStop);
    }
}

void AppTransceiver::handleConversationModeToggle()
{
    if (_protocol.phase() != transceiver::Phase::Connected) {
        return;
    }
    const auto desired = _protocol.conversationMode() == transceiver::ConversationMode::Ptt
                             ? transceiver::ConversationMode::OpenMic
                             : transceiver::ConversationMode::Ptt;
    if (_protocol.role() == transceiver::Role::Parent) {
        applyConversationMode(desired, true);
    } else {
        PendingControl pending;
        pending.packet.header.type      = transceiver::PacketType::ModeRequest;
        pending.packet.header.session   = _protocol.session();
        pending.packet.header.sequence  = _sequence.fetch_add(1) + 1;
        pending.packet.role             = _protocol.role();
        pending.packet.conversationMode = desired;
        pending.destination             = _protocol.peer();
        _pending_control                = pending;
        flushPendingControl();
    }
}

bool AppTransceiver::applyConversationMode(transceiver::ConversationMode mode, bool notifyPeer)
{
    if (_protocol.phase() != transceiver::Phase::Connected) {
        return false;
    }
    if (_protocol.conversationMode() == mode) {
        if (notifyPeer) {
            sendControl(transceiver::PacketType::ModeSet);
        }
        return true;
    }

    if (mode == transceiver::ConversationMode::OpenMic) {
        _capture_enabled.store(false);
        if (!GetHAL().audioDuplexStart()) {
            mclog::tagError(getAppInfo().name, "failed to start open mic audio path");
            return false;
        }
        _protocol.setConversationMode(mode);
        _open_mic_active.store(true);
        _playback_talk_ended.store(false);
        _playback_report_pending.store(false);
        if (_playback_queue != nullptr) {
            xQueueReset(_playback_queue);
        }
        startCapture(false);
        _playback_talk_ended.store(false);
    } else {
        _open_mic_active.store(false);
        stopCapture(false);
        GetHAL().audioDuplexStop();
        _protocol.setConversationMode(mode);
        resetStreamState();
    }

    if (notifyPeer) {
        sendControl(transceiver::PacketType::ModeSet);
    }
    mclog::tagInfo(getAppInfo().name, "conversation mode: {}",
                   mode == transceiver::ConversationMode::OpenMic ? "open mic" : "ptt");
    return true;
}

void AppTransceiver::cycleSpeakerVolume()
{
    const int next = transceiver::volume::nextPreset(GetHAL().getSpeakerVolume());
    GetHAL().setSpeakerVolume(next, true);
    mclog::tagInfo(getAppInfo().name, "yellow button selected speaker volume: {}", next);
}

void AppTransceiver::handleReceivedPackets()
{
    transceiver::EspNowRadio::ReceivedPacket received;
    while (_radio.receive(received)) {
        if (received.length >= sizeof(transceiver::PacketHeader)) {
            handlePacket(received);
        }
    }
}

void AppTransceiver::handlePacket(const transceiver::EspNowRadio::ReceivedPacket& received)
{
    transceiver::PacketHeader header;
    std::memcpy(&header, received.payload.data(), sizeof(header));
    if (!transceiver::validHeader(header)) {
        return;
    }

    if (header.type == transceiver::PacketType::Audio) {
        if (received.length != sizeof(transceiver::AudioPacket)) {
            return;
        }
        transceiver::AudioPacket packet;
        std::memcpy(&packet, received.payload.data(), sizeof(packet));
        handleAudio(received, packet);
        return;
    }

    if (received.length != sizeof(transceiver::ControlPacket)) {
        return;
    }
    transceiver::ControlPacket packet;
    std::memcpy(&packet, received.payload.data(), sizeof(packet));
    handleControl(received, packet);
}

void AppTransceiver::handleControl(const transceiver::EspNowRadio::ReceivedPacket& received,
                                   const transceiver::ControlPacket& packet)
{
    if ((packet.header.type == transceiver::PacketType::ModeRequest ||
         packet.header.type == transceiver::PacketType::ModeSet) &&
        !transceiver::validConversationMode(packet.conversationMode)) {
        return;
    }
    switch (packet.header.type) {
        case transceiver::PacketType::Discover:
            if (_protocol.phase() == transceiver::Phase::Discovering) {
                startCall(received.sender);
            } else if (_protocol.phase() == transceiver::Phase::Calling) {
                sendControl(transceiver::PacketType::Call);
            } else if (_protocol.receivePeerRestart(received.sender)) {
                resetStreamState();
                mclog::tagInfo(getAppInfo().name, "peer restarted, returning to discovery");
                sendControl(transceiver::PacketType::Discover, true);
            }
            break;
        case transceiver::PacketType::Call: {
            const transceiver::Phase before = _protocol.phase();
            _protocol.receiveCall(received.sender, packet.header.session);
            if (_protocol.phase() == transceiver::Phase::Incoming && before != transceiver::Phase::Incoming) {
                _radio.addPeer(received.sender);
                resetStreamState();
                mclog::tagInfo(getAppInfo().name, "incoming call as child, session={}", _protocol.session());
                answerCall("automatic");
            } else if (before == transceiver::Phase::Calling && _protocol.phase() == transceiver::Phase::Calling) {
                sendControl(transceiver::PacketType::Call);
            }
            break;
        }
        case transceiver::PacketType::Answer: {
            const transceiver::Phase before = _protocol.phase();
            _protocol.receiveAnswer(received.sender, packet.header.session);
            if (before == transceiver::Phase::Calling && _protocol.phase() == transceiver::Phase::Connected) {
                _radio.addPeer(received.sender);
                resetStreamState();
                mclog::tagInfo(getAppInfo().name, "call connected as parent, session={}", _protocol.session());
                sendControl(transceiver::PacketType::Confirm);
            }
            break;
        }
        case transceiver::PacketType::Confirm:
            _protocol.receiveConfirm(received.sender, packet.header.session);
            if (_protocol.phase() == transceiver::Phase::Connected) {
                mclog::tagInfo(getAppInfo().name, "call confirmed as child, session={}", _protocol.session());
                sendControl(transceiver::PacketType::ModeRequest);
            }
            break;
        case transceiver::PacketType::Hangup:
            _protocol.receiveHangup(received.sender, packet.header.session);
            resetStreamState();
            mclog::tagInfo(getAppInfo().name, "remote hangup, session={}", packet.header.session);
            break;
        case transceiver::PacketType::TalkStart: {
            const bool was_talking = _protocol.remoteTalking();
            _protocol.receiveTalkStart(received.sender, packet.header.session, packet.role);
            if (!was_talking && _protocol.remoteTalking()) {
                beginRemoteTalk(packet.role);
            }
        } break;
        case transceiver::PacketType::TalkStop:
            _protocol.receiveTalkStop(received.sender, packet.header.session, packet.role);
            _playback_talk_ended.store(true);
            _playback_report_pending.store(true);
            if (_playback_task != nullptr) {
                xTaskNotifyGive(_playback_task);
            }
            _spectrum.fill(0);
            _talker_role = transceiver::Role::None;
            break;
        case transceiver::PacketType::ModeRequest:
            if (_protocol.role() == transceiver::Role::Parent &&
                _protocol.matchesPeerSession(received.sender, packet.header.session) &&
                packet.role == transceiver::Role::Child) {
                applyConversationMode(packet.conversationMode, true);
            }
            break;
        case transceiver::PacketType::ModeSet:
            if (_protocol.role() == transceiver::Role::Child &&
                _protocol.matchesPeerSession(received.sender, packet.header.session) &&
                packet.role == transceiver::Role::Parent) {
                if (!applyConversationMode(packet.conversationMode, false) &&
                    packet.conversationMode == transceiver::ConversationMode::OpenMic) {
                    sendControl(transceiver::PacketType::ModeRequest);
                }
            }
            break;
        case transceiver::PacketType::Audio:
            break;
    }
}

void AppTransceiver::handleAudio(const transceiver::EspNowRadio::ReceivedPacket& received,
                                 const transceiver::AudioPacket& packet)
{
    if (packet.sampleCount != transceiver::audioSamplesPerPacket) {
        return;
    }
    const bool was_talking = _protocol.remoteTalking();
    if (!_protocol.acceptAudio(received.sender, packet.header.session, packet.talker)) {
        return;
    }
    if (!was_talking && _protocol.conversationMode() == transceiver::ConversationMode::Ptt) {
        beginRemoteTalk(packet.talker);
    }
    if (_has_audio_sequence && static_cast<int32_t>(packet.header.sequence - _last_audio_sequence) <= 0) {
        return;
    }
    _has_audio_sequence  = true;
    _last_audio_sequence = packet.header.sequence;
    _spectrum            = packet.spectrum;
    _talker_role         = packet.talker;

    _rx_audio_frames.fetch_add(1);
    if (_playback_queue == nullptr || xQueueSend(_playback_queue, &packet, 0) != pdTRUE) {
        _playback_queue_drops.fetch_add(1);
        return;
    }
    if (_playback_task != nullptr) {
        xTaskNotifyGive(_playback_task);
    }
}

void AppTransceiver::beginRemoteTalk(transceiver::Role talker)
{
    if (_protocol.conversationMode() == transceiver::ConversationMode::Ptt) {
        _capture_enabled.store(false);
    }
    _rx_audio_frames                  = 0;
    _rx_audio_peak                    = 0;
    const Hal::AudioStreamStats stats = GetHAL().getAudioStreamStats();
    _playback_writes_at_talk_start    = stats.writtenFrames;
    _playback_failures_at_talk_start  = stats.failedFrames;
    _playback_queue_drops.store(0);
    _playback_talk_ended.store(false);
    _playback_report_pending.store(false);
    if (_playback_queue != nullptr) {
        xQueueReset(_playback_queue);
    }
    GetHAL().setSpeakerEnabled(true);
    _speaker_enabled = true;
    _talker_role     = talker;
}

void AppTransceiver::captureAudioTask()
{
    std::vector<int16_t> source;
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        transceiver::MacAddress peer = {};
        uint32_t session             = 0;
        transceiver::Role role       = transceiver::Role::None;
        {
            std::lock_guard<std::mutex> lock(_capture_mutex);
            peer    = _capture_peer;
            session = _capture_session;
            role    = _capture_role;
        }
        while (_capture_enabled.load()) {
            if (_open_mic_active.load()) {
                GetHAL().audioDuplexRecord(source, transceiver::captureDurationMs);
            } else {
                GetHAL().audioRecord(source, transceiver::captureDurationMs);
            }
            if (!_capture_enabled.load() || source.size() < transceiver::sourceSamplesPerPacket) {
                continue;
            }
            const auto spectrum                                              = GetHAL().audioAnalyzePacket(source);
            std::array<int16_t, transceiver::sourceSamplesPerPacket> capture = {};
            std::copy_n(source.begin(), capture.size(), capture.begin());

            transceiver::AudioPacket packet;
            packet.header.type     = transceiver::PacketType::Audio;
            packet.header.session  = session;
            packet.header.sequence = _sequence.fetch_add(1) + 1;
            packet.talker          = role;
            packet.spectrum        = spectrum;
            packet.samples         = transceiver::g711::encodeFrame<transceiver::captureSampleRate>(capture);
            _captured_audio_frames.fetch_add(1);
            const bool sent = _radio.sendToBlocking(peer, &packet, sizeof(packet));

            std::lock_guard<std::mutex> lock(_capture_mutex);
            _captured_spectrum = spectrum;
            for (const int16_t sample : capture) {
                const int64_t value = sample;
                ++_tx_sample_count;
                _tx_sample_sum += value;
                _tx_sample_square_sum += static_cast<uint64_t>(value * value);
                _tx_sample_min = std::min(_tx_sample_min, sample);
                _tx_sample_max = std::max(_tx_sample_max, sample);
                if (sample == std::numeric_limits<int16_t>::min() || sample == std::numeric_limits<int16_t>::max()) {
                    ++_tx_clipped_samples;
                }
            }
            if (sent) {
                ++_tx_audio_frames;
                _tx_audio_peak = std::max(_tx_audio_peak, peakAmplitude(capture.data(), capture.size()));
            }
        }
    }
}

void AppTransceiver::playbackAudioTask()
{
    transceiver::AudioPacket packet;
    std::vector<int16_t> playback(transceiver::playbackSamplesPerPacket);
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        bool playing = false;
        while (_playback_queue != nullptr) {
            const UBaseType_t queued = uxQueueMessagesWaiting(_playback_queue);
            if (!playing && queued < transceiver::playbackPrebufferFrames && !_playback_talk_ended.load()) {
                break;
            }
            if (xQueueReceive(_playback_queue, &packet, 0) != pdTRUE) {
                if (_playback_talk_ended.load() && _playback_report_pending.exchange(false)) {
                    const Hal::AudioStreamStats stats = GetHAL().getAudioStreamStats();
                    mclog::tagInfo(
                        getAppInfo().name,
                        "remote audio drained, rx_frames={}, peak={}, playback_writes={}, playback_failures={}, "
                        "last_playback_error={}, playback_queue_drops={}, radio_queue_drops={}",
                        _rx_audio_frames.load(), _rx_audio_peak.load(),
                        stats.writtenFrames - _playback_writes_at_talk_start.load(),
                        stats.failedFrames - _playback_failures_at_talk_start.load(), stats.lastError,
                        _playback_queue_drops.load(), _radio.receiveDropCount());
                }
                break;
            }
            playing = true;
            if (_open_mic_active.load()) {
                playback.resize(transceiver::duplexSamplesPerPacket);
                transceiver::g711::decodeFrameInto<transceiver::duplexSampleRate, transceiver::duplexSamplesPerPacket>(
                    packet.samples, playback.data());
            } else {
                playback.resize(transceiver::playbackSamplesPerPacket);
                transceiver::g711::decodeFrameInto<transceiver::playbackSampleRate,
                                                   transceiver::playbackSamplesPerPacket>(packet.samples,
                                                                                          playback.data());
            }
            const uint32_t frame_peak = peakAmplitude(playback.data(), playback.size());
            uint32_t observed_peak    = _rx_audio_peak.load();
            while (observed_peak < frame_peak && !_rx_audio_peak.compare_exchange_weak(observed_peak, frame_peak)) {
            }
            if (_open_mic_active.load()) {
                GetHAL().audioDuplexStreamWrite(playback);
            } else {
                GetHAL().audioStreamWrite(playback);
            }
        }
    }
}

void AppTransceiver::sendControl(transceiver::PacketType type, bool broadcast)
{
    PendingControl pending;
    pending.packet.header.type      = type;
    pending.packet.header.session   = _protocol.session();
    pending.packet.header.sequence  = _sequence.fetch_add(1) + 1;
    pending.packet.role             = _protocol.role();
    pending.packet.conversationMode = _protocol.conversationMode();
    pending.destination =
        type == transceiver::PacketType::Call && !_protocol.hasPeer() ? _call_target : _protocol.peer();
    pending.broadcast = broadcast;
    _pending_control  = pending;
    flushPendingControl();
}

void AppTransceiver::flushPendingControl()
{
    if (!_pending_control || !_radio.isSendReady()) {
        return;
    }
    const bool sent =
        _pending_control->broadcast
            ? _radio.sendBroadcast(&_pending_control->packet, sizeof(_pending_control->packet))
            : _radio.sendTo(_pending_control->destination, &_pending_control->packet, sizeof(_pending_control->packet));
    if (sent) {
        _pending_control.reset();
    }
}

void AppTransceiver::syncView()
{
    if (!_view) {
        return;
    }
    _view->setState(toViewState(_protocol));
    _view->setRole(toViewRole(_protocol.role()));
    _view->setTalkerRole(toViewRole(_talker_role));
    if (_protocol.localTalking()) {
        std::lock_guard<std::mutex> lock(_capture_mutex);
        _spectrum = _captured_spectrum;
    }
    _view->setSpectrum(_spectrum);
    _view->setVolumePercent(GetHAL().getSpeakerVolume());
    _view->setConversationMode(toViewConversationMode(_protocol.conversationMode()));
}

void AppTransceiver::resetStreamState()
{
    _capture_enabled.store(false);
    _open_mic_active.store(false);
    GetHAL().audioDuplexStop();
    _playback_talk_ended.store(true);
    _playback_report_pending.store(false);
    if (_playback_queue != nullptr) {
        xQueueReset(_playback_queue);
    }
    if (_playback_task != nullptr) {
        xTaskNotifyGive(_playback_task);
    }
    setSpeakerEnabled(true);
    _spectrum.fill(0);
    _talker_role             = transceiver::Role::None;
    _has_audio_sequence      = false;
    _last_audio_sequence     = 0;
    _tx_audio_frames         = 0;
    _rx_audio_frames         = 0;
    _tx_audio_peak           = 0;
    _rx_audio_peak           = 0;
    _answer_release_required = false;
}

void AppTransceiver::setSpeakerEnabled(bool enabled)
{
    if (_speaker_enabled == enabled) {
        return;
    }
    _speaker_enabled = enabled;
    GetHAL().setSpeakerEnabled(enabled);
}
