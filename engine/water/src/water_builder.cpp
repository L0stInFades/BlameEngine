#include "next/water/water_builder.h"

#include <cmath>

namespace Next::water {

WaterBuilder::WaterBuilder(std::string sceneId, std::string name) {
    scene_.id = std::move(sceneId);
    scene_.name = std::move(name);
    scene_.schemaVersion = kWaterSchemaVersion;
}

WaterBodyDef& WaterBuilder::Last() {
    if (scene_.bodies.empty()) {
        scene_.bodies.emplace_back();  // defensive: a setter before AddBody targets a dummy (validator rejects)
    }
    return scene_.bodies.back();
}

WaterBuilder& WaterBuilder::AddBody(std::string id, WaterType type) {
    WaterBodyDef body;
    body.id = std::move(id);
    body.type = type;
    scene_.bodies.push_back(std::move(body));
    return *this;
}

WaterBuilder& WaterBuilder::WithBounds(float minX, float minY, float minZ, float maxX, float maxY, float maxZ) {
    WaterBodyDef& b = Last();
    b.boundsMin[0] = minX;
    b.boundsMin[1] = minY;
    b.boundsMin[2] = minZ;
    b.boundsMax[0] = maxX;
    b.boundsMax[1] = maxY;
    b.boundsMax[2] = maxZ;
    return *this;
}

WaterBuilder& WaterBuilder::WithSurfaceHeight(float y) {
    Last().surfaceHeight = y;
    return *this;
}

WaterBuilder& WaterBuilder::WithDensity(float density) {
    Last().density = density;
    return *this;
}

WaterBuilder& WaterBuilder::WithFlow(float vx, float vy, float vz) {
    WaterBodyDef& b = Last();
    b.flowVelocity[0] = vx;
    b.flowVelocity[1] = vy;
    b.flowVelocity[2] = vz;
    b.flags |= WaterCurrent;  // declaring a flow implies the current acts
    return *this;
}

WaterBuilder& WaterBuilder::WithDrag(float linear, float quadratic) {
    WaterBodyDef& b = Last();
    b.linearDrag = linear;
    b.quadraticDrag = quadratic;
    return *this;
}

WaterBuilder& WaterBuilder::WithFlood(float rate, float maxHeight) {
    WaterBodyDef& b = Last();
    b.floodRate = rate;
    b.floodMaxHeight = maxHeight;
    return *this;
}

WaterBuilder& WaterBuilder::WithVisual(uint32_t visual) {
    Last().visual = visual;
    return *this;
}

WaterBuilder& WaterBuilder::WithFlags(uint16_t flags) {
    Last().flags = flags;
    return *this;
}

WaterBuilder& WaterBuilder::SetFlag(WaterFlags flag, bool on) {
    WaterBodyDef& b = Last();
    if (on) {
        b.flags = static_cast<uint16_t>(b.flags | flag);
    } else {
        b.flags = static_cast<uint16_t>(b.flags & ~static_cast<uint16_t>(flag));
    }
    return *this;
}

WaterBuilder& WaterBuilder::AddWave(float amplitude, float wavelength, float dirX, float dirZ, float speed,
                                    float steepness) {
    WaterBodyDef& b = Last();
    WaveComponent w;
    w.amplitude = amplitude;
    w.wavelength = wavelength;
    const float len = std::sqrt((dirX * dirX) + (dirZ * dirZ));
    if (len > 1e-8f) {
        w.direction[0] = dirX / len;  // surface math assumes a unit travel direction
        w.direction[1] = dirZ / len;
    } else {
        w.direction[0] = 1.0f;
        w.direction[1] = 0.0f;
    }
    w.speed = speed;
    w.steepness = steepness;
    b.waves.push_back(w);
    return *this;
}

WaterBuilder& WaterBuilder::AddOceanWaves(const OceanSpectrumParams& params, uint8_t count) {
    WaterBodyDef& b = Last();
    const std::vector<WaveComponent> waves = GenerateOceanWaves(params, count);
    for (const WaveComponent& w : waves) {
        b.waves.push_back(w);
    }
    return *this;
}

}  // namespace Next::water
