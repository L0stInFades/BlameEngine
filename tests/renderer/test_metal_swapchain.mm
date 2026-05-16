#include "metal_swapchain.h"

#include "metal_resource.h"
#include "next/foundation/logger.h"
#include "next/platform/window.h"

#include <gtest/gtest.h>

#include <memory>

namespace Next {
namespace MetalBackend {
namespace testing {
namespace {

class MetalSwapchainTest : public ::testing::Test {
protected:
    void SetUp() override {
        Logger::Initialize();
        if (!device.Initialize()) {
            GTEST_SKIP() << "Metal device unavailable";
        }
        ASSERT_TRUE(texturePool.Initialize(device));
    }

    void TearDown() override {
        texturePool.Shutdown();
        device.Shutdown();
        Logger::Shutdown();
    }

    std::unique_ptr<Window> CreateTestWindow(int width, int height) {
        WindowDesc desc;
        desc.title = "NEXT Metal Swapchain Test";
        desc.width = width;
        desc.height = height;
        desc.resizable = true;

        std::unique_ptr<Window> window(CreateWindow());
        if (!window || !window->Initialize(desc)) {
            return nullptr;
        }
        window->PollEvents();
        return window;
    }

    RHI::SwapchainDesc MakeSwapchainDesc(uint32_t width, uint32_t height) const {
        RHI::SwapchainDesc desc;
        desc.drawableSize = RHI::Extent2D{width, height};
        desc.colorFormat = RHI::Format::BGRA8Unorm;
        desc.depthFormat = RHI::Format::Depth32Float;
        desc.framebufferOnly = true;
        return desc;
    }

    MetalDevice device;
    MetalTexturePool texturePool;
};

} // namespace

TEST_F(MetalSwapchainTest, AcquireBeforeInitializeRecordsFailureStats) {
    @autoreleasepool {
        MetalSwapchain swapchain;
        const RHI::ClearColor clearColor;

        EXPECT_FALSE(swapchain.AcquireFrame(clearColor));
        swapchain.ReleaseFrame();

        const MetalSwapchainStats stats = swapchain.GetStats();
        EXPECT_FALSE(stats.ready);
        EXPECT_FALSE(stats.frameAcquired);
        EXPECT_FALSE(stats.HasDrawableSize());
        EXPECT_TRUE(stats.HasAcquireFailures());
        EXPECT_EQ(stats.acquireAttemptCount, 1u);
        EXPECT_EQ(stats.acquiredFrameCount, 0u);
        EXPECT_EQ(stats.acquireFailureCount, 1u);
        EXPECT_EQ(stats.releasedFrameCount, 0u);
        EXPECT_EQ(stats.presentedFrameCount, 0u);
    }
}

TEST_F(MetalSwapchainTest, InitializesLayerDepthTextureAndResizeStats) {
    @autoreleasepool {
        std::unique_ptr<Window> window = CreateTestWindow(320, 240);
        ASSERT_NE(window, nullptr);
        ASSERT_NE(window->GetNativeHandle(), nullptr);

        MetalSwapchain swapchain;
        ASSERT_TRUE(swapchain.Initialize(window.get(), device, texturePool, MakeSwapchainDesc(320, 240)));

        MetalSwapchainStats stats = swapchain.GetStats();
        EXPECT_TRUE(stats.ready);
        EXPECT_TRUE(stats.HasDrawableSize());
        EXPECT_EQ(stats.colorFormat, RHI::Format::BGRA8Unorm);
        EXPECT_EQ(stats.depthFormat, RHI::Format::Depth32Float);
        EXPECT_TRUE(stats.framebufferOnly);
        EXPECT_EQ(stats.resizeCount, 1u);
        EXPECT_EQ(stats.depthCreateFailureCount, 0u);
        EXPECT_GE(stats.drawableSize.width, 320u);
        EXPECT_GE(stats.drawableSize.height, 240u);
        EXPECT_EQ(swapchain.GetDrawableSize().width, stats.drawableSize.width);
        EXPECT_EQ(swapchain.GetDrawableSize().height, stats.drawableSize.height);
        EXPECT_NE(swapchain.NativeLayer(), nil);
        EXPECT_NE(swapchain.DepthTexture(), nil);
        EXPECT_EQ(texturePool.GetLiveTextureCount(), 1u);

        swapchain.Resize(window.get(), 160, 120, 4);
        stats = swapchain.GetStats();
        EXPECT_TRUE(stats.ready);
        EXPECT_EQ(stats.resizeCount, 2u);
        EXPECT_EQ(stats.depthCreateFailureCount, 0u);
        EXPECT_GE(stats.drawableSize.width, 160u);
        EXPECT_GE(stats.drawableSize.height, 120u);
        EXPECT_NE(swapchain.DepthTexture(), nil);
        EXPECT_EQ(texturePool.GetLiveTextureCount(), 1u);
        const size_t pendingAfterResize = device.PendingReleaseCount();
        EXPECT_GE(pendingAfterResize, 1u);

        swapchain.Resize(window.get(), 0, 120, 5);
        stats = swapchain.GetStats();
        EXPECT_EQ(stats.resizeCount, 2u);
        EXPECT_EQ(texturePool.GetLiveTextureCount(), 1u);

        swapchain.Shutdown(&device, 6);
        stats = swapchain.GetStats();
        EXPECT_FALSE(stats.ready);
        EXPECT_FALSE(stats.HasDrawableSize());
        EXPECT_EQ(swapchain.NativeLayer(), nil);
        EXPECT_EQ(swapchain.DepthTexture(), nil);
        EXPECT_EQ(texturePool.GetLiveTextureCount(), 0u);
        EXPECT_EQ(device.PendingReleaseCount(), pendingAfterResize + 1u);

        device.CollectReleasedResources(16, true);
        EXPECT_EQ(device.PendingReleaseCount(), 0u);
        window->Shutdown();
    }
}

TEST_F(MetalSwapchainTest, AcquireBuildsRenderPassDescriptorFromRhiDesc) {
    @autoreleasepool {
        std::unique_ptr<Window> window = CreateTestWindow(320, 240);
        ASSERT_NE(window, nullptr);
        ASSERT_NE(window->GetNativeHandle(), nullptr);

        MetalSwapchain swapchain;
        ASSERT_TRUE(swapchain.Initialize(window.get(), device, texturePool, MakeSwapchainDesc(320, 240)));

        const RHI::ClearColor clearColor{0.1, 0.2, 0.3, 1.0};
        ASSERT_TRUE(swapchain.AcquireFrame(clearColor));

        id<CAMetalDrawable> drawable = swapchain.CurrentDrawable();
        MTLRenderPassDescriptor* pass = swapchain.CurrentPassDescriptor();
        ASSERT_NE(drawable, nil);
        ASSERT_NE(pass, nil);

        EXPECT_EQ(pass.colorAttachments[0].texture, drawable.texture);
        EXPECT_EQ(pass.colorAttachments[0].loadAction, MTLLoadActionClear);
        EXPECT_EQ(pass.colorAttachments[0].storeAction, MTLStoreActionStore);
        EXPECT_DOUBLE_EQ(pass.colorAttachments[0].clearColor.red, clearColor.r);
        EXPECT_DOUBLE_EQ(pass.colorAttachments[0].clearColor.green, clearColor.g);
        EXPECT_DOUBLE_EQ(pass.colorAttachments[0].clearColor.blue, clearColor.b);
        EXPECT_DOUBLE_EQ(pass.colorAttachments[0].clearColor.alpha, clearColor.a);

        EXPECT_EQ(pass.depthAttachment.texture, swapchain.DepthTexture());
        EXPECT_EQ(pass.depthAttachment.loadAction, MTLLoadActionClear);
        EXPECT_EQ(pass.depthAttachment.storeAction, MTLStoreActionDontCare);
        EXPECT_DOUBLE_EQ(pass.depthAttachment.clearDepth, 1.0);

        const MetalRenderPassSnapshot snapshot = swapchain.GetRenderPassSnapshot();
        EXPECT_TRUE(snapshot.ready);
        EXPECT_EQ(snapshot.frameGraphValidation.error, RHI::FrameGraphDescriptorError::None);
        EXPECT_EQ(snapshot.frameGraphTransitionCount, 3u);
        EXPECT_EQ(snapshot.frameGraphCompileResult.dependencyCount, 1u);
        const RHI::FrameGraphPassCompileInfo* presentInfo =
            snapshot.frameGraphCompileResult.GetPass(1);
        ASSERT_NE(presentInfo, nullptr);
        EXPECT_EQ(presentInfo->dependencyOffset, 0u);
        EXPECT_EQ(presentInfo->dependencyCount, 1u);
        const RHI::FrameGraphCompiledPassDependency* presentDependency =
            snapshot.frameGraphCompileResult.GetDependency(0);
        ASSERT_NE(presentDependency, nullptr);
        EXPECT_EQ(presentDependency->passIndex, 1u);
        EXPECT_EQ(presentDependency->dependencyPassIndex, 0u);
        EXPECT_STREQ(snapshot.desc.debugName, "NEXT swapchain pass");
        EXPECT_EQ(snapshot.desc.colorAttachmentCount, 1u);
        EXPECT_TRUE(snapshot.desc.hasDepthStencil);

        MetalSwapchainStats stats = swapchain.GetStats();
        EXPECT_TRUE(stats.frameAcquired);
        EXPECT_EQ(stats.acquireAttemptCount, 1u);
        EXPECT_EQ(stats.acquiredFrameCount, 1u);
        EXPECT_EQ(stats.acquireFailureCount, 0u);

        swapchain.ReleaseFrame();
        stats = swapchain.GetStats();
        EXPECT_FALSE(stats.frameAcquired);
        EXPECT_EQ(stats.releasedFrameCount, 1u);

        swapchain.Shutdown(&device, 4);
        device.CollectReleasedResources(16, true);
        window->Shutdown();
    }
}

TEST_F(MetalSwapchainTest, FailedAcquireClearsPreviousFrameState) {
    @autoreleasepool {
        std::unique_ptr<Window> window = CreateTestWindow(320, 240);
        ASSERT_NE(window, nullptr);
        ASSERT_NE(window->GetNativeHandle(), nullptr);

        MetalSwapchain swapchain;
        ASSERT_TRUE(swapchain.Initialize(window.get(), device, texturePool, MakeSwapchainDesc(320, 240)));

        const RHI::ClearColor clearColor{0.1, 0.2, 0.3, 1.0};
        ASSERT_TRUE(swapchain.AcquireFrame(clearColor));
        ASSERT_NE(swapchain.CurrentDrawable(), nil);
        ASSERT_NE(swapchain.CurrentPassDescriptor(), nil);
        EXPECT_TRUE(swapchain.GetRenderPassSnapshot().ready);

        MetalSceneResources uninitializedSceneResources;
        EXPECT_FALSE(swapchain.AcquireFrame(clearColor, &uninitializedSceneResources));

        const MetalSwapchainStats stats = swapchain.GetStats();
        EXPECT_FALSE(stats.frameAcquired);
        EXPECT_EQ(stats.acquireAttemptCount, 2u);
        EXPECT_EQ(stats.acquiredFrameCount, 1u);
        EXPECT_EQ(stats.acquireFailureCount, 1u);
        EXPECT_EQ(swapchain.CurrentDrawable(), nil);
        EXPECT_EQ(swapchain.CurrentPassDescriptor(), nil);
        EXPECT_FALSE(swapchain.GetRenderPassSnapshot().ready);

        swapchain.ReleaseFrame();
        EXPECT_EQ(swapchain.GetStats().releasedFrameCount, 0u);

        swapchain.Shutdown(&device, 4);
        device.CollectReleasedResources(16, true);
        window->Shutdown();
    }
}

TEST_F(MetalSwapchainTest, RejectsInvalidDescriptorBeforeLayerSetup) {
    @autoreleasepool {
        std::unique_ptr<Window> window = CreateTestWindow(320, 240);
        ASSERT_NE(window, nullptr);
        ASSERT_NE(window->GetNativeHandle(), nullptr);

        RHI::SwapchainDesc desc = MakeSwapchainDesc(320, 240);
        desc.colorFormat = RHI::Format::Depth32Float;

        MetalSwapchain swapchain;
        EXPECT_FALSE(swapchain.Initialize(window.get(), device, texturePool, desc));
        EXPECT_EQ(swapchain.NativeLayer(), nil);
        EXPECT_EQ(swapchain.DepthTexture(), nil);
        EXPECT_FALSE(swapchain.GetStats().ready);
        EXPECT_EQ(texturePool.GetLiveTextureCount(), 0u);

        window->Shutdown();
    }
}

} // namespace testing
} // namespace MetalBackend
} // namespace Next
