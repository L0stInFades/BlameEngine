#include "next/vegetation_world/vegetation_store.h"

#include "next/vegetation/vegetation_cell.h"

namespace Next::vegetation {

bool VegetationStore::LoadCell(int32_t cellX, int32_t cellZ, const uint8_t* vegBlob, size_t size) {
    VegetationCellData parsed;
    if (!UnpackCell(vegBlob, size, parsed)) {
        return false;  // fail-closed: a malformed blob indexes nothing
    }
    cells_[CellKey{cellX, cellZ}] = std::move(parsed.instances);
    return true;
}

void VegetationStore::UnloadCell(int32_t cellX, int32_t cellZ) {
    cells_.erase(CellKey{cellX, cellZ});
    for (auto it = removed_.begin(); it != removed_.end();) {
        if (it->cellX == cellX && it->cellZ == cellZ) {
            it = removed_.erase(it);
        } else {
            ++it;
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

std::vector<VegetationKey> VegetationStore::QueryRadius(float x, float z, float radius) const {
    return QueryRadiusWithFlags(x, z, radius, 0);
}

std::vector<VegetationKey> VegetationStore::QueryRadiusWithFlags(float x, float z, float radius,
                                                                 uint16_t flagMask) const {
    std::vector<VegetationKey> result;
    if (!(radius > 0.0f)) {
        return result;
    }
    const float r2 = radius * radius;
    for (const auto& entry : cells_) {  // ordered cells -> deterministic
        const CellKey& cell = entry.first;
        const std::vector<VegetationInstance>& instances = entry.second;
        for (size_t i = 0; i < instances.size(); ++i) {
            const VegetationInstance& inst = instances[i];
            if (flagMask != 0 && (inst.flags & flagMask) != flagMask) {
                continue;
            }
            const float dx = inst.position[0] - x;
            const float dz = inst.position[2] - z;
            if (dx * dx + dz * dz > r2) {
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

}  // namespace Next::vegetation
