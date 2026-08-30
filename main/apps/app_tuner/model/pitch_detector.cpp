#include "pitch_detector.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace tuner {
namespace {

constexpr float yinThreshold = 0.1f;
constexpr float lowestFrequencyHz = 27.5f;
constexpr float highestFrequencyHz = 4186.01f;

}  // namespace

float midiFrequency(int midi, float calibrationHz)
{
    return calibrationHz * std::pow(2.0f, static_cast<float>(midi - 69) / 12.0f);
}

std::string_view katakanaNoteName(int pitchClass)
{
    static constexpr std::array<std::string_view, 12> names = {
        "ド", "レフラット", "レ", "ミフラット", "ミ", "ファ",
        "ソフラット", "ソ", "ラフラット", "ラ", "シフラット", "シ",
    };
    const int normalized = ((pitchClass % 12) + 12) % 12;
    return names[static_cast<std::size_t>(normalized)];
}

PitchResult PitchDetector::analyze(const std::vector<int16_t>& samples, float calibrationHz) const
{
    PitchResult result;
    if (samples.size() < frameSize || calibrationHz < calibrationMinHz || calibrationHz > calibrationMaxHz) {
        return result;
    }

    const std::size_t minTau = static_cast<std::size_t>(std::ceil(sampleRate / highestFrequencyHz));
    const std::size_t maxTau = std::min(frameSize / 2, static_cast<std::size_t>(sampleRate / lowestFrequencyHz));
    const std::size_t analysisWindow = frameSize - maxTau;
    std::vector<float> difference(maxTau + 1, 0.0f);
    std::vector<float> normalized(maxTau + 1, 1.0f);

    for (std::size_t tau = 1; tau <= maxTau; ++tau) {
        float sum = 0.0f;
        for (std::size_t i = 0; i < analysisWindow; ++i) {
            const float delta = static_cast<float>(samples[i]) - static_cast<float>(samples[i + tau]);
            sum += delta * delta;
        }
        difference[tau] = sum;
    }

    float runningSum = 0.0f;
    for (std::size_t tau = 1; tau <= maxTau; ++tau) {
        runningSum += difference[tau];
        normalized[tau] = runningSum > 0.0f ? difference[tau] * static_cast<float>(tau) / runningSum : 1.0f;
    }

    std::size_t bestTau = 0;
    for (std::size_t tau = minTau; tau <= maxTau; ++tau) {
        if (normalized[tau] >= yinThreshold) {
            continue;
        }
        while (tau + 1 <= maxTau && normalized[tau + 1] < normalized[tau]) {
            ++tau;
        }
        bestTau = tau;
        break;
    }
    if (bestTau == 0) {
        return result;
    }

    float refinedTau = static_cast<float>(bestTau);
    if (bestTau > 1 && bestTau < maxTau) {
        const float left = normalized[bestTau - 1];
        const float center = normalized[bestTau];
        const float right = normalized[bestTau + 1];
        const float denominator = 2.0f * (2.0f * center - right - left);
        if (std::abs(denominator) > std::numeric_limits<float>::epsilon()) {
            refinedTau += (right - left) / denominator;
        }
    }

    const float frequency = static_cast<float>(sampleRate) / refinedTau;
    const int midi = static_cast<int>(std::lround(69.0f + 12.0f * std::log2(frequency / calibrationHz)));
    const float target = midiFrequency(midi, calibrationHz);
    result.valid = std::isfinite(frequency) && frequency >= lowestFrequencyHz && frequency <= highestFrequencyHz;
    result.frequencyHz = frequency;
    result.cents = 1200.0f * std::log2(frequency / target);
    result.confidence = 1.0f - normalized[bestTau];
    result.midi = midi;
    result.octave = midi / 12 - 1;
    result.pitchClass = ((midi % 12) + 12) % 12;
    return result;
}

}  // namespace tuner
