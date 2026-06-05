// Jolt-backed IPhysicsWorld (ADR-0009). Compiled only when BUILD_WITH_JOLT=ON. This is the ONLY
// file in the tree that includes Jolt headers; everything else sees the engine-agnostic
// IPhysicsWorld. Uses a single-threaded job system so the authoritative simulation is deterministic
// (server authority / replay), and maps our flat BodyDesc onto Jolt's BodyInterface.

#include "next/physics/jolt_physics_world.h"

#include <cstdint>
#include <unordered_map>

#include <Jolt/Jolt.h>
// (Jolt.h must be first.)
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

namespace Next::physics {
namespace {

// --- Layer setup (canonical Jolt two-layer scheme: non-moving statics vs moving bodies) --------
namespace Layers {
constexpr JPH::ObjectLayer kNonMoving = 0;
constexpr JPH::ObjectLayer kMoving = 1;
constexpr JPH::ObjectLayer kNumLayers = 2;
}  // namespace Layers

namespace BroadPhaseLayers {
constexpr JPH::BroadPhaseLayer kNonMoving(0);
constexpr JPH::BroadPhaseLayer kMoving(1);
constexpr unsigned int kNumLayers = 2;
}  // namespace BroadPhaseLayers

class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface {
public:
    BPLayerInterfaceImpl() {
        objectToBroadPhase_[Layers::kNonMoving] = BroadPhaseLayers::kNonMoving;
        objectToBroadPhase_[Layers::kMoving] = BroadPhaseLayers::kMoving;
    }
    JPH::uint GetNumBroadPhaseLayers() const override { return BroadPhaseLayers::kNumLayers; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
        return objectToBroadPhase_[layer];
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer) const override { return "layer"; }
#endif
private:
    JPH::BroadPhaseLayer objectToBroadPhase_[Layers::kNumLayers];
};

class ObjectVsBroadPhaseLayerFilterImpl final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer layer1, JPH::BroadPhaseLayer layer2) const override {
        if (layer1 == Layers::kNonMoving) {
            return layer2 == BroadPhaseLayers::kMoving;
        }
        return true;  // moving collides with everything
    }
};

class ObjectLayerPairFilterImpl final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer object1, JPH::ObjectLayer object2) const override {
        if (object1 == Layers::kNonMoving) {
            return object2 == Layers::kMoving;  // statics don't collide with each other
        }
        return true;
    }
};

// Process-wide one-time Jolt setup (allocator + factory + type registration). Lives for the
// process; teardown at exit is intentionally skipped (standard Jolt singleton pattern).
void EnsureGlobalInit() {
    static const bool kOnce = []() {
        JPH::RegisterDefaultAllocator();
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
        return true;
    }();
    (void)kOnce;
}

JPH::EMotionType ToJoltMotion(MotionType m) {
    switch (m) {
        case MotionType::Static:
            return JPH::EMotionType::Static;
        case MotionType::Kinematic:
            return JPH::EMotionType::Kinematic;
        case MotionType::Dynamic:
            break;
    }
    return JPH::EMotionType::Dynamic;
}

class JoltPhysicsWorld final : public IPhysicsWorld {
public:
    explicit JoltPhysicsWorld(const PhysicsConfig& config) {
        EnsureGlobalInit();
        constexpr JPH::uint kMaxBodies = 10240;
        constexpr JPH::uint kNumBodyMutexes = 0;
        constexpr JPH::uint kMaxBodyPairs = 65536;
        constexpr JPH::uint kMaxContactConstraints = 10240;
        system_.Init(kMaxBodies, kNumBodyMutexes, kMaxBodyPairs, kMaxContactConstraints, bpLayer_, objVsBpFilter_,
                     objPairFilter_);
        system_.SetGravity(JPH::Vec3(config.gravity[0], config.gravity[1], config.gravity[2]));
        tempAllocator_ = std::make_unique<JPH::TempAllocatorImpl>(16 * 1024 * 1024);
        jobSystem_ = std::make_unique<JPH::JobSystemSingleThreaded>(JPH::cMaxPhysicsJobs);
    }

    ~JoltPhysicsWorld() override {
        JPH::BodyInterface& bi = system_.GetBodyInterface();
        for (const auto& [id, jid] : bodies_) {
            (void)id;
            bi.RemoveBody(jid);
            bi.DestroyBody(jid);
        }
    }

    BodyId CreateBody(const BodyDesc& desc) override {
        JPH::BodyInterface& bi = system_.GetBodyInterface();

        JPH::ShapeRefC shape;
        if (desc.shape == ShapeType::Sphere) {
            shape = new JPH::SphereShape(desc.halfExtents[0]);
        } else {
            shape = new JPH::BoxShape(JPH::Vec3(desc.halfExtents[0], desc.halfExtents[1], desc.halfExtents[2]));
        }

        const JPH::EMotionType motion = ToJoltMotion(desc.motion);
        const JPH::ObjectLayer layer = (desc.motion == MotionType::Static) ? Layers::kNonMoving : Layers::kMoving;
        JPH::BodyCreationSettings settings(
            shape, JPH::RVec3(desc.position[0], desc.position[1], desc.position[2]),
            JPH::Quat(desc.rotation[0], desc.rotation[1], desc.rotation[2], desc.rotation[3]), motion, layer);
        settings.mRestitution = desc.restitution;
        if (motion != JPH::EMotionType::Static) {
            settings.mLinearVelocity =
                JPH::Vec3(desc.linearVelocity[0], desc.linearVelocity[1], desc.linearVelocity[2]);
        }
        // Respect BodyDesc::mass for moving bodies so the SAME level data (BodyDefData.mass in
        // level_def.h:52-60) produces comparable dynamics on the Jolt backend and the reference
        // backend. CalculateInertia keeps the shape's natural inertia tensor and scales it to the
        // requested mass, so a heavier sphere has proportionally larger inertia. mass <= 0 is
        // normalized to 1 to match ReferencePhysicsWorld::CreateBody's defensive default.
        if (motion != JPH::EMotionType::Static) {
            const float mass = desc.mass > 0.0f ? desc.mass : 1.0f;
            settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
            settings.mMassPropertiesOverride.mMass = mass;
        }

        const BodyId id = nextId_++;
        settings.mUserData = static_cast<JPH::uint64>(id);

        JPH::Body* body = bi.CreateBody(settings);
        if (body == nullptr) {
            return kInvalidBody;  // body budget exhausted
        }
        bi.AddBody(body->GetID(),
                   motion == JPH::EMotionType::Static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
        bodies_.emplace(id, body->GetID());
        return id;
    }

    void DestroyBody(BodyId id) override {
        auto it = bodies_.find(id);
        if (it == bodies_.end()) {
            return;
        }
        JPH::BodyInterface& bi = system_.GetBodyInterface();
        bi.RemoveBody(it->second);
        bi.DestroyBody(it->second);
        bodies_.erase(it);
    }

    bool IsValid(BodyId id) const override { return id != kInvalidBody && bodies_.find(id) != bodies_.end(); }

    void SetLinearVelocity(BodyId id, const float v[3]) override {
        const JPH::BodyID* jid = Find(id);
        if (jid != nullptr) {
            system_.GetBodyInterface().SetLinearVelocity(*jid, JPH::Vec3(v[0], v[1], v[2]));
        }
    }

    void GetLinearVelocity(BodyId id, float outV[3]) const override {
        const JPH::BodyID* jid = Find(id);
        if (jid == nullptr) {
            outV[0] = outV[1] = outV[2] = 0.0f;
            return;
        }
        const JPH::Vec3 v = system_.GetBodyInterface().GetLinearVelocity(*jid);
        outV[0] = v.GetX();
        outV[1] = v.GetY();
        outV[2] = v.GetZ();
    }

    void SetPosition(BodyId id, const float p[3]) override {
        const JPH::BodyID* jid = Find(id);
        if (jid != nullptr) {
            system_.GetBodyInterface().SetPosition(*jid, JPH::RVec3(p[0], p[1], p[2]), JPH::EActivation::Activate);
        }
    }

    // Jolt accumulates AddForce until the next Update() and clears it afterward — exactly the
    // per-Step semantics our IPhysicsWorld contract specifies. Re-activate the body so a sleeping
    // float gets woken by the force (a body parked on the seabed must respond when water rises).
    void AddForce(BodyId id, const float force[3]) override {
        const JPH::BodyID* jid = Find(id);
        if (jid != nullptr) {
            JPH::BodyInterface& bi = system_.GetBodyInterface();
            bi.ActivateBody(*jid);
            bi.AddForce(*jid, JPH::Vec3(force[0], force[1], force[2]));
        }
    }

    void AddImpulse(BodyId id, const float impulse[3]) override {
        const JPH::BodyID* jid = Find(id);
        if (jid != nullptr) {
            // BodyInterface::AddImpulse is mass-normalized internally (dv = impulse / mass) and wakes
            // the body. Matches the reference backend's AddImpulse semantics.
            system_.GetBodyInterface().AddImpulse(*jid, JPH::Vec3(impulse[0], impulse[1], impulse[2]));
        }
    }

    void AddTorque(BodyId id, const float torque[3]) override {
        const JPH::BodyID* jid = Find(id);
        if (jid != nullptr) {
            JPH::BodyInterface& bi = system_.GetBodyInterface();
            bi.ActivateBody(*jid);
            bi.AddTorque(*jid, JPH::Vec3(torque[0], torque[1], torque[2]));
        }
    }

    void AddForceAtPosition(BodyId id, const float force[3], const float worldPoint[3]) override {
        const JPH::BodyID* jid = Find(id);
        if (jid != nullptr) {
            JPH::BodyInterface& bi = system_.GetBodyInterface();
            bi.ActivateBody(*jid);
            // Jolt's AddForce(point) overload accumulates the force AND the induced torque about the COM.
            bi.AddForce(*jid, JPH::Vec3(force[0], force[1], force[2]),
                        JPH::RVec3(worldPoint[0], worldPoint[1], worldPoint[2]));
        }
    }

    void GetAngularVelocity(BodyId id, float outAngular[3]) const override {
        const JPH::BodyID* jid = Find(id);
        if (jid == nullptr) {
            outAngular[0] = outAngular[1] = outAngular[2] = 0.0f;
            return;
        }
        const JPH::Vec3 w = system_.GetBodyInterface().GetAngularVelocity(*jid);
        outAngular[0] = w.GetX();
        outAngular[1] = w.GetY();
        outAngular[2] = w.GetZ();
    }

    void GetTransform(BodyId id, float outPos[3], float outRot[4]) const override {
        const JPH::BodyID* jid = Find(id);
        if (jid == nullptr) {
            outPos[0] = outPos[1] = outPos[2] = 0.0f;
            outRot[0] = outRot[1] = outRot[2] = 0.0f;
            outRot[3] = 1.0f;
            return;
        }
        const JPH::BodyInterface& bi = system_.GetBodyInterface();
        const JPH::RVec3 p = bi.GetPosition(*jid);
        const JPH::Quat q = bi.GetRotation(*jid);
        outPos[0] = static_cast<float>(p.GetX());
        outPos[1] = static_cast<float>(p.GetY());
        outPos[2] = static_cast<float>(p.GetZ());
        outRot[0] = q.GetX();
        outRot[1] = q.GetY();
        outRot[2] = q.GetZ();
        outRot[3] = q.GetW();
    }

    RaycastResult Raycast(const float origin[3], const float direction[3], float maxDistance) const override {
        RaycastResult result;
        const float len =
            std::sqrt((direction[0] * direction[0]) + (direction[1] * direction[1]) + (direction[2] * direction[2]));
        if (len < 1e-8f || maxDistance < 0.0f) {
            return result;
        }
        const JPH::Vec3 dir(direction[0] / len, direction[1] / len, direction[2] / len);
        // RRayCast direction carries the ray length, so mFraction is in [0,1] of maxDistance.
        const JPH::RRayCast ray{JPH::RVec3(origin[0], origin[1], origin[2]), dir * maxDistance};
        JPH::RayCastResult hit;
        if (!system_.GetNarrowPhaseQuery().CastRay(ray, hit)) {
            return result;
        }
        result.hit = true;
        result.body = static_cast<BodyId>(system_.GetBodyInterface().GetUserData(hit.mBodyID));
        result.distance = hit.mFraction * maxDistance;
        const JPH::RVec3 worldPoint = ray.GetPointOnRay(hit.mFraction);
        result.point[0] = static_cast<float>(worldPoint.GetX());
        result.point[1] = static_cast<float>(worldPoint.GetY());
        result.point[2] = static_cast<float>(worldPoint.GetZ());

        // Real surface normal via the leaf shape. Jolt's CastRay returns mSubShapeID2 (the leaf hit
        // by the ray); we then ask that shape for its local normal at the world hit point, and
        // rotate the result by the body's world transform. Falls back to -direction only when the
        // shape (e.g. via a future compound) cannot produce a normal at this point.
        const JPH::BodyInterface& bi = system_.GetBodyInterface();
        const JPH::ShapeRefC shape = bi.GetShape(hit.mBodyID);
        const JPH::RMat44 worldFromLocal = bi.GetWorldTransform(hit.mBodyID);
        if (shape != nullptr) {
            const JPH::Vec3 localPoint = JPH::Vec3(worldFromLocal.Inversed() * worldPoint);
            const JPH::Vec3 localNormal = shape->GetSurfaceNormal(hit.mSubShapeID2, localPoint);
            const JPH::Vec3 worldNormal = worldFromLocal.Multiply3x3(localNormal);
            const float nl = worldNormal.Length();
            if (nl > 0.0f) {
                const JPH::Vec3 n = worldNormal / nl;
                // Surface normal must face back along the ray (toward the origin), matching the
                // contract documented in physics_world.h:50 and the reference backend's behavior.
                const float facing = n.Dot(-dir);
                if (facing >= 0.0f) {
                    result.normal[0] = n.GetX();
                    result.normal[1] = n.GetY();
                    result.normal[2] = n.GetZ();
                } else {
                    result.normal[0] = -n.GetX();
                    result.normal[1] = -n.GetY();
                    result.normal[2] = -n.GetZ();
                }
            } else {
                result.normal[0] = -dir.GetX();
                result.normal[1] = -dir.GetY();
                result.normal[2] = -dir.GetZ();
            }
        } else {
            result.normal[0] = -dir.GetX();
            result.normal[1] = -dir.GetY();
            result.normal[2] = -dir.GetZ();
        }
        return result;
    }

    void Step(float dt) override { system_.Update(dt, 1, tempAllocator_.get(), jobSystem_.get()); }

    size_t BodyCount() const override { return bodies_.size(); }
    const char* BackendName() const override { return "jolt"; }

private:
    const JPH::BodyID* Find(BodyId id) const {
        auto it = bodies_.find(id);
        return it != bodies_.end() ? &it->second : nullptr;
    }

    BPLayerInterfaceImpl bpLayer_;
    ObjectVsBroadPhaseLayerFilterImpl objVsBpFilter_;
    ObjectLayerPairFilterImpl objPairFilter_;
    JPH::PhysicsSystem system_;
    std::unique_ptr<JPH::TempAllocator> tempAllocator_;
    std::unique_ptr<JPH::JobSystem> jobSystem_;
    std::unordered_map<BodyId, JPH::BodyID> bodies_;
    BodyId nextId_ = 1;
};

}  // namespace

std::unique_ptr<IPhysicsWorld> MakeJoltPhysicsWorld(const PhysicsConfig& config) {
    return std::make_unique<JoltPhysicsWorld>(config);
}

}  // namespace Next::physics
