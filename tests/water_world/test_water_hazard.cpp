// Water x electronics short-circuit (W11). A device standing in conductive, submerged water shorts out
// (functional -> false, latched), edge-triggered (one 'WSHT' event), deterministic. Non-conductive water,
// dry positions, and above-surface positions leave it alone. A rising FLOOD reaches a fixed device and
// shorts it once the surface passes it — the gameplay payoff of the water sim's time-varying surface.

#include <gtest/gtest.h>

#include <set>
#include <vector>

#include "next/boundary/transport.h"
#include "next/gameapi/sim_clock.h"
#include "next/runtime/transform.h"
#include "next/runtime/world.h"
#include "next/water/water_cell.h"
#include "next/water/water_def.h"
#include "next/water_world/water_hazard.h"
#include "next/water_world/water_store.h"
#include "next/water_world/water_view.h"  // kWaterEventShort

using namespace Next;
using namespace Next::water;
namespace boundary = Next::boundary;

namespace {

constexpr float kDt = 1.0f / 60.0f;

// A still pool with the given flags: surface y=0, floor y=-10.
void LoadPool(WaterStore& store, uint16_t flags) {
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
    b.flags = flags;
    const std::vector<uint8_t> blob = PackCell(0, 0, 4096.0f, {b});
    store.LoadCell(0, 0, blob.data(), blob.size());
}

// A conductive flood rising from y=0 at `rate` m/s up to `maxH`.
void LoadConductiveFlood(WaterStore& store, float rate, float maxH) {
    WaterBodyInstance b;
    b.bodyId = 1;
    b.type = static_cast<uint8_t>(WaterType::Flood);
    b.boundsMin[0] = -50.0f;
    b.boundsMin[1] = -10.0f;
    b.boundsMin[2] = -50.0f;
    b.boundsMax[0] = 50.0f;
    b.boundsMax[1] = maxH;
    b.boundsMax[2] = 50.0f;
    b.surfaceHeight = 0.0f;
    b.density = 1000.0f;
    b.floodRate = rate;
    b.floodMaxHeight = maxH;
    b.flags = static_cast<uint16_t>(WaterBuoyant | WaterConductive);
    const std::vector<uint8_t> blob = PackCell(0, 0, 4096.0f, {b});
    store.LoadCell(0, 0, blob.data(), blob.size());
}

Entity SpawnDevice(World& world, float x, float y, float z) {
    Entity e = world.CreateEntity();
    auto& t = world.AddComponent<TransformComponent>(e);
    t.position[0] = x;
    t.position[1] = y;
    t.position[2] = z;
    world.AddComponent<ElectronicComponent>(e, ElectronicComponent{});
    return e;
}

uint32_t DrainShortEvents(boundary::InProcessTransport& transport, Entity expectSubject, bool& subjectOk) {
    uint32_t n = 0;
    subjectOk = true;
    boundary::GameEvent ev{};
    while (transport.PopEvent(ev)) {
        if (ev.type == kWaterEventShort) {
            ++n;
            if (ev.subject != static_cast<boundary::EntityId>(expectSubject)) {
                subjectOk = false;
            }
        }
    }
    return n;
}

}  // namespace

TEST(WaterHazard, DeviceSubmergedInConductiveWaterShortsOut) {
    World world;
    WaterStore store;
    LoadPool(store, static_cast<uint16_t>(WaterBuoyant | WaterConductive));
    boundary::InProcessTransport transport;
    gameapi::SimClock clock;
    WaterHazardSystem hazard(&store, &clock, &transport);
    world.RegisterSystem(&hazard);

    Entity dev = SpawnDevice(world, 0.0f, -2.0f, 0.0f);  // 2 m under the surface, in conductive water
    world.Update(kDt);

    const ElectronicComponent* ec = world.GetComponent<ElectronicComponent>(dev);
    ASSERT_NE(ec, nullptr);
    EXPECT_TRUE(ec->shortedOut);
    EXPECT_FALSE(ec->functional);
    EXPECT_EQ(hazard.TotalShortedOut(), 1u);
    bool subjectOk = false;
    EXPECT_EQ(DrainShortEvents(transport, dev, subjectOk), 1u);
    EXPECT_TRUE(subjectOk);
}

TEST(WaterHazard, ShortIsEdgeTriggeredOncePerDevice) {
    World world;
    WaterStore store;
    LoadPool(store, static_cast<uint16_t>(WaterBuoyant | WaterConductive));
    boundary::InProcessTransport transport;
    WaterHazardSystem hazard(&store, nullptr, &transport);
    world.RegisterSystem(&hazard);

    Entity dev = SpawnDevice(world, 0.0f, -2.0f, 0.0f);
    for (int i = 0; i < 10; ++i) {
        world.Update(kDt);
    }
    EXPECT_EQ(hazard.TotalShortedOut(), 1u);  // latched: shorted once, not every tick
    bool subjectOk = false;
    EXPECT_EQ(DrainShortEvents(transport, dev, subjectOk), 1u);  // exactly one event
}

TEST(WaterHazard, DryAboveSurfaceOrNonConductiveStaysFunctional) {
    // Non-conductive submerged: safe.
    {
        World world;
        WaterStore store;
        LoadPool(store, WaterBuoyant);  // NOT conductive
        WaterHazardSystem hazard(&store, nullptr, nullptr);
        world.RegisterSystem(&hazard);
        Entity dev = SpawnDevice(world, 0.0f, -2.0f, 0.0f);  // submerged but water is inert
        world.Update(kDt);
        EXPECT_TRUE(world.GetComponent<ElectronicComponent>(dev)->functional);
        EXPECT_EQ(hazard.TotalShortedOut(), 0u);
    }
    // Conductive water but the device is ABOVE the surface: safe.
    {
        World world;
        WaterStore store;
        LoadPool(store, static_cast<uint16_t>(WaterBuoyant | WaterConductive));
        WaterHazardSystem hazard(&store, nullptr, nullptr);
        world.RegisterSystem(&hazard);
        Entity dev = SpawnDevice(world, 0.0f, 3.0f, 0.0f);  // over the water, not submerged
        world.Update(kDt);
        EXPECT_TRUE(world.GetComponent<ElectronicComponent>(dev)->functional);
        EXPECT_EQ(hazard.TotalShortedOut(), 0u);
    }
    // Dry (outside the body's XZ): safe.
    {
        World world;
        WaterStore store;
        LoadPool(store, static_cast<uint16_t>(WaterBuoyant | WaterConductive));
        WaterHazardSystem hazard(&store, nullptr, nullptr);
        world.RegisterSystem(&hazard);
        Entity dev = SpawnDevice(world, 1000.0f, -2.0f, 0.0f);  // far outside the pool
        world.Update(kDt);
        EXPECT_TRUE(world.GetComponent<ElectronicComponent>(dev)->functional);
        EXPECT_EQ(hazard.TotalShortedOut(), 0u);
    }
}

// Multiple devices each short exactly once; events are per-subject (N>1 coverage).
TEST(WaterHazard, MultipleDevicesEachShortOnce) {
    World world;
    WaterStore store;
    LoadPool(store, static_cast<uint16_t>(WaterBuoyant | WaterConductive));
    boundary::InProcessTransport transport;
    WaterHazardSystem hazard(&store, nullptr, &transport);
    world.RegisterSystem(&hazard);

    std::vector<Entity> devices;
    for (int i = 0; i < 5; ++i) {
        devices.push_back(SpawnDevice(world, static_cast<float>(i * 2), -2.0f, 0.0f));
    }
    for (int i = 0; i < 5; ++i) {
        world.Update(kDt);
    }
    for (Entity e : devices) {
        EXPECT_TRUE(world.GetComponent<ElectronicComponent>(e)->shortedOut);
    }
    EXPECT_EQ(hazard.TotalShortedOut(), 5u);
    std::set<boundary::EntityId> shorted;
    boundary::GameEvent ev{};
    while (transport.PopEvent(ev)) {
        if (ev.type == kWaterEventShort) {
            shorted.insert(ev.subject);
        }
    }
    EXPECT_EQ(shorted.size(), 5u);  // one event per distinct device
}

TEST(WaterHazard, RisingFloodReachesAndShortsFixedDevice) {
    World world;
    WaterStore store;
    LoadConductiveFlood(store, /*rate*/ 1.0f, /*maxH*/ 5.0f);  // surface climbs 1 m/s
    boundary::InProcessTransport transport;
    gameapi::SimClock clock;  // fixedDt = 1/60
    WaterHazardSystem hazard(&store, &clock, &transport);
    world.RegisterSystem(&hazard);

    Entity dev = SpawnDevice(world, 0.0f, 2.0f, 0.0f);  // fixed device 2 m up; flood starts below it

    // Early on the surface is below the device -> still functional.
    for (int i = 0; i < 60; ++i) {  // ~1 s -> surface ~1 m < 2 m
        clock.Advance();
        world.Update(kDt);
    }
    EXPECT_TRUE(world.GetComponent<ElectronicComponent>(dev)->functional) << "shorted before the flood arrived";

    // Keep rising; once the surface passes y=2 the device is submerged in conductive water -> shorts.
    for (int i = 0; i < 180; ++i) {  // ~3 more s -> surface passes 2 m
        clock.Advance();
        world.Update(kDt);
    }
    EXPECT_TRUE(world.GetComponent<ElectronicComponent>(dev)->shortedOut) << "flood did not short the device";
    EXPECT_EQ(hazard.TotalShortedOut(), 1u);
    bool subjectOk = false;
    EXPECT_EQ(DrainShortEvents(transport, dev, subjectOk), 1u);
    EXPECT_TRUE(subjectOk);
}
