// LevelValidator: the defect gate. A canonical level validates clean; every ValidationCode is
// triggered by exactly one targeted mutation; the validator is total (accumulates all errors).

#include <gtest/gtest.h>

#include <cmath>
#include <functional>
#include <limits>
#include <string>
#include <vector>

#include "next/level/level_builder.h"
#include "next/level/level_validator.h"

using namespace Next::level;

namespace {

// A canonical, minimal VALID level: an agent at the origin, a tagged goal, one objective, and one
// non-failure win condition (reach the goal). Every reject test starts from a copy of this.
LevelDef MakeValid() {
    LevelBuilder b("tutorial-01", "Tutorial");
    const LevelEntityRef agent = b.AddEntity("agent");
    b.WithPosition(0.0f, 0.0f, 0.0f).WithTagIndex(1);
    b.SetAgent(agent);
    const LevelEntityRef goal = b.AddEntity("goal");
    b.WithPosition(5.0f, 0.0f, 0.0f).WithTagIndex(2);
    b.AddObjective(1, 0);
    WinConditionDef win;
    win.kind = WinKind::EntityReached;
    win.entity = agent;
    win.point[0] = 5.0f;
    win.radius = 0.6f;
    b.AddWinCondition(win);
    (void)goal;
    return b.Build();
}

TEST(LevelValidator, AcceptsCanonicalMinimalLevel) {
    const ValidationReport r = LevelValidator::Validate(MakeValid());
    EXPECT_TRUE(r.Ok()) << (r.errors.empty() ? "" : (std::string("first: ") + ToString(r.errors.front().code)));
}

// Each row mutates a valid level to trigger exactly one code, then asserts that code is present.
TEST(LevelValidator, RejectsEachBadCase) {
    struct Case {
        ValidationCode code;
        std::function<void(LevelDef&)> mutate;
    };
    const float kNaN = std::numeric_limits<float>::quiet_NaN();
    const std::vector<Case> cases = {
        {ValidationCode::EmptyLevelId, [](LevelDef& d) { d.metadata.id.clear(); }},
        {ValidationCode::UnknownSchemaVersion, [](LevelDef& d) { d.metadata.schemaVersion = 999; }},
        {ValidationCode::NoEntities, [](LevelDef& d) { d.entities.clear(); }},
        {ValidationCode::AgentRefDangling, [](LevelDef& d) { d.metadata.agent.value = 9999; }},
        {ValidationCode::AgentMissingTransform,
         [](LevelDef& d) {
             EntityDef e;
             e.ref.value = 50;
             e.hasTag = true;
             e.tag.bits = 1;
             d.entities.push_back(e);
             d.metadata.agent.value = 50;
         }},
        {ValidationCode::ReservedEntityRef,
         [](LevelDef& d) {
             EntityDef e;
             e.ref.value = 0;
             e.hasTag = true;
             d.entities.push_back(e);
         }},
        {ValidationCode::DuplicateEntityRef,
         [](LevelDef& d) {
             EntityDef e = d.entities.front();
             d.entities.push_back(e);  // same ref again
         }},
        {ValidationCode::EntityHasNoComponents,
         [](LevelDef& d) {
             EntityDef e;
             e.ref.value = 77;
             d.entities.push_back(e);
         }},
        {ValidationCode::DanglingParentRef, [](LevelDef& d) { d.entities.front().transform.parent.value = 4242; }},
        {ValidationCode::SelfParent, [](LevelDef& d) { d.entities.front().transform.parent = d.entities.front().ref; }},
        {ValidationCode::ParentCycle,
         [](LevelDef& d) {
             d.entities[0].transform.parent = d.entities[1].ref;
             d.entities[1].hasTransform = true;
             d.entities[1].transform.parent = d.entities[0].ref;
         }},
        {ValidationCode::NonFiniteTransform, [kNaN](LevelDef& d) { d.entities.front().transform.position[0] = kNaN; }},
        {ValidationCode::NonPositiveScale, [](LevelDef& d) { d.entities.front().transform.scale[1] = 0.0f; }},
        {ValidationCode::DegenerateQuaternion,
         [](LevelDef& d) {
             for (float& q : d.entities.front().transform.rotation)
                 q = 0.0f;
         }},
        {ValidationCode::InvalidBodyMotion,
         [](LevelDef& d) {
             d.entities.front().hasBody = true;
             d.entities.front().body.motion = 9;
         }},
        {ValidationCode::InvalidBodyShape,
         [](LevelDef& d) {
             d.entities.front().hasBody = true;
             d.entities.front().body.shape = 9;
         }},
        {ValidationCode::NonFiniteBodyField,
         [kNaN](LevelDef& d) {
             d.entities.front().hasBody = true;
             d.entities.front().body.mass = kNaN;
         }},
        {ValidationCode::NonPositiveBodyExtent,
         [](LevelDef& d) {
             d.entities.front().hasBody = true;
             d.entities.front().body.halfExtents[0] = 0.0f;
         }},
        {ValidationCode::BadRestitution,
         [](LevelDef& d) {
             d.entities.front().hasBody = true;
             d.entities.front().body.restitution = 2.0f;
         }},
        {ValidationCode::NonPositiveDynamicMass,
         [](LevelDef& d) {
             d.entities.front().hasBody = true;
             d.entities.front().body.motion = 2;  // Dynamic
             d.entities.front().body.mass = 0.0f;
         }},
        {ValidationCode::DuplicateObjectiveId, [](LevelDef& d) { d.objectives.push_back(ObjectiveDef{1, 3}); }},
        {ValidationCode::WinConditionObjectiveDangling,
         [](LevelDef& d) {
             WinConditionDef c;
             c.kind = WinKind::ObjectiveAtLeast;
             c.objectiveId = 9999;
             c.threshold = 1;
             d.winConditions.push_back(c);
         }},
        {ValidationCode::WinConditionEntityDangling, [](LevelDef& d) { d.winConditions.front().entity.value = 9999; }},
        {ValidationCode::WinConditionTagOutOfRange,
         [](LevelDef& d) {
             WinConditionDef c;
             c.kind = WinKind::AllTaggedDestroyed;
             c.tagIndex = 64;
             d.winConditions.push_back(c);
         }},
        {ValidationCode::WinConditionBadRadius, [](LevelDef& d) { d.winConditions.front().radius = -1.0f; }},
        {ValidationCode::WinConditionNonFinitePoint, [kNaN](LevelDef& d) { d.winConditions.front().point[2] = kNaN; }},
        {ValidationCode::NoWinCondition, [](LevelDef& d) { d.winConditions.clear(); }},
        // --- rules added after strict review ---
        {ValidationCode::NonFiniteMove,
         [kNaN](LevelDef& d) {
             d.entities.front().hasMove = true;
             d.entities.front().move.target[0] = kNaN;
         }},
        {ValidationCode::NegativeMoveSpeed,
         [](LevelDef& d) {
             d.entities.front().hasMove = true;
             d.entities.front().move.maxSpeed = -1.0f;
         }},
        {ValidationCode::EntityRefOutOfRange,
         [](LevelDef& d) {
             EntityDef e;
             e.ref.value = kMaxLevelEntities + 5;
             e.hasTag = true;
             d.entities.push_back(e);
         }},
        {ValidationCode::RenderableMissingTransform,
         [](LevelDef& d) {
             EntityDef e;
             e.ref.value = 61;
             e.hasRender = true;
             d.entities.push_back(e);
         }},
        {ValidationCode::MoveTargetInert,  // move with neither a body nor a transform: nothing to drive
         [](LevelDef& d) {
             EntityDef e;
             e.ref.value = 62;
             e.hasMove = true;
             e.move.active = true;
             d.entities.push_back(e);
         }},
        {ValidationCode::MoveTargetInert,  // move on a STATIC body: the body never moves
         [](LevelDef& d) {
             EntityDef e;
             e.ref.value = 67;
             e.hasMove = true;
             e.hasBody = true;
             e.body.motion = 0;  // Static
             d.entities.push_back(e);
         }},
        {ValidationCode::WinConditionEntityMissingTransform,
         [](LevelDef& d) {
             EntityDef tagOnly;
             tagOnly.ref.value = 63;
             tagOnly.hasTag = true;
             d.entities.push_back(tagOnly);
             WinConditionDef c;
             c.kind = WinKind::EntityReached;
             c.entity.value = 63;
             c.radius = 1.0f;
             d.winConditions.push_back(c);
         }},
        {ValidationCode::WinConditionEntityTransformStale,
         [](LevelDef& d) {
             EntityDef movingBody;
             movingBody.ref.value = 66;
             movingBody.hasTransform = true;
             movingBody.hasBody = true;
             movingBody.body.motion = 1;               // Kinematic (moves)
             movingBody.body.syncToTransform = false;  // transform stays frozen -> evaluator can't track
             d.entities.push_back(movingBody);
             WinConditionDef c;
             c.kind = WinKind::EntityReached;
             c.entity.value = 66;
             c.radius = 1.0f;
             d.winConditions.push_back(c);
         }},
        {ValidationCode::WinConditionTagNeverPresent,
         [](LevelDef& d) {
             WinConditionDef c;
             c.kind = WinKind::AllTaggedDestroyed;
             c.tagIndex = 40;  // MakeValid tags only bits 1 and 2
             d.winConditions.push_back(c);
         }},
        {ValidationCode::InvalidWinKind,
         [](LevelDef& d) {
             WinConditionDef c;
             c.kind = static_cast<WinKind>(99);
             d.winConditions.push_back(c);
         }},
    };

    for (const Case& c : cases) {
        LevelDef def = MakeValid();
        c.mutate(def);
        const ValidationReport r = LevelValidator::Validate(def);
        EXPECT_FALSE(r.Ok()) << "expected rejection for " << ToString(c.code);
        EXPECT_TRUE(r.Has(c.code)) << "missing expected code " << ToString(c.code);
    }
}

TEST(LevelValidator, CapacityOverflowRejected) {
    LevelDef def;
    def.metadata.id = "big";
    def.entities.resize(kMaxLevelEntities + 1);
    for (uint32_t i = 0; i < def.entities.size(); ++i) {
        def.entities[i].ref.value = i + 1;
        def.entities[i].hasTag = true;
    }
    WinConditionDef c;
    c.kind = WinKind::ObjectiveAtLeast;
    c.objectiveId = 1;
    def.objectives.push_back(ObjectiveDef{1, 0});
    def.winConditions.push_back(c);
    const ValidationReport r = LevelValidator::Validate(def);
    EXPECT_TRUE(r.Has(ValidationCode::CapacityOverflow));
}

TEST(LevelValidator, AccumulatesAllErrorsNotJustFirst) {
    LevelDef def = MakeValid();
    def.metadata.id.clear();                          // EmptyLevelId
    def.entities.front().transform.scale[0] = -1.0f;  // NonPositiveScale
    def.objectives.push_back(ObjectiveDef{1, 9});     // DuplicateObjectiveId
    const ValidationReport r = LevelValidator::Validate(def);
    EXPECT_GE(r.errors.size(), 3u);
    EXPECT_TRUE(r.Has(ValidationCode::EmptyLevelId));
    EXPECT_TRUE(r.Has(ValidationCode::NonPositiveScale));
    EXPECT_TRUE(r.Has(ValidationCode::DuplicateObjectiveId));
}

TEST(LevelValidator, DeterministicErrorOrder) {
    LevelDef def = MakeValid();
    def.metadata.id.clear();
    def.entities.front().transform.scale[0] = -1.0f;
    const ValidationReport a = LevelValidator::Validate(def);
    const ValidationReport b = LevelValidator::Validate(def);
    ASSERT_EQ(a.errors.size(), b.errors.size());
    for (size_t i = 0; i < a.errors.size(); ++i) {
        EXPECT_EQ(a.errors[i].code, b.errors[i].code);
    }
}

}  // namespace
