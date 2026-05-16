#include "metal_device.h"

#include <gtest/gtest.h>

namespace Next {
namespace MetalBackend {
namespace testing {

TEST(MetalDeviceTest, ReportsQueueFeaturesAndNativeQueues) {
    MetalDevice device;
    if (!device.Initialize()) {
        GTEST_SKIP() << "Metal device unavailable";
    }

    EXPECT_EQ(device.GetBackend(), RHI::Backend::Metal);
    ASSERT_NE(device.NativeDevice(), nil);
    ASSERT_NE(device.GetDeviceName(), nullptr);
    EXPECT_NE(device.GetDeviceName()[0], '\0');

    const RHI::DeviceFeatures& features = device.GetFeatures();
    EXPECT_EQ(features.maxFramesInFlight, 3u);
    EXPECT_TRUE(features.SupportsQueueClass(RHI::QueueClass::Graphics));
    EXPECT_TRUE(features.SupportsQueueClass(RHI::QueueClass::Compute));
    EXPECT_TRUE(features.SupportsQueueClass(RHI::QueueClass::Copy));
    EXPECT_TRUE(features.HasDedicatedQueueClass(RHI::QueueClass::Graphics));
    EXPECT_FALSE(features.HasDedicatedQueueClass(RHI::QueueClass::Compute));
    EXPECT_EQ(features.asyncUploadQueue, features.HasDedicatedQueueClass(RHI::QueueClass::Copy));
    EXPECT_EQ(features.argumentBuffers,
              features.SupportsArgumentBufferTier(RHI::ArgumentBufferTier::Tier1));
    if (features.argumentBuffers) {
        EXPECT_NE(features.argumentBufferTier, RHI::ArgumentBufferTier::Unsupported);
        EXPECT_TRUE(features.SupportsArgumentBufferTier(RHI::ArgumentBufferTier::Tier1));
    } else {
        EXPECT_EQ(features.argumentBufferTier, RHI::ArgumentBufferTier::Unsupported);
    }
    EXPECT_EQ(features.bindlessResources,
              features.SupportsArgumentBufferTier(RHI::ArgumentBufferTier::Tier2));

    id<MTLCommandQueue> graphicsQueue = device.QueueForClass(RHI::QueueClass::Graphics);
    id<MTLCommandQueue> computeQueue = device.QueueForClass(RHI::QueueClass::Compute);
    id<MTLCommandQueue> copyQueue = device.QueueForClass(RHI::QueueClass::Copy);
    EXPECT_NE(graphicsQueue, nil);
    EXPECT_NE(computeQueue, nil);
    EXPECT_NE(copyQueue, nil);
    EXPECT_EQ(computeQueue, graphicsQueue);
    EXPECT_EQ(device.HasDedicatedQueueForClass(RHI::QueueClass::Graphics),
              features.HasDedicatedQueueClass(RHI::QueueClass::Graphics));
    EXPECT_EQ(device.HasDedicatedQueueForClass(RHI::QueueClass::Compute),
              features.HasDedicatedQueueClass(RHI::QueueClass::Compute));
    EXPECT_EQ(device.HasDedicatedQueueForClass(RHI::QueueClass::Copy),
              features.HasDedicatedQueueClass(RHI::QueueClass::Copy));
    EXPECT_EQ(device.QueueForClass(static_cast<RHI::QueueClass>(255)), nil);

    device.Shutdown();
}

TEST(MetalDeviceTest, ShutdownClearsNativeQueuesAndFeatureFlags) {
    MetalDevice device;
    if (!device.Initialize()) {
        GTEST_SKIP() << "Metal device unavailable";
    }

    ASSERT_NE(device.NativeDevice(), nil);
    ASSERT_NE(device.QueueForClass(RHI::QueueClass::Graphics), nil);

    device.Shutdown();

    EXPECT_EQ(device.NativeDevice(), nil);
    EXPECT_EQ(device.QueueForClass(RHI::QueueClass::Graphics), nil);
    EXPECT_EQ(device.QueueForClass(RHI::QueueClass::Compute), nil);
    EXPECT_EQ(device.QueueForClass(RHI::QueueClass::Copy), nil);
    EXPECT_EQ(device.GetDeviceName()[0], '\0');
    EXPECT_FALSE(device.GetFeatures().SupportsQueueClass(RHI::QueueClass::Graphics));
    EXPECT_FALSE(device.GetFeatures().SupportsQueueClass(RHI::QueueClass::Compute));
    EXPECT_FALSE(device.GetFeatures().SupportsQueueClass(RHI::QueueClass::Copy));
    EXPECT_FALSE(device.GetFeatures().HasDedicatedQueueClass(RHI::QueueClass::Graphics));
    EXPECT_FALSE(device.GetFeatures().asyncUploadQueue);
    EXPECT_EQ(device.GetFeatures().argumentBufferTier, RHI::ArgumentBufferTier::Unsupported);
    EXPECT_FALSE(device.GetFeatures().argumentBuffers);
    EXPECT_FALSE(device.GetFeatures().bindlessResources);
    EXPECT_EQ(device.GetFeatures().maxFramesInFlight, 2u);
}

} // namespace testing
} // namespace MetalBackend
} // namespace Next
