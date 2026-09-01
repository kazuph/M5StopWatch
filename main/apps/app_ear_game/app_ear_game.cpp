#include "app_ear_game.h"

#include <apps/app_transceiver/model/volume.h>
#include <assets/assets.h>
#include <esp_random.h>
#include <hal/hal.h>
#include <mooncake_log.h>
#include <smooth_lvgl.hpp>

AppEarGame::AppEarGame()
{
    setAppInfo().name = "Ear Game";
    setAppInfo().icon = (void*)&icon_lucky_wheel;
}

void AppEarGame::onOpen()
{
    GetHAL().setSpeakerEnabled(true);
    _keyManager = std::make_unique<input::KeyManager>();
    LvglLockGuard lock;
    _view = std::make_unique<view::EarGameView>();
    _view->init(lv_screen_active());
    _view->setVolumePercent(GetHAL().getSpeakerVolume());
    bindView();
}

void AppEarGame::onRunning()
{
    if (!_keyManager) {
        return;
    }
    const input::KeyEvent event = _keyManager->update();
    if (event == input::KeyEvent::GoHome) {
        close();
        return;
    }
    if (event == input::KeyEvent::GoPrevious) {
        const int volume = transceiver::volume::nextPreset(GetHAL().getSpeakerVolume());
        GetHAL().setSpeakerVolume(volume, true);
        {
            LvglLockGuard lock;
            _view->setVolumePercent(volume);
        }
        if (_hasQuestion && volume > 0) {
            playQuestionOnce();
        }
    } else if (event == input::KeyEvent::GoNext && _hasQuestion) {
        playQuestionOnce();
    }
}

void AppEarGame::onClose()
{
    _keyManager.reset();
    LvglLockGuard lock;
    _view.reset();
}

void AppEarGame::bindView()
{
    _view->onModeSelected = [this](ear_game::Mode mode) { selectMode(mode); };
    _view->onPlay = [this]() { playQuestionOnce(); };
    _view->onChoiceSelected = [this](int choice) { selectChoice(choice); };
    _view->onNext = [this]() { nextQuestion(); };
}

void AppEarGame::selectMode(ear_game::Mode mode)
{
    _mode = mode;
    nextQuestion();
}

void AppEarGame::nextQuestion()
{
    _question = ear_game::makeQuestion(_mode, esp_random());
    _hasQuestion = true;
    _view->showQuestion(_question);
    playQuestionOnce();
}

void AppEarGame::playQuestionOnce()
{
    if (!_hasQuestion) {
        return;
    }
    const int volume = GetHAL().getSpeakerVolume();
    if (volume <= 0) {
        mclog::tagWarn(getAppInfo().name, "question playback skipped because speaker is muted");
        return;
    }
    auto audio = ear_game::renderQuestionAudio(_question.sounds);
    mclog::tagInfo(getAppInfo().name, "play question once: samples={}, volume={}", audio.size(),
                   volume);
    GetHAL().audioPlay(audio, true);
}

void AppEarGame::selectChoice(int choice)
{
    _view->showAnswer(choice == _question.correctChoice, _question.answerDetail);
}
