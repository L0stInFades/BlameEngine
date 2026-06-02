#pragma once

#include <cstdint>
#include <map>

#include "next/boundary/snapshot.h"                  // GameEvent / EntityId
#include "next/vegetation_world/vegetation_query.h"  // VegetationDestroyedEvent

// sim->UE5 vegetation view contract (ADR-0014). Bulk vegetation streams to UE5 as per-cell payloads
// (the same wire-ready CellLayer::Vegetation blob), NOT as per-entity snapshot records — millions of
// static instances would swamp the per-entity stream. One-shot destruction cues ride the boundary's
// existing GameEvent channel (cosmetic, carries no authority). The MockVegetationConsumer is a faithful
// headless stand-in for the UE5 MirrorSubsystem: it proves the cooked/streamed bytes are consumable.

namespace Next::vegetation {

// GameEvent.type for a one-shot "this instance was felled" cosmetic cue (FX + remove the visual).
constexpr uint32_t kVegEventInstanceDestroyed = 0x56454744u;  // 'VEGD'

// Convert an authoritative destruction into a boundary GameEvent for the view (subject = the visual
// id that fell, so UE5 can pick the right FX; params = world position). Carries no authority.
Next::boundary::GameEvent ToBoundaryEvent(const VegetationDestroyedEvent& ev);

// A faithful UE5-side consumer stand-in (test double). UE5 would, on cell-load, unpack the Vegetation
// blob, group instances by VisualStateId, and batch each group into a HISM/Nanite instance buffer; on
// cell-unload it drops those buffers; on destroy it removes one instance from its bucket. It holds NO
// placement authority — it only mirrors what the sim sent.
class MockVegetationConsumer {
public:
    // Cell streamed in: parse the Vegetation blob and bucket its instances by visual. FAIL-CLOSED:
    // returns false (and ignores the cell) on a malformed blob.
    bool OnCellLoaded(int32_t cellX, int32_t cellZ, const uint8_t* vegBlob, size_t size);
    void OnCellUnloaded(int32_t cellX, int32_t cellZ);
    // Remove one instance (by its per-cell ordinal). Returns false if it is not currently instantiated.
    bool OnInstanceDestroyed(int32_t cellX, int32_t cellZ, uint32_t instanceId);

    size_t LoadedCellCount() const { return cells_.size(); }
    size_t TotalInstanceCount() const;                     // across all loaded cells, minus destroyed
    size_t InstanceCountForVisual(uint32_t visual) const;  // HISM bucket size for a mesh variant
    size_t VisualBucketCount() const;                      // distinct visuals currently instantiated

private:
    struct CellKey {
        int32_t x = 0;
        int32_t z = 0;
        bool operator<(const CellKey& o) const { return x != o.x ? x < o.x : z < o.z; }
    };

    // cell -> (instanceId -> visual). The per-visual grouping (HISM batching) is derived on query.
    std::map<CellKey, std::map<uint32_t, uint32_t>> cells_;
};

}  // namespace Next::vegetation
