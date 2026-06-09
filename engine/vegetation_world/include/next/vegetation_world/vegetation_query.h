#pragma once

#include <cstdint>

#include "next/gameapi/world_query.h"
#include "next/vegetation_world/vegetation_store.h"

// Vegetation gameplay queries (ADR-0014): line-of-sight / cover and destruction, over the authoritative
// VegetationStore. The IWorldQuery adapter makes vegetation participate in the EXISTING Game API
// Sense-gated raycast (ADR-0007/0010), so player code and AI see vegetation cover without any new ABI.

namespace Next::vegetation {

// True if the XZ segment (ax,az)->(bx,bz) passes within any live VegBlocksLineOfSight instance's
// logicalRadius — i.e. line of sight is blocked by vegetation cover. Deterministic.
bool SegmentBlockedByVegetation(const VegetationStore& store, float ax, float az, float bx, float bz);

// IWorldQuery over vegetation: every live VegBlocksLineOfSight instance is an infinite vertical cylinder
// of radius logicalRadius. Raycast returns the nearest such blocker within maxDistance. An optional
// fallback IWorldQuery (e.g. the physics PhysicsWorldQuery) is also consulted and the NEARER hit wins —
// so installing this as the Game API's worldQuery folds vegetation into the existing Sense raycast.
class VegetationWorldQuery final : public gameapi::IWorldQuery {
public:
    explicit VegetationWorldQuery(const VegetationStore* store, gameapi::IWorldQuery* fallback = nullptr)
        : store_(store), fallback_(fallback) {}

    gameapi::RaycastResult Raycast(const float origin[3], const float direction[3], float maxDistance) override;

private:
    const VegetationStore* store_;
    gameapi::IWorldQuery* fallback_;
};

// sim->UE5 destruction event payload (flat POD). Produced when a destructible instance is felled; the
// boundary layer forwards it to the view (see engine/boundary wiring in ADR-0014).
struct VegetationDestroyedEvent {
    int32_t cellX = 0;
    int32_t cellZ = 0;
    uint32_t instanceId = 0;
    uint32_t visual = 0;
    float position[3] = {0.0f, 0.0f, 0.0f};
};

// Remove a destructible instance from the authoritative store and fill `outEvent`. Returns false (store
// unchanged) if the instance is absent, not flagged VegDestructible, or already removed.
bool DestroyVegetation(VegetationStore& store, const VegetationKey& key, VegetationDestroyedEvent& outEvent);

}  // namespace Next::vegetation
