// Headline buoyancy scenarios on the JOLT backend (W6). Built only when BUILD_WITH_JOLT=ON. The whole
// point of ADR-0009 is that the product runs on Jolt; the water force model (ComputeWaterForce /
// ComputeBoxBuoyancy) is applied through IPhysicsWorld::AddForce/AddImpulse/AddForceAtPosition, so it
// must produce the same PHYSICS on Jolt as on the reference backend — not just "compile against it".
// These re-run the reference backend's headline cases (Archimedes equilibrium, no rocket-launch, current
// sweep, and the boat self-right) on a real Jolt PhysicsSystem. A specific risk this guards: Jolt sleeps
// bodies at rest, so a settled float would stop receiving buoyancy and sink — the Jolt backend re-wakes
// the body on every AddForce, and these tests prove that keeps it afloat indefinitely.

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "next/physics/components.h"
#include "next/physics/jolt_physics_world.h"
#include "next/physics/physics_system.h"
#include "next/runtime/transform.h"
#include "next/runtime/world.h"
#include "next/water/water_cell.h"
#include "next/water/water_def.h"
#include "next/water/water_volume.h"
#include "next/water_world/water_force_system.h"
#include "next/water_world/water_store.h"

using namespace Next;
using namespace Next::physics;
using namespace Next::water;

namespace {

constexpr float kDt = 1.0f / 60.0f;
constexpr float kPi = 3.14159265358979323846f;

void LoadPool(WaterStore& store, float rho, const float flow[3] = nullptr) {
    WaterBodyInstance b;
    b.bodyId = 1;
    b.type = static_cast<uint8_t>(flow != nullptr ? WaterType::River : WaterType::Pool);
    b.boundsMin[0] = -200.0f;
    b.boundsMin[1] = -50.0f;
    b.boundsMin[2] = -200.0f;
    b.boundsMax[0] = 200.0f;
    b.boundsMax[1] = 0.0f;
    b.boundsMax[2] = 200.0f;
    b.surfaceHeight = 0.0f;
    b.density = rho;
    b.linearDrag = 2.0f;
    b.quadraticDrag = 1.0f;
    b.flags = WaterBuoyant;
    if (flow != nullptr) {
        b.flowVelocity[0] = flow[0];
        b.flowVelocity[1] = flow[1];
        b.flowVelocity[2] = flow[2];
        b.flags = static_cast<uint16_t>(WaterBuoyant | WaterCurrent);
    }
    const std::vector<uint8_t> blob = PackCell(0, 0, 4096.0f, {b});
    store.LoadCell(0, 0, blob.data(), blob.size());
}

Entity SpawnSphere(World& world, float radius, float bodyDensity, float startX, float startY) {
    Entity e = world.CreateEntity();
    auto& t = world.AddComponent<TransformComponent>(e);
    t.position[0] = startX;
    t.position[1] = startY;
    RigidBodyComponent rb;
    rb.desc.motion = MotionType::Dynamic;
    rb.desc.shape = ShapeType::Sphere;
    rb.desc.halfExtents[0] = rb.desc.halfExtents[1] = rb.desc.halfExtents[2] = radius;
    rb.desc.mass = bodyDensity * (4.0f / 3.0f) * kPi * radius * radius * radius;
    rb.desc.position[0] = startX;
    rb.desc.position[1] = startY;
    world.AddComponent<RigidBodyComponent>(e, rb);
    return e;
}

BodyId BodyOf(World& world, Entity e) {
    const RigidBodyComponent* rb = world.GetComponent<RigidBodyComponent>(e);
    return rb != nullptr ? rb->body : kInvalidBody;
}

}  // namespace

// The headline: spheres of varied density settle to the Archimedes equilibrium (submerged fraction ==
// bodyDensity/rho) on the REAL Jolt solver, and stay there (Jolt sleep does not strand them dry).
TEST(WaterBuoyancyJolt, SpheresSettleToArchimedesEquilibrium) {
    const float densities[] = {250.0f, 500.0f, 800.0f};
    const float r = 0.5f;
    for (const float rho : densities) {
        World world;
        auto physics = MakeJoltPhysicsWorld();
        WaterStore store;
        LoadPool(store, 1000.0f);
        WaterForceSystem waterForce(&store, physics.get());
        PhysicsSystem physicsSys(physics.get());
        world.RegisterSystem(&waterForce);
        world.RegisterSystem(&physicsSys);

        Entity ball = SpawnSphere(world, r, rho, 0.0f, 0.6f);
        for (int i = 0; i < 4000; ++i) {
            world.Update(kDt);
        }

        float pos[3];
        float rot[4];
        physics->GetTransform(BodyOf(world, ball), pos, rot);
        float frac = 0.0f;
        SubmergedSphereVolume(pos[1], r, /*surfaceY*/ 0.0f, /*floorY*/ -50.0f, frac);
        EXPECT_NEAR(frac, rho / 1000.0f, 0.04f) << "rho=" << rho << " settledY=" << pos[1];
        float v[3];
        physics->GetLinearVelocity(BodyOf(world, ball), v);
        EXPECT_NEAR(v[1], 0.0f, 0.1f) << "rho=" << rho;  // at rest
    }
}

// Numerical stability of the velocity-clamped drag (the "no cork-rocket" guarantee) on Jolt. A very
// light sphere released DEEP underwater is a stress case: a naive explicit drag at a fixed step can blow
// the velocity up unboundedly ("cork rockets out"). The clamp (k = clamp(rate*dt, 0, 1)) makes it
// unconditionally stable. We assert the PHYSICS, not a position bound: the peak speed stays physically
// bounded (a drag explosion would be hundreds of m/s, not single digits) and the body still settles to
// the Archimedes equilibrium at rest. (A styrofoam-light ball released 10 m down legitimately breaches
// the surface and leaps a few metres — that kinetic overshoot is real, so we do NOT forbid it.)
TEST(WaterBuoyancyJolt, ClampedDragIsStableForDeepLightRelease) {
    World world;
    auto physics = MakeJoltPhysicsWorld();
    WaterStore store;
    LoadPool(store, 1000.0f);
    WaterForceSystem waterForce(&store, physics.get());
    PhysicsSystem physicsSys(physics.get());
    world.RegisterSystem(&waterForce);
    world.RegisterSystem(&physicsSys);

    const float r = 0.5f;
    Entity ball = SpawnSphere(world, r, 100.0f, 0.0f, -10.0f);  // styrofoam-light, deep -> strong buoyancy
    float maxSpeed = 0.0f;
    for (int i = 0; i < 4000; ++i) {
        world.Update(kDt);
        float v[3];
        physics->GetLinearVelocity(BodyOf(world, ball), v);
        maxSpeed = std::max(maxSpeed, std::sqrt((v[0] * v[0]) + (v[1] * v[1]) + (v[2] * v[2])));
    }
    // Bounded peak speed => the drag never exploded (a cork-rocket bug is 100s of m/s here).
    EXPECT_LT(maxSpeed, 15.0f) << "clamped drag was unstable on Jolt (peak speed=" << maxSpeed << ")";
    // And it still comes to rest at the Archimedes equilibrium (fraction 0.1).
    float pos[3];
    float rot[4];
    physics->GetTransform(BodyOf(world, ball), pos, rot);
    float frac = 0.0f;
    SubmergedSphereVolume(pos[1], r, 0.0f, -50.0f, frac);
    EXPECT_NEAR(frac, 0.1f, 0.05f) << "settledY=" << pos[1];
    float v[3];
    physics->GetLinearVelocity(BodyOf(world, ball), v);
    EXPECT_NEAR(v[1], 0.0f, 0.1f);  // settled
}

// A river current sweeps a floating body downstream on Jolt (WaterCurrent -> flow drag via AddImpulse).
TEST(WaterBuoyancyJolt, CurrentSweepsFloatingBody) {
    World world;
    auto physics = MakeJoltPhysicsWorld();
    WaterStore store;
    const float flow[3] = {3.0f, 0.0f, 0.0f};  // 3 m/s downstream +x
    LoadPool(store, 1000.0f, flow);
    WaterForceSystem waterForce(&store, physics.get());
    PhysicsSystem physicsSys(physics.get());
    world.RegisterSystem(&waterForce);
    world.RegisterSystem(&physicsSys);

    Entity ball = SpawnSphere(world, 0.5f, 500.0f, 0.0f, 0.0f);
    for (int i = 0; i < 1200; ++i) {
        world.Update(kDt);
    }
    float pos[3];
    float rot[4];
    physics->GetTransform(BodyOf(world, ball), pos, rot);
    EXPECT_GT(pos[0], 5.0f) << "current did not sweep the body downstream on Jolt";
    float v[3];
    physics->GetLinearVelocity(BodyOf(world, ball), v);
    EXPECT_GT(v[0], 0.0f);            // moving downstream
    EXPECT_LT(v[0], flow[0] + 0.5f);  // but never faster than the current (drag-limited)
}

// The boat physics on Jolt (W5 x W6): a wide flat raft dropped TILTED self-rights via multi-point
// pontoon buoyancy (AddForceAtPosition -> Jolt force+torque about the COM), settling ~level.
TEST(WaterBuoyancyJolt, BoxSelfRightsFromTilt) {
    World world;
    auto physics = MakeJoltPhysicsWorld();
    WaterStore store;
    LoadPool(store, 1000.0f);
    WaterForceSystem waterForce(&store, physics.get());
    PhysicsSystem physicsSys(physics.get());
    world.RegisterSystem(&waterForce);
    world.RegisterSystem(&physicsSys);

    const float angle = 0.5f;  // ~28.6 deg initial roll about +z
    const float qz = std::sin(angle * 0.5f);
    const float qw = std::cos(angle * 0.5f);

    Entity e = world.CreateEntity();
    auto& tc = world.AddComponent<TransformComponent>(e);
    tc.position[1] = 0.2f;
    tc.rotation[2] = qz;
    tc.rotation[3] = qw;
    RigidBodyComponent rb;
    rb.desc.motion = MotionType::Dynamic;
    rb.desc.shape = ShapeType::Box;
    rb.desc.halfExtents[0] = 2.0f;  // wide raft
    rb.desc.halfExtents[1] = 0.25f;
    rb.desc.halfExtents[2] = 1.5f;
    rb.desc.mass = 500.0f * (8.0f * 2.0f * 0.25f * 1.5f);  // density 500
    rb.desc.position[1] = 0.2f;
    rb.desc.rotation[2] = qz;
    rb.desc.rotation[3] = qw;
    world.AddComponent<RigidBodyComponent>(e, rb);

    for (int i = 0; i < 5000; ++i) {
        world.Update(kDt);
    }
    float pos[3];
    float rot[4];
    physics->GetTransform(BodyOf(world, e), pos, rot);
    // Roll angle from the quaternion (rotation about z): theta = 2*atan2(|qz|, qw). Should be ~level.
    const float roll = 2.0f * std::atan2(std::fabs(rot[2]), std::fabs(rot[3]));
    EXPECT_LT(roll, 0.15f) << "raft did not self-right on Jolt (roll=" << roll << " rad)";
}
