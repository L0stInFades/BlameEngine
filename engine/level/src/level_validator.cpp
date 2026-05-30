#include "next/level/level_validator.h"

#include <cmath>
#include <functional>
#include <map>
#include <set>

namespace Next::level {

const char* ToString(ValidationCode code) {
    switch (code) {
        case ValidationCode::EmptyLevelId:
            return "EmptyLevelId";
        case ValidationCode::UnknownSchemaVersion:
            return "UnknownSchemaVersion";
        case ValidationCode::NoEntities:
            return "NoEntities";
        case ValidationCode::CapacityOverflow:
            return "CapacityOverflow";
        case ValidationCode::AgentRefDangling:
            return "AgentRefDangling";
        case ValidationCode::AgentMissingTransform:
            return "AgentMissingTransform";
        case ValidationCode::ReservedEntityRef:
            return "ReservedEntityRef";
        case ValidationCode::EntityRefOutOfRange:
            return "EntityRefOutOfRange";
        case ValidationCode::DuplicateEntityRef:
            return "DuplicateEntityRef";
        case ValidationCode::EntityHasNoComponents:
            return "EntityHasNoComponents";
        case ValidationCode::DanglingParentRef:
            return "DanglingParentRef";
        case ValidationCode::SelfParent:
            return "SelfParent";
        case ValidationCode::ParentCycle:
            return "ParentCycle";
        case ValidationCode::NonFiniteTransform:
            return "NonFiniteTransform";
        case ValidationCode::NonPositiveScale:
            return "NonPositiveScale";
        case ValidationCode::DegenerateQuaternion:
            return "DegenerateQuaternion";
        case ValidationCode::NonFiniteMove:
            return "NonFiniteMove";
        case ValidationCode::NegativeMoveSpeed:
            return "NegativeMoveSpeed";
        case ValidationCode::RenderableMissingTransform:
            return "RenderableMissingTransform";
        case ValidationCode::MoveTargetInert:
            return "MoveTargetInert";
        case ValidationCode::InvalidBodyMotion:
            return "InvalidBodyMotion";
        case ValidationCode::InvalidBodyShape:
            return "InvalidBodyShape";
        case ValidationCode::NonFiniteBodyField:
            return "NonFiniteBodyField";
        case ValidationCode::NonPositiveBodyExtent:
            return "NonPositiveBodyExtent";
        case ValidationCode::BadRestitution:
            return "BadRestitution";
        case ValidationCode::NonPositiveDynamicMass:
            return "NonPositiveDynamicMass";
        case ValidationCode::DuplicateObjectiveId:
            return "DuplicateObjectiveId";
        case ValidationCode::WinConditionObjectiveDangling:
            return "WinConditionObjectiveDangling";
        case ValidationCode::WinConditionEntityDangling:
            return "WinConditionEntityDangling";
        case ValidationCode::WinConditionEntityMissingTransform:
            return "WinConditionEntityMissingTransform";
        case ValidationCode::WinConditionEntityTransformStale:
            return "WinConditionEntityTransformStale";
        case ValidationCode::WinConditionTagOutOfRange:
            return "WinConditionTagOutOfRange";
        case ValidationCode::WinConditionTagNeverPresent:
            return "WinConditionTagNeverPresent";
        case ValidationCode::WinConditionBadRadius:
            return "WinConditionBadRadius";
        case ValidationCode::WinConditionNonFinitePoint:
            return "WinConditionNonFinitePoint";
        case ValidationCode::InvalidWinKind:
            return "InvalidWinKind";
        case ValidationCode::NoWinCondition:
            return "NoWinCondition";
    }
    return "Unknown";
}

bool ValidationReport::Has(ValidationCode code) const {
    for (const ValidationError& e : errors) {
        if (e.code == code) {
            return true;
        }
    }
    return false;
}

namespace {
bool Finite3(const float v[3]) {
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}
}  // namespace

ValidationReport LevelValidator::Validate(const LevelDef& def) {
    ValidationReport rep;
    auto add = [&](ValidationCode c, uint32_t idx, std::string detail) {
        rep.errors.push_back(ValidationError{c, idx, std::move(detail)});
    };

    // --- metadata ---
    if (def.metadata.id.empty()) {
        add(ValidationCode::EmptyLevelId, UINT32_MAX, "level metadata.id is empty");
    }
    if (def.metadata.schemaVersion != kLevelSchemaVersion) {
        add(ValidationCode::UnknownSchemaVersion, UINT32_MAX,
            "schemaVersion " + std::to_string(def.metadata.schemaVersion) + " unsupported");
    }
    if (def.entities.empty()) {
        add(ValidationCode::NoEntities, UINT32_MAX, "level has no entities");
    }
    if (def.entities.size() > kMaxLevelEntities) {
        add(ValidationCode::CapacityOverflow, UINT32_MAX, "entity count exceeds kMaxLevelEntities");
    }

    // --- ref table (deterministic std::map); detect reserved/duplicate refs ---
    std::map<uint32_t, uint32_t> refToIndex;
    for (uint32_t i = 0; i < def.entities.size(); ++i) {
        const uint32_t ref = def.entities[i].ref.value;
        if (ref == 0) {
            add(ValidationCode::ReservedEntityRef, i, "entity ref 0 is reserved/invalid");
            continue;
        }
        // The loader sizes entityByRef by max ref VALUE, so an unbounded ref would force a
        // pathological allocation. Bound it to the same auditable ceiling as the entity count.
        if (ref > kMaxLevelEntities) {
            add(ValidationCode::EntityRefOutOfRange, i,
                "entity ref " + std::to_string(ref) + " exceeds kMaxLevelEntities");
        }
        if (!refToIndex.emplace(ref, i).second) {
            add(ValidationCode::DuplicateEntityRef, i, "duplicate entity ref " + std::to_string(ref));
        }
    }
    auto resolves = [&](LevelEntityRef r) { return r.IsValid() && refToIndex.count(r.value) != 0; };

    // --- per-entity component checks (by ascending index) ---
    for (uint32_t i = 0; i < def.entities.size(); ++i) {
        const EntityDef& e = def.entities[i];
        if (!(e.hasTransform || e.hasTag || e.hasRender || e.hasMove || e.hasAction || e.hasBody)) {
            add(ValidationCode::EntityHasNoComponents, i, "entity '" + e.name + "' has no components");
        }
        if (e.hasTransform) {
            const TransformDef& t = e.transform;
            const bool rotFinite = std::isfinite(t.rotation[0]) && std::isfinite(t.rotation[1]) &&
                                   std::isfinite(t.rotation[2]) && std::isfinite(t.rotation[3]);
            if (!Finite3(t.position) || !rotFinite || !Finite3(t.scale)) {
                add(ValidationCode::NonFiniteTransform, i, "transform has a non-finite field");
            }
            if (t.scale[0] <= 0.0f || t.scale[1] <= 0.0f || t.scale[2] <= 0.0f) {
                add(ValidationCode::NonPositiveScale, i, "scale components must be > 0");
            }
            if (rotFinite) {
                const float q2 = t.rotation[0] * t.rotation[0] + t.rotation[1] * t.rotation[1] +
                                 t.rotation[2] * t.rotation[2] + t.rotation[3] * t.rotation[3];
                if (q2 < 1e-6f || std::fabs(q2 - 1.0f) > 1e-3f) {
                    add(ValidationCode::DegenerateQuaternion, i, "rotation quaternion is not unit length");
                }
            }
            if (t.parent.IsValid()) {
                if (t.parent.value == e.ref.value) {
                    add(ValidationCode::SelfParent, i, "entity is its own parent");
                } else if (!resolves(t.parent)) {
                    add(ValidationCode::DanglingParentRef, i, "parent ref does not resolve");
                }
            }
        }
        if (e.hasMove) {
            if (!Finite3(e.move.target) || !std::isfinite(e.move.maxSpeed)) {
                add(ValidationCode::NonFiniteMove, i, "move target/maxSpeed has a non-finite field");
            }
            if (std::isfinite(e.move.maxSpeed) && e.move.maxSpeed < 0.0f) {
                add(ValidationCode::NegativeMoveSpeed, i, "move maxSpeed must be >= 0");
            }
        }
        // A renderable with no transform never streams (the publisher iterates <Transform,
        // Renderable>). (A physics body is self-sufficient via its desc, so there is no
        // body-needs-transform rule.)
        if (e.hasRender && !e.hasTransform) {
            add(ValidationCode::RenderableMissingTransform, i, "renderable entity needs a transform");
        }
        // A MoveTarget can only move the entity if the actuator has something to drive: a NON-STATIC
        // physics body (ActuationSystem's physics path sets its velocity; the reference world only
        // integrates Kinematic/Dynamic) OR, for a non-physics entity, a transform the non-physics
        // path integrates. A move target on a Static body, or with neither body nor transform, is a
        // guaranteed silent no-op.
        if (e.hasMove) {
            const bool canMove = e.hasBody ? (e.body.motion != 0 /*Static*/) : e.hasTransform;
            if (!canMove) {
                add(ValidationCode::MoveTargetInert, i,
                    "move target can never move (needs a non-static body, or a transform when bodyless)");
            }
        }
        if (e.hasBody) {
            const BodyDefData& b = e.body;
            if (b.motion > 2) {
                add(ValidationCode::InvalidBodyMotion, i, "body motion enum out of range");
            }
            if (b.shape > 1) {
                add(ValidationCode::InvalidBodyShape, i, "body shape enum out of range");
            }
            if (!Finite3(b.halfExtents) || !Finite3(b.linearVelocity) || !std::isfinite(b.mass) ||
                !std::isfinite(b.restitution)) {
                add(ValidationCode::NonFiniteBodyField, i, "body has a non-finite field");
            }
            const bool box = b.shape == 1;  // 0 Sphere (radius = halfExtents[0]), 1 Box (all three)
            if (b.halfExtents[0] <= 0.0f || (box && (b.halfExtents[1] <= 0.0f || b.halfExtents[2] <= 0.0f))) {
                add(ValidationCode::NonPositiveBodyExtent, i, "body half-extent(s) must be > 0");
            }
            if (std::isfinite(b.restitution) && (b.restitution < 0.0f || b.restitution > 1.0f)) {
                add(ValidationCode::BadRestitution, i, "restitution must be in [0,1]");
            }
            if (b.motion == 2 && std::isfinite(b.mass) && b.mass <= 0.0f) {  // Dynamic
                add(ValidationCode::NonPositiveDynamicMass, i, "dynamic body mass must be > 0");
            }
        }
    }

    // --- parent-cycle detection over the functional parent graph (each node has <= 1 parent) ---
    // ITERATIVE (no recursion): a deep-but-acyclic chain up to kMaxLevelEntities must not overflow
    // the call stack inside the fail-closed validator. Each node has one parent, so we walk the
    // chain marking nodes Gray; hitting a Gray node closes a cycle and we report it at THAT node
    // (which is genuinely in the cycle, not merely pointing into it).
    {
        const uint32_t n = static_cast<uint32_t>(def.entities.size());
        enum Color : uint8_t { White, Gray, Black };
        std::vector<Color> color(n, White);
        auto parentIndex = [&](uint32_t u, uint32_t& out) -> bool {
            const EntityDef& e = def.entities[u];
            if (!e.hasTransform || !e.transform.parent.IsValid() || e.transform.parent.value == e.ref.value) {
                return false;
            }
            auto it = refToIndex.find(e.transform.parent.value);
            if (it == refToIndex.end()) {
                return false;
            }
            out = it->second;
            return true;
        };
        std::vector<uint32_t> path;
        for (uint32_t s = 0; s < n; ++s) {
            if (color[s] != White) {
                continue;
            }
            path.clear();
            uint32_t u = s;
            while (true) {
                if (color[u] == Gray) {  // back-edge to a node on the current path -> cycle at u
                    add(ValidationCode::ParentCycle, u, "transform parent chain forms a cycle");
                    break;
                }
                if (color[u] == Black) {  // joins an already-resolved chain -> no new cycle
                    break;
                }
                color[u] = Gray;
                path.push_back(u);
                uint32_t p = 0;
                if (!parentIndex(u, p)) {
                    break;  // chain ends (no/unresolved/self parent)
                }
                u = p;
            }
            for (uint32_t x : path) {
                color[x] = Black;
            }
        }
    }

    // --- agent ---
    if (def.metadata.agent.IsValid()) {
        if (!resolves(def.metadata.agent)) {
            add(ValidationCode::AgentRefDangling, UINT32_MAX, "agent ref does not resolve");
        } else {
            const uint32_t idx = refToIndex[def.metadata.agent.value];
            if (!def.entities[idx].hasTransform) {
                add(ValidationCode::AgentMissingTransform, idx, "agent entity needs a transform");
            }
        }
    }

    // --- objectives (duplicate ids) ---
    std::set<uint32_t> objIds;
    for (const ObjectiveDef& o : def.objectives) {
        if (!objIds.insert(o.id).second) {
            add(ValidationCode::DuplicateObjectiveId, UINT32_MAX, "duplicate objective id " + std::to_string(o.id));
        }
    }

    // --- win conditions ---
    bool hasNonFailure = false;
    for (const WinConditionDef& c : def.winConditions) {
        bool knownKind = true;
        switch (c.kind) {
            case WinKind::ObjectiveAtLeast:
            case WinKind::ObjectiveEquals:
                if (objIds.count(c.objectiveId) == 0) {
                    add(ValidationCode::WinConditionObjectiveDangling, UINT32_MAX,
                        "win condition references undeclared objective " + std::to_string(c.objectiveId));
                }
                break;
            case WinKind::EntityReached:
                if (!resolves(c.entity)) {
                    add(ValidationCode::WinConditionEntityDangling, UINT32_MAX,
                        "win condition references an unresolved entity");
                } else {
                    // The evaluator reads the target's TransformComponent.position. It must exist AND
                    // actually track the entity's real position. A PARENT is fine (nothing composes
                    // parent transforms, so mover and evaluator share the same raw field). But a
                    // physics body with syncToTransform=false leaves the transform frozen at its spawn
                    // pose while the body moves — the read position never tracks the body, so the
                    // condition can never fire. Both are silent-never-fires the gate must catch.
                    const EntityDef& te = def.entities[refToIndex[c.entity.value]];
                    if (!te.hasTransform) {
                        add(ValidationCode::WinConditionEntityMissingTransform, refToIndex[c.entity.value],
                            "EntityReached target has no transform; condition can never fire");
                    } else if (te.hasBody && !te.body.syncToTransform && te.body.motion != 0 /*Static*/) {
                        // A MOVING body (Kinematic/Dynamic) with syncToTransform=false leaves the
                        // transform frozen while the body moves -> the read position never tracks it.
                        // (A Static body never moves, so its frozen transform stays accurate — allowed.)
                        add(ValidationCode::WinConditionEntityTransformStale, refToIndex[c.entity.value],
                            "EntityReached target is a moving body with syncToTransform=false; its "
                            "transform never tracks the body's motion");
                    }
                }
                if (!Finite3(c.point)) {
                    add(ValidationCode::WinConditionNonFinitePoint, UINT32_MAX, "win condition point is non-finite");
                }
                if (!std::isfinite(c.radius) || c.radius < 0.0f) {
                    add(ValidationCode::WinConditionBadRadius, UINT32_MAX,
                        "win condition radius must be finite and >= 0");
                }
                break;
            case WinKind::AllTaggedDestroyed:
                if (c.tagIndex > 63) {
                    add(ValidationCode::WinConditionTagOutOfRange, UINT32_MAX, "win condition tagIndex > 63");
                } else {
                    bool present = false;
                    for (const EntityDef& e : def.entities) {
                        if (e.hasTag && ((e.tag.bits >> c.tagIndex) & 1ull) != 0) {
                            present = true;
                            break;
                        }
                    }
                    if (!present) {
                        // No entity carries the tag -> "all destroyed" is vacuously true at t=0 -> an
                        // instant, accidental win/loss. ASSUMPTION: nothing mutates GameTags at runtime
                        // in this engine (GameTag is read-only over the Game API — QueryByTag/Sense;
                        // there is no tag-write call and no runtime entity-spawn path), so a tag absent
                        // statically is absent forever. If a spawn/wave system that adds tagged entities
                        // at runtime is introduced, downgrade this to a warning or gate it on !isFailure.
                        add(ValidationCode::WinConditionTagNeverPresent, UINT32_MAX,
                            "AllTaggedDestroyed tag " + std::to_string(c.tagIndex) + " is carried by no entity");
                    }
                }
                break;
            default:
                knownKind = false;
                add(ValidationCode::InvalidWinKind, UINT32_MAX, "win condition kind is not a recognized WinKind");
                break;
        }
        if (knownKind && !c.isFailure) {
            hasNonFailure = true;
        }
    }
    if (!hasNonFailure) {
        add(ValidationCode::NoWinCondition, UINT32_MAX, "level declares no non-failure win condition");
    }

    return rep;
}

}  // namespace Next::level
