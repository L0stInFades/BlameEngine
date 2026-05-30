// Jolt backend smoke test (ADR-0009). Built only when BUILD_WITH_JOLT=ON. Verifies the Jolt-backed
// IPhysicsWorld implements the same contract: a dynamic sphere dropped onto a static floor settles
// near the floor (true Jolt collision), and the factory reports the "jolt" backend.

#include <gtest/gtest.h>

#include "next/physics/jolt_physics_world.h"
#include "next/physics/physics_world.h"

using namespace Next::physics;

namespace {

TEST(JoltPhysics, BackendReportsJolt) {
    auto world = MakeJoltPhysicsWorld();
    EXPECT_STREQ(world->BackendName(), "jolt");
    EXPECT_EQ(world->BodyCount(), 0u);
}

TEST(JoltPhysics, SphereDropsAndRestsOnFloor) {
    auto world = MakeJoltPhysicsWorld();

    BodyDesc floor;
    floor.motion = MotionType::Static;
    floor.shape = ShapeType::Box;
    floor.halfExtents[0] = 50.0f;
    floor.halfExtents[1] = 0.5f;
    floor.halfExtents[2] = 50.0f;
    floor.position[1] = -0.5f;  // top surface at y = 0
    world->CreateBody(floor);

    BodyDesc ball;
    ball.motion = MotionType::Dynamic;
    ball.shape = ShapeType::Sphere;
    ball.halfExtents[0] = ball.halfExtents[1] = ball.halfExtents[2] = 0.5f;
    ball.position[1] = 5.0f;
    BodyId id = world->CreateBody(ball);
    ASSERT_TRUE(world->IsValid(id));
    EXPECT_EQ(world->BodyCount(), 2u);

    for (int i = 0; i < 600; ++i) {
        world->Step(1.0f / 60.0f);
    }

    float pos[3];
    float rot[4];
    world->GetTransform(id, pos, rot);
    EXPECT_GT(pos[1], 0.3f);  // did not fall through the floor
    EXPECT_LT(pos[1], 1.0f);  // settled near the floor top + radius (~0.5)
}

TEST(JoltPhysics, RaycastHitsFloor) {
    auto world = MakeJoltPhysicsWorld();
    BodyDesc floor;
    floor.motion = MotionType::Static;
    floor.shape = ShapeType::Box;
    floor.halfExtents[0] = 50.0f;
    floor.halfExtents[1] = 0.5f;
    floor.halfExtents[2] = 50.0f;
    floor.position[1] = -0.5f;  // top at y=0
    BodyId floorId = world->CreateBody(floor);

    const float origin[3] = {0, 5, 0};
    const float down[3] = {0, -1, 0};
    RaycastResult hit = world->Raycast(origin, down, 100.0f);
    EXPECT_TRUE(hit.hit);
    EXPECT_EQ(hit.body, floorId);
    EXPECT_NEAR(hit.distance, 5.0f, 0.05f);

    const float up[3] = {0, 1, 0};
    EXPECT_FALSE(world->Raycast(origin, up, 100.0f).hit);
}

}  // namespace
