#include "metal_release_queue.h"

#include <gtest/gtest.h>

#import <Foundation/Foundation.h>

namespace Next {
namespace MetalBackend {
namespace testing {

TEST(MetalReleaseQueueTest, TracksLatencyAndCollectionStats) {
    MetalReleaseQueue queue;
    queue.Initialize(0);

    MetalReleaseQueueStats stats = queue.GetStats();
    EXPECT_EQ(queue.GetCollectLatency(), 1u);
    EXPECT_EQ(stats.collectLatency, 1u);
    EXPECT_EQ(stats.pendingObjectCount, 0u);
    EXPECT_EQ(stats.peakPendingObjectCount, 0u);

    @autoreleasepool {
        NSObject* first = [[NSObject alloc] init];
        NSObject* second = [[NSObject alloc] init];
        queue.Queue(first, 10);
        queue.Queue(nil, 10);
        queue.Queue(second, 11);
    }

    stats = queue.GetStats();
    EXPECT_EQ(stats.pendingObjectCount, 2u);
    EXPECT_EQ(stats.peakPendingObjectCount, 2u);
    EXPECT_EQ(stats.queuedObjectCount, 2u);
    EXPECT_EQ(stats.collectedObjectCount, 0u);

    queue.Collect(10);
    stats = queue.GetStats();
    EXPECT_EQ(stats.pendingObjectCount, 2u);
    EXPECT_EQ(stats.collectedObjectCount, 0u);
    EXPECT_EQ(stats.collectPassCount, 1u);

    queue.Collect(11);
    stats = queue.GetStats();
    EXPECT_EQ(stats.pendingObjectCount, 1u);
    EXPECT_EQ(stats.collectedObjectCount, 1u);
    EXPECT_EQ(stats.collectPassCount, 2u);

    queue.Collect(12);
    stats = queue.GetStats();
    EXPECT_EQ(stats.pendingObjectCount, 0u);
    EXPECT_EQ(stats.peakPendingObjectCount, 2u);
    EXPECT_EQ(stats.queuedObjectCount, 2u);
    EXPECT_EQ(stats.collectedObjectCount, 2u);
    EXPECT_EQ(stats.collectPassCount, 3u);
    EXPECT_EQ(stats.forcedCollectPassCount, 0u);
}

TEST(MetalReleaseQueueTest, TracksForcedCollectionStats) {
    MetalReleaseQueue queue;
    queue.Initialize(3);

    @autoreleasepool {
        NSObject* object = [[NSObject alloc] init];
        queue.Queue(object, 4);
    }

    queue.Collect(5);
    MetalReleaseQueueStats stats = queue.GetStats();
    EXPECT_EQ(stats.pendingObjectCount, 1u);
    EXPECT_EQ(stats.collectedObjectCount, 0u);
    EXPECT_EQ(stats.collectPassCount, 1u);
    EXPECT_EQ(stats.forcedCollectPassCount, 0u);

    queue.Collect(5, true);
    stats = queue.GetStats();
    EXPECT_EQ(stats.pendingObjectCount, 0u);
    EXPECT_EQ(stats.queuedObjectCount, 1u);
    EXPECT_EQ(stats.collectedObjectCount, 1u);
    EXPECT_EQ(stats.collectPassCount, 2u);
    EXPECT_EQ(stats.forcedCollectPassCount, 1u);

    queue.Collect(5, true);
    stats = queue.GetStats();
    EXPECT_EQ(stats.pendingObjectCount, 0u);
    EXPECT_EQ(stats.collectedObjectCount, 1u);
    EXPECT_EQ(stats.collectPassCount, 3u);
    EXPECT_EQ(stats.forcedCollectPassCount, 2u);
}

} // namespace testing
} // namespace MetalBackend
} // namespace Next
