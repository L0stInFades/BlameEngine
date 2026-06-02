#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "next/streaming/streaming_manager.h"
#include "next/vegetation/vegetation_builder.h"
#include "next/vegetation/vegetation_cell.h"
#include "next/vegetation/vegetation_scatter.h"
#include "next/vegetation_world/vegetation_cook.h"

using namespace Next::vegetation;
using Next::Vec3;
using Next::Streaming::CellCoord;
using Next::Streaming::CellData;
using Next::Streaming::CellLayer;
using Next::Streaming::StreamingManager;
using Next::Streaming::StreamingManagerConfig;

namespace {

std::filesystem::path MakeTempDir() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    std::filesystem::path dir = std::filesystem::temp_directory_path() / ("next_veg_stream_" + std::to_string(stamp));
    std::filesystem::create_directories(dir);
    return dir;
}

std::filesystem::path CellPath(const std::filesystem::path& dir, int x, int z) {
    return dir / ("cell_" + std::to_string(x) + "_" + std::to_string(z) + ".nlc");
}

VegetationDef MakeForest() {
    VegetationBuilder b("stream-forest");
    b.WithMasterSeed(7).WithMaxInstancesPerCell(100000);
    b.AddSpecies(101);
    b.WithDensity(0.03f).WithSpacing(2.0f).WithLogicalRadius(1.5f).BlocksLineOfSight();
    return b.Take();
}

StreamingManagerConfig BaseConfig(const std::filesystem::path& dir) {
    StreamingManagerConfig cfg;
    cfg.memoryBudgetMB = 64;
    cfg.loadRadius = 128.0f;
    cfg.unloadRadius = 256.0f;
    cfg.enablePrediction = false;
    cfg.allowPlaceholderCellLoad = true;
    cfg.cellDataDirectory = dir.wstring();
    return cfg;
}

}  // namespace

TEST(VegetationStreaming, LoadCellLayerReadsRealCookedBlob) {
    const std::filesystem::path dir = MakeTempDir();

    StreamingManager mgr;
    ASSERT_TRUE(mgr.Initialize(BaseConfig(dir)));

    // One Update creates+loads cells around the origin camera (placeholder; dir is empty).
    mgr.Update(0.016f, Vec3(0, 0, 0), Vec3(0, 0, -1), Vec3(0, 0, 0));
    const std::vector<CellCoord> loaded = mgr.GetLoadedCells();
    ASSERT_FALSE(loaded.empty());
    const CellCoord coord = loaded.front();

    // Cook a REAL vegetation layer for that cell into the (resolved) streaming directory.
    const std::filesystem::path resolvedDir(mgr.GetConfig().cellDataDirectory);
    std::filesystem::create_directories(resolvedDir);

    const VegetationDef def = MakeForest();
    FlatTerrainSampler terrain;
    const CellData* preCell = mgr.GetCell(coord);
    ASSERT_NE(preCell, nullptr);
    const float cellSize = preCell->metadata.cellSize > 0.0f ? preCell->metadata.cellSize : 64.0f;

    const CookResult cooked = CookVegetationCell(def, terrain, coord.x, coord.z, cellSize);
    ASSERT_TRUE(cooked.ok);
    ASSERT_GT(cooked.instanceCount, 0u);
    ASSERT_TRUE(WriteCellFile(CellPath(resolvedDir, coord.x, coord.z).string(), cooked.bytes));

    // Load the Vegetation layer for real (synchronous framework path).
    mgr.LoadCellLayer(coord, CellLayer::Vegetation);
    EXPECT_TRUE(mgr.IsCellLayerLoaded(coord, CellLayer::Vegetation));

    const CellData* cell = mgr.GetCell(coord);
    ASSERT_NE(cell, nullptr);
    const auto it = cell->layers.find(CellLayer::Vegetation);
    ASSERT_NE(it, cell->layers.end());
    ASSERT_NE(it->second.data, nullptr);  // real bytes, not a placeholder
    ASSERT_GT(it->second.size, 0u);

    // The loaded bytes parse back to the EXACT cooked instances.
    VegetationCellData parsed;
    ASSERT_TRUE(UnpackCell(static_cast<const uint8_t*>(it->second.data), it->second.size, parsed));
    EXPECT_EQ(parsed.instances.size(), cooked.instanceCount);

    const std::vector<VegetationInstance> direct = ScatterCell(def, terrain, coord.x, coord.z, cellSize);
    ASSERT_EQ(parsed.instances.size(), direct.size());
    EXPECT_EQ(0, std::memcmp(parsed.instances.data(), direct.data(), direct.size() * sizeof(VegetationInstance)));

    mgr.Shutdown();
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST(VegetationStreaming, FailsClosedWhenLayerMissing) {
    const std::filesystem::path dir = MakeTempDir();

    StreamingManager mgr;
    StreamingManagerConfig cfg = BaseConfig(dir);
    ASSERT_TRUE(mgr.Initialize(cfg));

    mgr.Update(0.016f, Vec3(0, 0, 0), Vec3(0, 0, -1), Vec3(0, 0, 0));
    const std::vector<CellCoord> loaded = mgr.GetLoadedCells();
    ASSERT_FALSE(loaded.empty());
    const CellCoord coord = loaded.front();

    // No .nlc cooked. Flip to fail-closed; loading the missing layer must NOT create a placeholder.
    cfg.allowPlaceholderCellLoad = false;
    mgr.SetConfig(cfg);
    mgr.LoadCellLayer(coord, CellLayer::Vegetation);
    EXPECT_FALSE(mgr.IsCellLayerLoaded(coord, CellLayer::Vegetation));

    mgr.Shutdown();
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST(VegetationStreaming, PlaceholderLayerHasNoData) {
    const std::filesystem::path dir = MakeTempDir();

    StreamingManager mgr;
    ASSERT_TRUE(mgr.Initialize(BaseConfig(dir)));  // placeholders ON
    mgr.Update(0.016f, Vec3(0, 0, 0), Vec3(0, 0, -1), Vec3(0, 0, 0));
    const std::vector<CellCoord> loaded = mgr.GetLoadedCells();
    ASSERT_FALSE(loaded.empty());
    const CellCoord coord = loaded.front();

    // No cooked file + placeholders ON -> a placeholder layer is created, but it carries NO real bytes.
    mgr.LoadCellLayer(coord, CellLayer::Vegetation);
    const CellData* cell = mgr.GetCell(coord);
    ASSERT_NE(cell, nullptr);
    const auto it = cell->layers.find(CellLayer::Vegetation);
    ASSERT_NE(it, cell->layers.end());
    EXPECT_EQ(it->second.data, nullptr);  // placeholder, distinguishable from a real load
    EXPECT_EQ(it->second.size, 0u);

    mgr.Shutdown();
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}
