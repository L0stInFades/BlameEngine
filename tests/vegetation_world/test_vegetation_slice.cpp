// End-to-end vegetation vertical slice (ADR-0014). Proves the WHOLE chain in one test:
//
//   cook (def + terrain -> validate -> scatter -> pack -> layered cell file)
//     -> StreamingManager::LoadCellLayer(Vegetation) reads the real .nlc blob
//       -> VegetationStore indexes it (authoritative)         } same bytes, same instances
//       -> MockVegetationConsumer mirrors it (UE5 stand-in)   }
//     -> gameplay: line-of-sight is blocked by vegetation cover
//     -> destruction: store removes + boundary GameEvent + consumer mirror update stay in sync
//     -> unload: every stage drops the cell.
//
// If this passes from a clean build, the vegetation system is wired end to end (not just unit-green).

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

#include "next/streaming/streaming_manager.h"
#include "next/vegetation/vegetation_builder.h"
#include "next/vegetation/vegetation_cell.h"
#include "next/vegetation_world/vegetation_cook.h"
#include "next/vegetation_world/vegetation_query.h"
#include "next/vegetation_world/vegetation_store.h"
#include "next/vegetation_world/vegetation_view.h"

using namespace Next::vegetation;
namespace boundary = Next::boundary;
using Next::Vec3;
using Next::Streaming::CellCoord;
using Next::Streaming::CellData;
using Next::Streaming::CellLayer;
using Next::Streaming::StreamingManager;
using Next::Streaming::StreamingManagerConfig;

namespace {

std::filesystem::path MakeTempDir() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    std::filesystem::path dir = std::filesystem::temp_directory_path() / ("next_veg_slice_" + std::to_string(stamp));
    std::filesystem::create_directories(dir);
    return dir;
}

VegetationDef Forest() {  // destructible + LOS-blocking, so the slice exercises both
    VegetationBuilder b("slice-forest");
    b.WithMasterSeed(2026).WithMaxInstancesPerCell(100000);
    b.AddSpecies(101);
    b.WithDensity(0.03f).WithSpacing(2.0f).WithLogicalRadius(1.5f).BlocksLineOfSight().Destructible();
    return b.Take();
}

}  // namespace

TEST(VegetationSlice, CookStreamStoreQueryDestroyMirror) {
    const std::filesystem::path dir = MakeTempDir();

    // ---- streaming up; one Update creates the cell grid around the origin ----
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

    // ---- STAGE 1: cook a real vegetation cell into the streaming directory ----
    const VegetationDef def = Forest();
    FlatTerrainSampler terrain;
    const CookResult cooked = CookVegetationCell(def, terrain, coord.x, coord.z, cellSize);
    ASSERT_TRUE(cooked.ok);
    ASSERT_GT(cooked.instanceCount, 2u);

    const std::filesystem::path resolvedDir(mgr.GetConfig().cellDataDirectory);
    std::filesystem::create_directories(resolvedDir);
    const std::filesystem::path cellFile =
        resolvedDir / ("cell_" + std::to_string(coord.x) + "_" + std::to_string(coord.z) + ".nlc");
    ASSERT_TRUE(WriteCellFile(cellFile.string(), cooked.bytes));

    // ---- STAGE 2: streaming loads the Vegetation layer via real IO ----
    mgr.LoadCellLayer(coord, CellLayer::Vegetation);
    ASSERT_TRUE(mgr.IsCellLayerLoaded(coord, CellLayer::Vegetation));
    const CellData* cell = mgr.GetCell(coord);
    ASSERT_NE(cell, nullptr);
    const auto layerIt = cell->layers.find(CellLayer::Vegetation);
    ASSERT_NE(layerIt, cell->layers.end());
    ASSERT_NE(layerIt->second.data, nullptr);
    const uint8_t* layerBytes = static_cast<const uint8_t*>(layerIt->second.data);
    const size_t layerSize = layerIt->second.size;

    // ---- STAGE 3: the SAME streamed bytes feed both the authoritative store and the UE5 mirror ----
    VegetationStore store;
    ASSERT_TRUE(store.LoadCell(coord.x, coord.z, layerBytes, layerSize));
    MockVegetationConsumer consumer;
    ASSERT_TRUE(consumer.OnCellLoaded(coord.x, coord.z, layerBytes, layerSize));

    EXPECT_EQ(store.LiveInstanceCount(), cooked.instanceCount);
    EXPECT_EQ(consumer.TotalInstanceCount(), cooked.instanceCount);         // sim and view agree
    EXPECT_EQ(consumer.InstanceCountForVisual(101), cooked.instanceCount);  // one species -> one HISM bucket
    EXPECT_EQ(consumer.VisualBucketCount(), 1u);

    // ---- STAGE 4: gameplay LOS is blocked by vegetation cover ----
    const std::vector<VegetationKey> blockers = store.AllLive(VegBlocksLineOfSight);
    ASSERT_FALSE(blockers.empty());
    const VegetationKey blockerKey = blockers.front();
    const VegetationInstance* blocker = store.Find(blockerKey);
    ASSERT_NE(blocker, nullptr);
    const float bx = blocker->position[0];
    const float bz = blocker->position[2];
    // A segment passing through the blocker's center is occluded; a far-away one is not.
    EXPECT_TRUE(SegmentBlockedByVegetation(store, bx - 5.0f, bz, bx + 5.0f, bz));
    EXPECT_FALSE(SegmentBlockedByVegetation(store, bx, bz + 1.0e6f, bx + 1.0f, bz + 1.0e6f));

    // The same blocker also occludes the Game API raycast (composed query).
    VegetationWorldQuery worldQuery(&store);
    const float origin[3] = {bx - 8.0f, blocker->position[1], bz};
    const float xdir[3] = {1.0f, 0.0f, 0.0f};
    EXPECT_EQ(worldQuery.Raycast(origin, xdir, 32.0f).hit, 1u);

    // ---- STAGE 5: destruction keeps sim authority and the UE5 mirror in lockstep ----
    const size_t before = store.LiveInstanceCount();
    VegetationDestroyedEvent ev;
    ASSERT_TRUE(DestroyVegetation(store, blockerKey, ev));
    const boundary::GameEvent cue = ToBoundaryEvent(ev);  // sim -> UE5 cosmetic event
    EXPECT_EQ(cue.type, kVegEventInstanceDestroyed);
    EXPECT_EQ(cue.subject, static_cast<boundary::EntityId>(blocker->visual));
    EXPECT_TRUE(consumer.OnInstanceDestroyed(blockerKey.cellX, blockerKey.cellZ, blockerKey.instanceId));

    EXPECT_EQ(store.LiveInstanceCount(), before - 1);
    EXPECT_EQ(consumer.TotalInstanceCount(), store.LiveInstanceCount());  // still in sync
    EXPECT_EQ(store.Find(blockerKey), nullptr);

    // ---- STAGE 6: unload drops the cell everywhere ----
    mgr.UnloadCellLayer(coord, CellLayer::Vegetation);
    store.UnloadCell(coord.x, coord.z);
    consumer.OnCellUnloaded(coord.x, coord.z);
    EXPECT_FALSE(mgr.IsCellLayerLoaded(coord, CellLayer::Vegetation));
    EXPECT_EQ(store.LiveInstanceCount(), 0u);
    EXPECT_EQ(consumer.TotalInstanceCount(), 0u);

    mgr.Shutdown();
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}
