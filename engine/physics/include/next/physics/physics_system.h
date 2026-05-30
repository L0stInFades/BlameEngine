#pragma once

#include <cstdint>
#include <unordered_map>

#include "next/physics/physics_world.h"
#include "next/runtime/system.h"

namespace Next::physics {

// The ECS ↔ physics bridge (ADR-0009). Each fixed tick it: (1) reconciles bodies — creates one for
// every RigidBodyComponent that lacks a live body (seeding the body's transform from the entity's
// TransformComponent), and destroys bodies whose entity or component has gone away; (2) steps the
// IPhysicsWorld by the fixed dt; (3) writes each synced body's transform back onto its
// TransformComponent, so physics results flow on through the snapshot publisher to the UE5 view.
//
// Lifecycle is reconciled in Update() rather than via OnComponentAdded/OnEntityDestroyed hooks, so
// it is robust to component-add ordering and to a body's entity being destroyed without notice.
class PhysicsSystem : public System {
public:
    explicit PhysicsSystem(IPhysicsWorld* physics) : physics_(physics) {}

    void Update(float deltaTime) override;
    void Shutdown() override;
    const char* GetName() const override { return "PhysicsSystem"; }

    IPhysicsWorld* PhysicsWorld() const { return physics_; }

private:
    void Reconcile();
    void SyncTransforms();

    IPhysicsWorld* physics_;
    std::unordered_map<uint64_t, BodyId> entityToBody_;  // ECS entity (packed) -> physics body
};

}  // namespace Next::physics
