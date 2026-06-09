// Water gameplay query tests (ADR-0015): WaterWorldQuery folds the water surface into the Game API
// Sense raycast (composing with a fallback, nearer hit wins), and the gameplay helpers answer height /
// submersion / stealth (breaks-sight) / conductivity (hacking) over the store.

#include <gtest/gtest.h>

#include <vector>

#include "next/water/water_cell.h"
#include "next/water/water_def.h"
#include "next/water/water_surface.h"
#include "next/water_world/water_query.h"
#include "next/water_world/water_store.h"

using namespace Next::water;
namespace gameapi = Next::gameapi;

namespace {

// A fixed-hit fallback query, to prove the nearer hit wins when composing.
class StubQuery final : public gameapi::IWorldQuery {
public:
    explicit StubQuery(float distance) : distance_(distance) {}
    gameapi::RaycastResult Raycast(const float[3], const float[3], float) override {
        gameapi::RaycastResult r{};
        r.hit = 1;
        r.distance = distance_;
        r.entity = 99;
        return r;
    }

private:
    float distance_;
};

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

}  // namespace

// W10: WaterWorldQuery also answers the Game API's IWaterQuery::QueryWater (GetWaterState) — the same
// authoritative state, surfaced to player code: submerged?, depth, surface height, current, and flags.
TEST(WaterQuery, QueryWaterReportsSubmersionFlowAndFlags) {
    WaterStore store;
    // A conductive, current-bearing pool (a flooded electrified canal): flags drive gameplay hooks.
    WaterBodyInstance b;
    b.bodyId = 1;
    b.type = static_cast<uint8_t>(WaterType::River);
    b.boundsMin[0] = -50.0f;
    b.boundsMin[1] = -10.0f;
    b.boundsMin[2] = -50.0f;
    b.boundsMax[0] = 50.0f;
    b.boundsMax[1] = 0.0f;
    b.boundsMax[2] = 50.0f;
    b.surfaceHeight = 0.0f;
    b.density = 1000.0f;
    b.flowVelocity[0] = 2.0f;
    b.flags = static_cast<uint16_t>(WaterBuoyant | WaterCurrent | WaterConductive);
    const std::vector<uint8_t> blob = PackCell(0, 0, 4096.0f, {b});
    store.LoadCell(0, 0, blob.data(), blob.size());

    WaterWorldQuery wq(&store);  // null clock -> t=0

    // A point 2 m under the surface: submerged, depth 2, flow + conductive flag visible.
    const float under[3] = {0.0f, -2.0f, 0.0f};
    const gameapi::WaterStateResult sub = wq.QueryWater(under);
    EXPECT_EQ(sub.inWater, 1u);
    EXPECT_EQ(sub.submerged, 1u);
    EXPECT_NEAR(sub.surfaceHeight, 0.0f, 1e-4f);
    EXPECT_NEAR(sub.submersionDepth, 2.0f, 1e-4f);
    EXPECT_FLOAT_EQ(sub.flowVelocity.x, 2.0f);
    EXPECT_NE(sub.flags & WaterConductive, 0u);

    // A point above the surface, still over the body: water present but NOT submerged.
    const float above[3] = {0.0f, 3.0f, 0.0f};
    const gameapi::WaterStateResult over = wq.QueryWater(above);
    EXPECT_EQ(over.inWater, 1u);
    EXPECT_EQ(over.submerged, 0u);

    // A point outside the body's XZ: no water at all (zeroed result).
    const float dry[3] = {1000.0f, -2.0f, 0.0f};
    const gameapi::WaterStateResult none = wq.QueryWater(dry);
    EXPECT_EQ(none.inWater, 0u);
    EXPECT_EQ(none.submerged, 0u);

    // BELOW the body floor (y=-20, floor=-10) but within the XZ footprint: a body governs the XZ, so
    // inWater=1, but the point is NOT submerged. The ABI contract says depth>0 IFF submerged — so the
    // reported depth must be <= 0 here (not the raw surfaceHeight - y = +20 the store computes).
    const float belowFloor[3] = {0.0f, -20.0f, 0.0f};
    const gameapi::WaterStateResult below = wq.QueryWater(belowFloor);
    EXPECT_EQ(below.inWater, 1u);
    EXPECT_EQ(below.submerged, 0u);
    EXPECT_LE(below.submersionDepth, 0.0f) << "depth must be <=0 when not submerged (W10 contract)";
}

TEST(WaterQuery, RaycastHitsSurface) {
    WaterStore store;
    LoadPool(store, WaterBuoyant);
    WaterWorldQuery wq(&store);  // null clock -> still water (t=0)
    const float origin[3] = {0.0f, 5.0f, 0.0f};
    const float down[3] = {0.0f, -1.0f, 0.0f};
    const gameapi::RaycastResult hit = wq.Raycast(origin, down, 100.0f);
    EXPECT_EQ(hit.hit, 1u);
    EXPECT_NEAR(hit.distance, 5.0f, 1e-2f);  // surface at y=0, origin at y=5
    EXPECT_NEAR(hit.point.y, 0.0f, 1e-2f);

    const float up[3] = {0.0f, 1.0f, 0.0f};
    EXPECT_EQ(wq.Raycast(origin, up, 100.0f).hit, 0u);  // pointing away from the surface, no fallback
}

TEST(WaterQuery, ComposesFallbackNearerWins) {
    WaterStore store;
    LoadPool(store, WaterBuoyant);
    const float origin[3] = {0.0f, 5.0f, 0.0f};
    const float down[3] = {0.0f, -1.0f, 0.0f};

    StubQuery closer(2.0f);  // closer than the water surface (5.0)
    WaterWorldQuery wqA(&store, nullptr, &closer);
    EXPECT_NEAR(wqA.Raycast(origin, down, 100.0f).distance, 2.0f, 1e-3f);  // fallback wins

    StubQuery farther(20.0f);  // farther than the water surface
    WaterWorldQuery wqB(&store, nullptr, &farther);
    EXPECT_NEAR(wqB.Raycast(origin, down, 100.0f).distance, 5.0f, 1e-2f);  // water wins
}

TEST(WaterQuery, HeightAndSubmersionHelpers) {
    WaterStore store;
    LoadPool(store, WaterBuoyant);
    bool found = false;
    EXPECT_NEAR(WaterHeightAt(store, 1.0f, 1.0f, 0.0, found), 0.0f, 1e-4f);
    EXPECT_TRUE(found);
    WaterHeightAt(store, 1000.0f, 1000.0f, 0.0, found);
    EXPECT_FALSE(found);  // no water out there
    EXPECT_NEAR(SubmersionDepthAt(store, 1.0f, -3.0f, 1.0f, 0.0), 3.0f, 1e-4f);
    EXPECT_TRUE(IsSubmergedAt(store, 1.0f, -1.0f, 1.0f, 0.0));
    EXPECT_FALSE(IsSubmergedAt(store, 1.0f, 1.0f, 1.0f, 0.0));  // above surface
}

TEST(WaterQuery, StealthAndConductivityFlags) {
    {
        WaterStore store;
        LoadPool(store, static_cast<uint16_t>(WaterBuoyant | WaterBreaksSight));
        EXPECT_TRUE(IsHiddenBySubmersion(store, 0.0f, -2.0f, 0.0f, 0.0));  // submerged + breaks sight
        EXPECT_FALSE(IsHiddenBySubmersion(store, 0.0f, 2.0f, 0.0f, 0.0));  // above the surface
        EXPECT_FALSE(IsInConductiveWater(store, 0.0f, -2.0f, 0.0f, 0.0));  // not flagged conductive
    }
    {
        WaterStore store;
        LoadPool(store, static_cast<uint16_t>(WaterBuoyant | WaterConductive));
        EXPECT_TRUE(IsInConductiveWater(store, 0.0f, -2.0f, 0.0f, 0.0));    // submerged + conductive
        EXPECT_FALSE(IsHiddenBySubmersion(store, 0.0f, -2.0f, 0.0f, 0.0));  // not flagged breaks-sight
    }
}

TEST(WaterQuery, ReadsTimeFromSharedClock) {
    // A wavy body + a SimClock: the query must evaluate the surface at the CLOCK's time (the unified
    // time source W2 establishes), so the ray hits the height SampleHeightFast reports at that tick —
    // NOT the t=0 surface.
    WaterStore store;
    WaterBodyInstance b;
    b.bodyId = 1;
    b.type = static_cast<uint8_t>(WaterType::Ocean);
    b.boundsMin[0] = -1000.0f;
    b.boundsMin[1] = -50.0f;
    b.boundsMin[2] = -1000.0f;
    b.boundsMax[0] = 1000.0f;
    b.boundsMax[1] = 5.0f;
    b.boundsMax[2] = 1000.0f;
    b.surfaceHeight = 0.0f;
    b.density = 1000.0f;
    b.flags = WaterBuoyant;
    b.waveCount = 1;
    b.waves[0] = {0.8f, 12.0f, {1.0f, 0.0f}, 2.0f, 0.0f};  // steepness 0 -> height exact at (x,z)
    const std::vector<uint8_t> blob = PackCell(0, 0, 4096.0f, {b});
    ASSERT_TRUE(store.LoadCell(0, 0, blob.data(), blob.size()));

    Next::gameapi::SimClock clock;
    clock.seconds = 3.5;  // an arbitrary non-zero time
    WaterWorldQuery wq(&store, &clock);
    const float origin[3] = {7.0f, 20.0f, 0.0f};
    const float down[3] = {0.0f, -1.0f, 0.0f};
    const gameapi::RaycastResult hit = wq.Raycast(origin, down, 100.0f);
    ASSERT_EQ(hit.hit, 1u);
    const float atClock = SampleHeightFast(b, 7.0f, 0.0f, clock.seconds);
    const float atZero = SampleHeightFast(b, 7.0f, 0.0f, 0.0);
    EXPECT_NEAR(hit.point.y, atClock, 1e-2f);       // used the clock's time
    EXPECT_GT(std::fabs(atClock - atZero), 0.05f);  // and the wavy surface really moved by t=3.5 (sanity)
}
