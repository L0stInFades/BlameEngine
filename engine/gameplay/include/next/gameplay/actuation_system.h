#pragma once

#include "next/physics/physics_world.h"
#include "next/runtime/system.h"

namespace Next::gameplay {

// The single authoritative mover (ADR-0010). It consumes the MoveTarget intent state (set by the
// Game API's DefaultIntentResolver) and moves each entity by exactly ONE path, so TransformComponent
// never has two writers:
//   * physics entity (has a valid RigidBody): translate MoveTarget into the body's velocity toward
//     the target (capped at maxSpeed); the PhysicsSystem then integrates and is the sole Transform
//     writer. On arrival it snaps the body, zeroes velocity, and deactivates the MoveTarget.
//   * non-physics entity (no body): integrate the TransformComponent toward the target directly.
//
// Tick order: drain intents → resolver.Apply (sets MoveTarget) → ActuationSystem → PhysicsSystem
// (integrates physics, writes Transform). Register this BEFORE the PhysicsSystem.
class ActuationSystem : public System {
public:
    explicit ActuationSystem(physics::IPhysicsWorld* physicsWorld) : physics_(physicsWorld) {}

    void Update(float deltaTime) override;
    const char* GetName() const override { return "ActuationSystem"; }

private:
    physics::IPhysicsWorld* physics_;  // may be null: then all MoveTargets take the non-physics path
};

}  // namespace Next::gameplay
