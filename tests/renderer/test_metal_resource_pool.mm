#include "metal_resource_pool.h"

#include <gtest/gtest.h>

namespace Next {
namespace MetalBackend {
namespace testing {
namespace {

class MetalResourcePoolTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!device.Initialize()) {
            GTEST_SKIP() << "Metal device unavailable";
        }
    }

    void TearDown() override {
        device.Shutdown();
    }

    MetalDevice device;
};

RHI::BufferDesc MakeSharedBufferDesc(uint64_t sizeBytes) {
    RHI::BufferDesc desc;
    desc.sizeBytes = sizeBytes;
    desc.memory = RHI::ResourceMemory::Shared;
    desc.usage = RHI::ResourceUsage::VertexBuffer | RHI::ResourceUsage::CopyDestination;
    desc.initialState = RHI::ResourceState::Common;
    desc.debugName = "test shared buffer";
    return desc;
}

RHI::TextureDesc MakeDeviceTextureDesc(uint32_t width, uint32_t height) {
    RHI::TextureDesc desc;
    desc.extent = RHI::Extent2D{width, height};
    desc.format = RHI::Format::RGBA8Unorm;
    desc.memory = RHI::ResourceMemory::DeviceLocal;
    desc.usage = RHI::ResourceUsage::ShaderRead | RHI::ResourceUsage::CopyDestination;
    desc.initialState = RHI::ResourceState::ShaderRead;
    desc.debugName = "test device texture";
    return desc;
}

} // namespace

TEST_F(MetalResourcePoolTest, BufferPoolTracksAllocReleaseAndBudgetFailures) {
    MetalBufferPool pool;
    ASSERT_TRUE(pool.Initialize(device));
    ASSERT_TRUE(pool.SetMemoryBudget(RHI::ResourceMemory::Shared, 512));

    MetalBuffer buffer;
    ASSERT_TRUE(pool.CreateBuffer(buffer, MakeSharedBufferDesc(256)));
    EXPECT_TRUE(buffer.IsReady());
    EXPECT_EQ(pool.GetLiveBufferCount(), 1u);
    EXPECT_EQ(pool.GetLiveBytes(), 256u);

    RHI::ResourcePoolStats stats = pool.GetStats();
    EXPECT_EQ(stats.resourceType, RHI::ResourceType::Buffer);
    EXPECT_EQ(stats.liveResourceCount, 1u);
    EXPECT_EQ(stats.liveBytes, 256u);
    EXPECT_EQ(stats.peakResourceCount, 1u);
    EXPECT_EQ(stats.peakBytes, 256u);
    ASSERT_NE(RHI::FindResourcePoolMemoryStats(stats, RHI::ResourceMemory::Shared), nullptr);
    EXPECT_EQ(RHI::FindResourcePoolMemoryStats(stats, RHI::ResourceMemory::Shared)->liveBytes, 256u);
    EXPECT_EQ(RHI::FindResourcePoolMemoryStats(stats, RHI::ResourceMemory::Shared)->budgetBytes, 512u);

    MetalBuffer rejectedBuffer;
    EXPECT_FALSE(pool.CreateBuffer(rejectedBuffer, MakeSharedBufferDesc(768)));
    EXPECT_FALSE(rejectedBuffer.IsReady());
    stats = pool.GetStats();
    EXPECT_EQ(stats.liveResourceCount, 1u);
    EXPECT_EQ(stats.liveBytes, 256u);
    EXPECT_EQ(stats.failedAllocationCount, 1u);
    EXPECT_EQ(stats.failedAllocationBytes, 768u);
    EXPECT_EQ(RHI::FindResourcePoolMemoryStats(stats, RHI::ResourceMemory::Shared)->failedAllocationCount, 1u);
    EXPECT_EQ(RHI::FindResourcePoolMemoryStats(stats, RHI::ResourceMemory::Shared)->failedAllocationBytes, 768u);

    pool.ReleaseBuffer(buffer, 2);
    EXPECT_FALSE(buffer.IsReady());
    stats = pool.GetStats();
    EXPECT_EQ(stats.liveResourceCount, 0u);
    EXPECT_EQ(stats.liveBytes, 0u);
    EXPECT_EQ(stats.peakResourceCount, 1u);
    EXPECT_EQ(stats.peakBytes, 256u);
    EXPECT_EQ(RHI::FindResourcePoolMemoryStats(stats, RHI::ResourceMemory::Shared)->liveBytes, 0u);
    EXPECT_EQ(device.PendingReleaseCount(), 1u);
    device.CollectReleasedResources(5, true);
    EXPECT_EQ(device.PendingReleaseCount(), 0u);

    pool.Shutdown();
}

TEST_F(MetalResourcePoolTest, BufferPoolReplacementUsesSubmittedFrameIndexForRetiredBuffer) {
    MetalBufferPool pool;
    ASSERT_TRUE(pool.Initialize(device));
    ASSERT_TRUE(pool.SetMemoryBudget(RHI::ResourceMemory::Shared, 512));

    MetalBuffer buffer;
    ASSERT_TRUE(pool.CreateBuffer(buffer, MakeSharedBufferDesc(128)));

    const uint64_t submittedFrameIndex = 7;
    ASSERT_TRUE(pool.CreateBuffer(buffer, MakeSharedBufferDesc(256), submittedFrameIndex));
    EXPECT_TRUE(buffer.IsReady());

    RHI::ResourcePoolStats stats = pool.GetStats();
    EXPECT_EQ(stats.liveResourceCount, 1u);
    EXPECT_EQ(stats.liveBytes, 256u);
    ASSERT_NE(RHI::FindResourcePoolMemoryStats(stats, RHI::ResourceMemory::Shared), nullptr);
    EXPECT_EQ(RHI::FindResourcePoolMemoryStats(stats, RHI::ResourceMemory::Shared)->liveBytes, 256u);
    EXPECT_EQ(device.PendingReleaseCount(), 1u);

    const uint32_t collectLatency = device.GetReleaseQueueStats().collectLatency;
    ASSERT_GT(collectLatency, 0u);
    device.CollectReleasedResources(submittedFrameIndex + collectLatency - 1);
    EXPECT_EQ(device.PendingReleaseCount(), 1u);
    device.CollectReleasedResources(submittedFrameIndex + collectLatency);
    EXPECT_EQ(device.PendingReleaseCount(), 0u);

    pool.ReleaseBuffer(buffer, submittedFrameIndex + collectLatency);
    device.CollectReleasedResources(submittedFrameIndex + collectLatency, true);
    pool.Shutdown();
}

TEST_F(MetalResourcePoolTest, TexturePoolTracksAllocReleaseAndBudgetFailures) {
    MetalTexturePool pool;
    ASSERT_TRUE(pool.Initialize(device));
    ASSERT_TRUE(pool.SetMemoryBudget(RHI::ResourceMemory::DeviceLocal, 64));

    MetalTexture texture;
    const RHI::TextureDesc smallDesc = MakeDeviceTextureDesc(4, 4);
    ASSERT_TRUE(pool.CreateTexture(texture, smallDesc));
    EXPECT_TRUE(texture.IsReady());
    EXPECT_EQ(pool.GetLiveTextureCount(), 1u);
    EXPECT_EQ(pool.GetLiveBytes(), 64u);

    RHI::ResourcePoolStats stats = pool.GetStats();
    EXPECT_EQ(stats.resourceType, RHI::ResourceType::Texture);
    EXPECT_EQ(stats.liveResourceCount, 1u);
    EXPECT_EQ(stats.liveBytes, 64u);
    EXPECT_EQ(stats.peakResourceCount, 1u);
    EXPECT_EQ(stats.peakBytes, 64u);
    ASSERT_NE(RHI::FindResourcePoolMemoryStats(stats, RHI::ResourceMemory::DeviceLocal), nullptr);
    EXPECT_EQ(RHI::FindResourcePoolMemoryStats(stats, RHI::ResourceMemory::DeviceLocal)->liveBytes, 64u);
    EXPECT_EQ(RHI::FindResourcePoolMemoryStats(stats, RHI::ResourceMemory::DeviceLocal)->budgetBytes, 64u);

    MetalTexture rejectedTexture;
    EXPECT_FALSE(pool.CreateTexture(rejectedTexture, MakeDeviceTextureDesc(8, 8)));
    EXPECT_FALSE(rejectedTexture.IsReady());
    stats = pool.GetStats();
    EXPECT_EQ(stats.liveResourceCount, 1u);
    EXPECT_EQ(stats.liveBytes, 64u);
    EXPECT_EQ(stats.failedAllocationCount, 1u);
    EXPECT_EQ(stats.failedAllocationBytes, 256u);
    EXPECT_EQ(RHI::FindResourcePoolMemoryStats(stats, RHI::ResourceMemory::DeviceLocal)->failedAllocationCount, 1u);
    EXPECT_EQ(RHI::FindResourcePoolMemoryStats(stats, RHI::ResourceMemory::DeviceLocal)->failedAllocationBytes, 256u);

    pool.ReleaseTexture(texture, 2);
    EXPECT_FALSE(texture.IsReady());
    stats = pool.GetStats();
    EXPECT_EQ(stats.liveResourceCount, 0u);
    EXPECT_EQ(stats.liveBytes, 0u);
    EXPECT_EQ(stats.peakResourceCount, 1u);
    EXPECT_EQ(stats.peakBytes, 64u);
    EXPECT_EQ(RHI::FindResourcePoolMemoryStats(stats, RHI::ResourceMemory::DeviceLocal)->liveBytes, 0u);
    EXPECT_EQ(device.PendingReleaseCount(), 1u);
    device.CollectReleasedResources(5, true);
    EXPECT_EQ(device.PendingReleaseCount(), 0u);

    pool.Shutdown();
}

TEST_F(MetalResourcePoolTest, TexturePoolReplacementUsesSubmittedFrameIndexForRetiredTexture) {
    MetalTexturePool pool;
    ASSERT_TRUE(pool.Initialize(device));
    ASSERT_TRUE(pool.SetMemoryBudget(RHI::ResourceMemory::DeviceLocal, 512));

    MetalTexture texture;
    ASSERT_TRUE(pool.CreateTexture(texture, MakeDeviceTextureDesc(4, 4)));

    const uint64_t submittedFrameIndex = 11;
    ASSERT_TRUE(pool.CreateTexture(texture, MakeDeviceTextureDesc(8, 8), submittedFrameIndex));
    EXPECT_TRUE(texture.IsReady());

    RHI::ResourcePoolStats stats = pool.GetStats();
    EXPECT_EQ(stats.liveResourceCount, 1u);
    EXPECT_EQ(stats.liveBytes, 256u);
    ASSERT_NE(RHI::FindResourcePoolMemoryStats(stats, RHI::ResourceMemory::DeviceLocal), nullptr);
    EXPECT_EQ(RHI::FindResourcePoolMemoryStats(stats, RHI::ResourceMemory::DeviceLocal)->liveBytes, 256u);
    EXPECT_EQ(device.PendingReleaseCount(), 1u);

    const uint32_t collectLatency = device.GetReleaseQueueStats().collectLatency;
    ASSERT_GT(collectLatency, 0u);
    device.CollectReleasedResources(submittedFrameIndex + collectLatency - 1);
    EXPECT_EQ(device.PendingReleaseCount(), 1u);
    device.CollectReleasedResources(submittedFrameIndex + collectLatency);
    EXPECT_EQ(device.PendingReleaseCount(), 0u);

    pool.ReleaseTexture(texture, submittedFrameIndex + collectLatency);
    device.CollectReleasedResources(submittedFrameIndex + collectLatency, true);
    pool.Shutdown();
}

TEST_F(MetalResourcePoolTest, TexturePoolAccountsForMultisampleBytes) {
    if (![device.NativeDevice() supportsTextureSampleCount:4]) {
        GTEST_SKIP() << "4x MSAA textures unavailable";
    }

    MetalTexturePool pool;
    ASSERT_TRUE(pool.Initialize(device));
    ASSERT_TRUE(pool.SetMemoryBudget(RHI::ResourceMemory::DeviceLocal, 256));

    RHI::TextureDesc desc = MakeDeviceTextureDesc(4, 4);
    desc.usage = RHI::ResourceUsageFlag(RHI::ResourceUsage::RenderTarget);
    desc.initialState = RHI::ResourceState::RenderTarget;
    desc.sampleCount = 4;

    MetalTexture texture;
    ASSERT_TRUE(pool.CreateTexture(texture, desc));
    EXPECT_TRUE(texture.IsReady());
    EXPECT_EQ(pool.GetLiveBytes(), 256u);

    RHI::ResourcePoolStats stats = pool.GetStats();
    EXPECT_EQ(stats.liveBytes, 256u);
    EXPECT_EQ(RHI::FindResourcePoolMemoryStats(stats, RHI::ResourceMemory::DeviceLocal)->liveBytes, 256u);

    MetalTexture rejectedTexture;
    RHI::TextureDesc rejectedDesc = desc;
    rejectedDesc.extent = RHI::Extent2D{8, 8};
    EXPECT_FALSE(pool.CreateTexture(rejectedTexture, rejectedDesc));
    stats = pool.GetStats();
    EXPECT_EQ(stats.failedAllocationCount, 1u);
    EXPECT_EQ(stats.failedAllocationBytes, 1024u);

    pool.ReleaseTexture(texture, 2);
    device.CollectReleasedResources(5, true);
    pool.Shutdown();
}

TEST_F(MetalResourcePoolTest, TexturePoolRejectsAttachmentUsageFormatMismatchBeforeAllocation) {
    MetalTexturePool pool;
    ASSERT_TRUE(pool.Initialize(device));

    RHI::TextureDesc depthUsageWithColorFormat = MakeDeviceTextureDesc(4, 4);
    depthUsageWithColorFormat.usage = RHI::ResourceUsageFlag(RHI::ResourceUsage::DepthStencil);
    depthUsageWithColorFormat.initialState = RHI::ResourceState::DepthWrite;

    MetalTexture rejectedTexture;
    EXPECT_FALSE(pool.CreateTexture(rejectedTexture, depthUsageWithColorFormat));
    EXPECT_FALSE(rejectedTexture.IsReady());

    RHI::ResourcePoolStats stats = pool.GetStats();
    EXPECT_EQ(stats.liveResourceCount, 0u);
    EXPECT_EQ(stats.liveBytes, 0u);
    EXPECT_EQ(stats.failedAllocationCount, 0u);
    EXPECT_EQ(pool.GetLiveTextureCount(), 0u);
    EXPECT_EQ(device.PendingReleaseCount(), 0u);

    pool.Shutdown();
}

} // namespace testing
} // namespace MetalBackend
} // namespace Next
