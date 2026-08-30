#include "../main/apps/app_ear_game/model/ear_game.h"
#include "../main/apps/app_tuner/model/pitch_detector.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <set>
#include <atomic>
#include <thread>

namespace {

std::vector<int16_t> sine(float frequency)
{
    std::vector<int16_t> samples(tuner::PitchDetector::frameSize);
    for (std::size_t i = 0; i < samples.size(); ++i) {
        samples[i] = static_cast<int16_t>(12000.0f * std::sin(2.0f * static_cast<float>(M_PI) * frequency * i /
                                                          tuner::PitchDetector::sampleRate));
    }
    return samples;
}

void verifyQuestion(ear_game::Mode mode)
{
    for (uint32_t seed = 0; seed < 200; ++seed) {
        const auto question = ear_game::makeQuestion(mode, seed);
        assert(question.correctChoice >= 0 && question.correctChoice < 3);
        assert(!question.sounds.empty());
        assert(!question.answerDetail.empty());
        assert(question.answerDetail.find('#') == std::string::npos);
        const std::set<std::string> choices(question.choices.begin(), question.choices.end());
        assert(choices.size() == 3);
        assert(question.choices[question.correctChoice] == question.answerDetail || mode == ear_game::Mode::Scale);
    }
}

}  // namespace

int main()
{
    tuner::PitchDetector detector;
    auto a440 = detector.analyze(sine(440.0f), 440.0f);
    assert(a440.valid && a440.pitchClass == 9 && a440.octave == 4);
    assert(std::abs(a440.cents) <= 1.0f);

    auto a442 = detector.analyze(sine(442.0f), 442.0f);
    assert(a442.valid && a442.pitchClass == 9 && a442.octave == 4);
    assert(std::abs(a442.cents) <= 1.0f);

    auto c4 = detector.analyze(sine(261.6256f), 440.0f);
    assert(c4.valid && c4.pitchClass == 0 && c4.octave == 4);
    assert(std::abs(c4.cents) <= 1.0f);

    for (int midi = 48; midi <= 84; ++midi) {
        const float frequency = tuner::midiFrequency(midi, 440.0f);
        const auto pitch = detector.analyze(sine(frequency), 440.0f);
        assert(pitch.valid && pitch.midi == midi);
    }

    for (const float cents : {-40.0f, 40.0f}) {
        const float detuned = 440.0f * std::pow(2.0f, cents / 1200.0f);
        const auto pitch = detector.analyze(sine(detuned), 440.0f);
        assert(pitch.valid && pitch.midi == 69);
        assert((pitch.cents < 0.0f) == (cents < 0.0f));
    }

    std::vector<int16_t> silence(tuner::PitchDetector::frameSize, 0);
    assert(!detector.analyze(silence, 440.0f).valid);
    assert(!detector.analyze(a440.valid ? sine(440.0f) : silence, 409.0f).valid);

    assert(tuner::katakanaNoteName(1) == "レフラット");
    assert(tuner::katakanaNoteName(10) == "シフラット");
    verifyQuestion(ear_game::Mode::Note);
    verifyQuestion(ear_game::Mode::Scale);
    verifyQuestion(ear_game::Mode::Chord);

    for (const auto mode : {ear_game::Mode::Note, ear_game::Mode::Scale, ear_game::Mode::Chord}) {
        const auto question = ear_game::makeQuestion(mode, 1);
        const auto questionAudio = ear_game::renderQuestionAudio(question.sounds);
        const std::size_t expectedSamples =
            question.sounds.size() * ear_game::ToneStream::framesPerBeat * ear_game::ToneStream::sampleRate *
            ear_game::ToneStream::frameDurationMs / 1000;
        assert(questionAudio.size() == expectedSamples);
        assert(std::any_of(questionAudio.begin(), questionAudio.end(), [](int16_t sample) { return sample != 0; }));
        assert(questionAudio.back() == 0);
    }

    const auto speakerRangeC = ear_game::renderQuestionAudio({{60}});
    std::size_t risingZeroCrossings = 0;
    for (std::size_t i = 1; i < speakerRangeC.size(); ++i) {
        risingZeroCrossings += speakerRangeC[i - 1] <= 0 && speakerRangeC[i] > 0;
    }
    const float renderedFrequency = static_cast<float>(risingZeroCrossings) * ear_game::ToneStream::sampleRate /
                                    static_cast<float>(speakerRangeC.size());
    const float expectedFrequency =
        tuner::midiFrequency(60 + ear_game::ToneStream::playbackOctaveShift * 12, 440.0f);
    assert(std::abs(renderedFrequency - expectedFrequency) / expectedFrequency < 0.01f);
    assert(*std::max_element(speakerRangeC.begin(), speakerRangeC.end()) >=
           std::numeric_limits<int16_t>::max() / 2 - 1);

    bool sawDoMiSo = false;
    bool sawSoSiRe = false;
    for (uint32_t seed = 0; seed < 200; ++seed) {
        const auto chord = ear_game::makeQuestion(ear_game::Mode::Chord, seed);
        sawDoMiSo = sawDoMiSo || chord.answerDetail == "ド ミ ソ";
        sawSoSiRe = sawSoSiRe || chord.answerDetail == "ソ シ レ";
    }
    assert(sawDoMiSo && sawSoSiRe);

    const auto scale = ear_game::makeQuestion(ear_game::Mode::Scale, 1);
    ear_game::ToneStream stream;
    stream.start(scale.sounds);
    assert(stream.active());
    const auto audio = stream.render();
    assert(audio.size() == ear_game::ToneStream::sampleRate * ear_game::ToneStream::frameDurationMs / 1000);
    assert(std::any_of(audio.begin(), audio.end(), [](int16_t sample) { return sample != 0; }));
    std::vector<int16_t> nextNote;
    for (int frame = 1; frame <= ear_game::ToneStream::framesPerBeat; ++frame) {
        nextNote = stream.render();
    }
    assert(audio != nextNote);

    ear_game::ToneStream continuousTone;
    continuousTone.start({{61}});
    std::vector<int16_t> beforeBoundary;
    for (int frame = 0; frame < ear_game::ToneStream::framesPerBeat; ++frame) {
        beforeBoundary = continuousTone.render();
    }
    const auto afterBoundary = continuousTone.render();
    const float frequency =
        tuner::midiFrequency(61 + ear_game::ToneStream::playbackOctaveShift * 12, 440.0f);
    const float angleStep = 2.0f * static_cast<float>(M_PI) * frequency / ear_game::ToneStream::sampleRate;
    const float peak = static_cast<float>(std::numeric_limits<int16_t>::max()) / 2.0f;
    const int maximumAdjacentDelta = static_cast<int>(std::ceil(2.0f * peak * std::sin(angleStep / 2.0f))) + 2;
    assert(std::abs(static_cast<int>(afterBoundary.front()) - static_cast<int>(beforeBoundary.back())) <=
           maximumAdjacentDelta);

    stream.stop();
    assert(!stream.active());
    const auto silenceFrame = stream.render();
    assert(std::all_of(silenceFrame.begin(), silenceFrame.end(), [](int16_t sample) { return sample == 0; }));

    ear_game::ToneStream concurrentStream;
    std::atomic<bool> rendering{true};
    std::thread renderer([&]() {
        while (rendering.load()) {
            concurrentStream.render();
        }
    });
    for (int iteration = 0; iteration < 100; ++iteration) {
        concurrentStream.start(scale.sounds);
        concurrentStream.stop();
    }
    rendering.store(false);
    renderer.join();
}
