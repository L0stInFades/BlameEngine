// WinEvaluator: each WinKind evaluates deterministically and flips with world/objective state;
// loss takes priority; unsatisfiable reads (untouched objective, destroyed entity) are total-false.

#include <gtest/gtest.h>

#include "next/gameapi/components.h"
#include "next/gameapi/objective_store.h"
#include "next/level/level_builder.h"
#include "next/level/level_loader.h"
#include "next/level/win_conditions.h"
#include "next/runtime/transform.h"
#include "next/runtime/world.h"

using namespace Next;
using namespace Next::level;

namespace {

// Base level: agent (ref 1) at origin tagged 1; sensor (ref 2) at (2,0,0) tagged 3; objective 1 = 0.
LevelDef MakeBase() {
    LevelBuilder b("conds", "C");
    const LevelEntityRef agent = b.AddEntity("agent");
    b.WithPosition(0, 0, 0).WithTagIndex(1);
    b.SetAgent(agent);
    b.AddEntity("sensor");
    b.WithPosition(2, 0, 0).WithTagIndex(3);
    b.AddObjective(1, 0);
    WinConditionDef win;
    win.kind = WinKind::ObjectiveAtLeast;
    win.objectiveId = 1;
    win.threshold = 1;
    b.AddWinCondition(win);
    return b.Build();
}

WinConditionDef ObjAtLeast(uint32_t id, int32_t thr) {
    WinConditionDef c;
    c.kind = WinKind::ObjectiveAtLeast;
    c.objectiveId = id;
    c.threshold = thr;
    return c;
}

TEST(WinConditions, ObjectiveComparators) {
    World world;
    gameapi::ObjectiveStore objectives;
    const LoadResult r = LevelLoader::Load(MakeBase(), world, &objectives);
    ASSERT_TRUE(r.loaded);

    WinConditionDef atLeast5 = ObjAtLeast(1, 5);
    EXPECT_FALSE(WinEvaluator::EvaluateCondition(atLeast5, world, &objectives, r.level));
    objectives.Set(1, 5);
    EXPECT_TRUE(WinEvaluator::EvaluateCondition(atLeast5, world, &objectives, r.level));

    WinConditionDef equals5 = atLeast5;
    equals5.kind = WinKind::ObjectiveEquals;
    EXPECT_TRUE(WinEvaluator::EvaluateCondition(equals5, world, &objectives, r.level));
    objectives.Set(1, 6);
    EXPECT_FALSE(WinEvaluator::EvaluateCondition(equals5, world, &objectives, r.level));

    // Untouched objective -> total-false, never a crash.
    EXPECT_FALSE(WinEvaluator::EvaluateCondition(ObjAtLeast(999, 0), world, &objectives, r.level));
    // No store at all -> total-false.
    EXPECT_FALSE(WinEvaluator::EvaluateCondition(atLeast5, world, nullptr, r.level));
}

TEST(WinConditions, EntityReachedFlips) {
    World world;
    gameapi::ObjectiveStore objectives;
    const LevelDef def = MakeBase();
    const LoadResult r = LevelLoader::Load(def, world, &objectives);
    ASSERT_TRUE(r.loaded);

    WinConditionDef reach;
    reach.kind = WinKind::EntityReached;
    reach.entity = def.metadata.agent;
    reach.point[0] = 0.0f;
    reach.radius = 0.5f;
    EXPECT_TRUE(WinEvaluator::EvaluateCondition(reach, world, &objectives, r.level));  // agent at origin

    world.GetComponent<TransformComponent>(r.level.agent)->position[0] = 10.0f;
    EXPECT_FALSE(WinEvaluator::EvaluateCondition(reach, world, &objectives, r.level));

    // Destroyed/invalid entity -> total-false.
    reach.entity.value = 4242;
    EXPECT_FALSE(WinEvaluator::EvaluateCondition(reach, world, &objectives, r.level));
}

TEST(WinConditions, AllTaggedDestroyed) {
    World world;
    gameapi::ObjectiveStore objectives;
    LevelDef def = MakeBase();
    const LoadResult r = LevelLoader::Load(def, world, &objectives);
    ASSERT_TRUE(r.loaded);

    WinConditionDef cleared;
    cleared.kind = WinKind::AllTaggedDestroyed;
    cleared.tagIndex = 3;  // the sensor carries tag 3
    EXPECT_FALSE(WinEvaluator::EvaluateCondition(cleared, world, &objectives, r.level));

    world.DestroyEntity(r.level.EntityFor(def.entities[1].ref));  // destroy the sensor
    EXPECT_TRUE(WinEvaluator::EvaluateCondition(cleared, world, &objectives, r.level));

    // Documented semantics: a tag no live entity carries is vacuously "all destroyed" -> true. (The
    // validator warns about this statically via WinConditionTagNeverPresent; the evaluator is total.)
    WinConditionDef neverPresent;
    neverPresent.kind = WinKind::AllTaggedDestroyed;
    neverPresent.tagIndex = 40;
    EXPECT_TRUE(WinEvaluator::EvaluateCondition(neverPresent, world, &objectives, r.level));
}

TEST(WinConditions, OutcomeAndLossPriority) {
    LevelBuilder b("outcome", "O");
    const LevelEntityRef agent = b.AddEntity("agent");
    b.WithPosition(0, 0, 0);
    b.SetAgent(agent);
    b.AddObjective(1, 0);
    b.AddWinCondition(ObjAtLeast(1, 5));  // non-failure win
    WinConditionDef fail;                 // failure: agent reaches the trap at (10,0,0)
    fail.kind = WinKind::EntityReached;
    fail.isFailure = true;
    fail.entity = agent;
    fail.point[0] = 10.0f;
    fail.radius = 0.5f;
    b.AddWinCondition(fail);
    const LevelDef def = b.Build();

    World world;
    gameapi::ObjectiveStore objectives;
    const LoadResult r = LevelLoader::Load(def, world, &objectives);
    ASSERT_TRUE(r.loaded);

    EXPECT_EQ(WinEvaluator::Evaluate(def, world, &objectives, r.level), LevelOutcome::InProgress);
    objectives.Set(1, 5);
    EXPECT_EQ(WinEvaluator::Evaluate(def, world, &objectives, r.level), LevelOutcome::Won);
    world.GetComponent<TransformComponent>(r.level.agent)->position[0] = 10.0f;               // trip the failure
    EXPECT_EQ(WinEvaluator::Evaluate(def, world, &objectives, r.level), LevelOutcome::Lost);  // loss wins
}

}  // namespace
