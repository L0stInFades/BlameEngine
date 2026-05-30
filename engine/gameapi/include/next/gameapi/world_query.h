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

}  // namespace Next::gameapi
