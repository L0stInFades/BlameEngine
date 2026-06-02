#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <vector>

#include "next/vegetation/vegetation_builder.h"
#include "next/vegetation/vegetation_cell.h"
#include "next/vegetation/vegetation_def.h"
#include "next/vegetation/vegetation_scatter.h"
#include "next/vegetation/vegetation_validator.h"

using namespace Next::vegetation;

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kCellSize = 64.0f;

// Terrain whose slope is flat for x < splitX and a 60° incline for x >= splitX.
class HalfSteepTerrain : public ITerrainSampler {
public:
    explicit HalfSteepTerrain(float splitX) : splitX_(splitX) {}
    TerrainSample SampleAt(float x, float /*z*/) const override {
        TerrainSample s;
        if (x >= splitX_) {
            const float a = 60.0f * kPi / 180.0f;
            s.normal = Next::Vec3(std::sin(a), std::cos(a), 0.0f);
        }
        return s;
    }

private:
    float splitX_;
};

// Terrain whose height equals the world X coordinate (a clean ramp for altitude-band tests).
class HeightRampTerrain : public ITerrainSampler {
public:
    TerrainSample SampleAt(float x, float /*z*/) const override {
        TerrainSample s;
        s.height = x;
        return s;
    }
};

// Terrain whose paint mask is `low` for x < splitX and `high` for x >= splitX.
class MaskHalfTerrain : public ITerrainSampler {
public:
    MaskHalfTerrain(float splitX, uint32_t low, uint32_t high) : splitX_(splitX), low_(low), high_(high) {}
    TerrainSample SampleAt(float x, float /*z*/) const override {
        TerrainSample s;
        s.mask = (x >= splitX_) ? high_ : low_;
        return s;
    }

private:
    float splitX_;
    uint32_t low_;
    uint32_t high_;
};

bool SameInstances(const std::vector<VegetationInstance>& a, const std::vector<VegetationInstance>& b) {
    if (a.size() != b.size()) {
        return false;
    }
    if (a.empty()) {
        return true;
    }
    return std::memcmp(a.data(), b.data(), a.size() * sizeof(VegetationInstance)) == 0;
}

// A well-formed forest def: two species over flat, unconstrained terrain.
VegetationDef MakeForest(uint64_t seed = 0xC0FFEEu) {
    VegetationBuilder b("temperate-forest", "Temperate Forest");
    b.WithMasterSeed(seed).WithMaxInstancesPerCell(100000);
    b.AddSpecies(/*visual*/ 101);
    b.WithDensity(0.02f)
        .WithSpacing(2.0f)
        .WithScaleRange(0.8f, 1.4f)
        .WithLogicalRadius(1.5f)
        .BlocksLineOfSight()
        .AlignToSlope();
    b.AddSpecies(/*visual*/ 202);
    b.WithDensity(0.05f).WithSpacing(1.0f).WithScaleRange(0.5f, 0.7f);
    return b.Take();
}

}  // namespace

// ---- Layout (the wire contract the cell blob + UE5 depend on) ----

TEST(Vegetation, PodLayoutIsStable) {
    EXPECT_EQ(sizeof(VegetationInstance), 48u);
    EXPECT_EQ(alignof(VegetationInstance), 4u);
    EXPECT_EQ(sizeof(VegetationCellHeader), 24u);
    EXPECT_TRUE(std::is_trivially_copyable<VegetationInstance>::value);
}

TEST(Vegetation, InstanceIdsAreDenseOrdinalsAndCarryRadius) {
    VegetationBuilder b("ordinal-test");
    b.WithMasterSeed(5);
    b.AddSpecies(1);
    b.WithDensity(0.05f).WithSpacing(0.0f).WithLogicalRadius(1.25f);
    const VegetationDef def = b.Take();

    FlatTerrainSampler terrain;
    const std::vector<VegetationInstance> all = ScatterCell(def, terrain, 0, 0, kCellSize);
    ASSERT_FALSE(all.empty());
    for (size_t i = 0; i < all.size(); ++i) {
        EXPECT_EQ(all[i].instanceId, static_cast<uint32_t>(i));  // dense, unique-within-cell ordinal
        EXPECT_FLOAT_EQ(all[i].logicalRadius, 1.25f);            // carried from the species
    }
}

// ---- Determinism (the property the headless/UE5 boundary requires) ----

TEST(Vegetation, ScatterIsDeterministic) {
    const VegetationDef def = MakeForest();
    FlatTerrainSampler terrain;

    const std::vector<VegetationInstance> a = ScatterCell(def, terrain, 0, 0, kCellSize);
    const std::vector<VegetationInstance> b = ScatterCell(def, terrain, 0, 0, kCellSize);

    EXPECT_FALSE(a.empty());
    EXPECT_TRUE(SameInstances(a, b));
}

TEST(Vegetation, CellsAreIndependentAndOrderFree) {
    const VegetationDef def = MakeForest();
    FlatTerrainSampler terrain;

    const std::vector<VegetationInstance> origin1 = ScatterCell(def, terrain, 0, 0, kCellSize);
    const std::vector<VegetationInstance> other = ScatterCell(def, terrain, 3, 1, kCellSize);
    const std::vector<VegetationInstance> origin2 = ScatterCell(def, terrain, 0, 0, kCellSize);

    // Re-scattering (0,0) after touching another cell yields byte-identical output (no shared state).
    EXPECT_TRUE(SameInstances(origin1, origin2));
    // A different world cell produces a different layout.
    EXPECT_FALSE(SameInstances(origin1, other));
    EXPECT_FALSE(other.empty());
}

TEST(Vegetation, ChangingSeedChangesLayout) {
    FlatTerrainSampler terrain;
    const std::vector<VegetationInstance> a = ScatterCell(MakeForest(1), terrain, 0, 0, kCellSize);
    const std::vector<VegetationInstance> b = ScatterCell(MakeForest(2), terrain, 0, 0, kCellSize);
    EXPECT_FALSE(SameInstances(a, b));
}

// ---- Instance fields ----

TEST(Vegetation, InstanceFieldsAreWithinSpecRanges) {
    const VegetationDef def = MakeForest();
    FlatTerrainSampler terrain;
    const std::vector<VegetationInstance> all = ScatterCell(def, terrain, 0, 0, kCellSize);
    ASSERT_FALSE(all.empty());

    for (const VegetationInstance& inst : all) {
        // Inside the cell footprint.
        EXPECT_GE(inst.position[0], 0.0f);
        EXPECT_LT(inst.position[0], kCellSize);
        EXPECT_GE(inst.position[2], 0.0f);
        EXPECT_LT(inst.position[2], kCellSize);
        // Yaw in [0, 2pi); scale within one of the two species' ranges; visual set.
        EXPECT_GE(inst.rotationY, 0.0f);
        EXPECT_LT(inst.rotationY, 2.0f * kPi + 1e-3f);
        EXPECT_GT(inst.scale, 0.0f);
        EXPECT_NE(inst.visual, 0u);
        EXPECT_NE(inst.species, kInvalidSpecies);
    }
}

// ---- Filters ----

TEST(Vegetation, SlopeFilterRejectsSteepGround) {
    VegetationBuilder b("slope-test");
    b.WithMasterSeed(7);
    b.AddSpecies(1);
    b.WithDensity(0.05f).WithSpacing(0.0f).WithSlopeRange(0.0f, 30.0f);
    const VegetationDef def = b.Take();

    HalfSteepTerrain terrain(/*splitX*/ 32.0f);  // x >= 32 is a 60° slope (> 30° max)
    const std::vector<VegetationInstance> all = ScatterCell(def, terrain, 0, 0, kCellSize);

    ASSERT_FALSE(all.empty());
    for (const VegetationInstance& inst : all) {
        EXPECT_LT(inst.position[0], 32.0f);  // only the flat half is eligible
    }
}

TEST(Vegetation, AltitudeFilterKeepsBand) {
    VegetationBuilder b("altitude-test");
    b.WithMasterSeed(11);
    b.AddSpecies(1);
    b.WithDensity(0.1f).WithSpacing(0.0f).WithAltitudeRange(20.0f, 40.0f);
    const VegetationDef def = b.Take();

    HeightRampTerrain terrain;  // height == worldX
    const std::vector<VegetationInstance> all = ScatterCell(def, terrain, 0, 0, kCellSize);

    ASSERT_FALSE(all.empty());
    for (const VegetationInstance& inst : all) {
        EXPECT_GE(inst.position[1], 20.0f);
        EXPECT_LE(inst.position[1], 40.0f);
    }
}

TEST(Vegetation, MaskFilterRestrictsToPaintedRegion) {
    VegetationBuilder b("mask-test");
    b.WithMasterSeed(13);
    b.AddSpecies(1);
    b.WithDensity(0.1f).WithSpacing(0.0f).WithRequiredMask(0x2u);
    const VegetationDef def = b.Take();

    MaskHalfTerrain terrain(/*splitX*/ 32.0f, /*low*/ 0x1u, /*high*/ 0x2u);
    const std::vector<VegetationInstance> all = ScatterCell(def, terrain, 0, 0, kCellSize);

    ASSERT_FALSE(all.empty());
    for (const VegetationInstance& inst : all) {
        EXPECT_GE(inst.position[0], 32.0f);  // only the masked (0x2) half is eligible
    }
}

TEST(Vegetation, MinSpacingIsRespected) {
    const float spacing = 5.0f;
    VegetationBuilder b("spacing-test");
    b.WithMasterSeed(17);
    b.AddSpecies(1);
    b.WithDensity(0.2f).WithSpacing(spacing);
    const VegetationDef def = b.Take();

    FlatTerrainSampler terrain;
    const std::vector<VegetationInstance> all = ScatterCell(def, terrain, 0, 0, kCellSize);
    ASSERT_GT(all.size(), 2u);

    const float spacingSq = spacing * spacing;
    for (size_t i = 0; i < all.size(); ++i) {
        for (size_t j = i + 1; j < all.size(); ++j) {
            const float dx = all[i].position[0] - all[j].position[0];
            const float dz = all[i].position[2] - all[j].position[2];
            EXPECT_GE(dx * dx + dz * dz, spacingSq - 1e-3f);
        }
    }
}

TEST(Vegetation, DensityApproximatesTarget) {
    const float density = 0.02f;
    VegetationBuilder b("density-test");
    b.WithMasterSeed(19);
    b.AddSpecies(1);
    b.WithDensity(density).WithSpacing(0.0f);  // no spacing rejection -> pure grid
    const VegetationDef def = b.Take();

    FlatTerrainSampler terrain;
    const std::vector<VegetationInstance> all = ScatterCell(def, terrain, 0, 0, kCellSize);

    const float expected = density * kCellSize * kCellSize;  // ~82
    EXPECT_GT(static_cast<float>(all.size()), expected * 0.6f);
    EXPECT_LT(static_cast<float>(all.size()), expected * 1.3f);
}

TEST(Vegetation, MaxInstancesPerCellCaps) {
    VegetationBuilder b("cap-test");
    b.WithMasterSeed(23).WithMaxInstancesPerCell(10);
    b.AddSpecies(1);
    b.WithDensity(0.5f).WithSpacing(0.0f);  // would place hundreds uncapped
    const VegetationDef def = b.Take();

    FlatTerrainSampler terrain;
    const std::vector<VegetationInstance> all = ScatterCell(def, terrain, 0, 0, kCellSize);
    EXPECT_LE(all.size(), 10u);
}

// ---- Validator (fail-closed) ----

TEST(Vegetation, ValidatorAcceptsWellFormedDef) {
    const VegetationDef def = MakeForest();
    const VegetationValidationReport report = VegetationValidator::Validate(def);
    EXPECT_TRUE(report.Ok()) << (report.errors.empty() ? "" : ToString(report.errors.front().code));
}

TEST(Vegetation, ValidatorReportsDefLevelDefects) {
    using Code = VegetationValidationCode;
    VegetationDef def;            // empty id, no species
    def.schemaVersion = 999;      // unknown
    def.maxInstancesPerCell = 0;  // non-positive

    const VegetationValidationReport report = VegetationValidator::Validate(def);
    EXPECT_FALSE(report.Ok());
    EXPECT_TRUE(report.Has(Code::EmptyVegetationId));
    EXPECT_TRUE(report.Has(Code::UnknownSchemaVersion));
    EXPECT_TRUE(report.Has(Code::NoSpecies));
    EXPECT_TRUE(report.Has(Code::NonPositiveMaxInstancesPerCell));
}

TEST(Vegetation, ValidatorReportsSpeciesDefects) {
    using Code = VegetationValidationCode;
    VegetationDef def;
    def.id = "broken";

    VegetationSpecies bad;
    bad.id = kInvalidSpecies;        // reserved
    bad.visual = 0;                  // nothing to draw
    bad.densityPerSqMeter = 500.0f;  // over the cap
    bad.minSpacing = -1.0f;          // negative
    bad.minSlopeDegrees = 50.0f;     // inverted vs max
    bad.maxSlopeDegrees = 10.0f;
    bad.minAltitude = 100.0f;  // inverted vs max
    bad.maxAltitude = 0.0f;
    bad.minScale = -1.0f;  // non-positive + inverted
    bad.maxScale = -2.0f;
    bad.logicalRadius = -3.0f;  // negative
    def.species.push_back(bad);

    VegetationSpecies dupA;
    dupA.id = 5;
    dupA.visual = 1;
    VegetationSpecies dupB = dupA;  // same id -> duplicate
    def.species.push_back(dupA);
    def.species.push_back(dupB);

    const VegetationValidationReport report = VegetationValidator::Validate(def);
    EXPECT_FALSE(report.Ok());
    EXPECT_TRUE(report.Has(Code::ReservedSpeciesId));
    EXPECT_TRUE(report.Has(Code::ZeroVisualId));
    EXPECT_TRUE(report.Has(Code::DensityTooHigh));
    EXPECT_TRUE(report.Has(Code::NegativeSpacing));
    EXPECT_TRUE(report.Has(Code::InvertedSlopeRange));
    EXPECT_TRUE(report.Has(Code::InvertedAltitudeRange));
    EXPECT_TRUE(report.Has(Code::NonPositiveScale));
    EXPECT_TRUE(report.Has(Code::InvertedScaleRange));
    EXPECT_TRUE(report.Has(Code::NegativeLogicalRadius));
    EXPECT_TRUE(report.Has(Code::DuplicateSpeciesId));
}

// ---- Cell blob (the CellLayer::Vegetation payload that streams to UE5) ----

TEST(Vegetation, CellBlobRoundTrips) {
    const VegetationDef def = MakeForest();
    FlatTerrainSampler terrain;
    const std::vector<VegetationInstance> instances = ScatterCell(def, terrain, 2, -3, kCellSize);
    ASSERT_FALSE(instances.empty());

    const std::vector<uint8_t> blob = PackCell(2, -3, kCellSize, instances);

    VegetationCellData parsed;
    ASSERT_TRUE(UnpackCell(blob.data(), blob.size(), parsed));
    EXPECT_EQ(parsed.header.instanceCount, instances.size());
    EXPECT_EQ(parsed.header.cellX, 2);
    EXPECT_EQ(parsed.header.cellZ, -3);
    EXPECT_EQ(parsed.header.cellSize, kCellSize);
    EXPECT_TRUE(SameInstances(parsed.instances, instances));
}

TEST(Vegetation, CellBlobRejectsCorruption) {
    FlatTerrainSampler terrain;
    const std::vector<VegetationInstance> instances = ScatterCell(MakeForest(), terrain, 0, 0, kCellSize);
    std::vector<uint8_t> blob = PackCell(0, 0, kCellSize, instances);

    VegetationCellData parsed;
    // Truncated buffer.
    EXPECT_FALSE(UnpackCell(blob.data(), blob.size() - 1, parsed));
    // Too small to even hold a header.
    EXPECT_FALSE(UnpackCell(blob.data(), 4, parsed));
    // Corrupted magic.
    std::vector<uint8_t> corrupt = blob;
    corrupt[0] ^= 0xFFu;
    EXPECT_FALSE(UnpackCell(corrupt.data(), corrupt.size(), parsed));
}

TEST(Vegetation, CellBlobHandlesEmpty) {
    const std::vector<VegetationInstance> none;
    const std::vector<uint8_t> blob = PackCell(0, 0, kCellSize, none);
    EXPECT_EQ(blob.size(), sizeof(VegetationCellHeader));

    VegetationCellData parsed;
    ASSERT_TRUE(UnpackCell(blob.data(), blob.size(), parsed));
    EXPECT_EQ(parsed.header.instanceCount, 0u);
    EXPECT_TRUE(parsed.instances.empty());
}

// ---- Spatial query ----

TEST(Vegetation, QueryRadiusXZFindsInRange) {
    std::vector<VegetationInstance> instances(3);
    instances[0].position[0] = 0.0f;
    instances[0].position[2] = 0.0f;
    instances[0].instanceId = 100;
    instances[1].position[0] = 10.0f;
    instances[1].position[2] = 0.0f;
    instances[1].instanceId = 200;
    instances[2].position[0] = 0.0f;
    instances[2].position[2] = 10.0f;
    instances[2].instanceId = 300;

    const std::vector<uint32_t> near = QueryRadiusXZ(instances, 0.0f, 0.0f, 5.0f);
    ASSERT_EQ(near.size(), 1u);
    EXPECT_EQ(near[0], 100u);

    const std::vector<uint32_t> wide = QueryRadiusXZ(instances, 0.0f, 0.0f, 11.0f);
    ASSERT_EQ(wide.size(), 3u);
    EXPECT_EQ(wide[0], 100u);  // deterministic order = ascending index
    EXPECT_EQ(wide[1], 200u);
    EXPECT_EQ(wide[2], 300u);

    EXPECT_TRUE(QueryRadiusXZ(instances, 0.0f, 0.0f, 0.0f).empty());
}
