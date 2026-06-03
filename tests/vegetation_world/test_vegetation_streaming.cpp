#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
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

// LoadCellLayer is async (ADR-0014): pump the streaming Update loop (which drives asyncIO_->Update() and thus
// the main-thread layer commit) until the layer loads, or bail after a bounded wait. The manager runs real IO
// worker threads, so the 1ms yield is load-bearing.
bool PumpUntilLayerLoaded(StreamingManager& mgr, const CellCoord& coord, CellLayer layer) {
    for (int i = 0; i < 3000 && !mgr.IsCellLayerLoaded(coord, layer); ++i) {
        mgr.Update(0.016f, Vec3(0, 0, 0), Vec3(0, 0, -1), Vec3(0, 0, 0));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return mgr.IsCellLayerLoaded(coord, layer);
}

// Pump until every async layer read has drained (for unload/abandon/absence assertions).
void PumpUntilLayersQuiescent(StreamingManager& mgr) {
    for (int i = 0; i < 3000 && mgr.PendingLayerLoadCount() != 0; ++i) {
        mgr.Update(0.016f, Vec3(0, 0, 0), Vec3(0, 0, -1), Vec3(0, 0, 0));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

// Pump until streaming memory has settled (neighbour cells all loaded) and no async layer read is in flight, so
// a subsequent single-layer load's memory delta is attributable to that layer alone.
void PumpUntilMemoryStable(StreamingManager& mgr) {
    size_t last = mgr.GetMemoryUsage();
    int stable = 0;
    for (int i = 0; i < 3000 && stable < 8; ++i) {
        mgr.Update(0.016f, Vec3(0, 0, 0), Vec3(0, 0, -1), Vec3(0, 0, 0));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        const size_t now = mgr.GetMemoryUsage();
        stable = (now == last && mgr.PendingLayerLoadCount() == 0) ? stable + 1 : 0;
        last = now;
    }
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

    // Load the Vegetation layer for real (async framework path, ADR-0014): kick the read, then pump Update()
    // until the main-thread commit lands.
    mgr.LoadCellLayer(coord, CellLayer::Vegetation);
    EXPECT_TRUE(PumpUntilLayerLoaded(mgr, coord, CellLayer::Vegetation));

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

TEST(VegetationStreaming, MemoryAccountingTracksVegetationLayer) {
    const std::filesystem::path dir = MakeTempDir();

    StreamingManager mgr;
    ASSERT_TRUE(mgr.Initialize(BaseConfig(dir)));
    mgr.Update(0.016f, Vec3(0, 0, 0), Vec3(0, 0, -1), Vec3(0, 0, 0));
    const std::vector<CellCoord> loaded = mgr.GetLoadedCells();
    ASSERT_FALSE(loaded.empty());
    const CellCoord coord = loaded.front();

    const std::filesystem::path resolvedDir(mgr.GetConfig().cellDataDirectory);
    std::filesystem::create_directories(resolvedDir);
    FlatTerrainSampler terrain;
    const CookResult cooked = CookVegetationCell(MakeForest(), terrain, coord.x, coord.z, 64.0f);
    ASSERT_TRUE(cooked.ok);
    ASSERT_TRUE(WriteCellFile(CellPath(resolvedDir, coord.x, coord.z).string(), cooked.bytes));

    // Async load (ADR-0014): settle the streaming memory first so the only delta we measure is the vegetation
    // layer's own bytes, then load + pump until the main-thread commit.
    PumpUntilMemoryStable(mgr);
    const size_t baseline = mgr.GetMemoryUsage();
    mgr.LoadCellLayer(coord, CellLayer::Vegetation);
    ASSERT_TRUE(PumpUntilLayerLoaded(mgr, coord, CellLayer::Vegetation));
    const size_t afterLoad = mgr.GetMemoryUsage();

    const CellData* cell = mgr.GetCell(coord);
    ASSERT_NE(cell, nullptr);
    const auto it = cell->layers.find(CellLayer::Vegetation);
    ASSERT_NE(it, cell->layers.end());
    // The streaming budget now sees exactly the vegetation layer's bytes (it didn't before this fix).
    EXPECT_EQ(afterLoad - baseline, it->second.size);
    EXPECT_GT(it->second.size, 0u);

    mgr.UnloadCellLayer(coord, CellLayer::Vegetation);
    EXPECT_EQ(mgr.GetMemoryUsage(), baseline);  // freed back to baseline

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

TEST(VegetationStreaming, LoadsVegetationLayerViaAsyncIO) {
    const std::filesystem::path dir = MakeTempDir();
    StreamingManager mgr;
    ASSERT_TRUE(mgr.Initialize(BaseConfig(dir)));
    mgr.Update(0.016f, Vec3(0, 0, 0), Vec3(0, 0, -1), Vec3(0, 0, 0));
    const std::vector<CellCoord> loaded = mgr.GetLoadedCells();
    ASSERT_FALSE(loaded.empty());
    const CellCoord coord = loaded.front();

    const std::filesystem::path resolvedDir(mgr.GetConfig().cellDataDirectory);
    std::filesystem::create_directories(resolvedDir);
    FlatTerrainSampler terrain;
    const CookResult cooked = CookVegetationCell(MakeForest(), terrain, coord.x, coord.z, 64.0f);
    ASSERT_TRUE(cooked.ok);
    ASSERT_TRUE(WriteCellFile(CellPath(resolvedDir, coord.x, coord.z).string(), cooked.bytes));

    // Settle StaticMesh IO, then load the vegetation layer: it must travel through the async IO system, so the
    // cumulative bytes-read counter grows by (at least) the whole .nlc file. This is what distinguishes the new
    // async path from the old synchronous ifstream side-path.
    PumpUntilMemoryStable(mgr);
    const uint64_t readBefore = mgr.GetIOStatistics().totalBytesRead;
    mgr.LoadCellLayer(coord, CellLayer::Vegetation);
    ASSERT_TRUE(PumpUntilLayerLoaded(mgr, coord, CellLayer::Vegetation));
    const uint64_t readAfter = mgr.GetIOStatistics().totalBytesRead;
    EXPECT_GE(readAfter - readBefore, static_cast<uint64_t>(cooked.bytes.size()));

    mgr.Shutdown();
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST(VegetationStreaming, UnloadDuringAsyncLoadIsSafe) {
    const std::filesystem::path dir = MakeTempDir();
    StreamingManager mgr;
    ASSERT_TRUE(mgr.Initialize(BaseConfig(dir)));
    mgr.Update(0.016f, Vec3(0, 0, 0), Vec3(0, 0, -1), Vec3(0, 0, 0));
    const std::vector<CellCoord> loaded = mgr.GetLoadedCells();
    ASSERT_FALSE(loaded.empty());
    const CellCoord coord = loaded.front();

    const std::filesystem::path resolvedDir(mgr.GetConfig().cellDataDirectory);
    std::filesystem::create_directories(resolvedDir);
    FlatTerrainSampler terrain;
    const CookResult cooked = CookVegetationCell(MakeForest(), terrain, coord.x, coord.z, 64.0f);
    ASSERT_TRUE(cooked.ok);
    ASSERT_TRUE(WriteCellFile(CellPath(resolvedDir, coord.x, coord.z).string(), cooked.bytes));

    // Kick the async read and abandon it the SAME frame, before pumping the commit. The abandoned read still
    // completes; FinishLayerLoad must drop the result and reclaim the buffer with no use-after-free (ASan).
    mgr.LoadCellLayer(coord, CellLayer::Vegetation);
    mgr.UnloadCellLayer(coord, CellLayer::Vegetation);
    PumpUntilLayersQuiescent(mgr);
    EXPECT_FALSE(mgr.IsCellLayerLoaded(coord, CellLayer::Vegetation));
    EXPECT_EQ(mgr.PendingLayerLoadCount(), 0u);

    mgr.Shutdown();
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST(VegetationStreaming, ShutdownDuringAsyncLoadIsSafe) {
    const std::filesystem::path dir = MakeTempDir();
    StreamingManager mgr;
    ASSERT_TRUE(mgr.Initialize(BaseConfig(dir)));
    mgr.Update(0.016f, Vec3(0, 0, 0), Vec3(0, 0, -1), Vec3(0, 0, 0));
    const std::vector<CellCoord> loaded = mgr.GetLoadedCells();
    ASSERT_FALSE(loaded.empty());
    const CellCoord coord = loaded.front();

    const std::filesystem::path resolvedDir(mgr.GetConfig().cellDataDirectory);
    std::filesystem::create_directories(resolvedDir);
    FlatTerrainSampler terrain;
    const CookResult cooked = CookVegetationCell(MakeForest(), terrain, coord.x, coord.z, 64.0f);
    ASSERT_TRUE(cooked.ok);
    ASSERT_TRUE(WriteCellFile(CellPath(resolvedDir, coord.x, coord.z).string(), cooked.bytes));

    // Kick the read, then Shutdown immediately without pumping the commit. Shutdown joins the IO workers BEFORE
    // it clears the layer ops, so the in-flight read buffer is freed only after the worker stops (no UAF; ASan).
    mgr.LoadCellLayer(coord, CellLayer::Vegetation);
    mgr.Shutdown();

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST(VegetationStreaming, EvictionDuringAsyncLoadIsSafe) {
    const std::filesystem::path dir = MakeTempDir();
    StreamingManager mgr;
    ASSERT_TRUE(mgr.Initialize(BaseConfig(dir)));
    mgr.Update(0.016f, Vec3(0, 0, 0), Vec3(0, 0, -1), Vec3(0, 0, 0));
    const std::vector<CellCoord> loaded = mgr.GetLoadedCells();
    ASSERT_FALSE(loaded.empty());
    const CellCoord coord = loaded.front();

    const std::filesystem::path resolvedDir(mgr.GetConfig().cellDataDirectory);
    std::filesystem::create_directories(resolvedDir);
    FlatTerrainSampler terrain;
    const CookResult cooked = CookVegetationCell(MakeForest(), terrain, coord.x, coord.z, 64.0f);
    ASSERT_TRUE(cooked.ok);
    ASSERT_TRUE(WriteCellFile(CellPath(resolvedDir, coord.x, coord.z).string(), cooked.bytes));

    // Kick the read, then drag the camera far away so the whole cell is evicted while its vegetation read may
    // still be in flight. ProcessCellUnload must abandon the layer op the same safe way UnloadCellLayer does.
    mgr.LoadCellLayer(coord, CellLayer::Vegetation);
    for (int i = 0; i < 400 && mgr.PendingLayerLoadCount() != 0; ++i) {
        mgr.Update(0.016f, Vec3(100000.0f, 0, 100000.0f), Vec3(0, 0, -1), Vec3(0, 0, 0));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    EXPECT_EQ(mgr.PendingLayerLoadCount(), 0u);
    EXPECT_FALSE(mgr.IsCellLayerLoaded(coord, CellLayer::Vegetation));

    mgr.Shutdown();
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}
