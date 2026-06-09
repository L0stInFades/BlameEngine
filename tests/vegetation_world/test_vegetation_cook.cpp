#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "next/streaming/layered_cell_file.h"
#include "next/vegetation/vegetation_builder.h"
#include "next/vegetation/vegetation_cell.h"
#include "next/vegetation/vegetation_scatter.h"
#include "next/vegetation_world/vegetation_cook.h"

using namespace Next::vegetation;
using Next::Streaming::CellFileCompression;
using Next::Streaming::CellLayer;
using Next::Streaming::ExtractLayer;

namespace {

constexpr float kCellSize = 64.0f;

VegetationDef MakeForest(uint64_t seed = 0xC0FFEEu) {
    VegetationBuilder b("temperate-forest");
    b.WithMasterSeed(seed).WithMaxInstancesPerCell(100000);
    b.AddSpecies(101);
    b.WithDensity(0.02f).WithSpacing(2.0f).WithLogicalRadius(1.5f).BlocksLineOfSight();
    b.AddSpecies(202);
    b.WithDensity(0.05f).WithSpacing(1.0f);
    return b.Take();
}

bool SameInstances(const std::vector<VegetationInstance>& a, const std::vector<VegetationInstance>& b) {
    if (a.size() != b.size()) {
        return false;
    }
    if (a.empty()) {
        return true;
    }
    return std::memcmp(a.data(), b.data(), a.size() * sizeof(VegetationInstance)) == 0;
}

}  // namespace

TEST(VegetationCook, FailsClosedOnBadDef) {
    VegetationDef bad;  // empty id, no species
    FlatTerrainSampler terrain;
    const CookResult r = CookVegetationCell(bad, terrain, 0, 0, kCellSize);
    EXPECT_FALSE(r.ok);
    EXPECT_FALSE(r.report.Ok());
    EXPECT_TRUE(r.bytes.empty());
}

TEST(VegetationCook, ProducesLayeredChunkMatchingScatter) {
    const VegetationDef def = MakeForest();
    FlatTerrainSampler terrain;
    const CookResult r = CookVegetationCell(def, terrain, 2, -3, kCellSize);
    ASSERT_TRUE(r.ok);
    ASSERT_FALSE(r.bytes.empty());

    std::vector<uint8_t> vegBlob;
    ASSERT_TRUE(ExtractLayer(r.bytes.data(), r.bytes.size(), CellLayer::Vegetation, vegBlob));
    VegetationCellData parsed;
    ASSERT_TRUE(UnpackCell(vegBlob.data(), vegBlob.size(), parsed));

    const std::vector<VegetationInstance> direct = ScatterCell(def, terrain, 2, -3, kCellSize);
    EXPECT_EQ(r.instanceCount, direct.size());
    EXPECT_TRUE(SameInstances(parsed.instances, direct));
}

TEST(VegetationCook, DeterministicGoldenBytes) {
    const VegetationDef def = MakeForest();
    FlatTerrainSampler terrain;
    const CookResult a = CookVegetationCell(def, terrain, 1, 1, kCellSize);
    const CookResult b = CookVegetationCell(def, terrain, 1, 1, kCellSize);
    ASSERT_TRUE(a.ok);
    ASSERT_TRUE(b.ok);
    EXPECT_EQ(a.bytes, b.bytes);  // stable golden output
}

TEST(VegetationCook, CompressedRoundTrips) {
    const VegetationDef def = MakeForest();
    FlatTerrainSampler terrain;
    for (CellFileCompression codec : {CellFileCompression::Zstd, CellFileCompression::LZ4}) {
        const CookResult r = CookVegetationCell(def, terrain, 0, 0, kCellSize, codec);
        ASSERT_TRUE(r.ok);
        std::vector<uint8_t> vegBlob;
        ASSERT_TRUE(ExtractLayer(r.bytes.data(), r.bytes.size(), CellLayer::Vegetation, vegBlob));
        VegetationCellData parsed;
        ASSERT_TRUE(UnpackCell(vegBlob.data(), vegBlob.size(), parsed));
        EXPECT_EQ(parsed.instances.size(), r.instanceCount);
    }
}

TEST(VegetationCook, ParsesTextDefAndCooks) {
    const char* text =
        "# sample def\n"
        "id temperate-forest\n"
        "name Temperate Forest\n"
        "seed 12345\n"
        "maxInstancesPerCell 50000\n"
        "species 101\n"
        "  density 0.02\n"
        "  spacing 2.0\n"
        "  slope 0 35\n"
        "  scale 0.8 1.4\n"
        "  radius 1.5\n"
        "  flags blocksLOS,alignToSlope\n"
        "species 202\n"
        "  density 0.05\n"
        "  spacing 1.0\n";

    VegetationDef def;
    std::string err;
    ASSERT_TRUE(ParseVegetationDefText(text, def, err)) << err;
    EXPECT_EQ(def.id, "temperate-forest");
    EXPECT_EQ(def.masterSeed, 12345u);
    EXPECT_EQ(def.maxInstancesPerCell, 50000u);
    ASSERT_EQ(def.species.size(), 2u);
    EXPECT_EQ(def.species[0].visual, 101u);
    EXPECT_FLOAT_EQ(def.species[0].densityPerSqMeter, 0.02f);
    EXPECT_FLOAT_EQ(def.species[0].logicalRadius, 1.5f);
    EXPECT_TRUE((def.species[0].flags & VegBlocksLineOfSight) != 0);
    EXPECT_TRUE((def.species[0].flags & VegAlignToSlope) != 0);
    EXPECT_EQ(def.species[1].visual, 202u);

    FlatTerrainSampler terrain;
    const CookResult r = CookVegetationCell(def, terrain, 0, 0, kCellSize);
    EXPECT_TRUE(r.ok);
}

TEST(VegetationCook, ParseRejectsGarbage) {
    VegetationDef def;
    std::string err;
    EXPECT_FALSE(ParseVegetationDefText("density 0.5\n", def, err));  // key before any species
    EXPECT_FALSE(err.empty());
    EXPECT_FALSE(ParseVegetationDefText("boguskey 1\n", def, err));
}
