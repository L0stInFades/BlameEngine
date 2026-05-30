#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "next/level/level_def.h"

namespace Next::level {

// Every way a LevelDef can be defective. The validator is TOTAL and FAIL-CLOSED: it accumulates
// ALL violations (never early-exits) and the loader refuses to create any World entity if the
// report is non-empty. This is the single authoring-time gate for "zero defects" — a malformed or
// contradictory level is rejected here, not discovered at runtime (where, e.g., an out-of-range
// tag would silently no-op and a win condition would silently never fire).
enum class ValidationCode : uint16_t {
    EmptyLevelId,
    UnknownSchemaVersion,
    NoEntities,
    CapacityOverflow,
    AgentRefDangling,
    AgentMissingTransform,

    ReservedEntityRef,    // ref.value == 0
    EntityRefOutOfRange,  // ref.value > kMaxLevelEntities (would force a pathological load alloc)
    DuplicateEntityRef,
    EntityHasNoComponents,

    DanglingParentRef,
    SelfParent,
    ParentCycle,

    NonFiniteTransform,  // NaN/Inf in position/rotation/scale
    NonPositiveScale,
    DegenerateQuaternion,  // zero quaternion (or not unit within tolerance)

    NonFiniteMove,      // NaN/Inf in MoveTarget.target / maxSpeed
    NegativeMoveSpeed,  // maxSpeed < 0 (would drive the entity away from its target)

    RenderableMissingTransform,  // a renderable with no transform never streams to the UE5 view
    MoveTargetInert,             // a MoveTarget that can never move the entity (no non-static body AND no transform)

    InvalidBodyMotion,
    InvalidBodyShape,
    NonFiniteBodyField,
    NonPositiveBodyExtent,
    BadRestitution,  // not in [0,1]
    NonPositiveDynamicMass,

    DuplicateObjectiveId,

    WinConditionObjectiveDangling,       // references an objective not declared
    WinConditionEntityDangling,          // references an entity not declared
    WinConditionEntityMissingTransform,  // EntityReached target has no transform -> can never fire
    WinConditionEntityTransformStale,    // EntityReached target's transform is decoupled from its body
                                         // (syncToTransform=false)
    WinConditionTagOutOfRange,           // tagIndex > 63
    WinConditionTagNeverPresent,         // AllTaggedDestroyed for a tag no entity carries -> instant win
    WinConditionBadRadius,               // EntityReached radius non-finite or < 0
    WinConditionNonFinitePoint,
    InvalidWinKind,  // kind is not a recognized WinKind
    NoWinCondition,  // no non-failure win condition declared
};

const char* ToString(ValidationCode code);

struct ValidationError {
    ValidationCode code;
    uint32_t entityIndex = UINT32_MAX;  // index into LevelDef.entities, or UINT32_MAX (not entity-scoped)
    std::string detail;                 // deterministic, human-readable
};

struct ValidationReport {
    std::vector<ValidationError>
        errors;  // deterministic order: metadata, entities (by index), objectives, win conditions
    bool Ok() const { return errors.empty(); }
    bool Has(ValidationCode code) const;
};

class LevelValidator {
public:
    // Pure: no World, no mutation. Emits errors in a fixed pass order so output is byte-stable.
    static ValidationReport Validate(const LevelDef& def);
};

}  // namespace Next::level
