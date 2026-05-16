#include "next/renderer/metal/metal_renderer.h"

#include "metal_command.h"
#include "metal_conversions.h"
#include "metal_device.h"
#include "metal_resource.h"
#include "metal_resource_pool.h"
#include "metal_swapchain.h"
#include "metal_upload_queue.h"
#include "next/foundation/logger.h"
#include "next/platform/window.h"
#include "next/rhi/rhi.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

namespace Next {
namespace {

RendererShaderLibrarySource ToRendererShaderLibrarySource(MetalBackend::MetalShaderLibraryInputKind kind) {
    switch (kind) {
        case MetalBackend::MetalShaderLibraryInputKind::Metallib:
            return RendererShaderLibrarySource::Metallib;
        case MetalBackend::MetalShaderLibraryInputKind::Source:
            return RendererShaderLibrarySource::Source;
        case MetalBackend::MetalShaderLibraryInputKind::None:
        default:
            return RendererShaderLibrarySource::Unknown;
    }
}

RendererDrawStateStats MakeRendererDrawStateStats(const RHI::Extent2D& drawableSize, bool swapchainReady) {
    RendererDrawStateStats stats;
    stats.drawableSize = drawableSize;
    stats.viewport = RHI::ViewportDesc{
        0.0,
        0.0,
        static_cast<double>(drawableSize.width),
        static_cast<double>(drawableSize.height),
        0.0,
        1.0};
    stats.scissor = RHI::ScissorRectDesc{0, 0, drawableSize.width, drawableSize.height};

    const RHI::ViewportDescriptorValidation viewportValidation = RHI::ValidateViewportDesc(stats.viewport);
    const RHI::ScissorDescriptorValidation scissorValidation = RHI::ValidateScissorRectDesc(stats.scissor);
    stats.viewportError = viewportValidation.error;
    stats.scissorError = scissorValidation.error;
    stats.ready = swapchainReady && stats.HasDrawableSize() && stats.HasValidDrawState();
    return stats;
}

void RecordRendererIndexedDraw(RendererDrawSubmissionStats& stats,
                               const RendererGeometryStats& geometryStats,
                               bool debugDraw,
                               bool materialShaderResourceGroupArgumentBufferBound) {
    ++stats.lastFrameDrawCount;
    ++stats.lastFrameIndexedDrawCount;
    if (debugDraw) {
        ++stats.lastFrameDebugDrawCount;
    } else {
        ++stats.lastFrameBaseDrawCount;
    }
    if (materialShaderResourceGroupArgumentBufferBound) {
        ++stats.lastFrameMaterialShaderResourceGroupArgumentBufferBindCount;
    }
    stats.lastFrameIndexCount += geometryStats.indexCount;
    stats.lastFrameInstanceCount += geometryStats.instanceCount;
}

RendererDrawItemInfo MakeRendererDrawItemInfo(RendererDrawItemKind kind,
                                              uint32_t drawIndex,
                                              uint32_t debugCellIndex,
                                              bool debugCellPlaceholder,
                                              uint64_t uniformBufferOffset,
                                              const RendererGeometryStats& geometryStats) {
    RendererDrawItemInfo item;
    item.active = true;
    item.kind = kind;
    item.drawIndex = drawIndex;
    item.debugCellIndex = debugCellIndex;
    item.debugCellPlaceholder = debugCellPlaceholder;
    item.uniformBufferIndex = kRendererMaterialUniformBufferIndex;
    item.uniformBufferOffset = uniformBufferOffset;
    item.vertexBufferIndex = geometryStats.vertexBufferIndex;
    item.vertexStride = geometryStats.vertexStride;
    item.indexFormat = geometryStats.indexFormat;
    item.indexBufferByteOffset = geometryStats.indexBufferByteOffset;
    item.resolvedIndexBufferByteOffset = geometryStats.resolvedIndexBufferByteOffset;
    item.indexCount = geometryStats.indexCount;
    item.instanceCount = geometryStats.instanceCount;
    item.indexOffset = geometryStats.indexOffset;
    item.vertexOffset = geometryStats.vertexOffset;
    item.instanceOffset = geometryStats.instanceOffset;
    return item;
}

void RecordRendererDrawItem(RendererDrawItemStats& stats, const RendererDrawItemInfo& item) {
    if (stats.itemCount >= stats.capacity || stats.itemCount >= stats.items.size()) {
        return;
    }

    stats.items[stats.itemCount] = item;
    ++stats.itemCount;
    if (item.IsBaseDraw()) {
        ++stats.baseItemCount;
    } else if (item.IsDebugCellDraw()) {
        ++stats.debugItemCount;
        if (item.debugCellPlaceholder) {
            ++stats.placeholderDebugItemCount;
        }
    }
}

RendererDrawSubmissionStats MergeRendererDrawSubmissionStats(
    RendererDrawSubmissionStats frameStats,
    const RendererDrawSubmissionStats& previousStats) {
    const bool submittedFrame = frameStats.HasLastFrameDraws();
    frameStats.ready = frameStats.HasRequiredDrawState() && submittedFrame;
    frameStats.submittedFrameCount = previousStats.submittedFrameCount + (submittedFrame ? 1u : 0u);
    frameStats.submittedDrawCount = previousStats.submittedDrawCount + frameStats.lastFrameDrawCount;
    frameStats.submittedIndexedDrawCount =
        previousStats.submittedIndexedDrawCount + frameStats.lastFrameIndexedDrawCount;
    frameStats.submittedMaterialShaderResourceGroupArgumentBufferBindCount =
        previousStats.submittedMaterialShaderResourceGroupArgumentBufferBindCount +
        frameStats.lastFrameMaterialShaderResourceGroupArgumentBufferBindCount;
    frameStats.submittedIndexCount = previousStats.submittedIndexCount + frameStats.lastFrameIndexCount;
    frameStats.submittedInstanceCount =
        previousStats.submittedInstanceCount + frameStats.lastFrameInstanceCount;
    return frameStats;
}

void MatrixIdentity(float* m) {
    std::fill(m, m + 16, 0.0f);
    m[0] = 1.0f;
    m[5] = 1.0f;
    m[10] = 1.0f;
    m[15] = 1.0f;
}

void MatrixMultiply(const float* a, const float* b, float* out) {
    float result[16] = {};
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            for (int k = 0; k < 4; ++k) {
                result[col * 4 + row] += a[k * 4 + row] * b[col * 4 + k];
            }
        }
    }
    std::memcpy(out, result, sizeof(result));
}

void MatrixPerspective(float fovyRadians, float aspect, float nearZ, float farZ, float* out) {
    std::fill(out, out + 16, 0.0f);
    const float f = 1.0f / std::tan(fovyRadians * 0.5f);
    out[0] = f / aspect;
    out[5] = f;
    out[10] = farZ / (farZ - nearZ);
    out[11] = 1.0f;
    out[14] = -(farZ * nearZ) / (farZ - nearZ);
}

void MatrixTranslation(float x, float y, float z, float* out) {
    MatrixIdentity(out);
    out[12] = x;
    out[13] = y;
    out[14] = z;
}

void MatrixScale(float x, float y, float z, float* out) {
    MatrixIdentity(out);
    out[0] = x;
    out[5] = y;
    out[10] = z;
}

float VectorDot3(const float* a, const float* b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

void VectorCross3(const float* a, const float* b, float* out) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

void VectorNormalize3(float* v) {
    const float length = std::sqrt(VectorDot3(v, v));
    if (length <= 0.0001f) {
        v[0] = 0.0f;
        v[1] = 0.0f;
        v[2] = 1.0f;
        return;
    }

    const float invLength = 1.0f / length;
    v[0] *= invLength;
    v[1] *= invLength;
    v[2] *= invLength;
}

void MatrixLookAt(float eyeX, float eyeY, float eyeZ,
                  float targetX, float targetY, float targetZ,
                  const float* up, float* out) {
    const float eye[3] = {eyeX, eyeY, eyeZ};
    float zAxis[3] = {targetX - eyeX, targetY - eyeY, targetZ - eyeZ};
    VectorNormalize3(zAxis);

    float xAxis[3] = {};
    VectorCross3(up, zAxis, xAxis);
    VectorNormalize3(xAxis);

    float yAxis[3] = {};
    VectorCross3(zAxis, xAxis, yAxis);

    MatrixIdentity(out);
    out[0] = xAxis[0];
    out[4] = xAxis[1];
    out[8] = xAxis[2];
    out[12] = -VectorDot3(xAxis, eye);
    out[1] = yAxis[0];
    out[5] = yAxis[1];
    out[9] = yAxis[2];
    out[13] = -VectorDot3(yAxis, eye);
    out[2] = zAxis[0];
    out[6] = zAxis[1];
    out[10] = zAxis[2];
    out[14] = -VectorDot3(zAxis, eye);
}

void MatrixRotationX(float radians, float* out) {
    MatrixIdentity(out);
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    out[5] = c;
    out[6] = s;
    out[9] = -s;
    out[10] = c;
}

void MatrixRotationY(float radians, float* out) {
    MatrixIdentity(out);
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    out[0] = c;
    out[2] = -s;
    out[8] = s;
    out[10] = c;
}

} // namespace

struct MetalRenderer::Impl {
    MetalBackend::MetalDevice device;
    MetalBackend::MetalBufferPool bufferPool;
    MetalBackend::MetalTexturePool texturePool;
    MetalBackend::MetalSwapchain swapchain;
    MetalBackend::MetalCommandContext commandContext;
    MetalBackend::MetalUploadQueue uploadQueue;
    MetalBackend::MetalSceneResources resources;
    RendererDrawSubmissionStats drawSubmissionStats;
    RendererDrawItemStats drawItemStats;
};

MetalRenderer::MetalRenderer()
    : impl_(new Impl())
    , window_(nullptr)
    , width_(0)
    , height_(0)
    , initialized_(false)
    , frameActive_(false)
    , time_(0.0f) {}

MetalRenderer::~MetalRenderer() {
    Shutdown();
    delete impl_;
    impl_ = nullptr;
}

bool MetalRenderer::Initialize(Window* window) {
    if (!window || !window->GetNativeHandle()) {
        NEXT_LOG_ERROR("Invalid window for Metal renderer");
        return false;
    }

    @autoreleasepool {
        window_ = window;
        width_ = window->GetWidth();
        height_ = window->GetHeight();

        if (!impl_->device.Initialize()) {
            return false;
        }

        if (!impl_->bufferPool.Initialize(impl_->device) ||
            !impl_->texturePool.Initialize(impl_->device)) {
            Shutdown();
            return false;
        }

        if (!impl_->uploadQueue.Initialize(impl_->device, impl_->bufferPool)) {
            Shutdown();
            return false;
        }

        RHI::SwapchainDesc swapchainDesc;
        swapchainDesc.nativeWindow = window->GetNativeHandle();
        swapchainDesc.drawableSize.width = static_cast<uint32_t>(std::max(1, width_));
        swapchainDesc.drawableSize.height = static_cast<uint32_t>(std::max(1, height_));
        swapchainDesc.colorFormat = RHI::Format::BGRA8Unorm;
        swapchainDesc.depthFormat = RHI::Format::Depth32Float;
        swapchainDesc.framebufferOnly = true;

        if (!impl_->swapchain.Initialize(window_, impl_->device, impl_->texturePool, swapchainDesc)) {
            Shutdown();
            return false;
        }

        if (!impl_->resources.Initialize(
                impl_->device,
                impl_->bufferPool,
                impl_->texturePool,
                impl_->uploadQueue,
                impl_->swapchain.GetColorFormat(),
                impl_->swapchain.GetDepthFormat())) {
            Shutdown();
            return false;
        }

        impl_->drawSubmissionStats = {};
        impl_->drawItemStats = {};
        window_->SetResizeCallback([this](int w, int h) { Resize(w, h); });

        initialized_ = true;
        NEXT_LOG_INFO("Metal renderer initialized (%dx%d)", width_, height_);
        return true;
    }
}

void MetalRenderer::Shutdown() {
    if (!impl_ || (!initialized_ && !window_)) {
        return;
    }

    @autoreleasepool {
        if (window_) {
            window_->SetResizeCallback({});
        }

        const uint64_t submittedFrameIndex = impl_->commandContext.GetSubmittedFrameIndex();
        impl_->commandContext.Reset();
        impl_->resources.Shutdown(&impl_->device, submittedFrameIndex);
        impl_->swapchain.Shutdown(&impl_->device, submittedFrameIndex);
        impl_->uploadQueue.Shutdown();
        impl_->texturePool.Shutdown();
        impl_->bufferPool.Shutdown();
        impl_->device.Shutdown();

        window_ = nullptr;
        width_ = 0;
        height_ = 0;
        frameActive_ = false;
        initialized_ = false;

        NEXT_LOG_INFO("Metal renderer shutdown complete");
    }
}

void MetalRenderer::SetFrameDesc(const RendererFrameDesc& frame) {
    frameDesc_ = frame;
}

RendererDeviceInfo MetalRenderer::GetDeviceInfo() const {
    RendererDeviceInfo info;
    if (!initialized_) {
        return info;
    }

    info.available = true;
    info.backend = RendererBackend::Metal;
    info.features = impl_->device.GetFeatures();
    info.SetDeviceName(impl_->device.GetDeviceName());
    return info;
}

RendererLifetimeStats MetalRenderer::GetLifetimeStats() const {
    RendererLifetimeStats stats;
    if (!initialized_) {
        return stats;
    }

    const MetalBackend::MetalReleaseQueueStats releaseStats = impl_->device.GetReleaseQueueStats();
    stats.submittedFrameIndex = impl_->commandContext.GetSubmittedFrameIndex();
    stats.pendingReleaseObjectCount = releaseStats.pendingObjectCount;
    stats.peakPendingReleaseObjectCount = releaseStats.peakPendingObjectCount;
    stats.queuedReleaseObjectCount = releaseStats.queuedObjectCount;
    stats.collectedReleaseObjectCount = releaseStats.collectedObjectCount;
    stats.releaseCollectPassCount = releaseStats.collectPassCount;
    stats.forcedReleaseCollectPassCount = releaseStats.forcedCollectPassCount;
    stats.releaseCollectLatency = releaseStats.collectLatency;
    return stats;
}

RendererCommandStats MetalRenderer::GetCommandStats() const {
    RendererCommandStats stats;
    if (!initialized_) {
        return stats;
    }

    const MetalBackend::MetalCommandContextStats commandStats = impl_->commandContext.GetStats();
    stats.recording = commandStats.recording;
    stats.queueClass = commandStats.queueClass;
    stats.submittedFrameIndex = commandStats.submittedFrameIndex;
    stats.beginAttemptCount = commandStats.beginAttemptCount;
    stats.begunCommandBufferCount = commandStats.begunCommandBufferCount;
    stats.beginFailureCount = commandStats.beginFailureCount;
    stats.renderPassAttemptCount = commandStats.renderPassAttemptCount;
    stats.renderPassBeginCount = commandStats.renderPassBeginCount;
    stats.renderPassFailureCount = commandStats.renderPassFailureCount;
    stats.renderPassEndCount = commandStats.renderPassEndCount;
    stats.commitAttemptCount = commandStats.commitAttemptCount;
    stats.committedCommandBufferCount = commandStats.committedCommandBufferCount;
    stats.commitFailureCount = commandStats.commitFailureCount;
    stats.presentAttemptCount = commandStats.presentAttemptCount;
    stats.presentedCommandBufferCount = commandStats.presentedCommandBufferCount;
    stats.presentFailureCount = commandStats.presentFailureCount;
    stats.frameGraphPassAttemptCount = commandStats.frameGraphPassAttemptCount;
    stats.frameGraphPassEncodedCount = commandStats.frameGraphPassEncodedCount;
    stats.frameGraphPassFailureCount = commandStats.frameGraphPassFailureCount;
    stats.frameGraphDependencyEncodedCount = commandStats.frameGraphDependencyEncodedCount;
    stats.frameGraphAccessEncodedCount = commandStats.frameGraphAccessEncodedCount;
    stats.frameGraphTransitionEncodedCount = commandStats.frameGraphTransitionEncodedCount;
    stats.frameGraphAttachmentTransitionCount = commandStats.frameGraphAttachmentTransitionCount;
    stats.frameGraphBufferTransitionCount = commandStats.frameGraphBufferTransitionCount;
    stats.frameGraphShaderTransitionCount = commandStats.frameGraphShaderTransitionCount;
    stats.frameGraphCopyTransitionCount = commandStats.frameGraphCopyTransitionCount;
    stats.frameGraphPresentTransitionCount = commandStats.frameGraphPresentTransitionCount;
    stats.frameGraphOtherTransitionCount = commandStats.frameGraphOtherTransitionCount;
    stats.frameGraphAttachmentAccessCount = commandStats.frameGraphAttachmentAccessCount;
    stats.frameGraphBufferAccessCount = commandStats.frameGraphBufferAccessCount;
    stats.frameGraphShaderAccessCount = commandStats.frameGraphShaderAccessCount;
    stats.frameGraphCopyAccessCount = commandStats.frameGraphCopyAccessCount;
    stats.frameGraphPresentAccessCount = commandStats.frameGraphPresentAccessCount;
    stats.frameGraphOtherAccessCount = commandStats.frameGraphOtherAccessCount;
    stats.frameGraphShaderStageHintAccessCount = commandStats.frameGraphShaderStageHintAccessCount;
    stats.frameGraphVertexStageHintAccessCount = commandStats.frameGraphVertexStageHintAccessCount;
    stats.frameGraphFragmentStageHintAccessCount = commandStats.frameGraphFragmentStageHintAccessCount;
    stats.frameGraphComputeStageHintAccessCount = commandStats.frameGraphComputeStageHintAccessCount;
    stats.frameGraphResourceUseAttemptCount = commandStats.frameGraphResourceUseAttemptCount;
    stats.frameGraphResourceUseDeclaredCount = commandStats.frameGraphResourceUseDeclaredCount;
    stats.frameGraphResourceUseSkippedCount = commandStats.frameGraphResourceUseSkippedCount;
    stats.frameGraphResourceUseFailureCount = commandStats.frameGraphResourceUseFailureCount;
    stats.frameGraphBufferUseDeclaredCount = commandStats.frameGraphBufferUseDeclaredCount;
    stats.frameGraphTextureUseDeclaredCount = commandStats.frameGraphTextureUseDeclaredCount;
    stats.frameGraphVertexStageUseDeclaredCount = commandStats.frameGraphVertexStageUseDeclaredCount;
    stats.frameGraphFragmentStageUseDeclaredCount = commandStats.frameGraphFragmentStageUseDeclaredCount;
    stats.lastFrameGraphResourceUsePassIndex = commandStats.lastFrameGraphResourceUsePassIndex;
    stats.lastFrameGraphResourceUseAccessOffset = commandStats.lastFrameGraphResourceUseAccessOffset;
    stats.lastFrameGraphResourceUseAccessCount = commandStats.lastFrameGraphResourceUseAccessCount;
    stats.lastFrameGraphResourceUseDeclaredCount = commandStats.lastFrameGraphResourceUseDeclaredCount;
    stats.lastFrameGraphResourceUseSkippedCount = commandStats.lastFrameGraphResourceUseSkippedCount;
    stats.lastFrameGraphPassIndex = commandStats.lastFrameGraphPassIndex;
    stats.lastFrameGraphDependencyOffset = commandStats.lastFrameGraphDependencyOffset;
    stats.lastFrameGraphDependencyCount = commandStats.lastFrameGraphDependencyCount;
    stats.lastFrameGraphTransitionOffset = commandStats.lastFrameGraphTransitionOffset;
    stats.lastFrameGraphTransitionCount = commandStats.lastFrameGraphTransitionCount;
    stats.lastFrameGraphAccessOffset = commandStats.lastFrameGraphAccessOffset;
    stats.lastFrameGraphAccessCount = commandStats.lastFrameGraphAccessCount;
    stats.lastFrameGraphPassQueueClass = commandStats.lastFrameGraphPassQueueClass;
    return stats;
}

RendererRenderPassStats MetalRenderer::GetRenderPassStats() const {
    RendererRenderPassStats stats;
    if (!initialized_) {
        return stats;
    }

    const MetalBackend::MetalRenderPassSnapshot snapshot = impl_->swapchain.GetRenderPassSnapshot();
    const RHI::RenderPassDescriptorValidation validation = RHI::ValidateRenderPassDesc(snapshot.desc);
    stats.frameGraphValidation = snapshot.frameGraphValidation;
    stats.frameGraphTransitionCount = snapshot.frameGraphTransitionCount;
    stats.ready = snapshot.ready && static_cast<bool>(validation) && stats.HasValidFrameGraphPassPlan();
    stats.descriptorError = validation.error;
    stats.descriptorErrorAttachmentIndex = validation.attachmentIndex;
    stats.descriptorErrorFormat = validation.format;
    stats.SetDebugName(snapshot.desc.debugName);
    stats.colorAttachmentCount = snapshot.desc.colorAttachmentCount;
    const uint32_t colorAttachmentTableCount = std::min<uint32_t>(
        snapshot.desc.colorAttachmentCount,
        static_cast<uint32_t>(stats.colorAttachments.size()));
    for (uint32_t i = 0; i < colorAttachmentTableCount; ++i) {
        const RHI::RenderPassColorAttachmentDesc& attachment = snapshot.desc.colorAttachments[i];
        RendererRenderPassColorAttachmentInfo& attachmentInfo = stats.colorAttachments[i];
        attachmentInfo.active = true;
        attachmentInfo.index = i;
        attachmentInfo.format = attachment.format;
        attachmentInfo.loadAction = attachment.loadAction;
        attachmentInfo.storeAction = attachment.storeAction;
        attachmentInfo.clearColor = attachment.clearColor;
        attachmentInfo.resolve = i < snapshot.colorResolveAttachments.size() &&
            snapshot.colorResolveAttachments[i];
    }
    stats.hasDepthStencil = snapshot.desc.hasDepthStencil;
    if (snapshot.desc.hasDepthStencil) {
        const RHI::RenderPassDepthStencilAttachmentDesc& attachment =
            snapshot.desc.depthStencilAttachment;
        stats.depthStencilAttachment.active = true;
        stats.depthStencilAttachment.format = attachment.format;
        stats.depthStencilAttachment.loadAction = attachment.loadAction;
        stats.depthStencilAttachment.storeAction = attachment.storeAction;
        stats.depthStencilAttachment.clearDepth = attachment.clearDepth;
        stats.depthStencilAttachment.stencilLoadAction = attachment.stencilLoadAction;
        stats.depthStencilAttachment.stencilStoreAction = attachment.stencilStoreAction;
        stats.depthStencilAttachment.clearStencil = attachment.clearStencil;
        stats.depthStencilAttachment.resolve = snapshot.depthStencilResolveAttachment;
    }
    return stats;
}

RendererUploadQueueStats MetalRenderer::GetUploadQueueStats() const {
    RendererUploadQueueStats stats;
    if (!initialized_) {
        return stats;
    }

    stats.ready = impl_->uploadQueue.IsReady();
    stats.dedicatedQueue = impl_->uploadQueue.UsesDedicatedQueue();
    stats.queueClass = impl_->uploadQueue.GetQueueClass();
    stats.pendingUploadCount = impl_->uploadQueue.GetPendingUploadCount();
    stats.pendingUploadBytes = impl_->uploadQueue.GetPendingUploadBytes();
    stats.stagingCapacityBytes = impl_->uploadQueue.GetStagingCapacityBytes();
    stats.submittedUploadCount = impl_->uploadQueue.GetSubmittedUploadCount();
    stats.completedUploadCount = impl_->uploadQueue.GetCompletedUploadCount();
    stats.failedUploadCount = impl_->uploadQueue.GetFailedUploadCount();
    stats.lastSubmittedUpload = impl_->uploadQueue.GetLastSubmittedUpload().serial;
    stats.retainedStatusCount = impl_->uploadQueue.GetRetainedStatusCount();
    stats.retainedStatusCapacity = impl_->uploadQueue.GetRetainedStatusCapacity();
    return stats;
}

RendererPipelineStats MetalRenderer::GetPipelineStats() const {
    RendererPipelineStats stats;
    if (!initialized_) {
        return stats;
    }

    const MetalBackend::MetalPipelineCacheStats pipelineStats = impl_->resources.GetPipelineCacheStats();
    const MetalBackend::MetalShaderLibraryInput shaderInput = impl_->resources.GetShaderLibraryInput();
    const MetalBackend::MetalShaderLibraryDesc& shaderDesc = impl_->resources.GetShaderLibraryDesc();
    stats.ready = impl_->resources.IsReady();
    stats.shaderLibraryReady = shaderInput.IsValid();
    stats.shaderLibrarySource = ToRendererShaderLibrarySource(shaderInput.kind);
    stats.shaderManifestVersion = shaderDesc.manifestVersion;
    stats.shaderRequiredArgumentBufferTier = shaderDesc.requiredArgumentBufferTier;
    stats.shaderMaterialShaderResourceGroupArgumentBufferIndex =
        shaderDesc.materialShaderResourceGroupArgumentBufferIndex;
    stats.shaderMaterialShaderResourceGroupUniformArgumentIndex =
        shaderDesc.materialShaderResourceGroupUniformArgumentIndex;
    stats.shaderMaterialShaderResourceGroupTextureArgumentBaseIndex =
        shaderDesc.materialShaderResourceGroupTextureArgumentBaseIndex;
    stats.shaderMaterialShaderResourceGroupSamplerArgumentBaseIndex =
        shaderDesc.materialShaderResourceGroupSamplerArgumentBaseIndex;
    stats.SetPipelineDebugName(impl_->resources.GetRenderPipelineDebugName());
    if (const RHI::GraphicsPipelineDesc* pipelineDesc = impl_->resources.GetRenderPipelineDesc()) {
        stats.colorAttachmentCount = pipelineDesc->colorAttachmentCount;
        const uint32_t colorAttachmentTableCount = std::min<uint32_t>(
            pipelineDesc->colorAttachmentCount,
            static_cast<uint32_t>(stats.colorAttachments.size()));
        for (uint32_t i = 0; i < colorAttachmentTableCount; ++i) {
            const RHI::ColorAttachmentDesc& colorAttachment = pipelineDesc->colorAttachments[i];
            RendererPipelineColorAttachmentInfo& attachmentInfo = stats.colorAttachments[i];
            attachmentInfo.active = true;
            attachmentInfo.index = i;
            attachmentInfo.format = colorAttachment.format;
            attachmentInfo.blendEnabled = colorAttachment.blendEnabled;
            attachmentInfo.sourceColorBlendFactor = colorAttachment.sourceColorBlendFactor;
            attachmentInfo.destinationColorBlendFactor = colorAttachment.destinationColorBlendFactor;
            attachmentInfo.colorBlendOperation = colorAttachment.colorBlendOperation;
            attachmentInfo.sourceAlphaBlendFactor = colorAttachment.sourceAlphaBlendFactor;
            attachmentInfo.destinationAlphaBlendFactor = colorAttachment.destinationAlphaBlendFactor;
            attachmentInfo.alphaBlendOperation = colorAttachment.alphaBlendOperation;
            attachmentInfo.writeMask = colorAttachment.writeMask;

            if (i == 0) {
                stats.pipelineColorFormat = colorAttachment.format;
                stats.colorBlendStateReady = true;
                stats.colorBlendEnabled = colorAttachment.blendEnabled;
                stats.sourceColorBlendFactor = colorAttachment.sourceColorBlendFactor;
                stats.destinationColorBlendFactor = colorAttachment.destinationColorBlendFactor;
                stats.colorBlendOperation = colorAttachment.colorBlendOperation;
                stats.sourceAlphaBlendFactor = colorAttachment.sourceAlphaBlendFactor;
                stats.destinationAlphaBlendFactor = colorAttachment.destinationAlphaBlendFactor;
                stats.alphaBlendOperation = colorAttachment.alphaBlendOperation;
                stats.colorWriteMask = colorAttachment.writeMask;
            }
        }
        stats.pipelineDepthStencilFormat = pipelineDesc->depthStencilFormat;
        stats.pipelineSampleCount = pipelineDesc->multisampleState.sampleCount;
        stats.alphaToCoverageEnabled = pipelineDesc->multisampleState.alphaToCoverageEnabled;
        stats.rasterStateReady = true;
        stats.primitiveTopology = pipelineDesc->rasterState.primitiveTopology;
        stats.fillMode = pipelineDesc->rasterState.fillMode;
        stats.cullMode = pipelineDesc->rasterState.cullMode;
        stats.frontFace = pipelineDesc->rasterState.frontFace;
        stats.depthBias = pipelineDesc->rasterState.depthBias;
        stats.depthBiasClamp = pipelineDesc->rasterState.depthBiasClamp;
        stats.depthBiasSlopeScale = pipelineDesc->rasterState.depthBiasSlopeScale;
        stats.depthClipEnabled = pipelineDesc->rasterState.depthClipEnabled;
        stats.depthStencilStateReady = true;
        stats.depthTestEnabled = pipelineDesc->depthStencilState.depthTestEnabled;
        stats.depthWriteEnabled = pipelineDesc->depthStencilState.depthWriteEnabled;
        stats.depthCompare = pipelineDesc->depthStencilState.depthCompare;
        stats.stencilTestEnabled = pipelineDesc->depthStencilState.stencilTestEnabled;
        stats.stencilReadMask = pipelineDesc->depthStencilState.stencilReadMask;
        stats.stencilWriteMask = pipelineDesc->depthStencilState.stencilWriteMask;
        stats.frontStencilCompare = pipelineDesc->depthStencilState.frontStencil.compare;
        stats.frontStencilFailOperation = pipelineDesc->depthStencilState.frontStencil.stencilFailOp;
        stats.frontStencilDepthFailOperation = pipelineDesc->depthStencilState.frontStencil.depthFailOp;
        stats.frontStencilPassOperation = pipelineDesc->depthStencilState.frontStencil.passOp;
        stats.backStencilCompare = pipelineDesc->depthStencilState.backStencil.compare;
        stats.backStencilFailOperation = pipelineDesc->depthStencilState.backStencil.stencilFailOp;
        stats.backStencilDepthFailOperation = pipelineDesc->depthStencilState.backStencil.depthFailOp;
        stats.backStencilPassOperation = pipelineDesc->depthStencilState.backStencil.passOp;
        stats.vertexBufferCount = pipelineDesc->vertexInput.bufferCount;
        stats.vertexAttributeCount = pipelineDesc->vertexInput.attributeCount;
        const uint32_t vertexBufferTableCount = std::min<uint32_t>(
            pipelineDesc->vertexInput.bufferCount,
            static_cast<uint32_t>(stats.vertexBuffers.size()));
        for (uint32_t i = 0; i < vertexBufferTableCount; ++i) {
            const RHI::VertexBufferLayoutDesc& vertexBuffer = pipelineDesc->vertexInput.buffers[i];
            RendererPipelineVertexBufferInfo& vertexBufferInfo = stats.vertexBuffers[i];
            vertexBufferInfo.active = true;
            vertexBufferInfo.index = i;
            vertexBufferInfo.stride = vertexBuffer.stride;
            vertexBufferInfo.stepFunction = vertexBuffer.stepFunction;
            vertexBufferInfo.stepRate = vertexBuffer.stepRate;
        }
        const uint32_t vertexAttributeTableCount = std::min<uint32_t>(
            pipelineDesc->vertexInput.attributeCount,
            static_cast<uint32_t>(stats.vertexAttributes.size()));
        for (uint32_t i = 0; i < vertexAttributeTableCount; ++i) {
            const RHI::VertexAttributeDesc& vertexAttribute = pipelineDesc->vertexInput.attributes[i];
            RendererPipelineVertexAttributeInfo& vertexAttributeInfo = stats.vertexAttributes[i];
            vertexAttributeInfo.active = true;
            vertexAttributeInfo.index = i;
            vertexAttributeInfo.location = vertexAttribute.location;
            vertexAttributeInfo.bufferIndex = vertexAttribute.bufferIndex;
            vertexAttributeInfo.format = vertexAttribute.format;
            vertexAttributeInfo.offset = vertexAttribute.offset;
        }
        if (pipelineDesc->vertexInput.bufferCount > 0) {
            stats.primaryVertexBufferIndex = kRendererGeometryVertexBufferIndex;
            stats.primaryVertexStride = pipelineDesc->vertexInput.buffers[0].stride;
        }
    }
    stats.SetShaderDebugName(shaderDesc.debugName.c_str());
    stats.SetShaderManifestPath(shaderDesc.manifestPath.c_str());
    stats.SetShaderLibraryPath(shaderInput.path.c_str());
    stats.SetShaderEntryPoints(shaderDesc.vertexEntryPoint.c_str(), shaderDesc.fragmentEntryPoint.c_str());
    stats.SetShaderMaterialLayout(shaderDesc.materialLayout.c_str());
    stats.SetShaderPipelineLayout(shaderDesc.pipelineLayout.c_str());
    stats.cachedPipelineCount = pipelineStats.cachedPipelineCount;
    stats.requestCount = pipelineStats.requestCount;
    stats.hitCount = pipelineStats.hitCount;
    stats.missCount = pipelineStats.missCount;
    stats.failedCreateCount = pipelineStats.failedCreateCount;
    return stats;
}

RendererGeometryStats MetalRenderer::GetGeometryStats() const {
    if (!initialized_) {
        return {};
    }

    return impl_->resources.GetGeometryStats();
}

RendererDrawStateStats MetalRenderer::GetDrawStateStats() const {
    if (!initialized_) {
        return {};
    }

    const MetalBackend::MetalSwapchainStats swapchainStats = impl_->swapchain.GetStats();
    return MakeRendererDrawStateStats(swapchainStats.drawableSize, swapchainStats.ready);
}

RendererDrawSubmissionStats MetalRenderer::GetDrawSubmissionStats() const {
    if (!initialized_) {
        return {};
    }

    return impl_->drawSubmissionStats;
}

RendererDrawItemStats MetalRenderer::GetDrawItemStats() const {
    if (!initialized_) {
        return {};
    }

    return impl_->drawItemStats;
}

RendererSwapchainStats MetalRenderer::GetSwapchainStats() const {
    RendererSwapchainStats stats;
    if (!initialized_) {
        return stats;
    }

    const MetalBackend::MetalSwapchainStats swapchainStats = impl_->swapchain.GetStats();
    stats.ready = swapchainStats.ready;
    stats.drawableSize = swapchainStats.drawableSize;
    stats.colorFormat = swapchainStats.colorFormat;
    stats.depthFormat = swapchainStats.depthFormat;
    stats.framebufferOnly = swapchainStats.framebufferOnly;
    stats.resizeCount = swapchainStats.resizeCount;
    stats.depthCreateFailureCount = swapchainStats.depthCreateFailureCount;
    stats.acquireAttemptCount = swapchainStats.acquireAttemptCount;
    stats.acquiredFrameCount = swapchainStats.acquiredFrameCount;
    stats.acquireFailureCount = swapchainStats.acquireFailureCount;
    stats.presentedFrameCount = swapchainStats.presentedFrameCount;
    stats.releasedFrameCount = swapchainStats.releasedFrameCount;
    stats.frameAcquired = swapchainStats.frameAcquired;
    return stats;
}

RendererSamplerStats MetalRenderer::GetSamplerStats() const {
    if (!initialized_) {
        return {};
    }

    return impl_->resources.GetSamplerStats();
}

RendererMaterialStats MetalRenderer::GetMaterialStats() const {
    if (!initialized_) {
        return {};
    }

    return impl_->resources.GetMaterialStats();
}

RendererResourceStateStats MetalRenderer::GetResourceStateStats() const {
    if (!initialized_) {
        return {};
    }

    return impl_->resources.GetResourceStateStats();
}

RendererTextureUploadHandle MetalRenderer::UploadTexture2D(const RendererTextureUploadDesc& texture) {
    if (!initialized_ || frameActive_ || !impl_->resources.IsReady()) {
        return {};
    }

    @autoreleasepool {
        return impl_->resources.UploadMaterialTexture(
            impl_->device,
            impl_->uploadQueue,
            texture,
            impl_->commandContext.GetSubmittedFrameIndex());
    }
}

RendererTextureUploadStats MetalRenderer::GetTextureUploadStats() {
    if (!initialized_) {
        return {};
    }

    impl_->resources.PromoteCompletedMaterialTextureUploads(
        impl_->uploadQueue,
        impl_->commandContext.GetSubmittedFrameIndex());
    return impl_->resources.GetTextureUploadStats();
}

RendererResourcePoolStats MetalRenderer::GetResourcePoolStats() {
    RendererResourcePoolStats stats;
    if (!initialized_) {
        return stats;
    }

    stats.buffers = impl_->bufferPool.GetStats();
    stats.textures = impl_->texturePool.GetStats();
    return stats;
}

bool MetalRenderer::SetResourcePoolBudget(const RendererResourcePoolBudgetDesc& budget) {
    if (!initialized_) {
        return false;
    }

    switch (budget.resourceType) {
        case RHI::ResourceType::Buffer:
            return impl_->bufferPool.SetMemoryBudget(budget.memory, budget.budgetBytes);
        case RHI::ResourceType::Texture:
            return impl_->texturePool.SetMemoryBudget(budget.memory, budget.budgetBytes);
        default:
            return false;
    }
}

RendererTextureUploadStatus MetalRenderer::GetTextureUploadStatus(RendererTextureUploadHandle handle) {
    if (!initialized_) {
        return RendererTextureUploadStatus::Unknown;
    }

    impl_->resources.PromoteCompletedMaterialTextureUploads(
        impl_->uploadQueue,
        impl_->commandContext.GetSubmittedFrameIndex());
    return impl_->resources.GetTextureUploadStatus(handle, impl_->uploadQueue);
}

bool MetalRenderer::GetTextureInfo(RendererTextureHandle texture, RendererTextureInfo& outInfo) {
    outInfo = {};
    if (!initialized_) {
        return false;
    }

    impl_->resources.PromoteCompletedMaterialTextureUploads(
        impl_->uploadQueue,
        impl_->commandContext.GetSubmittedFrameIndex());
    return impl_->resources.GetTextureInfo(texture, outInfo);
}

RendererMaterialHandle MetalRenderer::CreateMaterial(const RendererMaterialDesc& material) {
    if (!initialized_ || !impl_->resources.IsReady()) {
        return {};
    }

    return impl_->resources.CreateMaterial(material);
}

bool MetalRenderer::UpdateMaterial(RendererMaterialHandle handle, const RendererMaterialDesc& material) {
    if (!initialized_ || !impl_->resources.IsReady()) {
        return false;
    }

    return impl_->resources.UpdateMaterial(handle, material);
}

bool MetalRenderer::SetActiveMaterial(RendererMaterialHandle handle) {
    if (!initialized_ || !impl_->resources.IsReady()) {
        return false;
    }

    return impl_->resources.SetActiveMaterial(handle);
}

bool MetalRenderer::GetMaterialInfo(RendererMaterialHandle handle, RendererMaterialInfo& outInfo) {
    outInfo = {};
    if (!initialized_ || !impl_->resources.IsReady()) {
        return false;
    }

    return impl_->resources.GetMaterialInfo(handle, outInfo);
}

void MetalRenderer::BeginFrame() {
    if (!initialized_ || frameActive_) {
        return;
    }

    @autoreleasepool {
        impl_->resources.PromoteCompletedMaterialTextureUploads(
            impl_->uploadQueue,
            impl_->commandContext.GetSubmittedFrameIndex());

        const double pulse = 0.5 + 0.5 * std::sin(static_cast<double>(time_) * 0.7);
        RHI::ClearColor clearColor;
        clearColor.r = 0.05;
        clearColor.g = 0.09 + 0.08 * pulse;
        clearColor.b = 0.13 + 0.12 * pulse;
        clearColor.a = 1.0;

        if (!impl_->swapchain.AcquireFrame(clearColor, &impl_->resources)) {
            return;
        }
        if (!impl_->commandContext.Begin(impl_->device, RHI::QueueClass::Graphics)) {
            impl_->swapchain.ReleaseFrame();
            return;
        }

        frameActive_ = true;
    }
}

void MetalRenderer::Render() {
    if (!initialized_ || !frameActive_ || !impl_->commandContext.IsRecording() || !impl_->resources.IsReady()) {
        return;
    }

    @autoreleasepool {
        const RendererMaterialDesc activeMaterial = impl_->resources.GetActiveMaterialDesc();
        MetalBackend::MetalUniforms uniforms = {};
        float rotationX[16] = {};
        float rotationY[16] = {};
        float model[16] = {};
        float view[16] = {};
        float modelView[16] = {};
        float projection[16] = {};

        MatrixRotationX(time_ * 0.65f, rotationX);
        MatrixRotationY(time_, rotationY);
        MatrixMultiply(rotationY, rotationX, model);
        MatrixLookAt(frameDesc_.cameraPosition[0],
                     frameDesc_.cameraPosition[1],
                     frameDesc_.cameraPosition[2],
                     frameDesc_.cameraTarget[0],
                     frameDesc_.cameraTarget[1],
                     frameDesc_.cameraTarget[2],
                     frameDesc_.cameraUp,
                     view);
        MatrixMultiply(view, model, modelView);

        const float aspect = height_ > 0 ? static_cast<float>(width_) / static_cast<float>(height_) : 1.0f;
        MatrixPerspective(60.0f * 3.1415926535f / 180.0f, aspect, 0.1f, 2000.0f, projection);
        MatrixMultiply(projection, modelView, uniforms.mvp);
        std::memcpy(uniforms.model, model, sizeof(model));
        uniforms.lightDirection[0] = -0.35f;
        uniforms.lightDirection[1] = -0.75f;
        uniforms.lightDirection[2] = -0.55f;
        uniforms.lightDirection[3] = 0.0f;
        uniforms.cameraPosition[0] = frameDesc_.cameraPosition[0];
        uniforms.cameraPosition[1] = frameDesc_.cameraPosition[1];
        uniforms.cameraPosition[2] = frameDesc_.cameraPosition[2];
        uniforms.cameraPosition[3] = 1.0f;
        uniforms.material[0] = activeMaterial.roughness;
        uniforms.material[1] = activeMaterial.metallic;
        uniforms.material[2] = activeMaterial.exposure;
        uniforms.material[3] = 0.0f;
        uniforms.ambientColor[0] = 0.05f;
        uniforms.ambientColor[1] = 0.06f;
        uniforms.ambientColor[2] = 0.08f;
        uniforms.ambientColor[3] = 1.0f;
        uniforms.debugTint[0] = activeMaterial.baseColorFactor[0];
        uniforms.debugTint[1] = activeMaterial.baseColorFactor[1];
        uniforms.debugTint[2] = activeMaterial.baseColorFactor[2];
        uniforms.debugTint[3] = activeMaterial.baseColorFactor[3];

        const size_t debugCellCount = frameDesc_.RenderedDebugCellCount();
        std::array<uint8_t, MetalBackend::kMetalUniformStride * (kMaxRendererDebugCells + 1)> uniformUploadBytes = {};
        uint8_t* uniformBytes = uniformUploadBytes.data();
        std::memcpy(uniformBytes, &uniforms, sizeof(uniforms));

        for (size_t i = 0; i < debugCellCount; ++i) {
            const RendererDebugCell& cell = frameDesc_.debugCells[i];
            const bool placeholder = cell.IsPlaceholder();
            const float halfSize = std::max(1.0f, cell.size) * 0.47f;

            float scale[16] = {};
            float translation[16] = {};
            MetalBackend::MetalUniforms cellUniforms = uniforms;

            MatrixScale(halfSize, 0.08f, halfSize, scale);
            MatrixTranslation(cell.center[0], cell.center[1] - 1.0f, cell.center[2], translation);
            MatrixMultiply(translation, scale, cellUniforms.model);
            MatrixMultiply(view, cellUniforms.model, modelView);
            MatrixMultiply(projection, modelView, cellUniforms.mvp);

            cellUniforms.material[0] = 0.85f;
            cellUniforms.material[1] = 0.0f;
            cellUniforms.material[2] = 1.4f;
            cellUniforms.debugTint[0] = placeholder ? 1.0f : 0.14f;
            cellUniforms.debugTint[1] = placeholder ? 0.64f : 0.86f;
            cellUniforms.debugTint[2] = placeholder ? 0.18f : 0.58f;
            cellUniforms.debugTint[3] = 1.0f;

            const NSUInteger uniformOffset = static_cast<NSUInteger>((i + 1) * MetalBackend::kMetalUniformStride);
            std::memcpy(uniformBytes + uniformOffset, &cellUniforms, sizeof(cellUniforms));
        }

        const size_t uniformUploadSize = MetalBackend::kMetalUniformStride * (debugCellCount + 1);
        if (!impl_->resources.UploadUniforms(
                impl_->device, impl_->uploadQueue, uniformUploadBytes.data(), uniformUploadSize)) {
            NEXT_LOG_ERROR("Skipping Metal render: failed to upload frame uniforms");
            return;
        }
        if (!impl_->resources.EncodeMaterialArgumentBuffers(debugCellCount + 1)) {
            NEXT_LOG_ERROR("Skipping Metal render: failed to encode per-draw material argument buffers");
            return;
        }

        if (!impl_->commandContext.EncodeFrameGraphPassTransitions(
                impl_->swapchain.CurrentFrameGraphCompileResult(), 0)) {
            return;
        }

        id<MTLRenderCommandEncoder> encoder =
            impl_->commandContext.BeginRenderPass(impl_->swapchain.CurrentPassDescriptor());
        if (!encoder) {
            return;
        }
        if (!impl_->commandContext.EncodeFrameGraphRenderPassResourceUsages(
                encoder,
                impl_->swapchain.CurrentFrameGraphCompileResult(),
                impl_->swapchain.CurrentFrameGraphResourceUsageTable(),
                0)) {
            impl_->commandContext.EndRenderPass(encoder);
            return;
        }

        const RendererDrawStateStats drawState = MakeRendererDrawStateStats(impl_->swapchain.GetDrawableSize(), true);
        if (!drawState.HasValidDrawState()) {
            NEXT_LOG_ERROR("Skipping Metal render: invalid viewport/scissor (%s/%s)",
                           RHI::ViewportDescriptorErrorName(drawState.viewportError),
                           RHI::ScissorDescriptorErrorName(drawState.scissorError));
            impl_->commandContext.EndRenderPass(encoder);
            return;
        }
        [encoder setViewport:MetalBackend::ToMetalViewport(drawState.viewport)];
        [encoder setScissorRect:MetalBackend::ToMetalScissorRect(drawState.scissor)];

        RendererDrawSubmissionStats drawSubmissionStats;
        RendererDrawItemStats drawItemStats;
        const RendererGeometryStats geometryStats = impl_->resources.GetGeometryStats();
        drawSubmissionStats.pipelineReady = impl_->resources.IsReady();
        drawSubmissionStats.geometryReady = geometryStats.HasReadyGeometry();
        drawSubmissionStats.drawStateReady = drawState.HasReadyDrawState();
        drawSubmissionStats.materialShaderResourceGroupArgumentBufferBindingIndex =
            impl_->resources.GetShaderLibraryDesc().materialShaderResourceGroupArgumentBufferIndex;
        drawItemStats.culledDebugItemCount = static_cast<uint32_t>(
            std::min(frameDesc_.DebugCellOverflowCount(), static_cast<size_t>(std::numeric_limits<uint32_t>::max())));

        const bool baseMaterialArgumentBufferBound = impl_->resources.BindDrawState(encoder, 0, false);
        if (impl_->resources.DrawCube(encoder)) {
            RecordRendererIndexedDraw(
                drawSubmissionStats, geometryStats, false, baseMaterialArgumentBufferBound);
            RecordRendererDrawItem(
                drawItemStats,
                MakeRendererDrawItemInfo(RendererDrawItemKind::Base,
                                         drawItemStats.itemCount,
                                         kRendererInvalidBindingIndex,
                                         false,
                                         0,
                                         geometryStats));
        }

        for (size_t i = 0; i < debugCellCount; ++i) {
            const NSUInteger uniformOffset = static_cast<NSUInteger>((i + 1) * MetalBackend::kMetalUniformStride);
            const bool debugMaterialArgumentBufferBound =
                impl_->resources.BindDrawState(encoder, uniformOffset, false);
            if (impl_->resources.DrawCube(encoder)) {
                RecordRendererIndexedDraw(
                    drawSubmissionStats, geometryStats, true, debugMaterialArgumentBufferBound);
                const RendererDebugCell& cell = frameDesc_.debugCells[i];
                RecordRendererDrawItem(
                    drawItemStats,
                    MakeRendererDrawItemInfo(RendererDrawItemKind::DebugCell,
                                             drawItemStats.itemCount,
                                             static_cast<uint32_t>(i),
                                             cell.IsPlaceholder(),
                                             static_cast<uint64_t>(uniformOffset),
                                             geometryStats));
            }
        }

        drawItemStats.ready = drawSubmissionStats.HasRequiredDrawState() && drawItemStats.HasItems();
        impl_->drawItemStats = drawItemStats;
        impl_->drawSubmissionStats =
            MergeRendererDrawSubmissionStats(drawSubmissionStats, impl_->drawSubmissionStats);
        impl_->commandContext.EndRenderPass(encoder);
    }
}

void MetalRenderer::EndFrame() {
    if (!initialized_ || !frameActive_) {
        return;
    }

    @autoreleasepool {
        const bool encodedPresentScope = impl_->commandContext.EncodeFrameGraphPassTransitions(
            impl_->swapchain.CurrentFrameGraphCompileResult(), 1);
        const bool submitted = encodedPresentScope &&
            impl_->commandContext.PresentAndCommit(impl_->swapchain.CurrentDrawable());
        if (!encodedPresentScope) {
            impl_->commandContext.Reset();
        }
        if (submitted) {
            impl_->swapchain.RecordPresent();
        }
        impl_->swapchain.ReleaseFrame();
        if (submitted) {
            impl_->device.CollectReleasedResources(impl_->commandContext.GetSubmittedFrameIndex());
        }
        frameActive_ = false;
        time_ += 1.0f / 60.0f;
    }
}

void MetalRenderer::Resize(int width, int height) {
    if (!impl_ || width <= 0 || height <= 0) {
        return;
    }

    @autoreleasepool {
        width_ = width;
        height_ = height;
        impl_->swapchain.Resize(window_, width_, height_, impl_->commandContext.GetSubmittedFrameIndex());
    }
}

} // namespace Next
