// Physics core tests (ADR-0009): the IPhysicsWorld contract + deterministic reference backend, and
// the ECS PhysicsSystem that steps bodies and writes results back onto TransformComponent. The
// reference backend is a deterministic stand-in (AABB, no rotation); these tests pin gravity,
// resting-on-a-static-floor, body lifecycle, transform sync, and run-to-run determinism.

#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "next/physics/components.h"
#include "next/physics/physics_system.h"
#include "next/physics/physics_world.h"
#include "next/physics/reference_physics_world.h"
#include "next/runtime/transform.h"
#include "next/runtime/world.h"

using namespace Next;
using namespace Next::physics;

namespace {

constexpr float kDt = 1.0f / 60.0f;

BodyDesc Sphere(float x, float y, float z, float radius, MotionType motion = MotionType::Dynamic) {
    BodyDesc d;
    d.motion = motion;
    d.shape = ShapeType::Sphere;
    d.halfExtents[0] = d.halfExtents[1] = d.halfExtents[2] = radius;
    d.position[0] = x;
    d.position[1] = y;
    d.position[2] = z;
    return d;
}

BodyDesc Floor(float topY, float halfThickness) {
    BodyDesc d;
    d.motion = MotionType::Static;
    d.shape = ShapeType::Box;
    d.halfExtents[0] = 50.0f;
    d.halfExtents[1] = halfThickness;
    d.halfExtents[2] = 50.0f;
    d.position[0] = 0.0f;
    d.position[1] = topY - halfThickness;  // top surface at topY
    d.position[2] = 0.0f;
    return d;
}

// ---- IPhysicsWorld contract / reference backend ----

TEST(ReferencePhysics, BodyLifecycleAndAccessors) {
    auto world = MakeReferencePhysicsWorld();
    EXPECT_EQ(world->BodyCount(), 0u);
    EXPECT_FALSE(world->IsValid(kInvalidBody));

    BodyId id = world->CreateBody(Sphere(1, 2, 3, 0.5f));
    EXPECT_TRUE(world->IsValid(id));
    EXPECT_EQ(world->BodyCount(), 1u);

    float pos[3];
    float rot[4];
    world->GetTransform(id, pos, rot);
    EXPECT_FLOAT_EQ(pos[0], 1.0f);
    EXPECT_FLOAT_EQ(pos[1], 2.0f);
    EXPECT_FLOAT_EQ(rot[3], 1.0f);

    const float v[3] = {4, 5, 6};
    world->SetLinearVelocity(id, v);
    float out[3];
    world->GetLinearVelocity(id, out);
    EXPECT_FLOAT_EQ(out[1], 5.0f);

    world->DestroyBody(id);
    EXPECT_FALSE(world->IsValid(id));
    EXPECT_EQ(world->BodyCount(), 0u);
}

TEST(ReferencePhysics, DynamicFallsUnderGravity) {
    auto world = MakeReferencePhysicsWorld();  // gravity (0,-9.81,0)
    BodyId ball = world->CreateBody(Sphere(0, 10, 0, 0.5f));
    float pos0[3];
    float rot[4];
    world->GetTransform(ball, pos0, rot);
    world->Step(kDt);
    float pos1[3];
    world->GetTransform(ball, pos1, rot);
    EXPECT_LT(pos1[1], pos0[1]);  // moved downward
}

TEST(ReferencePhysics, DynamicRestsOnStaticFloor) {
    auto world = MakeReferencePhysicsWorld();
    world->CreateBody(Floor(0.0f, 0.5f));                    // floor top at y=0
    BodyId ball = world->CreateBody(Sphere(0, 5, 0, 0.5f));  // radius 0.5 -> rests at y=0.5
    for (int i = 0; i < 600; ++i) {
        world->Step(kDt);
    }
    float pos[3];
    float rot[4];
    world->GetTransform(ball, pos, rot);
    EXPECT_NEAR(pos[1], 0.5f, 1e-3f);
    float v[3];
    world->GetLinearVelocity(ball, v);
    EXPECT_NEAR(v[1], 0.0f, 1e-3f);
}

TEST(ReferencePhysics, StaticBodyNeverMoves) {
    auto world = MakeReferencePhysicsWorld();
    BodyId wall = world->CreateBody(Floor(0.0f, 0.5f));
    for (int i = 0; i < 100; ++i) {
        world->Step(kDt);
    }
    float pos[3];
    float rot[4];
    world->GetTransform(wall, pos, rot);
    EXPECT_FLOAT_EQ(pos[1], -0.5f);  // unchanged
}

TEST(ReferencePhysics, DeterministicAcrossRuns) {
    auto run = [](std::vector<float>& trajectory) {
        auto world = MakeReferencePhysicsWorld();
        world->CreateBody(Floor(0.0f, 0.5f));
        BodyId ball = world->CreateBody(Sphere(0.25f, 7, -0.3f, 0.5f));
        for (int i = 0; i < 300; ++i) {
            world->Step(kDt);
            float pos[3];
            float rot[4];
            world->GetTransform(ball, pos, rot);
            trajectory.push_back(pos[1]);
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

// ---- raycast ----

TEST(ReferencePhysics, RaycastHitsFloorAndMissesAway) {
    auto world = MakeReferencePhysicsWorld();
    BodyId floor = world->CreateBody(Floor(0.0f, 0.5f));  // top at y=0
    const float origin[3] = {0, 5, 0};
    const float down[3] = {0, -1, 0};

    RaycastResult hit = world->Raycast(origin, down, 100.0f);
    EXPECT_TRUE(hit.hit);
    EXPECT_EQ(hit.body, floor);
    EXPECT_NEAR(hit.distance, 5.0f, 1e-3f);
    EXPECT_NEAR(hit.point[1], 0.0f, 1e-3f);
    EXPECT_NEAR(hit.normal[1], 1.0f, 1e-3f);  // floor top normal points up

    const float up[3] = {0, 1, 0};
    EXPECT_FALSE(world->Raycast(origin, up, 100.0f).hit);  // pointing away
    EXPECT_FALSE(world->Raycast(origin, down, 2.0f).hit);  // floor beyond maxDistance
}

TEST(ReferencePhysics, RaycastReturnsNearestBody) {
    auto world = MakeReferencePhysicsWorld();
    BodyId nearBody = world->CreateBody(Sphere(0, 2, 0, 0.5f, MotionType::Static));
    world->CreateBody(Sphere(0, 8, 0, 0.5f, MotionType::Static));  // farther
    const float origin[3] = {0, 0, 0};
    const float up[3] = {0, 1, 0};

    RaycastResult hit = world->Raycast(origin, up, 100.0f);
    EXPECT_TRUE(hit.hit);
    EXPECT_EQ(hit.body, nearBody);
    EXPECT_NEAR(hit.distance, 1.5f, 1e-3f);  // sphere center y=2, radius 0.5 -> AABB bottom y=1.5
}

// ---- ECS PhysicsSystem integration ----

TEST(PhysicsSystem, DrivesTransformComponentAndRestsOnFloor) {
    World world;
    auto physics = MakeReferencePhysicsWorld();
    PhysicsSystem system(physics.get());
    world.RegisterSystem(&system);

    Entity floor = world.CreateEntity();
    auto& ft = world.AddComponent<TransformComponent>(floor);
    ft.position[1] = -0.5f;  // matched by the body seed; floor top at y=0
    RigidBodyComponent floorBody;
    floorBody.desc = Floor(0.0f, 0.5f);
    world.AddComponent<RigidBodyComponent>(floor, floorBody);

    Entity ball = world.CreateEntity();
    auto& bt = world.AddComponent<TransformComponent>(ball);
    bt.position[1] = 5.0f;
    RigidBodyComponent ballBody;
    ballBody.desc = Sphere(0, 5, 0, 0.5f);
    world.AddComponent<RigidBodyComponent>(ball, ballBody);

    for (int i = 0; i < 600; ++i) {
        world.Update(kDt);
    }

    const TransformComponent* t = world.GetComponent<TransformComponent>(ball);
    ASSERT_NE(t, nullptr);
    EXPECT_NEAR(t->position[1], 0.5f, 1e-3f);  // physics result flowed back to the Transform
    EXPECT_EQ(physics->BodyCount(), 2u);
}

TEST(PhysicsSystem, DestroyingEntityDestroysBody) {
    World world;
    auto physics = MakeReferencePhysicsWorld();
    PhysicsSystem system(physics.get());
    world.RegisterSystem(&system);

    Entity ball = world.CreateEntity();
    world.AddComponent<TransformComponent>(ball);
    RigidBodyComponent rb;
    rb.desc = Sphere(0, 5, 0, 0.5f);
    world.AddComponent<RigidBodyComponent>(ball, rb);

    world.Update(kDt);
    EXPECT_EQ(physics->BodyCount(), 1u);  // body created on first tick

    world.DestroyEntity(ball);
    world.Update(kDt);
    EXPECT_EQ(physics->BodyCount(), 0u);  // body reconciled away
}

}  // namespace
