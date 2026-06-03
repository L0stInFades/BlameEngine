#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

#include "next/streaming/streaming_manager.h"
#include "next/vegetation/vegetation_builder.h"
#include "next/vegetation_world/vegetation_cook.h"
#include "next/vegetation_world/vegetation_streaming_system.h"

using namespace Next::vegetation;
using Next::Vec3;
using Next::Streaming::CellCoord;
using Next::Streaming::CellLayer;
using Next::Streaming::StreamingManager;
using Next::Streaming::StreamingManagerConfig;

namespace {

std::filesystem::path MakeTempDir() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    std::filesystem::path dir = std::filesystem::temp_directory_path() / ("next_veg_sys_" + std::to_string(stamp));
    std::filesystem::create_directories(dir);
    return dir;
}

VegetationDef Forest() {
    VegetationBuilder b("sys-forest");
    b.WithMasterSeed(11).WithMaxInstancesPerCell(100000);
    b.AddSpecies(101);
    b.WithDensity(0.03f).WithSpacing(2.0f).WithLogicalRadius(1.5f);
    return b.Take();
}

}  // namespace

TEST(VegetationStreamingSystem, AutoIngestsAndEvictsFromStreaming) {
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

    // Cook + write the cell's vegetation, then stream the layer in.
    const std::filesystem::path resolvedDir(mgr.GetConfig().cellDataDirectory);
    std::filesystem::create_directories(resolvedDir);
    FlatTerrainSampler terrain;
    const CookResult cooked = CookVegetationCell(Forest(), terrain, coord.x, coord.z, 64.0f);
    ASSERT_TRUE(cooked.ok);
    ASSERT_TRUE(WriteCellFile(
        (resolvedDir / ("cell_" + std::to_string(coord.x) + "_" + std::to_string(coord.z) + ".nlc")).string(),
        cooked.bytes));
    mgr.LoadCellLayer(coord, CellLayer::Vegetation);

    // The system maintains the store with NO manual store.LoadCell/UnloadCell.
    VegetationStreamingSystem sys;
    EXPECT_EQ(sys.Store().LoadedCellCount(), 0u);  // nothing until the first Sync

    const size_t ingested = sys.Sync(mgr);
    EXPECT_GE(ingested, 1u);
    EXPECT_TRUE(sys.Store().IsCellLoaded(coord.x, coord.z));
    EXPECT_GT(sys.Store().LiveInstanceCount(), 0u);
    EXPECT_EQ(sys.TrackedCellCount(), sys.Store().LoadedCellCount());

    EXPECT_EQ(sys.Sync(mgr), 0u);  // idempotent: already in sync

    // Stream the layer out -> the next Sync evicts it automatically.
    mgr.UnloadCellLayer(coord, CellLayer::Vegetation);
    const size_t evicted = sys.Sync(mgr);
    EXPECT_GE(evicted, 1u);
    EXPECT_FALSE(sys.Store().IsCellLoaded(coord.x, coord.z));
    EXPECT_EQ(sys.Store().LiveInstanceCount(), 0u);
    EXPECT_EQ(sys.TrackedCellCount(), 0u);

    mgr.Shutdown();
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST(VegetationStreamingSystem, ReingestsOnLayerReload) {
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

    const std::filesystem::path resolvedDir(mgr.GetConfig().cellDataDirectory);
    std::filesystem::create_directories(resolvedDir);
    const std::string path =
        (resolvedDir / ("cell_" + std::to_string(coord.x) + "_" + std::to_string(coord.z) + ".nlc")).string();
    FlatTerrainSampler terrain;

    // First: a dense forest, ingested by Sync.
    const CookResult dense = CookVegetationCell(Forest(), terrain, coord.x, coord.z, 64.0f);
    ASSERT_TRUE(dense.ok);
    ASSERT_TRUE(WriteCellFile(path, dense.bytes));
    mgr.LoadCellLayer(coord, CellLayer::Vegetation);

    VegetationStreamingSystem sys;
    sys.Sync(mgr);
    const size_t count1 = sys.Store().LiveInstanceCount();
    ASSERT_GT(count1, 0u);
    EXPECT_EQ(count1, dense.instanceCount);

    // Reload the SAME cell's layer with a sparser def (different count -> different byte size).
    VegetationBuilder sparseBuilder("sparse-forest");
    sparseBuilder.WithMasterSeed(11).WithMaxInstancesPerCell(100000);
    sparseBuilder.AddSpecies(101);
    sparseBuilder.WithDensity(0.005f).WithSpacing(6.0f).WithLogicalRadius(1.5f);
    const CookResult sparse = CookVegetationCell(sparseBuilder.Take(), terrain, coord.x, coord.z, 64.0f);
    ASSERT_TRUE(sparse.ok);
    ASSERT_NE(sparse.instanceCount, dense.instanceCount);

    mgr.UnloadCellLayer(coord, CellLayer::Vegetation);
    ASSERT_TRUE(WriteCellFile(path, sparse.bytes));
    mgr.LoadCellLayer(coord, CellLayer::Vegetation);

    // Without the (ptr,size) content token, Sync would see the coord already present and keep stale data.
    const size_t reingested = sys.Sync(mgr);
    EXPECT_GE(reingested, 1u);
    const size_t count2 = sys.Store().LiveInstanceCount();
    EXPECT_EQ(count2, sparse.instanceCount);
    EXPECT_NE(count2, count1);

    mgr.Shutdown();
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST(VegetationStreamingSystem, ReingestsOnSameSizeReload) {
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

    const std::filesystem::path resolvedDir(mgr.GetConfig().cellDataDirectory);
    std::filesystem::create_directories(resolvedDir);
    const std::string path =
        (resolvedDir / ("cell_" + std::to_string(coord.x) + "_" + std::to_string(coord.z) + ".nlc")).string();
    FlatTerrainSampler terrain;

    // Two defs identical except the seed; spacing 0 makes the instance count seed-independent, so the
    // cooked blobs are the SAME size with DIFFERENT content -> exactly the (ptr,size) token's blind spot.
    auto cookSeed = [&](uint64_t seed) {
        VegetationBuilder b("same-size");
        b.WithMasterSeed(seed).WithMaxInstancesPerCell(100000);
        b.AddSpecies(101);
        b.WithDensity(0.03f).WithSpacing(0.0f);
        return CookVegetationCell(b.Take(), terrain, coord.x, coord.z, 64.0f);
    };
    const CookResult a = cookSeed(1);
    const CookResult bb = cookSeed(999);
    ASSERT_TRUE(a.ok);
    ASSERT_TRUE(bb.ok);
    ASSERT_EQ(a.bytes.size(), bb.bytes.size());  // same byte size
    ASSERT_EQ(a.instanceCount, bb.instanceCount);

    ASSERT_TRUE(WriteCellFile(path, a.bytes));
    mgr.LoadCellLayer(coord, CellLayer::Vegetation);
    VegetationStreamingSystem sys;
    sys.Sync(mgr);
    const VegetationInstance* before = sys.Store().Find(VegetationKey{coord.x, coord.z, 0});
    ASSERT_NE(before, nullptr);
    const float beforeX = before->position[0];

    mgr.UnloadCellLayer(coord, CellLayer::Vegetation);
    ASSERT_TRUE(WriteCellFile(path, bb.bytes));
    mgr.LoadCellLayer(coord, CellLayer::Vegetation);
    const size_t changed = sys.Sync(mgr);
    EXPECT_GE(changed, 1u);  // generation bumped -> re-ingested despite identical byte size
    const VegetationInstance* after = sys.Store().Find(VegetationKey{coord.x, coord.z, 0});
    ASSERT_NE(after, nullptr);
    EXPECT_NE(after->position[0], beforeX);  // store now reflects the NEW content, not stale

    mgr.Shutdown();
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}
