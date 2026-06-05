#pragma once

#include <cstdint>
#include <unordered_map>

#include "next/boundary/transport.h"
#include "next/gameapi/sim_clock.h"
#include "next/physics/physics_world.h"
#include "next/runtime/system.h"
#include "next/water_world/water_store.h"

// The water <-> physics bridge (ADR-0015): each fixed tick it applies buoyancy + hydrodynamic drag +
// current to every Dynamic rigid body overlapping loaded water, via IPhysicsWorld::AddForce/AddImpulse
// (the pure-core ComputeWaterForce model). It reads each body's LIVE pose/velocity from the physics
// world (GetTransform/GetLinearVelocity) and its mass/shape from RigidBodyComponent.desc — it NEVER
// writes Transform, so the single-Transform-writer invariant (ActuationSystem) is untouched and water
// forces simply COMPOSE in the next PhysicsSystem::Step.
//
// TIME: the wave/flood surface is evaluated at the ONE authoritative gameapi::SimClock the sim shares
// (ADR-0007 determinism red line) — NOT a private accumulator — so the force system, the WaterWorldQuery,
// and a future renderer all agree on the surface at every tick. A null clock means t=0 (still water).
//
// ORDERING CONTRACT: register this system BEFORE PhysicsSystem so the forces it accumulates are applied
// in that same Step. On water entry/exit it emits a cosmetic splash/exit GameEvent through the boundary
// transport (when one is wired); it carries no authority.

namespace Next::water {

class WaterForceSystem : public Next::System {
public:
    WaterForceSystem(const WaterStore* store, Next::physics::IPhysicsWorld* physics,
                     const Next::gameapi::SimClock* clock = nullptr,
                     Next::boundary::ISnapshotTransport* transport = nullptr)
        : store_(store), physics_(physics), clock_(clock), transport_(transport) {}

    void Update(float deltaTime) override;
    const char* GetName() const override { return "WaterForceSystem"; }

    // The authoritative sim time the surface is evaluated at (from the shared SimClock; 0 if none).
    double SimTimeSeconds() const { return clock_ != nullptr ? clock_->seconds : 0.0; }
    // How many tracked bodies are currently submerged (for diagnostics/tests).
    size_t SubmergedBodyCount() const;

private:
    const WaterStore* store_;
    Next::physics::IPhysicsWorld* physics_;
    const Next::gameapi::SimClock* clock_;
    Next::boundary::ISnapshotTransport* transport_;
    std::unordered_map<uint64_t, bool> submerged_;  // entity (packed) -> was in water last tick (edge detect)
};

}  // namespace Next::water
