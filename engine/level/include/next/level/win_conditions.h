#pragma once

#include <cstdint>

#include "next/level/level_def.h"
#include "next/level/level_loader.h"

namespace Next {
class World;
namespace gameapi {
class ObjectiveStore;
}
}  // namespace Next

namespace Next::level {

enum class LevelOutcome : uint8_t { InProgress, Won, Lost };

const char* ToString(LevelOutcome outcome);

// Evaluates the level's win/lose conditions against the live authoritative World + ObjectiveStore.
// Pure read (const World, never mutates) so it can run every tick without perturbing determinism.
// TOTAL: every WinKind is handled; an unsatisfiable read (untouched objective, destroyed/invalid
// entity, missing objective store) evaluates deterministically to false, never a crash.
class WinEvaluator {
public:
    // Loss takes priority: Lost if ANY failure condition holds; else Won if ALL non-failure
    // conditions hold; else InProgress.
    static LevelOutcome Evaluate(const LevelDef& def, const World& world, const gameapi::ObjectiveStore* objectives,
                                 const LoadedLevel& loaded);

    // Evaluate a single condition (exposed for fine-grained testing). `loaded` resolves entity refs.
    static bool EvaluateCondition(const WinConditionDef& cond, const World& world,
                                  const gameapi::ObjectiveStore* objectives, const LoadedLevel& loaded);
};

}  // namespace Next::level
