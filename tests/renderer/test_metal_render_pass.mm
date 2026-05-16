#include "metal_device.h"
#include "metal_render_pass.h"
#include "next/foundation/logger.h"

#include <gtest/gtest.h>

#import <Metal/Metal.h>

namespace Next {
namespace MetalBackend {
namespace testing {
namespace {

class MetalRenderPassBuilderTest : public ::testing::Test {
protected:
    void SetUp() override {
        Logger::Initialize();
        if (!device.Initialize()) {
            GTEST_SKIP() << "Metal device unavailable";
        }
    }

    void TearDown() override {
        device.Shutdown();
        Logger::Shutdown();
    }

    id<MTLTexture> CreateTexture(MTLPixelFormat format,
                                 MTLTextureUsage usage,
                                 NSUInteger width = 4,
                                 NSUInteger height = 4,
                                 NSUInteger sampleCount = 1) {
        @autoreleasepool {
            MTLTextureDescriptor* desc =
                [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format
                                                                    width:width
                                                                   height:height
                                                                mipmapped:NO];
            desc.usage = usage;
            desc.storageMode = MTLStorageModePrivate;
            if (sampleCount > 1) {
                desc.textureType = MTLTextureType2DMultisample;
                desc.sampleCount = sampleCount;
            }
            return [device.NativeDevice() newTextureWithDescriptor:desc];
        }
    }

    RHI::RenderPassDesc MakeColorDepthPassDesc() const {
        RHI::RenderPassDesc desc;
        desc.debugName = "test render pass";
        desc.colorAttachmentCount = 1;
        desc.colorAttachments[0].format = RHI::Format::BGRA8Unorm;
        desc.colorAttachments[0].loadAction = RHI::AttachmentLoadAction::Clear;
        desc.colorAttachments[0].storeAction = RHI::AttachmentStoreAction::Store;
        desc.colorAttachments[0].clearColor = RHI::ClearColor{0.25, 0.5, 0.75, 1.0};
        desc.hasDepthStencil = true;
        desc.depthStencilAttachment.format = RHI::Format::Depth32Float;
        desc.depthStencilAttachment.loadAction = RHI::AttachmentLoadAction::Clear;
        desc.depthStencilAttachment.storeAction = RHI::AttachmentStoreAction::DontCare;
        desc.depthStencilAttachment.clearDepth = 0.5;
        return desc;
    }

    RHI::RenderPassDesc MakeColorOnlyPassDesc(RHI::AttachmentStoreAction storeAction) const {
        RHI::RenderPassDesc desc;
        desc.debugName = "test color render pass";
        desc.colorAttachmentCount = 1;
        desc.colorAttachments[0].format = RHI::Format::BGRA8Unorm;
        desc.colorAttachments[0].loadAction = RHI::AttachmentLoadAction::Clear;
        desc.colorAttachments[0].storeAction = storeAction;
        return desc;
    }

    RHI::RenderPassDesc MakeMergedDepthStencilPassDesc() const {
        RHI::RenderPassDesc desc;
        desc.debugName = "test merged depth-stencil render pass";
        desc.hasDepthStencil = true;
        desc.depthStencilAttachment.format = RHI::Format::Depth32FloatStencil8;
        desc.depthStencilAttachment.loadAction = RHI::AttachmentLoadAction::Clear;
        desc.depthStencilAttachment.storeAction = RHI::AttachmentStoreAction::Store;
        desc.depthStencilAttachment.clearDepth = 0.25;
        desc.depthStencilAttachment.stencilLoadAction = RHI::AttachmentLoadAction::Clear;
        desc.depthStencilAttachment.stencilStoreAction = RHI::AttachmentStoreAction::Store;
        desc.depthStencilAttachment.clearStencil = 7;
        return desc;
    }

    RHI::RenderPassDesc MakeTwoColorPassDesc() const {
        RHI::RenderPassDesc desc;
        desc.debugName = "test two color render pass";
        desc.colorAttachmentCount = 2;
        desc.colorAttachments[0].format = RHI::Format::BGRA8Unorm;
        desc.colorAttachments[0].loadAction = RHI::AttachmentLoadAction::Clear;
        desc.colorAttachments[0].storeAction = RHI::AttachmentStoreAction::Store;
        desc.colorAttachments[1].format = RHI::Format::BGRA8Unorm;
        desc.colorAttachments[1].loadAction = RHI::AttachmentLoadAction::Clear;
        desc.colorAttachments[1].storeAction = RHI::AttachmentStoreAction::Store;
        return desc;
    }

    MetalDevice device;
};

} // namespace

TEST_F(MetalRenderPassBuilderTest, BuildsColorDepthDescriptorFromRhiDesc) {
    @autoreleasepool {
        id<MTLTexture> color = CreateTexture(MTLPixelFormatBGRA8Unorm, MTLTextureUsageRenderTarget);
        id<MTLTexture> depth = CreateTexture(MTLPixelFormatDepth32Float, MTLTextureUsageRenderTarget);
        ASSERT_NE(color, nil);
        ASSERT_NE(depth, nil);

        MetalRenderPassAttachments attachments;
        attachments.colorTextures[0] = color;
        attachments.depthStencilTexture = depth;

        const MetalRenderPassBuildResult result =
            BuildMetalRenderPassDescriptor(MakeColorDepthPassDesc(), attachments);

        ASSERT_TRUE(result);
        ASSERT_NE(result.descriptor, nil);
        EXPECT_EQ(result.error, MetalRenderPassBuildError::None);
        EXPECT_TRUE(result.validation);

        MTLRenderPassColorAttachmentDescriptor* colorAttachment =
            result.descriptor.colorAttachments[0];
        EXPECT_EQ(colorAttachment.texture, color);
        EXPECT_EQ(colorAttachment.loadAction, MTLLoadActionClear);
        EXPECT_EQ(colorAttachment.storeAction, MTLStoreActionStore);
        EXPECT_DOUBLE_EQ(colorAttachment.clearColor.red, 0.25);
        EXPECT_DOUBLE_EQ(colorAttachment.clearColor.green, 0.5);
        EXPECT_DOUBLE_EQ(colorAttachment.clearColor.blue, 0.75);
        EXPECT_DOUBLE_EQ(colorAttachment.clearColor.alpha, 1.0);

        MTLRenderPassDepthAttachmentDescriptor* depthAttachment =
            result.descriptor.depthAttachment;
        EXPECT_EQ(depthAttachment.texture, depth);
        EXPECT_EQ(depthAttachment.loadAction, MTLLoadActionClear);
        EXPECT_EQ(depthAttachment.storeAction, MTLStoreActionDontCare);
        EXPECT_DOUBLE_EQ(depthAttachment.clearDepth, 0.5);
    }
}

TEST_F(MetalRenderPassBuilderTest, RejectsInvalidRhiDescriptorBeforeTouchingTextures) {
    @autoreleasepool {
        id<MTLTexture> color = CreateTexture(MTLPixelFormatBGRA8Unorm, MTLTextureUsageRenderTarget);
        ASSERT_NE(color, nil);

        RHI::RenderPassDesc desc = MakeColorDepthPassDesc();
        desc.colorAttachments[0].format = RHI::Format::Depth32Float;

        MetalRenderPassAttachments attachments;
        attachments.colorTextures[0] = color;
        attachments.depthStencilTexture = nil;

        const MetalRenderPassBuildResult result = BuildMetalRenderPassDescriptor(desc, attachments);
        EXPECT_FALSE(result);
        EXPECT_EQ(result.descriptor, nil);
        EXPECT_EQ(result.error, MetalRenderPassBuildError::InvalidDescriptor);
        EXPECT_EQ(result.validation.error, RHI::RenderPassDescriptorError::UnsupportedColorFormat);
        EXPECT_EQ(result.attachmentIndex, 0u);
    }
}

TEST_F(MetalRenderPassBuilderTest, RejectsMissingNativeTextures) {
    @autoreleasepool {
        RHI::RenderPassDesc desc = MakeColorDepthPassDesc();
        MetalRenderPassAttachments attachments;

        MetalRenderPassBuildResult result = BuildMetalRenderPassDescriptor(desc, attachments);
        EXPECT_FALSE(result);
        EXPECT_EQ(result.descriptor, nil);
        EXPECT_EQ(result.error, MetalRenderPassBuildError::MissingColorTexture);
        EXPECT_EQ(result.attachmentIndex, 0u);
        EXPECT_TRUE(result.validation);

        id<MTLTexture> color = CreateTexture(MTLPixelFormatBGRA8Unorm, MTLTextureUsageRenderTarget);
        ASSERT_NE(color, nil);
        attachments.colorTextures[0] = color;
        result = BuildMetalRenderPassDescriptor(desc, attachments);
        EXPECT_FALSE(result);
        EXPECT_EQ(result.descriptor, nil);
        EXPECT_EQ(result.error, MetalRenderPassBuildError::MissingDepthStencilTexture);
        EXPECT_EQ(result.attachmentIndex, 1u);
        EXPECT_TRUE(result.validation);
    }
}

TEST_F(MetalRenderPassBuilderTest, RejectsNativeTextureFormatMismatch) {
    @autoreleasepool {
        id<MTLTexture> color = CreateTexture(MTLPixelFormatRGBA8Unorm, MTLTextureUsageRenderTarget);
        id<MTLTexture> depth = CreateTexture(MTLPixelFormatDepth32Float, MTLTextureUsageRenderTarget);
        ASSERT_NE(color, nil);
        ASSERT_NE(depth, nil);

        MetalRenderPassAttachments attachments;
        attachments.colorTextures[0] = color;
        attachments.depthStencilTexture = depth;

        MetalRenderPassBuildResult result =
            BuildMetalRenderPassDescriptor(MakeColorDepthPassDesc(), attachments);
        EXPECT_FALSE(result);
        EXPECT_EQ(result.descriptor, nil);
        EXPECT_EQ(result.error, MetalRenderPassBuildError::TextureFormatMismatch);
        EXPECT_EQ(result.attachmentIndex, 0u);
        EXPECT_TRUE(result.validation);

        color = CreateTexture(MTLPixelFormatBGRA8Unorm, MTLTextureUsageRenderTarget);
        depth = CreateTexture(MTLPixelFormatRGBA8Unorm, MTLTextureUsageRenderTarget);
        ASSERT_NE(color, nil);
        ASSERT_NE(depth, nil);
        attachments.colorTextures[0] = color;
        attachments.depthStencilTexture = depth;

        result = BuildMetalRenderPassDescriptor(MakeColorDepthPassDesc(), attachments);
        EXPECT_FALSE(result);
        EXPECT_EQ(result.descriptor, nil);
        EXPECT_EQ(result.error, MetalRenderPassBuildError::TextureFormatMismatch);
        EXPECT_EQ(result.attachmentIndex, 1u);
        EXPECT_TRUE(result.validation);
    }
}

TEST_F(MetalRenderPassBuilderTest, RejectsNativeTexturesWithoutRenderTargetUsage) {
    @autoreleasepool {
        id<MTLTexture> color = CreateTexture(MTLPixelFormatBGRA8Unorm, MTLTextureUsageShaderRead);
        id<MTLTexture> depth = CreateTexture(MTLPixelFormatDepth32Float, MTLTextureUsageRenderTarget);
        ASSERT_NE(color, nil);
        ASSERT_NE(depth, nil);

        MetalRenderPassAttachments attachments;
        attachments.colorTextures[0] = color;
        attachments.depthStencilTexture = depth;

        MetalRenderPassBuildResult result =
            BuildMetalRenderPassDescriptor(MakeColorDepthPassDesc(), attachments);
        EXPECT_FALSE(result);
        EXPECT_EQ(result.descriptor, nil);
        EXPECT_EQ(result.error, MetalRenderPassBuildError::TextureMissingRenderTargetUsage);
        EXPECT_EQ(result.attachmentIndex, 0u);
        EXPECT_TRUE(result.validation);

        color = CreateTexture(MTLPixelFormatBGRA8Unorm, MTLTextureUsageRenderTarget);
        depth = CreateTexture(MTLPixelFormatDepth32Float, MTLTextureUsageShaderRead);
        ASSERT_NE(color, nil);
        ASSERT_NE(depth, nil);
        attachments.colorTextures[0] = color;
        attachments.depthStencilTexture = depth;

        result = BuildMetalRenderPassDescriptor(MakeColorDepthPassDesc(), attachments);
        EXPECT_FALSE(result);
        EXPECT_EQ(result.descriptor, nil);
        EXPECT_EQ(result.error, MetalRenderPassBuildError::TextureMissingRenderTargetUsage);
        EXPECT_EQ(result.attachmentIndex, 1u);
        EXPECT_TRUE(result.validation);
    }
}

TEST_F(MetalRenderPassBuilderTest, RejectsAttachmentSizeMismatch) {
    @autoreleasepool {
        RHI::RenderPassDesc desc = MakeTwoColorPassDesc();
        MetalRenderPassAttachments attachments;
        attachments.colorTextures[0] = CreateTexture(MTLPixelFormatBGRA8Unorm,
                                                     MTLTextureUsageRenderTarget,
                                                     4,
                                                     4);
        attachments.colorTextures[1] = CreateTexture(MTLPixelFormatBGRA8Unorm,
                                                     MTLTextureUsageRenderTarget,
                                                     8,
                                                     4);
        ASSERT_NE(attachments.colorTextures[0], nil);
        ASSERT_NE(attachments.colorTextures[1], nil);

        MetalRenderPassBuildResult result = BuildMetalRenderPassDescriptor(desc, attachments);
        EXPECT_FALSE(result);
        EXPECT_EQ(result.descriptor, nil);
        EXPECT_EQ(result.error, MetalRenderPassBuildError::TextureSizeMismatch);
        EXPECT_EQ(result.attachmentIndex, 1u);
        EXPECT_TRUE(result.validation);

        desc = MakeColorDepthPassDesc();
        attachments = {};
        attachments.colorTextures[0] = CreateTexture(MTLPixelFormatBGRA8Unorm,
                                                     MTLTextureUsageRenderTarget,
                                                     4,
                                                     4);
        attachments.depthStencilTexture = CreateTexture(MTLPixelFormatDepth32Float,
                                                        MTLTextureUsageRenderTarget,
                                                        4,
                                                        8);
        ASSERT_NE(attachments.colorTextures[0], nil);
        ASSERT_NE(attachments.depthStencilTexture, nil);

        result = BuildMetalRenderPassDescriptor(desc, attachments);
        EXPECT_FALSE(result);
        EXPECT_EQ(result.descriptor, nil);
        EXPECT_EQ(result.error, MetalRenderPassBuildError::TextureSizeMismatch);
        EXPECT_EQ(result.attachmentIndex, 1u);
        EXPECT_TRUE(result.validation);
    }
}

TEST_F(MetalRenderPassBuilderTest, BuildsColorResolveDescriptorFromNativeTextures) {
    @autoreleasepool {
        if (![device.NativeDevice() supportsTextureSampleCount:2]) {
            GTEST_SKIP() << "2x MSAA textures unavailable";
        }

        MetalRenderPassAttachments attachments;
        attachments.colorTextures[0] = CreateTexture(MTLPixelFormatBGRA8Unorm,
                                                     MTLTextureUsageRenderTarget,
                                                     4,
                                                     4,
                                                     2);
        attachments.colorResolveTextures[0] = CreateTexture(MTLPixelFormatBGRA8Unorm,
                                                            MTLTextureUsageRenderTarget);
        ASSERT_NE(attachments.colorTextures[0], nil);
        ASSERT_NE(attachments.colorResolveTextures[0], nil);

        RHI::RenderPassDesc desc = MakeColorOnlyPassDesc(RHI::AttachmentStoreAction::Store);
        MetalRenderPassBuildResult result = BuildMetalRenderPassDescriptor(desc, attachments);
        ASSERT_TRUE(result);
        ASSERT_NE(result.descriptor, nil);

        MTLRenderPassColorAttachmentDescriptor* colorAttachment =
            result.descriptor.colorAttachments[0];
        EXPECT_EQ(colorAttachment.texture, attachments.colorTextures[0]);
        EXPECT_EQ(colorAttachment.resolveTexture, attachments.colorResolveTextures[0]);
        EXPECT_EQ(colorAttachment.storeAction, MTLStoreActionStoreAndMultisampleResolve);

        desc.colorAttachments[0].storeAction = RHI::AttachmentStoreAction::DontCare;
        result = BuildMetalRenderPassDescriptor(desc, attachments);
        ASSERT_TRUE(result);
        colorAttachment = result.descriptor.colorAttachments[0];
        EXPECT_EQ(colorAttachment.resolveTexture, attachments.colorResolveTextures[0]);
        EXPECT_EQ(colorAttachment.storeAction, MTLStoreActionMultisampleResolve);
    }
}

TEST_F(MetalRenderPassBuilderTest, BuildsDepthResolveDescriptorFromNativeTextures) {
    @autoreleasepool {
        if (![device.NativeDevice() supportsTextureSampleCount:2]) {
            GTEST_SKIP() << "2x MSAA textures unavailable";
        }

        RHI::RenderPassDesc desc = MakeColorDepthPassDesc();
        desc.depthStencilAttachment.storeAction = RHI::AttachmentStoreAction::Store;

        MetalRenderPassAttachments attachments;
        attachments.colorTextures[0] = CreateTexture(MTLPixelFormatBGRA8Unorm,
                                                     MTLTextureUsageRenderTarget,
                                                     4,
                                                     4,
                                                     2);
        attachments.depthStencilTexture = CreateTexture(MTLPixelFormatDepth32Float,
                                                        MTLTextureUsageRenderTarget,
                                                        4,
                                                        4,
                                                        2);
        attachments.depthStencilResolveTexture = CreateTexture(MTLPixelFormatDepth32Float,
                                                               MTLTextureUsageRenderTarget);
        ASSERT_NE(attachments.colorTextures[0], nil);
        ASSERT_NE(attachments.depthStencilTexture, nil);
        ASSERT_NE(attachments.depthStencilResolveTexture, nil);

        const MetalRenderPassBuildResult result = BuildMetalRenderPassDescriptor(desc, attachments);
        ASSERT_TRUE(result);
        ASSERT_NE(result.descriptor, nil);

        MTLRenderPassDepthAttachmentDescriptor* depthAttachment =
            result.descriptor.depthAttachment;
        EXPECT_EQ(depthAttachment.texture, attachments.depthStencilTexture);
        EXPECT_EQ(depthAttachment.resolveTexture, attachments.depthStencilResolveTexture);
        EXPECT_EQ(depthAttachment.storeAction, MTLStoreActionStoreAndMultisampleResolve);
    }
}

TEST_F(MetalRenderPassBuilderTest, BuildsMergedDepthStencilResolveDescriptorFromNativeTextures) {
    @autoreleasepool {
        if (![device.NativeDevice() supportsTextureSampleCount:2]) {
            GTEST_SKIP() << "2x MSAA textures unavailable";
        }

        RHI::RenderPassDesc desc = MakeMergedDepthStencilPassDesc();
        desc.depthStencilAttachment.storeAction = RHI::AttachmentStoreAction::Store;
        desc.depthStencilAttachment.stencilStoreAction = RHI::AttachmentStoreAction::Store;

        MetalRenderPassAttachments attachments;
        attachments.depthStencilTexture = CreateTexture(MTLPixelFormatDepth32Float_Stencil8,
                                                        MTLTextureUsageRenderTarget,
                                                        4,
                                                        4,
                                                        2);
        attachments.depthStencilResolveTexture = CreateTexture(MTLPixelFormatDepth32Float_Stencil8,
                                                               MTLTextureUsageRenderTarget);
        if (!attachments.depthStencilTexture || !attachments.depthStencilResolveTexture) {
            GTEST_SKIP() << "MSAA depth-stencil resolve textures unavailable";
        }

        MetalRenderPassBuildResult result = BuildMetalRenderPassDescriptor(desc, attachments);
        ASSERT_TRUE(result);
        ASSERT_NE(result.descriptor, nil);

        MTLRenderPassDepthAttachmentDescriptor* depthAttachment =
            result.descriptor.depthAttachment;
        EXPECT_EQ(depthAttachment.texture, attachments.depthStencilTexture);
        EXPECT_EQ(depthAttachment.resolveTexture, attachments.depthStencilResolveTexture);
        EXPECT_EQ(depthAttachment.storeAction, MTLStoreActionStoreAndMultisampleResolve);

        MTLRenderPassStencilAttachmentDescriptor* stencilAttachment =
            result.descriptor.stencilAttachment;
        EXPECT_EQ(stencilAttachment.texture, attachments.depthStencilTexture);
        EXPECT_EQ(stencilAttachment.resolveTexture, attachments.depthStencilResolveTexture);
        EXPECT_EQ(stencilAttachment.storeAction, MTLStoreActionStoreAndMultisampleResolve);

        desc.depthStencilAttachment.storeAction = RHI::AttachmentStoreAction::DontCare;
        desc.depthStencilAttachment.stencilStoreAction = RHI::AttachmentStoreAction::DontCare;
        result = BuildMetalRenderPassDescriptor(desc, attachments);
        ASSERT_TRUE(result);
        ASSERT_NE(result.descriptor, nil);
        depthAttachment = result.descriptor.depthAttachment;
        stencilAttachment = result.descriptor.stencilAttachment;
        EXPECT_EQ(depthAttachment.storeAction, MTLStoreActionMultisampleResolve);
        EXPECT_EQ(stencilAttachment.storeAction, MTLStoreActionMultisampleResolve);
    }
}

TEST_F(MetalRenderPassBuilderTest, BuildsMergedDepthStencilDescriptorFromRhiDesc) {
    @autoreleasepool {
        id<MTLTexture> depthStencil = CreateTexture(MTLPixelFormatDepth32Float_Stencil8,
                                                    MTLTextureUsageRenderTarget);
        ASSERT_NE(depthStencil, nil);

        MetalRenderPassAttachments attachments;
        attachments.depthStencilTexture = depthStencil;

        const MetalRenderPassBuildResult result =
            BuildMetalRenderPassDescriptor(MakeMergedDepthStencilPassDesc(), attachments);

        ASSERT_TRUE(result);
        ASSERT_NE(result.descriptor, nil);

        MTLRenderPassDepthAttachmentDescriptor* depthAttachment =
            result.descriptor.depthAttachment;
        EXPECT_EQ(depthAttachment.texture, depthStencil);
        EXPECT_EQ(depthAttachment.loadAction, MTLLoadActionClear);
        EXPECT_EQ(depthAttachment.storeAction, MTLStoreActionStore);
        EXPECT_DOUBLE_EQ(depthAttachment.clearDepth, 0.25);

        MTLRenderPassStencilAttachmentDescriptor* stencilAttachment =
            result.descriptor.stencilAttachment;
        EXPECT_EQ(stencilAttachment.texture, depthStencil);
        EXPECT_EQ(stencilAttachment.loadAction, MTLLoadActionClear);
        EXPECT_EQ(stencilAttachment.storeAction, MTLStoreActionStore);
        EXPECT_EQ(stencilAttachment.clearStencil, 7u);
    }
}

TEST_F(MetalRenderPassBuilderTest, RejectsAttachmentSampleCountMismatch) {
    @autoreleasepool {
        if (![device.NativeDevice() supportsTextureSampleCount:2]) {
            GTEST_SKIP() << "2x MSAA textures unavailable";
        }

        RHI::RenderPassDesc desc = MakeTwoColorPassDesc();
        MetalRenderPassAttachments attachments;
        attachments.colorTextures[0] = CreateTexture(MTLPixelFormatBGRA8Unorm,
                                                     MTLTextureUsageRenderTarget,
                                                     4,
                                                     4,
                                                     1);
        attachments.colorTextures[1] = CreateTexture(MTLPixelFormatBGRA8Unorm,
                                                     MTLTextureUsageRenderTarget,
                                                     4,
                                                     4,
                                                     2);
        ASSERT_NE(attachments.colorTextures[0], nil);
        ASSERT_NE(attachments.colorTextures[1], nil);

        const MetalRenderPassBuildResult result = BuildMetalRenderPassDescriptor(desc, attachments);
        EXPECT_FALSE(result);
        EXPECT_EQ(result.descriptor, nil);
        EXPECT_EQ(result.error, MetalRenderPassBuildError::TextureSampleCountMismatch);
        EXPECT_EQ(result.attachmentIndex, 1u);
        EXPECT_TRUE(result.validation);
    }
}

TEST_F(MetalRenderPassBuilderTest, RejectsInvalidResolveSampleCounts) {
    @autoreleasepool {
        if (![device.NativeDevice() supportsTextureSampleCount:2]) {
            GTEST_SKIP() << "2x MSAA textures unavailable";
        }

        const RHI::RenderPassDesc desc = MakeColorOnlyPassDesc(RHI::AttachmentStoreAction::Store);
        MetalRenderPassAttachments attachments;
        attachments.colorTextures[0] = CreateTexture(MTLPixelFormatBGRA8Unorm,
                                                     MTLTextureUsageRenderTarget);
        attachments.colorResolveTextures[0] = CreateTexture(MTLPixelFormatBGRA8Unorm,
                                                            MTLTextureUsageRenderTarget);
        ASSERT_NE(attachments.colorTextures[0], nil);
        ASSERT_NE(attachments.colorResolveTextures[0], nil);

        MetalRenderPassBuildResult result = BuildMetalRenderPassDescriptor(desc, attachments);
        EXPECT_FALSE(result);
        EXPECT_EQ(result.descriptor, nil);
        EXPECT_EQ(result.error, MetalRenderPassBuildError::ResolveSourceSampleCountInvalid);
        EXPECT_EQ(result.attachmentIndex, 0u);
        EXPECT_TRUE(result.validation);

        attachments.colorTextures[0] = CreateTexture(MTLPixelFormatBGRA8Unorm,
                                                     MTLTextureUsageRenderTarget,
                                                     4,
                                                     4,
                                                     2);
        attachments.colorResolveTextures[0] = CreateTexture(MTLPixelFormatBGRA8Unorm,
                                                            MTLTextureUsageRenderTarget,
                                                            4,
                                                            4,
                                                            2);
        ASSERT_NE(attachments.colorTextures[0], nil);
        ASSERT_NE(attachments.colorResolveTextures[0], nil);

        result = BuildMetalRenderPassDescriptor(desc, attachments);
        EXPECT_FALSE(result);
        EXPECT_EQ(result.descriptor, nil);
        EXPECT_EQ(result.error, MetalRenderPassBuildError::ResolveTextureSampleCountInvalid);
        EXPECT_EQ(result.attachmentIndex, 0u);
        EXPECT_TRUE(result.validation);
    }
}

TEST(MetalRenderPassBuilderStandaloneTest, NamesBuildErrors) {
    EXPECT_STREQ(MetalRenderPassBuildErrorName(MetalRenderPassBuildError::None), "none");
    EXPECT_STREQ(MetalRenderPassBuildErrorName(MetalRenderPassBuildError::InvalidDescriptor),
                 "invalid_descriptor");
    EXPECT_STREQ(MetalRenderPassBuildErrorName(MetalRenderPassBuildError::MissingColorTexture),
                 "missing_color_texture");
    EXPECT_STREQ(MetalRenderPassBuildErrorName(MetalRenderPassBuildError::MissingDepthStencilTexture),
                 "missing_depth_stencil_texture");
    EXPECT_STREQ(MetalRenderPassBuildErrorName(MetalRenderPassBuildError::TextureFormatMismatch),
                 "texture_format_mismatch");
    EXPECT_STREQ(MetalRenderPassBuildErrorName(MetalRenderPassBuildError::TextureMissingRenderTargetUsage),
                 "texture_missing_render_target_usage");
    EXPECT_STREQ(MetalRenderPassBuildErrorName(MetalRenderPassBuildError::TextureSizeMismatch),
                 "texture_size_mismatch");
    EXPECT_STREQ(MetalRenderPassBuildErrorName(MetalRenderPassBuildError::TextureSampleCountMismatch),
                 "texture_sample_count_mismatch");
    EXPECT_STREQ(MetalRenderPassBuildErrorName(MetalRenderPassBuildError::ResolveSourceSampleCountInvalid),
                 "resolve_source_sample_count_invalid");
    EXPECT_STREQ(MetalRenderPassBuildErrorName(MetalRenderPassBuildError::ResolveTextureSampleCountInvalid),
                 "resolve_texture_sample_count_invalid");
    EXPECT_STREQ(MetalRenderPassBuildErrorName(static_cast<MetalRenderPassBuildError>(255)), "unknown");
}

} // namespace testing
} // namespace MetalBackend
} // namespace Next
