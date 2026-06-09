#include "next/water_world/water_swim.h"

#include "next/runtime/transform.h"
#include "next/runtime/world.h"
#include "next/water_world/water_query.h"
#include "next/water_world/water_view.h"  // kWaterEventDrown

namespace Next::water {

void SwimSystem::Update(float deltaTime) {
    if (world_ == nullptr || store_ == nullptr || deltaTime <= 0.0f) {
        return;
    }
    const double t = (clock_ != nullptr) ? clock_->seconds : 0.0;  // shared authoritative time

    world_->Each<SwimmerComponent>([&](Entity e, SwimmerComponent& sc) {
        if (sc.dead) {
            return;  // latched: a drowned character stays dead (its 'WDRN' event already fired)
        }
        const TransformComponent* tc = world_->GetComponent<TransformComponent>(e);
        if (tc == nullptr) {
            return;
        }
        // The breathing point is the head, headOffset above the origin: a character WADING (body wet,
        // head dry) breathes; only true submersion of the head matters.
        const float hx = tc->position[0];
        const float hy = tc->position[1] + sc.headOffset;
        const float hz = tc->position[2];
        WaterSample sample;
        const bool headInWater = store_->SampleWaterAt(hx, hy, hz, t, sample) && sample.submerged;
        sc.submerged = headInWater;  // swim/animation state: head underwater in ANY water

        // Drowning only happens in water authored as lethal (WaterLethal — "full submersion is lethal
        // over time"). Non-lethal water (a decorative/shallow pool) never depletes air or drowns, so the
        // flag is a meaningful authoring toggle. Treat non-drowning water exactly like being at the surface.
        const bool drowningWater = headInWater && (sample.flags & WaterLethal) != 0;
        if (!drowningWater) {
            sc.drowning = false;
            sc.oxygen += oxygenRecoveryPerSec_ * deltaTime;  // catch your breath at the surface
            if (sc.oxygen > sc.maxOxygen) {
                sc.oxygen = sc.maxOxygen;
            }
            return;
        }

        // Head underwater: hold breath until the air runs out, then drown.
        sc.oxygen -= deltaTime;
        if (sc.oxygen > 0.0f) {
            sc.drowning = false;
            return;
        }
        sc.oxygen = 0.0f;
        sc.drowning = true;
        sc.health -= drownDamagePerSec_ * deltaTime;
        if (sc.health > 0.0f) {
            return;
        }
        // Out of air AND out of health -> drowned. Latch + fire one event.
        sc.health = 0.0f;
        sc.dead = true;
        ++totalDrownings_;
        if (transport_ != nullptr) {
            Next::boundary::GameEvent ev{};
            ev.type = kWaterEventDrown;
            ev.subject = static_cast<Next::boundary::EntityId>(e);
            ev.params[0] = hx;
            ev.params[1] = hy;
            ev.params[2] = hz;
            ev.params[3] = 0.0f;
            transport_->PushEvent(ev);  // cosmetic/UI cue; authority is sc.dead
        }
    });
}

}  // namespace Next::water
