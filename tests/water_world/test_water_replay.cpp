// Replay-divergence diagnostic harness (W4). Determinism is the replay/anti-cheat red line, but a
// whole-state hash only tells you THAT two runs diverged, not WHERE. This harness records a checksum
// PER SUBSYSTEM each tick — physics (body kinematics), water (surface/submersion probes), actuation
// (intent state) — and a comparator that reports the EARLIEST tick and the SUBSYSTEM that first
// diverged. It is proven two ways: (1) two identical runs never diverge on any channel; (2) a
// divergence injected into ONE subsystem is localized to exactly that channel (and the right tick),
// so the harness can actually catch a regression, not merely assert determinism it can't verify.
//
// Runs under ASan/UBSan in CI.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "next/gameapi/components.h"
#include "next/gameapi/sim_clock.h"
#include "next/gameplay/actuation_system.h"
#include "next/physics/components.h"
#include "next/physics/physics_system.h"
#include "next/physics/reference_physics_world.h"
#include "next/runtime/transform.h"
#include "next/runtime/world.h"
#include "next/water/water_cell.h"
#include "next/water/water_def.h"
#include "next/water_world/water_force_system.h"
#include "next/water_world/water_store.h"

using namespace Next;
using namespace Next::physics;
using namespace Next::water;

namespace {

constexpr float kDt = 1.0f / 60.0f;

// ---- deterministic FNV-1a folding over the raw float bits (no locale / formatting in the path) ----
constexpr uint64_t kFnvOffset = 1469598103934665603ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;

void HashBytes(uint64_t& h, const void* p, size_t n) {
    const auto* b = static_cast<const uint8_t*>(p);
    for (size_t i = 0; i < n; ++i) {
        h = (h ^ b[i]) * kFnvPrime;
    }
}
void HashF(uint64_t& h, float f) {
    uint32_t u = 0;
    std::memcpy(&u, &f, sizeof(u));
    HashBytes(h, &u, sizeof(u));
}
void HashU(uint64_t& h, uint64_t u) {
    HashBytes(h, &u, sizeof(u));
}

// The three per-subsystem checksums captured at one tick.
struct Channels {
    uint64_t physics = kFnvOffset;
    uint64_t water = kFnvOffset;
    uint64_t actuation = kFnvOffset;
};

// Fixed water probe lattice — a pure function of the water surface math (det_trig + waves + phase),
// INDEPENDENT of the bodies, so the water channel isolates surface regressions from physics ones.
struct Probe {
    float x;
    float z;
};
const Probe kProbes[] = {{-30, -30}, {-10, 5}, {0, 0}, {7, -12}, {25, 25}, {40, -3}, {-50, 18}};

Channels Snapshot(World& world, IPhysicsWorld* physics, const WaterStore& store, double t) {
    Channels c;

    // physics: every dynamic/static body's full kinematic state, folded with its entity id.
    world.Each<RigidBodyComponent>([&](Entity e, RigidBodyComponent& rb) {
        if (rb.body == kInvalidBody || !physics->IsValid(rb.body)) {
            return;
        }
        HashU(c.physics, static_cast<uint64_t>(e));
        float pos[3];
        float rot[4];
        float vel[3];
        float ang[3];
        physics->GetTransform(rb.body, pos, rot);
        physics->GetLinearVelocity(rb.body, vel);
        physics->GetAngularVelocity(rb.body, ang);
        for (float v : pos)
            HashF(c.physics, v);
        for (float v : rot)
            HashF(c.physics, v);
        for (float v : vel)
            HashF(c.physics, v);
        for (float v : ang)
            HashF(c.physics, v);
    });

    // water: surface height + submersion sampled at the fixed lattice at the authoritative time t.
    for (const Probe& p : kProbes) {
        WaterSample s;
        const bool hit = store.SampleWaterAt(p.x, -0.25f, p.z, t, s);
        HashU(c.water, hit ? 1u : 0u);
        if (hit) {
            HashF(c.water, s.surfaceHeight);
            HashF(c.water, s.submersionDepth);
            HashU(c.water, s.submerged ? 1u : 0u);
        }
    }

    // actuation: the intent state driving the single mover, folded with its entity id.
    world.Each<gameapi::MoveTarget>([&](Entity e, gameapi::MoveTarget& mt) {
        HashU(c.actuation, static_cast<uint64_t>(e));
        HashF(c.actuation, mt.target.x);
        HashF(c.actuation, mt.target.y);
        HashF(c.actuation, mt.target.z);
        HashF(c.actuation, mt.maxSpeed);
        HashU(c.actuation, static_cast<uint64_t>(mt.active));
    });

    return c;
}

struct Divergence {
    bool diverged = false;
    size_t tick = 0;
    const char* channel = "none";
};

// Earliest tick at which ANY channel differs; the channel is reported in a fixed inspection order.
Divergence FirstDivergence(const std::vector<Channels>& a, const std::vector<Channels>& b) {
    const size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; ++i) {
        if (a[i].physics != b[i].physics)
            return {true, i, "physics"};
        if (a[i].water != b[i].water)
            return {true, i, "water"};
        if (a[i].actuation != b[i].actuation)
            return {true, i, "actuation"};
    }
    if (a.size() != b.size())
        return {true, n, "length"};
    return {false, 0, "none"};
}

// A wavy pool so the water channel is a rich function of time/space. surfaceAmp lets a test perturb
// ONLY the water surface; bodyId 1, surface y=0, floor y=-50.
void LoadWavyPool(WaterStore& store, float surfaceAmp) {
    WaterBodyInstance b;
    b.bodyId = 1;
    b.type = static_cast<uint8_t>(WaterType::Lake);
    b.boundsMin[0] = -200.0f;
    b.boundsMin[1] = -50.0f;
    b.boundsMin[2] = -200.0f;
    b.boundsMax[0] = 200.0f;
    b.boundsMax[1] = 0.0f;
    b.boundsMax[2] = 200.0f;
    b.surfaceHeight = 0.0f;
    b.density = 1000.0f;
    b.linearDrag = 2.0f;
    b.quadraticDrag = 1.0f;
    b.flags = WaterBuoyant;
    b.waveCount = 1;
    b.waves[0] = {surfaceAmp, 12.0f, {1.0f, 0.0f}, 1.0f, 0.2f};
    const std::vector<uint8_t> blob = PackCell(0, 0, 4096.0f, {b});
    store.LoadCell(0, 0, blob.data(), blob.size());
}

Entity SpawnBoat(World& world, float startX, float massScale) {
    Entity e = world.CreateEntity();
    auto& t = world.AddComponent<TransformComponent>(e);
    t.position[0] = startX;
    t.position[1] = -0.1f;
    RigidBodyComponent rb;
    rb.desc.motion = MotionType::Dynamic;
    rb.desc.shape = ShapeType::Box;
    rb.desc.halfExtents[0] = 1.0f;
    rb.desc.halfExtents[1] = 0.5f;
    rb.desc.halfExtents[2] = 0.5f;
    rb.desc.mass = 600.0f * (8.0f * 1.0f * 0.5f * 0.5f) * massScale;
    rb.desc.position[0] = startX;
    rb.desc.position[1] = -0.1f;
    world.AddComponent<RigidBodyComponent>(e, rb);
    return e;
}

gameapi::MoveTarget MoveTo(float x, float y, float z, float maxSpeed) {
    gameapi::MoveTarget mt;
    mt.target = {x, y, z};
    mt.maxSpeed = maxSpeed;
    mt.active = 1;
    return mt;
}

}  // namespace

// (1) Two byte-identical setups produce byte-identical per-subsystem checksum streams: no divergence.
TEST(WaterReplay, IdenticalRunsNeverDiverge) {
    auto record = [](std::vector<Channels>& out) {
        World world;
        auto physics = MakeReferencePhysicsWorld();
        WaterStore store;
        LoadWavyPool(store, 0.3f);
        gameapi::SimClock clock;
        gameplay::ActuationSystem actuation(physics.get());
        WaterForceSystem waterForce(&store, physics.get(), &clock);
        PhysicsSystem physicsSys(physics.get());
        world.RegisterSystem(&actuation);
        world.RegisterSystem(&waterForce);
        world.RegisterSystem(&physicsSys);
        Entity boat = SpawnBoat(world, -20.0f, 1.0f);
        world.AddComponent<gameapi::MoveTarget>(boat, MoveTo(20.0f, -0.1f, 0.0f, 5.0f));
        for (int i = 0; i < 400; ++i) {
            clock.Advance();
            world.Update(kDt);
            out.push_back(Snapshot(world, physics.get(), store, clock.seconds));
        }
    };
    std::vector<Channels> a;
    std::vector<Channels> b;
    record(a);
    record(b);
    const Divergence d = FirstDivergence(a, b);
    EXPECT_FALSE(d.diverged) << "diverged on '" << d.channel << "' at tick " << d.tick;
}

// (2) A divergence injected into PHYSICS ONLY (a tiny boat-mass change) localizes to the physics
// channel; water (body-independent probes) and actuation (same intent) stay identical.
TEST(WaterReplay, LocalizesPhysicsDivergence) {
    auto record = [](std::vector<Channels>& out, float massScale) {
        World world;
        auto physics = MakeReferencePhysicsWorld();
        WaterStore store;
        LoadWavyPool(store, 0.3f);  // SAME water in both runs
        gameapi::SimClock clock;
        gameplay::ActuationSystem actuation(physics.get());
        WaterForceSystem waterForce(&store, physics.get(), &clock);
        PhysicsSystem physicsSys(physics.get());
        world.RegisterSystem(&actuation);
        world.RegisterSystem(&waterForce);
        world.RegisterSystem(&physicsSys);
        Entity boat = SpawnBoat(world, -20.0f, massScale);
        world.AddComponent<gameapi::MoveTarget>(boat, MoveTo(20.0f, -0.1f, 0.0f, 5.0f));
        for (int i = 0; i < 400; ++i) {
            clock.Advance();
            world.Update(kDt);
            out.push_back(Snapshot(world, physics.get(), store, clock.seconds));
        }
    };
    std::vector<Channels> a;
    std::vector<Channels> b;
    record(a, 1.0f);
    record(b, 1.0005f);  // 0.05% heavier boat
    const Divergence d = FirstDivergence(a, b);
    ASSERT_TRUE(d.diverged);
    EXPECT_STREQ(d.channel, "physics") << "expected the physics channel to catch a mass change";
}

// (3) A divergence injected into WATER ONLY (a different wave amplitude) localizes to the water channel
// at tick 0. With no dynamic bodies, the physics channel stays identical — clean isolation.
TEST(WaterReplay, LocalizesWaterDivergence) {
    auto record = [](std::vector<Channels>& out, float surfaceAmp) {
        World world;
        auto physics = MakeReferencePhysicsWorld();
        WaterStore store;
        LoadWavyPool(store, surfaceAmp);
        gameapi::SimClock clock;
        WaterForceSystem waterForce(&store, physics.get(), &clock);
        PhysicsSystem physicsSys(physics.get());
        world.RegisterSystem(&waterForce);
        world.RegisterSystem(&physicsSys);
        for (int i = 0; i < 50; ++i) {
            clock.Advance();
            world.Update(kDt);
            out.push_back(Snapshot(world, physics.get(), store, clock.seconds));
        }
    };
    std::vector<Channels> a;
    std::vector<Channels> b;
    record(a, 0.30f);
    record(b, 0.35f);  // taller waves
    const Divergence d = FirstDivergence(a, b);
    ASSERT_TRUE(d.diverged);
    EXPECT_STREQ(d.channel, "water");
    EXPECT_EQ(d.tick, 0u) << "a surface change is observable on the very first probe";
}

// (4) A divergence injected into ACTUATION ONLY (a different MoveTarget on a NON-physics entity)
// localizes to the actuation channel at tick 0; physics/water are untouched.
TEST(WaterReplay, LocalizesActuationDivergence) {
    auto record = [](std::vector<Channels>& out, float targetX) {
        World world;
        auto physics = MakeReferencePhysicsWorld();
        WaterStore store;
        LoadWavyPool(store, 0.3f);
        gameapi::SimClock clock;
        gameplay::ActuationSystem actuation(physics.get());
        WaterForceSystem waterForce(&store, physics.get(), &clock);
        PhysicsSystem physicsSys(physics.get());
        world.RegisterSystem(&actuation);
        world.RegisterSystem(&waterForce);
        world.RegisterSystem(&physicsSys);
        Entity e = world.CreateEntity();  // NON-physics: actuation integrates its Transform directly
        world.AddComponent<TransformComponent>(e);
        world.AddComponent<gameapi::MoveTarget>(e, MoveTo(targetX, 3.0f, 0.0f, 1.0f));
        for (int i = 0; i < 50; ++i) {
            clock.Advance();
            world.Update(kDt);
            out.push_back(Snapshot(world, physics.get(), store, clock.seconds));
        }
    };
    std::vector<Channels> a;
    std::vector<Channels> b;
    record(a, 5.0f);
    record(b, 6.0f);  // different destination
    const Divergence d = FirstDivergence(a, b);
    ASSERT_TRUE(d.diverged);
    EXPECT_STREQ(d.channel, "actuation");
    EXPECT_EQ(d.tick, 0u);
}
