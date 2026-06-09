// Water streaming integration (ADR-0015): cook a layered Water cell -> StreamingManager loads the Water
// layer via REAL async IO -> WaterStreamingSystem.Sync ingests it into the store; eviction + idempotent
// Sync verified. Mirrors the vegetation streaming path.

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include "next/streaming/streaming_manager.h"
#include "next/water/water_builder.h"
#include "next/water_world/water_cook.h"
#include "next/water_world/water_streaming_system.h"

using namespace Next::water;
using Next::Vec3;
using Next::Streaming::CellCoord;
using Next::Streaming::CellData;
using Next::Streaming::CellLayer;
using Next::Streaming::StreamingManager;
using Next::Streaming::StreamingManagerConfig;

namespace {

std::filesystem::path MakeTempDir() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    std::filesystem::path dir = std::filesystem::temp_directory_path() / ("next_water_stream_" + std::to_string(stamp));
    std::filesystem::create_directories(dir);
    return dir;
}

bool PumpUntilLayerLoaded(StreamingManager& mgr, const CellCoord& coord, CellLayer layer) {
    for (int i = 0; i < 3000 && !mgr.IsCellLayerLoaded(coord, layer); ++i) {
        mgr.Update(0.016f, Vec3(0, 0, 0), Vec3(0, 0, -1), Vec3(0, 0, 0));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return mgr.IsCellLayerLoaded(coord, layer);
}

WaterSceneDef OceanScene() {
    WaterBuilder b("stream-sea");
    b.AddBody("sea", WaterType::Ocean)
        .WithBounds(-100000.0f, -50.0f, -100000.0f, 100000.0f, 0.0f, 100000.0f)
        .WithSurfaceHeight(0.0f);
    return b.Take();
}

}  // namespace

TEST(WaterStreaming, CookStreamSyncIngestAndEvict) {
    const std::filesystem::path dir = MakeTempDir();

    StreamingManager mgr;
    StreamingManagerConfig cfg;
    cfg.memoryBudgetMB = 64;
    cfg.loadRadius = 128.0f;
    cfg.unloadRadius = 256.0f;
    cfg.enablePrediction = false;
    cfg.allowPlaceholderCellLoad = true;
    cfg.cellDataDirectory = dir.wstring();
    ASSERT_TRUE(mgr.Initialize(cfg));
    mgr.Update(0.016f, Vec3(0, 0, 0), Vec3(0, 0, -1), Vec3(0, 0, 0));
    const std::vector<CellCoord> loaded = mgr.GetLoadedCells();
    ASSERT_FALSE(loaded.empty());
    const CellCoord coord = loaded.front();
    const CellData* preCell = mgr.GetCell(coord);
    ASSERT_NE(preCell, nullptr);
    const float cellSize = preCell->metadata.cellSize > 0.0f ? preCell->metadata.cellSize : 64.0f;

    const WaterCookResult cooked = CookWaterCell(OceanScene(), coord.x, coord.z, cellSize);
    ASSERT_TRUE(cooked.ok);
    ASSERT_GE(cooked.bodyCount, 1u);

    const std::filesystem::path resolvedDir(mgr.GetConfig().cellDataDirectory);
    std::filesystem::create_directories(resolvedDir);
    const std::filesystem::path cellFile =
        resolvedDir / ("cell_" + std::to_string(coord.x) + "_" + std::to_string(coord.z) + ".nlc");
    ASSERT_TRUE(WriteWaterCellFile(cellFile.string(), cooked.bytes));

    mgr.LoadCellLayer(coord, CellLayer::Water);
    ASSERT_TRUE(PumpUntilLayerLoaded(mgr, coord, CellLayer::Water));

    WaterStreamingSystem wss;
    const size_t changed = wss.Sync(mgr);
    EXPECT_GE(changed, 1u);
    EXPECT_GE(wss.Store().BodyCount(), 1u);
    EXPECT_EQ(wss.TrackedCellCount(), 1u);

    // Idempotent: nothing changed since the last Sync.
    EXPECT_EQ(wss.Sync(mgr), 0u);

    // Evict: drop the layer, Sync removes it from the store.
    mgr.UnloadCellLayer(coord, CellLayer::Water);
    const size_t evicted = wss.Sync(mgr);
    EXPECT_GE(evicted, 1u);
    EXPECT_EQ(wss.Store().BodyCount(), 0u);
    EXPECT_EQ(wss.TrackedCellCount(), 0u);

    mgr.Shutdown();
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}
