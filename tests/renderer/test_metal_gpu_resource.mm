#include "metal_gpu_resource.h"

#include <gtest/gtest.h>

#include <cstring>

namespace Next {
namespace MetalBackend {
namespace testing {
namespace {

class MetalGpuResourceTest : public ::testing::Test {
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
    desc.debugName = "test direct metal buffer";
    return desc;
}

RHI::TextureDesc MakeDeviceTextureDesc(uint32_t width, uint32_t height) {
    RHI::TextureDesc desc;
    desc.extent = RHI::Extent2D{width, height};
    desc.format = RHI::Format::RGBA8Unorm;
    desc.memory = RHI::ResourceMemory::DeviceLocal;
    desc.usage = RHI::ResourceUsage::ShaderRead |
        RHI::ResourceUsage::ShaderWrite |
        RHI::ResourceUsage::RenderTarget |
        RHI::ResourceUsage::CopyDestination;
    desc.initialState = RHI::ResourceState::ShaderRead;
    desc.debugName = "test direct metal texture";
    return desc;
}

} // namespace

TEST_F(MetalGpuResourceTest, BufferStoresDescriptorStateLabelAndQueuesRelease) {
    MetalBuffer buffer;
    const RHI::BufferDesc desc = MakeSharedBufferDesc(128);

    ASSERT_TRUE(buffer.Initialize(device, desc));
    EXPECT_TRUE(buffer.IsReady());
    ASSERT_NE(buffer.NativeBuffer(), nil);
    EXPECT_EQ(buffer.GetResourceType(), RHI::ResourceType::Buffer);
    EXPECT_EQ(buffer.GetUsageFlags(), desc.usage);
    EXPECT_EQ(buffer.GetCurrentState(), RHI::ResourceState::Common);
    EXPECT_EQ(buffer.GetDesc().sizeBytes, 128u);
    EXPECT_EQ(buffer.GetDesc().memory, RHI::ResourceMemory::Shared);
    EXPECT_STREQ(buffer.GetDebugName(), "test direct metal buffer");
    EXPECT_STREQ(buffer.GetDesc().debugName, buffer.GetDebugName());
    ASSERT_NE(buffer.NativeBuffer().label, nil);
    EXPECT_STREQ([buffer.NativeBuffer().label UTF8String], "test direct metal buffer");

    ASSERT_NE(buffer.Contents(), nullptr);
    constexpr uint8_t fillByte = 0x5a;
    std::memset(buffer.Contents(), fillByte, static_cast<size_t>(desc.sizeBytes));
    EXPECT_EQ(static_cast<const uint8_t*>(buffer.Contents())[127], fillByte);

    buffer.SetCurrentState(RHI::ResourceState::VertexBuffer);
    EXPECT_EQ(buffer.GetCurrentState(), RHI::ResourceState::VertexBuffer);

    buffer.Shutdown(&device, 2);
    EXPECT_FALSE(buffer.IsReady());
    EXPECT_EQ(buffer.NativeBuffer(), nil);
    EXPECT_EQ(buffer.GetCurrentState(), RHI::ResourceState::Undefined);
    EXPECT_STREQ(buffer.GetDebugName(), "");
    EXPECT_EQ(device.PendingReleaseCount(), 1u);

    device.CollectReleasedResources(5, true);
    EXPECT_EQ(device.PendingReleaseCount(), 0u);
}

TEST_F(MetalGpuResourceTest, TextureStoresDescriptorStateUsageLabelAndQueuesRelease) {
    MetalTexture texture;
    const RHI::TextureDesc desc = MakeDeviceTextureDesc(4, 4);

    ASSERT_TRUE(texture.Initialize(device, desc));
    EXPECT_TRUE(texture.IsReady());
    ASSERT_NE(texture.NativeTexture(), nil);
    EXPECT_EQ(texture.GetResourceType(), RHI::ResourceType::Texture);
    EXPECT_EQ(texture.GetUsageFlags(), desc.usage);
    EXPECT_EQ(texture.GetCurrentState(), RHI::ResourceState::ShaderRead);
    EXPECT_EQ(texture.GetDesc().extent.width, 4u);
    EXPECT_EQ(texture.GetDesc().extent.height, 4u);
    EXPECT_EQ(texture.GetDesc().format, RHI::Format::RGBA8Unorm);
    EXPECT_EQ(texture.GetDesc().sampleCount, 1u);
    EXPECT_EQ(texture.GetDesc().memory, RHI::ResourceMemory::DeviceLocal);
    EXPECT_STREQ(texture.GetDebugName(), "test direct metal texture");
    EXPECT_STREQ(texture.GetDesc().debugName, texture.GetDebugName());
    ASSERT_NE(texture.NativeTexture().label, nil);
    EXPECT_STREQ([texture.NativeTexture().label UTF8String], "test direct metal texture");
    EXPECT_EQ(texture.NativeTexture().pixelFormat, MTLPixelFormatRGBA8Unorm);
    EXPECT_EQ(texture.NativeTexture().width, 4u);
    EXPECT_EQ(texture.NativeTexture().height, 4u);
    EXPECT_EQ(texture.NativeTexture().sampleCount, 1u);
    EXPECT_EQ(texture.NativeTexture().textureType, MTLTextureType2D);
    EXPECT_NE(texture.Usage() & MTLTextureUsageShaderRead, 0u);
    EXPECT_NE(texture.Usage() & MTLTextureUsageShaderWrite, 0u);
    EXPECT_NE(texture.Usage() & MTLTextureUsageRenderTarget, 0u);

    texture.SetCurrentState(RHI::ResourceState::RenderTarget);
    EXPECT_EQ(texture.GetCurrentState(), RHI::ResourceState::RenderTarget);

    texture.Shutdown(&device, 2);
    EXPECT_FALSE(texture.IsReady());
    EXPECT_EQ(texture.NativeTexture(), nil);
    EXPECT_EQ(texture.GetCurrentState(), RHI::ResourceState::Undefined);
    EXPECT_EQ(texture.Usage(), MTLTextureUsageUnknown);
    EXPECT_STREQ(texture.GetDebugName(), "");
    EXPECT_EQ(device.PendingReleaseCount(), 1u);

    device.CollectReleasedResources(5, true);
    EXPECT_EQ(device.PendingReleaseCount(), 0u);
}

TEST_F(MetalGpuResourceTest, TextureCreatesMultisampleRenderTargetWhenSupported) {
    if (![device.NativeDevice() supportsTextureSampleCount:4]) {
        GTEST_SKIP() << "4x MSAA textures unavailable";
    }

    MetalTexture texture;
    RHI::TextureDesc desc;
    desc.extent = RHI::Extent2D{4, 4};
    desc.format = RHI::Format::BGRA8Unorm;
    desc.memory = RHI::ResourceMemory::DeviceLocal;
    desc.usage = RHI::ResourceUsageFlag(RHI::ResourceUsage::RenderTarget);
    desc.initialState = RHI::ResourceState::RenderTarget;
    desc.sampleCount = 4;
    desc.debugName = "test multisample render target";

    ASSERT_TRUE(texture.Initialize(device, desc));
    ASSERT_NE(texture.NativeTexture(), nil);
    EXPECT_EQ(texture.GetDesc().sampleCount, 4u);
    EXPECT_EQ(texture.NativeTexture().sampleCount, 4u);
    EXPECT_EQ(texture.NativeTexture().textureType, MTLTextureType2DMultisample);
    EXPECT_NE(texture.Usage() & MTLTextureUsageRenderTarget, 0u);

    texture.Shutdown(&device, 2);
    EXPECT_EQ(device.PendingReleaseCount(), 1u);
    device.CollectReleasedResources(5, true);
    EXPECT_EQ(device.PendingReleaseCount(), 0u);
}

TEST_F(MetalGpuResourceTest, TextureSupportsMergedDepthStencilFormat) {
    MetalTexture texture;
    RHI::TextureDesc desc;
    desc.extent = RHI::Extent2D{4, 4};
    desc.format = RHI::Format::Depth32FloatStencil8;
    desc.memory = RHI::ResourceMemory::DeviceLocal;
    desc.usage = RHI::ResourceUsageFlag(RHI::ResourceUsage::DepthStencil);
    desc.initialState = RHI::ResourceState::DepthWrite;
    desc.debugName = "test merged depth-stencil texture";

    ASSERT_TRUE(texture.Initialize(device, desc));
    EXPECT_TRUE(texture.IsReady());
    ASSERT_NE(texture.NativeTexture(), nil);
    EXPECT_EQ(texture.GetDesc().format, RHI::Format::Depth32FloatStencil8);
    EXPECT_EQ(texture.NativeTexture().pixelFormat, MTLPixelFormatDepth32Float_Stencil8);
    EXPECT_NE(texture.Usage() & MTLTextureUsageRenderTarget, 0u);

    texture.Shutdown(&device, 2);
    EXPECT_EQ(device.PendingReleaseCount(), 1u);
    device.CollectReleasedResources(5, true);
    EXPECT_EQ(device.PendingReleaseCount(), 0u);
}

TEST_F(MetalGpuResourceTest, InvalidDescriptorsDoNotReplaceExistingResources) {
    MetalBuffer buffer;
    ASSERT_TRUE(buffer.Initialize(device, MakeSharedBufferDesc(64)));
    id<MTLBuffer> originalBuffer = buffer.NativeBuffer();
    RHI::BufferDesc invalidBufferDesc = MakeSharedBufferDesc(0);

    EXPECT_FALSE(buffer.Initialize(device, invalidBufferDesc));
    EXPECT_TRUE(buffer.IsReady());
    EXPECT_EQ(buffer.NativeBuffer(), originalBuffer);
    EXPECT_EQ(buffer.GetDesc().sizeBytes, 64u);
    EXPECT_EQ(device.PendingReleaseCount(), 0u);

    invalidBufferDesc = MakeSharedBufferDesc(64);
    invalidBufferDesc.usage = RHI::ResourceUsageFlag(RHI::ResourceUsage::None);
    EXPECT_FALSE(buffer.Initialize(device, invalidBufferDesc));
    EXPECT_TRUE(buffer.IsReady());
    EXPECT_EQ(buffer.NativeBuffer(), originalBuffer);
    EXPECT_EQ(buffer.GetDesc().sizeBytes, 64u);
    EXPECT_EQ(device.PendingReleaseCount(), 0u);

    MetalTexture texture;
    ASSERT_TRUE(texture.Initialize(device, MakeDeviceTextureDesc(4, 4)));
    id<MTLTexture> originalTexture = texture.NativeTexture();
    RHI::TextureDesc invalidTextureDesc = MakeDeviceTextureDesc(4, 4);
    invalidTextureDesc.format = RHI::Format::Unknown;

    EXPECT_FALSE(texture.Initialize(device, invalidTextureDesc));
    EXPECT_TRUE(texture.IsReady());
    EXPECT_EQ(texture.NativeTexture(), originalTexture);
    EXPECT_EQ(texture.GetDesc().format, RHI::Format::RGBA8Unorm);
    EXPECT_EQ(device.PendingReleaseCount(), 0u);

    invalidTextureDesc = MakeDeviceTextureDesc(4, 4);
    invalidTextureDesc.usage = RHI::ResourceUsageFlag(RHI::ResourceUsage::None);
    EXPECT_FALSE(texture.Initialize(device, invalidTextureDesc));
    EXPECT_TRUE(texture.IsReady());
    EXPECT_EQ(texture.NativeTexture(), originalTexture);
    EXPECT_EQ(texture.GetDesc().format, RHI::Format::RGBA8Unorm);
    EXPECT_EQ(device.PendingReleaseCount(), 0u);

    buffer.Shutdown(&device, 2);
    texture.Shutdown(&device, 2);
    EXPECT_EQ(device.PendingReleaseCount(), 2u);
    device.CollectReleasedResources(5, true);
    EXPECT_EQ(device.PendingReleaseCount(), 0u);
}

} // namespace testing
} // namespace MetalBackend
} // namespace Next
