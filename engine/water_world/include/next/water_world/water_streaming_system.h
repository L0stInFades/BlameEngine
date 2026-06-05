#pragma once

#include <cstdint>
#include <unordered_map>

#include "next/streaming/streaming_manager.h"
#include "next/streaming/world_partition.h"
#include "next/water_world/water_store.h"

// Running system that keeps a WaterStore in sync with the StreamingManager (ADR-0015), mirroring
// VegetationStreamingSystem: each call it ingests cells whose CellLayer::Water layer just loaded (or
// reloaded — detected via the per-layer generation counter) and evicts cells that streamed out. Pure
// glue; the authority is the store.

namespace Next::water {

class WaterStreamingSystem {
public:
    explicit WaterStreamingSystem(float broadphaseCellSize = 16.0f) : store_(broadphaseCellSize) {}

    // Reconcile the store with the manager's currently-loaded Water layers. Returns the number of
    // ingest/evict changes applied (0 == already in sync).
    size_t Sync(const Next::Streaming::StreamingManager& manager);

    WaterStore& Store() { return store_; }
    const WaterStore& Store() const { return store_; }
    size_t TrackedCellCount() const { return ingested_.size(); }

private:
    WaterStore store_;
    std::unordered_map<Next::Streaming::CellCoord, uint64_t, Next::Streaming::CellCoord::Hash> ingested_;
};

}  // namespace Next::water
