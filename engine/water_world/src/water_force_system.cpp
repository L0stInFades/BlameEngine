#include "next/water_world/water_force_system.h"

#include <cmath>
#include <unordered_set>

#include "next/physics/components.h"
#include "next/runtime/world.h"
#include "next/water/buoyancy.h"
#include "next/water/water_surface.h"
#include "next/water_world/water_view.h"

namespace Next::water {

using Next::physics::BodyId;
using Next::physics::MotionType;
using Next::physics::RigidBodyComponent;

void WaterForceSystem::Update(float deltaTime) {
    if (world_ == nullptr || physics_ == nullptr || store_ == nullptr) {
        return;
    }
    const double t = SimTimeSeconds();  // authoritative shared SimClock (0 if none); not a private accumulator

    std::unordered_set<uint64_t> seen;

    world_->Each<RigidBodyComponent>([&](Entity e, RigidBodyComponent& rb) {
        if (rb.desc.motion != MotionType::Dynamic || rb.body == Next::physics::kInvalidBody ||
            !physics_->IsValid(rb.body)) {
            return;
        }
        const uint64_t key = static_cast<uint64_t>(e);
        seen.insert(key);

        float pos[3];
        float rot[4];
        physics_->GetTransform(rb.body, pos, rot);
        float vel[3];
        physics_->GetLinearVelocity(rb.body, vel);

        const WaterBodyInstance* body = store_->BodyAt(pos[0], pos[2], t);
        const bool wasIn = submerged_.count(key) != 0 ? submerged_[key] : false;

        if (body == nullptr) {
            if (wasIn && transport_ != nullptr) {
                WaterContactEvent ev;
                ev.entered = false;
                ev.entity = static_cast<Next::boundary::EntityId>(key);
                ev.position[0] = pos[0];
                ev.position[1] = pos[1];
                ev.position[2] = pos[2];
                ev.speed = std::sqrt((vel[0] * vel[0]) + (vel[1] * vel[1]) + (vel[2] * vel[2]));
                transport_->PushEvent(ToBoundaryEvent(ev));
            }
            submerged_[key] = false;
            return;
        }

        const float surfaceY = SampleHeightFast(*body, pos[0], pos[2], t);
        const bool buoyant = (body->flags & WaterBuoyant) != 0;
        const float mass = rb.desc.mass > 0.0f ? rb.desc.mass : 1.0f;
        bool nowIn = false;

        if (rb.desc.shape == Next::physics::ShapeType::Box) {
            // Multi-point (pontoon) buoyancy: a per-corner force applied at world points yields net
            // buoyancy + a RIGHTING torque (a tilted box self-rights; a raft floats level), plus COM
            // horizontal/flow drag. The boat physics foundation.
            float angVel[3];
            physics_->GetAngularVelocity(rb.body, angVel);
            const BoxBuoyancyResult br =
                ComputeBoxBuoyancy(*body, pos, rot, rb.desc.halfExtents, vel, angVel, mass, t, deltaTime);
            nowIn = br.inWater;
            if (buoyant && br.inWater) {
                for (int i = 0; i < br.pointCount; ++i) {
                    physics_->AddForceAtPosition(rb.body, br.points[i].force, br.points[i].point);
                }
                physics_->AddImpulse(rb.body, br.comDragImpulse);
            }
        } else {
            // Sphere (orientation-free): single COM buoyancy + clamped drag/flow.
            FluidSample fluid;
            fluid.density = body->density;
            fluid.surfaceHeight = surfaceY;
            fluid.floorY = body->boundsMin[1];
            fluid.flowVelocity[0] = body->flowVelocity[0];
            fluid.flowVelocity[1] = body->flowVelocity[1];
            fluid.flowVelocity[2] = body->flowVelocity[2];
            fluid.linearDrag = body->linearDrag;
            fluid.quadraticDrag = body->quadraticDrag;
            fluid.flags = body->flags;

            BodyBuoyancyInput input;
            input.shape = static_cast<uint8_t>(rb.desc.shape);
            input.halfExtents[0] = rb.desc.halfExtents[0];
            input.halfExtents[1] = rb.desc.halfExtents[1];
            input.halfExtents[2] = rb.desc.halfExtents[2];
            input.position[0] = pos[0];
            input.position[1] = pos[1];
            input.position[2] = pos[2];
            input.velocity[0] = vel[0];
            input.velocity[1] = vel[1];
            input.velocity[2] = vel[2];
            input.mass = mass;

            const WaterForceOutput out = ComputeWaterForce(fluid, input, deltaTime);
            nowIn = out.inWater;
            if (buoyant && out.inWater) {
                physics_->AddForce(rb.body, out.force);
                physics_->AddImpulse(rb.body, out.dragImpulse);
            }
        }

        if (nowIn != wasIn && transport_ != nullptr) {
            WaterContactEvent ev;
            ev.entered = nowIn;
            ev.entity = static_cast<Next::boundary::EntityId>(key);
            ev.position[0] = pos[0];
            ev.position[1] = surfaceY;  // contact at the surface
            ev.position[2] = pos[2];
            ev.speed = std::sqrt((vel[0] * vel[0]) + (vel[1] * vel[1]) + (vel[2] * vel[2]));
            transport_->PushEvent(ToBoundaryEvent(ev));
        }
        submerged_[key] = nowIn;
    });

    // Prune entities that no longer exist / have no dynamic body, so the edge-state map stays bounded.
    for (auto it = submerged_.begin(); it != submerged_.end();) {
        if (seen.find(it->first) == seen.end()) {
            it = submerged_.erase(it);
        } else {
            ++it;
        }
    }
}

size_t WaterForceSystem::SubmergedBodyCount() const {
    size_t n = 0;
    for (const auto& [entity, in] : submerged_) {
        (void)entity;
        if (in) {
            ++n;
        }
    }
    return n;
}

}  // namespace Next::water
