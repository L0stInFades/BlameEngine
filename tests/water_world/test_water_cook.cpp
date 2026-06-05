// Water cook tests (ADR-0015): scene -> validate (fail-closed) -> per-cell body selection -> stable
// 1-based bodyId -> PackCell -> layered Water cell. Golden byte stability + the text-def parser +
// round-trip through ExtractLayer/UnpackCell.

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "next/streaming/layered_cell_file.h"
#include "next/water/water_builder.h"
#include "next/water/water_cell.h"
#include "next/water_world/water_cook.h"

using namespace Next::water;
using Next::Streaming::CellLayer;

namespace {

WaterSceneDef HarborScene() {
    WaterBuilder b("harbor");
    b.AddBody("sea", WaterType::Ocean)
        .WithBounds(-100000.0f, -50.0f, -100000.0f, 100000.0f, 0.0f, 100000.0f)
        .WithSurfaceHeight(0.0f)
        .WithDensity(1025.0f)
        .AddWave(0.6f, 24.0f, 1.0f, 0.2f, 3.0f, 0.4f);
    b.AddBody("dock-pool", WaterType::Pool).WithBounds(0.0f, -4.0f, 0.0f, 32.0f, 0.0f, 32.0f).WithSurfaceHeight(0.0f);
    return b.Take();
}

}  // namespace

TEST(WaterCook, GoldenByteStable) {
    const WaterSceneDef scene = HarborScene();
    const WaterCookResult a = CookWaterCell(scene, 0, 0, 64.0f);
    const WaterCookResult b = CookWaterCell(scene, 0, 0, 64.0f);
    ASSERT_TRUE(a.ok);
    ASSERT_TRUE(b.ok);
    EXPECT_EQ(a.bytes, b.bytes);  // deterministic -> byte-identical
    EXPECT_GT(a.bytes.size(), 0u);
    // Cell (0,0) at size 64 covers [0,64]x[0,64]: both the global sea and the dock-pool overlap it.
    EXPECT_EQ(a.bodyCount, 2u);
}

TEST(WaterCook, PerCellSelection) {
    const WaterSceneDef scene = HarborScene();
    // A far cell ([6400,6464]) still has the global sea (huge bounds) but NOT the small dock-pool.
    const WaterCookResult far = CookWaterCell(scene, 100, 100, 64.0f);
    ASSERT_TRUE(far.ok);
    EXPECT_EQ(far.bodyCount, 1u);  // only the sea
}

TEST(WaterCook, FailClosedOnBadScene) {
    WaterBuilder b("bad");
    b.AddBody("p", WaterType::Pool).WithBounds(-5, -2, -5, 5, 0, 5).WithDensity(-1.0f);  // bad density
    const WaterCookResult r = CookWaterCell(b.Take(), 0, 0, 64.0f);
    EXPECT_FALSE(r.ok);
    EXPECT_FALSE(r.report.Ok());
    EXPECT_TRUE(r.bytes.empty());  // nothing cooked
}

TEST(WaterCook, CookedCellIsAValidLayeredWaterCell) {
    const WaterCookResult r = CookWaterCell(HarborScene(), 0, 0, 64.0f);
    ASSERT_TRUE(r.ok);
    // The cooked bytes are a layered cell carrying exactly the Water layer.
    std::vector<uint8_t> waterLayer;
    ASSERT_TRUE(Next::Streaming::ExtractLayer(r.bytes.data(), r.bytes.size(), CellLayer::Water, waterLayer));
    WaterCellData parsed;
    ASSERT_TRUE(UnpackCell(waterLayer.data(), waterLayer.size(), parsed));
    EXPECT_EQ(parsed.bodies.size(), 2u);
    // Stable 1-based ids assigned in scene order.
    EXPECT_EQ(parsed.bodies[0].bodyId, 1u);
    EXPECT_EQ(parsed.bodies[1].bodyId, 2u);
}

TEST(WaterCook, ParseDefTextThenCook) {
    const std::string text =
        "scene tidal\n"
        "name Tidal Test\n"
        "body sea ocean\n"
        "  bounds -100000 -50 -100000 100000 0 100000\n"
        "  surface 0\n"
        "  density 1025\n"
        "  flags buoyant,conductive\n"
        "  ocean 1 0 12 24 4\n"
        "body river river\n"
        "  bounds 0 -3 0 200 0 20\n"
        "  surface 0\n"
        "  flow 2.5 0 0\n";
    WaterSceneDef scene;
    std::string err;
    ASSERT_TRUE(ParseWaterDefText(text, scene, err)) << err;
    EXPECT_EQ(scene.id, "tidal");
    ASSERT_EQ(scene.bodies.size(), 2u);
    EXPECT_EQ(scene.bodies[0].waves.size(), 4u);         // ocean spectrum produced 4 waves
    EXPECT_NE(scene.bodies[1].flags & WaterCurrent, 0);  // flow implied a current
    const WaterCookResult r = CookWaterCell(scene, 0, 0, 64.0f);
    EXPECT_TRUE(r.ok) << (r.report.Ok() ? "" : ToString(r.report.errors.front().code));
}

TEST(WaterCook, BakeBodyKeepsParamsAndWaves) {
    WaterBuilder b("bake");
    b.AddBody("p", WaterType::River)
        .WithBounds(0, -2, 0, 10, 0, 10)
        .WithSurfaceHeight(-0.5f)
        .WithDensity(1100.0f)
        .WithFlow(1.0f, 0.0f, 2.0f)
        .AddWave(0.2f, 5.0f, 1.0f, 0.0f, 1.0f, 0.1f);
    const WaterBodyInstance baked = BakeBody(b.Def().bodies[0], 7);
    EXPECT_EQ(baked.bodyId, 7u);
    EXPECT_EQ(baked.type, static_cast<uint8_t>(WaterType::River));
    EXPECT_FLOAT_EQ(baked.surfaceHeight, -0.5f);
    EXPECT_FLOAT_EQ(baked.density, 1100.0f);
    EXPECT_EQ(baked.waveCount, 1u);
    EXPECT_NE(baked.flags & WaterCurrent, 0);
}

TEST(WaterCook, FailsClosedWhenCellExceedsBodyCeiling) {
    // A scene-valid layout that piles more than kMaxWaterBodiesPerCell bodies into one cell must FAIL the
    // cook (symmetric with UnpackCell's ceiling) rather than emit a blob the reader would refuse to load.
    WaterBuilder b("dense");
    for (uint32_t i = 0; i <= kMaxWaterBodiesPerCell; ++i) {  // kMaxWaterBodiesPerCell + 1 overlapping pools
        b.AddBody("p" + std::to_string(i), WaterType::Pool)
            .WithBounds(0.0f, -2.0f, 0.0f, 10.0f, 0.0f, 10.0f)
            .WithSurfaceHeight(0.0f);
    }
    const WaterCookResult r = CookWaterCell(b.Take(), 0, 0, 64.0f);
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(r.bytes.empty());
    EXPECT_TRUE(r.report.Has(WaterValidationCode::TooManyBodies));
}

TEST(WaterCook, MergePreservesOtherLayers) {
    namespace S = Next::Streaming;
    // A cell that already carries a (fake) Vegetation layer.
    const std::vector<uint8_t> vegBytes = {1, 2, 3, 4, 5, 6, 7, 8};
    S::LayeredCellChunkInput veg;
    veg.layer = S::CellLayer::Vegetation;
    veg.codec = S::CellFileCompression::None;
    veg.data = vegBytes;
    const std::vector<uint8_t> vegCell = S::PackLayeredCell({veg});

    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / ("next_water_merge_" + std::to_string(stamp) + ".nlc");
    ASSERT_TRUE(WriteWaterCellFile(path.string(), vegCell));

    // Cook water for the same cell and write it MERGED (must NOT clobber the Vegetation layer).
    const WaterCookResult cooked = CookWaterCell(HarborScene(), 0, 0, 64.0f);
    ASSERT_TRUE(cooked.ok);
    ASSERT_TRUE(WriteWaterCellFileMerged(path.string(), cooked.bytes));

    std::vector<uint8_t> file;
    {
        std::ifstream in(path.string(), std::ios::binary | std::ios::ate);
        ASSERT_TRUE(static_cast<bool>(in));
        const std::streamoff n = in.tellg();
        file.resize(static_cast<size_t>(n));
        in.seekg(0);
        in.read(reinterpret_cast<char*>(file.data()), static_cast<std::streamsize>(n));
    }
    std::vector<uint8_t> outVeg;
    std::vector<uint8_t> outWater;
    EXPECT_TRUE(S::ExtractLayer(file.data(), file.size(), S::CellLayer::Vegetation, outVeg));
    EXPECT_EQ(outVeg, vegBytes);  // vegetation layer preserved byte-for-byte
    EXPECT_TRUE(S::ExtractLayer(file.data(), file.size(), S::CellLayer::Water, outWater));  // water added
    EXPECT_FALSE(outWater.empty());

    std::error_code ec;
    std::filesystem::remove(path, ec);
}
