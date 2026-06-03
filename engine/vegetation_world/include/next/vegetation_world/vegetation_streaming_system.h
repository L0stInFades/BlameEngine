#pragma once

#include <cstddef>
#include <unordered_map>
#include <utility>

#include "next/streaming/streaming_manager.h"
#include "next/vegetation_world/vegetation_store.h"

// A RUNNING system (ADR-0014): keeps a VegetationStore in sync with a StreamingManager. Each frame
// (after the streaming Update) call Sync() — it ingests the CellLayer::Vegetation blob of every newly
// loaded cell into the store and evicts cells whose vegetation layer has streamed out. This is the
// wiring that makes vegetation an actual runtime system, not parts assembled by hand in a test.

namespace Next::vegetation {

class VegetationStreamingSystem {
public:
    explicit VegetationStreamingSystem(float broadphaseCellSize = 8.0f) : store_(broadphaseCellSize) {}

    // Reconcile the store with the manager's currently-loaded Vegetation layers. Idempotent: ingests
    // newly-loaded cells, evicts gone ones, leaves unchanged cells alone. Returns the number of cells
    // ingested or evicted this call (0 when already in sync).
    size_t Sync(const Next::Streaming::StreamingManager& manager);

    VegetationStore& Store() { return store_; }
    const VegetationStore& Store() const { return store_; }
    size_t TrackedCellCount() const { return ingested_.size(); }

private:
    VegetationStore store_;
    // coord -> (data ptr, size) of the layer bytes last ingested. The (ptr,size) token detects an
    // in-place unload+reload (same coord, different allocation/size) so the store doesn't serve stale
    // instances. (A reload to the identical ptr AND size with different content is not distinguished —
    // it would need a per-layer version/checksum, which the streaming LayerData does not yet carry.)
    std::unordered_map<Next::Streaming::CellCoord, std::pair<const void*, size_t>, Next::Streaming::CellCoord::Hash>
        ingested_;
};

}  // namespace Next::vegetation
