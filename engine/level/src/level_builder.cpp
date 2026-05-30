#include "next/level/level_builder.h"

#include <utility>

namespace Next::level {

LevelBuilder::LevelBuilder(std::string levelId, std::string displayName) {
    def_.metadata.id = std::move(levelId);
    def_.metadata.name = std::move(displayName);
    def_.metadata.schemaVersion = kLevelSchemaVersion;
}

LevelEntityRef LevelBuilder::AddEntity(std::string name) {
    EntityDef e;
    e.ref.value = nextRef_++;
    e.name = std::move(name);
    def_.entities.push_back(std::move(e));
    last_ = def_.entities.back().ref;
    return last_;
}

LevelBuilder& LevelBuilder::SetAgent(LevelEntityRef agent) {
    def_.metadata.agent = agent;
    return *this;
}

EntityDef* LevelBuilder::Resolve(LevelEntityRef target) {
    const LevelEntityRef r = target.IsValid() ? target : last_;
    if (!r.IsValid()) {
        return nullptr;
    }
    for (EntityDef& e : def_.entities) {
        if (e.ref == r) {
            return &e;
        }
    }
    return nullptr;
}

LevelBuilder& LevelBuilder::WithTransform(const TransformDef& t, LevelEntityRef target) {
    if (EntityDef* e = Resolve(target)) {
        e->hasTransform = true;
        e->transform = t;
    }
    return *this;
}

LevelBuilder& LevelBuilder::WithPosition(float x, float y, float z, LevelEntityRef target) {
    if (EntityDef* e = Resolve(target)) {
        e->hasTransform = true;
        e->transform.position[0] = x;
        e->transform.position[1] = y;
        e->transform.position[2] = z;
    }
    return *this;
}

LevelBuilder& LevelBuilder::WithParent(LevelEntityRef parent, LevelEntityRef target) {
    if (EntityDef* e = Resolve(target)) {
        e->hasTransform = true;  // parent lives on the transform component
        e->transform.parent = parent;
    }
    return *this;
}

LevelBuilder& LevelBuilder::WithTag(uint64_t bits, LevelEntityRef target) {
    if (EntityDef* e = Resolve(target)) {
        e->hasTag = true;
        e->tag.bits |= bits;
    }
    return *this;
}

LevelBuilder& LevelBuilder::WithTagIndex(uint32_t tagIndex, LevelEntityRef target) {
    if (tagIndex <= 63) {
        if (EntityDef* e = Resolve(target)) {
            e->hasTag = true;
            e->tag.bits |= (uint64_t{1} << tagIndex);
        }
    }
    return *this;
}

LevelBuilder& LevelBuilder::WithRenderable(uint32_t visual, uint32_t animState, LevelEntityRef target) {
    if (EntityDef* e = Resolve(target)) {
        e->hasRender = true;
        e->render.visual = visual;
        e->render.animState = animState;
    }
    return *this;
}

LevelBuilder& LevelBuilder::WithMoveTarget(const MoveDef& m, LevelEntityRef target) {
    if (EntityDef* e = Resolve(target)) {
        e->hasMove = true;
        e->move = m;
    }
    return *this;
}

LevelBuilder& LevelBuilder::WithActionFlags(uint32_t bits, LevelEntityRef target) {
    if (EntityDef* e = Resolve(target)) {
        e->hasAction = true;
        e->action.bits = bits;
    }
    return *this;
}

LevelBuilder& LevelBuilder::WithRigidBody(const BodyDefData& b, LevelEntityRef target) {
    if (EntityDef* e = Resolve(target)) {
        e->hasBody = true;
        e->body = b;
    }
    return *this;
}

LevelBuilder& LevelBuilder::AddObjective(uint32_t id, int32_t initialState) {
    def_.objectives.push_back(ObjectiveDef{id, initialState});
    return *this;
}

LevelBuilder& LevelBuilder::AddWinCondition(const WinConditionDef& c) {
    def_.winConditions.push_back(c);
    return *this;
}

}  // namespace Next::level
