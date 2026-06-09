#include "next/vegetation_world/vegetation_store.h"

#include <algorithm>
#include <cmath>

#include "next/vegetation/vegetation_cell.h"

namespace Next::vegetation {

VegetationStore::VegetationStore(float broadphaseCellSize)
    : broadphaseCellSize_(broadphaseCellSize > 0.0f ? broadphaseCellSize : 8.0f) {}

VegetationStore::GridKey VegetationStore::BucketOf(float x, float z) const {
    // Clamp before the float->int32 cast: an out-of-int32-range quotient (extreme coord/radius) would be
    // undefined behavior and could corrupt the bucket range.
    auto bucket = [](float v) -> int32_t {
        const float f = std::floor(v);
        if (f < -2.0e9f) {
            return -2000000000;
        }
        if (f > 2.0e9f) {
            return 2000000000;
        }
        return static_cast<int32_t>(f);
    };
    return GridKey{bucket(x / broadphaseCellSize_), bucket(z / broadphaseCellSize_)};
}

bool VegetationStore::LoadCell(int32_t cellX, int32_t cellZ, const uint8_t* vegBlob, size_t size) {
    VegetationCellData parsed;
    if (!UnpackCell(vegBlob, size, parsed)) {
        return false;  // fail-closed: a malformed blob indexes nothing
    }
    if (cells_.find(CellKey{cellX, cellZ}) != cells_.end()) {
        UnloadCell(cellX, cellZ);  // replace cleanly (keeps the grid in sync)
    }

    std::vector<VegetationInstance>& instances = cells_[CellKey{cellX, cellZ}];
    instances = std::move(parsed.instances);
    for (size_t i = 0; i < instances.size(); ++i) {
        const VegetationInstance& inst = instances[i];
        if (inst.logicalRadius > maxRadius_) {
            maxRadius_ = inst.logicalRadius;
        }
        grid_[BucketOf(inst.position[0], inst.position[2])].push_back(
            VegetationKey{cellX, cellZ, static_cast<uint32_t>(i)});
    }
    return true;
}

void VegetationStore::UnloadCell(int32_t cellX, int32_t cellZ) {
    const auto it = cells_.find(CellKey{cellX, cellZ});
    if (it != cells_.end()) {
        const std::vector<VegetationInstance>& instances = it->second;
        for (size_t i = 0; i < instances.size(); ++i) {
            const GridKey bucket = BucketOf(instances[i].position[0], instances[i].position[2]);
            const auto git = grid_.find(bucket);
            if (git == grid_.end()) {
                continue;
            }
            std::vector<VegetationKey>& vec = git->second;
            const VegetationKey key{cellX, cellZ, static_cast<uint32_t>(i)};
            vec.erase(std::remove(vec.begin(), vec.end(), key), vec.end());
            if (vec.empty()) {
                grid_.erase(git);
            }
        }
        cells_.erase(it);
    }
    for (auto rit = removed_.begin(); rit != removed_.end();) {
        if (rit->cellX == cellX && rit->cellZ == cellZ) {
            rit = removed_.erase(rit);
        } else {
            ++rit;
        }
    }
}

bool VegetationStore::IsCellLoaded(int32_t cellX, int32_t cellZ) const {
    return cells_.find(CellKey{cellX, cellZ}) != cells_.end();
}

size_t VegetationStore::LiveInstanceCount() const {
    size_t total = 0;
    for (const auto& entry : cells_) {
        total += entry.second.size();
    }
    return total - removed_.size();  // removed_ only ever holds keys of loaded instances
}

void VegetationStore::GatherCandidates(float minX, float minZ, float maxX, float maxZ,
                                       std::vector<VegetationKey>& out) const {
    if (grid_.empty() || maxX < minX || maxZ < minZ) {
        return;
    }
    const GridKey lo = BucketOf(minX, minZ);
    const GridKey hi = BucketOf(maxX, maxZ);
    const int64_t populated = static_cast<int64_t>(grid_.size());
    const int64_t spanX = static_cast<int64_t>(hi.gx) - static_cast<int64_t>(lo.gx) + 1;
    const int64_t spanZ = static_cast<int64_t>(hi.gz) - static_cast<int64_t>(lo.gz) + 1;

    // Probe the dense range only when it is no larger than the populated bucket set; otherwise a wide
    // query (e.g. a huge radius) would walk millions of empty buckets, so scan the populated set
    // instead. Cost is O(min(rangeCells, populatedBuckets)) either way — no perf cliff.
    const bool denseCheaper = spanX <= populated && spanZ <= populated && (spanX * spanZ) <= populated;
    if (denseCheaper) {
        for (int32_t gx = lo.gx; gx <= hi.gx; ++gx) {
            for (int32_t gz = lo.gz; gz <= hi.gz; ++gz) {
                const auto git = grid_.find(GridKey{gx, gz});
                if (git != grid_.end()) {
                    out.insert(out.end(), git->second.begin(), git->second.end());
                }
            }
        }
    } else {
        for (const auto& entry : grid_) {
            const GridKey& g = entry.first;
            if (g.gx >= lo.gx && g.gx <= hi.gx && g.gz >= lo.gz && g.gz <= hi.gz) {
                out.insert(out.end(), entry.second.begin(), entry.second.end());
            }
        }
    }
}

std::vector<VegetationKey> VegetationStore::AllLive(uint16_t flagMask) const {
    std::vector<VegetationKey> result;
    for (const auto& entry : cells_) {  // ordered cells -> deterministic
        const CellKey& cell = entry.first;
        const std::vector<VegetationInstance>& instances = entry.second;
        for (size_t i = 0; i < instances.size(); ++i) {
            if (flagMask != 0 && (instances[i].flags & flagMask) != flagMask) {
                continue;
            }
            const VegetationKey key{cell.x, cell.z, static_cast<uint32_t>(i)};
            if (IsRemoved(key)) {
                continue;
            }
            result.push_back(key);
        }
    }
    return result;
}

std::vector<VegetationKey> VegetationStore::QueryAABB(float minX, float minZ, float maxX, float maxZ,
                                                      uint16_t flagMask) const {
    std::vector<VegetationKey> candidates;
    GatherCandidates(minX, minZ, maxX, maxZ, candidates);

    std::vector<VegetationKey> result;
    result.reserve(candidates.size());
    for (const VegetationKey& key : candidates) {
        const VegetationInstance* p = Find(key);  // skips removed / out-of-range
        if (p == nullptr) {
            continue;
        }
        if (flagMask != 0 && (p->flags & flagMask) != flagMask) {
            continue;
        }
        if (p->position[0] < minX || p->position[0] > maxX || p->position[2] < minZ || p->position[2] > maxZ) {
            continue;
        }
        result.push_back(key);
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::vector<VegetationKey> VegetationStore::QueryRadius(float x, float z, float radius) const {
    return QueryRadiusWithFlags(x, z, radius, 0);
}

std::vector<VegetationKey> VegetationStore::QueryRadiusWithFlags(float x, float z, float radius,
                                                                 uint16_t flagMask) const {
    std::vector<VegetationKey> result;
    if (!(radius > 0.0f)) {
        return result;
    }
    std::vector<VegetationKey> candidates;
    GatherCandidates(x - radius, z - radius, x + radius, z + radius, candidates);

    const float r2 = radius * radius;
    for (const VegetationKey& key : candidates) {
        const VegetationInstance* p = Find(key);
        if (p == nullptr) {
            continue;
        }
        if (flagMask != 0 && (p->flags & flagMask) != flagMask) {
            continue;
        }
        const float dx = p->position[0] - x;
        const float dz = p->position[2] - z;
        if (dx * dx + dz * dz > r2) {
            continue;
        }
        result.push_back(key);
    }
    std::sort(result.begin(), result.end());  // deterministic regardless of bucket/load order
    return result;
}

const VegetationInstance* VegetationStore::Find(const VegetationKey& key) const {
    if (IsRemoved(key)) {
        return nullptr;
    }
    const auto it = cells_.find(CellKey{key.cellX, key.cellZ});
    if (it == cells_.end() || key.instanceId >= it->second.size()) {
        return nullptr;
    }
    return &it->second[key.instanceId];
}

bool VegetationStore::IsRemoved(const VegetationKey& key) const {
    return removed_.find(key) != removed_.end();
}

bool VegetationStore::Remove(const VegetationKey& key) {
    const auto it = cells_.find(CellKey{key.cellX, key.cellZ});
    if (it == cells_.end() || key.instanceId >= it->second.size()) {
        return false;
    }
    const VegetationInstance& inst = it->second[key.instanceId];
    if ((inst.flags & VegDestructible) == 0) {
        return false;  // not destructible
    }
    if (IsRemoved(key)) {
        return false;  // already removed
    }
    removed_.insert(key);
    return true;
}

}  // namespace Next::vegetation
