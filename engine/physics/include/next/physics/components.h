#pragma once

#include "next/physics/physics_world.h"

namespace Next::physics {

// ECS component linking an entity to a physics body. `desc` holds the creation parameters; the
// PhysicsSystem lazily creates the body (seeding its transform from the entity's
// TransformComponent if present), fills `body`, steps the world, and — when `syncToTransform` —
// writes the body's transform back onto the entity's TransformComponent each tick. When the
// component or entity goes away, the system destroys the body.
struct RigidBodyComponent {
    BodyDesc desc;
    BodyId body = kInvalidBody;   // runtime link, owned by the PhysicsSystem
    bool syncToTransform = true;  // copy body transform -> TransformComponent after each step
};

}  // namespace Next::physics
