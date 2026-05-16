#pragma once

#include "metal_device.h"
#include "metal_frame_graph.h"
#include "metal_resource_pool.h"
#include "next/platform/window.h"
#include "next/rhi/frame_graph.h"
#include "next/rhi/render_pass.h"
#include "next/rhi/swapchain.h"

#include <array>

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

namespace Next {
namespace MetalBackend {

class MetalSceneResources;

struct MetalRenderPassSnapshot {
    bool ready = false;
    RHI::FrameGraphDescriptorValidation frameGraphValidation;
    uint32_t frameGraphTransitionCount = 0;
    RHI::FrameGraphCompileResult frameGraphCompileResult;
    MetalFrameGraphResourceUsageTable frameGraphResourceUsages;
    RHI::RenderPassDesc desc;
    std::array<bool, RHI::kMaxRenderPassColorAttachments> colorResolveAttachments{};
    bool depthStencilResolveAttachment = false;
};

struct MetalSwapchainStats {
    bool ready = false;
    RHI::Extent2D drawableSize;
    RHI::Format colorFormat = RHI::Format::Unknown;
    RHI::Format depthFormat = RHI::Format::Unknown;
    bool framebufferOnly = false;
    uint64_t resizeCount = 0;
    uint64_t depthCreateFailureCount = 0;
    uint64_t acquireAttemptCount = 0;
    uint64_t acquiredFrameCount = 0;
    uint64_t acquireFailureCount = 0;
    uint64_t presentedFrameCount = 0;
    uint64_t releasedFrameCount = 0;
    bool frameAcquired = false;

    bool HasDrawableSize() const { return drawableSize.width != 0 && drawableSize.height != 0; }
    bool HasAcquireFailures() const { return acquireFailureCount != 0; }
};

class MetalSwapchain final : public RHI::Swapchain {
public:
    bool Initialize(Window* window, MetalDevice& device, MetalTexturePool& texturePool, const RHI::SwapchainDesc& desc);
    void Shutdown(MetalDevice* device = nullptr, uint64_t submittedFrameIndex = 0);

    void Resize(Window* window, int width, int height, uint64_t submittedFrameIndex = 0);
    bool AcquireFrame(const RHI::ClearColor& clearColor, const MetalSceneResources* sceneResources = nullptr);
    void RecordPresent();
    void ReleaseFrame();
    MetalSwapchainStats GetStats() const;
    MetalRenderPassSnapshot GetRenderPassSnapshot() const { return renderPassSnapshot_; }

    RHI::Extent2D GetDrawableSize() const override { return drawableSize_; }
    RHI::Format GetColorFormat() const override { return colorFormat_; }
    RHI::Format GetDepthFormat() const override { return depthFormat_; }

    CAMetalLayer* NativeLayer() const { return layer_; }
    id<CAMetalDrawable> CurrentDrawable() const { return drawable_; }
    MTLRenderPassDescriptor* CurrentPassDescriptor() const { return passDescriptor_; }
    const RHI::FrameGraphCompileResult& CurrentFrameGraphCompileResult() const {
        return renderPassSnapshot_.frameGraphCompileResult;
    }
    const MetalFrameGraphResourceUsageTable& CurrentFrameGraphResourceUsageTable() const {
        return renderPassSnapshot_.frameGraphResourceUsages;
    }
    id<MTLTexture> DepthTexture() const { return depthTexture_.NativeTexture(); }

private:
    MetalDevice* ownerDevice_ = nullptr;
    MetalTexturePool* texturePool_ = nullptr;
    id<MTLDevice> device_ = nil;
    CAMetalLayer* layer_ = nil;
    id<CAMetalDrawable> drawable_ = nil;
    MTLRenderPassDescriptor* passDescriptor_ = nil;
    MetalTexture depthTexture_;
    RHI::Extent2D drawableSize_;
    RHI::Format colorFormat_ = RHI::Format::BGRA8Unorm;
    RHI::Format depthFormat_ = RHI::Format::Depth32Float;
    bool framebufferOnly_ = true;
    MetalSwapchainStats stats_;
    MetalRenderPassSnapshot renderPassSnapshot_;
};

} // namespace MetalBackend
} // namespace Next
