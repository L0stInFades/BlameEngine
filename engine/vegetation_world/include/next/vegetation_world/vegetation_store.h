#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <vector>

#include "next/vegetation/vegetation_def.h"

// Runtime vegetation store (ADR-0014). The authoritative, headless index over the vegetation instances
// of currently-loaded cells. A streaming system calls LoadCell when a CellLayer::Vegetation blob loads
// and UnloadCell when it streams out; gameplay queries by radius/flags and toggles a destructible
// "removed" overlay. Spatial queries go through a uniform-grid BROADPHASE so they cost O(candidates),
// not O(all loaded instances) — open worlds carry millions of plants. No rendering: UE5 consumes the
// same instances separately via the boundary.

namespace Next::vegetation {

// Collision-free global handle: world cell coordinate + the instance's per-cell ordinal (which equals
// its index within the cell). Unique across the whole world.
struct VegetationKey {
    int32_t cellX = 0;
    int32_t cellZ = 0;
    uint32_t instanceId = 0;  // per-cell ordinal

    bool operator==(const VegetationKey& o) const {
        return cellX == o.cellX && cellZ == o.cellZ && instanceId == o.instanceId;
    }
    bool operator<(const VegetationKey& o) const {
        if (cellX != o.cellX) {
            return cellX < o.cellX;
        }
        if (cellZ != o.cellZ) {
            return cellZ < o.cellZ;
        }
        return instanceId < o.instanceId;
    }
};

class VegetationStore {
public:
    // broadphaseCellSize: side (meters) of the uniform grid that accelerates spatial queries.
    explicit VegetationStore(float broadphaseCellSize = 8.0f);

    // Index a cell from its CellLayer::Vegetation blob bytes (what LoadCellLayer stored). FAIL-CLOSED:
    // returns false (and indexes nothing) if the blob is malformed. Re-loading a cell replaces it.
    bool LoadCell(int32_t cellX, int32_t cellZ, const uint8_t* vegBlob, size_t size);
    void UnloadCell(int32_t cellX, int32_t cellZ);
    bool IsCellLoaded(int32_t cellX, int32_t cellZ) const;

    size_t LoadedCellCount() const { return cells_.size(); }
    size_t LiveInstanceCount() const;  // excludes removed

    // All live instances (optionally flag-filtered), ascending (cell,ordinal). O(total) — for sweeps
    // that genuinely need everything; the spatial queries below use the broadphase grid instead.
    std::vector<VegetationKey> AllLive(uint16_t flagMask = 0) const;

    // Live instances whose XZ position lies within the AABB [minX,maxX] x [minZ,maxZ] (flag-filtered),
    // via the broadphase grid. Deterministic order (ascending key). The primitive the LOS/cover queries
    // build on — a ray/segment first reduces to its AABB (expanded by MaxLoadedRadius).
    std::vector<VegetationKey> QueryAABB(float minX, float minZ, float maxX, float maxZ, uint16_t flagMask = 0) const;

    // Live instances within `radius` of (x,z), excluding removed. Deterministic order. `radius` <= 0 ->
    // empty. Grid-accelerated.
    std::vector<VegetationKey> QueryRadius(float x, float z, float radius) const;
    std::vector<VegetationKey> QueryRadiusWithFlags(float x, float z, float radius, uint16_t flagMask) const;

    // Largest logicalRadius among loaded instances — a safe broadphase margin for ray/segment AABBs.
    // Never shrinks on unload (a stale-high bound only widens the search, staying correct).
    float MaxLoadedRadius() const { return maxRadius_; }

    // The live instance for a key, or nullptr if absent or removed.
    const VegetationInstance* Find(const VegetationKey& key) const;

    // Destructible overlay: mark an instance removed (felled/burned). Returns false if the instance is
    // absent, not flagged VegDestructible, or already removed.
    bool Remove(const VegetationKey& key);
    bool IsRemoved(const VegetationKey& key) const;
    void ClearRemovals() { removed_.clear(); }
    size_t RemovedCount() const { return removed_.size(); }

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

    GridKey BucketOf(float x, float z) const;
    // Collect (unfiltered, possibly-removed) candidate keys from every grid bucket overlapping the AABB.
    void GatherCandidates(float minX, float minZ, float maxX, float maxZ, std::vector<VegetationKey>& out) const;

    float broadphaseCellSize_;
    float maxRadius_ = 0.0f;
    std::map<CellKey, std::vector<VegetationInstance>> cells_;  // ordered -> deterministic AllLive
    std::map<GridKey, std::vector<VegetationKey>> grid_;        // broadphase: bucket -> keys
    std::set<VegetationKey> removed_;
};

}  // namespace Next::vegetation
