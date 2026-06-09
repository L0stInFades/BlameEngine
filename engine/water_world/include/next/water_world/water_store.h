#pragma once

#include <cstdint>
#include <map>
#include <vector>

#include "next/water/water_def.h"

// Runtime water store (ADR-0015). The authoritative, headless index over the water bodies of currently
// loaded cells. A streaming system calls LoadCell when a CellLayer::Water blob loads and UnloadCell when
// it streams out; the buoyancy/force system and gameplay queries sample it. A body may span many cells —
// it is recorded (same bodyId) in each, so the store DE-DUPLICATES by bodyId with a per-cell refcount.
// Spatial lookups go through a uniform-grid BROADPHASE so they cost O(candidates); LARGE/Ocean bodies,
// whose AABB would smear across the whole grid, are kept in a small "global" list checked on every
// query instead (so an infinite sea never explodes the grid). No rendering: UE5 consumes the same
// bodies separately via the boundary.

namespace Next::water {

// The water state at a queried world point (filled by SampleWaterAt).
struct WaterSample {
    uint32_t bodyId = kInvalidWaterBody;
    bool inWater = false;          // (x,z) lies within some body's footprint
    bool submerged = false;        // the point is at/below that body's surface (and above its floor)
    float surfaceHeight = 0.0f;    // water surface Y at (x,z), this tick
    float submersionDepth = 0.0f;  // surfaceHeight - y  (> 0 when submerged)
    float floorY = 0.0f;           // the body's container floor (boundsMin.y)
    float density = 0.0f;
    float flowVelocity[3] = {0.0f, 0.0f, 0.0f};
    uint16_t flags = 0;  // WaterFlags of the governing body
};

class WaterStore {
public:
    // broadphaseCellSize: side (meters) of the uniform grid that accelerates spatial queries.
    explicit WaterStore(float broadphaseCellSize = 16.0f);

    // Index a cell from its CellLayer::Water blob bytes. FAIL-CLOSED: returns false (indexing nothing)
    // on a malformed blob. Re-loading a cell replaces its contribution (reload-safe).
    bool LoadCell(int32_t cellX, int32_t cellZ, const uint8_t* waterBlob, size_t size);
    void UnloadCell(int32_t cellX, int32_t cellZ);
    bool IsCellLoaded(int32_t cellX, int32_t cellZ) const;

    size_t LoadedCellCount() const { return cellBodies_.size(); }
    size_t BodyCount() const { return bodies_.size(); }

    // The body with this id, or nullptr.
    const WaterBodyInstance* Find(uint32_t bodyId) const;

    // The topmost body whose XZ footprint contains (x,z) at time t (highest effective surface; ties
    // broken by lowest bodyId for determinism), or nullptr if (x,z) is in no water.
    const WaterBodyInstance* BodyAt(float x, float z, double timeSeconds) const;

    // Sample the water at a world point: governing body, surface height, submersion. Returns false
    // (out left default) if (x,z) is in no water body.
    bool SampleWaterAt(float x, float y, float z, double timeSeconds, WaterSample& out) const;

    // Convenience: is the point at/below the water surface (and above the floor) of some body?
    bool IsSubmerged(float x, float y, float z, double timeSeconds) const;

    // Body ids whose AABB overlaps the XZ AABB [minX,maxX] x [minZ,maxZ] (broadphase grid + globals).
    // Ascending, deduplicated. The primitive the buoyancy system uses to find a body's water.
    std::vector<uint32_t> BodiesOverlappingAabb(float minX, float minZ, float maxX, float maxZ) const;

    // All loaded body ids, ascending (deterministic sweeps / hashing).
    std::vector<uint32_t> AllBodyIds() const;

private:
    struct CellKey {
        int32_t x = 0;
        int32_t z = 0;
        bool operator<(const CellKey& o) const { return x != o.x ? x < o.x : z < o.z; }
    };
    struct GridKey {
        int32_t gx = 0;
        int32_t gz = 0;
        bool operator<(const GridKey& o) const { return gx != o.gx ? gx < o.gx : gz < o.gz; }
    };

    int32_t BucketCoord(float v) const;
    bool IsLargeBody(const WaterBodyInstance& b) const;  // -> tracked as a global, not gridded
    void IndexBody(uint32_t id);                         // add to grid/globals
    void DeindexBody(uint32_t id);                       // remove from grid/globals
    void RemoveCellContribution(const CellKey& key);     // shared by UnloadCell + reload-replace

    float broadphaseCellSize_;
    std::map<uint32_t, WaterBodyInstance> bodies_;         // bodyId -> body (ascending -> deterministic)
    std::map<uint32_t, int32_t> refs_;                     // bodyId -> #loaded cells referencing it
    std::map<CellKey, std::vector<uint32_t>> cellBodies_;  // cell -> body ids it contributed
    std::map<GridKey, std::vector<uint32_t>> grid_;        // broadphase bucket -> bounded body ids
    std::vector<uint32_t> globalBodies_;                   // large/Ocean bodies (ascending), always candidates
};

}  // namespace Next::water
