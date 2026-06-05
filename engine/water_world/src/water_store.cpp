#include "next/water_world/water_store.h"

#include <algorithm>
#include <cmath>

#include "next/water/water_cell.h"
#include "next/water/water_surface.h"

namespace Next::water {
namespace {

// A bounded body indexes into at most this many grid buckets; anything larger (or an Ocean) is tracked
// as a "global" candidate instead, so an unbounded sea never smears across millions of buckets.
constexpr int64_t kMaxGridCellsPerBody = 1024;

bool AabbOverlapXZ(const WaterBodyInstance& b, float minX, float minZ, float maxX, float maxZ) {
    return b.boundsMin[0] <= maxX && b.boundsMax[0] >= minX && b.boundsMin[2] <= maxZ && b.boundsMax[2] >= minZ;
}

}  // namespace

WaterStore::WaterStore(float broadphaseCellSize)
    : broadphaseCellSize_(broadphaseCellSize > 0.0f ? broadphaseCellSize : 16.0f) {}

int32_t WaterStore::BucketCoord(float v) const {
    // Saturating float->int: a far-from-origin (or garbage) coordinate must never overflow the int32
    // cast (UB). Real content is bounded by the validator (kMaxWaterCoord); this is belt-and-suspenders
    // for bodies fed straight into the store (tests/tools bypassing the cook).
    const double g = std::floor(static_cast<double>(v) / static_cast<double>(broadphaseCellSize_));
    if (g < -2.0e9) {
        return -2000000000;
    }
    if (g > 2.0e9) {
        return 2000000000;
    }
    return static_cast<int32_t>(g);
}

bool WaterStore::IsLargeBody(const WaterBodyInstance& b) const {
    if (static_cast<WaterType>(b.type) == WaterType::Ocean) {
        return true;
    }
    const float spanX = b.boundsMax[0] - b.boundsMin[0];
    const float spanZ = b.boundsMax[2] - b.boundsMin[2];
    if (!(spanX >= 0.0f) || !(spanZ >= 0.0f)) {
        return true;  // degenerate/non-finite -> don't try to grid it
    }
    // Compute the cell-count product in DOUBLE and compare before any integer cast: a huge (but finite)
    // span would overflow an int64 cast (UB). double comfortably represents the product and the compare
    // is well-defined for any finite span.
    const double cellsX = (static_cast<double>(spanX) / static_cast<double>(broadphaseCellSize_)) + 1.0;
    const double cellsZ = (static_cast<double>(spanZ) / static_cast<double>(broadphaseCellSize_)) + 1.0;
    return (cellsX * cellsZ) > static_cast<double>(kMaxGridCellsPerBody);
}

void WaterStore::IndexBody(uint32_t id) {
    const WaterBodyInstance& b = bodies_.at(id);
    if (IsLargeBody(b)) {
        const auto it = std::lower_bound(globalBodies_.begin(), globalBodies_.end(), id);
        if (it == globalBodies_.end() || *it != id) {
            globalBodies_.insert(it, id);
        }
        return;
    }
    const int32_t gx0 = BucketCoord(b.boundsMin[0]);
    const int32_t gx1 = BucketCoord(b.boundsMax[0]);
    const int32_t gz0 = BucketCoord(b.boundsMin[2]);
    const int32_t gz1 = BucketCoord(b.boundsMax[2]);
    for (int32_t gx = gx0; gx <= gx1; ++gx) {
        for (int32_t gz = gz0; gz <= gz1; ++gz) {
            grid_[GridKey{gx, gz}].push_back(id);
        }
    }
}

void WaterStore::DeindexBody(uint32_t id) {
    const WaterBodyInstance& b = bodies_.at(id);
    if (IsLargeBody(b)) {
        const auto it = std::lower_bound(globalBodies_.begin(), globalBodies_.end(), id);
        if (it != globalBodies_.end() && *it == id) {
            globalBodies_.erase(it);
        }
        return;
    }
    const int32_t gx0 = BucketCoord(b.boundsMin[0]);
    const int32_t gx1 = BucketCoord(b.boundsMax[0]);
    const int32_t gz0 = BucketCoord(b.boundsMin[2]);
    const int32_t gz1 = BucketCoord(b.boundsMax[2]);
    for (int32_t gx = gx0; gx <= gx1; ++gx) {
        for (int32_t gz = gz0; gz <= gz1; ++gz) {
            const auto bit = grid_.find(GridKey{gx, gz});
            if (bit == grid_.end()) {
                continue;
            }
            std::vector<uint32_t>& bucket = bit->second;
            bucket.erase(std::remove(bucket.begin(), bucket.end(), id), bucket.end());
            if (bucket.empty()) {
                grid_.erase(bit);
            }
        }
    }
}

void WaterStore::RemoveCellContribution(const CellKey& key) {
    const auto it = cellBodies_.find(key);
    if (it == cellBodies_.end()) {
        return;
    }
    for (const uint32_t id : it->second) {
        const auto rit = refs_.find(id);
        if (rit == refs_.end()) {
            continue;
        }
        if (--rit->second <= 0) {
            DeindexBody(id);  // reads bodies_.at(id) — must precede the erase
            bodies_.erase(id);
            refs_.erase(rit);
        }
    }
    cellBodies_.erase(it);
}

bool WaterStore::LoadCell(int32_t cellX, int32_t cellZ, const uint8_t* waterBlob, size_t size) {
    WaterCellData parsed;
    if (!UnpackCell(waterBlob, size, parsed)) {
        return false;  // fail-closed
    }
    const CellKey key{cellX, cellZ};
    RemoveCellContribution(key);  // reload-safe: drop any previous contribution first

    std::vector<uint32_t> ids;
    ids.reserve(parsed.bodies.size());
    for (const WaterBodyInstance& body : parsed.bodies) {
        const uint32_t id = body.bodyId;
        if (id == kInvalidWaterBody) {
            continue;  // unkeyed body (shouldn't happen post-cook); skip rather than collide on 0
        }
        ids.push_back(id);
        if (bodies_.find(id) == bodies_.end()) {
            bodies_[id] = body;
            IndexBody(id);
        }
        ++refs_[id];
    }
    cellBodies_[key] = std::move(ids);
    return true;
}

void WaterStore::UnloadCell(int32_t cellX, int32_t cellZ) {
    RemoveCellContribution(CellKey{cellX, cellZ});
}

bool WaterStore::IsCellLoaded(int32_t cellX, int32_t cellZ) const {
    return cellBodies_.find(CellKey{cellX, cellZ}) != cellBodies_.end();
}

const WaterBodyInstance* WaterStore::Find(uint32_t bodyId) const {
    const auto it = bodies_.find(bodyId);
    return it != bodies_.end() ? &it->second : nullptr;
}

std::vector<uint32_t> WaterStore::BodiesOverlappingAabb(float minX, float minZ, float maxX, float maxZ) const {
    std::vector<uint32_t> out;
    const int32_t gx0 = BucketCoord(minX);
    const int32_t gx1 = BucketCoord(maxX);
    const int32_t gz0 = BucketCoord(minZ);
    const int32_t gz1 = BucketCoord(maxZ);
    for (int32_t gx = gx0; gx <= gx1; ++gx) {
        for (int32_t gz = gz0; gz <= gz1; ++gz) {
            const auto bit = grid_.find(GridKey{gx, gz});
            if (bit == grid_.end()) {
                continue;
            }
            for (const uint32_t id : bit->second) {
                const auto bodyIt = bodies_.find(id);
                if (bodyIt != bodies_.end() && AabbOverlapXZ(bodyIt->second, minX, minZ, maxX, maxZ)) {
                    out.push_back(id);
                }
            }
        }
    }
    for (const uint32_t id : globalBodies_) {
        const auto bodyIt = bodies_.find(id);
        if (bodyIt != bodies_.end() && AabbOverlapXZ(bodyIt->second, minX, minZ, maxX, maxZ)) {
            out.push_back(id);
        }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

const WaterBodyInstance* WaterStore::BodyAt(float x, float z, double timeSeconds) const {
    // W7 hot path: this is called per buoyant body per tick. Walk the SINGLE grid bucket containing
    // (x,z) plus the (few) global bodies INLINE — no per-call vector allocation or sort. Topmost surface
    // wins; exact ties break to the lowest bodyId, so the result is order-independent / deterministic.
    const WaterBodyInstance* best = nullptr;
    float bestSurface = 0.0f;
    uint32_t bestId = 0;
    const auto consider = [&](uint32_t id) {
        const auto it = bodies_.find(id);
        if (it == bodies_.end()) {
            return;
        }
        const WaterBodyInstance& b = it->second;
        if (!WaterBodyContainsXZ(b, x, z)) {
            return;
        }
        const float surf = SampleHeightFast(b, x, z, timeSeconds);
        if (best == nullptr || surf > bestSurface || (surf == bestSurface && id < bestId)) {
            best = &b;
            bestSurface = surf;
            bestId = id;
        }
    };
    const auto bit = grid_.find(GridKey{BucketCoord(x), BucketCoord(z)});
    if (bit != grid_.end()) {
        for (const uint32_t id : bit->second) {
            consider(id);
        }
    }
    for (const uint32_t id : globalBodies_) {
        consider(id);
    }
    return best;
}

bool WaterStore::SampleWaterAt(float x, float y, float z, double timeSeconds, WaterSample& out) const {
    const WaterBodyInstance* b = BodyAt(x, z, timeSeconds);
    if (b == nullptr) {
        return false;
    }
    const float surfaceY = SampleHeightFast(*b, x, z, timeSeconds);
    out.bodyId = b->bodyId;
    out.inWater = true;
    out.surfaceHeight = surfaceY;
    out.submersionDepth = surfaceY - y;
    out.floorY = b->boundsMin[1];
    out.submerged = (y <= surfaceY) && (y >= b->boundsMin[1]);
    out.density = b->density;
    out.flowVelocity[0] = b->flowVelocity[0];
    out.flowVelocity[1] = b->flowVelocity[1];
    out.flowVelocity[2] = b->flowVelocity[2];
    out.flags = b->flags;
    return true;
}

bool WaterStore::IsSubmerged(float x, float y, float z, double timeSeconds) const {
    WaterSample s;
    return SampleWaterAt(x, y, z, timeSeconds, s) && s.submerged;
}

std::vector<uint32_t> WaterStore::AllBodyIds() const {
    std::vector<uint32_t> ids;
    ids.reserve(bodies_.size());
    for (const auto& [id, body] : bodies_) {
        (void)body;
        ids.push_back(id);
    }
    return ids;  // std::map -> already ascending
}

}  // namespace Next::water
