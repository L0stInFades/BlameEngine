#include <gtest/gtest.h>

#include <vector>

#include "next/vegetation/vegetation_cell.h"
#include "next/vegetation_world/vegetation_query.h"
#include "next/vegetation_world/vegetation_store.h"

using namespace Next::vegetation;
namespace gameapi = Next::gameapi;

namespace {

VegetationInstance MakeInstance(uint32_t ordinal, float x, float z, float radius, uint16_t flags,
                                uint32_t visual = 42) {
    VegetationInstance inst;
    inst.position[0] = x;
    inst.position[1] = 0.0f;
    inst.position[2] = z;
    inst.logicalRadius = radius;
    inst.flags = flags;
    inst.visual = visual;
    inst.instanceId = ordinal;
    inst.species = 1;
    inst.scale = 1.0f;
    return inst;
}

void LoadInstances(VegetationStore& store, int32_t cx, int32_t cz, const std::vector<VegetationInstance>& insts) {
    const std::vector<uint8_t> blob = PackCell(cx, cz, 64.0f, insts);
    ASSERT_TRUE(store.LoadCell(cx, cz, blob.data(), blob.size()));
}

struct StubQuery final : gameapi::IWorldQuery {
    gameapi::RaycastResult result{};
    gameapi::RaycastResult Raycast(const float[3], const float[3], float) override { return result; }
};

}  // namespace

TEST(VegetationQuery, SegmentBlockedByBlocker) {
    VegetationStore store;
    LoadInstances(store, 0, 0, {MakeInstance(0, 10, 10, 2.0f, VegBlocksLineOfSight)});
    EXPECT_TRUE(SegmentBlockedByVegetation(store, 0, 0, 20, 20));    // passes through (10,10)
    EXPECT_FALSE(SegmentBlockedByVegetation(store, 0, 0, 20, -20));  // misses
}

TEST(VegetationQuery, NonBlockerDoesNotBlock) {
    VegetationStore store;
    LoadInstances(store, 0, 0, {MakeInstance(0, 10, 10, 5.0f, VegNone)});  // no LOS flag
    EXPECT_FALSE(SegmentBlockedByVegetation(store, 0, 0, 20, 20));
}

TEST(VegetationQuery, RaycastHitsNearestBlocker) {
    VegetationStore store;
    LoadInstances(
        store, 0, 0,
        {MakeInstance(0, 10, 0, 1.0f, VegBlocksLineOfSight), MakeInstance(1, 5, 0, 1.0f, VegBlocksLineOfSight)});
    VegetationWorldQuery q(&store);
    const float o[3] = {0, 0, 0};
    const float d[3] = {1, 0, 0};
    const gameapi::RaycastResult r = q.Raycast(o, d, 20.0f);
    EXPECT_EQ(r.hit, 1u);
    EXPECT_NEAR(r.distance, 4.0f, 0.01f);  // nearest blocker at x=5, r=1 -> hit at x=4
    EXPECT_NEAR(r.point.x, 4.0f, 0.01f);
}

TEST(VegetationQuery, RaycastMissesAndRespectsMaxDistance) {
    VegetationStore store;
    LoadInstances(store, 0, 0, {MakeInstance(0, 10, 0, 1.0f, VegBlocksLineOfSight)});
    VegetationWorldQuery q(&store);
    const float o[3] = {0, 0, 0};
    const float dz[3] = {0, 0, 1};
    EXPECT_EQ(q.Raycast(o, dz, 20.0f).hit, 0u);  // blocker not along +z
    const float dx[3] = {1, 0, 0};
    EXPECT_EQ(q.Raycast(o, dx, 5.0f).hit, 0u);  // blocker at ~9, beyond maxDistance 5
}

TEST(VegetationQuery, RaycastComposesWithFallback) {
    VegetationStore store;
    LoadInstances(store, 0, 0, {MakeInstance(0, 10, 0, 1.0f, VegBlocksLineOfSight)});  // veg hit ~9
    StubQuery fb;
    fb.result.hit = 1;
    fb.result.distance = 3.0f;
    fb.result.point = {3, 0, 0};
    VegetationWorldQuery q(&store, &fb);
    const float o[3] = {0, 0, 0};
    const float dx[3] = {1, 0, 0};

    gameapi::RaycastResult r = q.Raycast(o, dx, 20.0f);
    EXPECT_EQ(r.hit, 1u);
    EXPECT_NEAR(r.distance, 3.0f, 0.01f);  // fallback (3) nearer than veg (9)

    fb.result.distance = 15.0f;  // now veg is nearer
    r = q.Raycast(o, dx, 20.0f);
    EXPECT_NEAR(r.distance, 9.0f, 0.01f);  // veg blocker at x=10, r=1 -> 9
}

TEST(VegetationQuery, DestroyRemovesAndEmitsEvent) {
    VegetationStore store;
    LoadInstances(store, 2, 3,
                  {MakeInstance(0, 7, 8, 1.0f, static_cast<uint16_t>(VegBlocksLineOfSight | VegDestructible), 77)});
    const VegetationKey key{2, 3, 0};
    VegetationDestroyedEvent ev;
    ASSERT_TRUE(DestroyVegetation(store, key, ev));
    EXPECT_EQ(ev.cellX, 2);
    EXPECT_EQ(ev.cellZ, 3);
    EXPECT_EQ(ev.instanceId, 0u);
    EXPECT_EQ(ev.visual, 77u);
    EXPECT_NEAR(ev.position[0], 7.0f, 1e-4f);
    EXPECT_NEAR(ev.position[2], 8.0f, 1e-4f);
    EXPECT_EQ(store.Find(key), nullptr);
    EXPECT_FALSE(DestroyVegetation(store, key, ev));  // already gone
}

TEST(VegetationQuery, DestroyRejectsNonDestructible) {
    VegetationStore store;
    LoadInstances(store, 0, 0, {MakeInstance(0, 1, 1, 1.0f, VegBlocksLineOfSight)});  // not destructible
    VegetationDestroyedEvent ev;
    EXPECT_FALSE(DestroyVegetation(store, VegetationKey{0, 0, 0}, ev));
}
