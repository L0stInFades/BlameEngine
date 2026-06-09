// UE5 water render contract (W17-W21). The render side is out-of-repo (ADR-0005: UE5 renders); what
// the engine OWES it is a streamed contract sufficient to reproduce the AUTHORITATIVE surface. This
// pins that end to end: a designer authors a scene -> cook -> the layered Water cell streams as bytes
// -> a consumer (the UE5 stand-in) reconstructs each body's animated Gerstner surface from ONLY those
// bytes -> its height AND normal match the authoritative sim surface to sub-millimetre, for every body
// type, across time, and for a body spanning multiple cells. If any parameter were dropped in
// cook/pack/stream/unpack, the reconstructed surface would diverge — so this is the completeness proof
// for the render contract. (UE5's HLSL Gerstner, fed the same WaveComponents + det-trig formula, agrees
// by construction; this is the headless reference, not a GPU.)

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "next/streaming/layered_cell_file.h"
#include "next/water/water_builder.h"
#include "next/water/water_def.h"
#include "next/water/water_surface.h"
#include "next/water_world/water_cook.h"
#include "next/water_world/water_store.h"
#include "next/water_world/water_view.h"

using namespace Next::water;
using Next::Streaming::CellLayer;

namespace {

// A scene with one body of every type, all overlapping cell (0,0) at size 256 except the global sea.
WaterSceneDef MakeAllTypesScene() {
    WaterBuilder b("render-contract");
    b.AddBody("sea", WaterType::Ocean)
        .WithBounds(-100000.0f, -50.0f, -100000.0f, 100000.0f, 0.0f, 100000.0f)
        .WithSurfaceHeight(0.0f)
        .WithDensity(1025.0f)
        .AddWave(0.6f, 24.0f, 1.0f, 0.2f, 3.0f, 0.4f)
        .AddWave(0.3f, 11.0f, 0.3f, 1.0f, 2.2f, 0.2f);
    b.AddBody("pool", WaterType::Pool).WithBounds(10.0f, -4.0f, 10.0f, 40.0f, 0.0f, 40.0f).WithSurfaceHeight(-0.5f);
    b.AddBody("river", WaterType::River)
        .WithBounds(60.0f, -3.0f, 0.0f, 120.0f, 0.0f, 20.0f)
        .WithSurfaceHeight(0.0f)
        .WithFlow(2.5f, 0.0f, 0.0f)
        .AddWave(0.15f, 6.0f, 1.0f, 0.0f, 1.5f, 0.1f);
    // Flood: surface RISES over time -> exercises the time-varying render path.
    b.AddBody("flood", WaterType::Flood)
        .WithBounds(150.0f, -5.0f, 150.0f, 200.0f, 5.0f, 200.0f)
        .WithSurfaceHeight(0.0f)
        .WithFlood(0.2f, 4.0f);
    return b.Take();
}

}  // namespace

TEST(WaterRender, ConsumerReconstructsAuthoritativeSurface) {
    const WaterSceneDef scene = MakeAllTypesScene();
    const float cellSize = 256.0f;

    // Cook two cells: (0,0) carries every body; (10,10) carries only the global sea (multi-cell body).
    MockWaterConsumer consumer;
    WaterStore store;
    for (const auto& coord : {std::pair<int32_t, int32_t>{0, 0}, std::pair<int32_t, int32_t>{10, 10}}) {
        const WaterCookResult cooked = CookWaterCell(scene, coord.first, coord.second, cellSize);
        ASSERT_TRUE(cooked.ok);
        std::vector<uint8_t> waterLayer;
        ASSERT_TRUE(
            Next::Streaming::ExtractLayer(cooked.bytes.data(), cooked.bytes.size(), CellLayer::Water, waterLayer));
        ASSERT_TRUE(consumer.OnCellLoaded(coord.first, coord.second, waterLayer.data(), waterLayer.size()));
        ASSERT_TRUE(store.LoadCell(coord.first, coord.second, waterLayer.data(), waterLayer.size()));
    }

    // The sea spans both cells but de-duplicates to one body the renderer instantiates.
    EXPECT_EQ(consumer.DistinctBodyCount(), 4u);

    // Render parity: for many points and times, the consumer's reconstructed surface (height + normal),
    // built from the streamed bytes alone, equals the authoritative body's surface to sub-mm.
    const double times[] = {0.0, 2.5, 60.0, 1.0e4};  // incl. a large t (flood risen, wave phase wrapped)
    int checked = 0;
    for (const double t : times) {
        for (float x = 5.0f; x <= 200.0f; x += 7.0f) {
            for (float z = 0.0f; z <= 200.0f; z += 11.0f) {
                const WaterBodyInstance* authBody = store.BodyAt(x, z, t);
                if (authBody == nullptr) {
                    continue;  // no water governs this XZ
                }
                SurfaceSample rendered{};
                ASSERT_TRUE(consumer.EvaluateSurface(authBody->bodyId, x, z, t, rendered));
                const SurfaceSample authoritative = SurfaceSampleAt(*authBody, x, z, t);
                EXPECT_NEAR(rendered.height, authoritative.height, 1e-3f)
                    << "body=" << authBody->bodyId << " x=" << x << " z=" << z << " t=" << t;
                EXPECT_NEAR(rendered.normal[0], authoritative.normal[0], 1e-3f);
                EXPECT_NEAR(rendered.normal[1], authoritative.normal[1], 1e-3f);
                EXPECT_NEAR(rendered.normal[2], authoritative.normal[2], 1e-3f);
                ++checked;
            }
        }
    }
    EXPECT_GT(checked, 100) << "the parity sweep must actually exercise the bodies";
}

TEST(WaterRender, StreamedParamsCarryEverythingTheRendererNeeds) {
    const WaterSceneDef scene = MakeAllTypesScene();
    MockWaterConsumer consumer;
    const WaterCookResult cooked = CookWaterCell(scene, 0, 0, 256.0f);
    ASSERT_TRUE(cooked.ok);
    std::vector<uint8_t> waterLayer;
    ASSERT_TRUE(Next::Streaming::ExtractLayer(cooked.bytes.data(), cooked.bytes.size(), CellLayer::Water, waterLayer));
    ASSERT_TRUE(consumer.OnCellLoaded(0, 0, waterLayer.data(), waterLayer.size()));

    // bodyIds are 1-based in scene order: sea=1, pool=2, river=3, flood=4.
    const WaterBodyInstance* sea = consumer.SurfaceParamsForBody(1);
    ASSERT_NE(sea, nullptr);
    EXPECT_EQ(sea->type, static_cast<uint8_t>(WaterType::Ocean));
    EXPECT_EQ(sea->waveCount, 2u);  // both Gerstner components survived
    EXPECT_FLOAT_EQ(sea->density, 1025.0f);

    const WaterBodyInstance* river = consumer.SurfaceParamsForBody(3);
    ASSERT_NE(river, nullptr);
    EXPECT_FLOAT_EQ(river->flowVelocity[0], 2.5f);  // flow survived (drives the flow map + buoyancy)
    EXPECT_NE(river->flags & WaterCurrent, 0);

    const WaterBodyInstance* flood = consumer.SurfaceParamsForBody(4);
    ASSERT_NE(flood, nullptr);
    EXPECT_EQ(flood->type, static_cast<uint8_t>(WaterType::Flood));
    EXPECT_FLOAT_EQ(flood->floodRate, 0.2f);
    EXPECT_FLOAT_EQ(flood->floodMaxHeight, 4.0f);

    // The flood surface the renderer would draw rises with time, exactly as the sim computes it.
    float h0 = 0.0f;
    float h1 = 0.0f;
    SurfaceSample s{};
    ASSERT_TRUE(consumer.EvaluateSurface(4, 175.0f, 175.0f, 0.0, s));
    h0 = s.height;
    ASSERT_TRUE(consumer.EvaluateSurface(4, 175.0f, 175.0f, 10.0, s));
    h1 = s.height;
    EXPECT_GT(h1, h0) << "flood surface should have risen by t=10";
    EXPECT_NEAR(h1, EffectiveSurfaceHeight(*flood, 10.0), 1e-4f);
}

// The parity test above compares the consumer against the sim's own SurfaceSampleAt, which proves the
// streamed bytes are LOSSLESS but (sharing the code) cannot prove the surface FORMULA. This one closes
// that gap: it reconstructs the surface from streamed bytes and compares against an INDEPENDENT,
// hand-rolled Gerstner sum using std::cos (a steepness-0 wave so there is no horizontal inversion). It
// would fail if the wave params were corrupted in cook/pack/stream OR the surface formula were wrong.
TEST(WaterRender, ConsumerSurfaceMatchesIndependentGerstnerReference) {
    constexpr float kPi = 3.14159265358979323846f;
    const float amp = 0.4f;
    const float wavelength = 18.0f;
    const float dirX = 1.0f;
    const float dirZ = 0.0f;  // axis-aligned, unit
    const float speed = 1.7f;
    WaterBuilder b("ref");
    b.AddBody("lake", WaterType::Lake)
        .WithBounds(-200.0f, -10.0f, -200.0f, 200.0f, 0.0f, 200.0f)
        .WithSurfaceHeight(0.0f)
        .AddWave(amp, wavelength, dirX, dirZ, speed, /*steepness*/ 0.0f);  // steepness 0 -> no pinch/inversion
    const WaterSceneDef scene = b.Take();

    MockWaterConsumer consumer;
    const WaterCookResult cooked = CookWaterCell(scene, 0, 0, 256.0f);
    ASSERT_TRUE(cooked.ok);
    std::vector<uint8_t> waterLayer;
    ASSERT_TRUE(Next::Streaming::ExtractLayer(cooked.bytes.data(), cooked.bytes.size(), CellLayer::Water, waterLayer));
    ASSERT_TRUE(consumer.OnCellLoaded(0, 0, waterLayer.data(), waterLayer.size()));

    const double k = 2.0 * static_cast<double>(kPi) / static_cast<double>(wavelength);
    const double omega = static_cast<double>(speed) * k;
    for (const double t : {0.0, 4.0, 250.0}) {
        for (float x = -40.0f; x <= 40.0f; x += 13.0f) {
            for (float z = -40.0f; z <= 40.0f; z += 17.0f) {
                // Independent reference: base + A*cos(k*(D.xz) - omega*t), no det_trig, no engine code.
                const double phase =
                    (k * (static_cast<double>(dirX) * x + static_cast<double>(dirZ) * z)) - (omega * t);
                const float ref = amp * static_cast<float>(std::cos(phase));
                SurfaceSample s{};
                ASSERT_TRUE(consumer.EvaluateSurface(1, x, z, t, s));
                // det_trig vs std::cos differ by the deterministic-trig approximation error only.
                EXPECT_NEAR(s.height, ref, 0.02f) << "x=" << x << " z=" << z << " t=" << t;
            }
        }
    }
}

TEST(WaterRender, LodHeightTracksFullSurfaceWithinDroppedAmplitude) {
    const WaterSceneDef scene = MakeAllTypesScene();
    MockWaterConsumer consumer;
    const WaterCookResult cooked = CookWaterCell(scene, 0, 0, 256.0f);
    ASSERT_TRUE(cooked.ok);
    std::vector<uint8_t> waterLayer;
    ASSERT_TRUE(Next::Streaming::ExtractLayer(cooked.bytes.data(), cooked.bytes.size(), CellLayer::Water, waterLayer));
    ASSERT_TRUE(consumer.OnCellLoaded(0, 0, waterLayer.data(), waterLayer.size()));

    const WaterBodyInstance* sea = consumer.SurfaceParamsForBody(1);
    ASSERT_NE(sea, nullptr);
    float dropped = 0.0f;
    for (int i = 1; i < sea->waveCount; ++i) {  // keep only the largest component
        dropped += std::fabs(sea->waves[i].amplitude);
    }
    const double t = 3.0;
    for (float x = -20.0f; x <= 20.0f; x += 9.0f) {
        for (float z = -20.0f; z <= 20.0f; z += 9.0f) {
            // Compare LOD(1) against the FULL LOD sum (maxWaves >= waveCount). Both are the same direct
            // (non-inverted) summation path, so the ONLY difference is the dropped components -> a TIGHT
            // bound (dropped amplitude + a hair), unlike comparing against the inverted SurfaceSampleAt.
            float full = 0.0f;
            ASSERT_TRUE(consumer.EvaluateSurfaceHeightLOD(1, x, z, t, kMaxWavesPerBody, full));
            float lod1 = 0.0f;
            ASSERT_TRUE(consumer.EvaluateSurfaceHeightLOD(1, x, z, t, 1, lod1));
            EXPECT_LE(std::fabs(lod1 - full), dropped + 1e-3f);
        }
    }
}
