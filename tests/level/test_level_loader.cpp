// LevelLoader: transactional (no partial load on a bad level), deterministic, and it sets exactly
// the declared components with the declared values.

#include <gtest/gtest.h>

#include "next/boundary/snapshot.h"
#include "next/gameapi/components.h"
#include "next/gameapi/objective_store.h"
#include "next/level/level_builder.h"
#include "next/level/level_loader.h"
#include "next/physics/components.h"
#include "next/runtime/transform.h"
#include "next/runtime/world.h"

using namespace Next;
using namespace Next::level;

namespace {

LevelDef MakeValid() {
    LevelBuilder b("lvl", "L");
    const LevelEntityRef agent = b.AddEntity("agent");
    b.WithPosition(0.0f, 0.0f, 0.0f).WithTagIndex(1);
    b.SetAgent(agent);
    b.AddEntity("goal");
    b.WithPosition(5.0f, 0.0f, 0.0f).WithTagIndex(2);
    b.AddObjective(7, 42);
    WinConditionDef win;
    win.kind = WinKind::EntityReached;
    win.entity = agent;
    win.point[0] = 5.0f;
    win.radius = 0.6f;
    b.AddWinCondition(win);
    return b.Build();
}

TEST(LevelLoader, RefusesToCreateWorldOnInvalidLevel) {
    LevelDef def = MakeValid();
    def.metadata.id.clear();  // invalid
    World world;
    gameapi::ObjectiveStore objectives;
    const LoadResult r = LevelLoader::Load(def, world, &objectives);
    EXPECT_FALSE(r.loaded);
    EXPECT_FALSE(r.report.Ok());
    EXPECT_EQ(world.GetEntityCount(), 0u);  // no partial entities
    EXPECT_EQ(objectives.Size(), 0u);       // no partial objective seeding
}

TEST(LevelLoader, RefusesOversizedRefWithoutPathologicalAllocation) {
    // A huge ref.value would, unguarded, size entityByRef to ~ref.value entries (multi-GB). The
    // validator rejects it (EntityRefOutOfRange) so Load returns early — no crash, World untouched.
    LevelDef def = MakeValid();
    def.entities.front().ref.value = 4000000000u;  // also breaks the agent ref, fine — we want rejection
    def.metadata.agent.value = 4000000000u;
    World world;
    const LoadResult r = LevelLoader::Load(def, world);
    EXPECT_FALSE(r.loaded);
    EXPECT_TRUE(r.report.Has(ValidationCode::EntityRefOutOfRange));
    EXPECT_EQ(world.GetEntityCount(), 0u);
}

TEST(LevelLoader, SetsExactComponentsPerEntity) {
    LevelBuilder b("lvl", "L");
    const LevelEntityRef full = b.AddEntity("full");
    TransformDef t;
    t.position[0] = 1.5f;
    t.position[1] = 2.5f;
    t.position[2] = 3.5f;
    t.scale[0] = t.scale[1] = t.scale[2] = 2.0f;
    b.WithTransform(t);
    b.WithTagIndex(5);
    b.WithRenderable(11, 3);
    MoveDef m;
    m.target[0] = 9.0f;
    m.maxSpeed = 4.0f;
    m.active = true;
    b.WithMoveTarget(m);
    b.WithActionFlags(0b101);
    BodyDefData body;
    body.motion = 1;  // Kinematic
    body.shape = 1;   // Box
    body.halfExtents[0] = body.halfExtents[1] = body.halfExtents[2] = 0.5f;
    b.WithRigidBody(body);
    b.SetAgent(full);

    const LevelEntityRef bare = b.AddEntity("bare");
    b.WithPosition(0.0f, 0.0f, 0.0f);  // transform only

    b.AddObjective(1, 0);
    WinConditionDef win;
    win.kind = WinKind::ObjectiveAtLeast;
    win.objectiveId = 1;
    win.threshold = 1;
    b.AddWinCondition(win);
    const LevelDef def = b.Build();

    World world;
    const LoadResult r = LevelLoader::Load(def, world);
    ASSERT_TRUE(r.loaded) << (r.report.errors.empty() ? "" : ToString(r.report.errors.front().code));

    const Entity e = r.level.EntityFor(full);
    const TransformComponent* tc = world.GetComponent<TransformComponent>(e);
    ASSERT_NE(tc, nullptr);
    EXPECT_FLOAT_EQ(tc->position[1], 2.5f);
    EXPECT_FLOAT_EQ(tc->scale[2], 2.0f);
    const gameapi::GameTag* tag = world.GetComponent<gameapi::GameTag>(e);
    ASSERT_NE(tag, nullptr);
    EXPECT_TRUE(tag->Has(5));
    const boundary::RenderableComponent* rnd = world.GetComponent<boundary::RenderableComponent>(e);
    ASSERT_NE(rnd, nullptr);
    EXPECT_EQ(rnd->visual, 11u);
    EXPECT_EQ(rnd->animState, 3u);
    const gameapi::MoveTarget* mt = world.GetComponent<gameapi::MoveTarget>(e);
    ASSERT_NE(mt, nullptr);
    EXPECT_FLOAT_EQ(mt->target.x, 9.0f);
    EXPECT_EQ(mt->active, 1);
    const gameapi::ActionFlags* af = world.GetComponent<gameapi::ActionFlags>(e);
    ASSERT_NE(af, nullptr);
    EXPECT_EQ(af->bits, 0b101u);
    const physics::RigidBodyComponent* rb = world.GetComponent<physics::RigidBodyComponent>(e);
    ASSERT_NE(rb, nullptr);
    EXPECT_EQ(rb->desc.motion, physics::MotionType::Kinematic);
    EXPECT_EQ(rb->desc.shape, physics::ShapeType::Box);
    EXPECT_FLOAT_EQ(rb->desc.position[0], 1.5f);  // body position seeded from transform

    // The bare entity has ONLY a transform — none of the optional components.
    const Entity eb = r.level.EntityFor(bare);
    EXPECT_NE(world.GetComponent<TransformComponent>(eb), nullptr);
    EXPECT_EQ(world.GetComponent<gameapi::GameTag>(eb), nullptr);
    EXPECT_EQ(world.GetComponent<physics::RigidBodyComponent>(eb), nullptr);
}

TEST(LevelLoader, SeedsBodyPositionAndRotationFromTransform) {
    LevelBuilder b("lvl", "L");
    const LevelEntityRef crate = b.AddEntity("crate");
    TransformDef t;
    t.position[0] = 4.0f;
    t.rotation[0] = 0.0f;
    t.rotation[1] = 0.0f;
    t.rotation[2] = 0.70710677f;  // 90deg about Z (unit quaternion)
    t.rotation[3] = 0.70710677f;
    b.WithTransform(t);
    BodyDefData body;
    body.motion = 0;  // Static
    body.shape = 1;   // Box
    b.WithRigidBody(body);
    b.SetAgent(crate);
    b.AddObjective(1, 0);
    WinConditionDef win;
    win.kind = WinKind::ObjectiveAtLeast;
    win.objectiveId = 1;
    b.AddWinCondition(win);

    World world;
    const LoadResult r = LevelLoader::Load(b.Build(), world);
    ASSERT_TRUE(r.loaded) << (r.report.errors.empty() ? "" : ToString(r.report.errors.front().code));
    const physics::RigidBodyComponent* rb = world.GetComponent<physics::RigidBodyComponent>(r.level.EntityFor(crate));
    ASSERT_NE(rb, nullptr);
    EXPECT_FLOAT_EQ(rb->desc.position[0], 4.0f);
    EXPECT_FLOAT_EQ(rb->desc.rotation[2], 0.70710677f);  // rotation seeded, not left at identity
    EXPECT_FLOAT_EQ(rb->desc.rotation[3], 0.70710677f);
}

TEST(LevelLoader, AcceptsTransformlessStaticCollider) {
    // A physics body is self-sufficient (desc carries pose/shape); a static collider with no
    // transform and no write-back is a legitimate, loadable level — the validator must NOT reject it.
    LevelBuilder b("lvl", "L");
    const LevelEntityRef agent = b.AddEntity("agent");
    b.WithPosition(0, 0, 0);
    b.SetAgent(agent);
    const LevelEntityRef wall = b.AddEntity("wall");
    BodyDefData body;
    body.motion = 0;               // Static
    body.syncToTransform = false;  // never writes back -> no transform needed
    b.WithRigidBody(body, wall);
    b.AddObjective(1, 0);
    WinConditionDef win;
    win.kind = WinKind::ObjectiveAtLeast;
    win.objectiveId = 1;
    b.AddWinCondition(win);

    World world;
    const LoadResult r = LevelLoader::Load(b.Build(), world);
    EXPECT_TRUE(r.loaded) << (r.report.errors.empty() ? "" : ToString(r.report.errors.front().code));
    const physics::RigidBodyComponent* rb = world.GetComponent<physics::RigidBodyComponent>(r.level.EntityFor(wall));
    ASSERT_NE(rb, nullptr);
    EXPECT_EQ(world.GetComponent<TransformComponent>(r.level.EntityFor(wall)), nullptr);  // no transform, allowed
    // Documented limitation: BodyDefData has no position field, so a transform-less body spawns at
    // the origin (desc default). Position a body elsewhere by giving its entity a transform.
    EXPECT_FLOAT_EQ(rb->desc.position[0], 0.0f);
    EXPECT_FLOAT_EQ(rb->desc.position[1], 0.0f);
    EXPECT_FLOAT_EQ(rb->desc.position[2], 0.0f);
}

TEST(LevelLoader, AcceptsMoveTargetOnBodyWithoutTransform) {
    // A MoveTarget on a physics body needs no transform: ActuationSystem's physics path drives the
    // body through the physics world. The validator must NOT reject this (MoveMissingTransform only
    // fires for move + no transform + no body).
    LevelBuilder b("lvl", "L");
    const LevelEntityRef agent = b.AddEntity("agent");
    b.WithPosition(0, 0, 0);
    b.SetAgent(agent);
    const LevelEntityRef crate = b.AddEntity("crate");
    BodyDefData body;
    body.motion = 1;  // Kinematic
    b.WithRigidBody(body, crate);
    MoveDef m;
    m.active = true;
    m.maxSpeed = 2.0f;
    b.WithMoveTarget(m, crate);  // move + body, no transform
    b.AddObjective(1, 0);
    WinConditionDef win;
    win.kind = WinKind::ObjectiveAtLeast;
    win.objectiveId = 1;
    b.AddWinCondition(win);

    World world;
    const LoadResult r = LevelLoader::Load(b.Build(), world);
    EXPECT_TRUE(r.loaded) << (r.report.errors.empty() ? "" : ToString(r.report.errors.front().code));
}

TEST(LevelLoader, BindsAgentAndSeedsObjectives) {
    const LevelDef def = MakeValid();
    World world;
    gameapi::ObjectiveStore objectives;
    const LoadResult r = LevelLoader::Load(def, world, &objectives);
    ASSERT_TRUE(r.loaded);
    EXPECT_TRUE(world.IsEntityValid(r.level.agent));
    EXPECT_EQ(r.level.agent, r.level.EntityFor(def.metadata.agent));
    int32_t state = -1;
    EXPECT_TRUE(objectives.Get(7, state));
    EXPECT_EQ(state, 42);
}

TEST(LevelLoader, ParentWiring) {
    LevelBuilder b("lvl", "L");
    const LevelEntityRef parent = b.AddEntity("parent");
    b.WithPosition(0, 0, 0);
    const LevelEntityRef child = b.AddEntity("child");
    b.WithPosition(1, 0, 0).WithParent(parent);
    b.SetAgent(parent);
    b.AddObjective(1, 0);
    WinConditionDef win;
    win.kind = WinKind::ObjectiveAtLeast;
    win.objectiveId = 1;
    b.AddWinCondition(win);

    World world;
    const LoadResult r = LevelLoader::Load(b.Build(), world);
    ASSERT_TRUE(r.loaded);
    const TransformComponent* ct = world.GetComponent<TransformComponent>(r.level.EntityFor(child));
    ASSERT_NE(ct, nullptr);
    EXPECT_EQ(ct->parent, r.level.EntityFor(parent));
}

TEST(LevelLoader, DeterministicAcrossTwoLoads) {
    const LevelDef def = MakeValid();
    World a;
    World b;
    const LoadResult ra = LevelLoader::Load(def, a);
    const LoadResult rb = LevelLoader::Load(def, b);
    ASSERT_TRUE(ra.loaded);
    ASSERT_TRUE(rb.loaded);
    EXPECT_EQ(a.GetEntityCount(), b.GetEntityCount());
    ASSERT_EQ(ra.level.entityByRef.size(), rb.level.entityByRef.size());
    for (const EntityDef& e : def.entities) {
        const Entity ea = ra.level.EntityFor(e.ref);
        const Entity eb = rb.level.EntityFor(e.ref);
        EXPECT_EQ(static_cast<uint64_t>(ea), static_cast<uint64_t>(eb));  // identical id+version
        const TransformComponent* ta = a.GetComponent<TransformComponent>(ea);
        const TransformComponent* tb = b.GetComponent<TransformComponent>(eb);
        ASSERT_EQ(ta != nullptr, tb != nullptr);
        if (ta != nullptr) {
            for (int k = 0; k < 3; ++k) {
                EXPECT_FLOAT_EQ(ta->position[k], tb->position[k]);
            }
        }
    }
}

}  // namespace
