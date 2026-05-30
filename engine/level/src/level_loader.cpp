#include "next/level/level_loader.h"

#include "next/boundary/snapshot.h"
#include "next/gameapi/components.h"
#include "next/gameapi/objective_store.h"
#include "next/level/level_validator.h"
#include "next/physics/components.h"
#include "next/runtime/transform.h"
#include "next/runtime/world.h"

namespace Next::level {

LoadResult LevelLoader::Load(const LevelDef& def, World& world, gameapi::ObjectiveStore* objectives) {
    LoadResult result;
    result.report = LevelValidator::Validate(def);
    if (!result.report.Ok()) {
        return result;  // transactional: World left untouched, loaded == false
    }

    LoadedLevel& loaded = result.level;

    uint32_t maxRef = 0;
    for (const EntityDef& e : def.entities) {
        if (e.ref.value > maxRef) {
            maxRef = e.ref.value;
        }
    }
    loaded.entityByRef.assign(static_cast<size_t>(maxRef) + 1, INVALID_ENTITY);

    // Pass 1: create each entity (vector order) and apply components in a FIXED order.
    for (const EntityDef& e : def.entities) {
        const Entity ent = world.CreateEntity();
        loaded.entityByRef[e.ref.value] = ent;

        if (e.hasTransform) {
            TransformComponent& t = world.AddComponent<TransformComponent>(ent);
            for (int k = 0; k < 3; ++k) {
                t.position[k] = e.transform.position[k];
                t.scale[k] = e.transform.scale[k];
            }
            for (int k = 0; k < 4; ++k) {
                t.rotation[k] = e.transform.rotation[k];
            }
            // parent resolved in pass 2 (the referenced entity may not exist yet)
        }
        if (e.hasTag) {
            gameapi::GameTag g;
            g.bits = e.tag.bits;
            world.AddComponent<gameapi::GameTag>(ent, g);
        }
        if (e.hasRender) {
            world.AddComponent<boundary::RenderableComponent>(
                ent, boundary::RenderableComponent{e.render.visual, e.render.animState});
        }
        if (e.hasMove) {
            gameapi::MoveTarget m;
            m.target = {e.move.target[0], e.move.target[1], e.move.target[2]};
            m.maxSpeed = e.move.maxSpeed;
            m.active = e.move.active ? 1 : 0;
            world.AddComponent<gameapi::MoveTarget>(ent, m);
        }
        if (e.hasAction) {
            gameapi::ActionFlags a;
            a.bits = e.action.bits;
            world.AddComponent<gameapi::ActionFlags>(ent, a);
        }
        if (e.hasBody) {
            physics::RigidBodyComponent rb;
            rb.desc.motion = static_cast<physics::MotionType>(e.body.motion);
            rb.desc.shape = static_cast<physics::ShapeType>(e.body.shape);
            for (int k = 0; k < 3; ++k) {
                rb.desc.halfExtents[k] = e.body.halfExtents[k];
                rb.desc.linearVelocity[k] = e.body.linearVelocity[k];
            }
            // Seed the body's spawn transform from the entity's transform (validation requires a body
            // to have one) so it agrees with PhysicsSystem, which re-seeds BOTH position and rotation.
            if (e.hasTransform) {
                for (int k = 0; k < 3; ++k) {
                    rb.desc.position[k] = e.transform.position[k];
                }
                for (int k = 0; k < 4; ++k) {
                    rb.desc.rotation[k] = e.transform.rotation[k];
                }
            }
            rb.desc.mass = e.body.mass;
            rb.desc.restitution = e.body.restitution;
            rb.syncToTransform = e.body.syncToTransform;
            world.AddComponent<physics::RigidBodyComponent>(ent, rb);
        }
    }

    // Pass 2: resolve transform parents now that every entity exists.
    for (const EntityDef& e : def.entities) {
        if (e.hasTransform && e.transform.parent.IsValid()) {
            if (TransformComponent* t = world.GetComponent<TransformComponent>(loaded.entityByRef[e.ref.value])) {
                t->parent = loaded.EntityFor(e.transform.parent);
            }
        }
    }

    loaded.agent = def.metadata.agent.IsValid() ? loaded.EntityFor(def.metadata.agent) : INVALID_ENTITY;

    if (objectives != nullptr) {
        for (const ObjectiveDef& o : def.objectives) {
            objectives->Set(o.id, o.initialState);
        }
    }

    result.loaded = true;
    return result;
}

}  // namespace Next::level
