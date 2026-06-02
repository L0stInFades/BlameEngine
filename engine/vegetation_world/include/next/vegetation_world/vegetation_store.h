#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <vector>

#include "next/vegetation/vegetation_def.h"

// Runtime vegetation store (ADR-0014). The authoritative, headless index over the vegetation instances
// of currently-loaded cells. A streaming system calls LoadCell when a CellLayer::Vegetation blob loads
// and UnloadCell when it streams out; gameplay queries by radius/flags and toggles a destructible
// "removed" overlay. No rendering — UE5 consumes the same instances separately via the boundary.

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
    // Index a cell from its CellLayer::Vegetation blob bytes (what LoadCellLayer stored). FAIL-CLOSED:
    // returns false (and indexes nothing) if the blob is malformed.
    bool LoadCell(int32_t cellX, int32_t cellZ, const uint8_t* vegBlob, size_t size);
    void UnloadCell(int32_t cellX, int32_t cellZ);
    bool IsCellLoaded(int32_t cellX, int32_t cellZ) const;

    size_t LoadedCellCount() const { return cells_.size(); }
    size_t LiveInstanceCount() const;  // excludes removed

    // All live instances (optionally filtered to those whose flags contain ALL bits of `flagMask`),
    // in deterministic order (ascending cell, then ordinal). The primitive for line-of-sight sweeps.
    std::vector<VegetationKey> AllLive(uint16_t flagMask = 0) const;

    // Instances whose XZ position is within `radius` of (x,z), excluding removed. Deterministic order
    // (ascending cell, then ordinal). `radius` <= 0 -> empty.
    std::vector<VegetationKey> QueryRadius(float x, float z, float radius) const;
    // Same, but only instances whose flags contain ALL bits of `flagMask` (e.g. VegBlocksLineOfSight).
    std::vector<VegetationKey> QueryRadiusWithFlags(float x, float z, float radius, uint16_t flagMask) const;

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

    std::map<CellKey, std::vector<VegetationInstance>> cells_;  // ordered -> deterministic iteration
    std::set<VegetationKey> removed_;
};

}  // namespace Next::vegetation
