#include "metal_upload_queue.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

namespace Next {
namespace MetalBackend {
namespace testing {
namespace {

class MetalUploadQueueTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!device.Initialize()) {
            GTEST_SKIP() << "Metal device unavailable";
        }
        ASSERT_TRUE(bufferPool.Initialize(device));
        ASSERT_TRUE(texturePool.Initialize(device));
        ASSERT_TRUE(uploadQueue.Initialize(device, bufferPool));
    }

    void TearDown() override {
        uploadQueue.Shutdown();
        texturePool.Shutdown();
        bufferPool.Shutdown();
        device.Shutdown();
    }

    MetalDevice device;
    MetalBufferPool bufferPool;
    MetalTexturePool texturePool;
    MetalUploadQueue uploadQueue;
};

RHI::BufferDesc MakeSharedUploadTargetBufferDesc(uint64_t sizeBytes) {
    RHI::BufferDesc desc;
    desc.sizeBytes = sizeBytes;
    desc.memory = RHI::ResourceMemory::Shared;
    desc.usage = RHI::ResourceUsage::VertexBuffer | RHI::ResourceUsage::CopyDestination;
    desc.initialState = RHI::ResourceState::Common;
    desc.debugName = "test upload target buffer";
    return desc;
}

RHI::TextureDesc MakeSharedUploadTargetTextureDesc(uint32_t width, uint32_t height) {
    RHI::TextureDesc desc;
    desc.extent = RHI::Extent2D{width, height};
    desc.format = RHI::Format::RGBA8Unorm;
    desc.memory = RHI::ResourceMemory::Shared;
    desc.usage = RHI::ResourceUsage::ShaderRead | RHI::ResourceUsage::CopyDestination;
    desc.initialState = RHI::ResourceState::CopyDestination;
    desc.debugName = "test upload target texture";
    return desc;
}

} // namespace

TEST_F(MetalUploadQueueTest, BufferUploadCopiesBytesAndTracksStatusHistory) {
    ASSERT_TRUE(uploadQueue.IsReady());
    EXPECT_EQ(uploadQueue.UsesDedicatedQueue(), device.HasDedicatedQueueForClass(RHI::QueueClass::Copy));
    EXPECT_EQ(uploadQueue.GetQueueClass(),
              uploadQueue.UsesDedicatedQueue() ? RHI::QueueClass::Copy : RHI::QueueClass::Graphics);
    EXPECT_EQ(uploadQueue.GetRetainedStatusCapacity(), 128u);

    MetalBuffer buffer;
    ASSERT_TRUE(bufferPool.CreateBuffer(buffer, MakeSharedUploadTargetBufferDesc(64)));
    ASSERT_NE(buffer.Contents(), nullptr);
    std::memset(buffer.Contents(), 0, 64);

    const std::array<uint8_t, 16> source = {
        0x10, 0x11, 0x12, 0x13,
        0x20, 0x21, 0x22, 0x23,
        0x30, 0x31, 0x32, 0x33,
        0x40, 0x41, 0x42, 0x43,
    };

    EXPECT_FALSE(uploadQueue.EnqueueBufferUpload(buffer, source.data(), source.size(), 60));
    EXPECT_FALSE(uploadQueue.HasPendingUploads());
    EXPECT_EQ(uploadQueue.GetPendingUploadCount(), 0u);
    EXPECT_EQ(uploadQueue.GetPendingUploadBytes(), 0u);

    ASSERT_TRUE(uploadQueue.EnqueueBufferUpload(buffer, source.data(), source.size(), 8));
    EXPECT_TRUE(uploadQueue.HasPendingUploads());
    EXPECT_EQ(uploadQueue.GetPendingUploadCount(), 1u);
    EXPECT_EQ(uploadQueue.GetPendingUploadBytes(), source.size());

    const MetalUploadHandle handle = uploadQueue.SubmitUploads();
    ASSERT_TRUE(handle);
    EXPECT_EQ(handle.serial, 1u);
    EXPECT_FALSE(uploadQueue.HasPendingUploads());
    EXPECT_EQ(uploadQueue.GetSubmittedUploadCount(), 1u);
    EXPECT_EQ(uploadQueue.GetLastSubmittedUpload().serial, 1u);

    EXPECT_EQ(uploadQueue.WaitForUploadStatus(handle), MetalUploadStatus::Completed);
    EXPECT_TRUE(uploadQueue.WaitForUpload(handle));
    EXPECT_TRUE(uploadQueue.IsUploadComplete(handle));
    EXPECT_FALSE(uploadQueue.DidUploadFail(handle));
    EXPECT_EQ(uploadQueue.GetUploadStatus(handle), MetalUploadStatus::Completed);
    EXPECT_STREQ(MetalUploadStatusName(uploadQueue.GetUploadStatus(handle)), "completed");
    EXPECT_EQ(uploadQueue.GetCompletedUploadCount(), 1u);
    EXPECT_EQ(uploadQueue.GetFailedUploadCount(), 0u);
    EXPECT_EQ(uploadQueue.GetRetainedStatusCount(), 1u);

    const uint8_t* contents = static_cast<const uint8_t*>(buffer.Contents());
    ASSERT_NE(contents, nullptr);
    EXPECT_EQ(std::memcmp(contents + 8, source.data(), source.size()), 0);

    bool callbackCalled = false;
    MetalUploadStatus callbackStatus = MetalUploadStatus::Unknown;
    JobHandle waitJob = uploadQueue.WaitForUploadAsync(handle, [&](MetalUploadStatus status) {
        callbackCalled = true;
        callbackStatus = status;
    });
    EXPECT_FALSE(waitJob.IsValid());
    EXPECT_TRUE(callbackCalled);
    EXPECT_EQ(callbackStatus, MetalUploadStatus::Completed);

    bufferPool.ReleaseBuffer(buffer, 2);
    device.CollectReleasedResources(5, true);
}

TEST_F(MetalUploadQueueTest, TextureUploadCopiesRowsAndRejectsShortPayloads) {
    MetalTexture texture;
    ASSERT_TRUE(texturePool.CreateTexture(texture, MakeSharedUploadTargetTextureDesc(2, 2)));
    ASSERT_TRUE(texture.IsReady());
    MetalBuffer readbackBuffer;
    constexpr size_t readbackBytesPerRow = 256;
    constexpr size_t readbackBytes = readbackBytesPerRow * 2;
    ASSERT_TRUE(bufferPool.CreateBuffer(readbackBuffer, MakeSharedUploadTargetBufferDesc(readbackBytes)));
    ASSERT_NE(readbackBuffer.Contents(), nullptr);
    std::memset(readbackBuffer.Contents(), 0, readbackBytes);

    const std::array<uint8_t, 16> pixels = {
        0xff, 0x00, 0x00, 0xff,
        0x00, 0xff, 0x00, 0xff,
        0x00, 0x00, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff,
    };

    EXPECT_FALSE(uploadQueue.EnqueueTexture2DUpload(texture,
                                                   pixels.data(),
                                                   pixels.size() - 1,
                                                   2,
                                                   2,
                                                   4));
    EXPECT_FALSE(uploadQueue.HasPendingUploads());

    ASSERT_TRUE(uploadQueue.EnqueueTexture2DUpload(texture, pixels.data(), pixels.size(), 2, 2, 4));
    EXPECT_TRUE(uploadQueue.HasPendingUploads());
    EXPECT_EQ(uploadQueue.GetPendingUploadCount(), 1u);
    EXPECT_EQ(uploadQueue.GetPendingUploadBytes(), 512u);

    const MetalUploadHandle handle = uploadQueue.SubmitUploads();
    ASSERT_TRUE(handle);
    EXPECT_EQ(uploadQueue.WaitForUploadStatus(handle), MetalUploadStatus::Completed);
    EXPECT_EQ(uploadQueue.GetCompletedUploadCount(), 1u);
    EXPECT_EQ(uploadQueue.GetFailedUploadCount(), 0u);
    EXPECT_EQ(uploadQueue.GetRetainedStatusCount(), 1u);

    id<MTLCommandQueue> readbackQueue = device.QueueForClass(RHI::QueueClass::Copy);
    ASSERT_NE(readbackQueue, nil);
    id<MTLCommandBuffer> readbackCommandBuffer = [readbackQueue commandBuffer];
    ASSERT_NE(readbackCommandBuffer, nil);
    id<MTLBlitCommandEncoder> readbackBlit = [readbackCommandBuffer blitCommandEncoder];
    ASSERT_NE(readbackBlit, nil);
    [readbackBlit copyFromTexture:texture.NativeTexture()
                       sourceSlice:0
                       sourceLevel:0
                      sourceOrigin:MTLOriginMake(0, 0, 0)
                        sourceSize:MTLSizeMake(2, 2, 1)
                          toBuffer:readbackBuffer.NativeBuffer()
                 destinationOffset:0
            destinationBytesPerRow:readbackBytesPerRow
          destinationBytesPerImage:readbackBytes];
    [readbackBlit endEncoding];
    [readbackCommandBuffer commit];
    [readbackCommandBuffer waitUntilCompleted];
    ASSERT_EQ(readbackCommandBuffer.status, MTLCommandBufferStatusCompleted);

    const uint8_t* readback = static_cast<const uint8_t*>(readbackBuffer.Contents());
    ASSERT_NE(readback, nullptr);
    EXPECT_TRUE(std::equal(readback, readback + 8, pixels.begin()));
    EXPECT_TRUE(std::equal(readback + readbackBytesPerRow,
                           readback + readbackBytesPerRow + 8,
                           pixels.begin() + 8));

    texturePool.ReleaseTexture(texture, 2);
    bufferPool.ReleaseBuffer(readbackBuffer, 2);
    device.CollectReleasedResources(5, true);
}

TEST_F(MetalUploadQueueTest, TextureUploadRejectsUploadsLargerThanDestinationTexture) {
    MetalTexture texture;
    ASSERT_TRUE(texturePool.CreateTexture(texture, MakeSharedUploadTargetTextureDesc(2, 2)));
    ASSERT_TRUE(texture.IsReady());

    const std::array<uint8_t, 24> pixels = {
        0xff, 0x00, 0x00, 0xff,
        0x00, 0xff, 0x00, 0xff,
        0x00, 0x00, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff,
        0x80, 0x80, 0x80, 0xff,
        0x20, 0x20, 0x20, 0xff,
    };

    EXPECT_FALSE(uploadQueue.EnqueueTexture2DUpload(texture,
                                                   pixels.data(),
                                                   pixels.size(),
                                                   3,
                                                   2,
                                                   4));
    EXPECT_FALSE(uploadQueue.HasPendingUploads());
    EXPECT_EQ(uploadQueue.GetPendingUploadCount(), 0u);
    EXPECT_EQ(uploadQueue.GetPendingUploadBytes(), 0u);
    EXPECT_EQ(uploadQueue.GetSubmittedUploadCount(), 0u);
    EXPECT_EQ(uploadQueue.GetCompletedUploadCount(), 0u);
    EXPECT_EQ(uploadQueue.GetFailedUploadCount(), 0u);

    texturePool.ReleaseTexture(texture, 2);
    device.CollectReleasedResources(5, true);
}

TEST_F(MetalUploadQueueTest, TextureUploadRejectsNon2DTextureTargets) {
    id<MTLTexture> arrayTexture = nil;
    @autoreleasepool {
        MTLTextureDescriptor* desc =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                               width:2
                                                              height:2
                                                           mipmapped:NO];
        desc.textureType = MTLTextureType2DArray;
        desc.arrayLength = 2;
        desc.storageMode = MTLStorageModeShared;
        desc.usage = MTLTextureUsageShaderRead;
        arrayTexture = [device.NativeDevice() newTextureWithDescriptor:desc];
    }
    ASSERT_NE(arrayTexture, nil);

    const std::array<uint8_t, 16> pixels = {
        0xff, 0x00, 0x00, 0xff,
        0x00, 0xff, 0x00, 0xff,
        0x00, 0x00, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff,
    };

    EXPECT_FALSE(uploadQueue.EnqueueTexture2DUpload(arrayTexture,
                                                   pixels.data(),
                                                   pixels.size(),
                                                   2,
                                                   2,
                                                   4));
    EXPECT_FALSE(uploadQueue.HasPendingUploads());
    EXPECT_EQ(uploadQueue.GetPendingUploadCount(), 0u);
    EXPECT_EQ(uploadQueue.GetPendingUploadBytes(), 0u);
    EXPECT_EQ(uploadQueue.GetSubmittedUploadCount(), 0u);
    EXPECT_EQ(uploadQueue.GetCompletedUploadCount(), 0u);
    EXPECT_EQ(uploadQueue.GetFailedUploadCount(), 0u);
}

TEST_F(MetalUploadQueueTest, AutoFlushesFullStagingPacketBeforeContinuing) {
    const size_t stagingCapacityBytes = uploadQueue.GetStagingCapacityBytes();
    ASSERT_GT(stagingCapacityBytes, 256u);

    MetalBuffer firstBuffer;
    MetalBuffer secondBuffer;
    ASSERT_TRUE(bufferPool.CreateBuffer(firstBuffer,
                                        MakeSharedUploadTargetBufferDesc(stagingCapacityBytes)));
    ASSERT_TRUE(bufferPool.CreateBuffer(secondBuffer, MakeSharedUploadTargetBufferDesc(16)));
    ASSERT_NE(firstBuffer.Contents(), nullptr);
    ASSERT_NE(secondBuffer.Contents(), nullptr);
    std::memset(firstBuffer.Contents(), 0, stagingCapacityBytes);
    std::memset(secondBuffer.Contents(), 0, 16);

    std::vector<uint8_t> firstPayload(stagingCapacityBytes);
    for (size_t i = 0; i < firstPayload.size(); ++i) {
        firstPayload[i] = static_cast<uint8_t>(i & 0xffu);
    }
    const std::array<uint8_t, 16> secondPayload = {
        0xa0, 0xa1, 0xa2, 0xa3,
        0xb0, 0xb1, 0xb2, 0xb3,
        0xc0, 0xc1, 0xc2, 0xc3,
        0xd0, 0xd1, 0xd2, 0xd3,
    };

    ASSERT_TRUE(uploadQueue.EnqueueBufferUpload(firstBuffer, firstPayload.data(), firstPayload.size(), 0));
    EXPECT_TRUE(uploadQueue.HasPendingUploads());
    EXPECT_EQ(uploadQueue.GetPendingUploadCount(), 1u);
    EXPECT_EQ(uploadQueue.GetPendingUploadBytes(), stagingCapacityBytes);
    EXPECT_EQ(uploadQueue.GetSubmittedUploadCount(), 0u);

    ASSERT_TRUE(uploadQueue.EnqueueBufferUpload(secondBuffer, secondPayload.data(), secondPayload.size(), 0));
    EXPECT_EQ(uploadQueue.GetSubmittedUploadCount(), 1u);
    EXPECT_TRUE(uploadQueue.HasPendingUploads());
    EXPECT_EQ(uploadQueue.GetPendingUploadCount(), 1u);
    EXPECT_EQ(uploadQueue.GetPendingUploadBytes(), secondPayload.size());

    const MetalUploadHandle firstHandle{1};
    EXPECT_EQ(uploadQueue.WaitForUploadStatus(firstHandle), MetalUploadStatus::Completed);

    const MetalUploadHandle secondHandle = uploadQueue.SubmitUploads();
    ASSERT_TRUE(secondHandle);
    EXPECT_EQ(secondHandle.serial, 2u);
    EXPECT_EQ(uploadQueue.WaitForUploadStatus(secondHandle), MetalUploadStatus::Completed);
    EXPECT_EQ(uploadQueue.GetCompletedUploadCount(), 2u);
    EXPECT_EQ(uploadQueue.GetFailedUploadCount(), 0u);
    EXPECT_EQ(uploadQueue.GetRetainedStatusCount(), 2u);

    const uint8_t* firstContents = static_cast<const uint8_t*>(firstBuffer.Contents());
    const uint8_t* secondContents = static_cast<const uint8_t*>(secondBuffer.Contents());
    ASSERT_NE(firstContents, nullptr);
    ASSERT_NE(secondContents, nullptr);
    EXPECT_EQ(firstContents[0], firstPayload.front());
    EXPECT_EQ(firstContents[stagingCapacityBytes / 2], firstPayload[stagingCapacityBytes / 2]);
    EXPECT_EQ(firstContents[stagingCapacityBytes - 1], firstPayload.back());
    EXPECT_EQ(std::memcmp(secondContents, secondPayload.data(), secondPayload.size()), 0);

    bufferPool.ReleaseBuffer(firstBuffer, 2);
    bufferPool.ReleaseBuffer(secondBuffer, 2);
    device.CollectReleasedResources(5, true);
}

} // namespace testing
} // namespace MetalBackend
} // namespace Next
