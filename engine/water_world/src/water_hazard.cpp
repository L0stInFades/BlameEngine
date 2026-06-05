#include "next/water_world/water_hazard.h"

#include "next/runtime/transform.h"
#include "next/runtime/world.h"
#include "next/water_world/water_query.h"
#include "next/water_world/water_view.h"  // kWaterEventShort

namespace Next::water {

void WaterHazardSystem::Update(float /*deltaTime*/) {
    if (world_ == nullptr || store_ == nullptr) {
        return;
    }
    const double t = (clock_ != nullptr) ? clock_->seconds : 0.0;  // shared authoritative time

    world_->Each<ElectronicComponent>([&](Entity e, ElectronicComponent& dev) {
        if (dev.shortedOut) {
            return;  // latched dead: a fried board stays fried (deterministic, fires its event once)
        }
        const TransformComponent* tc = world_->GetComponent<TransformComponent>(e);
        if (tc == nullptr) {
            return;
        }
        if (!IsInConductiveWater(*store_, tc->position[0], tc->position[1], tc->position[2], t)) {
            return;  // dry, or in non-conductive water, or above the surface
        }
        dev.shortedOut = true;
        dev.functional = false;
        ++totalShortedOut_;
        if (transport_ != nullptr) {
            Next::boundary::GameEvent ev{};
            ev.type = kWaterEventShort;
            ev.subject = static_cast<Next::boundary::EntityId>(e);
            ev.params[0] = tc->position[0];
            ev.params[1] = tc->position[1];
            ev.params[2] = tc->position[2];
            ev.params[3] = 0.0f;
            transport_->PushEvent(ev);  // cosmetic sparks/smoke cue; carries no authority
        }
    });
}

}  // namespace Next::water
