#pragma once

#include "next/platform/window.h"
#include "next/rhi/command.h"
#include "next/rhi/device.h"
#include "next/rhi/frame_graph.h"
#include "next/rhi/pipeline.h"
#include "next/rhi/render_pass.h"
#include "next/rhi/resource.h"
#include "next/rhi/shader_resource_group.h"
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace Next {

enum class RendererBackend : uint8_t {
    Auto = 0,
    DX12,
    Metal,
    Null,
};

static constexpr size_t kRendererDeviceNameMaxLength = 128;
static constexpr size_t kRendererPipelineDebugNameMaxLength = 128;
static constexpr size_t kRendererShaderDebugNameMaxLength = 128;
static constexpr size_t kRendererShaderLibraryPathMaxLength = 256;
static constexpr size_t kRendererShaderEntryPointMaxLength = 64;
static constexpr size_t kRendererShaderMaterialLayoutMaxLength = 64;
static constexpr size_t kRendererShaderPipelineLayoutMaxLength = 64;
static constexpr size_t kRendererRenderPassDebugNameMaxLength = 128;
static constexpr size_t kRendererResourceStateDebugNameMaxLength = 128;
static constexpr size_t kRendererShaderResourceBindingDebugNameMaxLength = 64;
static constexpr size_t kRendererRenderPassColorAttachmentMaxCount = RHI::kMaxRenderPassColorAttachments;
static constexpr size_t kRendererPipelineColorAttachmentMaxCount = RHI::kMaxGraphicsPipelineColorAttachments;
static constexpr size_t kRendererPipelineVertexBufferMaxCount = RHI::kMaxGraphicsPipelineVertexBuffers;
static constexpr size_t kRendererPipelineVertexAttributeMaxCount = RHI::kMaxGraphicsPipelineVertexAttributes;
static constexpr uint32_t kRendererInvalidBindingIndex = std::numeric_limits<uint32_t>::max();
static constexpr uint32_t kRendererGeometryVertexBufferIndex = 0;

struct RendererDeviceInfo {
    RendererBackend backend = RendererBackend::Null;
    RHI::DeviceFeatures features;
    char deviceName[kRendererDeviceNameMaxLength] = "unknown";
    bool available = false;

    const char* GetDeviceName() const { return deviceName; }
    bool HasBackend() const { return backend != RendererBackend::Null && backend != RendererBackend::Auto; }
    bool HasDeviceName() const {
        if (deviceName[0] == '\0') {
            return false;
        }

        const char* unknown = "unknown";
        size_t i = 0;
        for (; deviceName[i] != '\0' && unknown[i] != '\0'; ++i) {
            if (deviceName[i] != unknown[i]) {
                return true;
            }
        }
        return deviceName[i] != unknown[i];
    }
    bool SupportsQueueClass(RHI::QueueClass queueClass) const {
        return features.SupportsQueueClass(queueClass);
    }
    bool HasDedicatedQueueClass(RHI::QueueClass queueClass) const {
        return features.HasDedicatedQueueClass(queueClass);
    }
    bool SupportsGraphicsQueue() const { return SupportsQueueClass(RHI::QueueClass::Graphics); }
    bool SupportsComputeQueue() const { return SupportsQueueClass(RHI::QueueClass::Compute); }
    bool SupportsCopyQueue() const { return SupportsQueueClass(RHI::QueueClass::Copy); }
    bool HasDedicatedGraphicsQueue() const { return HasDedicatedQueueClass(RHI::QueueClass::Graphics); }
    bool HasDedicatedComputeQueue() const { return HasDedicatedQueueClass(RHI::QueueClass::Compute); }
    bool HasDedicatedCopyQueue() const { return HasDedicatedQueueClass(RHI::QueueClass::Copy); }
    bool HasQueueSupport() const { return features.supportedQueueClasses != 0; }
    bool HasDedicatedQueueSupport() const { return features.dedicatedQueueClasses != 0; }
    bool HasUnifiedMemory() const { return features.unifiedMemory; }
    bool HasArgumentBuffers() const { return features.argumentBuffers; }
    bool HasBindlessResources() const { return features.bindlessResources; }
    bool HasAsyncUploadQueue() const { return features.asyncUploadQueue; }
    bool HasArgumentBufferTier() const {
        return features.argumentBufferTier != RHI::ArgumentBufferTier::Unsupported;
    }
    bool SupportsArgumentBufferTier(RHI::ArgumentBufferTier tier) const {
        return features.SupportsArgumentBufferTier(tier);
    }
    bool HasDeviceCapabilities() const {
        return HasQueueSupport() || HasDedicatedQueueSupport() || HasUnifiedMemory() ||
            HasArgumentBuffers() || HasArgumentBufferTier() || HasBindlessResources() || HasAsyncUploadQueue();
    }
    bool HasRenderableDevice() const { return available && HasBackend() && SupportsGraphicsQueue(); }

    void SetDeviceName(const char* name) {
        const char* source = (name && name[0] != '\0') ? name : "unknown";
        size_t i = 0;
        for (; i + 1 < kRendererDeviceNameMaxLength && source[i] != '\0'; ++i) {
            deviceName[i] = source[i];
        }
        deviceName[i] = '\0';
        for (++i; i < kRendererDeviceNameMaxLength; ++i) {
            deviceName[i] = '\0';
        }
    }
};

struct RendererLifetimeStats {
    uint64_t submittedFrameIndex = 0;
    size_t pendingReleaseObjectCount = 0;
    size_t peakPendingReleaseObjectCount = 0;
    uint64_t queuedReleaseObjectCount = 0;
    uint64_t collectedReleaseObjectCount = 0;
    uint64_t releaseCollectPassCount = 0;
    uint64_t forcedReleaseCollectPassCount = 0;
    uint32_t releaseCollectLatency = 0;

    bool HasSubmittedFrames() const { return submittedFrameIndex != 0; }
    bool HasPendingReleases() const { return pendingReleaseObjectCount != 0; }
    bool HasPeakPendingReleases() const { return peakPendingReleaseObjectCount != 0; }
    bool HasQueuedReleases() const { return queuedReleaseObjectCount != 0; }
    bool HasCollectedReleases() const { return collectedReleaseObjectCount != 0; }
    bool HasReleaseCollectPasses() const { return releaseCollectPassCount != 0; }
    bool HasForcedReleaseCollectPasses() const { return forcedReleaseCollectPassCount != 0; }
    bool HasReleaseQueueActivity() const {
        return HasPendingReleases() || HasQueuedReleases() || HasCollectedReleases() ||
            HasReleaseCollectPasses() || HasForcedReleaseCollectPasses();
    }
};

struct RendererCommandStats {
    bool recording = false;
    RHI::QueueClass queueClass = RHI::QueueClass::Graphics;
    uint64_t submittedFrameIndex = 0;
    uint64_t beginAttemptCount = 0;
    uint64_t begunCommandBufferCount = 0;
    uint64_t beginFailureCount = 0;
    uint64_t renderPassAttemptCount = 0;
    uint64_t renderPassBeginCount = 0;
    uint64_t renderPassFailureCount = 0;
    uint64_t renderPassEndCount = 0;
    uint64_t commitAttemptCount = 0;
    uint64_t committedCommandBufferCount = 0;
    uint64_t commitFailureCount = 0;
    uint64_t presentAttemptCount = 0;
    uint64_t presentedCommandBufferCount = 0;
    uint64_t presentFailureCount = 0;
    uint64_t frameGraphPassAttemptCount = 0;
    uint64_t frameGraphPassEncodedCount = 0;
    uint64_t frameGraphPassFailureCount = 0;
    uint64_t frameGraphDependencyEncodedCount = 0;
    uint64_t frameGraphAccessEncodedCount = 0;
    uint64_t frameGraphTransitionEncodedCount = 0;
    uint64_t frameGraphAttachmentTransitionCount = 0;
    uint64_t frameGraphBufferTransitionCount = 0;
    uint64_t frameGraphShaderTransitionCount = 0;
    uint64_t frameGraphCopyTransitionCount = 0;
    uint64_t frameGraphPresentTransitionCount = 0;
    uint64_t frameGraphOtherTransitionCount = 0;
    uint64_t frameGraphAttachmentAccessCount = 0;
    uint64_t frameGraphBufferAccessCount = 0;
    uint64_t frameGraphShaderAccessCount = 0;
    uint64_t frameGraphCopyAccessCount = 0;
    uint64_t frameGraphPresentAccessCount = 0;
    uint64_t frameGraphOtherAccessCount = 0;
    uint64_t frameGraphShaderStageHintAccessCount = 0;
    uint64_t frameGraphVertexStageHintAccessCount = 0;
    uint64_t frameGraphFragmentStageHintAccessCount = 0;
    uint64_t frameGraphComputeStageHintAccessCount = 0;
    uint64_t frameGraphResourceUseAttemptCount = 0;
    uint64_t frameGraphResourceUseDeclaredCount = 0;
    uint64_t frameGraphResourceUseSkippedCount = 0;
    uint64_t frameGraphResourceUseFailureCount = 0;
    uint64_t frameGraphBufferUseDeclaredCount = 0;
    uint64_t frameGraphTextureUseDeclaredCount = 0;
    uint64_t frameGraphVertexStageUseDeclaredCount = 0;
    uint64_t frameGraphFragmentStageUseDeclaredCount = 0;
    uint32_t lastFrameGraphResourceUsePassIndex = 0;
    uint32_t lastFrameGraphResourceUseAccessOffset = 0;
    uint32_t lastFrameGraphResourceUseAccessCount = 0;
    uint64_t lastFrameGraphResourceUseDeclaredCount = 0;
    uint64_t lastFrameGraphResourceUseSkippedCount = 0;
    uint32_t lastFrameGraphPassIndex = 0;
    uint32_t lastFrameGraphDependencyOffset = 0;
    uint32_t lastFrameGraphDependencyCount = 0;
    uint32_t lastFrameGraphTransitionOffset = 0;
    uint32_t lastFrameGraphTransitionCount = 0;
    uint32_t lastFrameGraphAccessOffset = 0;
    uint32_t lastFrameGraphAccessCount = 0;
    RHI::QueueClass lastFrameGraphPassQueueClass = RHI::QueueClass::Graphics;

    bool HasSubmittedFrames() const { return submittedFrameIndex != 0; }
    bool HasBeginAttempts() const { return beginAttemptCount != 0; }
    bool HasBegunCommandBuffers() const { return begunCommandBufferCount != 0; }
    bool HasRenderPassAttempts() const { return renderPassAttemptCount != 0; }
    bool HasRenderPasses() const { return renderPassBeginCount != 0 || renderPassEndCount != 0; }
    bool HasCommitAttempts() const { return commitAttemptCount != 0; }
    bool HasCommittedCommandBuffers() const { return committedCommandBufferCount != 0; }
    bool HasPresentAttempts() const { return presentAttemptCount != 0; }
    bool HasPresentedCommandBuffers() const { return presentedCommandBufferCount != 0; }
    bool HasFrameGraphPassAttempts() const { return frameGraphPassAttemptCount != 0; }
    bool HasFrameGraphPasses() const { return frameGraphPassEncodedCount != 0; }
    bool HasFrameGraphDependencies() const { return frameGraphDependencyEncodedCount != 0; }
    bool HasFrameGraphAccesses() const { return frameGraphAccessEncodedCount != 0; }
    bool HasFrameGraphTransitions() const { return frameGraphTransitionEncodedCount != 0; }
    bool HasFrameGraphTransitionSummary() const {
        return frameGraphAttachmentTransitionCount != 0 || frameGraphBufferTransitionCount != 0 ||
            frameGraphShaderTransitionCount != 0 || frameGraphCopyTransitionCount != 0 ||
            frameGraphPresentTransitionCount != 0 || frameGraphOtherTransitionCount != 0;
    }
    bool HasFrameGraphAccessSummary() const {
        return frameGraphAttachmentAccessCount != 0 || frameGraphBufferAccessCount != 0 ||
            frameGraphShaderAccessCount != 0 || frameGraphCopyAccessCount != 0 ||
            frameGraphPresentAccessCount != 0 || frameGraphOtherAccessCount != 0;
    }
    bool HasFrameGraphShaderStageHints() const { return frameGraphShaderStageHintAccessCount != 0; }
    bool HasFrameGraphResourceUseAttempts() const { return frameGraphResourceUseAttemptCount != 0; }
    bool HasFrameGraphResourceUses() const { return frameGraphResourceUseDeclaredCount != 0; }
    bool HasFrameGraphSkippedResourceUses() const { return frameGraphResourceUseSkippedCount != 0; }
    bool HasFrameGraphStageResourceUses() const {
        return frameGraphVertexStageUseDeclaredCount != 0 || frameGraphFragmentStageUseDeclaredCount != 0;
    }
    bool HasCommandActivity() const {
        return HasSubmittedFrames() || HasBeginAttempts() || HasRenderPassAttempts() ||
            HasCommitAttempts() || HasPresentAttempts() || HasFrameGraphPassAttempts() ||
            HasFrameGraphResourceUseAttempts();
    }
    bool HasFailures() const {
        return beginFailureCount != 0 || renderPassFailureCount != 0 || commitFailureCount != 0 ||
            presentFailureCount != 0 || frameGraphPassFailureCount != 0 ||
            frameGraphResourceUseFailureCount != 0;
    }
};

struct RendererUploadQueueStats {
    bool ready = false;
    bool dedicatedQueue = false;
    RHI::QueueClass queueClass = RHI::QueueClass::Graphics;
    size_t pendingUploadCount = 0;
    size_t pendingUploadBytes = 0;
    size_t stagingCapacityBytes = 0;
    uint64_t submittedUploadCount = 0;
    uint64_t completedUploadCount = 0;
    uint64_t failedUploadCount = 0;
    uint64_t lastSubmittedUpload = 0;
    size_t retainedStatusCount = 0;
    size_t retainedStatusCapacity = 0;

    bool HasPendingUploads() const { return pendingUploadCount != 0; }
    bool HasPendingUploadBytes() const { return pendingUploadBytes != 0; }
    bool HasStagingCapacity() const { return stagingCapacityBytes != 0; }
    bool HasSubmittedUploads() const { return submittedUploadCount != 0; }
    bool HasCompletedUploads() const { return completedUploadCount != 0; }
    uint64_t FinishedUploadCount() const { return completedUploadCount + failedUploadCount; }
    bool HasFinishedUploads() const { return FinishedUploadCount() != 0; }
    bool HasLastSubmittedUpload() const { return lastSubmittedUpload != 0; }
    bool HasRetainedStatuses() const { return retainedStatusCount != 0; }
    bool HasRetainedStatusCapacity() const { return retainedStatusCapacity != 0; }
    bool HasFailures() const { return failedUploadCount != 0; }
    bool HasUploadActivity() const {
        return HasPendingUploads() || HasSubmittedUploads() || HasFinishedUploads() || HasRetainedStatuses();
    }
};

enum class RendererShaderLibrarySource : uint8_t {
    Unknown = 0,
    Metallib,
    Source,
};

inline const char* RendererShaderLibrarySourceName(RendererShaderLibrarySource source) {
    switch (source) {
        case RendererShaderLibrarySource::Metallib:
            return "metallib";
        case RendererShaderLibrarySource::Source:
            return "source";
        case RendererShaderLibrarySource::Unknown:
        default:
            return "unknown";
    }
}

inline void CopyRendererText(char* destination, size_t capacity, const char* text) {
    if (!destination || capacity == 0) {
        return;
    }

    const char* source = text ? text : "";
    size_t i = 0;
    for (; i + 1 < capacity && source[i] != '\0'; ++i) {
        destination[i] = source[i];
    }
    destination[i] = '\0';
    for (++i; i < capacity; ++i) {
        destination[i] = '\0';
    }
}

struct RendererRenderPassColorAttachmentInfo {
    bool active = false;
    uint32_t index = kRendererInvalidBindingIndex;
    RHI::Format format = RHI::Format::Unknown;
    RHI::AttachmentLoadAction loadAction = RHI::AttachmentLoadAction::Clear;
    RHI::AttachmentStoreAction storeAction = RHI::AttachmentStoreAction::Store;
    RHI::ClearColor clearColor;
    bool resolve = false;

    bool HasAttachment() const { return active; }
    bool HasFormat() const { return active && format != RHI::Format::Unknown; }
    bool Clears() const { return active && loadAction == RHI::AttachmentLoadAction::Clear; }
    bool Stores() const { return active && storeAction == RHI::AttachmentStoreAction::Store; }
    bool HasResolve() const { return active && resolve; }
    bool HasReadyAttachment() const { return HasAttachment() && HasFormat(); }
};

struct RendererRenderPassDepthStencilAttachmentInfo {
    bool active = false;
    RHI::Format format = RHI::Format::Unknown;
    RHI::AttachmentLoadAction loadAction = RHI::AttachmentLoadAction::Clear;
    RHI::AttachmentStoreAction storeAction = RHI::AttachmentStoreAction::DontCare;
    double clearDepth = 1.0;
    RHI::AttachmentLoadAction stencilLoadAction = RHI::AttachmentLoadAction::DontCare;
    RHI::AttachmentStoreAction stencilStoreAction = RHI::AttachmentStoreAction::DontCare;
    uint32_t clearStencil = 0;
    bool resolve = false;

    bool HasAttachment() const { return active; }
    bool HasFormat() const { return active && format != RHI::Format::Unknown; }
    bool HasDepth() const { return HasFormat() && RHI::FormatHasDepth(format); }
    bool HasStencil() const { return HasFormat() && RHI::FormatHasStencil(format); }
    bool ClearsDepth() const { return HasDepth() && loadAction == RHI::AttachmentLoadAction::Clear; }
    bool StoresDepth() const { return HasDepth() && storeAction == RHI::AttachmentStoreAction::Store; }
    bool ClearsStencil() const {
        return HasStencil() && stencilLoadAction == RHI::AttachmentLoadAction::Clear;
    }
    bool StoresStencil() const {
        return HasStencil() && stencilStoreAction == RHI::AttachmentStoreAction::Store;
    }
    bool HasResolve() const { return active && resolve; }
    bool HasReadyDepthStencil() const { return HasAttachment() && HasFormat(); }
};

struct RendererRenderPassStats {
    bool ready = false;
    uint32_t colorAttachmentCount = 0;
    std::array<RendererRenderPassColorAttachmentInfo, kRendererRenderPassColorAttachmentMaxCount> colorAttachments;
    bool hasDepthStencil = false;
    RendererRenderPassDepthStencilAttachmentInfo depthStencilAttachment;
    RHI::RenderPassDescriptorError descriptorError = RHI::RenderPassDescriptorError::MissingAttachment;
    uint32_t descriptorErrorAttachmentIndex = 0;
    RHI::Format descriptorErrorFormat = RHI::Format::Unknown;
    RHI::FrameGraphDescriptorValidation frameGraphValidation;
    uint32_t frameGraphTransitionCount = 0;
    char debugName[kRendererRenderPassDebugNameMaxLength] = {};

    const char* GetDebugName() const { return debugName; }

    void SetDebugName(const char* name) {
        CopyRendererText(debugName, kRendererRenderPassDebugNameMaxLength, name);
    }

    const RendererRenderPassColorAttachmentInfo* GetColorAttachment(size_t index) const {
        return index < colorAttachmentCount && index < colorAttachments.size() ? &colorAttachments[index] : nullptr;
    }

    bool HasDebugName() const { return debugName[0] != '\0'; }
    bool HasValidDescriptor() const { return descriptorError == RHI::RenderPassDescriptorError::None; }
    bool HasValidFrameGraphPassPlan() const {
        return frameGraphValidation.error == RHI::FrameGraphDescriptorError::None &&
            frameGraphTransitionCount != 0;
    }
    bool HasFrameGraphTransitions() const { return frameGraphTransitionCount != 0; }
    bool HasColorAttachments() const { return colorAttachmentCount != 0; }
    bool HasMultipleColorAttachments() const { return colorAttachmentCount > 1; }
    bool HasDepthStencilAttachment() const {
        return hasDepthStencil && depthStencilAttachment.HasAttachment();
    }
    bool HasCompleteColorAttachmentTable() const {
        if (!HasColorAttachments() || colorAttachmentCount > colorAttachments.size()) {
            return false;
        }
        for (size_t i = 0; i < colorAttachmentCount; ++i) {
            if (!colorAttachments[i].HasReadyAttachment() || colorAttachments[i].index != i) {
                return false;
            }
        }
        return true;
    }
    bool HasAnyClear() const {
        for (size_t i = 0; i < colorAttachmentCount && i < colorAttachments.size(); ++i) {
            if (colorAttachments[i].Clears()) {
                return true;
            }
        }
        return depthStencilAttachment.ClearsDepth() || depthStencilAttachment.ClearsStencil();
    }
    bool HasAnyStore() const {
        for (size_t i = 0; i < colorAttachmentCount && i < colorAttachments.size(); ++i) {
            if (colorAttachments[i].Stores()) {
                return true;
            }
        }
        return depthStencilAttachment.StoresDepth() || depthStencilAttachment.StoresStencil();
    }
    bool HasAnyResolve() const {
        for (size_t i = 0; i < colorAttachmentCount && i < colorAttachments.size(); ++i) {
            if (colorAttachments[i].HasResolve()) {
                return true;
            }
        }
        return depthStencilAttachment.HasResolve();
    }
    bool HasReadyRenderPass() const {
        if (!ready || !HasValidDescriptor() || !HasValidFrameGraphPassPlan() ||
            (!HasColorAttachments() && !HasDepthStencilAttachment())) {
            return false;
        }
        if (HasColorAttachments() && !HasCompleteColorAttachmentTable()) {
            return false;
        }
        return !hasDepthStencil || depthStencilAttachment.HasReadyDepthStencil();
    }
    std::string BuildLogSummary() const;
};

struct RendererPipelineColorAttachmentInfo {
    bool active = false;
    uint32_t index = kRendererInvalidBindingIndex;
    RHI::Format format = RHI::Format::Unknown;
    bool blendEnabled = false;
    RHI::BlendFactor sourceColorBlendFactor = RHI::BlendFactor::One;
    RHI::BlendFactor destinationColorBlendFactor = RHI::BlendFactor::Zero;
    RHI::BlendOperation colorBlendOperation = RHI::BlendOperation::Add;
    RHI::BlendFactor sourceAlphaBlendFactor = RHI::BlendFactor::One;
    RHI::BlendFactor destinationAlphaBlendFactor = RHI::BlendFactor::Zero;
    RHI::BlendOperation alphaBlendOperation = RHI::BlendOperation::Add;
    RHI::ColorWriteMaskFlags writeMask = RHI::ColorWriteMaskFlag(RHI::ColorWriteMask::All);

    bool HasAttachment() const { return active; }
    bool HasFormat() const { return active && format != RHI::Format::Unknown; }
    bool HasBlendState() const { return active; }
    bool HasBlend() const { return active && blendEnabled; }
    bool HasColorWriteMask() const { return active && writeMask != 0; }
    bool WritesAllColorChannels() const {
        return active && writeMask == RHI::ColorWriteMaskFlag(RHI::ColorWriteMask::All);
    }
};

struct RendererPipelineVertexBufferInfo {
    bool active = false;
    uint32_t index = kRendererInvalidBindingIndex;
    uint32_t stride = 0;
    RHI::VertexStepFunction stepFunction = RHI::VertexStepFunction::PerVertex;
    uint32_t stepRate = 1;

    bool HasBuffer() const { return active; }
    bool HasStride() const { return active && stride != 0; }
    bool HasStepRate() const { return active && stepRate != 0; }
    bool HasPerInstanceStep() const { return active && stepFunction == RHI::VertexStepFunction::PerInstance; }
    bool HasReadyBufferLayout() const { return HasBuffer() && HasStride() && HasStepRate(); }
};

struct RendererPipelineVertexAttributeInfo {
    bool active = false;
    uint32_t index = kRendererInvalidBindingIndex;
    uint32_t location = 0;
    uint32_t bufferIndex = kRendererInvalidBindingIndex;
    RHI::VertexFormat format = RHI::VertexFormat::Unknown;
    uint32_t offset = 0;

    bool HasAttribute() const { return active; }
    bool HasBufferReference() const { return active && bufferIndex != kRendererInvalidBindingIndex; }
    bool HasFormat() const { return active && format != RHI::VertexFormat::Unknown; }
    bool HasOffset() const { return active && offset != 0; }
    bool HasReadyAttributeLayout() const { return HasAttribute() && HasBufferReference() && HasFormat(); }
};

struct RendererPipelineStats {
    bool ready = false;
    bool shaderLibraryReady = false;
    RendererShaderLibrarySource shaderLibrarySource = RendererShaderLibrarySource::Unknown;
    uint32_t shaderManifestVersion = 0;
    RHI::ArgumentBufferTier shaderRequiredArgumentBufferTier = RHI::ArgumentBufferTier::Unsupported;
    uint32_t shaderMaterialShaderResourceGroupArgumentBufferIndex = kRendererInvalidBindingIndex;
    uint32_t shaderMaterialShaderResourceGroupUniformArgumentIndex = kRendererInvalidBindingIndex;
    uint32_t shaderMaterialShaderResourceGroupTextureArgumentBaseIndex = kRendererInvalidBindingIndex;
    uint32_t shaderMaterialShaderResourceGroupSamplerArgumentBaseIndex = kRendererInvalidBindingIndex;
    RHI::Format pipelineColorFormat = RHI::Format::Unknown;
    uint32_t colorAttachmentCount = 0;
    std::array<RendererPipelineColorAttachmentInfo, kRendererPipelineColorAttachmentMaxCount> colorAttachments;
    RHI::Format pipelineDepthStencilFormat = RHI::Format::Unknown;
    uint32_t pipelineSampleCount = 0;
    bool alphaToCoverageEnabled = false;
    bool rasterStateReady = false;
    RHI::PrimitiveTopology primitiveTopology = RHI::PrimitiveTopology::TriangleList;
    RHI::FillMode fillMode = RHI::FillMode::Solid;
    RHI::CullMode cullMode = RHI::CullMode::None;
    RHI::FrontFaceWinding frontFace = RHI::FrontFaceWinding::CounterClockwise;
    int32_t depthBias = 0;
    float depthBiasClamp = 0.0f;
    float depthBiasSlopeScale = 0.0f;
    bool depthClipEnabled = true;
    bool depthStencilStateReady = false;
    bool depthTestEnabled = true;
    bool depthWriteEnabled = true;
    RHI::CompareFunction depthCompare = RHI::CompareFunction::Less;
    bool stencilTestEnabled = false;
    uint8_t stencilReadMask = 0xff;
    uint8_t stencilWriteMask = 0xff;
    RHI::CompareFunction frontStencilCompare = RHI::CompareFunction::Always;
    RHI::StencilOperation frontStencilFailOperation = RHI::StencilOperation::Keep;
    RHI::StencilOperation frontStencilDepthFailOperation = RHI::StencilOperation::Keep;
    RHI::StencilOperation frontStencilPassOperation = RHI::StencilOperation::Keep;
    RHI::CompareFunction backStencilCompare = RHI::CompareFunction::Always;
    RHI::StencilOperation backStencilFailOperation = RHI::StencilOperation::Keep;
    RHI::StencilOperation backStencilDepthFailOperation = RHI::StencilOperation::Keep;
    RHI::StencilOperation backStencilPassOperation = RHI::StencilOperation::Keep;
    bool colorBlendStateReady = false;
    bool colorBlendEnabled = false;
    RHI::BlendFactor sourceColorBlendFactor = RHI::BlendFactor::One;
    RHI::BlendFactor destinationColorBlendFactor = RHI::BlendFactor::Zero;
    RHI::BlendOperation colorBlendOperation = RHI::BlendOperation::Add;
    RHI::BlendFactor sourceAlphaBlendFactor = RHI::BlendFactor::One;
    RHI::BlendFactor destinationAlphaBlendFactor = RHI::BlendFactor::Zero;
    RHI::BlendOperation alphaBlendOperation = RHI::BlendOperation::Add;
    RHI::ColorWriteMaskFlags colorWriteMask = RHI::ColorWriteMaskFlag(RHI::ColorWriteMask::All);
    uint32_t vertexBufferCount = 0;
    uint32_t vertexAttributeCount = 0;
    std::array<RendererPipelineVertexBufferInfo, kRendererPipelineVertexBufferMaxCount> vertexBuffers;
    std::array<RendererPipelineVertexAttributeInfo, kRendererPipelineVertexAttributeMaxCount> vertexAttributes;
    uint32_t primaryVertexBufferIndex = kRendererInvalidBindingIndex;
    uint32_t primaryVertexStride = 0;
    char pipelineDebugName[kRendererPipelineDebugNameMaxLength] = {};
    char shaderDebugName[kRendererShaderDebugNameMaxLength] = {};
    char shaderManifestPath[kRendererShaderLibraryPathMaxLength] = {};
    char shaderLibraryPath[kRendererShaderLibraryPathMaxLength] = {};
    char shaderVertexEntryPoint[kRendererShaderEntryPointMaxLength] = {};
    char shaderFragmentEntryPoint[kRendererShaderEntryPointMaxLength] = {};
    char shaderMaterialLayout[kRendererShaderMaterialLayoutMaxLength] = {};
    char shaderPipelineLayout[kRendererShaderPipelineLayoutMaxLength] = {};
    size_t cachedPipelineCount = 0;
    uint64_t requestCount = 0;
    uint64_t hitCount = 0;
    uint64_t missCount = 0;
    uint64_t failedCreateCount = 0;

    const char* GetPipelineDebugName() const { return pipelineDebugName; }
    const char* GetShaderDebugName() const { return shaderDebugName; }
    const char* GetShaderManifestPath() const { return shaderManifestPath; }
    const char* GetShaderLibraryPath() const { return shaderLibraryPath; }
    const char* GetShaderVertexEntryPoint() const { return shaderVertexEntryPoint; }
    const char* GetShaderFragmentEntryPoint() const { return shaderFragmentEntryPoint; }
    const char* GetShaderMaterialLayout() const { return shaderMaterialLayout; }
    const char* GetShaderPipelineLayout() const { return shaderPipelineLayout; }

    void SetPipelineDebugName(const char* debugName) {
        CopyRendererText(pipelineDebugName, kRendererPipelineDebugNameMaxLength, debugName);
    }

    void SetShaderDebugName(const char* debugName) {
        CopyRendererText(shaderDebugName, kRendererShaderDebugNameMaxLength, debugName);
    }

    void SetShaderManifestPath(const char* path) {
        CopyRendererText(shaderManifestPath, kRendererShaderLibraryPathMaxLength, path);
    }

    void SetShaderLibraryPath(const char* path) {
        CopyRendererText(shaderLibraryPath, kRendererShaderLibraryPathMaxLength, path);
    }

    void SetShaderEntryPoints(const char* vertexEntryPoint, const char* fragmentEntryPoint) {
        CopyRendererText(shaderVertexEntryPoint, kRendererShaderEntryPointMaxLength, vertexEntryPoint);
        CopyRendererText(shaderFragmentEntryPoint, kRendererShaderEntryPointMaxLength, fragmentEntryPoint);
    }

    void SetShaderMaterialLayout(const char* materialLayout) {
        CopyRendererText(shaderMaterialLayout, kRendererShaderMaterialLayoutMaxLength, materialLayout);
    }

    void SetShaderPipelineLayout(const char* pipelineLayout) {
        CopyRendererText(shaderPipelineLayout, kRendererShaderPipelineLayoutMaxLength, pipelineLayout);
    }

    bool HasCachedPipelines() const { return cachedPipelineCount != 0; }
    bool HasPipelineRequests() const { return requestCount != 0; }
    bool HasCacheHits() const { return hitCount != 0; }
    bool HasCacheMisses() const { return missCount != 0; }
    bool HasCreateFailures() const { return failedCreateCount != 0; }
    bool HasPipelineCacheActivity() const {
        return HasCachedPipelines() || HasPipelineRequests() || HasCacheHits() ||
            HasCacheMisses() || HasCreateFailures();
    }
    bool HasPipelineColorFormat() const { return pipelineColorFormat != RHI::Format::Unknown; }
    const RendererPipelineColorAttachmentInfo* GetColorAttachment(size_t index) const {
        return index < colorAttachmentCount && index < colorAttachments.size() ? &colorAttachments[index] : nullptr;
    }
    bool HasColorAttachments() const { return colorAttachmentCount != 0; }
    bool HasMultipleColorAttachments() const { return colorAttachmentCount > 1; }
    bool HasCompleteColorAttachmentTable() const {
        if (!HasColorAttachments() || colorAttachmentCount > colorAttachments.size()) {
            return false;
        }
        for (size_t i = 0; i < colorAttachmentCount; ++i) {
            if (!colorAttachments[i].HasAttachment() || colorAttachments[i].index != i) {
                return false;
            }
        }
        return true;
    }
    bool HasPipelineDepthStencilFormat() const { return pipelineDepthStencilFormat != RHI::Format::Unknown; }
    bool HasPipelineSampleCount() const { return pipelineSampleCount != 0; }
    bool HasAlphaToCoverage() const { return alphaToCoverageEnabled; }
    bool HasRasterState() const { return rasterStateReady; }
    bool HasTriangleTopology() const {
        return rasterStateReady &&
            (primitiveTopology == RHI::PrimitiveTopology::TriangleList ||
             primitiveTopology == RHI::PrimitiveTopology::TriangleStrip);
    }
    bool HasDepthBias() const {
        return depthBias != 0 || depthBiasClamp != 0.0f || depthBiasSlopeScale != 0.0f;
    }
    bool HasDepthClipState() const { return rasterStateReady; }
    bool HasDepthStencilState() const { return depthStencilStateReady; }
    bool HasDepthTesting() const { return depthStencilStateReady && depthTestEnabled; }
    bool HasDepthWriting() const { return depthStencilStateReady && depthWriteEnabled; }
    bool HasStencilTesting() const { return depthStencilStateReady && stencilTestEnabled; }
    bool HasStencilMasks() const {
        return depthStencilStateReady && (stencilReadMask != 0 || stencilWriteMask != 0);
    }
    bool HasStencilFaceState() const { return depthStencilStateReady; }
    bool HasActiveStencilFaceState() const { return depthStencilStateReady && stencilTestEnabled; }
    bool HasNonDefaultStencilFaceState() const {
        return depthStencilStateReady &&
            (frontStencilCompare != RHI::CompareFunction::Always ||
             frontStencilFailOperation != RHI::StencilOperation::Keep ||
             frontStencilDepthFailOperation != RHI::StencilOperation::Keep ||
             frontStencilPassOperation != RHI::StencilOperation::Keep ||
             backStencilCompare != RHI::CompareFunction::Always ||
             backStencilFailOperation != RHI::StencilOperation::Keep ||
             backStencilDepthFailOperation != RHI::StencilOperation::Keep ||
             backStencilPassOperation != RHI::StencilOperation::Keep);
    }
    bool HasColorBlendState() const { return colorBlendStateReady; }
    bool HasColorBlend() const { return colorBlendStateReady && colorBlendEnabled; }
    bool HasColorWriteMask() const { return colorBlendStateReady && colorWriteMask != 0; }
    bool WritesAllColorChannels() const {
        return colorBlendStateReady && colorWriteMask == RHI::ColorWriteMaskFlag(RHI::ColorWriteMask::All);
    }
    bool HasVertexBuffers() const { return vertexBufferCount != 0; }
    bool HasVertexAttributes() const { return vertexAttributeCount != 0; }
    bool HasPrimaryVertexBuffer() const { return primaryVertexBufferIndex != kRendererInvalidBindingIndex; }
    bool HasPrimaryVertexStride() const { return primaryVertexStride != 0; }
    const RendererPipelineVertexBufferInfo* GetVertexBuffer(size_t index) const {
        return index < vertexBufferCount && index < vertexBuffers.size() ? &vertexBuffers[index] : nullptr;
    }
    const RendererPipelineVertexAttributeInfo* GetVertexAttribute(size_t index) const {
        return index < vertexAttributeCount && index < vertexAttributes.size() ? &vertexAttributes[index] : nullptr;
    }
    bool HasMultipleVertexBuffers() const { return vertexBufferCount > 1; }
    bool HasMultipleVertexAttributes() const { return vertexAttributeCount > 1; }
    bool HasCompleteVertexBufferTable() const {
        if (!HasVertexBuffers() || vertexBufferCount > vertexBuffers.size()) {
            return false;
        }
        for (size_t i = 0; i < vertexBufferCount; ++i) {
            if (!vertexBuffers[i].HasReadyBufferLayout() || vertexBuffers[i].index != i) {
                return false;
            }
        }
        return true;
    }
    bool HasCompleteVertexAttributeTable() const {
        if (!HasVertexAttributes() || vertexAttributeCount > vertexAttributes.size()) {
            return false;
        }
        for (size_t i = 0; i < vertexAttributeCount; ++i) {
            if (!vertexAttributes[i].HasReadyAttributeLayout() || vertexAttributes[i].index != i) {
                return false;
            }
        }
        return true;
    }
    bool HasVertexInputLayout() const {
        return HasVertexBuffers() && HasVertexAttributes() && HasPrimaryVertexBuffer() &&
            HasPrimaryVertexStride();
    }
    bool HasDetailedVertexInputLayout() const {
        return HasVertexInputLayout() && HasCompleteVertexBufferTable() && HasCompleteVertexAttributeTable();
    }
    bool HasPipelineDebugName() const { return pipelineDebugName[0] != '\0'; }
    bool HasShaderManifestVersion() const { return shaderManifestVersion != 0; }
    bool HasShaderRequiredArgumentBufferTier() const {
        return shaderRequiredArgumentBufferTier != RHI::ArgumentBufferTier::Unsupported;
    }
    bool HasShaderMaterialShaderResourceGroupArgumentBufferIndex() const {
        return shaderMaterialShaderResourceGroupArgumentBufferIndex != kRendererInvalidBindingIndex;
    }
    bool HasShaderMaterialShaderResourceGroupArgumentLayout() const {
        return shaderMaterialShaderResourceGroupUniformArgumentIndex != kRendererInvalidBindingIndex &&
            shaderMaterialShaderResourceGroupTextureArgumentBaseIndex != kRendererInvalidBindingIndex &&
            shaderMaterialShaderResourceGroupSamplerArgumentBaseIndex != kRendererInvalidBindingIndex;
    }
    bool HasShaderMaterialLayout() const { return shaderMaterialLayout[0] != '\0'; }
    bool HasShaderPipelineLayout() const { return shaderPipelineLayout[0] != '\0'; }
    bool HasShaderEntryPoints() const {
        return shaderVertexEntryPoint[0] != '\0' && shaderFragmentEntryPoint[0] != '\0';
    }
    bool HasShaderDebugName() const { return shaderDebugName[0] != '\0'; }
    bool HasShaderManifest() const { return shaderManifestPath[0] != '\0'; }
    bool HasShaderLibraryPath() const { return shaderLibraryPath[0] != '\0'; }
    bool HasCompleteShaderDescriptor() const {
        return HasShaderManifestVersion() && HasShaderRequiredArgumentBufferTier() &&
            HasShaderMaterialShaderResourceGroupArgumentBufferIndex() &&
            HasShaderMaterialShaderResourceGroupArgumentLayout() && HasShaderMaterialLayout() &&
            HasShaderPipelineLayout() && HasShaderManifest() && HasShaderLibraryPath() &&
            HasShaderEntryPoints();
    }
    bool HasShaderLibrary() const {
        return shaderLibraryReady && shaderLibrarySource != RendererShaderLibrarySource::Unknown;
    }
};

struct RendererGeometryStats {
    bool ready = false;
    bool vertexBufferReady = false;
    bool indexBufferReady = false;
    uint32_t vertexBufferIndex = kRendererGeometryVertexBufferIndex;
    uint32_t vertexStride = 0;
    uint64_t vertexBufferBytes = 0;
    uint64_t indexBufferBytes = 0;
    RHI::IndexFormat indexFormat = RHI::IndexFormat::Unknown;
    uint64_t indexBufferByteOffset = 0;
    uint64_t resolvedIndexBufferByteOffset = 0;
    uint32_t indexCount = 0;
    uint32_t instanceCount = 0;
    uint32_t indexOffset = 0;
    int32_t vertexOffset = 0;
    uint32_t instanceOffset = 0;
    uint8_t stencilReference = 0;
    std::array<float, 4> blendConstant{0.0f, 0.0f, 0.0f, 0.0f};

    bool HasVertexBuffer() const { return vertexBufferReady && vertexBufferBytes != 0; }
    bool HasIndexBuffer() const { return indexBufferReady && indexBufferBytes != 0; }
    bool HasVertexStride() const { return vertexStride != 0; }
    bool HasIndexFormat() const { return indexFormat != RHI::IndexFormat::Unknown; }
    bool HasIndexCount() const { return indexCount != 0; }
    bool HasInstances() const { return instanceCount != 0; }
    bool HasResolvedIndexBufferOffset() const {
        return HasIndexCount() || resolvedIndexBufferByteOffset != 0;
    }
    bool HasBlendConstant() const {
        for (float value : blendConstant) {
            if (value != 0.0f) {
                return true;
            }
        }
        return false;
    }
    bool HasIndexedDraw() const {
        return HasIndexFormat() && HasIndexCount() && HasInstances();
    }
    bool HasReadyGeometry() const {
        return ready && HasVertexBuffer() && HasIndexBuffer() && HasVertexStride() && HasIndexedDraw();
    }
};

struct RendererDrawStateStats {
    bool ready = false;
    RHI::Extent2D drawableSize;
    RHI::ViewportDesc viewport;
    RHI::ScissorRectDesc scissor;
    RHI::ViewportDescriptorError viewportError = RHI::ViewportDescriptorError::EmptyViewport;
    RHI::ScissorDescriptorError scissorError = RHI::ScissorDescriptorError::EmptyScissor;

    bool HasDrawableSize() const { return drawableSize.width != 0 && drawableSize.height != 0; }
    bool HasViewport() const { return viewport.maxX > viewport.minX && viewport.maxY > viewport.minY; }
    bool HasScissor() const { return scissor.maxX > scissor.minX && scissor.maxY > scissor.minY; }
    bool HasViewportDepthRange() const {
        return viewport.minZ >= 0.0 && viewport.maxZ <= 1.0 && viewport.maxZ >= viewport.minZ;
    }
    bool HasValidViewport() const { return viewportError == RHI::ViewportDescriptorError::None; }
    bool HasValidScissor() const { return scissorError == RHI::ScissorDescriptorError::None; }
    bool HasValidDrawState() const { return HasValidViewport() && HasValidScissor(); }
    bool HasReadyDrawState() const { return ready && HasValidDrawState(); }
    bool ViewportMatchesDrawable() const {
        return HasDrawableSize() && viewport.minX == 0.0 && viewport.minY == 0.0 &&
            viewport.maxX == static_cast<double>(drawableSize.width) &&
            viewport.maxY == static_cast<double>(drawableSize.height) &&
            viewport.minZ == 0.0 && viewport.maxZ == 1.0;
    }
    bool ScissorMatchesDrawable() const {
        return HasDrawableSize() && scissor.minX == 0 && scissor.minY == 0 &&
            scissor.maxX == drawableSize.width && scissor.maxY == drawableSize.height;
    }
    bool HasFullDrawableDrawState() const {
        return HasReadyDrawState() && ViewportMatchesDrawable() && ScissorMatchesDrawable();
    }
};

struct RendererDrawSubmissionStats {
    bool ready = false;
    bool pipelineReady = false;
    bool geometryReady = false;
    bool drawStateReady = false;
    uint32_t lastFrameDrawCount = 0;
    uint32_t lastFrameIndexedDrawCount = 0;
    uint32_t lastFrameBaseDrawCount = 0;
    uint32_t lastFrameDebugDrawCount = 0;
    uint32_t materialShaderResourceGroupArgumentBufferBindingIndex = kRendererInvalidBindingIndex;
    uint32_t lastFrameMaterialShaderResourceGroupArgumentBufferBindCount = 0;
    uint64_t lastFrameIndexCount = 0;
    uint64_t lastFrameInstanceCount = 0;
    uint64_t submittedFrameCount = 0;
    uint64_t submittedDrawCount = 0;
    uint64_t submittedIndexedDrawCount = 0;
    uint64_t submittedMaterialShaderResourceGroupArgumentBufferBindCount = 0;
    uint64_t submittedIndexCount = 0;
    uint64_t submittedInstanceCount = 0;

    bool HasLastFrameDraws() const { return lastFrameDrawCount != 0; }
    bool HasLastFrameIndexedDraws() const { return lastFrameIndexedDrawCount != 0; }
    bool HasLastFrameBaseDraws() const { return lastFrameBaseDrawCount != 0; }
    bool HasLastFrameDebugDraws() const { return lastFrameDebugDrawCount != 0; }
    bool HasMaterialShaderResourceGroupArgumentBufferBindingIndex() const {
        return materialShaderResourceGroupArgumentBufferBindingIndex != kRendererInvalidBindingIndex;
    }
    bool HasLastFrameMaterialShaderResourceGroupArgumentBufferBindings() const {
        return lastFrameMaterialShaderResourceGroupArgumentBufferBindCount != 0;
    }
    bool HasLastFrameIndices() const { return lastFrameIndexCount != 0; }
    bool HasLastFrameInstances() const { return lastFrameInstanceCount != 0; }
    bool HasSubmittedFrames() const { return submittedFrameCount != 0; }
    bool HasSubmittedDraws() const { return submittedDrawCount != 0; }
    bool HasSubmittedIndexedDraws() const { return submittedIndexedDrawCount != 0; }
    bool HasSubmittedMaterialShaderResourceGroupArgumentBufferBindings() const {
        return submittedMaterialShaderResourceGroupArgumentBufferBindCount != 0;
    }
    bool HasSubmittedIndices() const { return submittedIndexCount != 0; }
    bool HasSubmittedInstances() const { return submittedInstanceCount != 0; }
    bool HasRequiredDrawState() const { return pipelineReady && geometryReady && drawStateReady; }
    bool HasCompleteMaterialShaderResourceGroupArgumentBufferBinding() const {
        return HasMaterialShaderResourceGroupArgumentBufferBindingIndex() && HasLastFrameDraws() &&
            lastFrameMaterialShaderResourceGroupArgumentBufferBindCount >= lastFrameDrawCount;
    }
    bool HasReadySubmission() const {
        return ready && HasRequiredDrawState() && HasLastFrameDraws() && HasLastFrameIndexedDraws();
    }
    bool HasSubmissionActivity() const {
        return HasSubmittedFrames() || HasSubmittedDraws() || HasSubmittedIndexedDraws() ||
            HasSubmittedMaterialShaderResourceGroupArgumentBufferBindings() || HasSubmittedIndices() ||
            HasSubmittedInstances();
    }
};

struct RendererSwapchainStats {
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
    bool HasColorFormat() const { return colorFormat != RHI::Format::Unknown; }
    bool HasDepthFormat() const { return depthFormat != RHI::Format::Unknown; }
    bool HasResizeActivity() const { return resizeCount != 0; }
    bool HasAcquireAttempts() const { return acquireAttemptCount != 0; }
    bool HasAcquiredFrames() const { return acquiredFrameCount != 0; }
    bool HasPresentedFrames() const { return presentedFrameCount != 0; }
    bool HasReleasedFrames() const { return releasedFrameCount != 0; }
    bool HasPresentationActivity() const { return HasPresentedFrames() || HasReleasedFrames(); }
    bool HasSwapchainActivity() const {
        return HasResizeActivity() || HasAcquireAttempts() || HasPresentationActivity() || HasFailures();
    }
    bool HasDepthCreateFailures() const { return depthCreateFailureCount != 0; }
    bool HasAcquireFailures() const { return acquireFailureCount != 0; }
    bool HasFailures() const { return HasDepthCreateFailures() || HasAcquireFailures(); }
};

inline bool RendererPipelineFormatsMatchSwapchain(const RendererPipelineStats& pipeline,
                                                  const RendererSwapchainStats& swapchain) {
    return pipeline.HasPipelineColorFormat() &&
           swapchain.HasColorFormat() &&
           pipeline.pipelineColorFormat == swapchain.colorFormat &&
           pipeline.pipelineDepthStencilFormat == swapchain.depthFormat;
}

enum class RendererTextureSlot : uint8_t {
    BaseColor = 0,
    Normal,
    MetallicRoughness,
    Emissive,
    Occlusion,
};

static constexpr size_t kRendererTextureSlotCount = 5;
static constexpr uint32_t kRendererMaterialUniformBufferIndex = 1;
static constexpr uint32_t kRendererMaterialShaderResourceGroupArgumentBufferIndex = 2;
static constexpr uint32_t kRendererMaterialTextureBindingBaseIndex = 0;
static constexpr uint32_t kRendererMaterialSamplerBindingBaseIndex = 0;
static constexpr uint32_t kRendererMaterialShaderResourceGroupBindingCount = 3;

inline size_t RendererTextureSlotIndex(RendererTextureSlot slot) {
    switch (slot) {
        case RendererTextureSlot::BaseColor:
            return 0;
        case RendererTextureSlot::Normal:
            return 1;
        case RendererTextureSlot::MetallicRoughness:
            return 2;
        case RendererTextureSlot::Emissive:
            return 3;
        case RendererTextureSlot::Occlusion:
            return 4;
        default:
            return kRendererTextureSlotCount;
    }
}

inline uint32_t RendererMaterialTextureBindingIndex(RendererTextureSlot slot) {
    const size_t slotIndex = RendererTextureSlotIndex(slot);
    if (slotIndex >= kRendererTextureSlotCount) {
        return kRendererInvalidBindingIndex;
    }

    return kRendererMaterialTextureBindingBaseIndex + static_cast<uint32_t>(slotIndex);
}

inline uint32_t RendererMaterialSamplerBindingIndex(RendererTextureSlot slot) {
    const size_t slotIndex = RendererTextureSlotIndex(slot);
    if (slotIndex >= kRendererTextureSlotCount) {
        return kRendererInvalidBindingIndex;
    }

    return kRendererMaterialSamplerBindingBaseIndex + static_cast<uint32_t>(slotIndex);
}

struct RendererMaterialBindingLayoutInfo {
    uint32_t vertexBufferIndex = kRendererGeometryVertexBufferIndex;
    uint32_t uniformBufferIndex = kRendererMaterialUniformBufferIndex;
    uint32_t argumentBufferIndex = kRendererMaterialShaderResourceGroupArgumentBufferIndex;
    uint32_t textureBindingBaseIndex = kRendererMaterialTextureBindingBaseIndex;
    uint32_t samplerBindingBaseIndex = kRendererMaterialSamplerBindingBaseIndex;
    uint32_t textureBindingCount = static_cast<uint32_t>(kRendererTextureSlotCount);
    uint32_t samplerBindingCount = static_cast<uint32_t>(kRendererTextureSlotCount);

    bool HasVertexBufferIndex() const { return vertexBufferIndex != kRendererInvalidBindingIndex; }
    bool HasUniformBufferIndex() const { return uniformBufferIndex != kRendererInvalidBindingIndex; }
    bool HasArgumentBufferIndex() const { return argumentBufferIndex != kRendererInvalidBindingIndex; }
    bool HasTextureBindingRange() const {
        return textureBindingBaseIndex != kRendererInvalidBindingIndex && textureBindingCount != 0;
    }
    bool HasSamplerBindingRange() const {
        return samplerBindingBaseIndex != kRendererInvalidBindingIndex && samplerBindingCount != 0;
    }
    bool HasCompleteMaterialBindingRange() const {
        return textureBindingCount == kRendererTextureSlotCount &&
            samplerBindingCount == kRendererTextureSlotCount;
    }
    bool HasFixedMaterialBindingLayout() const {
        return HasVertexBufferIndex() && HasUniformBufferIndex() && HasArgumentBufferIndex() &&
            HasTextureBindingRange() &&
            HasSamplerBindingRange() && HasCompleteMaterialBindingRange();
    }

    RHI::ShaderResourceGroupLayoutDesc ToShaderResourceGroupLayoutDesc(const char* debugName = nullptr) const {
        RHI::ShaderResourceGroupLayoutDesc desc;
        desc.debugName = debugName;
        desc.bindingCount = kRendererMaterialShaderResourceGroupBindingCount;
        desc.bindings[0].type = RHI::ShaderResourceBindingType::ConstantBuffer;
        desc.bindings[0].shaderStages = RHI::ShaderStage::Vertex | RHI::ShaderStage::Fragment;
        desc.bindings[0].bindingIndex = uniformBufferIndex;
        desc.bindings[0].bindingCount = 1;
        desc.bindings[0].debugName = "material_uniforms";
        desc.bindings[1].type = RHI::ShaderResourceBindingType::Texture;
        desc.bindings[1].shaderStages = RHI::ShaderStageFlag(RHI::ShaderStage::Fragment);
        desc.bindings[1].bindingIndex = textureBindingBaseIndex;
        desc.bindings[1].bindingCount = textureBindingCount;
        desc.bindings[1].debugName = "material_textures";
        desc.bindings[2].type = RHI::ShaderResourceBindingType::Sampler;
        desc.bindings[2].shaderStages = RHI::ShaderStageFlag(RHI::ShaderStage::Fragment);
        desc.bindings[2].bindingIndex = samplerBindingBaseIndex;
        desc.bindings[2].bindingCount = samplerBindingCount;
        desc.bindings[2].debugName = "material_samplers";
        return desc;
    }

    RHI::ShaderResourceGroupLayoutValidation ValidateShaderResourceGroupLayout() const {
        return RHI::ValidateShaderResourceGroupLayoutDesc(ToShaderResourceGroupLayoutDesc("NEXT material layout"));
    }

    bool HasValidShaderResourceGroupLayout() const {
        return static_cast<bool>(ValidateShaderResourceGroupLayout());
    }

    uint32_t GetTextureBindingIndex(RendererTextureSlot slot) const {
        const size_t slotIndex = RendererTextureSlotIndex(slot);
        if (slotIndex >= kRendererTextureSlotCount || slotIndex >= textureBindingCount ||
            textureBindingBaseIndex == kRendererInvalidBindingIndex ||
            textureBindingBaseIndex > kRendererInvalidBindingIndex - static_cast<uint32_t>(slotIndex)) {
            return kRendererInvalidBindingIndex;
        }

        return textureBindingBaseIndex + static_cast<uint32_t>(slotIndex);
    }

    uint32_t GetSamplerBindingIndex(RendererTextureSlot slot) const {
        const size_t slotIndex = RendererTextureSlotIndex(slot);
        if (slotIndex >= kRendererTextureSlotCount || slotIndex >= samplerBindingCount ||
            samplerBindingBaseIndex == kRendererInvalidBindingIndex ||
            samplerBindingBaseIndex > kRendererInvalidBindingIndex - static_cast<uint32_t>(slotIndex)) {
            return kRendererInvalidBindingIndex;
        }

        return samplerBindingBaseIndex + static_cast<uint32_t>(slotIndex);
    }
};

struct RendererShaderResourceBindingInfo {
    bool active = false;
    uint32_t index = kRendererInvalidBindingIndex;
    RHI::ShaderResourceBindingType type = RHI::ShaderResourceBindingType::Texture;
    RHI::ShaderStageFlags shaderStages = 0;
    uint32_t bindingIndex = RHI::kInvalidShaderResourceBindingIndex;
    uint32_t bindingCount = 0;
    uint32_t boundResourceCount = 0;
    char debugName[kRendererShaderResourceBindingDebugNameMaxLength] = {};

    const char* GetDebugName() const { return debugName; }

    void SetDebugName(const char* name) {
        CopyRendererText(debugName, kRendererShaderResourceBindingDebugNameMaxLength, name);
    }

    bool HasDebugName() const { return debugName[0] != '\0'; }
    bool HasShaderStages() const { return shaderStages != 0; }
    bool HasVertexStage() const { return RHI::HasShaderStage(shaderStages, RHI::ShaderStage::Vertex); }
    bool HasFragmentStage() const { return RHI::HasShaderStage(shaderStages, RHI::ShaderStage::Fragment); }
    bool HasComputeStage() const { return RHI::HasShaderStage(shaderStages, RHI::ShaderStage::Compute); }
    bool HasBindingRange() const {
        return bindingIndex != RHI::kInvalidShaderResourceBindingIndex && bindingCount != 0 &&
            bindingIndex <= RHI::kMaxShaderResourceBindingIndex &&
            bindingCount - 1 <= RHI::kMaxShaderResourceBindingIndex - bindingIndex;
    }
    uint32_t LastBindingIndex() const {
        return HasBindingRange() ? bindingIndex + bindingCount - 1 : RHI::kInvalidShaderResourceBindingIndex;
    }
    bool ContainsBindingIndex(uint32_t targetIndex) const {
        return HasBindingRange() && targetIndex >= bindingIndex && targetIndex <= LastBindingIndex();
    }
    bool IsConstantBuffer() const { return type == RHI::ShaderResourceBindingType::ConstantBuffer; }
    bool IsTexture() const { return type == RHI::ShaderResourceBindingType::Texture; }
    bool IsSampler() const { return type == RHI::ShaderResourceBindingType::Sampler; }
    bool HasReadyBinding() const { return active && HasShaderStages() && HasBindingRange(); }
    bool HasBoundResources() const { return boundResourceCount != 0; }
    bool HasCompleteBoundResources() const {
        return HasReadyBinding() && boundResourceCount >= bindingCount;
    }
    bool HasMissingBoundResources() const {
        return HasReadyBinding() && boundResourceCount < bindingCount;
    }
};

inline const char* RendererTextureSlotName(RendererTextureSlot slot) {
    switch (slot) {
        case RendererTextureSlot::BaseColor:
            return "baseColor";
        case RendererTextureSlot::Normal:
            return "normal";
        case RendererTextureSlot::MetallicRoughness:
            return "metallicRoughness";
        case RendererTextureSlot::Emissive:
            return "emissive";
        case RendererTextureSlot::Occlusion:
            return "occlusion";
        default:
            return "unknown";
    }
}

enum class RendererResourceStateKind : uint8_t {
    Unknown = 0,
    VertexBuffer,
    IndexBuffer,
    UniformBuffer,
    MaterialTexture,
};

inline const char* RendererResourceStateKindName(RendererResourceStateKind kind) {
    switch (kind) {
        case RendererResourceStateKind::VertexBuffer:
            return "vertexBuffer";
        case RendererResourceStateKind::IndexBuffer:
            return "indexBuffer";
        case RendererResourceStateKind::UniformBuffer:
            return "uniformBuffer";
        case RendererResourceStateKind::MaterialTexture:
            return "materialTexture";
        case RendererResourceStateKind::Unknown:
        default:
            return "unknown";
    }
}

static constexpr size_t kRendererResourceStateMaxCount = 3 + kRendererTextureSlotCount;

struct RendererResourceStateInfo {
    bool active = false;
    uint32_t index = kRendererInvalidBindingIndex;
    RendererResourceStateKind kind = RendererResourceStateKind::Unknown;
    RHI::ResourceType resourceType = RHI::ResourceType::Unknown;
    RHI::ResourceUsageFlags usage = 0;
    RHI::ResourceState currentState = RHI::ResourceState::Undefined;
    RHI::ResourceState expectedState = RHI::ResourceState::Undefined;
    RendererTextureSlot textureSlot = RendererTextureSlot::BaseColor;
    uint32_t bindingIndex = kRendererInvalidBindingIndex;
    char debugName[kRendererResourceStateDebugNameMaxLength] = {};

    const char* GetDebugName() const { return debugName; }
    void SetDebugName(const char* name) {
        CopyRendererText(debugName, kRendererResourceStateDebugNameMaxLength, name);
    }

    bool HasResource() const { return active; }
    bool HasDebugName() const { return debugName[0] != '\0'; }
    bool IsBuffer() const { return active && resourceType == RHI::ResourceType::Buffer; }
    bool IsTexture() const { return active && resourceType == RHI::ResourceType::Texture; }
    bool IsMaterialTexture() const { return active && kind == RendererResourceStateKind::MaterialTexture; }
    bool HasUsage() const { return active && usage != 0; }
    bool HasBindingIndex() const { return active && bindingIndex != kRendererInvalidBindingIndex; }
    bool HasExpectedState() const { return active && expectedState != RHI::ResourceState::Undefined; }
    bool MatchesExpectedState() const { return HasExpectedState() && currentState == expectedState; }
    bool IsShaderReadable() const { return active && currentState == RHI::ResourceState::ShaderRead; }
    bool IsCopyDestination() const { return active && currentState == RHI::ResourceState::CopyDestination; }
    bool HasReadyResourceState() const { return HasResource() && HasUsage() && MatchesExpectedState(); }
};

struct RendererResourceStateStats {
    bool ready = false;
    RHI::FrameGraphDescriptorValidation frameGraphValidation;
    uint32_t frameGraphTransitionCount = 0;
    uint32_t frameGraphPassCount = 0;
    uint32_t frameGraphReadyPassIndex = RHI::kInvalidFrameGraphPassIndex;
    uint32_t frameGraphReadyPassTransitionCount = 0;
    uint32_t resourceCount = 0;
    uint32_t bufferResourceCount = 0;
    uint32_t textureResourceCount = 0;
    uint32_t expectedStateMatchCount = 0;
    std::array<RendererResourceStateInfo, kRendererResourceStateMaxCount> resources;

    const RendererResourceStateInfo* GetResource(size_t index) const {
        return index < resourceCount && index < resources.size() ? &resources[index] : nullptr;
    }

    const RendererResourceStateInfo* GetMaterialTexture(RendererTextureSlot slot) const {
        for (size_t i = 0; i < resourceCount && i < resources.size(); ++i) {
            if (resources[i].IsMaterialTexture() && resources[i].textureSlot == slot) {
                return &resources[i];
            }
        }
        return nullptr;
    }

    bool HasResources() const { return resourceCount != 0; }
    bool HasBuffers() const { return bufferResourceCount != 0; }
    bool HasTextures() const { return textureResourceCount != 0; }
    bool HasExpectedStateMatches() const { return expectedStateMatchCount != 0; }
    bool HasValidFrameGraphResourcePlan() const {
        return frameGraphValidation.error == RHI::FrameGraphDescriptorError::None &&
            frameGraphTransitionCount != 0 &&
            HasFrameGraphReadyPass();
    }
    bool HasFrameGraphTransitions() const { return frameGraphTransitionCount != 0; }
    bool HasFrameGraphPasses() const { return frameGraphPassCount != 0; }
    bool HasFrameGraphReadyPass() const {
        return frameGraphReadyPassIndex != RHI::kInvalidFrameGraphPassIndex &&
            frameGraphReadyPassTransitionCount != 0;
    }
    bool HasStateMismatches() const { return resourceCount != expectedStateMatchCount; }
    bool HasCompleteResourceStateTable() const {
        if (!HasResources() || resourceCount > resources.size()) {
            return false;
        }
        for (size_t i = 0; i < resourceCount; ++i) {
            if (!resources[i].HasResource() || resources[i].index != i) {
                return false;
            }
        }
        return true;
    }
    bool HasAllExpectedStates() const {
        return HasCompleteResourceStateTable() && resourceCount == expectedStateMatchCount;
    }
    bool HasReadyResourceStates() const {
        return ready && HasValidFrameGraphResourcePlan() && HasAllExpectedStates() && HasBuffers() && HasTextures();
    }
};

enum class RendererTextureFormat : uint8_t {
    Unknown = 0,
    RGBA8Unorm,
};

inline const char* RendererTextureFormatName(RendererTextureFormat format) {
    switch (format) {
        case RendererTextureFormat::RGBA8Unorm:
            return "rgba8unorm";
        case RendererTextureFormat::Unknown:
        default:
            return "unknown";
    }
}

inline uint32_t RendererTextureFormatBytesPerPixel(RendererTextureFormat format) {
    switch (format) {
        case RendererTextureFormat::RGBA8Unorm:
            return 4;
        case RendererTextureFormat::Unknown:
        default:
            return 0;
    }
}

struct RendererSamplerStats {
    bool ready = false;
    size_t cachedSamplerCount = 0;
    uint32_t materialSamplerSlotCount = 0;
    uint32_t boundMaterialSamplerCount = 0;

    bool HasCachedSamplers() const { return cachedSamplerCount != 0; }
    bool HasMaterialSamplerSlots() const { return materialSamplerSlotCount != 0; }
    bool HasBoundMaterialSamplers() const { return boundMaterialSamplerCount != 0; }
    bool HasCompleteMaterialSamplerTable() const {
        return HasMaterialSamplerSlots() && boundMaterialSamplerCount == materialSamplerSlotCount;
    }
    bool HasSamplerActivity() const {
        return ready || HasCachedSamplers() || HasBoundMaterialSamplers();
    }
};

inline bool RendererTextureUploadRequiredBytes(RendererTextureFormat format,
                                               uint32_t width,
                                               uint32_t height,
                                               size_t& outBytes) {
    outBytes = 0;
    const uint32_t bytesPerPixel = RendererTextureFormatBytesPerPixel(format);
    if (bytesPerPixel == 0 || width == 0 || height == 0) {
        return false;
    }

    const size_t maxSize = std::numeric_limits<size_t>::max();
    if (width > maxSize / bytesPerPixel) {
        return false;
    }

    const size_t rowBytes = static_cast<size_t>(width) * bytesPerPixel;
    if (height > maxSize / rowBytes) {
        return false;
    }

    outBytes = rowBytes * static_cast<size_t>(height);
    return true;
}

enum class RendererTextureUploadValidationError : uint8_t {
    None = 0,
    InvalidSlot,
    MissingPixels,
    InvalidDimensions,
    UnsupportedFormat,
    ByteSizeOverflow,
    InsufficientPixels,
};

inline const char* RendererTextureUploadValidationErrorName(RendererTextureUploadValidationError error) {
    switch (error) {
        case RendererTextureUploadValidationError::None:
            return "none";
        case RendererTextureUploadValidationError::InvalidSlot:
            return "invalid_slot";
        case RendererTextureUploadValidationError::MissingPixels:
            return "missing_pixels";
        case RendererTextureUploadValidationError::InvalidDimensions:
            return "invalid_dimensions";
        case RendererTextureUploadValidationError::UnsupportedFormat:
            return "unsupported_format";
        case RendererTextureUploadValidationError::ByteSizeOverflow:
            return "byte_size_overflow";
        case RendererTextureUploadValidationError::InsufficientPixels:
            return "insufficient_pixels";
        default:
            return "unknown";
    }
}

enum class RendererTextureUploadStatus : uint8_t {
    Unknown = 0,
    Pending,
    Completed,
    Failed,
};

inline const char* RendererTextureUploadStatusName(RendererTextureUploadStatus status) {
    switch (status) {
        case RendererTextureUploadStatus::Pending:
            return "pending";
        case RendererTextureUploadStatus::Completed:
            return "completed";
        case RendererTextureUploadStatus::Failed:
            return "failed";
        case RendererTextureUploadStatus::Unknown:
        default:
            return "unknown";
    }
}

static constexpr size_t kMaxRendererDebugCells = 256;
static constexpr size_t kRendererDrawItemStatsMaxCount = kMaxRendererDebugCells + 1;
static constexpr uint32_t kRendererDebugCellPlaceholder = 1u << 0;

struct RendererDebugCell {
    float center[3] = {0.0f, 0.0f, 0.0f};
    float size = 64.0f;
    uint32_t flags = 0;

    bool IsPlaceholder() const { return (flags & kRendererDebugCellPlaceholder) != 0; }
};

struct RendererFrameDebugStats {
    size_t submittedDebugCellCount = 0;
    size_t renderedDebugCellCount = 0;
    size_t overflowDebugCellCount = 0;
    size_t placeholderDebugCellCount = 0;
    size_t renderedPlaceholderDebugCellCount = 0;

    bool HasDebugCells() const { return submittedDebugCellCount != 0; }
    bool HasRenderedDebugCells() const { return renderedDebugCellCount != 0; }
    bool HasDebugCellOverflow() const { return overflowDebugCellCount != 0; }
    bool HasPlaceholderDebugCells() const { return placeholderDebugCellCount != 0; }
    bool HasRenderedPlaceholderDebugCells() const { return renderedPlaceholderDebugCellCount != 0; }
    bool HasDebugCellActivity() const {
        return HasDebugCells() || HasRenderedDebugCells() || HasDebugCellOverflow() ||
            HasPlaceholderDebugCells() || HasRenderedPlaceholderDebugCells();
    }
};

struct RendererFrameDesc {
    float cameraPosition[3] = {0.0f, 0.0f, -5.0f};
    float cameraTarget[3] = {0.0f, 0.0f, 0.0f};
    float cameraUp[3] = {0.0f, 1.0f, 0.0f};
    float deltaSeconds = 1.0f / 60.0f;
    std::vector<RendererDebugCell> debugCells;

    size_t DebugCellCount() const { return debugCells.size(); }
    size_t RenderedDebugCellCount() const {
        return debugCells.size() > kMaxRendererDebugCells ? kMaxRendererDebugCells : debugCells.size();
    }
    size_t DebugCellOverflowCount() const {
        return debugCells.size() > kMaxRendererDebugCells ? debugCells.size() - kMaxRendererDebugCells : 0;
    }
    size_t PlaceholderDebugCellCount() const {
        size_t count = 0;
        for (const RendererDebugCell& cell : debugCells) {
            if (cell.IsPlaceholder()) {
                ++count;
            }
        }
        return count;
    }
    size_t RenderedPlaceholderDebugCellCount() const {
        size_t count = 0;
        const size_t renderedCount = RenderedDebugCellCount();
        for (size_t i = 0; i < renderedCount; ++i) {
            if (debugCells[i].IsPlaceholder()) {
                ++count;
            }
        }
        return count;
    }
    RendererFrameDebugStats GetDebugStats() const {
        RendererFrameDebugStats stats;
        stats.submittedDebugCellCount = DebugCellCount();
        stats.renderedDebugCellCount = RenderedDebugCellCount();
        stats.overflowDebugCellCount = DebugCellOverflowCount();
        stats.placeholderDebugCellCount = PlaceholderDebugCellCount();
        stats.renderedPlaceholderDebugCellCount = RenderedPlaceholderDebugCellCount();
        return stats;
    }
    bool HasDebugCells() const { return !debugCells.empty(); }
    bool HasRenderedDebugCells() const { return RenderedDebugCellCount() != 0; }
    bool HasDebugCellOverflow() const { return debugCells.size() > kMaxRendererDebugCells; }
    bool HasPlaceholderDebugCells() const { return PlaceholderDebugCellCount() != 0; }
    bool HasRenderedPlaceholderDebugCells() const { return RenderedPlaceholderDebugCellCount() != 0; }
    bool HasDebugCellActivity() const { return GetDebugStats().HasDebugCellActivity(); }
};

enum class RendererDrawItemKind : uint8_t {
    Unknown = 0,
    Base,
    DebugCell,
};

inline const char* RendererDrawItemKindName(RendererDrawItemKind kind) {
    switch (kind) {
        case RendererDrawItemKind::Base:
            return "base";
        case RendererDrawItemKind::DebugCell:
            return "debugCell";
        case RendererDrawItemKind::Unknown:
        default:
            return "unknown";
    }
}

struct RendererDrawItemInfo {
    bool active = false;
    RendererDrawItemKind kind = RendererDrawItemKind::Unknown;
    uint32_t drawIndex = 0;
    uint32_t debugCellIndex = kRendererInvalidBindingIndex;
    bool debugCellPlaceholder = false;
    uint32_t uniformBufferIndex = kRendererMaterialUniformBufferIndex;
    uint64_t uniformBufferOffset = 0;
    uint32_t vertexBufferIndex = kRendererGeometryVertexBufferIndex;
    uint32_t vertexStride = 0;
    RHI::IndexFormat indexFormat = RHI::IndexFormat::Unknown;
    uint64_t indexBufferByteOffset = 0;
    uint64_t resolvedIndexBufferByteOffset = 0;
    uint32_t indexCount = 0;
    uint32_t instanceCount = 0;
    uint32_t indexOffset = 0;
    int32_t vertexOffset = 0;
    uint32_t instanceOffset = 0;

    bool IsBaseDraw() const { return kind == RendererDrawItemKind::Base; }
    bool IsDebugCellDraw() const { return kind == RendererDrawItemKind::DebugCell; }
    bool HasDebugCellIndex() const { return debugCellIndex != kRendererInvalidBindingIndex; }
    bool HasUniformBufferBinding() const { return uniformBufferIndex != kRendererInvalidBindingIndex; }
    bool HasVertexBufferBinding() const {
        return vertexBufferIndex != kRendererInvalidBindingIndex && vertexStride != 0;
    }
    bool HasIndexFormat() const { return indexFormat != RHI::IndexFormat::Unknown; }
    bool HasIndexCount() const { return indexCount != 0; }
    bool HasInstances() const { return instanceCount != 0; }
    bool HasResolvedIndexBufferOffset() const {
        return HasIndexCount() || resolvedIndexBufferByteOffset != 0;
    }
    bool HasIndexedDraw() const {
        return active && HasIndexFormat() && HasIndexCount() && HasInstances();
    }
    bool HasReadyItem() const {
        return HasIndexedDraw() && HasUniformBufferBinding() && HasVertexBufferBinding();
    }
};

struct RendererDrawItemStats {
    bool ready = false;
    uint32_t capacity = static_cast<uint32_t>(kRendererDrawItemStatsMaxCount);
    uint32_t itemCount = 0;
    uint32_t baseItemCount = 0;
    uint32_t debugItemCount = 0;
    uint32_t placeholderDebugItemCount = 0;
    uint32_t culledDebugItemCount = 0;
    std::array<RendererDrawItemInfo, kRendererDrawItemStatsMaxCount> items;

    const RendererDrawItemInfo* GetItem(size_t index) const {
        return index < itemCount && index < items.size() ? &items[index] : nullptr;
    }
    const RendererDrawItemInfo* GetFirstItem() const { return GetItem(0); }
    const RendererDrawItemInfo* GetLastItem() const {
        return itemCount != 0 ? GetItem(static_cast<size_t>(itemCount - 1)) : nullptr;
    }
    bool HasCapacity() const { return capacity != 0; }
    bool HasItems() const { return itemCount != 0; }
    bool HasBaseItem() const { return baseItemCount != 0; }
    bool HasDebugItems() const { return debugItemCount != 0; }
    bool HasPlaceholderDebugItems() const { return placeholderDebugItemCount != 0; }
    bool HasCulledDebugItems() const { return culledDebugItemCount != 0; }
    bool HasCompleteDrawItemTable() const {
        const uint64_t classifiedItemCount =
            static_cast<uint64_t>(baseItemCount) + static_cast<uint64_t>(debugItemCount);
        return itemCount <= capacity && itemCount <= items.size() &&
            static_cast<uint64_t>(itemCount) == classifiedItemCount;
    }
    bool HasReadyItems() const { return ready && HasItems() && HasCompleteDrawItemTable(); }
};

struct RendererTextureUploadDesc {
    RendererTextureSlot slot = RendererTextureSlot::BaseColor;
    RendererTextureFormat format = RendererTextureFormat::RGBA8Unorm;
    uint64_t sourceAssetId = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    const void* pixels = nullptr;
    size_t pixelBytes = 0;
};

struct RendererTextureUploadValidation {
    RendererTextureUploadValidationError error = RendererTextureUploadValidationError::None;
    size_t requiredBytes = 0;

    explicit operator bool() const { return error == RendererTextureUploadValidationError::None; }
};

inline RendererTextureUploadValidation ValidateRendererTextureUploadDesc(const RendererTextureUploadDesc& texture) {
    RendererTextureUploadValidation validation;
    if (RendererTextureSlotIndex(texture.slot) >= kRendererTextureSlotCount) {
        validation.error = RendererTextureUploadValidationError::InvalidSlot;
        return validation;
    }
    if (!texture.pixels) {
        validation.error = RendererTextureUploadValidationError::MissingPixels;
        return validation;
    }
    if (texture.width == 0 || texture.height == 0) {
        validation.error = RendererTextureUploadValidationError::InvalidDimensions;
        return validation;
    }
    if (RendererTextureFormatBytesPerPixel(texture.format) == 0) {
        validation.error = RendererTextureUploadValidationError::UnsupportedFormat;
        return validation;
    }
    if (!RendererTextureUploadRequiredBytes(texture.format,
                                            texture.width,
                                            texture.height,
                                            validation.requiredBytes)) {
        validation.error = RendererTextureUploadValidationError::ByteSizeOverflow;
        validation.requiredBytes = 0;
        return validation;
    }
    if (texture.pixelBytes < validation.requiredBytes) {
        validation.error = RendererTextureUploadValidationError::InsufficientPixels;
        return validation;
    }
    return validation;
}

struct RendererTextureHandle {
    uint64_t id = 0;

    explicit operator bool() const { return id != 0; }
};

struct RendererMaterialHandle {
    uint64_t id = 0;

    explicit operator bool() const { return id != 0; }
};

enum class RendererMaterialValidationError : uint8_t {
    None,
    NonFiniteBaseColorFactor,
    BaseColorFactorOutOfRange,
    NonFiniteRoughness,
    RoughnessOutOfRange,
    NonFiniteMetallic,
    MetallicOutOfRange,
    NonFiniteExposure,
    ExposureOutOfRange,
};

inline const char* RendererMaterialValidationErrorName(RendererMaterialValidationError error) {
    switch (error) {
        case RendererMaterialValidationError::None:
            return "none";
        case RendererMaterialValidationError::NonFiniteBaseColorFactor:
            return "non-finite-base-color-factor";
        case RendererMaterialValidationError::BaseColorFactorOutOfRange:
            return "base-color-factor-out-of-range";
        case RendererMaterialValidationError::NonFiniteRoughness:
            return "non-finite-roughness";
        case RendererMaterialValidationError::RoughnessOutOfRange:
            return "roughness-out-of-range";
        case RendererMaterialValidationError::NonFiniteMetallic:
            return "non-finite-metallic";
        case RendererMaterialValidationError::MetallicOutOfRange:
            return "metallic-out-of-range";
        case RendererMaterialValidationError::NonFiniteExposure:
            return "non-finite-exposure";
        case RendererMaterialValidationError::ExposureOutOfRange:
            return "exposure-out-of-range";
        default:
            return "unknown";
    }
}

struct RendererMaterialValidation {
    RendererMaterialValidationError error = RendererMaterialValidationError::None;
    size_t baseColorFactorIndex = 0;

    explicit operator bool() const { return error == RendererMaterialValidationError::None; }
};

inline bool RendererMaterialUnitValueIsValid(float value) {
    return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
}

struct RendererMaterialDesc {
    uint64_t sourceAssetId = 0;
    RendererTextureHandle baseColorTexture;
    RendererTextureHandle normalTexture;
    RendererTextureHandle metallicRoughnessTexture;
    RendererTextureHandle emissiveTexture;
    RendererTextureHandle occlusionTexture;
    float baseColorFactor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float roughness = 0.42f;
    float metallic = 0.05f;
    float exposure = 1.25f;

    RendererTextureHandle GetTexture(RendererTextureSlot slot) const {
        switch (slot) {
            case RendererTextureSlot::BaseColor:
                return baseColorTexture;
            case RendererTextureSlot::Normal:
                return normalTexture;
            case RendererTextureSlot::MetallicRoughness:
                return metallicRoughnessTexture;
            case RendererTextureSlot::Emissive:
                return emissiveTexture;
            case RendererTextureSlot::Occlusion:
                return occlusionTexture;
            default:
                return {};
        }
    }

    bool HasSourceAsset() const { return sourceAssetId != 0; }
    bool HasTexture(RendererTextureSlot slot) const { return static_cast<bool>(GetTexture(slot)); }
    bool HasBaseColorTexture() const { return HasTexture(RendererTextureSlot::BaseColor); }
    bool HasNormalTexture() const { return HasTexture(RendererTextureSlot::Normal); }
    bool HasMetallicRoughnessTexture() const { return HasTexture(RendererTextureSlot::MetallicRoughness); }
    bool HasEmissiveTexture() const { return HasTexture(RendererTextureSlot::Emissive); }
    bool HasOcclusionTexture() const { return HasTexture(RendererTextureSlot::Occlusion); }

    size_t BoundTextureCount() const {
        size_t count = 0;
        const RendererTextureSlot slots[] = {
            RendererTextureSlot::BaseColor,
            RendererTextureSlot::Normal,
            RendererTextureSlot::MetallicRoughness,
            RendererTextureSlot::Emissive,
            RendererTextureSlot::Occlusion,
        };
        for (RendererTextureSlot slot : slots) {
            if (HasTexture(slot)) {
                ++count;
            }
        }
        return count;
    }
    bool HasAnyTexture() const { return BoundTextureCount() != 0; }
    bool HasCompleteTextureSet() const {
        return HasBaseColorTexture() && HasNormalTexture() && HasMetallicRoughnessTexture() &&
            HasEmissiveTexture() && HasOcclusionTexture();
    }
    bool HasValidBaseColorFactor() const {
        for (float channel : baseColorFactor) {
            if (!RendererMaterialUnitValueIsValid(channel)) {
                return false;
            }
        }
        return true;
    }
    bool HasValidRoughness() const { return RendererMaterialUnitValueIsValid(roughness); }
    bool HasValidMetallic() const { return RendererMaterialUnitValueIsValid(metallic); }
    bool HasValidExposure() const { return std::isfinite(exposure) && exposure >= 0.0f; }
    bool HasValidParameters() const {
        return HasValidBaseColorFactor() && HasValidRoughness() && HasValidMetallic() &&
            HasValidExposure();
    }

    void SetTexture(RendererTextureSlot slot, RendererTextureHandle texture) {
        switch (slot) {
            case RendererTextureSlot::BaseColor:
                baseColorTexture = texture;
                break;
            case RendererTextureSlot::Normal:
                normalTexture = texture;
                break;
            case RendererTextureSlot::MetallicRoughness:
                metallicRoughnessTexture = texture;
                break;
            case RendererTextureSlot::Emissive:
                emissiveTexture = texture;
                break;
            case RendererTextureSlot::Occlusion:
                occlusionTexture = texture;
                break;
            default:
                break;
        }
    }
};

inline RendererMaterialValidation ValidateRendererMaterialDesc(const RendererMaterialDesc& material) {
    RendererMaterialValidation validation;
    for (size_t i = 0; i < sizeof(material.baseColorFactor) / sizeof(material.baseColorFactor[0]); ++i) {
        const float channel = material.baseColorFactor[i];
        if (!std::isfinite(channel)) {
            validation.error = RendererMaterialValidationError::NonFiniteBaseColorFactor;
            validation.baseColorFactorIndex = i;
            return validation;
        }
        if (channel < 0.0f || channel > 1.0f) {
            validation.error = RendererMaterialValidationError::BaseColorFactorOutOfRange;
            validation.baseColorFactorIndex = i;
            return validation;
        }
    }

    if (!std::isfinite(material.roughness)) {
        validation.error = RendererMaterialValidationError::NonFiniteRoughness;
        return validation;
    }
    if (material.roughness < 0.0f || material.roughness > 1.0f) {
        validation.error = RendererMaterialValidationError::RoughnessOutOfRange;
        return validation;
    }

    if (!std::isfinite(material.metallic)) {
        validation.error = RendererMaterialValidationError::NonFiniteMetallic;
        return validation;
    }
    if (material.metallic < 0.0f || material.metallic > 1.0f) {
        validation.error = RendererMaterialValidationError::MetallicOutOfRange;
        return validation;
    }

    if (!std::isfinite(material.exposure)) {
        validation.error = RendererMaterialValidationError::NonFiniteExposure;
        return validation;
    }
    if (material.exposure < 0.0f) {
        validation.error = RendererMaterialValidationError::ExposureOutOfRange;
        return validation;
    }

    return validation;
}

struct RendererMaterialInfo {
    RendererMaterialHandle material;
    RendererMaterialDesc desc;
    bool active = false;

    bool HasMaterial() const { return static_cast<bool>(material); }
    bool HasSourceAsset() const { return desc.HasSourceAsset(); }
    bool HasTexture(RendererTextureSlot slot) const { return desc.HasTexture(slot); }
    size_t BoundTextureCount() const { return desc.BoundTextureCount(); }
    bool HasBoundTextures() const { return BoundTextureCount() != 0; }
    bool HasBaseColorTexture() const { return desc.HasBaseColorTexture(); }
    bool HasNormalTexture() const { return desc.HasNormalTexture(); }
    bool HasMetallicRoughnessTexture() const { return desc.HasMetallicRoughnessTexture(); }
    bool HasEmissiveTexture() const { return desc.HasEmissiveTexture(); }
    bool HasOcclusionTexture() const { return desc.HasOcclusionTexture(); }
    bool HasCompleteTextureSet() const { return desc.HasCompleteTextureSet(); }
    bool HasValidParameters() const { return desc.HasValidParameters(); }
    bool HasActiveMaterial() const { return active && HasMaterial(); }
    bool HasMaterialActivity() const { return HasActiveMaterial() || HasSourceAsset() || HasBoundTextures(); }
};

enum class RendererMaterialBindingSource : uint8_t {
    Missing = 0,
    MaterialTexture,
    ActiveSlotTexture,
    NeutralTexture,
};

inline const char* RendererMaterialBindingSourceName(RendererMaterialBindingSource source) {
    switch (source) {
        case RendererMaterialBindingSource::MaterialTexture:
            return "materialTexture";
        case RendererMaterialBindingSource::ActiveSlotTexture:
            return "activeSlotTexture";
        case RendererMaterialBindingSource::NeutralTexture:
            return "neutralTexture";
        case RendererMaterialBindingSource::Missing:
        default:
            return "missing";
    }
}

struct RendererMaterialBindingInfo {
    RendererTextureSlot slot = RendererTextureSlot::BaseColor;
    RendererMaterialBindingSource source = RendererMaterialBindingSource::Missing;
    RendererTextureHandle requestedTexture;
    RendererTextureHandle boundTexture;
    RendererTextureFormat format = RendererTextureFormat::Unknown;
    uint64_t sourceAssetId = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t textureBindingIndex = kRendererInvalidBindingIndex;
    uint32_t samplerBindingIndex = kRendererInvalidBindingIndex;
    uint32_t uniformBufferIndex = kRendererInvalidBindingIndex;
    bool textureReady = false;
    bool samplerReady = false;

    bool HasRequestedTexture() const { return static_cast<bool>(requestedTexture); }
    bool HasBoundTexture() const { return static_cast<bool>(boundTexture); }
    bool HasFormat() const { return format != RendererTextureFormat::Unknown; }
    bool HasSourceAsset() const { return sourceAssetId != 0; }
    bool HasDimensions() const { return width != 0 && height != 0; }
    bool HasTextureBindingIndex() const { return textureBindingIndex != kRendererInvalidBindingIndex; }
    bool HasSamplerBindingIndex() const { return samplerBindingIndex != kRendererInvalidBindingIndex; }
    bool HasUniformBufferIndex() const { return uniformBufferIndex != kRendererInvalidBindingIndex; }
    bool HasBindingIndices() const {
        return HasTextureBindingIndex() && HasSamplerBindingIndex() && HasUniformBufferIndex();
    }
    bool HasTextureReady() const { return textureReady; }
    bool HasSamplerReady() const { return samplerReady; }
    bool UsesMaterialTexture() const { return source == RendererMaterialBindingSource::MaterialTexture; }
    bool UsesActiveSlotTexture() const { return source == RendererMaterialBindingSource::ActiveSlotTexture; }
    bool UsesNeutralTexture() const { return source == RendererMaterialBindingSource::NeutralTexture; }
    bool UsesFallbackTexture() const { return UsesActiveSlotTexture() || UsesNeutralTexture(); }
    bool IsShaderVisible() const { return textureReady && samplerReady; }
    bool HasBindingActivity() const {
        return HasRequestedTexture() || HasBoundTexture() || HasTextureReady() || HasSamplerReady();
    }
};

struct RendererMaterialStats {
    bool ready = false;
    uint32_t materialCapacity = 0;
    uint32_t materialCount = 0;
    RendererMaterialHandle activeMaterial;
    uint32_t activeMaterialIndex = 0;
    uint32_t activeMaterialBoundTextureCount = 0;
    bool activeMaterialCompleteTextureSet = false;
    bool activeMaterialParametersValid = false;
    uint32_t shaderVisibleTextureCount = 0;
    uint32_t shaderVisibleSamplerCount = 0;
    uint32_t fallbackTextureCount = 0;
    RendererMaterialBindingLayoutInfo bindingLayout;
    uint32_t shaderResourceGroupBindingCount = 0;
    RHI::ShaderResourceGroupLayoutValidation shaderResourceGroupLayoutValidation;
    bool shaderResourceGroupArgumentEncoderReady = false;
    uint32_t shaderResourceGroupArgumentCount = 0;
    uint64_t shaderResourceGroupEncodedLength = 0;
    uint64_t shaderResourceGroupEncodedStride = 0;
    bool shaderResourceGroupArgumentBufferReady = false;
    uint64_t shaderResourceGroupArgumentBufferBytes = 0;
    uint32_t shaderResourceGroupArgumentBufferDrawCapacity = 0;
    uint32_t shaderResourceGroupEncodedDrawCount = 0;
    uint32_t shaderResourceGroupArgumentBufferBindingIndex = kRendererInvalidBindingIndex;
    bool shaderResourceGroupArgumentBufferBound = false;
    uint64_t shaderResourceGroupArgumentBufferBindCount = 0;
    uint32_t shaderResourceGroupEncodedResourceCount = 0;
    std::array<RendererShaderResourceBindingInfo, kRendererMaterialShaderResourceGroupBindingCount>
        shaderResourceGroupBindings;
    std::array<RendererMaterialBindingInfo, kRendererTextureSlotCount> activeMaterialBindings;

    void SetShaderResourceGroupLayout(const RHI::ShaderResourceGroupLayoutDesc& layout) {
        shaderResourceGroupBindings = {};
        shaderResourceGroupBindingCount = layout.bindingCount;
        shaderResourceGroupLayoutValidation = RHI::ValidateShaderResourceGroupLayoutDesc(layout);
        const uint32_t bindingTableCount =
            layout.bindingCount < shaderResourceGroupBindings.size()
                ? layout.bindingCount
                : static_cast<uint32_t>(shaderResourceGroupBindings.size());
        for (uint32_t i = 0; i < bindingTableCount; ++i) {
            const RHI::ShaderResourceBindingDesc& binding = layout.bindings[i];
            RendererShaderResourceBindingInfo& info = shaderResourceGroupBindings[i];
            info.active = true;
            info.index = i;
            info.type = binding.type;
            info.shaderStages = binding.shaderStages;
            info.bindingIndex = binding.bindingIndex;
            info.bindingCount = binding.bindingCount;
            info.SetDebugName(binding.debugName);
        }
    }

    const RendererShaderResourceBindingInfo* GetShaderResourceGroupBinding(size_t index) const {
        return index < shaderResourceGroupBindingCount && index < shaderResourceGroupBindings.size()
            ? &shaderResourceGroupBindings[index]
            : nullptr;
    }

    void SetShaderResourceGroupBoundResourceCount(size_t index, uint32_t boundResourceCount) {
        if (index >= shaderResourceGroupBindingCount || index >= shaderResourceGroupBindings.size()) {
            return;
        }

        shaderResourceGroupBindings[index].boundResourceCount = boundResourceCount;
    }

    RendererMaterialBindingInfo GetActiveMaterialBinding(RendererTextureSlot slot) const {
        const size_t index = RendererTextureSlotIndex(slot);
        if (index >= activeMaterialBindings.size()) {
            return {};
        }

        RendererMaterialBindingInfo info = activeMaterialBindings[index];
        info.slot = slot;
        return info;
    }

    bool HasMaterialCapacity() const { return materialCapacity != 0; }
    bool HasMaterials() const { return materialCount != 0; }
    bool HasActiveMaterial() const { return static_cast<bool>(activeMaterial); }
    bool HasActiveMaterialSlot() const { return HasActiveMaterial() && activeMaterialIndex < materialCapacity; }
    bool HasFreeMaterialSlots() const { return HasMaterialCapacity() && materialCount < materialCapacity; }
    bool IsMaterialTableFull() const { return HasMaterialCapacity() && materialCount >= materialCapacity; }
    bool HasActiveBoundTextures() const { return activeMaterialBoundTextureCount != 0; }
    bool HasCompleteActiveTextureSet() const { return activeMaterialCompleteTextureSet; }
    bool HasValidActiveParameters() const { return activeMaterialParametersValid; }
    bool HasShaderVisibleTextures() const { return shaderVisibleTextureCount != 0; }
    bool HasShaderVisibleSamplers() const { return shaderVisibleSamplerCount != 0; }
    bool HasFallbackTextures() const { return fallbackTextureCount != 0; }
    bool HasCompleteShaderBindings() const {
        return shaderVisibleTextureCount == kRendererTextureSlotCount &&
            shaderVisibleSamplerCount == kRendererTextureSlotCount;
    }
    bool HasMaterialBindingLayout() const { return bindingLayout.HasFixedMaterialBindingLayout(); }
    bool HasShaderResourceGroupLayout() const { return shaderResourceGroupBindingCount != 0; }
    bool HasShaderResourceGroupArgumentEncoder() const {
        return shaderResourceGroupArgumentEncoderReady;
    }
    bool HasShaderResourceGroupArguments() const {
        return shaderResourceGroupArgumentCount != 0;
    }
    bool HasShaderResourceGroupEncodedLength() const {
        return shaderResourceGroupEncodedLength != 0;
    }
    bool HasShaderResourceGroupEncodedStride() const {
        return shaderResourceGroupEncodedStride != 0;
    }
    bool HasShaderResourceGroupArgumentBuffer() const {
        return shaderResourceGroupArgumentBufferReady;
    }
    bool HasShaderResourceGroupArgumentBufferBytes() const {
        return shaderResourceGroupArgumentBufferBytes != 0;
    }
    bool HasShaderResourceGroupArgumentBufferDrawCapacity() const {
        return shaderResourceGroupArgumentBufferDrawCapacity != 0;
    }
    bool HasEncodedShaderResourceGroupDraws() const {
        return shaderResourceGroupEncodedDrawCount != 0;
    }
    bool HasShaderResourceGroupArgumentBufferBindingIndex() const {
        return shaderResourceGroupArgumentBufferBindingIndex != kRendererInvalidBindingIndex;
    }
    bool HasBoundShaderResourceGroupArgumentBuffer() const {
        return shaderResourceGroupArgumentBufferBound;
    }
    bool HasShaderResourceGroupArgumentBufferBindings() const {
        return shaderResourceGroupArgumentBufferBindCount != 0;
    }
    bool HasEncodedShaderResourceGroupResources() const {
        return shaderResourceGroupEncodedResourceCount != 0;
    }
    bool HasReadyShaderResourceGroupArgumentEncoder() const {
        return HasShaderResourceGroupArgumentEncoder() && HasShaderResourceGroupArguments() &&
            HasShaderResourceGroupEncodedLength() && HasShaderResourceGroupEncodedStride() &&
            shaderResourceGroupArgumentCount == shaderResourceGroupBindingCount;
    }
    uint32_t RequiredShaderResourceGroupEncodedResourceCount() const {
        if (!HasCompleteShaderResourceGroupBindingTable()) {
            return 0;
        }

        uint32_t requiredResourceCount = 0;
        for (size_t i = 0; i < shaderResourceGroupBindingCount; ++i) {
            requiredResourceCount += shaderResourceGroupBindings[i].bindingCount;
        }
        return requiredResourceCount;
    }
    bool HasCompleteShaderResourceGroupEncoding() const {
        const uint32_t requiredResourceCount = RequiredShaderResourceGroupEncodedResourceCount();
        const uint64_t requiredTotalResourceCount =
            static_cast<uint64_t>(requiredResourceCount) * shaderResourceGroupEncodedDrawCount;
        return HasReadyShaderResourceGroupArgumentEncoder() && HasShaderResourceGroupArgumentBuffer() &&
            HasShaderResourceGroupArgumentBufferBytes() &&
            HasShaderResourceGroupArgumentBufferDrawCapacity() && HasEncodedShaderResourceGroupDraws() &&
            HasShaderResourceGroupArgumentBufferBindingIndex() && requiredResourceCount != 0 &&
            shaderResourceGroupEncodedDrawCount <= shaderResourceGroupArgumentBufferDrawCapacity &&
            shaderResourceGroupEncodedResourceCount >= requiredTotalResourceCount;
    }
    bool HasCompleteShaderResourceGroupArgumentBufferBinding() const {
        return HasCompleteShaderResourceGroupEncoding() && HasBoundShaderResourceGroupArgumentBuffer() &&
            HasShaderResourceGroupArgumentBufferBindings();
    }
    bool HasCompleteShaderResourceGroupBindingTable() const {
        if (!HasShaderResourceGroupLayout() || shaderResourceGroupBindingCount > shaderResourceGroupBindings.size()) {
            return false;
        }
        for (size_t i = 0; i < shaderResourceGroupBindingCount; ++i) {
            if (!shaderResourceGroupBindings[i].HasReadyBinding() || shaderResourceGroupBindings[i].index != i) {
                return false;
            }
        }
        return true;
    }
    bool HasValidShaderResourceGroupLayout() const {
        return HasCompleteShaderResourceGroupBindingTable() && static_cast<bool>(shaderResourceGroupLayoutValidation);
    }
    bool HasCompleteShaderResourceGroupResources() const {
        if (!HasValidShaderResourceGroupLayout()) {
            return false;
        }

        for (size_t i = 0; i < shaderResourceGroupBindingCount; ++i) {
            if (!shaderResourceGroupBindings[i].HasCompleteBoundResources()) {
                return false;
            }
        }
        return true;
    }
    bool HasMaterialActivity() const {
        return ready || HasMaterials() || HasActiveMaterial() || HasShaderVisibleTextures() ||
            HasShaderVisibleSamplers() || HasShaderResourceGroupArgumentEncoder() ||
            HasShaderResourceGroupArgumentBuffer();
    }
};

struct RendererTextureInfo {
    RendererTextureHandle texture;
    RendererTextureSlot slot = RendererTextureSlot::BaseColor;
    RendererTextureFormat format = RendererTextureFormat::Unknown;
    uint64_t sourceAssetId = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    bool active = false;
    bool uploadPending = false;

    bool HasTexture() const { return static_cast<bool>(texture); }
    bool HasFormat() const { return format != RendererTextureFormat::Unknown; }
    bool HasSourceAsset() const { return sourceAssetId != 0; }
    bool HasDimensions() const { return width != 0 && height != 0; }
    bool HasPendingUpload() const { return uploadPending; }
    bool HasReadyTexture() const { return HasTexture() && HasFormat() && HasDimensions() && !HasPendingUpload(); }
    bool HasActiveTexture() const { return active && HasTexture() && HasDimensions(); }
    bool HasTextureActivity() const { return HasPendingUpload() || HasActiveTexture() || HasReadyTexture(); }
};

struct RendererActiveTextureSlotInfo {
    RendererTextureHandle texture;
    uint64_t sourceAssetId = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    bool active = false;

    bool HasTexture() const { return static_cast<bool>(texture); }
    bool HasSourceAsset() const { return sourceAssetId != 0; }
    bool HasDimensions() const { return width != 0 && height != 0; }
    bool HasReadyTexture() const { return HasTexture() && HasDimensions(); }
    bool HasActiveTexture() const { return active && HasTexture() && HasDimensions(); }
};

struct RendererTextureUploadStats {
    uint64_t queuedUploads = 0;
    uint64_t completedUploads = 0;
    uint64_t failedUploads = 0;
    uint64_t lastQueuedUpload = 0;
    uint64_t lastCompletedUpload = 0;
    uint64_t lastFailedUpload = 0;
    RendererTextureHandle lastQueuedTexture;
    RendererTextureHandle lastCompletedTexture;
    RendererTextureHandle lastFailedTexture;
    RendererTextureSlot lastQueuedSlot = RendererTextureSlot::BaseColor;
    RendererTextureSlot lastCompletedSlot = RendererTextureSlot::BaseColor;
    RendererTextureSlot lastFailedSlot = RendererTextureSlot::BaseColor;
    bool materialTextureUploadPending = false;
    bool baseColorUploadPending = false;
    uint32_t activeMaterialTextureCount = 0;
    RendererTextureHandle activeBaseColorTexture;
    uint64_t activeBaseColorSourceAssetId = 0;
    uint32_t activeBaseColorWidth = 0;
    uint32_t activeBaseColorHeight = 0;
    std::array<RendererActiveTextureSlotInfo, kRendererTextureSlotCount> activeMaterialTextures;

    RendererActiveTextureSlotInfo GetActiveTexture(RendererTextureSlot slot) const {
        const size_t index = RendererTextureSlotIndex(slot);
        if (index >= activeMaterialTextures.size()) {
            return {};
        }
        return activeMaterialTextures[index];
    }

    uint64_t FinishedUploadCount() const { return completedUploads + failedUploads; }
    bool HasQueuedUploads() const { return queuedUploads != 0; }
    bool HasCompletedUploads() const { return completedUploads != 0; }
    bool HasFailedUploads() const { return failedUploads != 0; }
    bool HasFinishedUploads() const { return FinishedUploadCount() != 0; }
    bool HasLastQueuedUpload() const { return lastQueuedUpload != 0; }
    bool HasLastCompletedUpload() const { return lastCompletedUpload != 0; }
    bool HasLastFailedUpload() const { return lastFailedUpload != 0; }
    bool HasLastQueuedTexture() const { return static_cast<bool>(lastQueuedTexture); }
    bool HasLastCompletedTexture() const { return static_cast<bool>(lastCompletedTexture); }
    bool HasLastFailedTexture() const { return static_cast<bool>(lastFailedTexture); }
    bool HasPendingMaterialTextureUploads() const { return materialTextureUploadPending; }
    bool HasPendingBaseColorUpload() const { return baseColorUploadPending; }
    bool HasPendingUploads() const { return materialTextureUploadPending || baseColorUploadPending; }
    bool HasActiveMaterialTextures() const { return activeMaterialTextureCount != 0; }
    bool HasActiveBaseColorTexture() const {
        return static_cast<bool>(activeBaseColorTexture) &&
            activeBaseColorWidth != 0 && activeBaseColorHeight != 0;
    }
    bool HasActiveBaseColorSourceAsset() const { return activeBaseColorSourceAssetId != 0; }
    bool HasTextureUploadActivity() const {
        return HasQueuedUploads() || HasFinishedUploads() || HasPendingUploads() ||
            HasActiveMaterialTextures();
    }
};

struct RendererResourcePoolStats {
    RHI::ResourcePoolStats buffers{RHI::ResourceType::Buffer};
    RHI::ResourcePoolStats textures{RHI::ResourceType::Texture};

    RHI::ResourcePoolStats* FindPoolStats(RHI::ResourceType resourceType) {
        switch (resourceType) {
            case RHI::ResourceType::Buffer:
                return &buffers;
            case RHI::ResourceType::Texture:
                return &textures;
            default:
                return nullptr;
        }
    }

    const RHI::ResourcePoolStats* FindPoolStats(RHI::ResourceType resourceType) const {
        switch (resourceType) {
            case RHI::ResourceType::Buffer:
                return &buffers;
            case RHI::ResourceType::Texture:
                return &textures;
            default:
                return nullptr;
        }
    }

    RHI::ResourcePoolMemoryStats* FindMemoryStats(RHI::ResourceType resourceType, RHI::ResourceMemory memory) {
        RHI::ResourcePoolStats* poolStats = FindPoolStats(resourceType);
        return poolStats ? RHI::FindResourcePoolMemoryStats(*poolStats, memory) : nullptr;
    }

    const RHI::ResourcePoolMemoryStats* FindMemoryStats(RHI::ResourceType resourceType,
                                                        RHI::ResourceMemory memory) const {
        const RHI::ResourcePoolStats* poolStats = FindPoolStats(resourceType);
        return poolStats ? RHI::FindResourcePoolMemoryStats(*poolStats, memory) : nullptr;
    }

    bool BudgetRemaining(RHI::ResourceType resourceType, RHI::ResourceMemory memory, uint64_t& outBytes) const {
        const RHI::ResourcePoolStats* poolStats = FindPoolStats(resourceType);
        if (!poolStats) {
            outBytes = 0;
            return false;
        }
        return RHI::ResourcePoolMemoryBudgetRemaining(*poolStats, memory, outBytes);
    }

    bool IsOverBudget(RHI::ResourceType resourceType, RHI::ResourceMemory memory) const {
        const RHI::ResourcePoolStats* poolStats = FindPoolStats(resourceType);
        return poolStats && RHI::ResourcePoolMemoryIsOverBudget(*poolStats, memory);
    }

    bool HasMemoryBudget(RHI::ResourceType resourceType, RHI::ResourceMemory memory) const {
        const RHI::ResourcePoolMemoryStats* memoryStats = FindMemoryStats(resourceType, memory);
        return memoryStats && memoryStats->budgetBytes != 0;
    }

    size_t TotalLiveResourceCount() const {
        return SaturatingAddSize(buffers.liveResourceCount, textures.liveResourceCount);
    }

    uint64_t TotalLiveBytes() const {
        return SaturatingAddUint64(buffers.liveBytes, textures.liveBytes);
    }

    size_t TotalPeakResourceCount() const {
        return SaturatingAddSize(buffers.peakResourceCount, textures.peakResourceCount);
    }

    uint64_t TotalPeakBytes() const {
        return SaturatingAddUint64(buffers.peakBytes, textures.peakBytes);
    }

    uint64_t TotalFailedAllocationCount() const {
        return SaturatingAddUint64(buffers.failedAllocationCount, textures.failedAllocationCount);
    }

    uint64_t TotalFailedAllocationBytes() const {
        return SaturatingAddUint64(buffers.failedAllocationBytes, textures.failedAllocationBytes);
    }

    bool HasLiveResources() const { return TotalLiveResourceCount() != 0 || TotalLiveBytes() != 0; }
    bool HasPeakResources() const { return TotalPeakResourceCount() != 0 || TotalPeakBytes() != 0; }
    bool HasMemoryBudgets() const {
        return HasMemoryBudget(RHI::ResourceType::Buffer, RHI::ResourceMemory::DeviceLocal) ||
            HasMemoryBudget(RHI::ResourceType::Buffer, RHI::ResourceMemory::Shared) ||
            HasMemoryBudget(RHI::ResourceType::Buffer, RHI::ResourceMemory::Upload) ||
            HasMemoryBudget(RHI::ResourceType::Buffer, RHI::ResourceMemory::Readback) ||
            HasMemoryBudget(RHI::ResourceType::Texture, RHI::ResourceMemory::DeviceLocal) ||
            HasMemoryBudget(RHI::ResourceType::Texture, RHI::ResourceMemory::Shared) ||
            HasMemoryBudget(RHI::ResourceType::Texture, RHI::ResourceMemory::Upload) ||
            HasMemoryBudget(RHI::ResourceType::Texture, RHI::ResourceMemory::Readback);
    }
    bool IsAnyOverBudget() const {
        return IsOverBudget(RHI::ResourceType::Buffer, RHI::ResourceMemory::DeviceLocal) ||
            IsOverBudget(RHI::ResourceType::Buffer, RHI::ResourceMemory::Shared) ||
            IsOverBudget(RHI::ResourceType::Buffer, RHI::ResourceMemory::Upload) ||
            IsOverBudget(RHI::ResourceType::Buffer, RHI::ResourceMemory::Readback) ||
            IsOverBudget(RHI::ResourceType::Texture, RHI::ResourceMemory::DeviceLocal) ||
            IsOverBudget(RHI::ResourceType::Texture, RHI::ResourceMemory::Shared) ||
            IsOverBudget(RHI::ResourceType::Texture, RHI::ResourceMemory::Upload) ||
            IsOverBudget(RHI::ResourceType::Texture, RHI::ResourceMemory::Readback);
    }
    bool HasFailedAllocations() const {
        return TotalFailedAllocationCount() != 0 || TotalFailedAllocationBytes() != 0;
    }
    bool HasPoolActivity() const {
        return HasLiveResources() || HasPeakResources() || HasMemoryBudgets() ||
            IsAnyOverBudget() || HasFailedAllocations();
    }

    static size_t SaturatingAddSize(size_t lhs, size_t rhs) {
        const size_t maxValue = std::numeric_limits<size_t>::max();
        return rhs > maxValue - lhs ? maxValue : lhs + rhs;
    }

    static uint64_t SaturatingAddUint64(uint64_t lhs, uint64_t rhs) {
        const uint64_t maxValue = std::numeric_limits<uint64_t>::max();
        return rhs > maxValue - lhs ? maxValue : lhs + rhs;
    }
};

struct RendererResourcePoolBudgetDesc {
    RHI::ResourceType resourceType = RHI::ResourceType::Unknown;
    RHI::ResourceMemory memory = RHI::ResourceMemory::DeviceLocal;
    uint64_t budgetBytes = 0;
};

struct RendererTextureUploadHandle {
    uint64_t id = 0;
    RendererTextureHandle texture;

    explicit operator bool() const { return id != 0; }
};

class Renderer {
public:
    static Renderer* Create(RendererBackend preferredBackend = RendererBackend::Auto);
    static const char* BackendToString(RendererBackend backend);
    static RendererBackend ParseBackend(const char* name);

    virtual ~Renderer() = default;

    virtual bool Initialize(Window* window) = 0;
    virtual void Shutdown() = 0;

    virtual const char* GetBackendName() const = 0;
    virtual RendererDeviceInfo GetDeviceInfo() const { return {}; }
    virtual RendererLifetimeStats GetLifetimeStats() const { return {}; }
    virtual RendererCommandStats GetCommandStats() const { return {}; }
    virtual RendererRenderPassStats GetRenderPassStats() const { return {}; }
    virtual RendererUploadQueueStats GetUploadQueueStats() const { return {}; }
    virtual RendererPipelineStats GetPipelineStats() const { return {}; }
    virtual RendererGeometryStats GetGeometryStats() const { return {}; }
    virtual RendererDrawStateStats GetDrawStateStats() const { return {}; }
    virtual RendererDrawSubmissionStats GetDrawSubmissionStats() const { return {}; }
    virtual RendererDrawItemStats GetDrawItemStats() const { return {}; }
    virtual RendererSwapchainStats GetSwapchainStats() const { return {}; }
    virtual RendererSamplerStats GetSamplerStats() const { return {}; }
    virtual RendererMaterialStats GetMaterialStats() const { return {}; }
    virtual RendererResourceStateStats GetResourceStateStats() const { return {}; }

    virtual void SetFrameDesc(const RendererFrameDesc& frame) { (void)frame; }
    virtual RendererTextureUploadHandle UploadTexture2D(const RendererTextureUploadDesc& texture) {
        (void)texture;
        return {};
    }
    virtual RendererTextureUploadStats GetTextureUploadStats() { return {}; }
    virtual RendererResourcePoolStats GetResourcePoolStats() { return {}; }
    virtual bool SetResourcePoolBudget(const RendererResourcePoolBudgetDesc& budget) {
        (void)budget;
        return false;
    }
    virtual RendererTextureUploadStatus GetTextureUploadStatus(RendererTextureUploadHandle handle) {
        (void)handle;
        return RendererTextureUploadStatus::Unknown;
    }
    virtual bool GetTextureInfo(RendererTextureHandle texture, RendererTextureInfo& outInfo) {
        (void)texture;
        outInfo = {};
        return false;
    }
    virtual RendererMaterialHandle CreateMaterial(const RendererMaterialDesc& material) {
        (void)material;
        return {};
    }
    virtual bool UpdateMaterial(RendererMaterialHandle handle, const RendererMaterialDesc& material) {
        (void)handle;
        (void)material;
        return false;
    }
    virtual bool SetActiveMaterial(RendererMaterialHandle handle) {
        (void)handle;
        return false;
    }
    virtual bool GetMaterialInfo(RendererMaterialHandle handle, RendererMaterialInfo& outInfo) {
        (void)handle;
        outInfo = {};
        return false;
    }

    virtual void BeginFrame() = 0;
    virtual void EndFrame() = 0;
    virtual void Render() = 0;

    virtual void Resize(int width, int height) = 0;
};

} // namespace Next
