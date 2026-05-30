#pragma once

#include <map>
#include <memory>

#include "next/physics/physics_world.h"

namespace Next::physics {

// Deterministic stand-in physics (ADR-0009): gravity + semi-implicit Euler integration + AABB
// collision of dynamic bodies against static bodies. It treats every shape as an axis-aligned box
// (a sphere becomes its bounding cube) and ignores rotation — enough to drive the
// Transform → snapshot pipeline and be fully headless-testable; TRUE collision is the Jolt
// backend's job. All-float math in a fixed (std::map, ascending-id) order ⇒ identical results
// across runs (replay / anti-cheat).
class ReferencePhysicsWorld final : public IPhysicsWorld {
public:
    explicit ReferencePhysicsWorld(const PhysicsConfig& config = {}) : config_(config) {}

    BodyId CreateBody(const BodyDesc& desc) override;
    void DestroyBody(BodyId id) override;
    bool IsValid(BodyId id) const override;

    void SetLinearVelocity(BodyId id, const float v[3]) override;
    void GetLinearVelocity(BodyId id, float outV[3]) const override;
    void SetPosition(BodyId id, const float p[3]) override;
    void GetTransform(BodyId id, float outPos[3], float outRot[4]) const override;

    RaycastResult Raycast(const float origin[3], const float direction[3], float maxDistance) const override;

    void Step(float dt) override;

    size_t BodyCount() const override { return bodies_.size(); }
    const char* BackendName() const override { return "reference"; }

private:
    struct Body {
        BodyDesc desc;
        float position[3];
        float velocity[3];
        float rotation[4];
    };

    // Resolve a dynamic body against one static body (AABB push-out + restitution on the contact
    // axis). No-op when separated.
    void ResolveAgainstStatic(Body& dynamicBody, const Body& staticBody) const;

    PhysicsConfig config_;
    std::map<BodyId, Body> bodies_;
    BodyId nextId_ = 1;
};

// Construct the built-in deterministic backend. Always available — no third-party dependency.
std::unique_ptr<IPhysicsWorld> MakeReferencePhysicsWorld(const PhysicsConfig& config = {});

}  // namespace Next::physics
