#include <gtest/gtest.h>

#include <vector>

#include "next/vegetation/vegetation_builder.h"
#include "next/vegetation/vegetation_cell.h"
#include "next/vegetation/vegetation_scatter.h"
#include "next/vegetation_world/vegetation_query.h"
#include "next/vegetation_world/vegetation_view.h"

using namespace Next::vegetation;
namespace boundary = Next::boundary;

namespace {

constexpr float kCellSize = 64.0f;

VegetationDef TwoSpecies() {
    VegetationBuilder b("view-forest");
    b.WithMasterSeed(5);
    b.AddSpecies(101);
    b.WithDensity(0.03f).WithSpacing(2.0f);
    b.AddSpecies(202);
    b.WithDensity(0.04f).WithSpacing(1.0f);
    return b.Take();
}

std::vector<uint8_t> CookBlob(const VegetationDef& def, int32_t x, int32_t z, size_t* outV101 = nullptr,
                              size_t* outV202 = nullptr) {
    FlatTerrainSampler terrain;
    const std::vector<VegetationInstance> inst = ScatterCell(def, terrain, x, z, kCellSize);
    if (outV101 != nullptr || outV202 != nullptr) {
        size_t a = 0;
        size_t b = 0;
        for (const VegetationInstance& i : inst) {
            if (i.visual == 101) {
                ++a;
            } else if (i.visual == 202) {
                ++b;
            }
        }
        if (outV101 != nullptr) {
            *outV101 = a;
        }
        if (outV202 != nullptr) {
            *outV202 = b;
        }
    }
    return PackCell(x, z, kCellSize, inst);
}

}  // namespace

TEST(VegetationView, ConsumerBucketsByVisual) {
    const VegetationDef def = TwoSpecies();
    size_t n101 = 0;
    size_t n202 = 0;
    const std::vector<uint8_t> blob = CookBlob(def, 0, 0, &n101, &n202);

    MockVegetationConsumer consumer;
    ASSERT_TRUE(consumer.OnCellLoaded(0, 0, blob.data(), blob.size()));
    EXPECT_EQ(consumer.LoadedCellCount(), 1u);
    EXPECT_EQ(consumer.TotalInstanceCount(), n101 + n202);
    EXPECT_EQ(consumer.InstanceCountForVisual(101), n101);  // HISM bucket for mesh 101
    EXPECT_EQ(consumer.InstanceCountForVisual(202), n202);  // HISM bucket for mesh 202
    EXPECT_EQ(consumer.VisualBucketCount(), 2u);
}

TEST(VegetationView, FailsClosedOnGarbage) {
    MockVegetationConsumer consumer;
    std::vector<uint8_t> junk(8, 0xABu);
    EXPECT_FALSE(consumer.OnCellLoaded(0, 0, junk.data(), junk.size()));
    EXPECT_EQ(consumer.LoadedCellCount(), 0u);
}

TEST(VegetationView, UnloadDropsCell) {
    const std::vector<uint8_t> blob = CookBlob(TwoSpecies(), 1, 2);
    MockVegetationConsumer consumer;
    ASSERT_TRUE(consumer.OnCellLoaded(1, 2, blob.data(), blob.size()));
    EXPECT_GT(consumer.TotalInstanceCount(), 0u);

    consumer.OnCellUnloaded(1, 2);
    EXPECT_EQ(consumer.LoadedCellCount(), 0u);
    EXPECT_EQ(consumer.TotalInstanceCount(), 0u);
}

TEST(VegetationView, DestroyDecrementsBucket) {
    const VegetationDef def = TwoSpecies();
    size_t n101 = 0;
    const std::vector<uint8_t> blob = CookBlob(def, 0, 0, &n101, nullptr);

    MockVegetationConsumer consumer;
    ASSERT_TRUE(consumer.OnCellLoaded(0, 0, blob.data(), blob.size()));
    const size_t before = consumer.TotalInstanceCount();

    // Find an instance with visual 101 -> its ordinal.
    VegetationCellData parsed;
    ASSERT_TRUE(UnpackCell(blob.data(), blob.size(), parsed));
    uint32_t target = 0;
    bool found = false;
    for (const VegetationInstance& i : parsed.instances) {
        if (i.visual == 101) {
            target = i.instanceId;
            found = true;
            break;
        }
    }
    ASSERT_TRUE(found);

    EXPECT_TRUE(consumer.OnInstanceDestroyed(0, 0, target));
    EXPECT_EQ(consumer.TotalInstanceCount(), before - 1);
    EXPECT_EQ(consumer.InstanceCountForVisual(101), n101 - 1);
    EXPECT_FALSE(consumer.OnInstanceDestroyed(0, 0, target));  // already gone
}

TEST(VegetationView, DestroyEventToBoundary) {
    VegetationDestroyedEvent ev;
    ev.cellX = 2;
    ev.cellZ = 3;
    ev.instanceId = 5;
    ev.visual = 77;
    ev.position[0] = 1.0f;
    ev.position[1] = 2.0f;
    ev.position[2] = 3.0f;

    const boundary::GameEvent e = ToBoundaryEvent(ev);
    EXPECT_EQ(e.type, kVegEventInstanceDestroyed);
    EXPECT_EQ(e.subject, 77u);  // the visual that fell (cosmetic cue), carries no authority
    EXPECT_FLOAT_EQ(e.params[0], 1.0f);
    EXPECT_FLOAT_EQ(e.params[1], 2.0f);
    EXPECT_FLOAT_EQ(e.params[2], 3.0f);
}
