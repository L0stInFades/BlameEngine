#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "next/water/water_def.h"

// Total, fail-closed validation of an authored WaterSceneDef (mirrors engine/vegetation's validator
// ethos). The cook NEVER bakes a scene that does not pass. The pass is TOTAL: it accumulates EVERY
// violation (never early-exits) in a byte-stable order, so the report is deterministic and complete.

namespace Next::water {

enum class WaterValidationCode : uint16_t {
    Ok = 0,
    SceneIdEmpty,
    SchemaVersionMismatch,
    NoBodies,
    TooManyBodies,
    BodyIdEmpty,
    BodyIdDuplicate,
    BoundsNotFinite,
    BoundsInverted,  // min > max on some axis
    BoundsTooLarge,  // a bound coordinate magnitude exceeds kMaxWaterCoord (would overflow the broadphase)
    SurfaceNotFinite,
    SurfaceOutsideBounds,  // for bounded bodies, surface must lie within [boundsMin.y, boundsMax.y]
    DensityOutOfRange,     // <= 0 or > kMaxWaterDensity
    FlowNotFinite,
    DragNegative,
    FloodRateNegative,
    FloodMaxBelowSurface,  // Flood body whose floodMaxHeight < surfaceHeight
    TooManyWaves,          // more than kMaxWavesPerBody (cook would silently drop — rejected instead)
    WaveAmplitudeBad,      // not finite, negative, or > kMaxWaveAmplitude
    WaveWavelengthBad,     // not finite or <= 0
    WaveDirectionDegenerate,
    WaveSpeedBad,              // not finite or negative
    WaveSteepnessOutOfRange,   // outside [0,1]
    TotalSteepnessExceedsOne,  // sum of Gerstner Q over a body's waves > 1 => self-intersecting surface
};

const char* ToString(WaterValidationCode code);

struct WaterValidationError {
    WaterValidationCode code = WaterValidationCode::Ok;
    int32_t bodyIndex = -1;  // -1 = scene-level
    int32_t waveIndex = -1;  // -1 = body-level
    std::string detail;
};

struct WaterValidationReport {
    std::vector<WaterValidationError> errors;
    bool Ok() const { return errors.empty(); }
    bool Has(WaterValidationCode code) const {
        for (const auto& e : errors) {
            if (e.code == code) {
                return true;
            }
        }
        return false;
    }
};

class WaterValidator {
public:
    static WaterValidationReport Validate(const WaterSceneDef& scene);
};

}  // namespace Next::water
