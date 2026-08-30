#include "../main/apps/app_transceiver/transport/g711.h"
#include <array>
#include <cassert>
#include <cstdint>

int main()
{
    using namespace transceiver::g711;

    assert(encodeMuLaw(0) == 0xFF);
    assert(decodeMuLaw(0xFF) == 0);
    assert(decodeMuLaw(0x7F) == 0);

    int16_t previous = 0;
    for (int32_t level = 0; level <= INT16_MAX; ++level) {
        const int16_t positive = decodeMuLaw(encodeMuLaw(static_cast<int16_t>(level)));
        const int16_t negative = decodeMuLaw(encodeMuLaw(static_cast<int16_t>(-level)));
        assert(positive >= previous);
        assert(positive >= 0);
        assert(negative <= 0);
        assert(positive == -negative);
        previous = positive;
    }
    assert(decodeMuLaw(encodeMuLaw(INT16_MIN)) < 0);

    constexpr std::size_t sourceRate       = 16000;
    constexpr std::size_t sourceSize       = sourceRate * frameDurationMs / 1000;
    std::array<int16_t, sourceSize> source = {};
    source.fill(1200);
    const auto encoded = encodeFrame<sourceRate>(source);
    for (const uint8_t sample : encoded) {
        assert(sample == encoded.front());
    }
    constexpr std::size_t playbackRate        = 44100;
    constexpr std::size_t playbackSize        = playbackRate * frameDurationMs / 1000;
    std::array<int16_t, playbackSize> decoded = {};
    decodeFrameInto<playbackRate, playbackSize>(encoded, decoded.data());
    for (const int16_t sample : decoded) {
        assert(sample == decoded.front());
    }

    return 0;
}
