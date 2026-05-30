#include "next/physics/physics_system.h"

#include <unordered_set>

#include "next/physics/components.h"
#include "next/runtime/transform.h"
#include "next/runtime/world.h"

namespace Next::physics {

void PhysicsSystem::Update(float deltaTime) {
    if (physics_ == nullptr || world_ == nullptr) {
        return;
    }
    Reconcile();
    physics_->Step(deltaTime);
    SyncTransforms();
}

void PhysicsSystem::Reconcile() {
    std::unordered_set<uint64_t> seen;
    seen.reserve(entityToBody_.size());

    world_->Each<RigidBodyComponent>([&](Entity e, RigidBodyComponent& rb) {
        const uint64_t key = static_cast<uint64_t>(e);
        seen.insert(key);

        if (rb.body != kInvalidBody && physics_->IsValid(rb.body)) {
            return;  // already has a live body
        }

        // Seed the body's initial transform from the entity's TransformComponent if it has one,
        // so a body spawns where its entity already is.
        BodyDesc desc = rb.desc;
        if (const TransformComponent* t = world_->GetComponent<TransformComponent>(e)) {
            for (int i = 0; i < 3; ++i) {
                desc.position[i] = t->position[i];
            }
            for (int i = 0; i < 4; ++i) {
                desc.rotation[i] = t->rotation[i];
            }
        }
        rb.body = physics_->CreateBody(desc);
        entityToBody_[key] = rb.body;
    });

    // Destroy bodies whose entity/component vanished this tick (not seen above).
    for (auto it = entityToBody_.begin(); it != entityToBody_.end();) {
        if (seen.find(it->first) == seen.end()) {
            physics_->DestroyBody(it->second);
            it = entityToBody_.erase(it);
        } else {
            ++it;
        }
    }
}

void PhysicsSystem::SyncTransforms() {
    world_->Each<RigidBodyComponent, TransformComponent>([&](Entity, RigidBodyComponent& rb, TransformComponent& t) {
        if (rb.body == kInvalidBody || !rb.syncToTransform || !physics_->IsValid(rb.body)) {
            return;
        }
        float pos[3];
        float rot[4];
        physics_->GetTransform(rb.body, pos, rot);
        for (int i = 0; i < 3; ++i) {
            t.position[i] = pos[i];
        }
        for (int i = 0; i < 4; ++i) {
            t.rotation[i] = rot[i];
        }
    });
}

void PhysicsSystem::Shutdown() {
    if (physics_ != nullptr) {
        for (const auto& [entity, body] : entityToBody_) {
            (void)entity;
            physics_->DestroyBody(body);
        }
    }
    entityToBody_.clear();
}

}  // namespace Next::physics
