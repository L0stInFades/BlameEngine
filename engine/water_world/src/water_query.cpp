#include "next/water_world/water_query.h"

#include <algorithm>
#include <cmath>

#include "next/water/water_surface.h"

namespace Next::water {

namespace gameapi = Next::gameapi;

gameapi::RaycastResult WaterWorldQuery::Raycast(const float origin[3], const float direction[3], float maxDistance) {
    gameapi::RaycastResult result{};
    result.hit = 0;
    result.entity = 0;
    const double simTime = (clock_ != nullptr) ? clock_->seconds : 0.0;  // shared authoritative time (0 if none)

    const float dlen =
        std::sqrt((direction[0] * direction[0]) + (direction[1] * direction[1]) + (direction[2] * direction[2]));

    bool hit = false;
    float best = maxDistance;
    float hx = 0.0f;
    float hy = 0.0f;
    float hz = 0.0f;
    float nx = 0.0f;
    float ny = 1.0f;
    float nz = 0.0f;

    if (store_ != nullptr && dlen > 1e-6f && maxDistance > 0.0f) {
        const float dx = direction[0] / dlen;
        const float dy = direction[1] / dlen;
        const float dz = direction[2] / dlen;
        const float ex = origin[0] + (dx * maxDistance);
        const float ez = origin[2] + (dz * maxDistance);
        const std::vector<uint32_t> candidates = store_->BodiesOverlappingAabb(
            std::min(origin[0], ex), std::min(origin[2], ez), std::max(origin[0], ex), std::max(origin[2], ez));

        for (const uint32_t id : candidates) {
            const WaterBodyInstance* b = store_->Find(id);
            if (b == nullptr || std::fabs(dy) < 1e-6f) {
                continue;  // ray ~parallel to the surface: no clean intersection
            }
            // Intersect with the still-water plane, then refine once against the wavy height at the hit.
            float planeY = EffectiveSurfaceHeight(*b, simTime);
            float t = (planeY - origin[1]) / dy;
            if (t < 0.0f || t > best) {
                continue;
            }
            float px = origin[0] + (dx * t);
            float pz = origin[2] + (dz * t);
            planeY = SampleHeightFast(*b, px, pz, simTime);
            t = (planeY - origin[1]) / dy;
            if (t < 0.0f || t > best) {
                continue;
            }
            px = origin[0] + (dx * t);
            pz = origin[2] + (dz * t);
            if (!WaterBodyContainsXZ(*b, px, pz)) {
                continue;
            }
            best = t;
            hit = true;
            hx = px;
            hy = origin[1] + (dy * t);
            hz = pz;
            const SurfaceSample s = SurfaceSampleAt(*b, px, pz, simTime);
            nx = s.normal[0];
            ny = s.normal[1];
            nz = s.normal[2];
        }
    }

    if (hit) {
        result.hit = 1;
        result.point = {hx, hy, hz};
        result.normal = {nx, ny, nz};
        result.distance = best;
    }

    // Consult the fallback world query (e.g. physics / vegetation); the nearer hit wins.
    if (fallback_ != nullptr) {
        const gameapi::RaycastResult fb = fallback_->Raycast(origin, direction, maxDistance);
        if (fb.hit != 0 && (result.hit == 0 || fb.distance < result.distance)) {
            result = fb;
        }
    }
    return result;
}

gameapi::WaterStateResult WaterWorldQuery::QueryWater(const float point[3]) {
    gameapi::WaterStateResult out{};  // zero-initialized: inWater=0 and all fields 0 when no water here
    if (store_ == nullptr) {
        return out;
    }
    const double simTime = (clock_ != nullptr) ? clock_->seconds : 0.0;  // same authoritative time buoyancy uses
    WaterSample s;
    if (!store_->SampleWaterAt(point[0], point[1], point[2], simTime, s)) {
        return out;  // no water body governs this XZ
    }
    out.inWater = 1u;  // a body governs this XZ (the point may still be above the surface)
    out.submerged = s.submerged ? 1u : 0u;
    out.flags = s.flags;
    out.surfaceHeight = s.surfaceHeight;
    // Honor the ABI contract (abi.h: ">0 when submerged; <=0 otherwise"). The raw store value is
    // surfaceHeight - y, which is large POSITIVE for a point below the body floor (submerged==0) — that
    // would mislead a guest branching on depth>0. Clamp the not-submerged case to <=0 (keeps the
    // "how far above the surface" negative value, kills the spurious below-floor positive).
    out.submersionDepth = s.submerged ? s.submersionDepth : std::min(s.submersionDepth, 0.0f);
    out.flowVelocity = {s.flowVelocity[0], s.flowVelocity[1], s.flowVelocity[2]};
    return out;
}

float WaterHeightAt(const WaterStore& store, float x, float z, double timeSeconds, bool& outFound) {
    const WaterBodyInstance* b = store.BodyAt(x, z, timeSeconds);
    outFound = (b != nullptr);
    return (b != nullptr) ? SampleHeightFast(*b, x, z, timeSeconds) : 0.0f;
}

float SubmersionDepthAt(const WaterStore& store, float x, float y, float z, double timeSeconds) {
    WaterSample s;
    if (!store.SampleWaterAt(x, y, z, timeSeconds, s) || !s.submerged) {
        return 0.0f;
    }
    return s.submersionDepth;
}

bool IsSubmergedAt(const WaterStore& store, float x, float y, float z, double timeSeconds) {
    return store.IsSubmerged(x, y, z, timeSeconds);
}

bool IsHiddenBySubmersion(const WaterStore& store, float x, float y, float z, double timeSeconds) {
    WaterSample s;
    return store.SampleWaterAt(x, y, z, timeSeconds, s) && s.submerged && (s.flags & WaterBreaksSight) != 0;
}

bool IsInConductiveWater(const WaterStore& store, float x, float y, float z, double timeSeconds) {
    WaterSample s;
    return store.SampleWaterAt(x, y, z, timeSeconds, s) && s.submerged && (s.flags & WaterConductive) != 0;
}

}  // namespace Next::water
