#include "../main/apps/app_pomodoro/model/pomodoro.h"

#include <cassert>

int main()
{
    model::Pomodoro timer;
    assert(timer.phase() == model::Pomodoro::Phase::Focus);
    assert(timer.state() == model::Pomodoro::State::Stopped);
    assert(timer.remainingSeconds() == 25 * 60);

    timer.toggle(100);
    assert(timer.state() == model::Pomodoro::State::Running);
    assert(!timer.update(1100));
    assert(timer.remainingSeconds() == 25 * 60 - 1);

    timer.toggle(2100);
    assert(timer.state() == model::Pomodoro::State::Paused);
    const auto paused_remaining = timer.remainingSeconds();
    assert(!timer.update(999999));
    assert(timer.remainingSeconds() == paused_remaining);

    timer.toggle(3000);
    assert(timer.update(3000 + paused_remaining * 1000));
    assert(timer.phase() == model::Pomodoro::Phase::Break);
    assert(timer.remainingSeconds() == 5 * 60);

    timer.reset();
    assert(timer.phase() == model::Pomodoro::Phase::Focus);
    assert(timer.state() == model::Pomodoro::State::Stopped);
    assert(timer.remainingSeconds() == 25 * 60);

    timer.toggle(UINT32_MAX - 500);
    assert(!timer.update(499));
    assert(timer.remainingSeconds() == 25 * 60 - 1);

    timer.reset();
    timer.toggle(0);
    const uint32_t complete_cycle_ms = (25 * 60 + 5 * 60) * 1000;
    assert(timer.update(complete_cycle_ms));
    assert(timer.phase() == model::Pomodoro::Phase::Focus);
    assert(timer.remainingSeconds() == 25 * 60);
}
