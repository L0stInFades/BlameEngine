#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Level-design data model (the authored artifact). Pure POD/STL — no engine-logic dependency, so a
// LevelDef is just data a designer builds (via LevelBuilder), a validator checks, and a loader
// instantiates into the authoritative ECS World. Integer-keyed and deterministic by construction:
// entity load order is the vector order, objectives key the ObjectiveStore (uint32), tags are bit
// indices 0..63 — matching the Game API contract, not the legacy string/variant Task System.

namespace Next::level {

// Append-only schema version (mirrors the Game API ABI version convention). The loader rejects a
// version it does not understand rather than best-effort loading it.
constexpr uint32_t kLevelSchemaVersion = 1;

// Hard cap on entities per level — a deterministic, auditable ceiling (CapacityOverflow otherwise).
constexpr uint32_t kMaxLevelEntities = 65536;

// Stable author-time handle into a LevelDef's entity list. NOT an ECS Entity — the loader resolves
// it to a live Entity. 0 is the invalid sentinel; the first defined entity gets ref 1.
struct LevelEntityRef {
    uint32_t value = 0;
    bool IsValid() const { return value != 0; }
    bool operator==(LevelEntityRef o) const { return value == o.value; }
};

// Optional components an entity may carry. The loader sets EXACTLY the components flagged present.
struct TransformDef {  // -> Next::TransformComponent
    float position[3] = {0.0f, 0.0f, 0.0f};
    float rotation[4] = {0.0f, 0.0f, 0.0f, 1.0f};  // quaternion (x,y,z,w)
    float scale[3] = {1.0f, 1.0f, 1.0f};
    LevelEntityRef parent;  // optional; resolved to an Entity in a second pass
};
struct TagDef {  // -> Next::gameapi::GameTag (bit indices 0..63)
    uint64_t bits = 0;
};
struct RenderDef {  // -> Next::boundary::RenderableComponent (streamed to UE5)
    uint32_t visual = 0;
    uint32_t animState = 0;
};
struct MoveDef {  // -> Next::gameapi::MoveTarget
    float target[3] = {0.0f, 0.0f, 0.0f};
    float maxSpeed = 0.0f;
    bool active = false;
};
struct ActionDef {  // -> Next::gameapi::ActionFlags (bit indices 0..31)
    uint32_t bits = 0;
};
struct BodyDefData {     // -> Next::physics::RigidBodyComponent.desc
    uint8_t motion = 2;  // 0 Static, 1 Kinematic, 2 Dynamic (mirrors physics::MotionType)
    uint8_t shape = 0;   // 0 Sphere, 1 Box (mirrors physics::ShapeType)
    float halfExtents[3] = {0.5f, 0.5f, 0.5f};
    float linearVelocity[3] = {0.0f, 0.0f, 0.0f};
    float mass = 1.0f;
    float restitution = 0.0f;
    bool syncToTransform = true;
};

struct EntityDef {
    LevelEntityRef ref;  // unique within the level; assigned 1-based by LevelBuilder
    std::string name;    // designer label; diagnostics only, never loaded as a component

    bool hasTransform = false;
    TransformDef transform;
    bool hasTag = false;
    TagDef tag;
    bool hasRender = false;
    RenderDef render;
    bool hasMove = false;
    MoveDef move;
    bool hasAction = false;
    ActionDef action;
    bool hasBody = false;
    BodyDefData body;
};

// Win/lose is data-driven and TOTAL: the evaluator handles every kind exhaustively, never throws.
enum class WinKind : uint8_t {
    ObjectiveAtLeast,    // ObjectiveStore[objectiveId] >= threshold
    ObjectiveEquals,     // ObjectiveStore[objectiveId] == threshold
    EntityReached,       // `entity` within `radius` of `point` (squared distance, no sqrt)
    AllTaggedDestroyed,  // no live entity carries tag bit `tagIndex`
};

struct WinConditionDef {
    WinKind kind = WinKind::ObjectiveAtLeast;
    bool isFailure = false;  // false: contributes to Win; true: triggers Loss when met (loss wins)
    uint32_t objectiveId = 0;
    int32_t threshold = 0;
    LevelEntityRef entity;  // for EntityReached
    float point[3] = {0.0f, 0.0f, 0.0f};
    float radius = 0.0f;    // for EntityReached (must be finite, >= 0)
    uint32_t tagIndex = 0;  // for AllTaggedDestroyed (0..63)
};

struct ObjectiveDef {
    uint32_t id = 0;
    int32_t initialState = 0;
};

struct LevelMetadata {
    std::string id;    // stable level id (non-empty), e.g. "tutorial-01"
    std::string name;  // display name (optional)
    uint32_t schemaVersion = kLevelSchemaVersion;
    LevelEntityRef agent;  // optional; the entity the sandbox's `self` will point at (must resolve)
};

struct LevelDef {
    LevelMetadata metadata;
    std::vector<EntityDef> entities;       // load order = vector order (deterministic)
    std::vector<ObjectiveDef> objectives;  // seeded into the ObjectiveStore
    std::vector<WinConditionDef> winConditions;
    // Outcome: Lost when ANY failure condition holds; else Won when ALL non-failure conditions hold;
    // else InProgress. A flat, total condition list (no boolean expression tree) keeps it bulletproof.
};

}  // namespace Next::level
