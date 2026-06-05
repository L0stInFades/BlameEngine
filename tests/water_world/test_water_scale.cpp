// Water scale / stress / determinism (ADR-0015) — the bar that SURPASSES vegetation's scale test, which
// has no force dynamics. Here: (A) the broadphase stays bounded AND correct vs brute force over many
// bodies; (B) hundreds of varied-density bodies all settle to their exact Archimedes equilibrium with
// NO rocket-launch over thousands of ticks (the velocity-clamp stability proof at scale); (C) a
// multi-body sim is bit-deterministic across runs (replay / anti-cheat). All deterministic, ASan-clean.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "next/physics/components.h"
#include "next/physics/physics_system.h"
#include "next/physics/reference_physics_world.h"
#include "next/runtime/transform.h"
#include "next/runtime/world.h"
#include "next/water/water_cell.h"
#include "next/water/water_def.h"
#include "next/water/water_volume.h"
#include "next/water_world/water_force_system.h"
#include "next/water_world/water_store.h"

using namespace Next;
using namespace Next::physics;
using namespace Next::water;

namespace {

constexpr float kDt = 1.0f / 60.0f;
constexpr float kPi = 3.14159265358979323846f;

void LoadBigPool(WaterStore& store, float rho = 1000.0f) {
    WaterBodyInstance b;
    b.bodyId = 1;
    b.type = static_cast<uint8_t>(WaterType::Pool);
    b.boundsMin[0] = -500.0f;
    b.boundsMin[1] = -50.0f;
    b.boundsMin[2] = -500.0f;
    b.boundsMax[0] = 500.0f;
    b.boundsMax[1] = 0.0f;
    b.boundsMax[2] = 500.0f;
    b.surfaceHeight = 0.0f;
    b.density = rho;
    b.linearDrag = 2.0f;
    b.quadraticDrag = 1.0f;
    b.flags = WaterBuoyant;
    const std::vector<uint8_t> blob = PackCell(0, 0, 4096.0f, {b});
    store.LoadCell(0, 0, blob.data(), blob.size());
}

}  // namespace

TEST(WaterScale, BroadphaseBoundedAndCorrectVsBruteForce) {
    WaterStore store(16.0f);
    // 400 small pools laid out on a 20x20 grid, 10 m apart (each 8x8 m).
    std::vector<WaterBodyInstance> bodies;
    bodies.reserve(400);
    uint32_t id = 1;
    for (int gx = 0; gx < 20; ++gx) {
        for (int gz = 0; gz < 20; ++gz) {
            WaterBodyInstance b;
            b.bodyId = id++;
            b.type = static_cast<uint8_t>(WaterType::Pool);
            const float x = static_cast<float>(gx) * 10.0f;
            const float z = static_cast<float>(gz) * 10.0f;
            b.boundsMin[0] = x;
            b.boundsMin[1] = -2.0f;
            b.boundsMin[2] = z;
            b.boundsMax[0] = x + 8.0f;
            b.boundsMax[1] = 0.0f;
            b.boundsMax[2] = z + 8.0f;
            b.surfaceHeight = 0.0f;
            b.density = 1000.0f;
            b.flags = WaterBuoyant;
            bodies.push_back(b);
        }
    }
    const std::vector<uint8_t> blob = PackCell(0, 0, 4096.0f, bodies);
    ASSERT_TRUE(store.LoadCell(0, 0, blob.data(), blob.size()));
    EXPECT_EQ(store.BodyCount(), 400u);

    // Query a small AABB; broadphase result must equal the brute-force overlap set, and be small.
    const float qMinX = 45.0f;
    const float qMinZ = 45.0f;
    const float qMaxX = 55.0f;
    const float qMaxZ = 55.0f;
    const std::vector<uint32_t> got = store.BodiesOverlappingAabb(qMinX, qMinZ, qMaxX, qMaxZ);
    EXPECT_LT(got.size(), 25u);  // bounded — nowhere near all 400

    std::vector<uint32_t> brute;
    for (const WaterBodyInstance& b : bodies) {
        if (b.boundsMin[0] <= qMaxX && b.boundsMax[0] >= qMinX && b.boundsMin[2] <= qMaxZ && b.boundsMax[2] >= qMinZ) {
            brute.push_back(b.bodyId);
        }
    }
    std::sort(brute.begin(), brute.end());
    EXPECT_EQ(got, brute);  // broadphase is exact, not just fast
}

TEST(WaterScale, ManyVariedBodiesSettleNoBlowup) {
    World world;
    auto physics = MakeReferencePhysicsWorld();
    WaterStore store;
    LoadBigPool(store, 1000.0f);
    WaterForceSystem waterForce(&store, physics.get());
    PhysicsSystem physicsSys(physics.get());
    world.RegisterSystem(&waterForce);
    world.RegisterSystem(&physicsSys);

    constexpr int kCount = 600;
    const float r = 0.5f;
    const float vTot = (4.0f / 3.0f) * kPi * r * r * r;
    std::vector<Entity> balls;
    std::vector<float> densities;
    balls.reserve(kCount);
    densities.reserve(kCount);
    for (int i = 0; i < kCount; ++i) {
        const float density = 200.0f + static_cast<float>(i % 16) * 50.0f;  // 200..950 (all float)
        densities.push_back(density);
        Entity e = world.CreateEntity();
        auto& t = world.AddComponent<TransformComponent>(e);
        // spread across the pool in XZ; start just above the surface
        const float x = static_cast<float>((i % 25) - 12) * 3.0f;
        const float z = static_cast<float>((i / 25) - 12) * 3.0f;
        t.position[0] = x;
        t.position[1] = 0.3f;
        t.position[2] = z;
        RigidBodyComponent rb;
        rb.desc.motion = MotionType::Dynamic;
        rb.desc.shape = ShapeType::Sphere;
        rb.desc.halfExtents[0] = rb.desc.halfExtents[1] = rb.desc.halfExtents[2] = r;
        rb.desc.mass = density * vTot;
        rb.desc.position[0] = x;
        rb.desc.position[1] = 0.3f;
        rb.desc.position[2] = z;
        world.AddComponent<RigidBodyComponent>(e, rb);
        balls.push_back(e);
    }

    float maxSpeed = 0.0f;
    for (int step = 0; step < 3000; ++step) {
        world.Update(kDt);
        if (step % 50 == 0 || step == 2999) {
            for (Entity e : balls) {
                const RigidBodyComponent* rb = world.GetComponent<RigidBodyComponent>(e);
                if (rb == nullptr) {
                    continue;
                }
                float v[3];
                physics->GetLinearVelocity(rb->body, v);
                const float speed = std::sqrt((v[0] * v[0]) + (v[1] * v[1]) + (v[2] * v[2]));
                maxSpeed = std::max(maxSpeed, speed);
            }
        }
    }
    EXPECT_LT(maxSpeed, 40.0f);  // NOTHING rocket-launched (drag clamp holds at scale)

    int settled = 0;
    for (int i = 0; i < kCount; ++i) {
        const RigidBodyComponent* rb = world.GetComponent<RigidBodyComponent>(balls[i]);
        ASSERT_NE(rb, nullptr);
        float pos[3];
        float rot[4];
        physics->GetTransform(rb->body, pos, rot);
        float frac = 0.0f;
        SubmergedSphereVolume(pos[1], r, 0.0f, -50.0f, frac);
        if (std::fabs(frac - densities[i] / 1000.0f) < 0.05f) {
            ++settled;
        }
    }
    EXPECT_EQ(settled, kCount);  // every body reached its analytic Archimedes equilibrium
}

TEST(WaterScale, DeterministicAtScale) {
    auto run = []() {
        World world;
        auto physics = MakeReferencePhysicsWorld();
        WaterStore store;
        LoadBigPool(store, 1000.0f);
        WaterForceSystem waterForce(&store, physics.get());
        PhysicsSystem physicsSys(physics.get());
        world.RegisterSystem(&waterForce);
        world.RegisterSystem(&physicsSys);

        const float r = 0.5f;
        const float vTot = (4.0f / 3.0f) * kPi * r * r * r;
        std::vector<Entity> balls;
        for (int i = 0; i < 200; ++i) {
            Entity e = world.CreateEntity();
            auto& t = world.AddComponent<TransformComponent>(e);
            const float x = static_cast<float>(i % 20) * 2.0f;
            t.position[0] = x;
            t.position[1] = 1.5f;
            RigidBodyComponent rb;
            rb.desc.motion = MotionType::Dynamic;
            rb.desc.shape = ShapeType::Sphere;
            rb.desc.halfExtents[0] = rb.desc.halfExtents[1] = rb.desc.halfExtents[2] = r;
            rb.desc.mass = (300.0f + static_cast<float>(i % 7) * 80.0f) * vTot;
            rb.desc.position[0] = x;
            rb.desc.position[1] = 1.5f;
            world.AddComponent<RigidBodyComponent>(e, rb);
            balls.push_back(e);
        }
        for (int s = 0; s < 1200; ++s) {
            world.Update(kDt);
        }
        // Hash the final state in deterministic (entity-order) iteration.
        uint64_t hash = 1469598103934665603ull;  // FNV-1a offset
        for (Entity e : balls) {
            const RigidBodyComponent* rb = world.GetComponent<RigidBodyComponent>(e);
            float pos[3];
            float rot[4];
            physics->GetTransform(rb->body, pos, rot);
            for (int k = 0; k < 3; ++k) {
                const int32_t q = static_cast<int32_t>(std::lround(pos[k] * 1000.0f));
                hash = (hash ^ static_cast<uint64_t>(static_cast<uint32_t>(q))) * 1099511628211ull;
            }
        }
        return hash;
    };
    EXPECT_EQ(run(), run());  // bit-identical state hash across two independent runs
}
