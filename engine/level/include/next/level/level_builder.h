#pragma once

#include <string>

#include "next/level/level_def.h"

namespace Next::level {

// Fluent, designer-facing authoring API. Builds a LevelDef in memory; assigns dense 1-based
// LevelEntityRefs. The With* setters apply to the LAST entity added (or an explicit ref). This is
// the C++ authoring surface today; a future text/binary loader would emit the same LevelDef.
class LevelBuilder {
public:
    explicit LevelBuilder(std::string levelId, std::string displayName = {});

    // Define a new entity; returns its stable ref. Subsequent With* target this entity unless an
    // explicit ref is passed.
    LevelEntityRef AddEntity(std::string name = {});

    // Designate the agent (the entity `self` points at). Must be a ref returned by AddEntity.
    LevelBuilder& SetAgent(LevelEntityRef agent);

    LevelBuilder& WithTransform(const TransformDef& t, LevelEntityRef target = {});
    LevelBuilder& WithPosition(float x, float y, float z, LevelEntityRef target = {});
    LevelBuilder& WithParent(LevelEntityRef parent, LevelEntityRef target = {});
    LevelBuilder& WithTag(uint64_t bits, LevelEntityRef target = {});
    LevelBuilder& WithTagIndex(uint32_t tagIndex, LevelEntityRef target = {});  // set one bit (0..63)
    LevelBuilder& WithRenderable(uint32_t visual, uint32_t animState = 0, LevelEntityRef target = {});
    LevelBuilder& WithMoveTarget(const MoveDef& m, LevelEntityRef target = {});
    LevelBuilder& WithActionFlags(uint32_t bits, LevelEntityRef target = {});
    LevelBuilder& WithRigidBody(const BodyDefData& b, LevelEntityRef target = {});

    LevelBuilder& AddObjective(uint32_t id, int32_t initialState = 0);
    LevelBuilder& AddWinCondition(const WinConditionDef& c);

    const LevelDef& Def() const { return def_; }
    LevelDef Build() const { return def_; }

private:
    // Resolve the With* target: explicit ref if valid, else the last-added entity. Returns nullptr
    // if neither exists (a builder misuse — With* before AddEntity); callers no-op on nullptr.
    EntityDef* Resolve(LevelEntityRef target);

    LevelDef def_;
    LevelEntityRef last_;
    uint32_t nextRef_ = 1;
};

}  // namespace Next::level
