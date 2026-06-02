#include "next/vegetation_world/vegetation_query.h"

#include <cmath>
#include <vector>

namespace Next::vegetation {
namespace {

// Squared XZ distance from point (px,pz) to the segment (ax,az)-(bx,bz).
float PointSegmentDistSq(float px, float pz, float ax, float az, float bx, float bz) {
    const float vx = bx - ax;
    const float vz = bz - az;
    const float wx = px - ax;
    const float wz = pz - az;
    const float vv = vx * vx + vz * vz;
    float t = (vv > 0.0f) ? (wx * vx + wz * vz) / vv : 0.0f;
    if (t < 0.0f) {
        t = 0.0f;
    } else if (t > 1.0f) {
        t = 1.0f;
    }
    const float cx = ax + t * vx;
    const float cz = az + t * vz;
    const float dx = px - cx;
    const float dz = pz - cz;
    return dx * dx + dz * dz;
}

}  // namespace

bool SegmentBlockedByVegetation(const VegetationStore& store, float ax, float az, float bx, float bz) {
    const std::vector<VegetationKey> blockers = store.AllLive(VegBlocksLineOfSight);
    for (const VegetationKey& k : blockers) {
        const VegetationInstance* p = store.Find(k);
        if (p == nullptr || p->logicalRadius <= 0.0f) {
            continue;
        }
        if (PointSegmentDistSq(p->position[0], p->position[2], ax, az, bx, bz) <= p->logicalRadius * p->logicalRadius) {
            return true;
        }
    }
    return false;
}

gameapi::RaycastResult VegetationWorldQuery::Raycast(const float origin[3], const float direction[3],
                                                     float maxDistance) {
    gameapi::RaycastResult result{};
    result.hit = 0;
    result.entity = 0;

    const float dlen =
        std::sqrt(direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2]);

    bool hit = false;
    float best = maxDistance;
    float hx = 0.0f;
    float hy = 0.0f;
    float hz = 0.0f;
    float nx = 0.0f;
    float nz = 0.0f;

    if (store_ != nullptr && dlen > 1e-6f && maxDistance > 0.0f) {
        const float dx = direction[0] / dlen;
        const float dy = direction[1] / dlen;
        const float dz = direction[2] / dlen;

        const std::vector<VegetationKey> blockers = store_->AllLive(VegBlocksLineOfSight);
        for (const VegetationKey& k : blockers) {
            const VegetationInstance* p = store_->Find(k);
            if (p == nullptr || p->logicalRadius <= 0.0f) {
                continue;
            }
            const float cx = p->position[0];
            const float cz = p->position[2];
            const float r = p->logicalRadius;
            const float ox = origin[0] - cx;
            const float oz = origin[2] - cz;

            // Ray vs infinite vertical cylinder = ray vs circle in XZ: a t^2 + b t + c = 0.
            const float a = dx * dx + dz * dz;
            const float b = 2.0f * (ox * dx + oz * dz);
            const float c = ox * ox + oz * oz - r * r;

            float t = -1.0f;
            if (a <= 1e-8f) {
                if (c <= 0.0f) {
                    t = 0.0f;  // moving vertically while already inside the disc
                }
            } else {
                const float disc = b * b - 4.0f * a * c;
                if (disc >= 0.0f) {
                    const float sq = std::sqrt(disc);
                    const float t0 = (-b - sq) / (2.0f * a);
                    const float t1 = (-b + sq) / (2.0f * a);
                    if (t0 >= 0.0f) {
                        t = t0;
                    } else if (t1 >= 0.0f) {
                        t = 0.0f;  // origin already inside the disc
                    }
                }
            }

            if (t >= 0.0f && t <= best) {
                best = t;
                hit = true;
                hx = origin[0] + dx * t;
                hy = origin[1] + dy * t;
                hz = origin[2] + dz * t;
                const float ex = hx - cx;
                const float ez = hz - cz;
                const float nlen = std::sqrt(ex * ex + ez * ez);
                if (nlen > 1e-6f) {
                    nx = ex / nlen;
                    nz = ez / nlen;
                } else {
                    nx = -dx;
                    nz = -dz;
                }
            }
        }
    }

    if (hit) {
        result.hit = 1;
        result.point = {hx, hy, hz};
        result.normal = {nx, 0.0f, nz};
        result.distance = best;
    }

    // Consult the fallback world query (e.g. physics); the nearer hit wins.
    if (fallback_ != nullptr) {
        const gameapi::RaycastResult fb = fallback_->Raycast(origin, direction, maxDistance);
        if (fb.hit != 0 && (result.hit == 0 || fb.distance < result.distance)) {
            result = fb;
        }
    }
    return result;
}

bool DestroyVegetation(VegetationStore& store, const VegetationKey& key, VegetationDestroyedEvent& outEvent) {
    const VegetationInstance* p = store.Find(key);
    if (p == nullptr) {
        return false;
    }
    VegetationDestroyedEvent ev;
    ev.cellX = key.cellX;
    ev.cellZ = key.cellZ;
    ev.instanceId = key.instanceId;
    ev.visual = p->visual;
    ev.position[0] = p->position[0];
    ev.position[1] = p->position[1];
    ev.position[2] = p->position[2];

    if (!store.Remove(key)) {  // enforces destructible + not-already-removed
        return false;
    }
    outEvent = ev;
    return true;
}

}  // namespace Next::vegetation
