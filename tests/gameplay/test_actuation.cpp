// Actuation unification tests (ADR-0010). The ActuationSystem is the single authoritative mover:
// a physics-owned entity is driven via its body's velocity (physics writes the Transform), a
// non-physics entity has its Transform integrated directly. These pin that movement reaches the
// target on both paths, that a physics entity with BOTH a MoveTarget and a RigidBody is moved by
// exactly one writer (no overshoot / oscillation), and that it is deterministic.

#include <gtest/gtest.h>

#include <vector>

#include "next/gameapi/components.h"
#include "next/gameapi/world_query.h"
#include "next/gameplay/actuation_system.h"
#include "next/gameplay/physics_world_query.h"
#include "next/physics/components.h"
#include "next/physics/physics_system.h"
#include "next/physics/reference_physics_world.h"
#include "next/runtime/transform.h"
#include "next/runtime/world.h"

using namespace Next;

namespace {

constexpr float kDt = 1.0f / 60.0f;

gameapi::MoveTarget MoveTo(float x, float y, float z, float maxSpeed) {
    gameapi::MoveTarget mt;
    mt.target = {x, y, z};
    mt.maxSpeed = maxSpeed;
    mt.active = 1;
    return mt;
}

physics::RigidBodyComponent KinematicSphere(float radius) {
    physics::RigidBodyComponent rb;
    rb.desc.motion = physics::MotionType::Kinematic;  // velocity-driven, ignores gravity
    rb.desc.shape = physics::ShapeType::Sphere;
    rb.desc.halfExtents[0] = rb.desc.halfExtents[1] = rb.desc.halfExtents[2] = radius;
    return rb;
}

TEST(Actuation, PhysicsEntityReachesTargetViaBodyVelocity) {
    World world;
    auto physics = physics::MakeReferencePhysicsWorld();
    gameplay::ActuationSystem actuation(physics.get());
    physics::PhysicsSystem physicsSystem(physics.get());
    world.RegisterSystem(&actuation);      // actuation sets velocity...
    world.RegisterSystem(&physicsSystem);  // ...then physics integrates + writes Transform

    Entity e = world.CreateEntity();
    world.AddComponent<TransformComponent>(e);  // origin
    world.AddComponent<physics::RigidBodyComponent>(e, KinematicSphere(0.5f));
    world.AddComponent<gameapi::MoveTarget>(e, MoveTo(5.0f, 0.0f, 0.0f, 2.0f));

    for (int i = 0; i < 300; ++i) {
        world.Update(kDt);
    }

    const TransformComponent* t = world.GetComponent<TransformComponent>(e);
    ASSERT_NE(t, nullptr);
    // Reached the target exactly (snapped on arrival) — no overshoot, so exactly one writer moved
    // it. A second writer (direct Transform integration) would have pushed it past the snap point.
    EXPECT_FLOAT_EQ(t->position[0], 5.0f);
    EXPECT_FLOAT_EQ(t->position[1], 0.0f);

    const gameapi::MoveTarget* mt = world.GetComponent<gameapi::MoveTarget>(e);
    ASSERT_NE(mt, nullptr);
    EXPECT_EQ(mt->active, 0);  // deactivated on arrival
}

TEST(Actuation, PhysicsEntityTransformTracksBodyNotDoubleIntegrated) {
    World world;
    auto physics = physics::MakeReferencePhysicsWorld();
    gameplay::ActuationSystem actuation(physics.get());
    physics::PhysicsSystem physicsSystem(physics.get());
    world.RegisterSystem(&actuation);
    world.RegisterSystem(&physicsSystem);

    Entity e = world.CreateEntity();
    world.AddComponent<TransformComponent>(e);
    world.AddComponent<physics::RigidBodyComponent>(e, KinematicSphere(0.5f));
    world.AddComponent<gameapi::MoveTarget>(e, MoveTo(10.0f, 0.0f, 0.0f, 3.0f));

    // After 10 ticks (mid-flight), the Transform must equal exactly the physics body's position —
    // proving ActuationSystem did NOT also move the Transform itself.
    for (int i = 0; i < 10; ++i) {
        world.Update(kDt);
    }
    const physics::RigidBodyComponent* rb = world.GetComponent<physics::RigidBodyComponent>(e);
    ASSERT_NE(rb, nullptr);
    float bodyPos[3];
    float bodyRot[4];
    physics->GetTransform(rb->body, bodyPos, bodyRot);
    const TransformComponent* t = world.GetComponent<TransformComponent>(e);
    ASSERT_NE(t, nullptr);
    EXPECT_FLOAT_EQ(t->position[0], bodyPos[0]);
    EXPECT_GT(t->position[0], 0.0f);   // actually moved
    EXPECT_LT(t->position[0], 10.0f);  // not yet arrived
}

TEST(Actuation, NonPhysicsEntityIntegratesTransformDirectly) {
    World world;
    gameplay::ActuationSystem actuation(nullptr);  // no physics world -> non-physics path
    world.RegisterSystem(&actuation);

    Entity e = world.CreateEntity();
    world.AddComponent<TransformComponent>(e);
    world.AddComponent<gameapi::MoveTarget>(e, MoveTo(0.0f, 3.0f, 0.0f, 1.0f));

    for (int i = 0; i < 300; ++i) {
        world.Update(kDt);
    }
    const TransformComponent* t = world.GetComponent<TransformComponent>(e);
    ASSERT_NE(t, nullptr);
    EXPECT_FLOAT_EQ(t->position[1], 3.0f);
    EXPECT_EQ(world.GetComponent<gameapi::MoveTarget>(e)->active, 0);
}

// ---- PhysicsWorldQuery: the IWorldQuery bridge mapping a struck body back to its ECS entity ----

TEST(PhysicsWorldQuery, RaycastMapsHitBodyToEntity) {
    World world;
    auto physics = physics::MakeReferencePhysicsWorld();
    physics::PhysicsSystem physicsSystem(physics.get());
    world.RegisterSystem(&physicsSystem);

    Entity floor = world.CreateEntity();
    auto& ft = world.AddComponent<TransformComponent>(floor);
    ft.position[1] = -0.5f;
    physics::RigidBodyComponent rb;
    rb.desc.motion = physics::MotionType::Static;
    rb.desc.shape = physics::ShapeType::Box;
    rb.desc.halfExtents[0] = 50.0f;
    rb.desc.halfExtents[1] = 0.5f;
    rb.desc.halfExtents[2] = 50.0f;
    rb.desc.position[1] = -0.5f;  // top at y=0
    world.AddComponent<physics::RigidBodyComponent>(floor, rb);

    world.Update(1.0f / 60.0f);  // PhysicsSystem creates the body

    gameplay::PhysicsWorldQuery query(physics.get(), &world);
    const float origin[3] = {0, 5, 0};
    const float down[3] = {0, -1, 0};
    gameapi::RaycastResult hit = query.Raycast(origin, down, 100.0f);
    EXPECT_EQ(hit.hit, 1u);
    EXPECT_EQ(hit.entity, static_cast<gameapi::EntityId>(floor));  // mapped back to the ECS entity
    EXPECT_NEAR(hit.distance, 5.0f, 1e-3f);

    const float up[3] = {0, 1, 0};
    EXPECT_EQ(query.Raycast(origin, up, 100.0f).hit, 0u);  // miss
}

TEST(Actuation, DeterministicAcrossRuns) {
    auto run = [](std::vector<float>& trajectory) {
        World world;
        auto physics = physics::MakeReferencePhysicsWorld();
        gameplay::ActuationSystem actuation(physics.get());
        physics::PhysicsSystem physicsSystem(physics.get());
        world.RegisterSystem(&actuation);
        world.RegisterSystem(&physicsSystem);

        Entity e = world.CreateEntity();
        world.AddComponent<TransformComponent>(e);
        world.AddComponent<physics::RigidBodyComponent>(e, KinematicSphere(0.5f));
        world.AddComponent<gameapi::MoveTarget>(e, MoveTo(4.0f, 1.0f, -2.0f, 2.5f));

        for (int i = 0; i < 200; ++i) {
            world.Update(kDt);
            const TransformComponent* t = world.GetComponent<TransformComponent>(e);
            trajectory.push_back(t->position[0]);
        }
    };
    std::vector<float> a;
    std::vector<float> b;
    run(a);
    run(b);
    ASSERT_EQ(a.size(), b.size());
    for (size_t i = 0; i < a.size(); ++i) {
        EXPECT_FLOAT_EQ(a[i], b[i]) << "divergence at step " << i;
    }
}

}  // namespace
