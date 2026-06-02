#include "next/vegetation_world/vegetation_streaming_system.h"

namespace Next::vegetation {

using Next::Streaming::CellCoord;
using Next::Streaming::CellData;
using Next::Streaming::CellLayer;
using Next::Streaming::CellLoadState;

size_t VegetationStreamingSystem::Sync(const Next::Streaming::StreamingManager& manager) {
    size_t changed = 0;
    std::unordered_set<CellCoord, CellCoord::Hash> nowLoaded;

    // Ingest cells that have a real (non-placeholder) Vegetation layer the store doesn't have yet.
    for (const CellCoord& coord : manager.GetLoadedCells()) {
        const CellData* cell = manager.GetCell(coord);
        if (cell == nullptr) {
            continue;
        }
        const auto it = cell->layers.find(CellLayer::Vegetation);
        if (it == cell->layers.end() || it->second.state != CellLoadState::Loaded || it->second.data == nullptr ||
            it->second.size == 0) {
            continue;  // no real vegetation data on this cell
        }
        nowLoaded.insert(coord);
        if (ingested_.find(coord) == ingested_.end()) {
            const bool ok =
                store_.LoadCell(coord.x, coord.z, static_cast<const uint8_t*>(it->second.data), it->second.size);
            if (ok) {
                ingested_.insert(coord);
                ++changed;
            }
        }
    }

    // Evict cells the store tracks that are no longer loaded with vegetation.
    for (auto it = ingested_.begin(); it != ingested_.end();) {
        if (nowLoaded.find(*it) == nowLoaded.end()) {
            store_.UnloadCell(it->x, it->z);
            it = ingested_.erase(it);
            ++changed;
        } else {
            ++it;
        }
    }
    return changed;
}

}  // namespace Next::vegetation
