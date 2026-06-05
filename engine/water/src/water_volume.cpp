#include "next/water/water_volume.h"

#include <algorithm>
#include <cmath>

#include "next/water/water_surface.h"

namespace Next::water {
namespace {

constexpr float kPi = 3.14159265358979323846f;

// Volume of the region of a sphere (radius r) below a horizontal plane sitting height `h` above the
// sphere's lowest point. Valid for any h (clamped to [0,2r]): 0 -> empty, r -> hemisphere, 2r -> full.
// V = pi * h^2 * (3r - h) / 3.
float SphereCapVolume(float r, float h) {
    const float hc = std::clamp(h, 0.0f, 2.0f * r);
    return (kPi * hc * hc * ((3.0f * r) - hc)) / 3.0f;
}

}  // namespace

float SubmergedSphereVolume(float centerY, float radius, float surfaceY, float floorY, float& outFraction) {
    outFraction = 0.0f;
    if (radius <= 0.0f) {
        return 0.0f;
    }
    const float bottom = centerY - radius;
    // Region of the sphere between the floor and the surface = cap(below surface) - cap(below floor).
    const float submerged = SphereCapVolume(radius, surfaceY - bottom) - SphereCapVolume(radius, floorY - bottom);
    const float total = (4.0f / 3.0f) * kPi * radius * radius * radius;
    const float v = std::max(0.0f, submerged);
    outFraction = (total > 0.0f) ? std::clamp(v / total, 0.0f, 1.0f) : 0.0f;
    return v;
}

float SubmergedBoxVolume(float centerY, const float halfExtents[3], float surfaceY, float floorY, float& outFraction) {
    outFraction = 0.0f;
    const float hx = halfExtents[0];
    const float hy = halfExtents[1];
    const float hz = halfExtents[2];
    if (hx <= 0.0f || hy <= 0.0f || hz <= 0.0f) {
        return 0.0f;
    }
    const float bottom = centerY - hy;
    const float top = centerY + hy;
    const float low = std::max(bottom, floorY);  // water region floor
    const float high = std::min(top, surfaceY);  // water region top
    const float submergedHeight = std::clamp(high - low, 0.0f, 2.0f * hy);
    outFraction = std::clamp(submergedHeight / (2.0f * hy), 0.0f, 1.0f);
    return outFraction * (8.0f * hx * hy * hz);
}

float SubmersionDepth(const WaterBodyInstance& body, float x, float y, float z, double timeSeconds) {
    if (!WaterBodyContainsXZ(body, x, z)) {
        return 0.0f;
    }
    if (y < body.boundsMin[1]) {
        return 0.0f;  // below the container floor: not in this water
    }
    const float surfaceY = SurfaceHeightAt(body, x, z, timeSeconds);
    return surfaceY - y;  // > 0 when below the surface
}

bool IsPointSubmerged(const WaterBodyInstance& body, float x, float y, float z, double timeSeconds) {
    return SubmersionDepth(body, x, y, z, timeSeconds) > 0.0f;
}

}  // namespace Next::water
