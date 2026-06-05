// Water pure-core tests (ADR-0015): the deterministic Gerstner surface, deterministic trig, analytic
// submerged-volume (sphere cap / box slab), the Archimedes + clamped-drag buoyancy model, the
// fail-closed validator, the NWTR cell round-trip, the fluent builder, and the ocean spectrum. These
// pin the MATH the whole water system stands on, in isolation (foundation-only, no physics/ECS).

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <cstring>
#include <vector>

#include "next/water/buoyancy.h"
#include "next/water/det_trig.h"
#include "next/water/water_builder.h"
#include "next/water/water_cell.h"
#include "next/water/water_def.h"
#include "next/water/water_spectrum.h"
#include "next/water/water_surface.h"
#include "next/water/water_validator.h"
#include "next/water/water_volume.h"

using namespace Next::water;

namespace {

constexpr float kPi = 3.14159265358979323846f;

WaterBodyInstance MakeFlatPool(float surfaceY, float density = 1000.0f) {
    WaterBodyInstance b;
    b.boundsMin[0] = -100.0f;
    b.boundsMin[1] = -100.0f;
    b.boundsMin[2] = -100.0f;
    b.boundsMax[0] = 100.0f;
    b.boundsMax[1] = surfaceY;
    b.boundsMax[2] = 100.0f;
    b.surfaceHeight = surfaceY;
    b.density = density;
    b.type = static_cast<uint8_t>(WaterType::Pool);
    b.flags = WaterBuoyant;
    b.waveCount = 0;
    b.bodyId = 1;
    return b;
}

}  // namespace

// ---- deterministic trig ----

TEST(WaterDetTrig, MatchesStdWithinTolerance) {
    for (float x = -12.0f; x <= 12.0f; x += 0.013f) {
        EXPECT_NEAR(DetSin(x), std::sin(x), 2e-3f) << "x=" << x;
        EXPECT_NEAR(DetCos(x), std::cos(x), 2e-3f) << "x=" << x;
    }
}

TEST(WaterDetTrig, KnownValues) {
    EXPECT_NEAR(DetSin(0.0f), 0.0f, 1e-4f);
    EXPECT_NEAR(DetSin(kPi / 2.0f), 1.0f, 1e-3f);
    EXPECT_NEAR(DetCos(0.0f), 1.0f, 1e-4f);
    EXPECT_NEAR(DetCos(kPi), -1.0f, 1e-3f);
}

TEST(WaterDetTrig, Deterministic) {
    for (float x = -30.0f; x <= 30.0f; x += 0.7f) {
        EXPECT_EQ(DetSin(x), DetSin(x));  // bit-identical, no hidden state
        EXPECT_EQ(DetCos(x), DetCos(x));
    }
}

// ---- POD wire layout ----

TEST(WaterDef, WireLayoutStable) {
    EXPECT_EQ(sizeof(WaveComponent), 24u);
    EXPECT_EQ(sizeof(WaterBodyInstance), 264u);
    EXPECT_EQ(sizeof(WaterCellHeader), 28u);  // v2: + bodyStride
}

// ---- surface ----

TEST(WaterSurface, FlatWhenNoWaves) {
    WaterBodyInstance b = MakeFlatPool(3.0f);
    EXPECT_FLOAT_EQ(SampleHeightFast(b, 5.0f, -2.0f, 1.23), 3.0f);
    EXPECT_FLOAT_EQ(SurfaceHeightAt(b, 5.0f, -2.0f, 1.23), 3.0f);
}

TEST(WaterSurface, SingleSineMatchesClosedForm) {
    WaterBodyInstance b = MakeFlatPool(0.0f);
    b.waveCount = 1;
    b.waves[0].amplitude = 0.5f;
    b.waves[0].wavelength = 10.0f;
    b.waves[0].direction[0] = 1.0f;
    b.waves[0].direction[1] = 0.0f;
    b.waves[0].speed = 2.0f;
    b.waves[0].steepness = 0.0f;  // plain sine -> no horizontal pinch, height is exact at (x,z)
    const float k = 2.0f * kPi / 10.0f;
    const double t = 0.75;
    for (float x = -20.0f; x <= 20.0f; x += 1.0f) {
        const float phase = (k * x) - (b.waves[0].speed * k * static_cast<float>(t));
        const float expected = 0.5f * DetCos(phase);
        EXPECT_NEAR(SampleHeightFast(b, x, 0.0f, t), expected, 1e-4f);
        EXPECT_NEAR(SurfaceHeightAt(b, x, 0.0f, t), expected, 1e-3f);
    }
}

TEST(WaterSurface, Deterministic) {
    WaterBodyInstance b = MakeFlatPool(0.0f);
    b.waveCount = 2;
    b.waves[0] = {0.4f, 8.0f, {1.0f, 0.0f}, 1.5f, 0.4f};
    b.waves[1] = {0.2f, 3.0f, {0.0f, 1.0f}, 0.9f, 0.3f};
    for (double t = 0.0; t < 5.0; t += 0.37) {
        EXPECT_EQ(SurfaceHeightAt(b, 1.0f, 2.0f, t), SurfaceHeightAt(b, 1.0f, 2.0f, t));
        const SurfaceSample s = SurfaceSampleAt(b, 1.0f, 2.0f, t);
        const float n = std::sqrt(s.normal[0] * s.normal[0] + s.normal[1] * s.normal[1] + s.normal[2] * s.normal[2]);
        EXPECT_NEAR(n, 1.0f, 1e-4f);  // normal stays unit
    }
}

TEST(WaterSurface, FloodRisesAndCaps) {
    WaterBodyInstance b = MakeFlatPool(0.0f);
    b.type = static_cast<uint8_t>(WaterType::Flood);
    b.floodRate = 0.5f;  // 0.5 m/s
    b.floodMaxHeight = 4.0f;
    EXPECT_NEAR(EffectiveSurfaceHeight(b, 0.0), 0.0f, 1e-5f);
    EXPECT_NEAR(EffectiveSurfaceHeight(b, 4.0), 2.0f, 1e-5f);    // risen 0.5*4
    EXPECT_NEAR(EffectiveSurfaceHeight(b, 100.0), 4.0f, 1e-5f);  // capped
}

TEST(WaterSurface, NoPhaseDriftAtLargeTime) {
    // W27: at a long session time, casting (omega*t) straight to float collapses precision; WrapPhase
    // does the phase in double + reduces mod 2π, so the height still matches a double reference closely.
    WaterBodyInstance b = MakeFlatPool(0.0f);
    b.waveCount = 1;
    b.waves[0] = {0.5f, 12.0f, {1.0f, 0.0f}, 2.0f, 0.0f};  // steepness 0
    const double pi = 3.14159265358979323846;
    const double t = 1.0e6;  // ~11.5 days of sim
    const double k = 2.0 * pi / 12.0;
    const double omega = 2.0 * k;  // speed (2) * k
    double ph = (k * 7.0) - (omega * t);
    ph -= 2.0 * pi * std::floor(ph / (2.0 * pi));
    const float expected = 0.5f * static_cast<float>(std::cos(ph));
    EXPECT_NEAR(SampleHeightFast(b, 7.0f, 0.0f, t), expected, 5e-3f);
}

// ---- submerged volume ----

TEST(WaterVolume, SphereCapHalfThenFull) {
    float frac = 0.0f;
    const float total = (4.0f / 3.0f) * kPi * 1.0f;
    // center exactly at surface -> half submerged
    float v = SubmergedSphereVolume(/*centerY*/ 0.0f, /*r*/ 1.0f, /*surfaceY*/ 0.0f, /*floorY*/ -100.0f, frac);
    EXPECT_NEAR(frac, 0.5f, 1e-4f);
    EXPECT_NEAR(v, total * 0.5f, 1e-4f);
    // fully below
    v = SubmergedSphereVolume(-5.0f, 1.0f, 0.0f, -100.0f, frac);
    EXPECT_NEAR(frac, 1.0f, 1e-5f);
    EXPECT_NEAR(v, total, 1e-4f);
    // fully above
    v = SubmergedSphereVolume(5.0f, 1.0f, 0.0f, -100.0f, frac);
    EXPECT_NEAR(frac, 0.0f, 1e-5f);
    EXPECT_NEAR(v, 0.0f, 1e-5f);
}

TEST(WaterVolume, BoxHalfAndFloorClip) {
    float frac = 0.0f;
    const float half[3] = {1.0f, 1.0f, 1.0f};
    const float total = 8.0f;
    // center at surface -> half
    float v = SubmergedBoxVolume(0.0f, half, 0.0f, -100.0f, frac);
    EXPECT_NEAR(frac, 0.5f, 1e-5f);
    EXPECT_NEAR(v, total * 0.5f, 1e-5f);
    // floor clips: water fills from floorY=0.5 up to the high surface, so only the box slice
    // [0.5, +1] of the [-1, +1] box is in water -> 0.5 / 2 = 0.25.
    v = SubmergedBoxVolume(0.0f, half, 100.0f, 0.5f, frac);  // surface high, floor at 0.5
    EXPECT_NEAR(frac, 0.25f, 1e-5f);
    EXPECT_NEAR(v, total * 0.25f, 1e-5f);
}

// ---- buoyancy ----

TEST(WaterBuoyancy, DryBodyNoForce) {
    FluidSample fluid;
    fluid.surfaceHeight = 0.0f;
    fluid.floorY = -100.0f;
    BodyBuoyancyInput body;
    body.shape = 0;
    body.position[1] = 10.0f;  // well above the surface
    const WaterForceOutput o = ComputeWaterForce(fluid, body, 1.0f / 60.0f);
    EXPECT_FALSE(o.inWater);
    EXPECT_FLOAT_EQ(o.force[1], 0.0f);
}

TEST(WaterBuoyancy, ArchimedesEquilibriumForceEqualsWeight) {
    // A sphere whose density is half the water's floats half-submerged; at that submersion the
    // buoyant force must equal its weight m*g (the equilibrium the scale test relies on).
    const float r = 1.0f;
    const float vTotal = (4.0f / 3.0f) * kPi * r * r * r;
    const float waterRho = 1000.0f;
    const float bodyRho = 500.0f;  // half -> equilibrium fraction 0.5
    const float mass = bodyRho * vTotal;
    FluidSample fluid;
    fluid.density = waterRho;
    fluid.surfaceHeight = 0.0f;
    fluid.floorY = -100.0f;
    BodyBuoyancyInput body;
    body.shape = 0;
    body.halfExtents[0] = body.halfExtents[1] = body.halfExtents[2] = r;
    body.position[1] = 0.0f;  // half submerged
    body.mass = mass;
    const WaterForceOutput o = ComputeWaterForce(fluid, body, 1.0f / 60.0f);
    EXPECT_TRUE(o.inWater);
    EXPECT_NEAR(o.submergedFraction, 0.5f, 1e-3f);
    EXPECT_NEAR(o.force[1], mass * kWaterGravity, mass * kWaterGravity * 0.01f);  // within 1%
}

TEST(WaterBuoyancy, DragImpulseNeverReversesVelocity) {
    // Clamp guarantee: even with an absurd drag*dt the impulse can at most cancel the relative
    // velocity, never flip it (the "no rocket-launch" stability property).
    FluidSample fluid;
    fluid.surfaceHeight = 100.0f;  // fully submerged
    fluid.floorY = -100.0f;
    fluid.linearDrag = 1000.0f;  // huge
    fluid.quadraticDrag = 1000.0f;
    BodyBuoyancyInput body;
    body.shape = 0;
    body.position[1] = 0.0f;
    body.mass = 1.0f;
    body.velocity[0] = 7.0f;
    body.velocity[1] = -3.0f;
    const WaterForceOutput o = ComputeWaterForce(fluid, body, 1.0f);  // big dt
    for (int i = 0; i < 3; ++i) {
        const float newV = body.velocity[i] + (o.dragImpulse[i] / body.mass);
        // same sign as before (or zero): |delta| <= |v|
        EXPECT_LE(std::fabs(o.dragImpulse[i] / body.mass), std::fabs(body.velocity[i]) + 1e-4f);
        EXPECT_GE(newV * body.velocity[i], -1e-4f) << "axis " << i << " reversed";
    }
}

TEST(WaterBuoyancy, CurrentSweepsTowardFlow) {
    FluidSample fluid;
    fluid.surfaceHeight = 100.0f;
    fluid.floorY = -100.0f;
    fluid.flags = WaterCurrent;
    fluid.flowVelocity[0] = 4.0f;  // river flowing +x
    BodyBuoyancyInput body;
    body.shape = 0;
    body.position[1] = 0.0f;  // submerged, at rest
    body.mass = 1.0f;
    const WaterForceOutput o = ComputeWaterForce(fluid, body, 1.0f / 60.0f);
    EXPECT_GT(o.dragImpulse[0], 0.0f);  // pushed downstream (+x)
}

// ---- validator ----

TEST(WaterValidator, AcceptsGoodScene) {
    WaterBuilder b("scene-ok");
    b.AddBody("pool", WaterType::Pool).WithBounds(-5, -2, -5, 5, 0, 5).WithSurfaceHeight(0.0f);
    const WaterValidationReport r = WaterValidator::Validate(b.Def());
    EXPECT_TRUE(r.Ok()) << (r.errors.empty() ? "" : ToString(r.errors.front().code));
}

TEST(WaterValidator, RejectsBadFieldsAndAccumulates) {
    WaterSceneDef scene;  // empty id, no bodies, wrong schema is fine (default ok) -> at least SceneIdEmpty+NoBodies
    scene.id = "";
    const WaterValidationReport r0 = WaterValidator::Validate(scene);
    EXPECT_FALSE(r0.Ok());
    EXPECT_TRUE(r0.Has(WaterValidationCode::SceneIdEmpty));
    EXPECT_TRUE(r0.Has(WaterValidationCode::NoBodies));

    WaterBuilder b("scene-bad");
    b.AddBody("x", WaterType::Pool)
        .WithBounds(5, 0, 5, -5, -2, -5)             // inverted
        .WithSurfaceHeight(100.0f)                   // outside bounds
        .WithDensity(-1.0f);                         // bad density
    b.AddWave(1.0f, -2.0f, 1.0f, 0.0f, 1.0f, 2.0f);  // bad wavelength + steepness>1
    const WaterValidationReport r = WaterValidator::Validate(b.Def());
    EXPECT_FALSE(r.Ok());
    EXPECT_TRUE(r.Has(WaterValidationCode::BoundsInverted));
    EXPECT_TRUE(r.Has(WaterValidationCode::DensityOutOfRange));
    EXPECT_TRUE(r.Has(WaterValidationCode::WaveWavelengthBad));
    EXPECT_TRUE(r.Has(WaterValidationCode::WaveSteepnessOutOfRange));
}

TEST(WaterValidator, RejectsTotalSteepnessOverOne) {
    WaterBuilder b("steep");
    b.AddBody("sea", WaterType::Ocean).WithBounds(-1e6f, -10, -1e6f, 1e6f, 10, 1e6f).WithSurfaceHeight(0.0f);
    b.AddWave(0.5f, 10.0f, 1.0f, 0.0f, 2.0f, 0.6f);
    b.AddWave(0.5f, 7.0f, 0.0f, 1.0f, 2.0f, 0.6f);  // 0.6 + 0.6 = 1.2 > 1
    const WaterValidationReport r = WaterValidator::Validate(b.Def());
    EXPECT_TRUE(r.Has(WaterValidationCode::TotalSteepnessExceedsOne));
}

TEST(WaterValidator, RejectsBoundsTooLarge) {
    WaterBuilder b("huge");
    b.AddBody("p", WaterType::Pool).WithBounds(-1.0e9f, -2.0f, -5.0f, 1.0e9f, 0.0f, 5.0f);  // |x|=1e9 > kMaxWaterCoord
    EXPECT_TRUE(WaterValidator::Validate(b.Def()).Has(WaterValidationCode::BoundsTooLarge));
}

// ---- cell round-trip ----

TEST(WaterCell, PackUnpackRoundTrip) {
    std::vector<WaterBodyInstance> bodies;
    bodies.push_back(MakeFlatPool(2.0f));
    WaterBodyInstance b2 = MakeFlatPool(-1.0f);
    b2.bodyId = 2;
    b2.waveCount = 1;
    b2.waves[0] = {0.3f, 5.0f, {1.0f, 0.0f}, 1.0f, 0.2f};
    bodies.push_back(b2);

    const std::vector<uint8_t> blob = PackCell(3, -4, 64.0f, bodies);
    WaterCellData out;
    ASSERT_TRUE(UnpackCell(blob.data(), blob.size(), out));
    EXPECT_EQ(out.header.cellX, 3);
    EXPECT_EQ(out.header.cellZ, -4);
    EXPECT_FLOAT_EQ(out.header.cellSize, 64.0f);
    ASSERT_EQ(out.bodies.size(), 2u);
    EXPECT_EQ(out.bodies[1].bodyId, 2u);
    EXPECT_EQ(out.bodies[1].waveCount, 1u);
    EXPECT_FLOAT_EQ(out.bodies[1].waves[0].wavelength, 5.0f);
}

TEST(WaterCell, FailClosedOnCorruption) {
    std::vector<WaterBodyInstance> bodies{MakeFlatPool(0.0f)};
    std::vector<uint8_t> blob = PackCell(0, 0, 32.0f, bodies);
    WaterCellData out;
    // truncated
    EXPECT_FALSE(UnpackCell(blob.data(), blob.size() - 1, out));
    // bad magic
    std::vector<uint8_t> bad = blob;
    bad[0] ^= 0xFF;
    EXPECT_FALSE(UnpackCell(bad.data(), bad.size(), out));
    // empty / null
    EXPECT_FALSE(UnpackCell(nullptr, 0, out));
    // a crafted huge bodyCount must be rejected BEFORE allocation (size won't match anyway)
    std::vector<uint8_t> huge = blob;
    huge[8] = 0xFF;
    huge[9] = 0xFF;
    huge[10] = 0xFF;
    huge[11] = 0xFF;
    EXPECT_FALSE(UnpackCell(huge.data(), huge.size(), out));
}

// ---- wire versioning + migration (W28) ----

TEST(WaterCell, MigratesLegacyV1ToCurrent) {
    WaterBodyInstance a = MakeFlatPool(1.5f);
    a.bodyId = 1;
    a.waveCount = 1;
    a.waves[0] = {0.3f, 5.0f, {1.0f, 0.0f}, 1.0f, 0.2f};
    WaterBodyInstance b = MakeFlatPool(-2.0f);
    b.bodyId = 2;
    const std::vector<WaterBodyInstance> bodies{a, b};

    // A v1 asset (cooked before the format moved to v2) still loads, migrated to the current layout.
    const std::vector<uint8_t> v1 = PackCellLegacyV1(7, -3, 64.0f, bodies);
    EXPECT_EQ(v1.size(), kWaterCellHeaderSizeV1 + bodies.size() * kWaterBodyRecordSizeV1);
    WaterCellData fromV1;
    ASSERT_TRUE(UnpackCell(v1.data(), v1.size(), fromV1));
    EXPECT_EQ(fromV1.header.version, kWaterCellVersionV1);  // provenance preserved
    EXPECT_EQ(fromV1.header.headerSize, kWaterCellHeaderSizeV1);
    EXPECT_EQ(fromV1.header.bodyStride, kWaterBodyRecordSizeV1);
    EXPECT_EQ(fromV1.header.cellX, 7);
    EXPECT_EQ(fromV1.header.cellZ, -3);
    ASSERT_EQ(fromV1.bodies.size(), 2u);

    // The current (v2) encoding of the same bodies genuinely differs on the wire (header grew 4 bytes)
    // yet decodes to byte-identical in-memory bodies — the migration is lossless.
    const std::vector<uint8_t> v2 = PackCell(7, -3, 64.0f, bodies);
    EXPECT_NE(v1.size(), v2.size());
    WaterCellData fromV2;
    ASSERT_TRUE(UnpackCell(v2.data(), v2.size(), fromV2));
    EXPECT_EQ(fromV2.header.version, kWaterCellVersion);
    ASSERT_EQ(fromV2.bodies.size(), 2u);
    for (size_t i = 0; i < bodies.size(); ++i) {
        EXPECT_EQ(0, std::memcmp(&fromV1.bodies[i], &fromV2.bodies[i], sizeof(WaterBodyInstance)));
    }
}

TEST(WaterCell, RejectsUnknownVersion) {
    const std::vector<WaterBodyInstance> bodies{MakeFlatPool(0.0f)};
    std::vector<uint8_t> blob = PackCell(0, 0, 32.0f, bodies);
    // Bump the on-disk version past what this build knows -> fail closed (no silent partial parse).
    const uint16_t future = static_cast<uint16_t>(kWaterCellVersion + 1);
    std::memcpy(blob.data() + 4, &future, sizeof(future));
    WaterCellData out;
    EXPECT_FALSE(UnpackCell(blob.data(), blob.size(), out));
}

TEST(WaterCell, RejectsCorruptWaveCount) {
    // waveCount indexes waves[kMaxWavesPerBody]; a corrupt value would drive an OOB read/write in every
    // sampler (incl. the W9 CompileWaves/SampleHeightLOD writers). UnpackCell must fail-closed on it.
    WaterBodyInstance b = MakeFlatPool(0.0f);
    b.waveCount = static_cast<uint8_t>(kMaxWavesPerBody + 1);  // 9 > 8
    const std::vector<uint8_t> blob = PackCell(0, 0, 32.0f, {b});
    WaterCellData out;
    EXPECT_FALSE(UnpackCell(blob.data(), blob.size(), out));

    b.waveCount = 255;  // pathological
    const std::vector<uint8_t> blob2 = PackCell(0, 0, 32.0f, {b});
    EXPECT_FALSE(UnpackCell(blob2.data(), blob2.size(), out));

    b.waveCount = kMaxWavesPerBody;  // the boundary value is still accepted
    const std::vector<uint8_t> blob3 = PackCell(0, 0, 32.0f, {b});
    EXPECT_TRUE(UnpackCell(blob3.data(), blob3.size(), out));
}

// ---- W9: compiled wave-set (bit-identical hot path) + cosmetic LOD + micro-benchmark ----

namespace {
WaterBodyInstance MakeWavyOcean() {
    WaterBodyInstance b = MakeFlatPool(0.0f);
    b.type = static_cast<uint8_t>(WaterType::Ocean);
    b.waveCount = 8;  // amplitude-descending (the LOD test relies on this ordering)
    b.waves[0] = {0.80f, 60.0f, {1.0f, 0.0f}, 3.0f, 0.15f};
    b.waves[1] = {0.50f, 31.0f, {0.7f, 0.7f}, 2.4f, 0.12f};
    b.waves[2] = {0.30f, 17.0f, {0.0f, 1.0f}, 2.0f, 0.10f};
    b.waves[3] = {0.18f, 9.0f, {-0.6f, 0.8f}, 1.6f, 0.08f};
    b.waves[4] = {0.10f, 5.0f, {0.3f, -0.95f}, 1.2f, 0.05f};
    b.waves[5] = {0.06f, 3.0f, {-1.0f, 0.0f}, 1.0f, 0.04f};
    b.waves[6] = {0.03f, 2.0f, {0.5f, 0.5f}, 0.8f, 0.02f};
    b.waves[7] = {0.02f, 1.3f, {0.0f, -1.0f}, 0.6f, 0.01f};
    return b;
}
}  // namespace

TEST(WaterWaveLOD, CompiledSampleIsBitIdenticalToInline) {
    const WaterBodyInstance b = MakeWavyOcean();
    const double times[] = {0.0, 1.234, 57.9, 1.0e5};  // incl. a large t (the WrapPhase regime)
    for (const double t : times) {
        const CompiledWaves cw = CompileWaves(b, t);
        EXPECT_EQ(cw.count, 8u);
        for (float x = -120.0f; x <= 120.0f; x += 13.0f) {
            for (float z = -120.0f; z <= 120.0f; z += 17.0f) {
                const float inl = SampleHeightFast(b, x, z, t);
                const float cmp = SampleHeightFast(cw, x, z, t);
                EXPECT_EQ(inl, cmp) << "x=" << x << " z=" << z << " t=" << t;  // bit-identical, not just close
            }
        }
    }
}

TEST(WaterWaveLOD, LodFullMatchesExactAndDroppedErrorIsBounded) {
    const WaterBodyInstance b = MakeWavyOcean();
    const double t = 3.5;
    for (float x = -50.0f; x <= 50.0f; x += 21.0f) {
        for (float z = -50.0f; z <= 50.0f; z += 23.0f) {
            const float exact = SampleHeightFast(b, x, z, t);
            // Full LOD sums the same set (only the summation order differs) -> within a hair of exact.
            EXPECT_NEAR(SampleHeightLOD(b, x, z, t, 8), exact, 1e-4f);
            // Keeping the 3 largest components: the error is bounded by the dropped amplitudes' sum.
            float dropped = 0.0f;
            for (int i = 3; i < 8; ++i) {
                dropped += std::fabs(b.waves[i].amplitude);
            }
            EXPECT_LE(std::fabs(SampleHeightLOD(b, x, z, t, 3) - exact), dropped + 1e-3f);
            EXPECT_FLOAT_EQ(SampleHeightLOD(b, x, z, t, 0), 0.0f);  // base only (still water, surface y=0)
        }
    }
}

TEST(WaterWaveLOD, MicroBenchmarkThroughputFloor) {
    const WaterBodyInstance b = MakeWavyOcean();
    const CompiledWaves cw = CompileWaves(b, 1.0);
    constexpr int kIters = 100000;  // 8 DetCos each
    volatile float sink = 0.0f;
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kIters; ++i) {
        const float x = static_cast<float>(i % 256);
        const float z = static_cast<float>((i * 7) % 256);
        sink += SampleHeightFast(cw, x, z, 1.0);
    }
    const auto t1 = std::chrono::steady_clock::now();
    (void)sink;
    const double secs = std::chrono::duration<double>(t1 - t0).count();
    const double rate = static_cast<double>(kIters) / (secs > 1e-9 ? secs : 1e-9);
    RecordProperty("compiled_8wave_samples_per_sec", static_cast<int>(rate));
    // Loose floor: catches a catastrophic regression while staying robust under sanitizers (~10-20x slow).
    EXPECT_GT(rate, 50000.0) << "compiled 8-wave sampler throughput collapsed (" << rate << "/s)";
}

// ---- builder + spectrum ----

TEST(WaterBuilder, NormalizesWaveDirection) {
    WaterBuilder b("dir");
    b.AddBody("p", WaterType::Pool).AddWave(0.5f, 10.0f, 3.0f, 4.0f, 1.0f, 0.2f);  // dir (3,4) len 5
    const WaveComponent& w = b.Def().bodies[0].waves[0];
    EXPECT_NEAR(std::sqrt(w.direction[0] * w.direction[0] + w.direction[1] * w.direction[1]), 1.0f, 1e-5f);
    EXPECT_NEAR(w.direction[0], 0.6f, 1e-5f);
    EXPECT_NEAR(w.direction[1], 0.8f, 1e-5f);
}

TEST(WaterSpectrum, DeterministicAndBudgeted) {
    OceanSpectrumParams p;
    p.windSpeed = 14.0f;
    p.steepnessBudget = 0.8f;
    p.seed = 12345;
    const std::vector<WaveComponent> a = GenerateOceanWaves(p, 6);
    const std::vector<WaveComponent> b = GenerateOceanWaves(p, 6);
    ASSERT_EQ(a.size(), b.size());
    ASSERT_EQ(a.size(), 6u);
    float steepSum = 0.0f;
    for (size_t i = 0; i < a.size(); ++i) {
        EXPECT_EQ(a[i].amplitude, b[i].amplitude);  // bit-identical (deterministic)
        EXPECT_EQ(a[i].wavelength, b[i].wavelength);
        EXPECT_GT(a[i].wavelength, 0.0f);
        EXPECT_GT(a[i].speed, 0.0f);
        EXPECT_NEAR(std::sqrt(a[i].direction[0] * a[i].direction[0] + a[i].direction[1] * a[i].direction[1]), 1.0f,
                    1e-4f);
        steepSum += a[i].steepness;
    }
    EXPECT_LE(steepSum, 0.8f + 1e-4f);  // never exceeds the budget (-> surface can't self-intersect)
}

TEST(WaterSpectrum, ClampsToMaxWaves) {
    OceanSpectrumParams p;
    const std::vector<WaveComponent> w = GenerateOceanWaves(p, 100);
    EXPECT_LE(w.size(), static_cast<size_t>(kMaxWavesPerBody));
}
