#include "next/vegetation_world/vegetation_streaming_system.h"

#include <unordered_set>

namespace Next::vegetation {

using Next::Streaming::CellCoord;
using Next::Streaming::CellData;
using Next::Streaming::CellLayer;
using Next::Streaming::CellLoadState;

size_t VegetationStreamingSystem::Sync(const Next::Streaming::StreamingManager& manager) {
    size_t changed = 0;
    std::unordered_set<CellCoord, CellCoord::Hash> nowLoaded;

    // Ingest cells with a real Vegetation layer the store doesn't have yet, OR whose layer bytes changed
    // (an in-place unload+reload between Syncs keeps the coord but yields a different allocation/size).
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

        const uint64_t generation = it->second.generation;
        const auto ingested = ingested_.find(coord);
        const bool fresh = ingested == ingested_.end();
        const bool reloaded = !fresh && ingested->second != generation;
        if (fresh || reloaded) {
            if (store_.LoadCell(coord.x, coord.z, static_cast<const uint8_t*>(it->second.data), it->second.size)) {
                ingested_[coord] = generation;  // LoadCell replaces on reload
                ++changed;
            }
        }
    }

    // Evict cells the store tracks that are no longer loaded with vegetation.
    for (auto it = ingested_.begin(); it != ingested_.end();) {
        if (nowLoaded.find(it->first) == nowLoaded.end()) {
            store_.UnloadCell(it->first.x, it->first.z);
            it = ingested_.erase(it);
            ++changed;
        } else {
            ++it;
        }
    }
    return changed;
}

}  // namespace Next::vegetation
