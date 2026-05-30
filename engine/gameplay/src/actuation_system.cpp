#include "next/gameplay/actuation_system.h"

#include <cmath>

#include "next/gameapi/components.h"
#include "next/physics/components.h"
#include "next/runtime/transform.h"
#include "next/runtime/world.h"

namespace Next::gameplay {
namespace {

float Length3(float x, float y, float z) {
    return std::sqrt((x * x) + (y * y) + (z * z));
}

}  // namespace

void ActuationSystem::Update(float deltaTime) {
    if (world_ == nullptr) {
        return;
    }

    world_->Each<gameapi::MoveTarget>([&](Entity e, gameapi::MoveTarget& mt) {
        if (mt.active == 0) {
            return;
        }
        const float step = mt.maxSpeed * deltaTime;

        // Physics-owned entity (has a RigidBodyComponent): physics owns its Transform, so we only
        // ever drive the body's velocity here — never the Transform. Branching on the COMPONENT
        // (not a live body) means that even before the PhysicsSystem has created the body, we leave
        // the Transform alone and actuate once the body exists next tick. One writer, always.
        if (physics::RigidBodyComponent* rb = world_->GetComponent<physics::RigidBodyComponent>(e)) {
            if (physics_ == nullptr || rb->body == physics::kInvalidBody || !physics_->IsValid(rb->body)) {
                return;  // body not created yet — PhysicsSystem will, and we actuate next tick
            }
            float pos[3];
            float rot[4];
            physics_->GetTransform(rb->body, pos, rot);
            const float dx = mt.target.x - pos[0];
            const float dy = mt.target.y - pos[1];
            const float dz = mt.target.z - pos[2];
            const float dist = Length3(dx, dy, dz);
            if (dist <= step || dist < 1e-6f) {
                const float snap[3] = {mt.target.x, mt.target.y, mt.target.z};
                const float zero[3] = {0.0f, 0.0f, 0.0f};
                physics_->SetPosition(rb->body, snap);
                physics_->SetLinearVelocity(rb->body, zero);
                mt.active = 0;  // arrived
            } else {
                const float s = mt.maxSpeed / dist;
                const float vel[3] = {dx * s, dy * s, dz * s};
                physics_->SetLinearVelocity(rb->body, vel);
            }
            return;
        }

        // Non-physics entity: integrate the TransformComponent directly toward the target.
        TransformComponent* t = world_->GetComponent<TransformComponent>(e);
        if (t == nullptr) {
            return;
        }
        const float dx = mt.target.x - t->position[0];
        const float dy = mt.target.y - t->position[1];
        const float dz = mt.target.z - t->position[2];
        const float dist = Length3(dx, dy, dz);
        if (dist <= step || dist < 1e-6f) {
            t->position[0] = mt.target.x;
            t->position[1] = mt.target.y;
            t->position[2] = mt.target.z;
            mt.active = 0;  // arrived
        } else {
            const float s = step / dist;
            t->position[0] += dx * s;
            t->position[1] += dy * s;
            t->position[2] += dz * s;
        }
    });
}

}  // namespace Next::gameplay
