#pragma once

#include "model/ear_game.h"
#include "view/view.h"
#include <apps/common/key_manager/key_manager.h>
#include <memory>
#include <mooncake.h>

class AppEarGame : public mooncake::AppAbility {
public:
    AppEarGame();
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    void bindView();
    void selectMode(ear_game::Mode mode);
    void nextQuestion();
    void playQuestionOnce();
    void selectChoice(int choice);

    std::unique_ptr<input::KeyManager> _keyManager;
    std::unique_ptr<view::EarGameView> _view;
    ear_game::Question _question;
    ear_game::Mode _mode = ear_game::Mode::Note;
    bool _hasQuestion = false;
};
