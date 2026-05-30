#pragma once

#include <memory>

#include "next/physics/physics_world.h"

namespace Next::physics {

// Construct the Jolt-backed physics world (ADR-0009). Available only when the project is built
// with BUILD_WITH_JOLT=ON (which compiles next_physics_jolt and FetchContent's JoltPhysics). This
// declaration is deliberately Jolt-free, so a caller that wants Jolt links next_physics_jolt and
// includes only this header — the core stays Jolt-agnostic.
std::unique_ptr<IPhysicsWorld> MakeJoltPhysicsWorld(const PhysicsConfig& config = {});

}  // namespace Next::physics
