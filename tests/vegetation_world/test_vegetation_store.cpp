#include <gtest/gtest.h>

#include <vector>

#include "next/vegetation/vegetation_builder.h"
#include "next/vegetation/vegetation_cell.h"
#include "next/vegetation/vegetation_scatter.h"
#include "next/vegetation_world/vegetation_store.h"

using namespace Next::vegetation;

namespace {

constexpr float kCellSize = 64.0f;

std::vector<uint8_t> CookBlob(const VegetationDef& def, int32_t x, int32_t z) {
    FlatTerrainSampler terrain;
    const std::vector<VegetationInstance> instances = ScatterCell(def, terrain, x, z, kCellSize);
    return PackCell(x, z, kCellSize, instances);
}

VegetationDef Forest() {  // destructible + blocks LOS
    VegetationBuilder b("store-forest");
    b.WithMasterSeed(3);
    b.AddSpecies(101);
    b.WithDensity(0.03f).WithSpacing(2.0f).WithLogicalRadius(1.0f).BlocksLineOfSight().Destructible();
    return b.Take();
}

VegetationDef PlainGrass() {  // neither destructible nor LOS-blocking
    VegetationBuilder b("grass");
    b.WithMasterSeed(9);
    b.AddSpecies(7);
    b.WithDensity(0.05f).WithSpacing(0.0f);
    return b.Take();
}

}  // namespace

TEST(VegetationStore, LoadIndexesInstances) {
    const VegetationDef def = Forest();
    const std::vector<uint8_t> blob = CookBlob(def, 0, 0);
    VegetationStore store;
    ASSERT_TRUE(store.LoadCell(0, 0, blob.data(), blob.size()));
    EXPECT_TRUE(store.IsCellLoaded(0, 0));

    FlatTerrainSampler terrain;
    const std::vector<VegetationInstance> direct = ScatterCell(def, terrain, 0, 0, kCellSize);
    EXPECT_EQ(store.LiveInstanceCount(), direct.size());
    EXPECT_EQ(store.LoadedCellCount(), 1u);
}

TEST(VegetationStore, LoadFailsClosedOnGarbage) {
    VegetationStore store;
    std::vector<uint8_t> junk(10, 0xABu);
    EXPECT_FALSE(store.LoadCell(0, 0, junk.data(), junk.size()));
    EXPECT_FALSE(store.IsCellLoaded(0, 0));
}

TEST(VegetationStore, QueryRadiusReturnsWithin) {
    const VegetationDef def = Forest();
    const std::vector<uint8_t> blob = CookBlob(def, 0, 0);
    VegetationStore store;
    ASSERT_TRUE(store.LoadCell(0, 0, blob.data(), blob.size()));

    const std::vector<VegetationKey> all = store.QueryRadius(32.0f, 32.0f, 10000.0f);
    EXPECT_EQ(all.size(), store.LiveInstanceCount());

    const float r = 10.0f;
    const std::vector<VegetationKey> near = store.QueryRadius(20.0f, 20.0f, r);
    for (const VegetationKey& k : near) {
        const VegetationInstance* p = store.Find(k);
        ASSERT_NE(p, nullptr);
        const float dx = p->position[0] - 20.0f;
        const float dz = p->position[2] - 20.0f;
        EXPECT_LE(dx * dx + dz * dz, r * r + 1e-3f);
    }
    EXPECT_LE(near.size(), all.size());
    EXPECT_TRUE(store.QueryRadius(20.0f, 20.0f, 0.0f).empty());  // non-positive radius
}

TEST(VegetationStore, QueryWithFlagsFiltersBlockers) {
    const std::vector<uint8_t> blob = CookBlob(PlainGrass(), 0, 0);
    VegetationStore store;
    ASSERT_TRUE(store.LoadCell(0, 0, blob.data(), blob.size()));

    EXPECT_GT(store.QueryRadius(32.0f, 32.0f, 10000.0f).size(), 0u);
    // No instance blocks LOS -> the filtered query is empty.
    EXPECT_TRUE(store.QueryRadiusWithFlags(32.0f, 32.0f, 10000.0f, VegBlocksLineOfSight).empty());
}

TEST(VegetationStore, RemoveDestructible) {
    const VegetationDef def = Forest();
    const std::vector<uint8_t> blob = CookBlob(def, 0, 0);
    VegetationStore store;
    ASSERT_TRUE(store.LoadCell(0, 0, blob.data(), blob.size()));

    const size_t before = store.LiveInstanceCount();
    ASSERT_GT(before, 0u);

    const VegetationKey key{0, 0, 0};
    ASSERT_NE(store.Find(key), nullptr);
    EXPECT_TRUE(store.Remove(key));
    EXPECT_TRUE(store.IsRemoved(key));
    EXPECT_EQ(store.Find(key), nullptr);
    EXPECT_EQ(store.LiveInstanceCount(), before - 1);
    EXPECT_FALSE(store.Remove(key));  // already removed

    for (const VegetationKey& k : store.QueryRadius(32.0f, 32.0f, 10000.0f)) {
        EXPECT_FALSE(k == key);  // excluded from queries
    }

    store.ClearRemovals();
    EXPECT_FALSE(store.IsRemoved(key));
    EXPECT_EQ(store.LiveInstanceCount(), before);
}

TEST(VegetationStore, RemoveRejectsNonDestructible) {
    const std::vector<uint8_t> blob = CookBlob(PlainGrass(), 0, 0);
    VegetationStore store;
    ASSERT_TRUE(store.LoadCell(0, 0, blob.data(), blob.size()));
    EXPECT_FALSE(store.Remove(VegetationKey{0, 0, 0}));       // not destructible
    EXPECT_FALSE(store.Remove(VegetationKey{0, 0, 999999}));  // out of range
    EXPECT_FALSE(store.Remove(VegetationKey{5, 5, 0}));       // cell not loaded
}

TEST(VegetationStore, UnloadRemovesInstances) {
    const VegetationDef def = Forest();
    const std::vector<uint8_t> blob = CookBlob(def, 1, 2);
    VegetationStore store;
    ASSERT_TRUE(store.LoadCell(1, 2, blob.data(), blob.size()));
    EXPECT_GT(store.LiveInstanceCount(), 0u);

    store.UnloadCell(1, 2);
    EXPECT_FALSE(store.IsCellLoaded(1, 2));
    EXPECT_EQ(store.LiveInstanceCount(), 0u);
    EXPECT_TRUE(store.QueryRadius(64.0f, 128.0f, 10000.0f).empty());
}

TEST(VegetationStore, QueryAcrossCellsIsDeterministic) {
    const VegetationDef def = Forest();
    VegetationStore store;
    for (int32_t x = 0; x < 2; ++x) {
        for (int32_t z = 0; z < 2; ++z) {
            const std::vector<uint8_t> blob = CookBlob(def, x, z);
            ASSERT_TRUE(store.LoadCell(x, z, blob.data(), blob.size()));
        }
    }
    const std::vector<VegetationKey> a = store.QueryRadius(32.0f, 32.0f, 100000.0f);
    const std::vector<VegetationKey> b = store.QueryRadius(32.0f, 32.0f, 100000.0f);
    ASSERT_EQ(a.size(), b.size());
    ASSERT_GT(a.size(), 0u);
    for (size_t i = 0; i < a.size(); ++i) {
        EXPECT_TRUE(a[i] == b[i]);  // stable order: ascending (cell, ordinal)
    }
}
