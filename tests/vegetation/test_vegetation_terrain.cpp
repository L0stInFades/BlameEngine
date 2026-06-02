// Vegetation on REAL terrain (ADR-0014). The other tests use FlatTerrainSampler; here we scatter over
// a heightmap with an actual slope and prove the filters behave on varying ground: instances sit on the
// surface, altitude bands are honored, and a too-steep slope is excluded.

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "next/vegetation/heightmap_terrain.h"
#include "next/vegetation/vegetation_builder.h"
#include "next/vegetation/vegetation_scatter.h"

using namespace Next::vegetation;

namespace {

constexpr float kCellSize = 64.0f;

// A uniform ramp: height = 0.5 * worldX  =>  a constant ~26.57-degree slope. The heightmap extends 16m
// past the cell on every side (origin -16, covers -16..80) so the cell [0,64] is fully INTERIOR — the
// central-difference gradient is a clean 0.5 there, with no edge-clamping artifact.
HeightmapTerrainSampler RampTerrain() {
    const int32_t w = 13;
    const int32_t h = 13;
    const float spacing = 8.0f;
    const float originX = -16.0f;
    const float originZ = -16.0f;
    std::vector<float> heights(static_cast<size_t>(w) * h);
    for (int32_t z = 0; z < h; ++z) {
        for (int32_t x = 0; x < w; ++x) {
            const float worldX = originX + (static_cast<float>(x) * spacing);
            heights[(static_cast<size_t>(z) * w) + x] = 0.5f * worldX;  // height = 0.5 * worldX
        }
    }
    return HeightmapTerrainSampler(w, h, spacing, originX, originZ, std::move(heights));
}

}  // namespace

TEST(VegetationTerrain, InstancesSitOnTheSurface) {
    VegetationBuilder b("on-surface");
    b.WithMasterSeed(1);
    b.AddSpecies(1);
    b.WithDensity(0.05f).WithSpacing(0.0f).WithSlopeRange(0.0f, 90.0f);  // accept the whole ramp
    const VegetationDef def = b.Take();

    const HeightmapTerrainSampler terrain = RampTerrain();
    const std::vector<VegetationInstance> all = ScatterCell(def, terrain, 0, 0, kCellSize);
    ASSERT_FALSE(all.empty());
    for (const VegetationInstance& inst : all) {
        const float expected = terrain.SampleAt(inst.position[0], inst.position[2]).height;
        EXPECT_NEAR(inst.position[1], expected, 1e-3f);                 // y is the terrain height, not 0
        EXPECT_NEAR(inst.position[1], 0.5f * inst.position[0], 1e-2f);  // == the ramp
    }
}

TEST(VegetationTerrain, AltitudeBandHonoredOnSlope) {
    VegetationBuilder b("altitude-band");
    b.WithMasterSeed(2);
    b.AddSpecies(1);
    b.WithDensity(0.1f).WithSpacing(0.0f).WithSlopeRange(0.0f, 90.0f).WithAltitudeRange(20.0f, 28.0f);
    const VegetationDef def = b.Take();

    const HeightmapTerrainSampler terrain = RampTerrain();
    const std::vector<VegetationInstance> all = ScatterCell(def, terrain, 0, 0, kCellSize);
    ASSERT_FALSE(all.empty());
    for (const VegetationInstance& inst : all) {
        EXPECT_GE(inst.position[1], 20.0f);
        EXPECT_LE(inst.position[1], 28.0f);
        // height = 0.5*x  =>  x in [40, 56]
        EXPECT_GE(inst.position[0], 39.0f);
        EXPECT_LE(inst.position[0], 57.0f);
    }
}

TEST(VegetationTerrain, SteepSlopeExcluded) {
    // The ramp is a constant ~26.57-degree slope everywhere.
    const HeightmapTerrainSampler terrain = RampTerrain();

    VegetationBuilder tooStrict("steep-reject");
    tooStrict.WithMasterSeed(3);
    tooStrict.AddSpecies(1);
    tooStrict.WithDensity(0.1f).WithSpacing(0.0f).WithSlopeRange(0.0f, 20.0f);  // 20 < 26.57 -> reject all
    EXPECT_TRUE(ScatterCell(tooStrict.Take(), terrain, 0, 0, kCellSize).empty());

    VegetationBuilder lenient("steep-accept");
    lenient.WithMasterSeed(3);
    lenient.AddSpecies(1);
    lenient.WithDensity(0.1f).WithSpacing(0.0f).WithSlopeRange(0.0f, 40.0f);  // 40 > 26.57 -> accept
    EXPECT_FALSE(ScatterCell(lenient.Take(), terrain, 0, 0, kCellSize).empty());
}
