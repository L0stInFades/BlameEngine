#include <gtest/gtest.h>

#include "next/streaming/interest_manager.h"
#include "next/streaming/streaming_manager.h"
#include "next/streaming/world_partition.h"

TEST(InterestManager, CameraFrustumInfluencesInterest) {
    using namespace Next::Streaming;
    using Next::Vec3;

    WorldPartition wp;
    WorldPartitionConfig wcfg;
    wcfg.cellSize = 64.0f;
    ASSERT_TRUE(wp.Initialize(wcfg));

    InterestManager im;
    InterestManagerConfig cfg;
    cfg.enablePrediction = false;
    ASSERT_TRUE(im.Initialize(cfg));
    im.SetWorldPartition(&wp);

    // Camera at origin looking +Z.
    im.SetCameraPosition(Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, 1.0f));

    const float front = im.CalculateCellInterest(CellCoord{0, 1});
    const float back = im.CalculateCellInterest(CellCoord{0, -1});

    EXPECT_GT(front, 0.0f);
    EXPECT_LE(back, front * 0.25f + 1e-6f);
    im.Update(0.016f);
    const InterestManager::Statistics stats = im.GetStatistics();
    EXPECT_TRUE(stats.HasActiveInterestPoints());
    EXPECT_TRUE(stats.HasInterestCells());

    im.Shutdown();
    wp.Shutdown();
}

TEST(WorldStreaming, PredictiveStreamingQueuesCells) {
    using namespace Next::Streaming;
    using Next::Vec3;

    StreamingManager mgr;
    StreamingManagerConfig cfg;
    cfg.memoryBudgetMB = 64;
    cfg.loadRadius = 0.0f;           // rely on prediction only
    cfg.unloadRadius = 256.0f;
    cfg.prefetchRadius = 128.0f;
    cfg.maxConcurrentLoads = 64;
    cfg.maxConcurrentUnloads = 64;
    cfg.enablePrediction = true;
    cfg.predictionTime = 1.0f;
    cfg.predictionSamples = 2;
    cfg.allowPlaceholderCellLoad = true;

    ASSERT_TRUE(mgr.Initialize(cfg));

    mgr.Update(0.016f, Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 0.0f, 0.0f), Vec3(200.0f, 0.0f, 0.0f));
    auto loaded = mgr.GetLoadedCells();
    EXPECT_FALSE(loaded.empty());

    mgr.Shutdown();
}
