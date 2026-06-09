// Water x Actuation composition (W29). The two systems that both run BEFORE the PhysicsSystem and both
// influence a dynamic body's motion are exercised together on one entity:
//   * ActuationSystem (ADR-0010) translates a MoveTarget intent into the body's LINEAR VELOCITY (a hard
//     SetLinearVelocity) and is the player's command channel.
//   * WaterForceSystem (ADR-0015) applies buoyancy as a FORCE (force accumulator, consumed at Step) plus
//     a drag/flow impulse — the environment reacting to the body.
// Because buoyancy rides the force accumulator (applied during Step, after BOTH systems have run),
// driving a boat horizontally must NOT sink it: the vertical equilibrium survives whatever the command
// channel does to the velocity. These pin that a player-driven boat crosses a pool while staying afloat,
// that exactly one writer owns the Transform, that buoyancy is robust to the two systems' relative order,
// and that the whole three-system composition is bit-deterministic.

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "next/gameapi/components.h"
#include "next/gameplay/actuation_system.h"
#include "next/physics/components.h"
#include "next/physics/physics_system.h"
#include "next/physics/reference_physics_world.h"
#include "next/runtime/transform.h"
#include "next/runtime/world.h"
#include "next/water/water_cell.h"
#include "next/water/water_def.h"
#include "next/water_world/water_force_system.h"
#include "next/water_world/water_store.h"

using namespace Next;
using namespace Next::physics;
using namespace Next::water;

namespace {

constexpr float kDt = 1.0f / 60.0f;

// A large still pool: surface y=0, floor y=-50, fresh water. Body id 1.
void LoadPool(WaterStore& store) {
    WaterBodyInstance b;
    b.bodyId = 1;
    b.type = static_cast<uint8_t>(WaterType::Pool);
    b.boundsMin[0] = -200.0f;
    b.boundsMin[1] = -50.0f;
    b.boundsMin[2] = -200.0f;
    b.boundsMax[0] = 200.0f;
    b.boundsMax[1] = 0.0f;
    b.boundsMax[2] = 200.0f;
    b.surfaceHeight = 0.0f;
    b.density = 1000.0f;
    b.linearDrag = 2.0f;
    b.quadraticDrag = 1.0f;
    b.flags = WaterBuoyant;
    const std::vector<uint8_t> blob = PackCell(0, 0, 4096.0f, {b});
    store.LoadCell(0, 0, blob.data(), blob.size());
}

// A buoyant boat: a box of density 600 (fraction 0.6). Half-extents (1,0.5,0.5) -> height 1, so the
// equilibrium bottom sits at -0.6 and the center floats at y = -0.1, independent of horizontal extents.
constexpr float kBoatEquilibriumY = -0.1f;

Entity SpawnBoat(World& world, float startX, float startY) {
    Entity e = world.CreateEntity();
    auto& t = world.AddComponent<TransformComponent>(e);
    t.position[0] = startX;
    t.position[1] = startY;
    RigidBodyComponent rb;
    rb.desc.motion = MotionType::Dynamic;
    rb.desc.shape = ShapeType::Box;
    rb.desc.halfExtents[0] = 1.0f;
    rb.desc.halfExtents[1] = 0.5f;
    rb.desc.halfExtents[2] = 0.5f;
    const float vTot = 8.0f * 1.0f * 0.5f * 0.5f;  // 2 m^3
    rb.desc.mass = 600.0f * vTot;                  // density 600 -> fraction 0.6
    rb.desc.position[0] = startX;
    rb.desc.position[1] = startY;
    world.AddComponent<RigidBodyComponent>(e, rb);
    return e;
}

gameapi::MoveTarget MoveTo(float x, float y, float z, float maxSpeed) {
    gameapi::MoveTarget mt;
    mt.target = {x, y, z};
    mt.maxSpeed = maxSpeed;
    mt.active = 1;
    return mt;
}

BodyId BodyOf(World& world, Entity e) {
    const RigidBodyComponent* rb = world.GetComponent<RigidBodyComponent>(e);
    return rb != nullptr ? rb->body : kInvalidBody;
}

}  // namespace

// A player drives a boat across a pool. The command channel (actuation -> body velocity) and the
// environment (water buoyancy -> force accumulator) must compose: the boat reaches its horizontal
// target AND stays afloat the entire way (never sinks toward the floor, never rocket-launches).
TEST(WaterActuation, PlayerDrivenBoatStaysAfloatWhileCrossing) {
    World world;
    auto physics = MakeReferencePhysicsWorld();
    WaterStore store;
    LoadPool(store);
    gameplay::ActuationSystem actuation(physics.get());
    WaterForceSystem waterForce(&store, physics.get());
    PhysicsSystem physicsSys(physics.get());
    // Canonical order: command (actuation) -> environment (water) -> integrate (physics).
    world.RegisterSystem(&actuation);
    world.RegisterSystem(&waterForce);
    world.RegisterSystem(&physicsSys);

    Entity boat = SpawnBoat(world, -20.0f, kBoatEquilibriumY);
    world.AddComponent<gameapi::MoveTarget>(boat, MoveTo(20.0f, kBoatEquilibriumY, 0.0f, 5.0f));

    float minY = 1e9f;
    float maxY = -1e9f;
    for (int i = 0; i < 1500; ++i) {
        world.Update(kDt);
        float pos[3];
        float rot[4];
        physics->GetTransform(BodyOf(world, boat), pos, rot);
        minY = std::min(minY, pos[1]);
        maxY = std::max(maxY, pos[1]);
    }

    float pos[3];
    float rot[4];
    physics->GetTransform(BodyOf(world, boat), pos, rot);
    // Reached the horizontal target (snapped on arrival) and held its lane in Z.
    EXPECT_NEAR(pos[0], 20.0f, 0.5f) << "boat did not cross to the target";
    EXPECT_NEAR(pos[2], 0.0f, 0.1f);
    // Stayed afloat near equilibrium the WHOLE crossing: never sank toward the -50 floor, never launched.
    EXPECT_GT(minY, -0.6f) << "boat sank while being driven";
    EXPECT_LT(maxY, 0.4f) << "boat rocket-launched while being driven";
    EXPECT_NEAR(pos[1], kBoatEquilibriumY, 0.12f) << "boat not at buoyant equilibrium at rest";

    const gameapi::MoveTarget* mt = world.GetComponent<gameapi::MoveTarget>(boat);
    ASSERT_NE(mt, nullptr);
    EXPECT_EQ(mt->active, 0);  // arrived -> deactivated
}

// Single Transform writer under composition: mid-flight, the ECS TransformComponent must equal the
// physics body's transform EXACTLY. If water (or actuation) also wrote the Transform, or physics
// double-integrated, these would diverge.
TEST(WaterActuation, SingleTransformWriterUnderComposition) {
    World world;
    auto physics = MakeReferencePhysicsWorld();
    WaterStore store;
    LoadPool(store);
    gameplay::ActuationSystem actuation(physics.get());
    WaterForceSystem waterForce(&store, physics.get());
    PhysicsSystem physicsSys(physics.get());
    world.RegisterSystem(&actuation);
    world.RegisterSystem(&waterForce);
    world.RegisterSystem(&physicsSys);

    Entity boat = SpawnBoat(world, -20.0f, kBoatEquilibriumY);
    world.AddComponent<gameapi::MoveTarget>(boat, MoveTo(20.0f, kBoatEquilibriumY, 0.0f, 5.0f));

    for (int i = 0; i < 60; ++i) {  // mid-flight
        world.Update(kDt);
    }
    float bodyPos[3];
    float bodyRot[4];
    physics->GetTransform(BodyOf(world, boat), bodyPos, bodyRot);
    const TransformComponent* t = world.GetComponent<TransformComponent>(boat);
    ASSERT_NE(t, nullptr);
    EXPECT_FLOAT_EQ(t->position[0], bodyPos[0]);
    EXPECT_FLOAT_EQ(t->position[1], bodyPos[1]);
    EXPECT_FLOAT_EQ(t->position[2], bodyPos[2]);
    EXPECT_GT(t->position[0], -20.0f);  // actually moved
    EXPECT_LT(t->position[0], 20.0f);   // not yet arrived
}

// Buoyancy is order-robust: it rides the force accumulator (applied at Step, after both systems), so the
// boat floats whether water runs before OR after actuation. The exact trajectory differs (the drag
// IMPULSE vs SetLinearVelocity interact order-dependently), but the afloat invariant must hold for both.
TEST(WaterActuation, BuoyancyComposesRegardlessOfSystemOrder) {
    auto runOrder = [](bool waterFirst, float& outFinalY, float& outFinalX, float& outMinY) {
        World world;
        auto physics = MakeReferencePhysicsWorld();
        WaterStore store;
        LoadPool(store);
        gameplay::ActuationSystem actuation(physics.get());
        WaterForceSystem waterForce(&store, physics.get());
        PhysicsSystem physicsSys(physics.get());
        if (waterFirst) {
            world.RegisterSystem(&waterForce);
            world.RegisterSystem(&actuation);
        } else {
            world.RegisterSystem(&actuation);
            world.RegisterSystem(&waterForce);
        }
        world.RegisterSystem(&physicsSys);

        Entity boat = SpawnBoat(world, -20.0f, kBoatEquilibriumY);
        world.AddComponent<gameapi::MoveTarget>(boat, MoveTo(20.0f, kBoatEquilibriumY, 0.0f, 5.0f));
        outMinY = 1e9f;
        for (int i = 0; i < 1500; ++i) {
            world.Update(kDt);
            float pos[3];
            float rot[4];
            physics->GetTransform(BodyOf(world, boat), pos, rot);
            outMinY = std::min(outMinY, pos[1]);
        }
        float pos[3];
        float rot[4];
        physics->GetTransform(BodyOf(world, boat), pos, rot);
        outFinalX = pos[0];
        outFinalY = pos[1];
    };

    for (const bool waterFirst : {false, true}) {
        float finalY = 0.0f;
        float finalX = 0.0f;
        float minY = 0.0f;
        runOrder(waterFirst, finalY, finalX, minY);
        EXPECT_GT(minY, -0.6f) << "sank, waterFirst=" << waterFirst;
        EXPECT_NEAR(finalY, kBoatEquilibriumY, 0.15f) << "not afloat at rest, waterFirst=" << waterFirst;
        EXPECT_NEAR(finalX, 20.0f, 0.5f) << "did not cross, waterFirst=" << waterFirst;
    }
}

// The full three-system composition is bit-deterministic across runs (a replay/anti-cheat prerequisite).
TEST(WaterActuation, DeterministicAcrossRuns) {
    auto run = [](std::vector<float>& trajectory) {
        World world;
        auto physics = MakeReferencePhysicsWorld();
        WaterStore store;
        LoadPool(store);
        gameplay::ActuationSystem actuation(physics.get());
        WaterForceSystem waterForce(&store, physics.get());
        PhysicsSystem physicsSys(physics.get());
        world.RegisterSystem(&actuation);
        world.RegisterSystem(&waterForce);
        world.RegisterSystem(&physicsSys);

        Entity boat = SpawnBoat(world, -20.0f, kBoatEquilibriumY);
        world.AddComponent<gameapi::MoveTarget>(boat, MoveTo(20.0f, kBoatEquilibriumY, 0.0f, 5.0f));
        for (int i = 0; i < 600; ++i) {
            world.Update(kDt);
            float pos[3];
            float rot[4];
            physics->GetTransform(BodyOf(world, boat), pos, rot);
            trajectory.push_back(pos[0]);
            trajectory.push_back(pos[1]);
        }
    };
    std::vector<float> a;
    std::vector<float> b;
    run(a);
    run(b);
    ASSERT_EQ(a.size(), b.size());
    for (size_t i = 0; i < a.size(); ++i) {
        EXPECT_FLOAT_EQ(a[i], b[i]) << "divergence at sample " << i;
    }
}
