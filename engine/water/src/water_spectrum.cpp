#include "next/water/water_spectrum.h"

#include <algorithm>
#include <cmath>

namespace Next::water {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kGravity = 9.81f;

// SplitMix64 — the same deterministic, platform-independent integer stream engine/vegetation uses,
// so wave jitter is reproducible across runs and machines.
uint64_t SplitMix64(uint64_t& state) {
    uint64_t z = (state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30u)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27u)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31u);
}

// Uniform float in [-1, 1).
float NextSigned(uint64_t& state) {
    const float u = static_cast<float>(SplitMix64(state) >> 40u) / static_cast<float>(1u << 24u);  // [0,1)
    return (u * 2.0f) - 1.0f;
}

}  // namespace

std::vector<WaveComponent> GenerateOceanWaves(const OceanSpectrumParams& params, uint8_t count) {
    std::vector<WaveComponent> out;
    const uint8_t n = std::min<uint8_t>(count, kMaxWavesPerBody);
    if (n == 0) {
        return out;
    }

    float wx = params.windDir[0];
    float wz = params.windDir[1];
    float wlen = std::sqrt((wx * wx) + (wz * wz));
    if (wlen < 1e-6f) {
        wx = 1.0f;
        wz = 0.0f;
        wlen = 1.0f;
    }
    wx /= wlen;
    wz /= wlen;
    const float windAngle = std::atan2(wz, wx);
    uint64_t state = (params.seed != 0) ? params.seed : 1;

    // Wind-driven base amplitude (gentle, monotonic in wavelength). Longer waves are taller.
    const float baseAmp = 0.015f * params.windSpeed * std::max(0.0f, params.amplitudeScale);

    std::vector<float> amps(n);
    std::vector<float> wavelengths(n);
    std::vector<float> angles(n);
    float ampSum = 0.0f;
    for (uint8_t i = 0; i < n; ++i) {
        const float u =
            (n == 1) ? 0.0f : ((static_cast<float>(i) / static_cast<float>(n - 1)) * 2.0f - 1.0f);  // [-1,1]
        const float wavelength = params.medianWavelength * std::pow(2.0f, u);                       // [/2,*2]
        const float amp = baseAmp * std::sqrt(wavelength / params.medianWavelength);
        amps[i] = amp;
        wavelengths[i] = wavelength;
        angles[i] = windAngle + (NextSigned(state) * params.directionalSpread);
        ampSum += amp;
    }

    // Split the steepness budget across waves in proportion to amplitude, so sum(Q) == budget <= 1
    // (the closed-form non-self-intersection bound for a Gerstner sum).
    const float budget = std::clamp(params.steepnessBudget, 0.0f, 1.0f);
    out.reserve(n);
    for (uint8_t i = 0; i < n; ++i) {
        WaveComponent w;
        w.wavelength = wavelengths[i];
        const float k = 2.0f * kPi / w.wavelength;
        w.speed = std::sqrt(kGravity / k);  // deep-water dispersion
        w.amplitude = amps[i];
        w.direction[0] = std::cos(angles[i]);
        w.direction[1] = std::sin(angles[i]);
        w.steepness = (ampSum > 0.0f) ? (budget * (amps[i] / ampSum)) : 0.0f;
        out.push_back(w);
    }
    return out;
}

}  // namespace Next::water
