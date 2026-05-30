#include "next/level/win_conditions.h"

#include "next/gameapi/components.h"
#include "next/gameapi/objective_store.h"
#include "next/runtime/transform.h"
#include "next/runtime/world.h"

namespace Next::level {

const char* ToString(LevelOutcome outcome) {
    switch (outcome) {
        case LevelOutcome::InProgress:
            return "InProgress";
        case LevelOutcome::Won:
            return "Won";
        case LevelOutcome::Lost:
            return "Lost";
    }
    return "Unknown";
}

namespace {
bool ObjectiveState(const gameapi::ObjectiveStore* store, uint32_t id, int32_t& out) {
    return store != nullptr && store->Get(id, out);
}
}  // namespace

bool WinEvaluator::EvaluateCondition(const WinConditionDef& cond, const World& world,
                                     const gameapi::ObjectiveStore* objectives, const LoadedLevel& loaded) {
    switch (cond.kind) {
        case WinKind::ObjectiveAtLeast: {
            int32_t state = 0;
            return ObjectiveState(objectives, cond.objectiveId, state) && state >= cond.threshold;
        }
        case WinKind::ObjectiveEquals: {
            int32_t state = 0;
            return ObjectiveState(objectives, cond.objectiveId, state) && state == cond.threshold;
        }
        case WinKind::EntityReached: {
            const Entity e = loaded.EntityFor(cond.entity);
            if (!world.IsEntityValid(e)) {
                return false;
            }
            const TransformComponent* t = world.GetComponent<TransformComponent>(e);
            if (t == nullptr) {
                return false;
            }
            const float dx = t->position[0] - cond.point[0];
            const float dy = t->position[1] - cond.point[1];
            const float dz = t->position[2] - cond.point[2];
            return (dx * dx + dy * dy + dz * dz) <= (cond.radius * cond.radius);
        }
        case WinKind::AllTaggedDestroyed: {
            bool anyLive = false;
            world.Each<gameapi::GameTag>([&](Entity, const gameapi::GameTag& g) {
                if (g.Has(cond.tagIndex)) {
                    anyLive = true;
                }
            });
            return !anyLive;
        }
    }
    return false;  // total: unknown kind is never satisfied
}

LevelOutcome WinEvaluator::Evaluate(const LevelDef& def, const World& world, const gameapi::ObjectiveStore* objectives,
                                    const LoadedLevel& loaded) {
    bool allWin = true;
    bool anyNonFailure = false;
    for (const WinConditionDef& c : def.winConditions) {
        const bool met = EvaluateCondition(c, world, objectives, loaded);
        if (c.isFailure) {
            if (met) {
                return LevelOutcome::Lost;  // loss takes priority
            }
        } else {
            anyNonFailure = true;
            if (!met) {
                allWin = false;
            }
        }
    }
    return (anyNonFailure && allWin) ? LevelOutcome::Won : LevelOutcome::InProgress;
}

}  // namespace Next::level
