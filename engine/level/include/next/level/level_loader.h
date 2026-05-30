#pragma once

#include <vector>

#include "next/level/level_def.h"
#include "next/level/level_validator.h"
#include "next/runtime/entity.h"

namespace Next {
class World;
namespace gameapi {
class ObjectiveStore;
}
}  // namespace Next

namespace Next::level {

// The result of instantiating a level: the ref -> spawned Entity mapping, plus the agent entity.
struct LoadedLevel {
    std::vector<Entity> entityByRef;  // entityByRef[ref.value]; index 0 is the invalid sentinel
    Entity agent = INVALID_ENTITY;    // INVALID_ENTITY if the level declared no agent

    // Self-contained: holds no pointer into the source LevelDef, so a LoadedLevel stays valid even
    // if the def was a temporary passed to Load(). WinEvaluator takes the def explicitly instead.
    Entity EntityFor(LevelEntityRef r) const {
        return (r.value < entityByRef.size()) ? entityByRef[r.value] : INVALID_ENTITY;
    }
};

struct LoadResult {
    ValidationReport report;  // validation outcome
    bool loaded = false;      // true only if validation passed AND instantiation completed
    LoadedLevel level;
};

class LevelLoader {
public:
    // Validate, then (only if valid) instantiate `def` into `world`, seeding `objectives` if given.
    // Transactional: on validation failure the World is left UNTOUCHED (no partial load) and
    // loaded == false. Deterministic: entities are created in vector order, components applied in a
    // fixed order, parents resolved in a second pass; two loads of the same def into fresh Worlds
    // produce identical entities/components.
    static LoadResult Load(const LevelDef& def, World& world, gameapi::ObjectiveStore* objectives = nullptr);
};

}  // namespace Next::level
