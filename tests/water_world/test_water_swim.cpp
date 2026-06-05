// Swim / drown / oxygen (W12). A character whose HEAD is underwater depletes oxygen, then drowns (health
// drains) and dies (latched, one 'WDRN' event); surfacing refills the lungs; a WADER (head above water)
// breathes normally. Deterministic across runs.

#include <gtest/gtest.h>

#include <set>
#include <vector>

#include "next/boundary/transport.h"
#include "next/gameapi/sim_clock.h"
#include "next/runtime/transform.h"
#include "next/runtime/world.h"
#include "next/water/water_cell.h"
#include "next/water/water_def.h"
#include "next/water_world/water_store.h"
#include "next/water_world/water_swim.h"
#include "next/water_world/water_view.h"  // kWaterEventDrown

using namespace Next;
using namespace Next::water;
namespace boundary = Next::boundary;

namespace {

constexpr float kDt = 1.0f / 60.0f;

// A still pool: surface y=0, floor y=-10 (deep enough to fully submerge a character).
void LoadPool(WaterStore& store) {
    WaterBodyInstance b;
    b.bodyId = 1;
    b.type = static_cast<uint8_t>(WaterType::Pool);
    b.boundsMin[0] = -50.0f;
    b.boundsMin[1] = -10.0f;
    b.boundsMin[2] = -50.0f;
    b.boundsMax[0] = 50.0f;
    b.boundsMax[1] = 0.0f;
    b.boundsMax[2] = 50.0f;
    b.surfaceHeight = 0.0f;
    b.density = 1000.0f;
    b.flags = static_cast<uint16_t>(WaterBuoyant | WaterLethal);
    const std::vector<uint8_t> blob = PackCell(0, 0, 4096.0f, {b});
    store.LoadCell(0, 0, blob.data(), blob.size());
}

Entity SpawnSwimmer(World& world, float x, float y, float z, float oxygen, float headOffset = 0.6f) {
    Entity e = world.CreateEntity();
    auto& t = world.AddComponent<TransformComponent>(e);
    t.position[0] = x;
    t.position[1] = y;
    t.position[2] = z;
    SwimmerComponent sc;
    sc.oxygen = oxygen;
    sc.maxOxygen = oxygen;
    sc.headOffset = headOffset;
    world.AddComponent<SwimmerComponent>(e, sc);
    return e;
}

uint32_t DrainDrownEvents(boundary::InProcessTransport& transport) {
    uint32_t n = 0;
    boundary::GameEvent ev{};
    while (transport.PopEvent(ev)) {
        if (ev.type == kWaterEventDrown) {
            ++n;
        }
    }
    return n;
}

}  // namespace

TEST(WaterSwim, SubmergedDepletesOxygenThenDrowns) {
    World world;
    WaterStore store;
    LoadPool(store);
    boundary::InProcessTransport transport;
    // Fast clock to drown quickly: 2 s of air, 50 hp/s drown damage -> dead ~2 s after air runs out.
    SwimSystem swim(&store, nullptr, &transport, /*recovery*/ 5.0f, /*drownDmg*/ 50.0f);
    world.RegisterSystem(&swim);

    // Fully submerged: origin at -2 -> head at -1.4, well under the surface.
    Entity ch = SpawnSwimmer(world, 0.0f, -2.0f, 0.0f, /*oxygen*/ 2.0f);

    // Phase 1: 3 s (past the 2 s of air) -> oxygen drained to 0, drowning underway, not yet dead.
    for (int i = 0; i < 180; ++i) {
        world.Update(kDt);
    }
    const SwimmerComponent* sc = world.GetComponent<SwimmerComponent>(ch);
    ASSERT_NE(sc, nullptr);
    EXPECT_TRUE(sc->submerged);
    EXPECT_LE(sc->oxygen, 0.01f);
    EXPECT_TRUE(sc->drowning);
    EXPECT_LT(sc->health, 100.0f);  // taking damage
    EXPECT_FALSE(sc->dead);

    // Phase 2: keep underwater until health is gone -> drowned (latched + one event).
    for (int i = 0; i < 300; ++i) {
        world.Update(kDt);
    }
    EXPECT_TRUE(sc->dead);
    EXPECT_FLOAT_EQ(sc->health, 0.0f);
    EXPECT_EQ(swim.TotalDrownings(), 1u);
    EXPECT_EQ(DrainDrownEvents(transport), 1u);  // edge-triggered: death fires exactly once
}

TEST(WaterSwim, SurfacingRefillsOxygenAndAvoidsDrowning) {
    World world;
    WaterStore store;
    LoadPool(store);
    SwimSystem swim(&store, nullptr, nullptr, /*recovery*/ 10.0f, /*drownDmg*/ 50.0f);
    world.RegisterSystem(&swim);

    Entity ch = SpawnSwimmer(world, 0.0f, -2.0f, 0.0f, /*oxygen*/ 3.0f);
    SwimmerComponent* sc = world.GetComponent<SwimmerComponent>(ch);
    ASSERT_NE(sc, nullptr);

    // Submerge for 1 s: some air spent, still breathing room.
    for (int i = 0; i < 60; ++i) {
        world.Update(kDt);
    }
    EXPECT_LT(sc->oxygen, 3.0f);
    EXPECT_FALSE(sc->drowning);
    const float dipped = sc->oxygen;

    // Surface (lift the character clear of the water) and recover.
    TransformComponent* t = world.GetComponent<TransformComponent>(ch);
    t->position[1] = 5.0f;  // head well above the surface
    for (int i = 0; i < 120; ++i) {
        world.Update(kDt);
    }
    EXPECT_FALSE(sc->submerged);
    EXPECT_GT(sc->oxygen, dipped);               // refilled
    EXPECT_FLOAT_EQ(sc->oxygen, sc->maxOxygen);  // back to full
    EXPECT_FALSE(sc->dead);
    EXPECT_EQ(swim.TotalDrownings(), 0u);
}

TEST(WaterSwim, WaderWithHeadAboveWaterBreathes) {
    World world;
    WaterStore store;
    LoadPool(store);
    SwimSystem swim(&store, nullptr, nullptr);
    world.RegisterSystem(&swim);

    // Body in the water (origin at -0.3) but head clears it: origin -0.3 + headOffset 0.6 = +0.3 > 0.
    Entity ch = SpawnSwimmer(world, 0.0f, -0.3f, 0.0f, /*oxygen*/ 5.0f, /*headOffset*/ 0.6f);
    for (int i = 0; i < 600; ++i) {  // 10 s wading
        world.Update(kDt);
    }
    const SwimmerComponent* sc = world.GetComponent<SwimmerComponent>(ch);
    ASSERT_NE(sc, nullptr);
    EXPECT_FALSE(sc->submerged);        // head dry
    EXPECT_FLOAT_EQ(sc->oxygen, 5.0f);  // never lost a breath
    EXPECT_FALSE(sc->drowning);
}

// Drowning is gated on WaterLethal: a character fully submerged in NON-lethal water never loses air.
TEST(WaterSwim, NonLethalWaterDoesNotDrown) {
    World world;
    WaterStore store;
    // Same pool but WITHOUT WaterLethal -> safe water.
    WaterBodyInstance b;
    b.bodyId = 1;
    b.type = static_cast<uint8_t>(WaterType::Pool);
    b.boundsMin[0] = -50.0f;
    b.boundsMin[1] = -10.0f;
    b.boundsMin[2] = -50.0f;
    b.boundsMax[0] = 50.0f;
    b.boundsMax[1] = 0.0f;
    b.boundsMax[2] = 50.0f;
    b.surfaceHeight = 0.0f;
    b.density = 1000.0f;
    b.flags = WaterBuoyant;  // NOT lethal
    const std::vector<uint8_t> blob = PackCell(0, 0, 4096.0f, {b});
    store.LoadCell(0, 0, blob.data(), blob.size());

    boundary::InProcessTransport transport;
    SwimSystem swim(&store, nullptr, &transport, 5.0f, 50.0f);
    world.RegisterSystem(&swim);
    Entity ch = SpawnSwimmer(world, 0.0f, -2.0f, 0.0f, /*oxygen*/ 2.0f);
    for (int i = 0; i < 600; ++i) {  // 10 s fully submerged in safe water
        world.Update(kDt);
    }
    const SwimmerComponent* sc = world.GetComponent<SwimmerComponent>(ch);
    ASSERT_NE(sc, nullptr);
    EXPECT_TRUE(sc->submerged);         // head IS underwater (swim state)
    EXPECT_FLOAT_EQ(sc->oxygen, 2.0f);  // ...but safe water never depletes air
    EXPECT_FALSE(sc->drowning);
    EXPECT_FALSE(sc->dead);
    EXPECT_EQ(swim.TotalDrownings(), 0u);
    EXPECT_EQ(DrainDrownEvents(transport), 0u);
}

// Multiple swimmers each drown exactly once; events are per-subject (N>1 coverage).
TEST(WaterSwim, MultipleSwimmersEachDrownOnce) {
    World world;
    WaterStore store;
    LoadPool(store);  // lethal
    boundary::InProcessTransport transport;
    SwimSystem swim(&store, nullptr, &transport, 5.0f, 100.0f);
    world.RegisterSystem(&swim);

    std::vector<Entity> chars;
    for (int i = 0; i < 4; ++i) {
        chars.push_back(SpawnSwimmer(world, static_cast<float>(i * 2), -2.0f, 0.0f, /*oxygen*/ 1.0f));
    }
    for (int i = 0; i < 600; ++i) {
        world.Update(kDt);
    }
    for (Entity e : chars) {
        EXPECT_TRUE(world.GetComponent<SwimmerComponent>(e)->dead);
    }
    EXPECT_EQ(swim.TotalDrownings(), 4u);
    // Each death fires exactly one event whose subject is a distinct swimmer.
    std::set<boundary::EntityId> drowned;
    boundary::GameEvent ev{};
    while (transport.PopEvent(ev)) {
        if (ev.type == kWaterEventDrown) {
            drowned.insert(ev.subject);
        }
    }
    EXPECT_EQ(drowned.size(), 4u);
}

TEST(WaterSwim, DeterministicAcrossRuns) {
    auto run = [](std::vector<float>& trace) {
        World world;
        WaterStore store;
        LoadPool(store);
        SwimSystem swim(&store, nullptr, nullptr, 5.0f, 30.0f);
        world.RegisterSystem(&swim);
        Entity ch = SpawnSwimmer(world, 0.0f, -2.0f, 0.0f, 2.0f);
        for (int i = 0; i < 400; ++i) {
            world.Update(kDt);
            const SwimmerComponent* sc = world.GetComponent<SwimmerComponent>(ch);
            trace.push_back(sc->oxygen);
            trace.push_back(sc->health);
        }
    };
    std::vector<float> a;
    std::vector<float> b;
    run(a);
    run(b);
    ASSERT_EQ(a.size(), b.size());
    for (size_t i = 0; i < a.size(); ++i) {
        EXPECT_FLOAT_EQ(a[i], b[i]) << "divergence at sample " << i;
    }
}
