#include "next/vegetation_world/vegetation_view.h"

#include <set>

#include "next/vegetation/vegetation_cell.h"

namespace Next::vegetation {

Next::boundary::GameEvent ToBoundaryEvent(const VegetationDestroyedEvent& ev) {
    Next::boundary::GameEvent e{};
    e.type = kVegEventInstanceDestroyed;
    e.subject = static_cast<Next::boundary::EntityId>(ev.visual);  // which mesh/variant fell (cosmetic)
    e.params[0] = ev.position[0];
    e.params[1] = ev.position[1];
    e.params[2] = ev.position[2];
    e.params[3] = 0.0f;
    return e;
}

bool MockVegetationConsumer::OnCellLoaded(int32_t cellX, int32_t cellZ, const uint8_t* vegBlob, size_t size) {
    VegetationCellData parsed;
    if (!UnpackCell(vegBlob, size, parsed)) {
        return false;  // fail-closed
    }
    std::map<uint32_t, uint32_t> live;
    for (const VegetationInstance& inst : parsed.instances) {
        live[inst.instanceId] = inst.visual;
    }
    cells_[CellKey{cellX, cellZ}] = std::move(live);
    return true;
}

void MockVegetationConsumer::OnCellUnloaded(int32_t cellX, int32_t cellZ) {
    cells_.erase(CellKey{cellX, cellZ});
}

bool MockVegetationConsumer::OnInstanceDestroyed(int32_t cellX, int32_t cellZ, uint32_t instanceId) {
    const auto it = cells_.find(CellKey{cellX, cellZ});
    if (it == cells_.end()) {
        return false;
    }
    return it->second.erase(instanceId) > 0;
}

size_t MockVegetationConsumer::TotalInstanceCount() const {
    size_t total = 0;
    for (const auto& cell : cells_) {
        total += cell.second.size();
    }
    return total;
}

size_t MockVegetationConsumer::InstanceCountForVisual(uint32_t visual) const {
    size_t n = 0;
    for (const auto& cell : cells_) {
        for (const auto& entry : cell.second) {
            if (entry.second == visual) {
                ++n;
            }
        }
    }
    return n;
}

size_t MockVegetationConsumer::VisualBucketCount() const {
    std::set<uint32_t> visuals;
    for (const auto& cell : cells_) {
        for (const auto& entry : cell.second) {
            visuals.insert(entry.second);
        }
    }
    return visuals.size();
}

}  // namespace Next::vegetation
