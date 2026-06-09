#pragma once

#include "next/gameapi/abi.h"

namespace Next::gameapi {

// Abstract spatial-query surface the Game API exposes for sensing (ADR-0007 / ADR-0010). It is
// deliberately physics-engine-free: gameapi defines the contract, and a higher layer (gameplay)
// implements it over the physics world. The GameApi facade holds an optional IWorldQuery* and a
// Sense-gated Raycast call delegates to it (returning Unsupported when none is wired). This keeps
// gameapi independent of engine/physics while still letting player code probe the physical world.
struct IWorldQuery {
    virtual ~IWorldQuery() = default;

    // Cast a ray from origin along direction (need not be normalized), up to maxDistance. Returns
    // the nearest entity struck (RaycastResult.hit == 0 when nothing is within range).
    virtual RaycastResult Raycast(const float origin[3], const float direction[3], float maxDistance) = 0;
};

// Abstract WATER-state query surface (ADR-0015 W10). Like IWorldQuery it is engine-free: gameapi defines
// the contract and the water layer (WaterWorldQuery) implements it over the authoritative WaterStore.
// The GameApi facade holds an optional IWaterQuery* and the Sense-gated GetWaterState call delegates to
// it (Unsupported when none is wired), so player code can read submersion/flow/conductivity without
// gameapi depending on engine/water.
struct IWaterQuery {
    virtual ~IWaterQuery() = default;

    // Water state at a world point. inWater == 0 (and the rest zeroed) when no body governs that XZ.
    virtual WaterStateResult QueryWater(const float point[3]) = 0;
};

}  // namespace Next::gameapi
