#pragma once

#include <cstdint>

#include "next/water/water_def.h"

// Pure, physics-engine-free buoyancy force model (mirrors how engine/vegetation keeps its math in the
// core). Given the local fluid state and a body's LIVE kinematics, returns the force + drag impulse to
// hand to IPhysicsWorld::AddForce / AddImpulse. Unit-testable in isolation; the WaterForceSystem in
// engine/water_world is the thin ECS/physics wiring on top.
//
// Model (real, and stable at fixed dt on the explicit reference backend):
//  * BUOYANCY = Archimedes: F_up = rho * g * V_submerged, applied via AddForce. Gravity stays the
//    physics backend's, so the net (gravity down + buoyancy up) is physically correct and the float
//    settles where V_submerged/V_total == bodyDensity/rho (the equilibrium the scale test pins).
//  * DRAG (+ current advection) = damping of the velocity RELATIVE TO THE FLUID (the body's velocity
//    minus the current), applied as a velocity-CLAMPED impulse: dv = -k*u with k = clamp(rate*dt, 0, 1).
//    Because k <= 1 the impulse can at most cancel the relative velocity, NEVER reverse it — this is
//    the Jolt ApplyBuoyancyImpulse recipe that makes explicit drag UNCONDITIONALLY stable at fixed dt
//    (no "cork rockets out of the water" blow-up). The same term sweeps a body toward a river current.

namespace Next::water {

constexpr float kWaterGravity = 9.81f;  // |g| used for the Archimedes term (matches PhysicsConfig default)

// Local fluid state at a body, sampled by the caller this tick (surfaceHeight from SampleHeightFast).
struct FluidSample {
    float density = 1000.0f;
    float surfaceHeight = 0.0f;  // water surface Y at the body's XZ, this tick
    float floorY = -1.0e9f;      // container floor (boundsMin.y); -inf-ish for an open sea
    float flowVelocity[3] = {0.0f, 0.0f, 0.0f};
    float linearDrag = 2.0f;     // per-second damping rate
    float quadraticDrag = 1.0f;  // additional damping per (m/s) of relative speed
    uint16_t flags = 0;          // WaterFlags (WaterCurrent gates flow advection)
};

// A body's shape + LIVE kinematics. position/velocity MUST be the live physics state
// (IPhysicsWorld::GetTransform / GetLinearVelocity), never the creation BodyDesc (stale after spawn).
struct BodyBuoyancyInput {
    uint8_t shape = 0;  // 0 Sphere, 1 Box (mirrors Next::physics::ShapeType)
    float halfExtents[3] = {0.5f, 0.5f, 0.5f};
    float position[3] = {0.0f, 0.0f, 0.0f};
    float velocity[3] = {0.0f, 0.0f, 0.0f};
    float mass = 1.0f;
};

struct WaterForceOutput {
    float force[3] = {0.0f, 0.0f, 0.0f};        // -> IPhysicsWorld::AddForce (buoyancy), applied over the Step
    float dragImpulse[3] = {0.0f, 0.0f, 0.0f};  // -> IPhysicsWorld::AddImpulse (clamped drag/advection), this tick
    float submergedFraction = 0.0f;             // [0,1]
    float submersionDepth = 0.0f;               // surfaceHeight - centerY (>0 submerged)
    bool inWater = false;                       // submergedFraction > 0
};

// Compute the buoyancy force + drag impulse for one body in one water body this tick. `dt` is the
// fixed physics step. Deterministic; allocation-free.
WaterForceOutput ComputeWaterForce(const FluidSample& fluid, const BodyBuoyancyInput& body, float dt);

// --- Multi-point (pontoon) buoyancy: the BOAT physics foundation (ADR-0015 W5) ---

// One world-space force to apply at a world point (via IPhysicsWorld::AddForceAtPosition).
struct BuoyancyPointForce {
    float point[3] = {0.0f, 0.0f, 0.0f};
    float force[3] = {0.0f, 0.0f, 0.0f};
};

// Result of multi-point box buoyancy: a per-corner buoyancy + vertical-drag force (apply each via
// AddForceAtPosition -> net buoyancy + a RIGHTING torque + heave/pitch/roll damping), plus a COM
// horizontal/flow drag impulse (apply via AddImpulse). A tilted box self-rights; a raft floats level.
struct BoxBuoyancyResult {
    BuoyancyPointForce points[4];
    int pointCount = 0;
    float comDragImpulse[3] = {0.0f, 0.0f, 0.0f};
    float submergedFraction = 0.0f;  // averaged over the 4 corners (== bodyDensity/rho at level rest)
    bool inWater = false;
};

// Sample the 4 bottom corners of a BOX (center + quaternion `rot` (x,y,z,w), half-extents) against the
// body's (wavy) surface and compute the pontoon forces. `angVel` is the world-axis angular velocity.
// Deterministic; allocation-free.
BoxBuoyancyResult ComputeBoxBuoyancy(const WaterBodyInstance& water, const float center[3], const float rot[4],
                                     const float halfExtents[3], const float linVel[3], const float angVel[3],
                                     float mass, double timeSeconds, float dt);

}  // namespace Next::water
