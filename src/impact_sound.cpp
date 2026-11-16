#include "spidy/impact_sound.hpp"
#include "spidy/block_sound.hpp"
#include "spidy/web_block_sound.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace spidy {
namespace {
constexpr float pi = 3.14159265f;
// A blow: the tone falls from bodyFrom to bodyTo hertz and dies away at
// bodyDecay a second; the noise is low-passed at snapTone hertz and dies at
// snapDecay. `drive` presses the sum into a soft clip (the harder the blow,
// the denser), `gain` is its peak of full scale.
struct Shape {
    float seconds, bodyFrom, bodyTo, bodyDecay, bodyGain, snapTone, snapDecay, snapGain, drive, gain;
};
constexpr Shape shapes[] = {
    {.12f, 190, 70, 28, .7f, 3500, 110, .5f, 1.2f, .42f},
    {.16f, 160, 55, 22, .9f, 3000, 90, .6f, 1.6f, .58f},
    {.24f, 130, 42, 15, 1.f, 2400, 70, .7f, 2.2f, .74f},
    {.42f, 110, 34, 9, 1.f, 2000, 45, .8f, 2.8f, .86f},
};
constexpr float rushSeconds = .22f, rushGain = .2f;
// The guards' sounds (ImpactSound::BlockBaton .. WebBlockShield) are sound effects from Pixabay
// (block_sound.hpp and web_block_sound.hpp), set to one loudness there, and they play at one level here:
// blockLevel, a factor on the samples, 10^(-5/20), 5 dB under the files' own level.
constexpr float blockLevel = .562f;
// The brute's refused web pull (slayerwebblock.mp3) alone plays 3 dB lower: this factor on blockLevel,
// 10^(-3/20) = .708, so it sits 8 dB under the files' own level, the others 5 dB. (The file has more high
// frequencies than the others, which K-weighted loudness counts for little: it was matched to them on that
// measure, and is the one that sounded loud.)
constexpr float webBlockSlayerTrim = .708f;
constexpr float trimOf(ImpactSound sound) {
    return sound == ImpactSound::WebBlockSlayer ? webBlockSlayerTrim : 1.f;
}
static_assert(blockWaveRate == blockSoundRate,
              "impact_sound.hpp's blockWaveRate is block_sound.hpp's blockSoundRate");
static_assert(blockWaveRate == webBlockSoundRate,
              "impact_sound.hpp's blockWaveRate is web_block_sound.hpp's webBlockSoundRate");
struct Recorded {
    const std::int16_t* samples;
    size_t count;
};
// In the order of ImpactSound::BlockBaton .. WebBlockShield (recordedFor indexes it by the difference). A
// swordsman's refused web pull plays the sword's block sound.
constexpr Recorded recorded[] = {
    {blockBatonSound, sizeof(blockBatonSound) / sizeof(blockBatonSound[0])},
    {blockSwordSound, sizeof(blockSwordSound) / sizeof(blockSwordSound[0])},
    {blockSlayerSound, sizeof(blockSlayerSound) / sizeof(blockSlayerSound[0])},
    {blockShieldSound, sizeof(blockShieldSound) / sizeof(blockShieldSound[0])},
    {webBlockBatonSound, sizeof(webBlockBatonSound) / sizeof(webBlockBatonSound[0])},
    {blockSwordSound, sizeof(blockSwordSound) / sizeof(blockSwordSound[0])},
    {webBlockSlayerSound, sizeof(webBlockSlayerSound) / sizeof(webBlockSlayerSound[0])},
    {webBlockShieldSound, sizeof(webBlockShieldSound) / sizeof(webBlockShieldSound[0])},
};
static_assert(sizeof(recorded) / sizeof(recorded[0]) == static_cast<size_t>(ImpactSound::WebBlockShield) -
                                                            static_cast<size_t>(ImpactSound::BlockBaton) + 1,
              "one recorded sound for each of ImpactSound::BlockBaton .. WebBlockShield");
const Recorded& recordedFor(ImpactSound sound) {
    return recorded[static_cast<unsigned>(sound) - static_cast<unsigned>(ImpactSound::BlockBaton)];
}
// Uniform noise in -1..1, the same every time.
struct Noise {
    std::uint32_t state = 0x2545f491;
    float next() {
        state = state * 1664525u + 1013904223u;
        return static_cast<float>(state >> 8) / 8388608.f - 1;
    }
};
} // namespace

float impactSeconds(ImpactSound sound) {
    return sound == ImpactSound::Rush ? rushSeconds
           : isBlockSound(sound)
               ? static_cast<float>(recordedFor(sound).count) / static_cast<float>(blockSoundRate)
               : shapes[static_cast<unsigned>(sound) & 3].seconds;
}
std::vector<std::uint8_t> impactWave(ImpactSound sound, unsigned rate) {
    rate = std::clamp(rate, 8000u, 48000u);
    const float seconds = impactSeconds(sound);
    const auto count = static_cast<std::uint32_t>(std::lround(seconds * static_cast<float>(rate)));
    std::vector<float> samples(count);
    Noise noise;
    if (isBlockSound(sound)) {
        // The recorded sound, read at its own rate and laid out at the one asked for: linear between its
        // samples, and at a lower rate each sample is the mean of the stretch of the original it stands for
        // (no aliasing).
        const Recorded& r = recordedFor(sound);
        const double step = static_cast<double>(blockSoundRate) / static_cast<double>(rate);
        const int taps = std::max(1, static_cast<int>(std::lround(step)));
        const auto at = [&](double position) {
            const size_t low = std::min(static_cast<size_t>(std::max(position, 0.)), r.count - 1);
            const size_t high = std::min(low + 1, r.count - 1);
            const float mix = std::clamp(static_cast<float>(position - static_cast<double>(low)), 0.f, 1.f);
            return static_cast<float>(r.samples[low]) * (1 - mix) + static_cast<float>(r.samples[high]) * mix;
        };
        for (std::uint32_t i = 0; i < count; ++i) {
            float sum = 0;
            for (int k = 0; k < taps; ++k)
                sum += at(static_cast<double>(i) * step + step * (static_cast<double>(k) + .5) / taps -
                          step * .5);
            samples[i] = sum / static_cast<float>(taps) / 32768.f * blockLevel * trimOf(sound);
        }
    } else if (sound == ImpactSound::Rush) {
        // Noise through a band that rises and falls back, swelling and fading.
        float low{}, band{};
        for (std::uint32_t i = 0; i < count; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(count);
            const float centre = 500 + 1100 * std::sin(pi * t);
            const float f = 2 * std::sin(pi * centre / static_cast<float>(rate));
            const float high = noise.next() - low - .7f * band;
            band += f * high;
            low += f * band;
            const float swell = std::sin(pi * t);
            samples[i] = band * swell * swell * rushGain;
        }
    } else {
        const auto& s = shapes[static_cast<unsigned>(sound) & 3];
        const float smooth = 1 - std::exp(-2 * pi * s.snapTone / static_cast<float>(rate));
        const float ceiling = std::tanh(s.drive);
        float phase{}, snap{};
        for (std::uint32_t i = 0; i < count; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(rate);
            phase += 2 * pi * (s.bodyTo + (s.bodyFrom - s.bodyTo) * std::exp(-t * 18)) / static_cast<float>(rate);
            snap += (noise.next() - snap) * smooth;
            const float sum = std::sin(phase) * std::exp(-t * s.bodyDecay) * s.bodyGain +
                              snap * std::exp(-t * s.snapDecay) * s.snapGain;
            samples[i] = std::tanh(sum * s.drive) / ceiling * s.gain;
        }
    }
    // No click at either end: half a millisecond in, five out.
    const auto in = std::max<std::uint32_t>(1, rate / 2000), out = std::max<std::uint32_t>(1, rate / 200);
    for (std::uint32_t i = 0; i < count; ++i) {
        const float fade = std::min({1.f, static_cast<float>(i) / static_cast<float>(in),
                                     static_cast<float>(count - 1 - i) / static_cast<float>(out)});
        samples[i] *= fade;
    }
    std::vector<std::uint8_t> wave(44 + count * 2);
    const auto put = [&](size_t at, std::uint32_t value, unsigned bytes) {
        for (unsigned b = 0; b < bytes; ++b)
            wave[at + b] = static_cast<std::uint8_t>(value >> (8 * b));
    };
    std::memcpy(wave.data(), "RIFF", 4);
    put(4, 36 + count * 2, 4);
    std::memcpy(wave.data() + 8, "WAVEfmt ", 8);
    put(16, 16, 4);       // the format chunk's size
    put(20, 1, 2);        // PCM
    put(22, 1, 2);        // mono
    put(24, rate, 4);
    put(28, rate * 2, 4); // bytes a second
    put(32, 2, 2);        // bytes a sample
    put(34, 16, 2);       // bits a sample
    std::memcpy(wave.data() + 36, "data", 4);
    put(40, count * 2, 4);
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto value = static_cast<std::int16_t>(std::lround(std::clamp(samples[i], -1.f, 1.f) * 32767));
        put(44 + i * 2ull, static_cast<std::uint16_t>(value), 2);
    }
    return wave;
}
} // namespace spidy
