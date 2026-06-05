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

// ---- forces: AddForce / AddImpulse (the buoyancy / drag / flow substrate) ----

TEST(ReferencePhysics, AddForceAcceleratesDeterministically) {
    auto world = MakeReferencePhysicsWorld();
    BodyId ball = world->CreateBody(Sphere(0, 0, 0, 0.5f));  // mass 1
    const float fx[3] = {10.0f, 0.0f, 0.0f};
    world->AddForce(ball, fx);
    world->Step(kDt);
    float v[3];
    world->GetLinearVelocity(ball, v);
    EXPECT_NEAR(v[0], 10.0f * kDt, 1e-5f);   // dv = (F/m)*dt, m = 1
    EXPECT_NEAR(v[1], -9.81f * kDt, 1e-5f);  // gravity still integrates on y
}

TEST(ReferencePhysics, ForceAccumulatorClearsEachStep) {
    auto world = MakeReferencePhysicsWorld();
    BodyId ball = world->CreateBody(Sphere(0, 0, 0, 0.5f));
    const float fx[3] = {10.0f, 0.0f, 0.0f};
    world->AddForce(ball, fx);
    world->Step(kDt);  // x-force applied this step
    world->Step(kDt);  // no new force -> x-velocity must NOT grow further
    float v[3];
    world->GetLinearVelocity(ball, v);
    EXPECT_NEAR(v[0], 10.0f * kDt, 1e-5f);  // the accumulator was consumed after the 1st step
}

TEST(ReferencePhysics, SustainedBuoyantForceHoldsAltitude) {
    // The canonical buoyancy equilibrium: an upward force equal to weight (m*|g|) re-applied every
    // tick exactly cancels gravity, so the body neither sinks nor rises. This is precisely what the
    // water sim does once a float reaches its draft.
    auto world = MakeReferencePhysicsWorld();
    BodyId ball = world->CreateBody(Sphere(0, 5, 0, 0.5f));  // mass 1
    const float up[3] = {0.0f, 9.81f, 0.0f};
    for (int i = 0; i < 600; ++i) {
        world->AddForce(ball, up);
        world->Step(kDt);
    }
    float pos[3];
    float rot[4];
    world->GetTransform(ball, pos, rot);
    EXPECT_NEAR(pos[1], 5.0f, 1e-2f);  // altitude held
    float v[3];
    world->GetLinearVelocity(ball, v);
    EXPECT_NEAR(v[1], 0.0f, 1e-2f);
}

TEST(ReferencePhysics, ForceOnlyAffectsDynamicBodies) {
    auto world = MakeReferencePhysicsWorld();
    BodyId stat = world->CreateBody(Sphere(0, 0, 0, 0.5f, MotionType::Static));
    BodyId kin = world->CreateBody(Sphere(10, 0, 0, 0.5f, MotionType::Kinematic));
    const float big[3] = {1000.0f, 1000.0f, 1000.0f};
    world->AddForce(stat, big);
    world->AddForce(kin, big);
    world->Step(kDt);
    float vs[3];
    float vk[3];
    world->GetLinearVelocity(stat, vs);
    world->GetLinearVelocity(kin, vk);
    EXPECT_FLOAT_EQ(vs[0], 0.0f);  // forces ignored on non-dynamic bodies
    EXPECT_FLOAT_EQ(vk[0], 0.0f);
}

TEST(ReferencePhysics, ImpulseChangesVelocityInstantly) {
    auto world = MakeReferencePhysicsWorld();
    BodyId light = world->CreateBody(Sphere(0, 0, 0, 0.5f));  // mass 1
    BodyDesc heavyDesc = Sphere(5, 0, 0, 0.5f);
    heavyDesc.mass = 2.0f;
    BodyId heavy = world->CreateBody(heavyDesc);

    const float j[3] = {6.0f, 0.0f, 0.0f};
    world->AddImpulse(light, j);  // dv = 6 / 1 = 6
    world->AddImpulse(heavy, j);  // dv = 6 / 2 = 3
    float vl[3];
    float vh[3];
    world->GetLinearVelocity(light, vl);  // no Step: an impulse is instantaneous
    world->GetLinearVelocity(heavy, vh);
    EXPECT_FLOAT_EQ(vl[0], 6.0f);
    EXPECT_FLOAT_EQ(vh[0], 3.0f);
}

TEST(ReferencePhysics, ForcedSimDeterministicAcrossRuns) {
    auto run = [](std::vector<float>& traj) {
        auto world = MakeReferencePhysicsWorld();
        BodyId ball = world->CreateBody(Sphere(0.25f, 5, -0.3f, 0.5f));
        for (int i = 0; i < 300; ++i) {
            const float f[3] = {0.7f, 9.81f + 0.13f, -0.4f};  // a quirky sustained force
            world->AddForce(ball, f);
            world->Step(kDt);
            float pos[3];
            float rot[4];
            world->GetTransform(ball, pos, rot);
            traj.push_back(pos[0]);
            traj.push_back(pos[1]);
            traj.push_back(pos[2]);
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

// ---- torque / angular dynamics (boat physics foundation, ADR-0015 W5) ----

TEST(ReferencePhysics, AddTorqueSpinsBody) {
    auto world = MakeReferencePhysicsWorld();
    BodyId b = world->CreateBody(Sphere(0, 0, 0, 0.5f));  // free dynamic
    const float ty[3] = {0.0f, 5.0f, 0.0f};
    for (int i = 0; i < 30; ++i) {
        world->AddTorque(b, ty);
        world->Step(kDt);
    }
    float w[3];
    world->GetAngularVelocity(b, w);
    EXPECT_GT(w[1], 0.0f);  // spun up about +y
    EXPECT_NEAR(w[0], 0.0f, 1e-4f);
    EXPECT_NEAR(w[2], 0.0f, 1e-4f);
}

TEST(ReferencePhysics, AddForceAtPositionInducesRotationAndTranslation) {
    auto world = MakeReferencePhysicsWorld();
    BodyId b = world->CreateBody(Sphere(0, 0, 0, 0.5f));
    const float up[3] = {0.0f, 50.0f, 0.0f};
    const float point[3] = {1.0f, 0.0f, 0.0f};  // 1 m in +x from the COM
    world->AddForceAtPosition(b, up, point);
    world->Step(kDt);
    float v[3];
    float w[3];
    world->GetLinearVelocity(b, v);
    world->GetAngularVelocity(b, w);
    EXPECT_GT(v[1], 0.0f);  // the force pushed it up (net of gravity)
    EXPECT_GT(w[2], 0.0f);  // r(+x) x F(+y) = +z torque -> spin about +z
}

TEST(ReferencePhysics, NoTorqueKeepsOrientation) {
    auto world = MakeReferencePhysicsWorld();
    BodyId ball = world->CreateBody(Sphere(0, 10, 0, 0.5f));  // just falls; never torqued
    for (int i = 0; i < 100; ++i) {
        world->Step(kDt);
    }
    float pos[3];
    float rot[4];
    world->GetTransform(ball, pos, rot);
    EXPECT_NEAR(rot[0], 0.0f, 1e-6f);
    EXPECT_NEAR(rot[1], 0.0f, 1e-6f);
    EXPECT_NEAR(rot[2], 0.0f, 1e-6f);
    EXPECT_NEAR(rot[3], 1.0f, 1e-6f);  // identity preserved (angular integration is a no-op at rest)
}

TEST(ReferencePhysics, AngularDeterministicAcrossRuns) {
    auto run = [](std::vector<float>& traj) {
        auto world = MakeReferencePhysicsWorld();
        BodyId b = world->CreateBody(Sphere(0, 0, 0, 0.5f));
        for (int i = 0; i < 200; ++i) {
            const float tq[3] = {0.3f, 0.7f, -0.2f};
            world->AddTorque(b, tq);
            world->Step(kDt);
            float pos[3];
            float rot[4];
            world->GetTransform(b, pos, rot);
            traj.push_back(rot[0]);
            traj.push_back(rot[1]);
            traj.push_back(rot[2]);
            traj.push_back(rot[3]);
        }
    };
    std::vector<float> a;
    std::vector<float> b2;
    run(a);
    run(b2);
    ASSERT_EQ(a.size(), b2.size());
    for (size_t i = 0; i < a.size(); ++i) {
        EXPECT_FLOAT_EQ(a[i], b2[i]) << "angular divergence at sample " << i;
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
