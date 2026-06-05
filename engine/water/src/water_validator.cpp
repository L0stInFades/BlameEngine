#include "next/water/water_validator.h"

#include <cmath>
#include <string>
#include <unordered_set>

namespace Next::water {

const char* ToString(WaterValidationCode code) {
    switch (code) {
        case WaterValidationCode::Ok:
            return "Ok";
        case WaterValidationCode::SceneIdEmpty:
            return "SceneIdEmpty";
        case WaterValidationCode::SchemaVersionMismatch:
            return "SchemaVersionMismatch";
        case WaterValidationCode::NoBodies:
            return "NoBodies";
        case WaterValidationCode::TooManyBodies:
            return "TooManyBodies";
        case WaterValidationCode::BodyIdEmpty:
            return "BodyIdEmpty";
        case WaterValidationCode::BodyIdDuplicate:
            return "BodyIdDuplicate";
        case WaterValidationCode::BoundsNotFinite:
            return "BoundsNotFinite";
        case WaterValidationCode::BoundsInverted:
            return "BoundsInverted";
        case WaterValidationCode::BoundsTooLarge:
            return "BoundsTooLarge";
        case WaterValidationCode::SurfaceNotFinite:
            return "SurfaceNotFinite";
        case WaterValidationCode::SurfaceOutsideBounds:
            return "SurfaceOutsideBounds";
        case WaterValidationCode::DensityOutOfRange:
            return "DensityOutOfRange";
        case WaterValidationCode::FlowNotFinite:
            return "FlowNotFinite";
        case WaterValidationCode::DragNegative:
            return "DragNegative";
        case WaterValidationCode::FloodRateNegative:
            return "FloodRateNegative";
        case WaterValidationCode::FloodMaxBelowSurface:
            return "FloodMaxBelowSurface";
        case WaterValidationCode::TooManyWaves:
            return "TooManyWaves";
        case WaterValidationCode::WaveAmplitudeBad:
            return "WaveAmplitudeBad";
        case WaterValidationCode::WaveWavelengthBad:
            return "WaveWavelengthBad";
        case WaterValidationCode::WaveDirectionDegenerate:
            return "WaveDirectionDegenerate";
        case WaterValidationCode::WaveSpeedBad:
            return "WaveSpeedBad";
        case WaterValidationCode::WaveSteepnessOutOfRange:
            return "WaveSteepnessOutOfRange";
        case WaterValidationCode::TotalSteepnessExceedsOne:
            return "TotalSteepnessExceedsOne";
    }
    return "Unknown";
}

namespace {

bool Finite1(float v) {
    return std::isfinite(v);
}
bool Finite3(const float v[3]) {
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}
bool IsBoundedType(WaterType t) {
    return t != WaterType::Ocean;  // the open sea may sit at any Y; bounded bodies must contain it
}

}  // namespace

WaterValidationReport WaterValidator::Validate(const WaterSceneDef& scene) {
    WaterValidationReport report;
    auto fail = [&report](WaterValidationCode code, int32_t bodyIndex, int32_t waveIndex, std::string detail) {
        report.errors.push_back({code, bodyIndex, waveIndex, std::move(detail)});
    };

    if (scene.id.empty()) {
        fail(WaterValidationCode::SceneIdEmpty, -1, -1, "scene id must be non-empty");
    }
    if (scene.schemaVersion != kWaterSchemaVersion) {
        fail(WaterValidationCode::SchemaVersionMismatch, -1, -1,
             "schemaVersion=" + std::to_string(scene.schemaVersion));
    }
    if (scene.bodies.empty()) {
        fail(WaterValidationCode::NoBodies, -1, -1, "scene declares no water bodies");
    }
    if (scene.bodies.size() > kMaxWaterBodiesPerScene) {
        fail(WaterValidationCode::TooManyBodies, -1, -1, std::to_string(scene.bodies.size()));
    }

    std::unordered_set<std::string> ids;
    for (size_t i = 0; i < scene.bodies.size(); ++i) {
        const WaterBodyDef& b = scene.bodies[i];
        const int32_t bi = static_cast<int32_t>(i);

        if (b.id.empty()) {
            fail(WaterValidationCode::BodyIdEmpty, bi, -1, "body id must be non-empty");
        } else if (!ids.insert(b.id).second) {
            fail(WaterValidationCode::BodyIdDuplicate, bi, -1, b.id);
        }

        const bool boundsFinite = Finite3(b.boundsMin) && Finite3(b.boundsMax);
        if (!boundsFinite) {
            fail(WaterValidationCode::BoundsNotFinite, bi, -1, "bounds contain a non-finite value");
        } else {
            for (int a = 0; a < 3; ++a) {
                if (b.boundsMin[a] > b.boundsMax[a]) {
                    fail(WaterValidationCode::BoundsInverted, bi, -1, "axis " + std::to_string(a) + " min > max");
                    break;
                }
            }
            for (int a = 0; a < 3; ++a) {
                if (std::fabs(b.boundsMin[a]) > kMaxWaterCoord || std::fabs(b.boundsMax[a]) > kMaxWaterCoord) {
                    fail(WaterValidationCode::BoundsTooLarge, bi, -1,
                         "axis " + std::to_string(a) + " bound magnitude exceeds kMaxWaterCoord");
                    break;
                }
            }
        }

        if (!Finite1(b.surfaceHeight)) {
            fail(WaterValidationCode::SurfaceNotFinite, bi, -1, "surfaceHeight not finite");
        } else if (IsBoundedType(b.type) && boundsFinite &&
                   (b.surfaceHeight < b.boundsMin[1] - 1e-3f || b.surfaceHeight > b.boundsMax[1] + 1e-3f)) {
            fail(WaterValidationCode::SurfaceOutsideBounds, bi, -1, "surfaceHeight outside [boundsMin.y, boundsMax.y]");
        }

        if (!Finite1(b.density) || b.density <= 0.0f || b.density > kMaxWaterDensity) {
            fail(WaterValidationCode::DensityOutOfRange, bi, -1, std::to_string(b.density));
        }
        if (!Finite3(b.flowVelocity)) {
            fail(WaterValidationCode::FlowNotFinite, bi, -1, "flowVelocity not finite");
        }
        if (!Finite1(b.linearDrag) || !Finite1(b.quadraticDrag) || b.linearDrag < 0.0f || b.quadraticDrag < 0.0f) {
            fail(WaterValidationCode::DragNegative, bi, -1, "linear/quadratic drag must be finite and >= 0");
        }
        if (!Finite1(b.floodRate) || b.floodRate < 0.0f) {
            fail(WaterValidationCode::FloodRateNegative, bi, -1, std::to_string(b.floodRate));
        }
        if (b.type == WaterType::Flood && Finite1(b.surfaceHeight) && Finite1(b.floodMaxHeight) &&
            b.floodMaxHeight < b.surfaceHeight) {
            fail(WaterValidationCode::FloodMaxBelowSurface, bi, -1, "floodMaxHeight < surfaceHeight");
        }

        if (b.waves.size() > kMaxWavesPerBody) {
            fail(WaterValidationCode::TooManyWaves, bi, -1,
                 std::to_string(b.waves.size()) + " > " + std::to_string(kMaxWavesPerBody));
        }

        float steepnessSum = 0.0f;
        for (size_t w = 0; w < b.waves.size(); ++w) {
            const WaveComponent& wv = b.waves[w];
            const int32_t wi = static_cast<int32_t>(w);
            if (!Finite1(wv.amplitude) || wv.amplitude < 0.0f || wv.amplitude > kMaxWaveAmplitude) {
                fail(WaterValidationCode::WaveAmplitudeBad, bi, wi, std::to_string(wv.amplitude));
            }
            if (!Finite1(wv.wavelength) || wv.wavelength <= 0.0f) {
                fail(WaterValidationCode::WaveWavelengthBad, bi, wi, std::to_string(wv.wavelength));
            }
            const float dl = (wv.direction[0] * wv.direction[0]) + (wv.direction[1] * wv.direction[1]);
            if (!Finite1(wv.direction[0]) || !Finite1(wv.direction[1]) || dl < 1e-12f) {
                fail(WaterValidationCode::WaveDirectionDegenerate, bi, wi, "zero-length wave direction");
            }
            if (!Finite1(wv.speed) || wv.speed < 0.0f) {
                fail(WaterValidationCode::WaveSpeedBad, bi, wi, std::to_string(wv.speed));
            }
            if (!Finite1(wv.steepness) || wv.steepness < 0.0f || wv.steepness > 1.0f) {
                fail(WaterValidationCode::WaveSteepnessOutOfRange, bi, wi, std::to_string(wv.steepness));
            } else {
                steepnessSum += wv.steepness;
            }
        }
        if (steepnessSum > 1.0f + 1e-4f) {
            fail(WaterValidationCode::TotalSteepnessExceedsOne, bi, -1,
                 "sum(steepness)=" + std::to_string(steepnessSum) + " (surface would self-intersect)");
        }
    }

    return report;
}

}  // namespace Next::water
