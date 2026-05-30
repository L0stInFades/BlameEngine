#include "next/gameplay/physics_world_query.h"

#include "next/physics/components.h"
#include "next/physics/physics_world.h"
#include "next/runtime/world.h"

namespace Next::gameplay {

gameapi::RaycastResult PhysicsWorldQuery::Raycast(const float origin[3], const float direction[3], float maxDistance) {
    gameapi::RaycastResult out{};  // hit == 0
    if (physics_ == nullptr) {
        return out;
    }

    const physics::RaycastResult hit = physics_->Raycast(origin, direction, maxDistance);
    if (!hit.hit) {
        return out;
    }

    out.hit = 1;
    out.entity = gameapi::kInvalidEntity;
    out.distance = hit.distance;
    out.point = {hit.point[0], hit.point[1], hit.point[2]};
    out.normal = {hit.normal[0], hit.normal[1], hit.normal[2]};

    // Map the struck physics body back to the ECS entity that owns it.
    if (world_ != nullptr) {
        world_->Each<physics::RigidBodyComponent>([&](Entity e, physics::RigidBodyComponent& rb) {
            if (rb.body == hit.body) {
                out.entity = static_cast<gameapi::EntityId>(e);
            }
        });
    }
    return out;
}

}  // namespace Next::gameplay
