// Water buoyancy system tests (ADR-0015) — the headline that SURPASSES vegetation: a real per-tick
// force simulation on the trunk (Jolt-free) reference backend. A World runs WaterForceSystem (buoyancy +
// velocity-clamped drag + current via IPhysicsWorld::AddForce/AddImpulse) BEFORE PhysicsSystem, and we
// assert the physics: bodies settle to the exact Archimedes equilibrium (submerged fraction ==
// bodyDensity/rho), nothing ever rocket-launches (the drag clamp), currents sweep bodies downstream,
// the sim is bit-deterministic across runs, and entering water raises a cosmetic splash event.

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "next/boundary/transport.h"
#include "next/physics/components.h"
#include "next/physics/physics_system.h"
#include "next/physics/reference_physics_world.h"
#include "next/runtime/transform.h"
#include "next/runtime/world.h"
#include "next/water/water_cell.h"
#include "next/water/water_def.h"
#include "next/water/water_volume.h"
#include "next/water_world/water_force_system.h"
#include "next/water_world/water_store.h"
#include "next/water_world/water_view.h"

using namespace Next;
using namespace Next::physics;
using namespace Next::water;

namespace {

constexpr float kDt = 1.0f / 60.0f;
constexpr float kPi = 3.14159265358979323846f;

// A large still pool: surface y=0, floor y=-50, density `rho`. Body id 1. Loaded into the store.
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

Entity SpawnSphere(World& world, float radius, float bodyDensity, float startY) {
    Entity e = world.CreateEntity();
    auto& t = world.AddComponent<TransformComponent>(e);
    t.position[1] = startY;
    RigidBodyComponent rb;
    rb.desc.motion = MotionType::Dynamic;
    rb.desc.shape = ShapeType::Sphere;
    rb.desc.halfExtents[0] = rb.desc.halfExtents[1] = rb.desc.halfExtents[2] = radius;
    const float vTot = (4.0f / 3.0f) * kPi * radius * radius * radius;
    rb.desc.mass = bodyDensity * vTot;
    rb.desc.position[1] = startY;
    world.AddComponent<RigidBodyComponent>(e, rb);
    return e;
}

BodyId BodyOf(World& world, Entity e) {
    const RigidBodyComponent* rb = world.GetComponent<RigidBodyComponent>(e);
    return rb != nullptr ? rb->body : kInvalidBody;
}

}  // namespace

TEST(WaterBuoyancy, SpheresSettleToArchimedesEquilibrium) {
    const float densities[] = {250.0f, 500.0f, 800.0f};
    const float r = 0.5f;
    for (const float rho : densities) {
        World world;
        auto physics = MakeReferencePhysicsWorld();
        WaterStore store;
        LoadPool(store, 1000.0f);
        WaterForceSystem waterForce(&store, physics.get());
        PhysicsSystem physicsSys(physics.get());
        world.RegisterSystem(&waterForce);  // BEFORE physics: its forces apply that same Step
        world.RegisterSystem(&physicsSys);

        Entity ball = SpawnSphere(world, r, rho, 0.6f);  // start just above the surface
        for (int i = 0; i < 4000; ++i) {
            world.Update(kDt);
        }

        float pos[3];
        float rot[4];
        physics->GetTransform(BodyOf(world, ball), pos, rot);
        float frac = 0.0f;
        SubmergedSphereVolume(pos[1], r, /*surfaceY*/ 0.0f, /*floorY*/ -50.0f, frac);
        EXPECT_NEAR(frac, rho / 1000.0f, 0.02f) << "rho=" << rho << " settledY=" << pos[1];
        float v[3];
        physics->GetLinearVelocity(BodyOf(world, ball), v);
        EXPECT_NEAR(v[1], 0.0f, 0.05f) << "rho=" << rho;  // at rest
    }
}

TEST(WaterBuoyancy, BoxSettlesToEquilibrium) {
    World world;
    auto physics = MakeReferencePhysicsWorld();
    WaterStore store;
    LoadPool(store, 1000.0f);
    WaterForceSystem waterForce(&store, physics.get());
    PhysicsSystem physicsSys(physics.get());
    world.RegisterSystem(&waterForce);
    world.RegisterSystem(&physicsSys);

    Entity e = world.CreateEntity();
    auto& t = world.AddComponent<TransformComponent>(e);
    t.position[1] = 0.5f;
    RigidBodyComponent rb;
    rb.desc.motion = MotionType::Dynamic;
    rb.desc.shape = ShapeType::Box;
    rb.desc.halfExtents[0] = rb.desc.halfExtents[1] = rb.desc.halfExtents[2] = 0.5f;
    const float vTot = 8.0f * 0.5f * 0.5f * 0.5f;  // 1 m^3
    rb.desc.mass = 600.0f * vTot;                  // density 600 -> fraction 0.6
    rb.desc.position[1] = 0.5f;
    world.AddComponent<RigidBodyComponent>(e, rb);

    for (int i = 0; i < 4000; ++i) {
        world.Update(kDt);
    }
    float pos[3];
    float rot[4];
    physics->GetTransform(BodyOf(world, e), pos, rot);
    // Box fraction = clamp((surfaceY - bottom)/height, 0, 1); at equilibrium that is 0.6 -> bottom = -0.6,
    // i.e. center y = -0.6 + 0.5 = -0.1.
    const float half[3] = {0.5f, 0.5f, 0.5f};
    float frac = 0.0f;
    SubmergedBoxVolume(pos[1], half, 0.0f, -50.0f, frac);
    EXPECT_NEAR(frac, 0.6f, 0.02f) << "settledY=" << pos[1];
}

TEST(WaterBuoyancy, BoxUsesPhysicsDefaultMassWhenDescMassIsZero) {
    World world;
    auto physics = MakeReferencePhysicsWorld();
    WaterStore store;
    LoadPool(store, 1000.0f);
    WaterForceSystem waterForce(&store, physics.get());
    PhysicsSystem physicsSys(physics.get());
    world.RegisterSystem(&waterForce);
    world.RegisterSystem(&physicsSys);

    Entity e = world.CreateEntity();
    auto& t = world.AddComponent<TransformComponent>(e);
    t.position[1] = -0.25f;
    RigidBodyComponent rb;
    rb.desc.motion = MotionType::Dynamic;
    rb.desc.shape = ShapeType::Box;
    rb.desc.halfExtents[0] = rb.desc.halfExtents[1] = rb.desc.halfExtents[2] = 0.5f;
    rb.desc.mass = 0.0f;  // physics normalizes this to 1; water must use the same effective mass.
    world.AddComponent<RigidBodyComponent>(e, rb);

    world.Update(kDt);  // creates the physics body
    world.Update(kDt);  // water force sees the live body

    EXPECT_EQ(waterForce.SubmergedBodyCount(), 1u);
}

TEST(WaterBuoyancy, SphereDragUsesPhysicsDefaultMassWhenDescMassIsZero) {
    World world;
    auto physics = MakeReferencePhysicsWorld();
    WaterStore store;
    LoadPool(store, 1000.0f);
    WaterForceSystem waterForce(&store, physics.get());
    PhysicsSystem physicsSys(physics.get());
    world.RegisterSystem(&waterForce);
    world.RegisterSystem(&physicsSys);

    Entity e = world.CreateEntity();
    auto& t = world.AddComponent<TransformComponent>(e);
    t.position[1] = -1.0f;
    RigidBodyComponent rb;
    rb.desc.motion = MotionType::Dynamic;
    rb.desc.shape = ShapeType::Sphere;
    rb.desc.halfExtents[0] = rb.desc.halfExtents[1] = rb.desc.halfExtents[2] = 0.5f;
    rb.desc.linearVelocity[0] = 10.0f;
    rb.desc.mass = 0.0f;  // physics normalizes this to 1; water drag must not disappear.
    world.AddComponent<RigidBodyComponent>(e, rb);

    world.Update(kDt);  // creates the physics body
    world.Update(kDt);  // applies water drag

    float v[3];
    physics->GetLinearVelocity(BodyOf(world, e), v);
    EXPECT_LT(v[0], 9.5f);
}

TEST(WaterBuoyancy, BoxSelfRightsFromTilt) {
    // The boat physics foundation (W5): a wide, flat raft dropped TILTED must self-right — the lower
    // corners are deeper -> more buoyancy -> a righting torque (multi-point pontoon buoyancy), with the
    // per-corner vertical drag damping the roll so it settles ~level.
    World world;
    auto physics = MakeReferencePhysicsWorld();
    WaterStore store;
    LoadPool(store, 1000.0f);
    WaterForceSystem waterForce(&store, physics.get());
    PhysicsSystem physicsSys(physics.get());
    world.RegisterSystem(&waterForce);
    world.RegisterSystem(&physicsSys);

    const float angle = 0.5f;  // initial tilt about +z, ~28.6 degrees
    const float qz = std::sin(angle * 0.5f);
    const float qw = std::cos(angle * 0.5f);

    Entity e = world.CreateEntity();
    auto& tc = world.AddComponent<TransformComponent>(e);
    tc.position[1] = 0.2f;
    tc.rotation[0] = 0.0f;
    tc.rotation[1] = 0.0f;
    tc.rotation[2] = qz;
    tc.rotation[3] = qw;
    RigidBodyComponent rb;
    rb.desc.motion = MotionType::Dynamic;
    rb.desc.shape = ShapeType::Box;
    rb.desc.halfExtents[0] = 1.0f;
    rb.desc.halfExtents[1] = 0.3f;  // flat raft (wide footprint, shallow draft -> strongly self-righting)
    rb.desc.halfExtents[2] = 1.0f;
    const float vTot = 8.0f * 1.0f * 0.3f * 1.0f;  // 2.4 m^3
    rb.desc.mass = 400.0f * vTot;                  // density 400 -> floats high
    rb.desc.position[1] = 0.2f;
    rb.desc.rotation[2] = qz;
    rb.desc.rotation[3] = qw;
    world.AddComponent<RigidBodyComponent>(e, rb);

    // body-up.y = 1 - 2(x^2 + z^2); cos(tilt). Starts at cos(0.5) ~ 0.878.
    auto upY = [](const float q[4]) { return 1.0f - (2.0f * ((q[0] * q[0]) + (q[2] * q[2]))); };

    for (int i = 0; i < 6000; ++i) {
        world.Update(kDt);
    }
    float pos[3];
    float rot[4];
    physics->GetTransform(BodyOf(world, e), pos, rot);
    EXPECT_GT(upY(rot), 0.97f) << "raft did not self-right; up.y=" << upY(rot);  // tilt < ~14 degrees

    float w[3];
    physics->GetAngularVelocity(BodyOf(world, e), w);
    const float spin = std::sqrt((w[0] * w[0]) + (w[1] * w[1]) + (w[2] * w[2]));
    EXPECT_LT(spin, 0.1f);  // and settled (roll damped out)
}

TEST(WaterBuoyancy, NoRocketLaunch) {
    // A very light body (density 150) dropped from a height plunges, is shoved back up hard, breaches,
    // re-enters... The velocity-clamped drag must keep its speed bounded the whole time (no blow-up).
    World world;
    auto physics = MakeReferencePhysicsWorld();
    WaterStore store;
    LoadPool(store, 1000.0f);
    WaterForceSystem waterForce(&store, physics.get());
    PhysicsSystem physicsSys(physics.get());
    world.RegisterSystem(&waterForce);
    world.RegisterSystem(&physicsSys);

    Entity ball = SpawnSphere(world, 0.5f, 150.0f, 4.0f);
    float maxSpeed = 0.0f;
    for (int i = 0; i < 6000; ++i) {
        world.Update(kDt);
        float v[3];
        physics->GetLinearVelocity(BodyOf(world, ball), v);
        const float speed = std::sqrt((v[0] * v[0]) + (v[1] * v[1]) + (v[2] * v[2]));
        if (speed > maxSpeed) {
            maxSpeed = speed;
        }
    }
    EXPECT_LT(maxSpeed, 30.0f);  // bounded — the drag clamp prevents energy gain / rocket-launch
}

TEST(WaterBuoyancy, CurrentSweepsDownstream) {
    World world;
    auto physics = MakeReferencePhysicsWorld();
    WaterStore store;
    const float flow[3] = {3.0f, 0.0f, 0.0f};  // river flowing +x at 3 m/s
    LoadPool(store, 1000.0f, flow);
    WaterForceSystem waterForce(&store, physics.get());
    PhysicsSystem physicsSys(physics.get());
    world.RegisterSystem(&waterForce);
    world.RegisterSystem(&physicsSys);

    Entity ball = SpawnSphere(world, 0.5f, 1000.0f, -2.0f);  // neutrally buoyant, fully submerged
    for (int i = 0; i < 1200; ++i) {
        world.Update(kDt);
    }
    float pos[3];
    float rot[4];
    physics->GetTransform(BodyOf(world, ball), pos, rot);
    float v[3];
    physics->GetLinearVelocity(BodyOf(world, ball), v);
    EXPECT_GT(pos[0], 10.0f);       // swept downstream
    EXPECT_NEAR(v[0], 3.0f, 0.5f);  // approaches the current speed
}

TEST(WaterBuoyancy, DeterministicReplay) {
    auto run = [](float& finalY) {
        World world;
        auto physics = MakeReferencePhysicsWorld();
        WaterStore store;
        LoadPool(store, 1000.0f);
        WaterForceSystem waterForce(&store, physics.get());
        PhysicsSystem physicsSys(physics.get());
        world.RegisterSystem(&waterForce);
        world.RegisterSystem(&physicsSys);
        Entity ball = SpawnSphere(world, 0.5f, 450.0f, 2.0f);
        for (int i = 0; i < 1500; ++i) {
            world.Update(kDt);
        }
        float pos[3];
        float rot[4];
        physics->GetTransform(BodyOf(world, ball), pos, rot);
        finalY = pos[1];
    };
    float a = 0.0f;
    float b = 0.0f;
    run(a);
    run(b);
    EXPECT_FLOAT_EQ(a, b);  // bit-identical across runs (replay / anti-cheat)
}

TEST(WaterBuoyancy, SplashEventOnWaterEntry) {
    World world;
    auto physics = MakeReferencePhysicsWorld();
    WaterStore store;
    LoadPool(store, 1000.0f);
    Next::boundary::InProcessTransport transport;
    WaterForceSystem waterForce(&store, physics.get(), nullptr, &transport);  // null clock: still water (t=0)
    PhysicsSystem physicsSys(physics.get());
    world.RegisterSystem(&waterForce);
    world.RegisterSystem(&physicsSys);

    SpawnSphere(world, 0.5f, 500.0f, 3.0f);  // starts dry, falls in
    bool gotSplash = false;
    for (int i = 0; i < 600; ++i) {
        world.Update(kDt);
        Next::boundary::GameEvent ev{};
        while (transport.PopEvent(ev)) {
            if (ev.type == kWaterEventSplash) {
                gotSplash = true;
            }
        }
        if (gotSplash) {
            break;
        }
    }
    EXPECT_TRUE(gotSplash);
}
