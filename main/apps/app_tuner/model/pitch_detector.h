#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace tuner {

constexpr int calibrationMinHz = 410;
constexpr int calibrationMaxHz = 480;
constexpr int calibrationDefaultHz = 440;

struct PitchResult {
    bool valid = false;
    float frequencyHz = 0.0f;
    float cents = 0.0f;
    float confidence = 0.0f;
    int midi = 69;
    int octave = 4;
    int pitchClass = 9;
};

class PitchDetector {
public:
    static constexpr uint32_t sampleRate = 16000;
    static constexpr std::size_t frameSize = 2048;

    PitchResult analyze(const std::vector<int16_t>& samples, float calibrationHz) const;
};

float midiFrequency(int midi, float calibrationHz);
std::string_view katakanaNoteName(int pitchClass);

}  // namespace tuner
