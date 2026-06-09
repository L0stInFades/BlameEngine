#pragma once

#include <cstddef>
#include <cstdint>

// Physics as a swappable capability, not a particular engine (ADR-0009). The headless
// authoritative world talks ONLY to this interface; concrete backends (the built-in deterministic
// reference world today, Jolt behind BUILD_WITH_JOLT) sit behind it. `engine/*` never includes a
// third-party physics header — physics stays decoupled, deterministic, and server-authoritative.

namespace Next::physics {

// Opaque, stable handle to a body within one IPhysicsWorld. 0 is always invalid.
using BodyId = uint64_t;
constexpr BodyId kInvalidBody = 0;

enum class MotionType : uint8_t {
    Static,     // never moves; collides, infinite mass (e.g. floors, walls)
    Kinematic,  // moves by its set velocity, ignores gravity, is not pushed (e.g. scripted platforms)
    Dynamic,    // fully simulated: gravity, integration, pushed out of statics
};

enum class ShapeType : uint8_t {
    Sphere,  // radius = halfExtents[0]
    Box,     // axis-aligned half-extents = halfExtents[0..2]
};

// Flat POD describing a body at creation. No pointers/STL — cheap to copy and to carry inside an
// ECS component.
struct BodyDesc {
    MotionType motion = MotionType::Dynamic;
    ShapeType shape = ShapeType::Sphere;
    float halfExtents[3] = {0.5f, 0.5f, 0.5f};
    float position[3] = {0.0f, 0.0f, 0.0f};
    float rotation[4] = {0.0f, 0.0f, 0.0f, 1.0f};  // quaternion (x,y,z,w)
    float linearVelocity[3] = {0.0f, 0.0f, 0.0f};
    float mass = 1.0f;         // dynamic only; <= 0 is treated as 1
    float restitution = 0.0f;  // 0 = no bounce, 1 = perfectly elastic
};

struct PhysicsConfig {
    float gravity[3] = {0.0f, -9.81f, 0.0f};
};

// Result of a ray cast against the world. `hit` false means nothing within maxDistance.
struct RaycastResult {
    bool hit = false;
    BodyId body = kInvalidBody;
    float point[3] = {0.0f, 0.0f, 0.0f};   // world-space contact point
    float normal[3] = {0.0f, 0.0f, 0.0f};  // surface normal at the contact (pointing back toward origin)
    float distance = 0.0f;                 // distance from origin to contact
};

// The single contract every backend implements. Step() is FIXED-STEP and the only time advances —
// the sim drives it from its fixed tick, so the simulation is deterministic (replay / anti-cheat /
// server authority). No wall clock, no hidden time.
struct IPhysicsWorld {
    virtual ~IPhysicsWorld() = default;

    virtual BodyId CreateBody(const BodyDesc& desc) = 0;
    virtual void DestroyBody(BodyId id) = 0;
    virtual bool IsValid(BodyId id) const = 0;

    virtual void SetLinearVelocity(BodyId id, const float v[3]) = 0;
    virtual void GetLinearVelocity(BodyId id, float outV[3]) const = 0;
    virtual void SetPosition(BodyId id, const float p[3]) = 0;

    // Current world-space transform of the body (position + quaternion).
    virtual void GetTransform(BodyId id, float outPos[3], float outRot[4]) const = 0;

    // Accumulate a world-space force on a DYNAMIC body for the NEXT Step only. The accumulator is
    // applied during Step (semi-implicit: a = gravity + sum(force)/mass) and then CLEARED, so a
    // sustained force must be re-applied each tick. No-op on non-dynamic or unknown bodies. This is
    // the substrate buoyancy / drag / flow forces (the water sim) ride on; gravity stays the
    // backend's, so buoyancy simply adds the upward force and the net is physically correct.
    virtual void AddForce(BodyId id, const float force[3]) = 0;

    // Apply an instantaneous, mass-normalized world-space impulse (dv = impulse / mass) to a DYNAMIC
    // body immediately (this tick). No-op on non-dynamic or unknown bodies. For one-shot kicks
    // (a splash shove, an explosion). Deterministic.
    virtual void AddImpulse(BodyId id, const float impulse[3]) = 0;

    // Accumulate a world-space TORQUE on a DYNAMIC body for the next Step (rotates it about its center
    // of mass), applied then cleared like AddForce. No-op on non-dynamic / unknown bodies. With
    // AddForceAtPosition this is the substrate for multi-point (pontoon) buoyancy that makes boats roll
    // and self-right.
    virtual void AddTorque(BodyId id, const float torque[3]) = 0;

    // Apply a world-space force at a world-space point on a DYNAMIC body: it adds the force to the linear
    // accumulator AND a torque ((worldPoint - centerOfMass) x force) about the COM. The primitive each
    // pontoon of a multi-point buoyancy model applies. No-op on non-dynamic / unknown bodies.
    virtual void AddForceAtPosition(BodyId id, const float force[3], const float worldPoint[3]) = 0;

    // Current angular velocity (rad/s, world axes); zero for unknown / non-rotating bodies.
    virtual void GetAngularVelocity(BodyId id, float outAngular[3]) const = 0;

    // Cast a ray from `origin` along `direction` (need not be normalized) up to `maxDistance`,
    // returning the nearest body hit. The backbone of line-of-sight / spatial sensing. Deterministic.
    virtual RaycastResult Raycast(const float origin[3], const float direction[3], float maxDistance) const = 0;

    // Advance the simulation by exactly `dt` seconds (fixed step).
    virtual void Step(float dt) = 0;

    virtual size_t BodyCount() const = 0;

    // Which concrete backend this is (for diagnostics / tests).
    virtual const char* BackendName() const = 0;
};

}  // namespace Next::physics
