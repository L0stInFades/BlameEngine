#include "metal_swapchain.h"

#include "metal_conversions.h"
#include "metal_render_pass.h"
#include "metal_resource.h"
#include "next/foundation/logger.h"

#include <algorithm>

#import <Cocoa/Cocoa.h>

namespace Next {
namespace MetalBackend {
namespace {

struct SwapchainRenderPassFrameGraphResult {
    RHI::FrameGraphDescriptorValidation validation;
    uint32_t transitionCount = 0;
    RHI::FrameGraphCompileResult compileResult;
    MetalFrameGraphResourceUsageTable resourceUsages;
};

CGFloat BackingScaleForView(NSView* view) {
    if (view && view.window && view.window.screen) {
        return view.window.screen.backingScaleFactor;
    }
    NSScreen* screen = [NSScreen mainScreen];
    return screen ? screen.backingScaleFactor : 1.0;
}

RHI::FrameGraphAccessType RenderPassAttachmentAccess(RHI::AttachmentLoadAction loadAction) {
    return loadAction == RHI::AttachmentLoadAction::Load ? RHI::FrameGraphAccessType::ReadWrite
                                                         : RHI::FrameGraphAccessType::Write;
}

SwapchainRenderPassFrameGraphResult CompileSwapchainRenderPassFrameGraph(
    const RHI::RenderPassDesc& desc,
    const MetalRenderPassAttachments& attachments,
    const MetalSceneResources* sceneResources) {
    SwapchainRenderPassFrameGraphResult result;
    RHI::FrameGraphDesc graph;
    RHI::FrameGraphPassDesc renderPass;
    renderPass.debugName = desc.debugName;
    renderPass.queueClass = RHI::QueueClass::Graphics;
    std::array<RHI::FrameGraphResourceHandle, RHI::kMaxRenderPassColorAttachments> colorHandles{};

    for (uint32_t i = 0; i < desc.colorAttachmentCount && i < colorHandles.size(); ++i) {
        RHI::FrameGraphResourceDesc colorResource;
        colorResource.debugName = i == 0 ? "NEXT swapchain color" : "NEXT swapchain color attachment";
        colorResource.type = RHI::ResourceType::Texture;
        colorResource.usage = RHI::ResourceUsage::RenderTarget | RHI::ResourceUsage::Present;
        colorResource.initialState = RHI::ResourceState::Undefined;
        colorResource.imported = true;

        if (!RHI::AddFrameGraphResource(graph, colorResource, &colorHandles[i])) {
            result.validation.error = RHI::FrameGraphDescriptorError::TooManyResources;
            result.validation.resourceIndex = i;
            return result;
        }
        if (!result.resourceUsages.SetResource(colorHandles[i].index,
                                               attachments.colorTextures[i],
                                               RHI::ResourceType::Texture)) {
            result.validation.error = RHI::FrameGraphDescriptorError::MissingResource;
            result.validation.resourceIndex = colorHandles[i].index;
            return result;
        }
        if (!RHI::AddFrameGraphPassAccess(
                renderPass,
                RHI::MakeFrameGraphPassResourceAccess(
                    colorHandles[i],
                    RHI::ResourceState::RenderTarget,
                    RenderPassAttachmentAccess(desc.colorAttachments[i].loadAction)))) {
            result.validation.error = RHI::FrameGraphDescriptorError::TooManyPassAccesses;
            result.validation.resourceIndex = colorHandles[i].index;
            result.validation.accessIndex = i;
            return result;
        }
    }

    if (desc.hasDepthStencil) {
        RHI::FrameGraphResourceDesc depthResource;
        depthResource.debugName = "NEXT swapchain depth";
        depthResource.type = RHI::ResourceType::Texture;
        depthResource.usage = RHI::ResourceUsageFlag(RHI::ResourceUsage::DepthStencil);
        depthResource.initialState = RHI::ResourceState::Undefined;
        depthResource.imported = true;

        RHI::FrameGraphResourceHandle depthHandle;
        if (!RHI::AddFrameGraphResource(graph, depthResource, &depthHandle)) {
            result.validation.error = RHI::FrameGraphDescriptorError::TooManyResources;
            result.validation.resourceIndex = graph.resourceCount;
            return result;
        }
        if (!result.resourceUsages.SetResource(depthHandle.index,
                                               attachments.depthStencilTexture,
                                               RHI::ResourceType::Texture)) {
            result.validation.error = RHI::FrameGraphDescriptorError::MissingResource;
            result.validation.resourceIndex = depthHandle.index;
            return result;
        }
        const bool loadsDepthStencil =
            desc.depthStencilAttachment.loadAction == RHI::AttachmentLoadAction::Load ||
            desc.depthStencilAttachment.stencilLoadAction == RHI::AttachmentLoadAction::Load;
        if (!RHI::AddFrameGraphPassAccess(
                renderPass,
                RHI::MakeFrameGraphPassResourceAccess(depthHandle,
                                                      RHI::ResourceState::DepthWrite,
                                                      loadsDepthStencil ? RHI::FrameGraphAccessType::ReadWrite
                                                                        : RHI::FrameGraphAccessType::Write))) {
            result.validation.error = RHI::FrameGraphDescriptorError::TooManyPassAccesses;
            result.validation.resourceIndex = depthHandle.index;
            result.validation.accessIndex = renderPass.accessCount;
            return result;
        }
    }

    if (sceneResources &&
        !sceneResources->AppendDrawFrameGraphResources(
            graph, renderPass, &result.resourceUsages, result.validation)) {
        return result;
    }

    const uint32_t renderPassIndex = graph.passCount;
    if (!RHI::AddFrameGraphPass(graph, renderPass)) {
        result.validation.error = RHI::FrameGraphDescriptorError::TooManyPasses;
        return result;
    }

    if (desc.colorAttachmentCount != 0 && colorHandles[0].IsValid()) {
        RHI::FrameGraphPassDesc presentPass;
        presentPass.debugName = "NEXT swapchain present";
        presentPass.queueClass = RHI::QueueClass::Graphics;
        if (!RHI::AddFrameGraphPassDependency(
                presentPass, RHI::MakeFrameGraphPassDependency(renderPassIndex))) {
            result.validation.error = RHI::FrameGraphDescriptorError::TooManyPassDependencies;
            result.validation.passIndex = graph.passCount;
            result.validation.dependencyPassIndex = renderPassIndex;
            return result;
        }
        if (!RHI::AddFrameGraphPassAccess(
                presentPass,
                RHI::MakeFrameGraphPassResourceAccess(colorHandles[0],
                                                      RHI::ResourceState::Present,
                                                      RHI::FrameGraphAccessType::Read))) {
            result.validation.error = RHI::FrameGraphDescriptorError::TooManyPassAccesses;
            result.validation.resourceIndex = colorHandles[0].index;
            return result;
        }
        if (!RHI::AddFrameGraphPass(graph, presentPass)) {
            result.validation.error = RHI::FrameGraphDescriptorError::TooManyPasses;
            return result;
        }
    }

    result.compileResult = RHI::CompileFrameGraphTransitions(graph);
    result.validation = result.compileResult.validation;
    result.transitionCount = result.compileResult.transitionCount;
    return result;
}

} // namespace

bool MetalSwapchain::Initialize(Window* window, MetalDevice& device, MetalTexturePool& texturePool, const RHI::SwapchainDesc& desc) {
    if (!window || !window->GetNativeHandle() || !device.NativeDevice()) {
        NEXT_LOG_ERROR("Invalid window or device for Metal swapchain");
        return false;
    }

    const RHI::SwapchainDescriptorValidation validation = RHI::ValidateSwapchainDesc(desc);
    if (!validation) {
        NEXT_LOG_ERROR("Metal swapchain descriptor rejected: %s (drawable=%ux%u color=%s depth=%s)",
                       RHI::SwapchainDescriptorErrorName(validation.error),
                       desc.drawableSize.width,
                       desc.drawableSize.height,
                       RHI::FormatName(desc.colorFormat),
                       RHI::FormatName(desc.depthFormat));
        return false;
    }

    @autoreleasepool {
        Shutdown();

        ownerDevice_ = &device;
        texturePool_ = &texturePool;
        device_ = device.NativeDevice();
        colorFormat_ = desc.colorFormat;
        depthFormat_ = desc.depthFormat;
        framebufferOnly_ = desc.framebufferOnly;
        stats_ = {};
        stats_.colorFormat = colorFormat_;
        stats_.depthFormat = depthFormat_;
        stats_.framebufferOnly = framebufferOnly_;
        renderPassSnapshot_ = {};

        NSView* view = (__bridge NSView*)window->GetNativeHandle();
        [view setWantsLayer:YES];

        layer_ = [CAMetalLayer layer];
        layer_.device = device_;
        layer_.pixelFormat = ToMetalPixelFormat(colorFormat_);
        layer_.framebufferOnly = framebufferOnly_;
        layer_.opaque = YES;
        view.layer = layer_;

        Resize(window, static_cast<int>(desc.drawableSize.width), static_cast<int>(desc.drawableSize.height));
        return layer_ != nil;
    }
}

void MetalSwapchain::Shutdown(MetalDevice* device, uint64_t submittedFrameIndex) {
    MetalDevice* releaseDevice = device ? device : ownerDevice_;
    if (texturePool_) {
        texturePool_->ReleaseTexture(depthTexture_, submittedFrameIndex);
    } else {
        depthTexture_.Shutdown(releaseDevice, submittedFrameIndex);
    }

    passDescriptor_ = nil;
    drawable_ = nil;
    layer_ = nil;
    device_ = nil;
    ownerDevice_ = nullptr;
    texturePool_ = nullptr;
    drawableSize_ = {};
    stats_ = {};
    renderPassSnapshot_ = {};
}

void MetalSwapchain::Resize(Window* window, int width, int height, uint64_t submittedFrameIndex) {
    if (!layer_ || !device_ || !window || !window->GetNativeHandle() || width <= 0 || height <= 0) {
        return;
    }

    @autoreleasepool {
        NSView* view = (__bridge NSView*)window->GetNativeHandle();
        const CGFloat scale = BackingScaleForView(view);
        const NSUInteger drawableWidth = static_cast<NSUInteger>(std::max<CGFloat>(1.0, width * scale));
        const NSUInteger drawableHeight = static_cast<NSUInteger>(std::max<CGFloat>(1.0, height * scale));

        layer_.contentsScale = scale;
        layer_.drawableSize = CGSizeMake(drawableWidth, drawableHeight);
        drawableSize_.width = static_cast<uint32_t>(drawableWidth);
        drawableSize_.height = static_cast<uint32_t>(drawableHeight);
        stats_.drawableSize = drawableSize_;
        ++stats_.resizeCount;

        if (texturePool_) {
            texturePool_->ReleaseTexture(depthTexture_, submittedFrameIndex);
        } else {
            depthTexture_.Shutdown(ownerDevice_, submittedFrameIndex);
        }

        RHI::TextureDesc depthDesc;
        depthDesc.extent = drawableSize_;
        depthDesc.format = depthFormat_;
        depthDesc.memory = RHI::ResourceMemory::DeviceLocal;
        depthDesc.usage = RHI::ResourceUsageFlag(RHI::ResourceUsage::DepthStencil);
        depthDesc.initialState = RHI::ResourceState::DepthWrite;
        depthDesc.debugName = "NEXT swapchain depth";

        if (!texturePool_ || !texturePool_->CreateTexture(depthTexture_, depthDesc)) {
            ++stats_.depthCreateFailureCount;
            NEXT_LOG_ERROR("Failed to create Metal depth texture");
        }

        NEXT_LOG_DEBUG("Metal drawable resized to %.0fx%.0f",
                       layer_.drawableSize.width,
                       layer_.drawableSize.height);
    }
}

bool MetalSwapchain::AcquireFrame(const RHI::ClearColor& clearColor, const MetalSceneResources* sceneResources) {
    ++stats_.acquireAttemptCount;
    drawable_ = nil;
    passDescriptor_ = nil;
    renderPassSnapshot_ = {};

    if (!layer_ || !depthTexture_.IsReady()) {
        ++stats_.acquireFailureCount;
        return false;
    }

    @autoreleasepool {
        drawable_ = [layer_ nextDrawable];
        if (!drawable_) {
            ++stats_.acquireFailureCount;
            NEXT_LOG_WARNING("Metal layer did not provide a drawable this frame");
            return false;
        }

        RHI::RenderPassDesc passDesc;
        passDesc.debugName = "NEXT swapchain pass";
        passDesc.colorAttachmentCount = 1;
        passDesc.colorAttachments[0].format = colorFormat_;
        passDesc.colorAttachments[0].loadAction = RHI::AttachmentLoadAction::Clear;
        passDesc.colorAttachments[0].storeAction = RHI::AttachmentStoreAction::Store;
        passDesc.colorAttachments[0].clearColor = clearColor;
        passDesc.hasDepthStencil = true;
        passDesc.depthStencilAttachment.format = depthFormat_;
        passDesc.depthStencilAttachment.loadAction = RHI::AttachmentLoadAction::Clear;
        passDesc.depthStencilAttachment.storeAction = RHI::AttachmentStoreAction::DontCare;
        passDesc.depthStencilAttachment.clearDepth = 1.0;

        MetalRenderPassAttachments attachments;
        attachments.colorTextures[0] = drawable_.texture;
        attachments.depthStencilTexture = depthTexture_.NativeTexture();

        const MetalRenderPassBuildResult buildResult =
            BuildMetalRenderPassDescriptor(passDesc, attachments);
        if (!buildResult) {
            ++stats_.acquireFailureCount;
            drawable_ = nil;
            passDescriptor_ = nil;
            renderPassSnapshot_ = {};
            NEXT_LOG_ERROR("Metal swapchain render pass build failed: %s (attachment=%u)",
                           MetalRenderPassBuildErrorName(buildResult.error),
                           buildResult.attachmentIndex);
            return false;
        }
        const SwapchainRenderPassFrameGraphResult frameGraphResult =
            CompileSwapchainRenderPassFrameGraph(passDesc, attachments, sceneResources);
        if (!frameGraphResult.validation) {
            ++stats_.acquireFailureCount;
            drawable_ = nil;
            passDescriptor_ = nil;
            renderPassSnapshot_ = {};
            NEXT_LOG_ERROR("Metal swapchain frame graph plan failed: %s (pass=%u access=%u resource=%u state=%s queue=%s)",
                           RHI::FrameGraphDescriptorErrorName(frameGraphResult.validation.error),
                           frameGraphResult.validation.passIndex,
                           frameGraphResult.validation.accessIndex,
                           frameGraphResult.validation.resourceIndex,
                           RHI::ResourceStateName(frameGraphResult.validation.state),
                           RHI::QueueClassName(frameGraphResult.validation.queueClass));
            return false;
        }

        passDescriptor_ = buildResult.descriptor;
        renderPassSnapshot_ = {};
        renderPassSnapshot_.ready = true;
        renderPassSnapshot_.frameGraphValidation = frameGraphResult.validation;
        renderPassSnapshot_.frameGraphTransitionCount = frameGraphResult.transitionCount;
        renderPassSnapshot_.frameGraphCompileResult = frameGraphResult.compileResult;
        renderPassSnapshot_.frameGraphResourceUsages = frameGraphResult.resourceUsages;
        renderPassSnapshot_.desc = passDesc;
        ++stats_.acquiredFrameCount;
        return true;
    }
}

void MetalSwapchain::RecordPresent() {
    ++stats_.presentedFrameCount;
}

void MetalSwapchain::ReleaseFrame() {
    if (drawable_ || passDescriptor_) {
        ++stats_.releasedFrameCount;
    }
    passDescriptor_ = nil;
    drawable_ = nil;
}

MetalSwapchainStats MetalSwapchain::GetStats() const {
    MetalSwapchainStats stats = stats_;
    stats.ready = layer_ != nil && depthTexture_.IsReady();
    stats.drawableSize = drawableSize_;
    stats.colorFormat = colorFormat_;
    stats.depthFormat = depthFormat_;
    stats.framebufferOnly = framebufferOnly_;
    stats.frameAcquired = drawable_ != nil;
    return stats;
}

} // namespace MetalBackend
} // namespace Next
