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

    void AddForce(BodyId id, const float force[3]) override;
    void AddImpulse(BodyId id, const float impulse[3]) override;
    void AddTorque(BodyId id, const float torque[3]) override;
    void AddForceAtPosition(BodyId id, const float force[3], const float worldPoint[3]) override;
    void GetAngularVelocity(BodyId id, float outAngular[3]) const override;

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
        float force[3];            // world-space force accumulator for the next Step; applied then cleared
        float angularVelocity[3];  // rad/s, world axes
        float torque[3];           // world-space torque accumulator for the next Step; applied then cleared
        float invInertia[3];       // diagonal inverse inertia (body ~ world for this stand-in)
    };

    // Resolve a dynamic body against one static body (AABB push-out + restitution on the contact
    // axis). No-op when separated.
    void ResolveAgainstStatic(Body& dynamicBody, const Body& staticBody) const;

    // Integrate the orientation quaternion from the body's angular velocity over dt (then renormalize).
    static void IntegrateOrientation(Body& body, float dt);

    PhysicsConfig config_;
    std::map<BodyId, Body> bodies_;
    BodyId nextId_ = 1;
};

// Construct the built-in deterministic backend. Always available — no third-party dependency.
std::unique_ptr<IPhysicsWorld> MakeReferencePhysicsWorld(const PhysicsConfig& config = {});

}  // namespace Next::physics
