#include "next/physics/reference_physics_world.h"

#include <cmath>
#include <limits>

namespace Next::physics {
namespace {

// Axis-aligned half-extents the reference backend uses for collision: a sphere becomes its
// bounding cube; a box uses its half-extents directly. Rotation is intentionally ignored (this is
// a deterministic stand-in, not a true solver).
void AabbHalf(const BodyDesc& desc, float out[3]) {
    if (desc.shape == ShapeType::Sphere) {
        out[0] = out[1] = out[2] = desc.halfExtents[0];
    } else {
        out[0] = desc.halfExtents[0];
        out[1] = desc.halfExtents[1];
        out[2] = desc.halfExtents[2];
    }
}

}  // namespace

BodyId ReferencePhysicsWorld::CreateBody(const BodyDesc& desc) {
    const BodyId id = nextId_++;
    if (nextId_ == kInvalidBody) {
        nextId_ = 1;  // never hand out 0 (unreachable in practice; defensive)
    }
    Body b;
    b.desc = desc;
    if (b.desc.mass <= 0.0f) {
        b.desc.mass = 1.0f;
    }
    for (int i = 0; i < 3; ++i) {
        b.position[i] = desc.position[i];
        b.velocity[i] = desc.linearVelocity[i];
    }
    for (int i = 0; i < 4; ++i) {
        b.rotation[i] = desc.rotation[i];
    }
    bodies_.emplace(id, b);
    return id;
}

void ReferencePhysicsWorld::DestroyBody(BodyId id) {
    bodies_.erase(id);
}

bool ReferencePhysicsWorld::IsValid(BodyId id) const {
    return id != kInvalidBody && bodies_.find(id) != bodies_.end();
}

void ReferencePhysicsWorld::SetLinearVelocity(BodyId id, const float v[3]) {
    auto it = bodies_.find(id);
    if (it == bodies_.end()) {
        return;
    }
    for (int i = 0; i < 3; ++i) {
        it->second.velocity[i] = v[i];
    }
}

void ReferencePhysicsWorld::GetLinearVelocity(BodyId id, float outV[3]) const {
    auto it = bodies_.find(id);
    const float* v = (it != bodies_.end()) ? it->second.velocity : nullptr;
    for (int i = 0; i < 3; ++i) {
        outV[i] = v != nullptr ? v[i] : 0.0f;
    }
}

void ReferencePhysicsWorld::SetPosition(BodyId id, const float p[3]) {
    auto it = bodies_.find(id);
    if (it == bodies_.end()) {
        return;
    }
    for (int i = 0; i < 3; ++i) {
        it->second.position[i] = p[i];
    }
}

void ReferencePhysicsWorld::GetTransform(BodyId id, float outPos[3], float outRot[4]) const {
    auto it = bodies_.find(id);
    if (it == bodies_.end()) {
        for (int i = 0; i < 3; ++i) {
            outPos[i] = 0.0f;
        }
        outRot[0] = outRot[1] = outRot[2] = 0.0f;
        outRot[3] = 1.0f;
        return;
    }
    for (int i = 0; i < 3; ++i) {
        outPos[i] = it->second.position[i];
    }
    for (int i = 0; i < 4; ++i) {
        outRot[i] = it->second.rotation[i];
    }
}

RaycastResult ReferencePhysicsWorld::Raycast(const float origin[3], const float direction[3], float maxDistance) const {
    RaycastResult result;
    const float dirLen =
        std::sqrt((direction[0] * direction[0]) + (direction[1] * direction[1]) + (direction[2] * direction[2]));
    if (dirLen < 1e-8f || maxDistance < 0.0f) {
        return result;  // degenerate ray
    }
    const float dir[3] = {direction[0] / dirLen, direction[1] / dirLen, direction[2] / dirLen};

    constexpr float kInf = std::numeric_limits<float>::infinity();
    float bestT = maxDistance;
    // Iterate bodies in ascending-id order (std::map) for deterministic tie-breaking.
    for (const auto& [id, b] : bodies_) {
        float half[3];
        AabbHalf(b.desc, half);

        float tNear = -kInf;
        float tFar = kInf;
        int entryAxis = -1;
        bool miss = false;
        for (int i = 0; i < 3; ++i) {
            const float bmin = b.position[i] - half[i];
            const float bmax = b.position[i] + half[i];
            if (std::fabs(dir[i]) < 1e-8f) {
                if (origin[i] < bmin || origin[i] > bmax) {
                    miss = true;  // ray parallel to this slab and outside it
                    break;
                }
                continue;
            }
            const float inv = 1.0f / dir[i];
            float t1 = (bmin - origin[i]) * inv;
            float t2 = (bmax - origin[i]) * inv;
            if (t1 > t2) {
                const float tmp = t1;
                t1 = t2;
                t2 = tmp;
            }
            if (t1 > tNear) {
                tNear = t1;
                entryAxis = i;
            }
            if (t2 < tFar) {
                tFar = t2;
            }
            if (tNear > tFar) {
                miss = true;
                break;
            }
        }
        if (miss || tFar < 0.0f) {
            continue;
        }
        const bool originInside = tNear < 0.0f;
        const float tHit = originInside ? 0.0f : tNear;
        if (tHit > bestT) {
            continue;
        }

        bestT = tHit;
        result.hit = true;
        result.body = id;
        result.distance = tHit;
        for (int i = 0; i < 3; ++i) {
            result.point[i] = origin[i] + (dir[i] * tHit);
            result.normal[i] = 0.0f;
        }
        if (originInside || entryAxis < 0) {
            // Inside the body: report a normal facing back toward the ray origin.
            for (int i = 0; i < 3; ++i) {
                result.normal[i] = -dir[i];
            }
        } else {
            result.normal[entryAxis] = (dir[entryAxis] > 0.0f) ? -1.0f : 1.0f;
        }
    }
    return result;
}

void ReferencePhysicsWorld::ResolveAgainstStatic(Body& d, const Body& s) const {
    float dh[3];
    float sh[3];
    AabbHalf(d.desc, dh);
    AabbHalf(s.desc, sh);

    float penetration[3];
    float sign[3];
    for (int i = 0; i < 3; ++i) {
        const float delta = d.position[i] - s.position[i];
        penetration[i] = (dh[i] + sh[i]) - std::fabs(delta);
        if (penetration[i] <= 0.0f) {
            return;  // separated on this axis -> no collision
        }
        sign[i] = (delta >= 0.0f) ? 1.0f : -1.0f;  // push d away from s along +/- axis
    }

    // Penetrating on all three axes: push out along the axis of least penetration.
    int axis = 0;
    if (penetration[1] < penetration[axis]) {
        axis = 1;
    }
    if (penetration[2] < penetration[axis]) {
        axis = 2;
    }
    d.position[axis] += sign[axis] * penetration[axis];
    // Kill (or bounce) the velocity component moving INTO the static body.
    if (d.velocity[axis] * sign[axis] < 0.0f) {
        d.velocity[axis] = -d.velocity[axis] * d.desc.restitution;
    }
}

void ReferencePhysicsWorld::Step(float dt) {
    // 1. Integrate. Dynamic bodies get gravity; kinematic bodies move by their set velocity;
    //    static bodies never move.
    for (auto& [id, b] : bodies_) {
        (void)id;
        if (b.desc.motion == MotionType::Dynamic) {
            for (int i = 0; i < 3; ++i) {
                b.velocity[i] += config_.gravity[i] * dt;
                b.position[i] += b.velocity[i] * dt;
            }
        } else if (b.desc.motion == MotionType::Kinematic) {
            for (int i = 0; i < 3; ++i) {
                b.position[i] += b.velocity[i] * dt;
            }
        }
    }

    // 2. Resolve dynamic bodies against static bodies (ascending-id order => deterministic).
    for (auto& [dynId, d] : bodies_) {
        (void)dynId;
        if (d.desc.motion != MotionType::Dynamic) {
            continue;
        }
        for (const auto& [staticId, s] : bodies_) {
            (void)staticId;
            if (s.desc.motion == MotionType::Static) {
                ResolveAgainstStatic(d, s);
            }
        }
    }
}

std::unique_ptr<IPhysicsWorld> MakeReferencePhysicsWorld(const PhysicsConfig& config) {
    return std::make_unique<ReferencePhysicsWorld>(config);
}

}  // namespace Next::physics
