#pragma once

#include "next/gameapi/world_query.h"

namespace Next {
class World;
}
namespace Next::physics {
struct IPhysicsWorld;
}

namespace Next::gameplay {

// Implements the Game API's IWorldQuery over the physics world (ADR-0010): it raycasts the physics
// world and maps the struck physics body back to its ECS entity (via RigidBodyComponent). This is
// the bridge that lets player code sense the physical world through the capability-gated Game API
// without gameapi ever depending on engine/physics.
class PhysicsWorldQuery final : public gameapi::IWorldQuery {
public:
    PhysicsWorldQuery(physics::IPhysicsWorld* physicsWorld, World* world) : physics_(physicsWorld), world_(world) {}

    gameapi::RaycastResult Raycast(const float origin[3], const float direction[3], float maxDistance) override;

private:
    physics::IPhysicsWorld* physics_;
    World* world_;
};

}  // namespace Next::gameplay
