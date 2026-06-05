#pragma once

#include <cstdint>
#include <string>
#include <utility>

#include "next/water/water_def.h"
#include "next/water/water_spectrum.h"

// Fluent, designer-facing authoring for a WaterSceneDef (mirrors engine/vegetation's VegetationBuilder).
// AddBody starts a new body; the per-body setters target the MOST RECENTLY added body, so a scene reads
// top-to-bottom. AddWave appends a Gerstner component (direction normalized); AddOceanWaves synthesizes
// a wind-driven set via the spectrum. Take()/Def() hand the result to the validator + cook.

namespace Next::water {

class WaterBuilder {
public:
    explicit WaterBuilder(std::string sceneId, std::string name = "");

    WaterBuilder& AddBody(std::string id, WaterType type);

    WaterBuilder& WithBounds(float minX, float minY, float minZ, float maxX, float maxY, float maxZ);
    WaterBuilder& WithSurfaceHeight(float y);
    WaterBuilder& WithDensity(float density);
    WaterBuilder& WithFlow(float vx, float vy, float vz);
    WaterBuilder& WithDrag(float linear, float quadratic);
    WaterBuilder& WithFlood(float rate, float maxHeight);
    WaterBuilder& WithVisual(uint32_t visual);
    WaterBuilder& WithFlags(uint16_t flags);
    WaterBuilder& SetFlag(WaterFlags flag, bool on = true);

    WaterBuilder& AddWave(float amplitude, float wavelength, float dirX, float dirZ, float speed, float steepness);
    WaterBuilder& AddOceanWaves(const OceanSpectrumParams& params, uint8_t count);

    const WaterSceneDef& Def() const { return scene_; }
    WaterSceneDef Take() { return std::move(scene_); }

private:
    WaterBodyDef& Last();
    WaterSceneDef scene_;
};

}  // namespace Next::water
