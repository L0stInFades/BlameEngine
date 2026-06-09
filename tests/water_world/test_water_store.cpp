// Runtime water store tests (ADR-0015): load/unload, de-dup of a body spanning many cells (refcount),
// broadphase candidate gathering, topmost-body resolution, point submersion, the Ocean "global" path
// (an unbounded sea never explodes the grid), and reload-replace.

#include <gtest/gtest.h>

#include <vector>

#include "next/water/water_cell.h"
#include "next/water/water_def.h"
#include "next/water_world/water_store.h"

using namespace Next::water;

namespace {

WaterBodyInstance MakeBody(uint32_t id, WaterType type, float minX, float minZ, float maxX, float maxZ,
                           float surfaceY) {
    WaterBodyInstance b;
    b.bodyId = id;
    b.type = static_cast<uint8_t>(type);
    b.boundsMin[0] = minX;
    b.boundsMin[1] = -10.0f;
    b.boundsMin[2] = minZ;
    b.boundsMax[0] = maxX;
    b.boundsMax[1] = surfaceY;
    b.boundsMax[2] = maxZ;
    b.surfaceHeight = surfaceY;
    b.density = 1000.0f;
    b.flags = WaterBuoyant;
    return b;
}

std::vector<uint8_t> Pack(int32_t cx, int32_t cz, const std::vector<WaterBodyInstance>& bs) {
    return PackCell(cx, cz, 64.0f, bs);
}

}  // namespace

TEST(WaterStore, LoadFindUnload) {
    WaterStore store;
    const std::vector<uint8_t> blob = Pack(0, 0, {MakeBody(1, WaterType::Pool, 0, 0, 32, 32, 0.0f)});
    ASSERT_TRUE(store.LoadCell(0, 0, blob.data(), blob.size()));
    EXPECT_EQ(store.BodyCount(), 1u);
    EXPECT_TRUE(store.IsCellLoaded(0, 0));
    EXPECT_NE(store.Find(1), nullptr);
    store.UnloadCell(0, 0);
    EXPECT_EQ(store.BodyCount(), 0u);
    EXPECT_EQ(store.Find(1), nullptr);
}

TEST(WaterStore, DedupAcrossCellsWithRefcount) {
    WaterStore store;
    // The SAME body (id 7) is recorded in two cells it spans.
    const WaterBodyInstance body = MakeBody(7, WaterType::River, 0, 0, 128, 16, 0.0f);
    const std::vector<uint8_t> a = Pack(0, 0, {body});
    const std::vector<uint8_t> b = Pack(1, 0, {body});
    ASSERT_TRUE(store.LoadCell(0, 0, a.data(), a.size()));
    ASSERT_TRUE(store.LoadCell(1, 0, b.data(), b.size()));
    EXPECT_EQ(store.BodyCount(), 1u);  // de-duplicated by bodyId
    store.UnloadCell(0, 0);
    EXPECT_EQ(store.BodyCount(), 1u);  // still referenced by cell (1,0)
    store.UnloadCell(1, 0);
    EXPECT_EQ(store.BodyCount(), 0u);  // last reference gone
}

TEST(WaterStore, BodyAtPicksTopmost) {
    WaterStore store;
    WaterBodyInstance low = MakeBody(1, WaterType::Pool, 0, 0, 32, 32, 0.0f);
    WaterBodyInstance high = MakeBody(2, WaterType::Pool, 0, 0, 32, 32, 5.0f);
    const std::vector<uint8_t> blob = Pack(0, 0, {low, high});
    ASSERT_TRUE(store.LoadCell(0, 0, blob.data(), blob.size()));
    const WaterBodyInstance* at = store.BodyAt(10.0f, 10.0f, 0.0);
    ASSERT_NE(at, nullptr);
    EXPECT_EQ(at->bodyId, 2u);  // higher surface wins
}

TEST(WaterStore, SampleSubmersion) {
    WaterStore store;
    const std::vector<uint8_t> blob = Pack(0, 0, {MakeBody(1, WaterType::Pool, 0, 0, 32, 32, 0.0f)});
    ASSERT_TRUE(store.LoadCell(0, 0, blob.data(), blob.size()));
    WaterSample s;
    ASSERT_TRUE(store.SampleWaterAt(10.0f, -2.0f, 10.0f, 0.0, s));
    EXPECT_TRUE(s.submerged);
    EXPECT_NEAR(s.submersionDepth, 2.0f, 1e-4f);
    EXPECT_TRUE(store.SampleWaterAt(10.0f, 3.0f, 10.0f, 0.0, s));
    EXPECT_FALSE(s.submerged);                                          // above the surface
    EXPECT_FALSE(store.SampleWaterAt(1000.0f, 0.0f, 1000.0f, 0.0, s));  // outside any body
}

TEST(WaterStore, BroadphaseOnlyReturnsOverlapping) {
    WaterStore store;
    const std::vector<uint8_t> blob = Pack(1, 1, {MakeBody(1, WaterType::Pool, 100, 100, 116, 116, 0.0f)});
    ASSERT_TRUE(store.LoadCell(1, 1, blob.data(), blob.size()));
    EXPECT_TRUE(store.BodiesOverlappingAabb(0, 0, 1, 1).empty());
    EXPECT_EQ(store.BodiesOverlappingAabb(100, 100, 116, 116).size(), 1u);
}

TEST(WaterStore, OceanIsGlobalCandidateEverywhere) {
    WaterStore store;
    const std::vector<uint8_t> blob =
        Pack(0, 0, {MakeBody(1, WaterType::Ocean, -100000, -100000, 100000, 100000, 0.0f)});
    ASSERT_TRUE(store.LoadCell(0, 0, blob.data(), blob.size()));
    // Far from cell (0,0), the global ocean is still found (it is not gridded).
    EXPECT_NE(store.BodyAt(50000.0f, -50000.0f, 0.0), nullptr);
}

TEST(WaterStore, ReloadReplaces) {
    WaterStore store;
    const std::vector<uint8_t> v0 = Pack(0, 0, {MakeBody(1, WaterType::Pool, 0, 0, 32, 32, 0.0f)});
    ASSERT_TRUE(store.LoadCell(0, 0, v0.data(), v0.size()));
    const std::vector<uint8_t> v1 = Pack(0, 0, {MakeBody(1, WaterType::Pool, 0, 0, 32, 32, 5.0f)});
    ASSERT_TRUE(store.LoadCell(0, 0, v1.data(), v1.size()));  // reload same cell
    EXPECT_EQ(store.BodyCount(), 1u);
    const WaterBodyInstance* b = store.Find(1);
    ASSERT_NE(b, nullptr);
    EXPECT_FLOAT_EQ(b->surfaceHeight, 5.0f);  // reflects the reloaded value
}

TEST(WaterStore, HugeSpanBodyIsSafeAndGlobal) {
    // A non-Ocean body with an absurd-but-finite span must NOT trigger a float->int overflow in
    // IsLargeBody / BucketCoord (UB); it is treated as a global candidate. The validator rejects such
    // content, but the store must be robust to it regardless (tests/tools can bypass the cook).
    WaterStore store;
    const WaterBodyInstance b = MakeBody(1, WaterType::Pool, -1.0e20f, -1.0e20f, 1.0e20f, 1.0e20f, 0.0f);
    const std::vector<uint8_t> blob = Pack(0, 0, {b});
    ASSERT_TRUE(store.LoadCell(0, 0, blob.data(), blob.size()));
    EXPECT_EQ(store.BodyCount(), 1u);
    EXPECT_NE(store.BodyAt(123.0f, 456.0f, 0.0), nullptr);  // found via the global path, no crash
    store.UnloadCell(0, 0);
    EXPECT_EQ(store.BodyCount(), 0u);
}
