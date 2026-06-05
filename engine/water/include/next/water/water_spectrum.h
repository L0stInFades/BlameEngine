#pragma once

#include <cstdint>
#include <vector>

#include "next/water/water_def.h"

// Deterministic ocean-wave synthesis. Turns a wind description into a small set of Gerstner waves a
// designer can drop on an Ocean body, instead of hand-tuning amplitudes/phases. Real model: deep-water
// dispersion (phase speed c = sqrt(g/k)), wavelengths spread geometrically around a median, directions
// spread around the wind, and a steepness BUDGET split across waves so the summed surface never
// self-intersects (sum of Gerstner Q <= 1). Same seed + params => identical waves (byte-stable cook).

namespace Next::water {

struct OceanSpectrumParams {
    float windDir[2] = {1.0f, 0.0f};  // XZ wind direction (normalized internally)
    float windSpeed = 12.0f;          // m/s; scales amplitude
    float medianWavelength = 24.0f;   // m; center of the wavelength band [median/2, median*2]
    float directionalSpread = 0.5f;   // radians; half-spread of wave directions around the wind
    float steepnessBudget = 0.75f;    // total Gerstner steepness in [0,1] split across the waves
    float amplitudeScale = 1.0f;      // designer multiplier on the wind-derived amplitude
    uint64_t seed = 1;                // deterministic jitter seed
};

// Synthesize up to `count` (clamped to kMaxWavesPerBody) Gerstner waves. Deterministic.
std::vector<WaveComponent> GenerateOceanWaves(const OceanSpectrumParams& params, uint8_t count);

}  // namespace Next::water
