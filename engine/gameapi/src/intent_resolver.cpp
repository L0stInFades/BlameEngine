#include "next/gameapi/intent_resolver.h"

#include <cmath>

#include "next/gameapi/components.h"
#include "next/gameapi/game_api.h"  // ToEntity/ToEntityId
#include "next/runtime/transform.h"
#include "next/runtime/world.h"

namespace Next::gameapi {

void DefaultIntentResolver::Apply(World& world, const std::vector<Intent>& intents) {
    signals_.clear();
    for (const Intent& in : intents) {
        const Entity src = ToEntity(in.source);
        if (!world.IsEntityValid(src)) {
            continue;  // entity destroyed between recording and application — skip gracefully
        }
        switch (in.type) {
            case IntentType::MoveTo: {
                MoveTarget mt;
                mt.target = in.vec;
                mt.maxSpeed = in.scalar;
                mt.active = 1;
                world.AddComponent<MoveTarget>(src, mt);  // set-or-overwrite the goal
                break;
            }
            case IntentType::Stop: {
                if (MoveTarget* mt = world.GetComponent<MoveTarget>(src)) {
                    mt->active = 0;
                }
                break;
            }
            case IntentType::SetActionFlag: {
                ActionFlags* af = world.GetComponent<ActionFlags>(src);
                if (af == nullptr) {
                    af = &world.AddComponent<ActionFlags>(src);
                }
                const uint32_t bit = (in.a < 32) ? (1u << in.a) : 0u;
                if (in.b != 0) {
                    af->bits |= bit;
                } else {
                    af->bits &= ~bit;
                }
                break;
            }
            case IntentType::SendSignal: {
                signals_.push_back(Signal{in.source, in.a, in.b, in.scalar});
                break;
            }
            case IntentType::ReportProgress: {
                if (objectives_ != nullptr) {
                    objectives_->Advance(in.a, in.b);
                }
                break;
            }
        }
    }
}

void DefaultIntentResolver::StepKinematics(World& world, double dt) {
    const float stepDt = static_cast<float>(dt);
    world.Each<TransformComponent, MoveTarget>([&](Entity, TransformComponent& t, MoveTarget& mt) {
        if (mt.active == 0) {
            return;
        }
        const float dx = mt.target.x - t.position[0];
        const float dy = mt.target.y - t.position[1];
        const float dz = mt.target.z - t.position[2];
        const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        const float step = mt.maxSpeed * stepDt;
        if (dist <= step || dist < 1e-6f) {
            t.position[0] = mt.target.x;
            t.position[1] = mt.target.y;
            t.position[2] = mt.target.z;
            mt.active = 0;  // arrived
        } else {
            const float s = step / dist;
            t.position[0] += dx * s;
            t.position[1] += dy * s;
            t.position[2] += dz * s;
        }
    });
}

}  // namespace Next::gameapi
