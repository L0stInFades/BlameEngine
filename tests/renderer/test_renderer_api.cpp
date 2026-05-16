#include "next/renderer/renderer.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

namespace Next {
namespace testing {
namespace {

constexpr std::array<RendererTextureSlot, kRendererTextureSlotCount> kMaterialSlots = {
    RendererTextureSlot::BaseColor,
    RendererTextureSlot::Normal,
    RendererTextureSlot::MetallicRoughness,
    RendererTextureSlot::Emissive,
    RendererTextureSlot::Occlusion,
};

} // namespace

TEST(RendererApiTest, DeviceInfoDefaultsAndStoresCapabilities) {
    RendererDeviceInfo info;
    EXPECT_FALSE(info.available);
    EXPECT_EQ(info.backend, RendererBackend::Null);
    EXPECT_STREQ(info.GetDeviceName(), "unknown");
    EXPECT_FALSE(info.HasBackend());
    EXPECT_FALSE(info.HasDeviceName());
    EXPECT_FALSE(info.HasRenderableDevice());
    EXPECT_FALSE(info.HasQueueSupport());
    EXPECT_FALSE(info.HasDedicatedQueueSupport());
    EXPECT_FALSE(info.SupportsGraphicsQueue());
    EXPECT_FALSE(info.SupportsComputeQueue());
    EXPECT_FALSE(info.SupportsCopyQueue());
    EXPECT_FALSE(info.HasDedicatedGraphicsQueue());
    EXPECT_FALSE(info.HasDedicatedComputeQueue());
    EXPECT_FALSE(info.HasDedicatedCopyQueue());
    EXPECT_FALSE(info.HasUnifiedMemory());
    EXPECT_FALSE(info.HasArgumentBuffers());
    EXPECT_FALSE(info.HasArgumentBufferTier());
    EXPECT_FALSE(info.HasBindlessResources());
    EXPECT_FALSE(info.HasAsyncUploadQueue());
    EXPECT_FALSE(info.HasDeviceCapabilities());
    EXPECT_EQ(info.features.maxFramesInFlight, 2u);
    EXPECT_FALSE(info.features.SupportsQueueClass(RHI::QueueClass::Graphics));
    EXPECT_EQ(info.features.argumentBufferTier, RHI::ArgumentBufferTier::Unsupported);
    EXPECT_FALSE(info.SupportsArgumentBufferTier(RHI::ArgumentBufferTier::Tier1));

    info.available = true;
    info.backend = RendererBackend::Metal;
    info.features.unifiedMemory = true;
    info.features.argumentBuffers = true;
    info.features.argumentBufferTier = RHI::ArgumentBufferTier::Tier2;
    info.features.bindlessResources = true;
    info.features.supportedQueueClasses =
        RHI::QueueClass::Graphics | RHI::QueueClass::Compute | RHI::QueueClass::Copy;
    info.features.dedicatedQueueClasses = RHI::QueueClass::Graphics | RHI::QueueClass::Copy;
    info.features.asyncUploadQueue = info.features.HasDedicatedQueueClass(RHI::QueueClass::Copy);
    info.features.maxFramesInFlight = 3;
    info.SetDeviceName("Metal GPU");

    EXPECT_TRUE(info.available);
    EXPECT_EQ(info.backend, RendererBackend::Metal);
    EXPECT_STREQ(info.GetDeviceName(), "Metal GPU");
    EXPECT_TRUE(info.HasBackend());
    EXPECT_TRUE(info.HasDeviceName());
    EXPECT_TRUE(info.HasRenderableDevice());
    EXPECT_TRUE(info.features.unifiedMemory);
    EXPECT_TRUE(info.features.argumentBuffers);
    EXPECT_EQ(info.features.argumentBufferTier, RHI::ArgumentBufferTier::Tier2);
    EXPECT_TRUE(info.features.bindlessResources);
    EXPECT_TRUE(info.features.asyncUploadQueue);
    EXPECT_TRUE(info.features.SupportsQueueClass(RHI::QueueClass::Compute));
    EXPECT_TRUE(info.features.HasDedicatedQueueClass(RHI::QueueClass::Copy));
    EXPECT_TRUE(info.HasQueueSupport());
    EXPECT_TRUE(info.HasDedicatedQueueSupport());
    EXPECT_TRUE(info.SupportsGraphicsQueue());
    EXPECT_TRUE(info.SupportsComputeQueue());
    EXPECT_TRUE(info.SupportsCopyQueue());
    EXPECT_TRUE(info.HasDedicatedGraphicsQueue());
    EXPECT_FALSE(info.HasDedicatedComputeQueue());
    EXPECT_TRUE(info.HasDedicatedCopyQueue());
    EXPECT_TRUE(info.HasUnifiedMemory());
    EXPECT_TRUE(info.HasArgumentBuffers());
    EXPECT_TRUE(info.HasArgumentBufferTier());
    EXPECT_TRUE(info.SupportsArgumentBufferTier(RHI::ArgumentBufferTier::Tier1));
    EXPECT_TRUE(info.SupportsArgumentBufferTier(RHI::ArgumentBufferTier::Tier2));
    EXPECT_TRUE(info.HasBindlessResources());
    EXPECT_TRUE(info.HasAsyncUploadQueue());
    EXPECT_TRUE(info.HasDeviceCapabilities());
    EXPECT_EQ(info.features.maxFramesInFlight, 3u);
}

TEST(RendererApiTest, DeviceInfoCopiesEmptyAndLongNamesSafely) {
    RendererDeviceInfo info;
    info.SetDeviceName("");
    EXPECT_STREQ(info.GetDeviceName(), "unknown");
    EXPECT_FALSE(info.HasDeviceName());

    char longName[kRendererDeviceNameMaxLength + 8] = {};
    for (size_t i = 0; i + 1 < sizeof(longName); ++i) {
        longName[i] = 'a';
    }

    info.SetDeviceName(longName);
    EXPECT_TRUE(info.HasDeviceName());
    EXPECT_EQ(info.deviceName[kRendererDeviceNameMaxLength - 1], '\0');
    for (size_t i = 0; i + 1 < kRendererDeviceNameMaxLength; ++i) {
        EXPECT_EQ(info.deviceName[i], 'a');
    }
}

TEST(RendererApiTest, LifetimeStatsExposeSubmittedFramesAndReleaseQueueSnapshot) {
    RendererLifetimeStats stats;
    EXPECT_EQ(stats.submittedFrameIndex, 0u);
    EXPECT_EQ(stats.pendingReleaseObjectCount, 0u);
    EXPECT_FALSE(stats.HasSubmittedFrames());
    EXPECT_FALSE(stats.HasPendingReleases());
    EXPECT_FALSE(stats.HasPeakPendingReleases());
    EXPECT_FALSE(stats.HasQueuedReleases());
    EXPECT_FALSE(stats.HasCollectedReleases());
    EXPECT_FALSE(stats.HasReleaseCollectPasses());
    EXPECT_FALSE(stats.HasForcedReleaseCollectPasses());
    EXPECT_FALSE(stats.HasReleaseQueueActivity());

    stats.submittedFrameIndex = 42;
    stats.pendingReleaseObjectCount = 3;
    stats.peakPendingReleaseObjectCount = 5;
    stats.queuedReleaseObjectCount = 7;
    stats.collectedReleaseObjectCount = 4;
    stats.releaseCollectPassCount = 6;
    stats.forcedReleaseCollectPassCount = 1;
    stats.releaseCollectLatency = 3;

    EXPECT_TRUE(stats.HasSubmittedFrames());
    EXPECT_TRUE(stats.HasPendingReleases());
    EXPECT_TRUE(stats.HasPeakPendingReleases());
    EXPECT_TRUE(stats.HasQueuedReleases());
    EXPECT_TRUE(stats.HasCollectedReleases());
    EXPECT_TRUE(stats.HasReleaseCollectPasses());
    EXPECT_TRUE(stats.HasForcedReleaseCollectPasses());
    EXPECT_TRUE(stats.HasReleaseQueueActivity());
    EXPECT_EQ(stats.submittedFrameIndex, 42u);
    EXPECT_EQ(stats.pendingReleaseObjectCount, 3u);
    EXPECT_EQ(stats.peakPendingReleaseObjectCount, 5u);
    EXPECT_EQ(stats.queuedReleaseObjectCount, 7u);
    EXPECT_EQ(stats.collectedReleaseObjectCount, 4u);
    EXPECT_EQ(stats.releaseCollectPassCount, 6u);
    EXPECT_EQ(stats.forcedReleaseCollectPassCount, 1u);
    EXPECT_EQ(stats.releaseCollectLatency, 3u);
}

TEST(RendererApiTest, CommandStatsExposeCommandContextSnapshot) {
    RendererCommandStats stats;
    EXPECT_FALSE(stats.recording);
    EXPECT_EQ(stats.queueClass, RHI::QueueClass::Graphics);
    EXPECT_FALSE(stats.HasSubmittedFrames());
    EXPECT_FALSE(stats.HasBeginAttempts());
    EXPECT_FALSE(stats.HasBegunCommandBuffers());
    EXPECT_FALSE(stats.HasRenderPassAttempts());
    EXPECT_FALSE(stats.HasRenderPasses());
    EXPECT_FALSE(stats.HasCommitAttempts());
    EXPECT_FALSE(stats.HasCommittedCommandBuffers());
    EXPECT_FALSE(stats.HasPresentAttempts());
    EXPECT_FALSE(stats.HasPresentedCommandBuffers());
    EXPECT_FALSE(stats.HasFrameGraphPassAttempts());
    EXPECT_FALSE(stats.HasFrameGraphPasses());
    EXPECT_FALSE(stats.HasFrameGraphDependencies());
    EXPECT_FALSE(stats.HasFrameGraphAccesses());
    EXPECT_FALSE(stats.HasFrameGraphTransitions());
    EXPECT_FALSE(stats.HasFrameGraphTransitionSummary());
    EXPECT_FALSE(stats.HasFrameGraphAccessSummary());
    EXPECT_FALSE(stats.HasFrameGraphShaderStageHints());
    EXPECT_FALSE(stats.HasFrameGraphResourceUseAttempts());
    EXPECT_FALSE(stats.HasFrameGraphResourceUses());
    EXPECT_FALSE(stats.HasFrameGraphSkippedResourceUses());
    EXPECT_FALSE(stats.HasFrameGraphStageResourceUses());
    EXPECT_EQ(stats.lastFrameGraphResourceUsePassIndex, 0u);
    EXPECT_EQ(stats.lastFrameGraphResourceUseAccessOffset, 0u);
    EXPECT_EQ(stats.lastFrameGraphResourceUseAccessCount, 0u);
    EXPECT_EQ(stats.lastFrameGraphResourceUseDeclaredCount, 0u);
    EXPECT_EQ(stats.lastFrameGraphResourceUseSkippedCount, 0u);
    EXPECT_EQ(stats.lastFrameGraphDependencyOffset, 0u);
    EXPECT_EQ(stats.lastFrameGraphDependencyCount, 0u);
    EXPECT_FALSE(stats.HasCommandActivity());
    EXPECT_FALSE(stats.HasFailures());

    stats.recording = true;
    stats.queueClass = RHI::QueueClass::Copy;
    stats.submittedFrameIndex = 12;
    stats.beginAttemptCount = 13;
    stats.begunCommandBufferCount = 12;
    stats.beginFailureCount = 1;
    stats.renderPassAttemptCount = 10;
    stats.renderPassBeginCount = 9;
    stats.renderPassFailureCount = 1;
    stats.renderPassEndCount = 9;
    stats.commitAttemptCount = 12;
    stats.committedCommandBufferCount = 11;
    stats.commitFailureCount = 1;
    stats.presentAttemptCount = 9;
    stats.presentedCommandBufferCount = 8;
    stats.presentFailureCount = 1;
    stats.frameGraphPassAttemptCount = 7;
    stats.frameGraphPassEncodedCount = 6;
    stats.frameGraphPassFailureCount = 1;
    stats.frameGraphDependencyEncodedCount = 4;
    stats.frameGraphAccessEncodedCount = 21;
    stats.frameGraphTransitionEncodedCount = 15;
    stats.frameGraphAttachmentTransitionCount = 5;
    stats.frameGraphBufferTransitionCount = 4;
    stats.frameGraphShaderTransitionCount = 3;
    stats.frameGraphCopyTransitionCount = 2;
    stats.frameGraphPresentTransitionCount = 1;
    stats.frameGraphOtherTransitionCount = 6;
    stats.frameGraphAttachmentAccessCount = 5;
    stats.frameGraphBufferAccessCount = 4;
    stats.frameGraphShaderAccessCount = 3;
    stats.frameGraphCopyAccessCount = 2;
    stats.frameGraphPresentAccessCount = 1;
    stats.frameGraphOtherAccessCount = 6;
    stats.frameGraphShaderStageHintAccessCount = 8;
    stats.frameGraphVertexStageHintAccessCount = 4;
    stats.frameGraphFragmentStageHintAccessCount = 3;
    stats.frameGraphComputeStageHintAccessCount = 1;
    stats.frameGraphResourceUseAttemptCount = 8;
    stats.frameGraphResourceUseDeclaredCount = 9;
    stats.frameGraphResourceUseSkippedCount = 2;
    stats.frameGraphResourceUseFailureCount = 1;
    stats.frameGraphBufferUseDeclaredCount = 4;
    stats.frameGraphTextureUseDeclaredCount = 5;
    stats.frameGraphVertexStageUseDeclaredCount = 6;
    stats.frameGraphFragmentStageUseDeclaredCount = 7;
    stats.lastFrameGraphResourceUsePassIndex = 3;
    stats.lastFrameGraphResourceUseAccessOffset = 14;
    stats.lastFrameGraphResourceUseAccessCount = 4;
    stats.lastFrameGraphResourceUseDeclaredCount = 3;
    stats.lastFrameGraphResourceUseSkippedCount = 1;
    stats.lastFrameGraphPassIndex = 5;
    stats.lastFrameGraphDependencyOffset = 9;
    stats.lastFrameGraphDependencyCount = 2;
    stats.lastFrameGraphTransitionOffset = 11;
    stats.lastFrameGraphTransitionCount = 4;
    stats.lastFrameGraphAccessOffset = 17;
    stats.lastFrameGraphAccessCount = 6;
    stats.lastFrameGraphPassQueueClass = RHI::QueueClass::Compute;

    EXPECT_TRUE(stats.recording);
    EXPECT_EQ(stats.queueClass, RHI::QueueClass::Copy);
    EXPECT_TRUE(stats.HasSubmittedFrames());
    EXPECT_TRUE(stats.HasBeginAttempts());
    EXPECT_TRUE(stats.HasBegunCommandBuffers());
    EXPECT_TRUE(stats.HasRenderPassAttempts());
    EXPECT_TRUE(stats.HasRenderPasses());
    EXPECT_TRUE(stats.HasCommitAttempts());
    EXPECT_TRUE(stats.HasCommittedCommandBuffers());
    EXPECT_TRUE(stats.HasPresentAttempts());
    EXPECT_TRUE(stats.HasPresentedCommandBuffers());
    EXPECT_TRUE(stats.HasFrameGraphPassAttempts());
    EXPECT_TRUE(stats.HasFrameGraphPasses());
    EXPECT_TRUE(stats.HasFrameGraphDependencies());
    EXPECT_TRUE(stats.HasFrameGraphAccesses());
    EXPECT_TRUE(stats.HasFrameGraphTransitions());
    EXPECT_TRUE(stats.HasFrameGraphTransitionSummary());
    EXPECT_TRUE(stats.HasFrameGraphAccessSummary());
    EXPECT_TRUE(stats.HasFrameGraphShaderStageHints());
    EXPECT_TRUE(stats.HasFrameGraphResourceUseAttempts());
    EXPECT_TRUE(stats.HasFrameGraphResourceUses());
    EXPECT_TRUE(stats.HasFrameGraphSkippedResourceUses());
    EXPECT_TRUE(stats.HasFrameGraphStageResourceUses());
    EXPECT_TRUE(stats.HasCommandActivity());
    EXPECT_TRUE(stats.HasFailures());
    EXPECT_EQ(stats.submittedFrameIndex, 12u);
    EXPECT_EQ(stats.beginAttemptCount, 13u);
    EXPECT_EQ(stats.begunCommandBufferCount, 12u);
    EXPECT_EQ(stats.beginFailureCount, 1u);
    EXPECT_EQ(stats.renderPassAttemptCount, 10u);
    EXPECT_EQ(stats.renderPassBeginCount, 9u);
    EXPECT_EQ(stats.renderPassFailureCount, 1u);
    EXPECT_EQ(stats.renderPassEndCount, 9u);
    EXPECT_EQ(stats.commitAttemptCount, 12u);
    EXPECT_EQ(stats.committedCommandBufferCount, 11u);
    EXPECT_EQ(stats.commitFailureCount, 1u);
    EXPECT_EQ(stats.presentAttemptCount, 9u);
    EXPECT_EQ(stats.presentedCommandBufferCount, 8u);
    EXPECT_EQ(stats.presentFailureCount, 1u);
    EXPECT_EQ(stats.frameGraphPassAttemptCount, 7u);
    EXPECT_EQ(stats.frameGraphPassEncodedCount, 6u);
    EXPECT_EQ(stats.frameGraphPassFailureCount, 1u);
    EXPECT_EQ(stats.frameGraphDependencyEncodedCount, 4u);
    EXPECT_EQ(stats.frameGraphAccessEncodedCount, 21u);
    EXPECT_EQ(stats.frameGraphTransitionEncodedCount, 15u);
    EXPECT_EQ(stats.frameGraphAttachmentTransitionCount, 5u);
    EXPECT_EQ(stats.frameGraphBufferTransitionCount, 4u);
    EXPECT_EQ(stats.frameGraphShaderTransitionCount, 3u);
    EXPECT_EQ(stats.frameGraphCopyTransitionCount, 2u);
    EXPECT_EQ(stats.frameGraphPresentTransitionCount, 1u);
    EXPECT_EQ(stats.frameGraphOtherTransitionCount, 6u);
    EXPECT_EQ(stats.frameGraphAttachmentAccessCount, 5u);
    EXPECT_EQ(stats.frameGraphBufferAccessCount, 4u);
    EXPECT_EQ(stats.frameGraphShaderAccessCount, 3u);
    EXPECT_EQ(stats.frameGraphCopyAccessCount, 2u);
    EXPECT_EQ(stats.frameGraphPresentAccessCount, 1u);
    EXPECT_EQ(stats.frameGraphOtherAccessCount, 6u);
    EXPECT_EQ(stats.frameGraphShaderStageHintAccessCount, 8u);
    EXPECT_EQ(stats.frameGraphVertexStageHintAccessCount, 4u);
    EXPECT_EQ(stats.frameGraphFragmentStageHintAccessCount, 3u);
    EXPECT_EQ(stats.frameGraphComputeStageHintAccessCount, 1u);
    EXPECT_EQ(stats.frameGraphResourceUseAttemptCount, 8u);
    EXPECT_EQ(stats.frameGraphResourceUseDeclaredCount, 9u);
    EXPECT_EQ(stats.frameGraphResourceUseSkippedCount, 2u);
    EXPECT_EQ(stats.frameGraphResourceUseFailureCount, 1u);
    EXPECT_EQ(stats.frameGraphBufferUseDeclaredCount, 4u);
    EXPECT_EQ(stats.frameGraphTextureUseDeclaredCount, 5u);
    EXPECT_EQ(stats.frameGraphVertexStageUseDeclaredCount, 6u);
    EXPECT_EQ(stats.frameGraphFragmentStageUseDeclaredCount, 7u);
    EXPECT_EQ(stats.lastFrameGraphResourceUsePassIndex, 3u);
    EXPECT_EQ(stats.lastFrameGraphResourceUseAccessOffset, 14u);
    EXPECT_EQ(stats.lastFrameGraphResourceUseAccessCount, 4u);
    EXPECT_EQ(stats.lastFrameGraphResourceUseDeclaredCount, 3u);
    EXPECT_EQ(stats.lastFrameGraphResourceUseSkippedCount, 1u);
    EXPECT_EQ(stats.lastFrameGraphPassIndex, 5u);
    EXPECT_EQ(stats.lastFrameGraphDependencyOffset, 9u);
    EXPECT_EQ(stats.lastFrameGraphDependencyCount, 2u);
    EXPECT_EQ(stats.lastFrameGraphTransitionOffset, 11u);
    EXPECT_EQ(stats.lastFrameGraphTransitionCount, 4u);
    EXPECT_EQ(stats.lastFrameGraphAccessOffset, 17u);
    EXPECT_EQ(stats.lastFrameGraphAccessCount, 6u);
    EXPECT_EQ(stats.lastFrameGraphPassQueueClass, RHI::QueueClass::Compute);
}

TEST(RendererApiTest, RenderPassStatsExposeAttachmentSnapshot) {
    RendererRenderPassStats stats;
    EXPECT_FALSE(stats.ready);
    EXPECT_EQ(stats.colorAttachmentCount, 0u);
    EXPECT_EQ(stats.colorAttachments.size(), kRendererRenderPassColorAttachmentMaxCount);
    EXPECT_EQ(stats.GetColorAttachment(0), nullptr);
    EXPECT_FALSE(stats.colorAttachments[0].active);
    EXPECT_EQ(stats.colorAttachments[0].index, kRendererInvalidBindingIndex);
    EXPECT_EQ(stats.colorAttachments[0].format, RHI::Format::Unknown);
    EXPECT_EQ(stats.colorAttachments[0].loadAction, RHI::AttachmentLoadAction::Clear);
    EXPECT_EQ(stats.colorAttachments[0].storeAction, RHI::AttachmentStoreAction::Store);
    EXPECT_FALSE(stats.colorAttachments[0].resolve);
    EXPECT_FALSE(stats.colorAttachments[0].HasAttachment());
    EXPECT_FALSE(stats.colorAttachments[0].HasFormat());
    EXPECT_FALSE(stats.colorAttachments[0].Clears());
    EXPECT_FALSE(stats.colorAttachments[0].Stores());
    EXPECT_FALSE(stats.colorAttachments[0].HasResolve());
    EXPECT_FALSE(stats.colorAttachments[0].HasReadyAttachment());
    EXPECT_FALSE(stats.hasDepthStencil);
    EXPECT_FALSE(stats.depthStencilAttachment.active);
    EXPECT_EQ(stats.depthStencilAttachment.format, RHI::Format::Unknown);
    EXPECT_FALSE(stats.depthStencilAttachment.HasDepth());
    EXPECT_FALSE(stats.depthStencilAttachment.HasStencil());
    EXPECT_FALSE(stats.depthStencilAttachment.ClearsDepth());
    EXPECT_FALSE(stats.depthStencilAttachment.StoresDepth());
    EXPECT_FALSE(stats.depthStencilAttachment.ClearsStencil());
    EXPECT_FALSE(stats.depthStencilAttachment.StoresStencil());
    EXPECT_FALSE(stats.depthStencilAttachment.HasResolve());
    EXPECT_FALSE(stats.depthStencilAttachment.HasReadyDepthStencil());
    EXPECT_EQ(stats.descriptorError, RHI::RenderPassDescriptorError::MissingAttachment);
    EXPECT_EQ(stats.descriptorErrorAttachmentIndex, 0u);
    EXPECT_EQ(stats.descriptorErrorFormat, RHI::Format::Unknown);
    EXPECT_EQ(stats.frameGraphValidation.error, RHI::FrameGraphDescriptorError::None);
    EXPECT_EQ(stats.frameGraphTransitionCount, 0u);
    EXPECT_STREQ(stats.GetDebugName(), "");
    EXPECT_FALSE(stats.HasDebugName());
    EXPECT_FALSE(stats.HasValidDescriptor());
    EXPECT_FALSE(stats.HasValidFrameGraphPassPlan());
    EXPECT_FALSE(stats.HasFrameGraphTransitions());
    EXPECT_FALSE(stats.HasColorAttachments());
    EXPECT_FALSE(stats.HasMultipleColorAttachments());
    EXPECT_FALSE(stats.HasCompleteColorAttachmentTable());
    EXPECT_FALSE(stats.HasDepthStencilAttachment());
    EXPECT_FALSE(stats.HasAnyClear());
    EXPECT_FALSE(stats.HasAnyStore());
    EXPECT_FALSE(stats.HasAnyResolve());
    EXPECT_FALSE(stats.HasReadyRenderPass());

    stats.ready = true;
    stats.SetDebugName("NEXT swapchain pass");
    stats.descriptorError = RHI::RenderPassDescriptorError::None;
    stats.frameGraphTransitionCount = 3;
    stats.colorAttachmentCount = 2;
    stats.colorAttachments[0].active = true;
    stats.colorAttachments[0].index = 0;
    stats.colorAttachments[0].format = RHI::Format::BGRA8Unorm;
    stats.colorAttachments[0].loadAction = RHI::AttachmentLoadAction::Clear;
    stats.colorAttachments[0].storeAction = RHI::AttachmentStoreAction::Store;
    stats.colorAttachments[0].clearColor = RHI::ClearColor{0.25, 0.5, 0.75, 1.0};
    stats.colorAttachments[1].active = true;
    stats.colorAttachments[1].index = 1;
    stats.colorAttachments[1].format = RHI::Format::RGBA8Unorm;
    stats.colorAttachments[1].loadAction = RHI::AttachmentLoadAction::Load;
    stats.colorAttachments[1].storeAction = RHI::AttachmentStoreAction::DontCare;
    stats.colorAttachments[1].resolve = true;
    stats.hasDepthStencil = true;
    stats.depthStencilAttachment.active = true;
    stats.depthStencilAttachment.format = RHI::Format::Depth32FloatStencil8;
    stats.depthStencilAttachment.loadAction = RHI::AttachmentLoadAction::Clear;
    stats.depthStencilAttachment.storeAction = RHI::AttachmentStoreAction::Store;
    stats.depthStencilAttachment.clearDepth = 0.5;
    stats.depthStencilAttachment.stencilLoadAction = RHI::AttachmentLoadAction::Clear;
    stats.depthStencilAttachment.stencilStoreAction = RHI::AttachmentStoreAction::Store;
    stats.depthStencilAttachment.clearStencil = 7;
    stats.depthStencilAttachment.resolve = true;

    const RendererRenderPassColorAttachmentInfo* firstColor = stats.GetColorAttachment(0);
    const RendererRenderPassColorAttachmentInfo* secondColor = stats.GetColorAttachment(1);
    ASSERT_NE(firstColor, nullptr);
    ASSERT_NE(secondColor, nullptr);
    EXPECT_EQ(stats.GetColorAttachment(2), nullptr);
    EXPECT_TRUE(firstColor->HasAttachment());
    EXPECT_TRUE(firstColor->HasFormat());
    EXPECT_TRUE(firstColor->Clears());
    EXPECT_TRUE(firstColor->Stores());
    EXPECT_FALSE(firstColor->HasResolve());
    EXPECT_TRUE(firstColor->HasReadyAttachment());
    EXPECT_EQ(firstColor->format, RHI::Format::BGRA8Unorm);
    EXPECT_DOUBLE_EQ(firstColor->clearColor.r, 0.25);
    EXPECT_DOUBLE_EQ(firstColor->clearColor.g, 0.5);
    EXPECT_DOUBLE_EQ(firstColor->clearColor.b, 0.75);
    EXPECT_DOUBLE_EQ(firstColor->clearColor.a, 1.0);
    EXPECT_TRUE(secondColor->HasAttachment());
    EXPECT_TRUE(secondColor->HasFormat());
    EXPECT_FALSE(secondColor->Clears());
    EXPECT_FALSE(secondColor->Stores());
    EXPECT_TRUE(secondColor->HasResolve());
    EXPECT_TRUE(secondColor->HasReadyAttachment());
    EXPECT_TRUE(stats.depthStencilAttachment.HasAttachment());
    EXPECT_TRUE(stats.depthStencilAttachment.HasFormat());
    EXPECT_TRUE(stats.depthStencilAttachment.HasDepth());
    EXPECT_TRUE(stats.depthStencilAttachment.HasStencil());
    EXPECT_TRUE(stats.depthStencilAttachment.ClearsDepth());
    EXPECT_TRUE(stats.depthStencilAttachment.StoresDepth());
    EXPECT_TRUE(stats.depthStencilAttachment.ClearsStencil());
    EXPECT_TRUE(stats.depthStencilAttachment.StoresStencil());
    EXPECT_TRUE(stats.depthStencilAttachment.HasResolve());
    EXPECT_TRUE(stats.depthStencilAttachment.HasReadyDepthStencil());
    EXPECT_STREQ(stats.GetDebugName(), "NEXT swapchain pass");
    EXPECT_TRUE(stats.HasDebugName());
    EXPECT_TRUE(stats.HasValidDescriptor());
    EXPECT_TRUE(stats.HasValidFrameGraphPassPlan());
    EXPECT_TRUE(stats.HasFrameGraphTransitions());
    EXPECT_TRUE(stats.HasColorAttachments());
    EXPECT_TRUE(stats.HasMultipleColorAttachments());
    EXPECT_TRUE(stats.HasCompleteColorAttachmentTable());
    EXPECT_TRUE(stats.HasDepthStencilAttachment());
    EXPECT_TRUE(stats.HasAnyClear());
    EXPECT_TRUE(stats.HasAnyStore());
    EXPECT_TRUE(stats.HasAnyResolve());
    EXPECT_TRUE(stats.HasReadyRenderPass());

    const std::string summary = stats.BuildLogSummary();
    EXPECT_NE(summary.find("Renderer render pass stats: ready=true"), std::string::npos);
    EXPECT_NE(summary.find("name=\"NEXT swapchain pass\""), std::string::npos);
    EXPECT_NE(summary.find("descriptor=none descriptorAttachment=0 descriptorFormat=unknown"), std::string::npos);
    EXPECT_NE(summary.find("frameGraph=none transitions=3 colors=2"), std::string::npos);
    EXPECT_NE(summary.find("firstColor=true/bgra8unorm/clear/store/clear=0.25,0.50,0.75,1.00/resolve=false"),
              std::string::npos);
    EXPECT_NE(summary.find("depth=true/depth32float_stencil8/clear/store/clear=0.50 stencil=clear/store/clear=7 resolve=true"),
              std::string::npos);
    EXPECT_NE(summary.find("hasReadyPass=true"), std::string::npos);
}

TEST(RendererApiTest, UploadQueueStatsExposeStagingAndSubmissionSnapshot) {
    RendererUploadQueueStats stats;
    EXPECT_FALSE(stats.ready);
    EXPECT_FALSE(stats.dedicatedQueue);
    EXPECT_EQ(stats.queueClass, RHI::QueueClass::Graphics);
    EXPECT_FALSE(stats.HasPendingUploads());
    EXPECT_FALSE(stats.HasPendingUploadBytes());
    EXPECT_FALSE(stats.HasStagingCapacity());
    EXPECT_FALSE(stats.HasSubmittedUploads());
    EXPECT_FALSE(stats.HasCompletedUploads());
    EXPECT_FALSE(stats.HasRetainedStatuses());
    EXPECT_FALSE(stats.HasRetainedStatusCapacity());
    EXPECT_FALSE(stats.HasFailures());
    EXPECT_EQ(stats.FinishedUploadCount(), 0u);
    EXPECT_FALSE(stats.HasFinishedUploads());
    EXPECT_FALSE(stats.HasLastSubmittedUpload());
    EXPECT_FALSE(stats.HasUploadActivity());

    stats.ready = true;
    stats.dedicatedQueue = true;
    stats.queueClass = RHI::QueueClass::Copy;
    stats.pendingUploadCount = 2;
    stats.pendingUploadBytes = 512;
    stats.stagingCapacityBytes = 4096;
    stats.submittedUploadCount = 8;
    stats.completedUploadCount = 7;
    stats.failedUploadCount = 1;
    stats.lastSubmittedUpload = 8;
    stats.retainedStatusCount = 4;
    stats.retainedStatusCapacity = 128;

    EXPECT_TRUE(stats.ready);
    EXPECT_TRUE(stats.dedicatedQueue);
    EXPECT_EQ(stats.queueClass, RHI::QueueClass::Copy);
    EXPECT_TRUE(stats.HasPendingUploads());
    EXPECT_TRUE(stats.HasPendingUploadBytes());
    EXPECT_TRUE(stats.HasStagingCapacity());
    EXPECT_TRUE(stats.HasSubmittedUploads());
    EXPECT_TRUE(stats.HasCompletedUploads());
    EXPECT_EQ(stats.pendingUploadCount, 2u);
    EXPECT_EQ(stats.pendingUploadBytes, 512u);
    EXPECT_EQ(stats.stagingCapacityBytes, 4096u);
    EXPECT_EQ(stats.submittedUploadCount, 8u);
    EXPECT_EQ(stats.completedUploadCount, 7u);
    EXPECT_EQ(stats.failedUploadCount, 1u);
    EXPECT_EQ(stats.FinishedUploadCount(), 8u);
    EXPECT_TRUE(stats.HasFinishedUploads());
    EXPECT_TRUE(stats.HasFailures());
    EXPECT_EQ(stats.lastSubmittedUpload, 8u);
    EXPECT_TRUE(stats.HasLastSubmittedUpload());
    EXPECT_TRUE(stats.HasRetainedStatuses());
    EXPECT_TRUE(stats.HasRetainedStatusCapacity());
    EXPECT_TRUE(stats.HasUploadActivity());
    EXPECT_EQ(stats.retainedStatusCount, 4u);
    EXPECT_EQ(stats.retainedStatusCapacity, 128u);
}

TEST(RendererApiTest, PipelineStatsExposeCacheSnapshot) {
    RendererPipelineStats stats;
    EXPECT_FALSE(stats.ready);
    EXPECT_FALSE(stats.shaderLibraryReady);
    EXPECT_EQ(stats.shaderLibrarySource, RendererShaderLibrarySource::Unknown);
    EXPECT_EQ(stats.shaderManifestVersion, 0u);
    EXPECT_EQ(stats.shaderRequiredArgumentBufferTier, RHI::ArgumentBufferTier::Unsupported);
    EXPECT_EQ(stats.shaderMaterialShaderResourceGroupArgumentBufferIndex, kRendererInvalidBindingIndex);
    EXPECT_EQ(stats.shaderMaterialShaderResourceGroupUniformArgumentIndex, kRendererInvalidBindingIndex);
    EXPECT_EQ(stats.shaderMaterialShaderResourceGroupTextureArgumentBaseIndex, kRendererInvalidBindingIndex);
    EXPECT_EQ(stats.shaderMaterialShaderResourceGroupSamplerArgumentBaseIndex, kRendererInvalidBindingIndex);
    EXPECT_EQ(stats.pipelineColorFormat, RHI::Format::Unknown);
    EXPECT_EQ(stats.colorAttachmentCount, 0u);
    EXPECT_EQ(stats.colorAttachments.size(), kRendererPipelineColorAttachmentMaxCount);
    EXPECT_EQ(stats.GetColorAttachment(0), nullptr);
    EXPECT_FALSE(stats.colorAttachments[0].active);
    EXPECT_EQ(stats.colorAttachments[0].index, kRendererInvalidBindingIndex);
    EXPECT_EQ(stats.colorAttachments[0].format, RHI::Format::Unknown);
    EXPECT_FALSE(stats.colorAttachments[0].blendEnabled);
    EXPECT_EQ(stats.colorAttachments[0].sourceColorBlendFactor, RHI::BlendFactor::One);
    EXPECT_EQ(stats.colorAttachments[0].destinationColorBlendFactor, RHI::BlendFactor::Zero);
    EXPECT_EQ(stats.colorAttachments[0].colorBlendOperation, RHI::BlendOperation::Add);
    EXPECT_EQ(stats.colorAttachments[0].sourceAlphaBlendFactor, RHI::BlendFactor::One);
    EXPECT_EQ(stats.colorAttachments[0].destinationAlphaBlendFactor, RHI::BlendFactor::Zero);
    EXPECT_EQ(stats.colorAttachments[0].alphaBlendOperation, RHI::BlendOperation::Add);
    EXPECT_EQ(stats.colorAttachments[0].writeMask, RHI::ColorWriteMaskFlag(RHI::ColorWriteMask::All));
    EXPECT_EQ(stats.pipelineDepthStencilFormat, RHI::Format::Unknown);
    EXPECT_EQ(stats.pipelineSampleCount, 0u);
    EXPECT_FALSE(stats.alphaToCoverageEnabled);
    EXPECT_FALSE(stats.rasterStateReady);
    EXPECT_EQ(stats.primitiveTopology, RHI::PrimitiveTopology::TriangleList);
    EXPECT_EQ(stats.fillMode, RHI::FillMode::Solid);
    EXPECT_EQ(stats.cullMode, RHI::CullMode::None);
    EXPECT_EQ(stats.frontFace, RHI::FrontFaceWinding::CounterClockwise);
    EXPECT_EQ(stats.depthBias, 0);
    EXPECT_EQ(stats.depthBiasClamp, 0.0f);
    EXPECT_EQ(stats.depthBiasSlopeScale, 0.0f);
    EXPECT_TRUE(stats.depthClipEnabled);
    EXPECT_FALSE(stats.depthStencilStateReady);
    EXPECT_TRUE(stats.depthTestEnabled);
    EXPECT_TRUE(stats.depthWriteEnabled);
    EXPECT_EQ(stats.depthCompare, RHI::CompareFunction::Less);
    EXPECT_FALSE(stats.stencilTestEnabled);
    EXPECT_EQ(stats.stencilReadMask, 0xffu);
    EXPECT_EQ(stats.stencilWriteMask, 0xffu);
    EXPECT_EQ(stats.frontStencilCompare, RHI::CompareFunction::Always);
    EXPECT_EQ(stats.frontStencilFailOperation, RHI::StencilOperation::Keep);
    EXPECT_EQ(stats.frontStencilDepthFailOperation, RHI::StencilOperation::Keep);
    EXPECT_EQ(stats.frontStencilPassOperation, RHI::StencilOperation::Keep);
    EXPECT_EQ(stats.backStencilCompare, RHI::CompareFunction::Always);
    EXPECT_EQ(stats.backStencilFailOperation, RHI::StencilOperation::Keep);
    EXPECT_EQ(stats.backStencilDepthFailOperation, RHI::StencilOperation::Keep);
    EXPECT_EQ(stats.backStencilPassOperation, RHI::StencilOperation::Keep);
    EXPECT_FALSE(stats.colorBlendStateReady);
    EXPECT_FALSE(stats.colorBlendEnabled);
    EXPECT_EQ(stats.sourceColorBlendFactor, RHI::BlendFactor::One);
    EXPECT_EQ(stats.destinationColorBlendFactor, RHI::BlendFactor::Zero);
    EXPECT_EQ(stats.colorBlendOperation, RHI::BlendOperation::Add);
    EXPECT_EQ(stats.sourceAlphaBlendFactor, RHI::BlendFactor::One);
    EXPECT_EQ(stats.destinationAlphaBlendFactor, RHI::BlendFactor::Zero);
    EXPECT_EQ(stats.alphaBlendOperation, RHI::BlendOperation::Add);
    EXPECT_EQ(stats.colorWriteMask, RHI::ColorWriteMaskFlag(RHI::ColorWriteMask::All));
    EXPECT_EQ(stats.vertexBufferCount, 0u);
    EXPECT_EQ(stats.vertexAttributeCount, 0u);
    EXPECT_EQ(stats.vertexBuffers.size(), kRendererPipelineVertexBufferMaxCount);
    EXPECT_EQ(stats.vertexAttributes.size(), kRendererPipelineVertexAttributeMaxCount);
    EXPECT_EQ(stats.GetVertexBuffer(0), nullptr);
    EXPECT_EQ(stats.GetVertexAttribute(0), nullptr);
    EXPECT_FALSE(stats.vertexBuffers[0].active);
    EXPECT_EQ(stats.vertexBuffers[0].index, kRendererInvalidBindingIndex);
    EXPECT_EQ(stats.vertexBuffers[0].stride, 0u);
    EXPECT_EQ(stats.vertexBuffers[0].stepFunction, RHI::VertexStepFunction::PerVertex);
    EXPECT_EQ(stats.vertexBuffers[0].stepRate, 1u);
    EXPECT_FALSE(stats.vertexAttributes[0].active);
    EXPECT_EQ(stats.vertexAttributes[0].index, kRendererInvalidBindingIndex);
    EXPECT_EQ(stats.vertexAttributes[0].location, 0u);
    EXPECT_EQ(stats.vertexAttributes[0].bufferIndex, kRendererInvalidBindingIndex);
    EXPECT_EQ(stats.vertexAttributes[0].format, RHI::VertexFormat::Unknown);
    EXPECT_EQ(stats.vertexAttributes[0].offset, 0u);
    EXPECT_EQ(stats.primaryVertexBufferIndex, kRendererInvalidBindingIndex);
    EXPECT_EQ(stats.primaryVertexStride, 0u);
    EXPECT_STREQ(stats.GetPipelineDebugName(), "");
    EXPECT_STREQ(stats.GetShaderDebugName(), "");
    EXPECT_STREQ(stats.GetShaderManifestPath(), "");
    EXPECT_STREQ(stats.GetShaderLibraryPath(), "");
    EXPECT_STREQ(stats.GetShaderVertexEntryPoint(), "");
    EXPECT_STREQ(stats.GetShaderFragmentEntryPoint(), "");
    EXPECT_STREQ(stats.GetShaderMaterialLayout(), "");
    EXPECT_STREQ(stats.GetShaderPipelineLayout(), "");
    EXPECT_FALSE(stats.HasShaderLibrary());
    EXPECT_FALSE(stats.HasShaderMaterialLayout());
    EXPECT_FALSE(stats.HasShaderPipelineLayout());
    EXPECT_FALSE(stats.HasPipelineColorFormat());
    EXPECT_FALSE(stats.HasColorAttachments());
    EXPECT_FALSE(stats.HasMultipleColorAttachments());
    EXPECT_FALSE(stats.HasCompleteColorAttachmentTable());
    EXPECT_FALSE(stats.colorAttachments[0].HasAttachment());
    EXPECT_FALSE(stats.colorAttachments[0].HasFormat());
    EXPECT_FALSE(stats.colorAttachments[0].HasBlendState());
    EXPECT_FALSE(stats.colorAttachments[0].HasBlend());
    EXPECT_FALSE(stats.colorAttachments[0].HasColorWriteMask());
    EXPECT_FALSE(stats.colorAttachments[0].WritesAllColorChannels());
    EXPECT_FALSE(stats.HasPipelineDepthStencilFormat());
    EXPECT_FALSE(stats.HasPipelineSampleCount());
    EXPECT_FALSE(stats.HasAlphaToCoverage());
    EXPECT_FALSE(stats.HasRasterState());
    EXPECT_FALSE(stats.HasTriangleTopology());
    EXPECT_FALSE(stats.HasDepthBias());
    EXPECT_FALSE(stats.HasDepthClipState());
    EXPECT_FALSE(stats.HasDepthStencilState());
    EXPECT_FALSE(stats.HasDepthTesting());
    EXPECT_FALSE(stats.HasDepthWriting());
    EXPECT_FALSE(stats.HasStencilTesting());
    EXPECT_FALSE(stats.HasStencilMasks());
    EXPECT_FALSE(stats.HasStencilFaceState());
    EXPECT_FALSE(stats.HasActiveStencilFaceState());
    EXPECT_FALSE(stats.HasNonDefaultStencilFaceState());
    EXPECT_FALSE(stats.HasColorBlendState());
    EXPECT_FALSE(stats.HasColorBlend());
    EXPECT_FALSE(stats.HasColorWriteMask());
    EXPECT_FALSE(stats.WritesAllColorChannels());
    EXPECT_FALSE(stats.HasVertexBuffers());
    EXPECT_FALSE(stats.HasVertexAttributes());
    EXPECT_FALSE(stats.HasMultipleVertexBuffers());
    EXPECT_FALSE(stats.HasMultipleVertexAttributes());
    EXPECT_FALSE(stats.HasCompleteVertexBufferTable());
    EXPECT_FALSE(stats.HasCompleteVertexAttributeTable());
    EXPECT_FALSE(stats.vertexBuffers[0].HasBuffer());
    EXPECT_FALSE(stats.vertexBuffers[0].HasStride());
    EXPECT_FALSE(stats.vertexBuffers[0].HasStepRate());
    EXPECT_FALSE(stats.vertexBuffers[0].HasPerInstanceStep());
    EXPECT_FALSE(stats.vertexBuffers[0].HasReadyBufferLayout());
    EXPECT_FALSE(stats.vertexAttributes[0].HasAttribute());
    EXPECT_FALSE(stats.vertexAttributes[0].HasBufferReference());
    EXPECT_FALSE(stats.vertexAttributes[0].HasFormat());
    EXPECT_FALSE(stats.vertexAttributes[0].HasOffset());
    EXPECT_FALSE(stats.vertexAttributes[0].HasReadyAttributeLayout());
    EXPECT_FALSE(stats.HasPrimaryVertexBuffer());
    EXPECT_FALSE(stats.HasPrimaryVertexStride());
    EXPECT_FALSE(stats.HasVertexInputLayout());
    EXPECT_FALSE(stats.HasDetailedVertexInputLayout());
    EXPECT_FALSE(stats.HasPipelineDebugName());
    EXPECT_FALSE(stats.HasShaderManifestVersion());
    EXPECT_FALSE(stats.HasShaderRequiredArgumentBufferTier());
    EXPECT_FALSE(stats.HasShaderMaterialShaderResourceGroupArgumentBufferIndex());
    EXPECT_FALSE(stats.HasShaderMaterialShaderResourceGroupArgumentLayout());
    EXPECT_FALSE(stats.HasShaderDebugName());
    EXPECT_FALSE(stats.HasShaderManifest());
    EXPECT_FALSE(stats.HasShaderLibraryPath());
    EXPECT_FALSE(stats.HasShaderEntryPoints());
    EXPECT_FALSE(stats.HasCompleteShaderDescriptor());
    EXPECT_FALSE(stats.HasCachedPipelines());
    EXPECT_FALSE(stats.HasPipelineRequests());
    EXPECT_FALSE(stats.HasCacheHits());
    EXPECT_FALSE(stats.HasCacheMisses());
    EXPECT_FALSE(stats.HasCreateFailures());
    EXPECT_FALSE(stats.HasPipelineCacheActivity());
    RendererSwapchainStats emptySwapchain;
    EXPECT_FALSE(RendererPipelineFormatsMatchSwapchain(stats, emptySwapchain));

    stats.ready = true;
    stats.shaderLibraryReady = true;
    stats.shaderLibrarySource = RendererShaderLibrarySource::Source;
    stats.shaderManifestVersion = 1;
    stats.shaderRequiredArgumentBufferTier = RHI::ArgumentBufferTier::Tier1;
    stats.shaderMaterialShaderResourceGroupArgumentBufferIndex =
        kRendererMaterialShaderResourceGroupArgumentBufferIndex;
    stats.shaderMaterialShaderResourceGroupUniformArgumentIndex = 0;
    stats.shaderMaterialShaderResourceGroupTextureArgumentBaseIndex = 1;
    stats.shaderMaterialShaderResourceGroupSamplerArgumentBaseIndex = 6;
    stats.pipelineColorFormat = RHI::Format::BGRA8Unorm;
    stats.colorAttachmentCount = 2;
    stats.colorAttachments[0].active = true;
    stats.colorAttachments[0].index = 0;
    stats.colorAttachments[0].format = RHI::Format::BGRA8Unorm;
    stats.colorAttachments[0].blendEnabled = true;
    stats.colorAttachments[0].sourceColorBlendFactor = RHI::BlendFactor::SourceAlpha;
    stats.colorAttachments[0].destinationColorBlendFactor = RHI::BlendFactor::OneMinusSourceAlpha;
    stats.colorAttachments[0].colorBlendOperation = RHI::BlendOperation::Add;
    stats.colorAttachments[0].sourceAlphaBlendFactor = RHI::BlendFactor::One;
    stats.colorAttachments[0].destinationAlphaBlendFactor = RHI::BlendFactor::Zero;
    stats.colorAttachments[0].alphaBlendOperation = RHI::BlendOperation::Max;
    stats.colorAttachments[0].writeMask = RHI::ColorWriteMask::Red | RHI::ColorWriteMask::Green;
    stats.colorAttachments[1].active = true;
    stats.colorAttachments[1].index = 1;
    stats.colorAttachments[1].format = RHI::Format::RGBA8Unorm;
    stats.colorAttachments[1].blendEnabled = false;
    stats.colorAttachments[1].writeMask = RHI::ColorWriteMaskFlag(RHI::ColorWriteMask::All);
    stats.pipelineDepthStencilFormat = RHI::Format::Depth32Float;
    stats.pipelineSampleCount = 1;
    stats.alphaToCoverageEnabled = true;
    stats.rasterStateReady = true;
    stats.primitiveTopology = RHI::PrimitiveTopology::TriangleList;
    stats.fillMode = RHI::FillMode::Wireframe;
    stats.cullMode = RHI::CullMode::Back;
    stats.frontFace = RHI::FrontFaceWinding::Clockwise;
    stats.depthBias = 2;
    stats.depthBiasClamp = 0.5f;
    stats.depthBiasSlopeScale = 1.25f;
    stats.depthClipEnabled = false;
    stats.depthStencilStateReady = true;
    stats.depthTestEnabled = true;
    stats.depthWriteEnabled = true;
    stats.depthCompare = RHI::CompareFunction::LessEqual;
    stats.stencilTestEnabled = true;
    stats.stencilReadMask = 0x0f;
    stats.stencilWriteMask = 0xf0;
    stats.frontStencilCompare = RHI::CompareFunction::Equal;
    stats.frontStencilFailOperation = RHI::StencilOperation::Replace;
    stats.frontStencilDepthFailOperation = RHI::StencilOperation::IncrementClamp;
    stats.frontStencilPassOperation = RHI::StencilOperation::IncrementWrap;
    stats.backStencilCompare = RHI::CompareFunction::NotEqual;
    stats.backStencilFailOperation = RHI::StencilOperation::Zero;
    stats.backStencilDepthFailOperation = RHI::StencilOperation::DecrementClamp;
    stats.backStencilPassOperation = RHI::StencilOperation::DecrementWrap;
    stats.colorBlendStateReady = true;
    stats.colorBlendEnabled = true;
    stats.sourceColorBlendFactor = RHI::BlendFactor::SourceAlpha;
    stats.destinationColorBlendFactor = RHI::BlendFactor::OneMinusSourceAlpha;
    stats.colorBlendOperation = RHI::BlendOperation::Add;
    stats.sourceAlphaBlendFactor = RHI::BlendFactor::One;
    stats.destinationAlphaBlendFactor = RHI::BlendFactor::Zero;
    stats.alphaBlendOperation = RHI::BlendOperation::Max;
    stats.colorWriteMask = RHI::ColorWriteMask::Red | RHI::ColorWriteMask::Green;
    stats.vertexBufferCount = 2;
    stats.vertexAttributeCount = 4;
    stats.vertexBuffers[0].active = true;
    stats.vertexBuffers[0].index = 0;
    stats.vertexBuffers[0].stride = 44;
    stats.vertexBuffers[0].stepFunction = RHI::VertexStepFunction::PerVertex;
    stats.vertexBuffers[0].stepRate = 1;
    stats.vertexBuffers[1].active = true;
    stats.vertexBuffers[1].index = 1;
    stats.vertexBuffers[1].stride = 16;
    stats.vertexBuffers[1].stepFunction = RHI::VertexStepFunction::PerInstance;
    stats.vertexBuffers[1].stepRate = 2;
    stats.vertexAttributes[0].active = true;
    stats.vertexAttributes[0].index = 0;
    stats.vertexAttributes[0].location = 0;
    stats.vertexAttributes[0].bufferIndex = 0;
    stats.vertexAttributes[0].format = RHI::VertexFormat::Float32x3;
    stats.vertexAttributes[0].offset = 0;
    stats.vertexAttributes[1].active = true;
    stats.vertexAttributes[1].index = 1;
    stats.vertexAttributes[1].location = 1;
    stats.vertexAttributes[1].bufferIndex = 0;
    stats.vertexAttributes[1].format = RHI::VertexFormat::Float32x3;
    stats.vertexAttributes[1].offset = 12;
    stats.vertexAttributes[2].active = true;
    stats.vertexAttributes[2].index = 2;
    stats.vertexAttributes[2].location = 2;
    stats.vertexAttributes[2].bufferIndex = 0;
    stats.vertexAttributes[2].format = RHI::VertexFormat::Float32x2;
    stats.vertexAttributes[2].offset = 24;
    stats.vertexAttributes[3].active = true;
    stats.vertexAttributes[3].index = 3;
    stats.vertexAttributes[3].location = 7;
    stats.vertexAttributes[3].bufferIndex = 1;
    stats.vertexAttributes[3].format = RHI::VertexFormat::Float32x4;
    stats.vertexAttributes[3].offset = 0;
    stats.primaryVertexBufferIndex = kRendererGeometryVertexBufferIndex;
    stats.primaryVertexStride = 44;
    stats.SetPipelineDebugName("NEXT demo forward pipeline");
    stats.SetShaderDebugName("NEXT demo forward shader");
    stats.SetShaderManifestPath("engine/renderer/shaders/metal/demo_forward.shader_manifest");
    stats.SetShaderLibraryPath("engine/renderer/shaders/metal/demo_forward.metal");
    stats.SetShaderEntryPoints("vertex_main", "fragment_main_material_srg");
    stats.SetShaderMaterialLayout("material_srg_v1");
    stats.SetShaderPipelineLayout("demo_forward_pipeline_v1");
    stats.cachedPipelineCount = 2;
    stats.requestCount = 5;
    stats.hitCount = 3;
    stats.missCount = 2;
    stats.failedCreateCount = 1;

    EXPECT_TRUE(stats.ready);
    EXPECT_TRUE(stats.HasShaderLibrary());
    EXPECT_EQ(stats.shaderLibrarySource, RendererShaderLibrarySource::Source);
    EXPECT_EQ(stats.shaderManifestVersion, 1u);
    EXPECT_EQ(stats.shaderRequiredArgumentBufferTier, RHI::ArgumentBufferTier::Tier1);
    EXPECT_EQ(stats.shaderMaterialShaderResourceGroupArgumentBufferIndex,
              kRendererMaterialShaderResourceGroupArgumentBufferIndex);
    EXPECT_EQ(stats.shaderMaterialShaderResourceGroupUniformArgumentIndex, 0u);
    EXPECT_EQ(stats.shaderMaterialShaderResourceGroupTextureArgumentBaseIndex, 1u);
    EXPECT_EQ(stats.shaderMaterialShaderResourceGroupSamplerArgumentBaseIndex, 6u);
    EXPECT_STREQ(RendererShaderLibrarySourceName(stats.shaderLibrarySource), "source");
    EXPECT_EQ(stats.pipelineColorFormat, RHI::Format::BGRA8Unorm);
    EXPECT_EQ(stats.colorAttachmentCount, 2u);
    const RendererPipelineColorAttachmentInfo* firstColorAttachment = stats.GetColorAttachment(0);
    const RendererPipelineColorAttachmentInfo* secondColorAttachment = stats.GetColorAttachment(1);
    EXPECT_NE(firstColorAttachment, nullptr);
    EXPECT_NE(secondColorAttachment, nullptr);
    ASSERT_NE(firstColorAttachment, nullptr);
    ASSERT_NE(secondColorAttachment, nullptr);
    EXPECT_TRUE(firstColorAttachment->active);
    EXPECT_EQ(firstColorAttachment->index, 0u);
    EXPECT_EQ(firstColorAttachment->format, RHI::Format::BGRA8Unorm);
    EXPECT_TRUE(firstColorAttachment->blendEnabled);
    EXPECT_EQ(firstColorAttachment->sourceColorBlendFactor, RHI::BlendFactor::SourceAlpha);
    EXPECT_EQ(firstColorAttachment->destinationColorBlendFactor, RHI::BlendFactor::OneMinusSourceAlpha);
    EXPECT_EQ(firstColorAttachment->colorBlendOperation, RHI::BlendOperation::Add);
    EXPECT_EQ(firstColorAttachment->sourceAlphaBlendFactor, RHI::BlendFactor::One);
    EXPECT_EQ(firstColorAttachment->destinationAlphaBlendFactor, RHI::BlendFactor::Zero);
    EXPECT_EQ(firstColorAttachment->alphaBlendOperation, RHI::BlendOperation::Max);
    EXPECT_EQ(firstColorAttachment->writeMask, RHI::ColorWriteMask::Red | RHI::ColorWriteMask::Green);
    EXPECT_TRUE(firstColorAttachment->HasAttachment());
    EXPECT_TRUE(firstColorAttachment->HasFormat());
    EXPECT_TRUE(firstColorAttachment->HasBlendState());
    EXPECT_TRUE(firstColorAttachment->HasBlend());
    EXPECT_TRUE(firstColorAttachment->HasColorWriteMask());
    EXPECT_FALSE(firstColorAttachment->WritesAllColorChannels());
    EXPECT_TRUE(secondColorAttachment->active);
    EXPECT_EQ(secondColorAttachment->index, 1u);
    EXPECT_EQ(secondColorAttachment->format, RHI::Format::RGBA8Unorm);
    EXPECT_FALSE(secondColorAttachment->blendEnabled);
    EXPECT_EQ(secondColorAttachment->writeMask, RHI::ColorWriteMaskFlag(RHI::ColorWriteMask::All));
    EXPECT_TRUE(secondColorAttachment->HasAttachment());
    EXPECT_TRUE(secondColorAttachment->HasFormat());
    EXPECT_TRUE(secondColorAttachment->HasBlendState());
    EXPECT_FALSE(secondColorAttachment->HasBlend());
    EXPECT_TRUE(secondColorAttachment->HasColorWriteMask());
    EXPECT_TRUE(secondColorAttachment->WritesAllColorChannels());
    EXPECT_EQ(stats.GetColorAttachment(2), nullptr);
    EXPECT_EQ(stats.pipelineDepthStencilFormat, RHI::Format::Depth32Float);
    EXPECT_EQ(stats.pipelineSampleCount, 1u);
    EXPECT_TRUE(stats.alphaToCoverageEnabled);
    EXPECT_TRUE(stats.rasterStateReady);
    EXPECT_EQ(stats.primitiveTopology, RHI::PrimitiveTopology::TriangleList);
    EXPECT_EQ(stats.fillMode, RHI::FillMode::Wireframe);
    EXPECT_EQ(stats.cullMode, RHI::CullMode::Back);
    EXPECT_EQ(stats.frontFace, RHI::FrontFaceWinding::Clockwise);
    EXPECT_EQ(stats.depthBias, 2);
    EXPECT_EQ(stats.depthBiasClamp, 0.5f);
    EXPECT_EQ(stats.depthBiasSlopeScale, 1.25f);
    EXPECT_FALSE(stats.depthClipEnabled);
    EXPECT_TRUE(stats.depthStencilStateReady);
    EXPECT_TRUE(stats.depthTestEnabled);
    EXPECT_TRUE(stats.depthWriteEnabled);
    EXPECT_EQ(stats.depthCompare, RHI::CompareFunction::LessEqual);
    EXPECT_TRUE(stats.stencilTestEnabled);
    EXPECT_EQ(stats.stencilReadMask, 0x0fu);
    EXPECT_EQ(stats.stencilWriteMask, 0xf0u);
    EXPECT_EQ(stats.frontStencilCompare, RHI::CompareFunction::Equal);
    EXPECT_EQ(stats.frontStencilFailOperation, RHI::StencilOperation::Replace);
    EXPECT_EQ(stats.frontStencilDepthFailOperation, RHI::StencilOperation::IncrementClamp);
    EXPECT_EQ(stats.frontStencilPassOperation, RHI::StencilOperation::IncrementWrap);
    EXPECT_EQ(stats.backStencilCompare, RHI::CompareFunction::NotEqual);
    EXPECT_EQ(stats.backStencilFailOperation, RHI::StencilOperation::Zero);
    EXPECT_EQ(stats.backStencilDepthFailOperation, RHI::StencilOperation::DecrementClamp);
    EXPECT_EQ(stats.backStencilPassOperation, RHI::StencilOperation::DecrementWrap);
    EXPECT_TRUE(stats.colorBlendStateReady);
    EXPECT_TRUE(stats.colorBlendEnabled);
    EXPECT_EQ(stats.sourceColorBlendFactor, RHI::BlendFactor::SourceAlpha);
    EXPECT_EQ(stats.destinationColorBlendFactor, RHI::BlendFactor::OneMinusSourceAlpha);
    EXPECT_EQ(stats.colorBlendOperation, RHI::BlendOperation::Add);
    EXPECT_EQ(stats.sourceAlphaBlendFactor, RHI::BlendFactor::One);
    EXPECT_EQ(stats.destinationAlphaBlendFactor, RHI::BlendFactor::Zero);
    EXPECT_EQ(stats.alphaBlendOperation, RHI::BlendOperation::Max);
    EXPECT_EQ(stats.colorWriteMask, RHI::ColorWriteMask::Red | RHI::ColorWriteMask::Green);
    EXPECT_EQ(stats.vertexBufferCount, 2u);
    EXPECT_EQ(stats.vertexAttributeCount, 4u);
    const RendererPipelineVertexBufferInfo* firstVertexBuffer = stats.GetVertexBuffer(0);
    const RendererPipelineVertexBufferInfo* secondVertexBuffer = stats.GetVertexBuffer(1);
    const RendererPipelineVertexAttributeInfo* firstVertexAttribute = stats.GetVertexAttribute(0);
    const RendererPipelineVertexAttributeInfo* lastVertexAttribute = stats.GetVertexAttribute(3);
    ASSERT_NE(firstVertexBuffer, nullptr);
    ASSERT_NE(secondVertexBuffer, nullptr);
    ASSERT_NE(firstVertexAttribute, nullptr);
    ASSERT_NE(lastVertexAttribute, nullptr);
    EXPECT_TRUE(firstVertexBuffer->active);
    EXPECT_EQ(firstVertexBuffer->index, 0u);
    EXPECT_EQ(firstVertexBuffer->stride, 44u);
    EXPECT_EQ(firstVertexBuffer->stepFunction, RHI::VertexStepFunction::PerVertex);
    EXPECT_EQ(firstVertexBuffer->stepRate, 1u);
    EXPECT_TRUE(firstVertexBuffer->HasBuffer());
    EXPECT_TRUE(firstVertexBuffer->HasStride());
    EXPECT_TRUE(firstVertexBuffer->HasStepRate());
    EXPECT_FALSE(firstVertexBuffer->HasPerInstanceStep());
    EXPECT_TRUE(firstVertexBuffer->HasReadyBufferLayout());
    EXPECT_TRUE(secondVertexBuffer->active);
    EXPECT_EQ(secondVertexBuffer->index, 1u);
    EXPECT_EQ(secondVertexBuffer->stride, 16u);
    EXPECT_EQ(secondVertexBuffer->stepFunction, RHI::VertexStepFunction::PerInstance);
    EXPECT_EQ(secondVertexBuffer->stepRate, 2u);
    EXPECT_TRUE(secondVertexBuffer->HasPerInstanceStep());
    EXPECT_TRUE(secondVertexBuffer->HasReadyBufferLayout());
    EXPECT_TRUE(firstVertexAttribute->active);
    EXPECT_EQ(firstVertexAttribute->index, 0u);
    EXPECT_EQ(firstVertexAttribute->location, 0u);
    EXPECT_EQ(firstVertexAttribute->bufferIndex, 0u);
    EXPECT_EQ(firstVertexAttribute->format, RHI::VertexFormat::Float32x3);
    EXPECT_EQ(firstVertexAttribute->offset, 0u);
    EXPECT_TRUE(firstVertexAttribute->HasAttribute());
    EXPECT_TRUE(firstVertexAttribute->HasBufferReference());
    EXPECT_TRUE(firstVertexAttribute->HasFormat());
    EXPECT_FALSE(firstVertexAttribute->HasOffset());
    EXPECT_TRUE(firstVertexAttribute->HasReadyAttributeLayout());
    EXPECT_TRUE(lastVertexAttribute->active);
    EXPECT_EQ(lastVertexAttribute->index, 3u);
    EXPECT_EQ(lastVertexAttribute->location, 7u);
    EXPECT_EQ(lastVertexAttribute->bufferIndex, 1u);
    EXPECT_EQ(lastVertexAttribute->format, RHI::VertexFormat::Float32x4);
    EXPECT_EQ(lastVertexAttribute->offset, 0u);
    EXPECT_EQ(stats.GetVertexBuffer(2), nullptr);
    EXPECT_EQ(stats.GetVertexAttribute(4), nullptr);
    EXPECT_EQ(stats.primaryVertexBufferIndex, kRendererGeometryVertexBufferIndex);
    EXPECT_EQ(stats.primaryVertexStride, 44u);
    EXPECT_TRUE(stats.HasPipelineColorFormat());
    EXPECT_TRUE(stats.HasColorAttachments());
    EXPECT_TRUE(stats.HasMultipleColorAttachments());
    EXPECT_TRUE(stats.HasCompleteColorAttachmentTable());
    EXPECT_TRUE(stats.HasPipelineDepthStencilFormat());
    EXPECT_TRUE(stats.HasPipelineSampleCount());
    EXPECT_TRUE(stats.HasAlphaToCoverage());
    EXPECT_TRUE(stats.HasRasterState());
    EXPECT_TRUE(stats.HasTriangleTopology());
    EXPECT_TRUE(stats.HasDepthBias());
    EXPECT_TRUE(stats.HasDepthClipState());
    EXPECT_TRUE(stats.HasDepthStencilState());
    EXPECT_TRUE(stats.HasDepthTesting());
    EXPECT_TRUE(stats.HasDepthWriting());
    EXPECT_TRUE(stats.HasStencilTesting());
    EXPECT_TRUE(stats.HasStencilMasks());
    EXPECT_TRUE(stats.HasStencilFaceState());
    EXPECT_TRUE(stats.HasActiveStencilFaceState());
    EXPECT_TRUE(stats.HasNonDefaultStencilFaceState());
    EXPECT_TRUE(stats.HasColorBlendState());
    EXPECT_TRUE(stats.HasColorBlend());
    EXPECT_TRUE(stats.HasColorWriteMask());
    EXPECT_FALSE(stats.WritesAllColorChannels());
    stats.colorWriteMask = RHI::ColorWriteMaskFlag(RHI::ColorWriteMask::All);
    EXPECT_TRUE(stats.WritesAllColorChannels());
    EXPECT_TRUE(stats.HasVertexBuffers());
    EXPECT_TRUE(stats.HasVertexAttributes());
    EXPECT_TRUE(stats.HasMultipleVertexBuffers());
    EXPECT_TRUE(stats.HasMultipleVertexAttributes());
    EXPECT_TRUE(stats.HasCompleteVertexBufferTable());
    EXPECT_TRUE(stats.HasCompleteVertexAttributeTable());
    EXPECT_TRUE(stats.HasPrimaryVertexBuffer());
    EXPECT_TRUE(stats.HasPrimaryVertexStride());
    EXPECT_TRUE(stats.HasVertexInputLayout());
    EXPECT_TRUE(stats.HasDetailedVertexInputLayout());
    RendererSwapchainStats matchingSwapchain;
    matchingSwapchain.colorFormat = RHI::Format::BGRA8Unorm;
    matchingSwapchain.depthFormat = RHI::Format::Depth32Float;
    EXPECT_TRUE(RendererPipelineFormatsMatchSwapchain(stats, matchingSwapchain));
    matchingSwapchain.colorFormat = RHI::Format::RGBA8Unorm;
    EXPECT_FALSE(RendererPipelineFormatsMatchSwapchain(stats, matchingSwapchain));
    matchingSwapchain.colorFormat = RHI::Format::BGRA8Unorm;
    matchingSwapchain.depthFormat = RHI::Format::Depth32FloatStencil8;
    EXPECT_FALSE(RendererPipelineFormatsMatchSwapchain(stats, matchingSwapchain));
    EXPECT_STREQ(stats.GetPipelineDebugName(), "NEXT demo forward pipeline");
    EXPECT_TRUE(stats.HasPipelineDebugName());
    EXPECT_STREQ(stats.GetShaderDebugName(), "NEXT demo forward shader");
    EXPECT_TRUE(stats.HasShaderDebugName());
    EXPECT_STREQ(stats.GetShaderManifestPath(), "engine/renderer/shaders/metal/demo_forward.shader_manifest");
    EXPECT_TRUE(stats.HasShaderManifestVersion());
    EXPECT_TRUE(stats.HasShaderManifest());
    EXPECT_STREQ(stats.GetShaderLibraryPath(), "engine/renderer/shaders/metal/demo_forward.metal");
    EXPECT_TRUE(stats.HasShaderLibraryPath());
    EXPECT_STREQ(stats.GetShaderVertexEntryPoint(), "vertex_main");
    EXPECT_STREQ(stats.GetShaderFragmentEntryPoint(), "fragment_main_material_srg");
    EXPECT_STREQ(stats.GetShaderMaterialLayout(), "material_srg_v1");
    EXPECT_STREQ(stats.GetShaderPipelineLayout(), "demo_forward_pipeline_v1");
    EXPECT_TRUE(stats.HasShaderRequiredArgumentBufferTier());
    EXPECT_TRUE(stats.HasShaderMaterialShaderResourceGroupArgumentBufferIndex());
    EXPECT_TRUE(stats.HasShaderMaterialShaderResourceGroupArgumentLayout());
    EXPECT_TRUE(stats.HasShaderMaterialLayout());
    EXPECT_TRUE(stats.HasShaderPipelineLayout());
    EXPECT_TRUE(stats.HasShaderEntryPoints());
    EXPECT_TRUE(stats.HasCompleteShaderDescriptor());
    EXPECT_TRUE(stats.HasCachedPipelines());
    EXPECT_TRUE(stats.HasPipelineRequests());
    EXPECT_TRUE(stats.HasCacheHits());
    EXPECT_TRUE(stats.HasCacheMisses());
    EXPECT_TRUE(stats.HasCreateFailures());
    EXPECT_TRUE(stats.HasPipelineCacheActivity());
    EXPECT_EQ(stats.cachedPipelineCount, 2u);
    EXPECT_EQ(stats.requestCount, 5u);
    EXPECT_EQ(stats.hitCount, 3u);
    EXPECT_EQ(stats.missCount, 2u);
    EXPECT_EQ(stats.failedCreateCount, 1u);
}

TEST(RendererApiTest, GeometryStatsExposeIndexedDrawSnapshot) {
    RendererGeometryStats stats;
    EXPECT_FALSE(stats.ready);
    EXPECT_FALSE(stats.vertexBufferReady);
    EXPECT_FALSE(stats.indexBufferReady);
    EXPECT_EQ(stats.vertexBufferIndex, kRendererGeometryVertexBufferIndex);
    EXPECT_EQ(stats.vertexStride, 0u);
    EXPECT_EQ(stats.vertexBufferBytes, 0u);
    EXPECT_EQ(stats.indexBufferBytes, 0u);
    EXPECT_EQ(stats.indexFormat, RHI::IndexFormat::Unknown);
    EXPECT_EQ(stats.indexBufferByteOffset, 0u);
    EXPECT_EQ(stats.resolvedIndexBufferByteOffset, 0u);
    EXPECT_EQ(stats.indexCount, 0u);
    EXPECT_EQ(stats.instanceCount, 0u);
    EXPECT_EQ(stats.indexOffset, 0u);
    EXPECT_EQ(stats.vertexOffset, 0);
    EXPECT_EQ(stats.instanceOffset, 0u);
    EXPECT_EQ(stats.stencilReference, 0u);
    EXPECT_FALSE(stats.HasVertexBuffer());
    EXPECT_FALSE(stats.HasIndexBuffer());
    EXPECT_FALSE(stats.HasVertexStride());
    EXPECT_FALSE(stats.HasIndexFormat());
    EXPECT_FALSE(stats.HasIndexCount());
    EXPECT_FALSE(stats.HasInstances());
    EXPECT_FALSE(stats.HasResolvedIndexBufferOffset());
    EXPECT_FALSE(stats.HasBlendConstant());
    EXPECT_FALSE(stats.HasIndexedDraw());
    EXPECT_FALSE(stats.HasReadyGeometry());

    stats.ready = true;
    stats.vertexBufferReady = true;
    stats.indexBufferReady = true;
    stats.vertexStride = 44;
    stats.vertexBufferBytes = 1056;
    stats.indexBufferBytes = 72;
    stats.indexFormat = RHI::IndexFormat::Uint16;
    stats.indexBufferByteOffset = 4;
    stats.resolvedIndexBufferByteOffset = 12;
    stats.indexCount = 36;
    stats.instanceCount = 2;
    stats.indexOffset = 4;
    stats.vertexOffset = -1;
    stats.instanceOffset = 3;
    stats.stencilReference = 7;
    stats.blendConstant = {0.25f, 0.0f, 0.5f, 1.0f};

    EXPECT_TRUE(stats.HasVertexBuffer());
    EXPECT_TRUE(stats.HasIndexBuffer());
    EXPECT_TRUE(stats.HasVertexStride());
    EXPECT_TRUE(stats.HasIndexFormat());
    EXPECT_TRUE(stats.HasIndexCount());
    EXPECT_TRUE(stats.HasInstances());
    EXPECT_TRUE(stats.HasResolvedIndexBufferOffset());
    EXPECT_TRUE(stats.HasBlendConstant());
    EXPECT_TRUE(stats.HasIndexedDraw());
    EXPECT_TRUE(stats.HasReadyGeometry());
}

TEST(RendererApiTest, DrawStateStatsExposeViewportScissorSnapshot) {
    RendererDrawStateStats stats;
    EXPECT_FALSE(stats.ready);
    EXPECT_EQ(stats.drawableSize.width, 0u);
    EXPECT_EQ(stats.drawableSize.height, 0u);
    EXPECT_EQ(stats.viewport.minX, 0.0);
    EXPECT_EQ(stats.viewport.minY, 0.0);
    EXPECT_EQ(stats.viewport.maxX, 0.0);
    EXPECT_EQ(stats.viewport.maxY, 0.0);
    EXPECT_EQ(stats.viewport.minZ, 0.0);
    EXPECT_EQ(stats.viewport.maxZ, 1.0);
    EXPECT_EQ(stats.scissor.minX, 0u);
    EXPECT_EQ(stats.scissor.minY, 0u);
    EXPECT_EQ(stats.scissor.maxX, 0u);
    EXPECT_EQ(stats.scissor.maxY, 0u);
    EXPECT_EQ(stats.viewportError, RHI::ViewportDescriptorError::EmptyViewport);
    EXPECT_EQ(stats.scissorError, RHI::ScissorDescriptorError::EmptyScissor);
    EXPECT_FALSE(stats.HasDrawableSize());
    EXPECT_FALSE(stats.HasViewport());
    EXPECT_FALSE(stats.HasScissor());
    EXPECT_TRUE(stats.HasViewportDepthRange());
    EXPECT_FALSE(stats.HasValidViewport());
    EXPECT_FALSE(stats.HasValidScissor());
    EXPECT_FALSE(stats.HasValidDrawState());
    EXPECT_FALSE(stats.HasReadyDrawState());
    EXPECT_FALSE(stats.ViewportMatchesDrawable());
    EXPECT_FALSE(stats.ScissorMatchesDrawable());
    EXPECT_FALSE(stats.HasFullDrawableDrawState());

    stats.ready = true;
    stats.drawableSize = RHI::Extent2D{1280, 720};
    stats.viewport = RHI::ViewportDesc{0.0, 0.0, 1280.0, 720.0, 0.0, 1.0};
    stats.scissor = RHI::ScissorRectDesc{0, 0, 1280, 720};
    stats.viewportError = RHI::ViewportDescriptorError::None;
    stats.scissorError = RHI::ScissorDescriptorError::None;

    EXPECT_TRUE(stats.HasDrawableSize());
    EXPECT_TRUE(stats.HasViewport());
    EXPECT_TRUE(stats.HasScissor());
    EXPECT_TRUE(stats.HasViewportDepthRange());
    EXPECT_TRUE(stats.HasValidViewport());
    EXPECT_TRUE(stats.HasValidScissor());
    EXPECT_TRUE(stats.HasValidDrawState());
    EXPECT_TRUE(stats.HasReadyDrawState());
    EXPECT_TRUE(stats.ViewportMatchesDrawable());
    EXPECT_TRUE(stats.ScissorMatchesDrawable());
    EXPECT_TRUE(stats.HasFullDrawableDrawState());

    stats.viewport.maxX = 640.0;
    EXPECT_TRUE(stats.HasValidDrawState());
    EXPECT_FALSE(stats.ViewportMatchesDrawable());
    EXPECT_FALSE(stats.HasFullDrawableDrawState());
}

TEST(RendererApiTest, DrawSubmissionStatsExposeFrameAndCumulativeCounts) {
    RendererDrawSubmissionStats stats;
    EXPECT_FALSE(stats.ready);
    EXPECT_FALSE(stats.pipelineReady);
    EXPECT_FALSE(stats.geometryReady);
    EXPECT_FALSE(stats.drawStateReady);
    EXPECT_EQ(stats.lastFrameDrawCount, 0u);
    EXPECT_EQ(stats.lastFrameIndexedDrawCount, 0u);
    EXPECT_EQ(stats.lastFrameBaseDrawCount, 0u);
    EXPECT_EQ(stats.lastFrameDebugDrawCount, 0u);
    EXPECT_EQ(stats.materialShaderResourceGroupArgumentBufferBindingIndex,
              kRendererInvalidBindingIndex);
    EXPECT_EQ(stats.lastFrameMaterialShaderResourceGroupArgumentBufferBindCount, 0u);
    EXPECT_EQ(stats.lastFrameIndexCount, 0u);
    EXPECT_EQ(stats.lastFrameInstanceCount, 0u);
    EXPECT_EQ(stats.submittedFrameCount, 0u);
    EXPECT_EQ(stats.submittedDrawCount, 0u);
    EXPECT_EQ(stats.submittedIndexedDrawCount, 0u);
    EXPECT_EQ(stats.submittedMaterialShaderResourceGroupArgumentBufferBindCount, 0u);
    EXPECT_EQ(stats.submittedIndexCount, 0u);
    EXPECT_EQ(stats.submittedInstanceCount, 0u);
    EXPECT_FALSE(stats.HasLastFrameDraws());
    EXPECT_FALSE(stats.HasLastFrameIndexedDraws());
    EXPECT_FALSE(stats.HasLastFrameBaseDraws());
    EXPECT_FALSE(stats.HasLastFrameDebugDraws());
    EXPECT_FALSE(stats.HasMaterialShaderResourceGroupArgumentBufferBindingIndex());
    EXPECT_FALSE(stats.HasLastFrameMaterialShaderResourceGroupArgumentBufferBindings());
    EXPECT_FALSE(stats.HasLastFrameIndices());
    EXPECT_FALSE(stats.HasLastFrameInstances());
    EXPECT_FALSE(stats.HasSubmittedFrames());
    EXPECT_FALSE(stats.HasSubmittedDraws());
    EXPECT_FALSE(stats.HasSubmittedIndexedDraws());
    EXPECT_FALSE(stats.HasSubmittedMaterialShaderResourceGroupArgumentBufferBindings());
    EXPECT_FALSE(stats.HasSubmittedIndices());
    EXPECT_FALSE(stats.HasSubmittedInstances());
    EXPECT_FALSE(stats.HasRequiredDrawState());
    EXPECT_FALSE(stats.HasCompleteMaterialShaderResourceGroupArgumentBufferBinding());
    EXPECT_FALSE(stats.HasReadySubmission());
    EXPECT_FALSE(stats.HasSubmissionActivity());

    stats.ready = true;
    stats.pipelineReady = true;
    stats.geometryReady = true;
    stats.drawStateReady = true;
    stats.lastFrameDrawCount = 10;
    stats.lastFrameIndexedDrawCount = 10;
    stats.lastFrameBaseDrawCount = 1;
    stats.lastFrameDebugDrawCount = 9;
    stats.materialShaderResourceGroupArgumentBufferBindingIndex =
        kRendererMaterialShaderResourceGroupArgumentBufferIndex;
    stats.lastFrameMaterialShaderResourceGroupArgumentBufferBindCount = 10;
    stats.lastFrameIndexCount = 360;
    stats.lastFrameInstanceCount = 10;
    stats.submittedFrameCount = 2;
    stats.submittedDrawCount = 20;
    stats.submittedIndexedDrawCount = 20;
    stats.submittedMaterialShaderResourceGroupArgumentBufferBindCount = 20;
    stats.submittedIndexCount = 720;
    stats.submittedInstanceCount = 20;

    EXPECT_TRUE(stats.HasLastFrameDraws());
    EXPECT_TRUE(stats.HasLastFrameIndexedDraws());
    EXPECT_TRUE(stats.HasLastFrameBaseDraws());
    EXPECT_TRUE(stats.HasLastFrameDebugDraws());
    EXPECT_TRUE(stats.HasMaterialShaderResourceGroupArgumentBufferBindingIndex());
    EXPECT_TRUE(stats.HasLastFrameMaterialShaderResourceGroupArgumentBufferBindings());
    EXPECT_TRUE(stats.HasLastFrameIndices());
    EXPECT_TRUE(stats.HasLastFrameInstances());
    EXPECT_TRUE(stats.HasSubmittedFrames());
    EXPECT_TRUE(stats.HasSubmittedDraws());
    EXPECT_TRUE(stats.HasSubmittedIndexedDraws());
    EXPECT_TRUE(stats.HasSubmittedMaterialShaderResourceGroupArgumentBufferBindings());
    EXPECT_TRUE(stats.HasSubmittedIndices());
    EXPECT_TRUE(stats.HasSubmittedInstances());
    EXPECT_TRUE(stats.HasRequiredDrawState());
    EXPECT_TRUE(stats.HasCompleteMaterialShaderResourceGroupArgumentBufferBinding());
    EXPECT_TRUE(stats.HasReadySubmission());
    EXPECT_TRUE(stats.HasSubmissionActivity());

    stats.lastFrameMaterialShaderResourceGroupArgumentBufferBindCount = 9;
    EXPECT_FALSE(stats.HasCompleteMaterialShaderResourceGroupArgumentBufferBinding());
    stats.lastFrameMaterialShaderResourceGroupArgumentBufferBindCount = 10;

    stats.geometryReady = false;
    EXPECT_FALSE(stats.HasRequiredDrawState());
    EXPECT_FALSE(stats.HasReadySubmission());
    EXPECT_TRUE(stats.HasSubmissionActivity());
}

TEST(RendererApiTest, DrawItemStatsExposeLastFrameDrawItemTable) {
    EXPECT_STREQ(RendererDrawItemKindName(RendererDrawItemKind::Unknown), "unknown");
    EXPECT_STREQ(RendererDrawItemKindName(RendererDrawItemKind::Base), "base");
    EXPECT_STREQ(RendererDrawItemKindName(RendererDrawItemKind::DebugCell), "debugCell");

    RendererDrawItemInfo item;
    EXPECT_FALSE(item.active);
    EXPECT_EQ(item.kind, RendererDrawItemKind::Unknown);
    EXPECT_EQ(item.debugCellIndex, kRendererInvalidBindingIndex);
    EXPECT_EQ(item.uniformBufferIndex, kRendererMaterialUniformBufferIndex);
    EXPECT_EQ(item.vertexBufferIndex, kRendererGeometryVertexBufferIndex);
    EXPECT_FALSE(item.IsBaseDraw());
    EXPECT_FALSE(item.IsDebugCellDraw());
    EXPECT_FALSE(item.HasDebugCellIndex());
    EXPECT_TRUE(item.HasUniformBufferBinding());
    EXPECT_FALSE(item.HasVertexBufferBinding());
    EXPECT_FALSE(item.HasIndexFormat());
    EXPECT_FALSE(item.HasIndexCount());
    EXPECT_FALSE(item.HasInstances());
    EXPECT_FALSE(item.HasResolvedIndexBufferOffset());
    EXPECT_FALSE(item.HasIndexedDraw());
    EXPECT_FALSE(item.HasReadyItem());

    RendererDrawItemStats stats;
    EXPECT_FALSE(stats.ready);
    EXPECT_EQ(stats.capacity, kRendererDrawItemStatsMaxCount);
    EXPECT_EQ(stats.itemCount, 0u);
    EXPECT_EQ(stats.GetItem(0), nullptr);
    EXPECT_EQ(stats.GetFirstItem(), nullptr);
    EXPECT_EQ(stats.GetLastItem(), nullptr);
    EXPECT_TRUE(stats.HasCapacity());
    EXPECT_FALSE(stats.HasItems());
    EXPECT_FALSE(stats.HasBaseItem());
    EXPECT_FALSE(stats.HasDebugItems());
    EXPECT_FALSE(stats.HasPlaceholderDebugItems());
    EXPECT_FALSE(stats.HasCulledDebugItems());
    EXPECT_TRUE(stats.HasCompleteDrawItemTable());
    EXPECT_FALSE(stats.HasReadyItems());

    RendererDrawItemInfo baseItem;
    baseItem.active = true;
    baseItem.kind = RendererDrawItemKind::Base;
    baseItem.drawIndex = 0;
    baseItem.uniformBufferOffset = 0;
    baseItem.vertexStride = 44;
    baseItem.indexFormat = RHI::IndexFormat::Uint16;
    baseItem.indexCount = 36;
    baseItem.instanceCount = 1;

    RendererDrawItemInfo debugItem = baseItem;
    debugItem.kind = RendererDrawItemKind::DebugCell;
    debugItem.drawIndex = 1;
    debugItem.debugCellIndex = 7;
    debugItem.debugCellPlaceholder = true;
    debugItem.uniformBufferOffset = 256;
    debugItem.resolvedIndexBufferByteOffset = 12;

    stats.ready = true;
    stats.itemCount = 2;
    stats.baseItemCount = 1;
    stats.debugItemCount = 1;
    stats.placeholderDebugItemCount = 1;
    stats.culledDebugItemCount = 3;
    stats.items[0] = baseItem;
    stats.items[1] = debugItem;

    ASSERT_NE(stats.GetItem(0), nullptr);
    ASSERT_NE(stats.GetItem(1), nullptr);
    EXPECT_EQ(stats.GetItem(2), nullptr);
    EXPECT_EQ(stats.GetFirstItem()->kind, RendererDrawItemKind::Base);
    EXPECT_EQ(stats.GetLastItem()->kind, RendererDrawItemKind::DebugCell);
    EXPECT_TRUE(stats.HasItems());
    EXPECT_TRUE(stats.HasBaseItem());
    EXPECT_TRUE(stats.HasDebugItems());
    EXPECT_TRUE(stats.HasPlaceholderDebugItems());
    EXPECT_TRUE(stats.HasCulledDebugItems());
    EXPECT_TRUE(stats.HasCompleteDrawItemTable());
    EXPECT_TRUE(stats.HasReadyItems());

    const RendererDrawItemInfo& storedDebugItem = *stats.GetLastItem();
    EXPECT_FALSE(storedDebugItem.IsBaseDraw());
    EXPECT_TRUE(storedDebugItem.IsDebugCellDraw());
    EXPECT_TRUE(storedDebugItem.HasDebugCellIndex());
    EXPECT_TRUE(storedDebugItem.HasUniformBufferBinding());
    EXPECT_TRUE(storedDebugItem.HasVertexBufferBinding());
    EXPECT_TRUE(storedDebugItem.HasIndexFormat());
    EXPECT_TRUE(storedDebugItem.HasIndexCount());
    EXPECT_TRUE(storedDebugItem.HasInstances());
    EXPECT_TRUE(storedDebugItem.HasResolvedIndexBufferOffset());
    EXPECT_TRUE(storedDebugItem.HasIndexedDraw());
    EXPECT_TRUE(storedDebugItem.HasReadyItem());

    RendererDrawItemStats oversizedStats;
    oversizedStats.ready = true;
    oversizedStats.capacity = static_cast<uint32_t>(oversizedStats.items.size() + 1);
    oversizedStats.itemCount = oversizedStats.capacity;
    oversizedStats.baseItemCount = oversizedStats.itemCount;
    EXPECT_EQ(oversizedStats.GetItem(oversizedStats.items.size()), nullptr);
    EXPECT_EQ(oversizedStats.GetLastItem(), nullptr);
    EXPECT_FALSE(oversizedStats.HasCompleteDrawItemTable());
    EXPECT_FALSE(oversizedStats.HasReadyItems());
}

TEST(RendererApiTest, SwapchainStatsExposeDrawableAndPresentationSnapshot) {
    RendererSwapchainStats stats;
    EXPECT_FALSE(stats.ready);
    EXPECT_FALSE(stats.HasDrawableSize());
    EXPECT_FALSE(stats.HasColorFormat());
    EXPECT_FALSE(stats.HasDepthFormat());
    EXPECT_FALSE(stats.HasResizeActivity());
    EXPECT_FALSE(stats.HasAcquireAttempts());
    EXPECT_FALSE(stats.HasAcquiredFrames());
    EXPECT_FALSE(stats.HasPresentedFrames());
    EXPECT_FALSE(stats.HasReleasedFrames());
    EXPECT_FALSE(stats.HasPresentationActivity());
    EXPECT_FALSE(stats.HasSwapchainActivity());
    EXPECT_FALSE(stats.HasDepthCreateFailures());
    EXPECT_FALSE(stats.HasAcquireFailures());
    EXPECT_FALSE(stats.HasFailures());
    EXPECT_EQ(stats.colorFormat, RHI::Format::Unknown);
    EXPECT_EQ(stats.depthFormat, RHI::Format::Unknown);

    stats.depthCreateFailureCount = 1;
    EXPECT_TRUE(stats.HasDepthCreateFailures());
    EXPECT_TRUE(stats.HasFailures());
    stats.depthCreateFailureCount = 0;
    stats.acquireFailureCount = 1;
    EXPECT_FALSE(stats.HasDepthCreateFailures());
    EXPECT_TRUE(stats.HasAcquireFailures());
    EXPECT_TRUE(stats.HasFailures());
    stats.acquireFailureCount = 0;

    stats.ready = true;
    stats.drawableSize.width = 2560;
    stats.drawableSize.height = 1440;
    stats.colorFormat = RHI::Format::BGRA8Unorm;
    stats.depthFormat = RHI::Format::Depth32Float;
    stats.framebufferOnly = true;
    stats.resizeCount = 2;
    stats.depthCreateFailureCount = 1;
    stats.acquireAttemptCount = 8;
    stats.acquiredFrameCount = 7;
    stats.acquireFailureCount = 1;
    stats.presentedFrameCount = 7;
    stats.releasedFrameCount = 7;
    stats.frameAcquired = true;

    EXPECT_TRUE(stats.ready);
    EXPECT_TRUE(stats.HasDrawableSize());
    EXPECT_TRUE(stats.HasColorFormat());
    EXPECT_TRUE(stats.HasDepthFormat());
    EXPECT_TRUE(stats.HasResizeActivity());
    EXPECT_TRUE(stats.HasAcquireAttempts());
    EXPECT_TRUE(stats.HasAcquiredFrames());
    EXPECT_TRUE(stats.HasPresentedFrames());
    EXPECT_TRUE(stats.HasReleasedFrames());
    EXPECT_TRUE(stats.HasPresentationActivity());
    EXPECT_TRUE(stats.HasSwapchainActivity());
    EXPECT_TRUE(stats.HasDepthCreateFailures());
    EXPECT_TRUE(stats.HasAcquireFailures());
    EXPECT_TRUE(stats.HasFailures());
    EXPECT_EQ(stats.drawableSize.width, 2560u);
    EXPECT_EQ(stats.drawableSize.height, 1440u);
    EXPECT_EQ(stats.colorFormat, RHI::Format::BGRA8Unorm);
    EXPECT_EQ(stats.depthFormat, RHI::Format::Depth32Float);
    EXPECT_TRUE(stats.framebufferOnly);
    EXPECT_EQ(stats.resizeCount, 2u);
    EXPECT_EQ(stats.depthCreateFailureCount, 1u);
    EXPECT_EQ(stats.acquireAttemptCount, 8u);
    EXPECT_EQ(stats.acquiredFrameCount, 7u);
    EXPECT_EQ(stats.acquireFailureCount, 1u);
    EXPECT_EQ(stats.presentedFrameCount, 7u);
    EXPECT_EQ(stats.releasedFrameCount, 7u);
    EXPECT_TRUE(stats.frameAcquired);
}

TEST(RendererApiTest, SamplerStatsExposeMaterialSamplerCacheState) {
    RendererSamplerStats stats;
    EXPECT_FALSE(stats.ready);
    EXPECT_EQ(stats.cachedSamplerCount, 0u);
    EXPECT_EQ(stats.materialSamplerSlotCount, 0u);
    EXPECT_EQ(stats.boundMaterialSamplerCount, 0u);
    EXPECT_FALSE(stats.HasCachedSamplers());
    EXPECT_FALSE(stats.HasMaterialSamplerSlots());
    EXPECT_FALSE(stats.HasBoundMaterialSamplers());
    EXPECT_FALSE(stats.HasCompleteMaterialSamplerTable());
    EXPECT_FALSE(stats.HasSamplerActivity());

    stats.materialSamplerSlotCount = static_cast<uint32_t>(kRendererTextureSlotCount);
    stats.boundMaterialSamplerCount = static_cast<uint32_t>(kRendererTextureSlotCount - 1);
    stats.cachedSamplerCount = 1;
    EXPECT_TRUE(stats.HasCachedSamplers());
    EXPECT_TRUE(stats.HasMaterialSamplerSlots());
    EXPECT_TRUE(stats.HasBoundMaterialSamplers());
    EXPECT_FALSE(stats.HasCompleteMaterialSamplerTable());
    EXPECT_TRUE(stats.HasSamplerActivity());

    stats.boundMaterialSamplerCount = static_cast<uint32_t>(kRendererTextureSlotCount);
    stats.ready = stats.HasCachedSamplers() && stats.HasCompleteMaterialSamplerTable();
    EXPECT_TRUE(stats.ready);
    EXPECT_TRUE(stats.HasCompleteMaterialSamplerTable());
    EXPECT_TRUE(stats.HasSamplerActivity());
}

TEST(RendererApiTest, TextureSlotCountMatchesFixedMaterialBindings) {
    EXPECT_EQ(kRendererTextureSlotCount, 5u);
    EXPECT_EQ(static_cast<size_t>(RendererTextureSlot::Occlusion) + 1u, kRendererTextureSlotCount);
    EXPECT_EQ(kRendererGeometryVertexBufferIndex, 0u);
    EXPECT_EQ(kRendererMaterialUniformBufferIndex, 1u);
    EXPECT_EQ(kRendererMaterialShaderResourceGroupArgumentBufferIndex, 2u);
    EXPECT_EQ(kRendererMaterialTextureBindingBaseIndex, 0u);
    EXPECT_EQ(kRendererMaterialSamplerBindingBaseIndex, 0u);
    RendererMaterialBindingLayoutInfo layout;
    EXPECT_EQ(layout.vertexBufferIndex, kRendererGeometryVertexBufferIndex);
    EXPECT_EQ(layout.uniformBufferIndex, kRendererMaterialUniformBufferIndex);
    EXPECT_EQ(layout.argumentBufferIndex, kRendererMaterialShaderResourceGroupArgumentBufferIndex);
    EXPECT_EQ(layout.textureBindingBaseIndex, kRendererMaterialTextureBindingBaseIndex);
    EXPECT_EQ(layout.samplerBindingBaseIndex, kRendererMaterialSamplerBindingBaseIndex);
    EXPECT_EQ(layout.textureBindingCount, static_cast<uint32_t>(kRendererTextureSlotCount));
    EXPECT_EQ(layout.samplerBindingCount, static_cast<uint32_t>(kRendererTextureSlotCount));
    EXPECT_TRUE(layout.HasVertexBufferIndex());
    EXPECT_TRUE(layout.HasUniformBufferIndex());
    EXPECT_TRUE(layout.HasArgumentBufferIndex());
    EXPECT_TRUE(layout.HasTextureBindingRange());
    EXPECT_TRUE(layout.HasSamplerBindingRange());
    EXPECT_TRUE(layout.HasCompleteMaterialBindingRange());
    EXPECT_TRUE(layout.HasFixedMaterialBindingLayout());
    for (size_t i = 0; i < kMaterialSlots.size(); ++i) {
        EXPECT_EQ(RendererTextureSlotIndex(kMaterialSlots[i]), i);
        EXPECT_EQ(RendererMaterialTextureBindingIndex(kMaterialSlots[i]), static_cast<uint32_t>(i));
        EXPECT_EQ(RendererMaterialSamplerBindingIndex(kMaterialSlots[i]), static_cast<uint32_t>(i));
        EXPECT_EQ(layout.GetTextureBindingIndex(kMaterialSlots[i]), static_cast<uint32_t>(i));
        EXPECT_EQ(layout.GetSamplerBindingIndex(kMaterialSlots[i]), static_cast<uint32_t>(i));
    }
    EXPECT_EQ(RendererTextureSlotIndex(static_cast<RendererTextureSlot>(255)), kRendererTextureSlotCount);
    EXPECT_EQ(RendererMaterialTextureBindingIndex(static_cast<RendererTextureSlot>(255)),
              kRendererInvalidBindingIndex);
    EXPECT_EQ(RendererMaterialSamplerBindingIndex(static_cast<RendererTextureSlot>(255)),
              kRendererInvalidBindingIndex);
    EXPECT_EQ(layout.GetTextureBindingIndex(static_cast<RendererTextureSlot>(255)),
              kRendererInvalidBindingIndex);
    EXPECT_EQ(layout.GetSamplerBindingIndex(static_cast<RendererTextureSlot>(255)),
              kRendererInvalidBindingIndex);

    layout.textureBindingCount = 1;
    EXPECT_FALSE(layout.HasCompleteMaterialBindingRange());
    EXPECT_FALSE(layout.HasFixedMaterialBindingLayout());
    EXPECT_EQ(layout.GetTextureBindingIndex(RendererTextureSlot::BaseColor), 0u);
    EXPECT_EQ(layout.GetTextureBindingIndex(RendererTextureSlot::Normal), kRendererInvalidBindingIndex);

    layout = {};
    layout.argumentBufferIndex = kRendererInvalidBindingIndex;
    EXPECT_FALSE(layout.HasArgumentBufferIndex());
    EXPECT_FALSE(layout.HasFixedMaterialBindingLayout());

    layout = {};
    layout.textureBindingBaseIndex = kRendererInvalidBindingIndex;
    EXPECT_FALSE(layout.HasTextureBindingRange());
    EXPECT_FALSE(layout.HasFixedMaterialBindingLayout());
    EXPECT_EQ(layout.GetTextureBindingIndex(RendererTextureSlot::BaseColor), kRendererInvalidBindingIndex);
}

TEST(RendererApiTest, TextureSlotNamesCoverPublicMaterialSlots) {
    EXPECT_STREQ(RendererTextureSlotName(RendererTextureSlot::BaseColor), "baseColor");
    EXPECT_STREQ(RendererTextureSlotName(RendererTextureSlot::Normal), "normal");
    EXPECT_STREQ(RendererTextureSlotName(RendererTextureSlot::MetallicRoughness), "metallicRoughness");
    EXPECT_STREQ(RendererTextureSlotName(RendererTextureSlot::Emissive), "emissive");
    EXPECT_STREQ(RendererTextureSlotName(RendererTextureSlot::Occlusion), "occlusion");
    EXPECT_STREQ(RendererTextureSlotName(static_cast<RendererTextureSlot>(255)), "unknown");
}

TEST(RendererApiTest, ResourceStateStatsExposeKeyResourceTable) {
    EXPECT_STREQ(RendererResourceStateKindName(RendererResourceStateKind::Unknown), "unknown");
    EXPECT_STREQ(RendererResourceStateKindName(RendererResourceStateKind::VertexBuffer), "vertexBuffer");
    EXPECT_STREQ(RendererResourceStateKindName(RendererResourceStateKind::IndexBuffer), "indexBuffer");
    EXPECT_STREQ(RendererResourceStateKindName(RendererResourceStateKind::UniformBuffer), "uniformBuffer");
    EXPECT_STREQ(RendererResourceStateKindName(RendererResourceStateKind::MaterialTexture), "materialTexture");
    EXPECT_STREQ(RendererResourceStateKindName(static_cast<RendererResourceStateKind>(255)), "unknown");
    EXPECT_EQ(kRendererResourceStateMaxCount, 3u + kRendererTextureSlotCount);

    RendererResourceStateStats stats;
    EXPECT_FALSE(stats.ready);
    EXPECT_EQ(stats.frameGraphValidation.error, RHI::FrameGraphDescriptorError::None);
    EXPECT_EQ(stats.frameGraphTransitionCount, 0u);
    EXPECT_EQ(stats.resourceCount, 0u);
    EXPECT_EQ(stats.bufferResourceCount, 0u);
    EXPECT_EQ(stats.textureResourceCount, 0u);
    EXPECT_EQ(stats.expectedStateMatchCount, 0u);
    EXPECT_EQ(stats.resources.size(), kRendererResourceStateMaxCount);
    EXPECT_EQ(stats.GetResource(0), nullptr);
    EXPECT_EQ(stats.GetMaterialTexture(RendererTextureSlot::BaseColor), nullptr);
    EXPECT_FALSE(stats.resources[0].active);
    EXPECT_EQ(stats.resources[0].index, kRendererInvalidBindingIndex);
    EXPECT_EQ(stats.resources[0].kind, RendererResourceStateKind::Unknown);
    EXPECT_EQ(stats.resources[0].resourceType, RHI::ResourceType::Unknown);
    EXPECT_EQ(stats.resources[0].usage, 0u);
    EXPECT_EQ(stats.resources[0].currentState, RHI::ResourceState::Undefined);
    EXPECT_EQ(stats.resources[0].expectedState, RHI::ResourceState::Undefined);
    EXPECT_EQ(stats.resources[0].textureSlot, RendererTextureSlot::BaseColor);
    EXPECT_EQ(stats.resources[0].bindingIndex, kRendererInvalidBindingIndex);
    EXPECT_STREQ(stats.resources[0].GetDebugName(), "");
    EXPECT_FALSE(stats.resources[0].HasResource());
    EXPECT_FALSE(stats.resources[0].HasDebugName());
    EXPECT_FALSE(stats.resources[0].IsBuffer());
    EXPECT_FALSE(stats.resources[0].IsTexture());
    EXPECT_FALSE(stats.resources[0].IsMaterialTexture());
    EXPECT_FALSE(stats.resources[0].HasUsage());
    EXPECT_FALSE(stats.resources[0].HasBindingIndex());
    EXPECT_FALSE(stats.resources[0].HasExpectedState());
    EXPECT_FALSE(stats.resources[0].MatchesExpectedState());
    EXPECT_FALSE(stats.resources[0].IsShaderReadable());
    EXPECT_FALSE(stats.resources[0].IsCopyDestination());
    EXPECT_FALSE(stats.resources[0].HasReadyResourceState());
    EXPECT_FALSE(stats.HasResources());
    EXPECT_FALSE(stats.HasBuffers());
    EXPECT_FALSE(stats.HasTextures());
    EXPECT_FALSE(stats.HasExpectedStateMatches());
    EXPECT_FALSE(stats.HasValidFrameGraphResourcePlan());
    EXPECT_FALSE(stats.HasFrameGraphTransitions());
    EXPECT_FALSE(stats.HasFrameGraphPasses());
    EXPECT_FALSE(stats.HasFrameGraphReadyPass());
    EXPECT_FALSE(stats.HasStateMismatches());
    EXPECT_FALSE(stats.HasCompleteResourceStateTable());
    EXPECT_FALSE(stats.HasAllExpectedStates());
    EXPECT_FALSE(stats.HasReadyResourceStates());

    stats.ready = true;
    stats.frameGraphTransitionCount = 4;
    stats.frameGraphPassCount = 1;
    stats.frameGraphReadyPassIndex = 0;
    stats.frameGraphReadyPassTransitionCount = 4;
    stats.resourceCount = 4;
    stats.bufferResourceCount = 3;
    stats.textureResourceCount = 1;
    stats.expectedStateMatchCount = 4;
    stats.resources[0].active = true;
    stats.resources[0].index = 0;
    stats.resources[0].kind = RendererResourceStateKind::VertexBuffer;
    stats.resources[0].resourceType = RHI::ResourceType::Buffer;
    stats.resources[0].usage = RHI::ResourceUsageFlag(RHI::ResourceUsage::VertexBuffer);
    stats.resources[0].currentState = RHI::ResourceState::VertexBuffer;
    stats.resources[0].expectedState = RHI::ResourceState::VertexBuffer;
    stats.resources[0].bindingIndex = kRendererGeometryVertexBufferIndex;
    stats.resources[0].SetDebugName("NEXT cube vertices");
    stats.resources[1].active = true;
    stats.resources[1].index = 1;
    stats.resources[1].kind = RendererResourceStateKind::IndexBuffer;
    stats.resources[1].resourceType = RHI::ResourceType::Buffer;
    stats.resources[1].usage = RHI::ResourceUsageFlag(RHI::ResourceUsage::IndexBuffer);
    stats.resources[1].currentState = RHI::ResourceState::IndexBuffer;
    stats.resources[1].expectedState = RHI::ResourceState::IndexBuffer;
    stats.resources[2].active = true;
    stats.resources[2].index = 2;
    stats.resources[2].kind = RendererResourceStateKind::UniformBuffer;
    stats.resources[2].resourceType = RHI::ResourceType::Buffer;
    stats.resources[2].usage = RHI::ResourceUsageFlag(RHI::ResourceUsage::ConstantBuffer);
    stats.resources[2].currentState = RHI::ResourceState::ConstantBuffer;
    stats.resources[2].expectedState = RHI::ResourceState::ConstantBuffer;
    stats.resources[2].bindingIndex = kRendererMaterialUniformBufferIndex;
    stats.resources[3].active = true;
    stats.resources[3].index = 3;
    stats.resources[3].kind = RendererResourceStateKind::MaterialTexture;
    stats.resources[3].resourceType = RHI::ResourceType::Texture;
    stats.resources[3].usage = RHI::ResourceUsageFlag(RHI::ResourceUsage::ShaderRead);
    stats.resources[3].currentState = RHI::ResourceState::ShaderRead;
    stats.resources[3].expectedState = RHI::ResourceState::ShaderRead;
    stats.resources[3].textureSlot = RendererTextureSlot::BaseColor;
    stats.resources[3].bindingIndex = RendererMaterialTextureBindingIndex(RendererTextureSlot::BaseColor);
    stats.resources[3].SetDebugName("NEXT base color texture");

    const RendererResourceStateInfo* firstResource = stats.GetResource(0);
    const RendererResourceStateInfo* baseColorTexture =
        stats.GetMaterialTexture(RendererTextureSlot::BaseColor);
    ASSERT_NE(firstResource, nullptr);
    ASSERT_NE(baseColorTexture, nullptr);
    EXPECT_EQ(stats.GetResource(4), nullptr);
    EXPECT_EQ(stats.GetMaterialTexture(RendererTextureSlot::Normal), nullptr);
    EXPECT_TRUE(firstResource->HasResource());
    EXPECT_TRUE(firstResource->HasDebugName());
    EXPECT_TRUE(firstResource->IsBuffer());
    EXPECT_FALSE(firstResource->IsTexture());
    EXPECT_FALSE(firstResource->IsMaterialTexture());
    EXPECT_TRUE(firstResource->HasUsage());
    EXPECT_TRUE(firstResource->HasBindingIndex());
    EXPECT_TRUE(firstResource->HasExpectedState());
    EXPECT_TRUE(firstResource->MatchesExpectedState());
    EXPECT_FALSE(firstResource->IsShaderReadable());
    EXPECT_FALSE(firstResource->IsCopyDestination());
    EXPECT_TRUE(firstResource->HasReadyResourceState());
    EXPECT_STREQ(firstResource->GetDebugName(), "NEXT cube vertices");
    EXPECT_TRUE(baseColorTexture->IsTexture());
    EXPECT_TRUE(baseColorTexture->IsMaterialTexture());
    EXPECT_TRUE(baseColorTexture->IsShaderReadable());
    EXPECT_TRUE(baseColorTexture->HasBindingIndex());
    EXPECT_EQ(baseColorTexture->textureSlot, RendererTextureSlot::BaseColor);
    EXPECT_EQ(baseColorTexture->bindingIndex, 0u);
    EXPECT_TRUE(stats.HasResources());
    EXPECT_TRUE(stats.HasBuffers());
    EXPECT_TRUE(stats.HasTextures());
    EXPECT_TRUE(stats.HasExpectedStateMatches());
    EXPECT_TRUE(stats.HasValidFrameGraphResourcePlan());
    EXPECT_TRUE(stats.HasFrameGraphTransitions());
    EXPECT_TRUE(stats.HasFrameGraphPasses());
    EXPECT_TRUE(stats.HasFrameGraphReadyPass());
    EXPECT_EQ(stats.frameGraphPassCount, 1u);
    EXPECT_EQ(stats.frameGraphReadyPassIndex, 0u);
    EXPECT_EQ(stats.frameGraphReadyPassTransitionCount, 4u);
    EXPECT_FALSE(stats.HasStateMismatches());
    EXPECT_TRUE(stats.HasCompleteResourceStateTable());
    EXPECT_TRUE(stats.HasAllExpectedStates());
    EXPECT_TRUE(stats.HasReadyResourceStates());

    stats.resources[3].currentState = RHI::ResourceState::CopyDestination;
    stats.expectedStateMatchCount = 3;
    EXPECT_TRUE(stats.resources[3].IsCopyDestination());
    EXPECT_FALSE(stats.resources[3].MatchesExpectedState());
    EXPECT_TRUE(stats.HasStateMismatches());
    EXPECT_FALSE(stats.HasAllExpectedStates());
    EXPECT_FALSE(stats.HasReadyResourceStates());
}

TEST(RendererApiTest, TextureFormatBytesPerPixelCoversUploadFormats) {
    EXPECT_STREQ(RendererTextureFormatName(RendererTextureFormat::Unknown), "unknown");
    EXPECT_STREQ(RendererTextureFormatName(RendererTextureFormat::RGBA8Unorm), "rgba8unorm");
    EXPECT_STREQ(RendererTextureFormatName(static_cast<RendererTextureFormat>(255)), "unknown");
    EXPECT_EQ(RendererTextureFormatBytesPerPixel(RendererTextureFormat::Unknown), 0u);
    EXPECT_EQ(RendererTextureFormatBytesPerPixel(RendererTextureFormat::RGBA8Unorm), 4u);
    EXPECT_EQ(RendererTextureFormatBytesPerPixel(static_cast<RendererTextureFormat>(255)), 0u);
}

TEST(RendererApiTest, TextureUploadRequiredBytesValidatesPayloadDimensions) {
    size_t requiredBytes = 0;
    EXPECT_TRUE(RendererTextureUploadRequiredBytes(RendererTextureFormat::RGBA8Unorm, 4, 8, requiredBytes));
    EXPECT_EQ(requiredBytes, 128u);

    requiredBytes = 99;
    EXPECT_FALSE(RendererTextureUploadRequiredBytes(RendererTextureFormat::Unknown, 4, 8, requiredBytes));
    EXPECT_EQ(requiredBytes, 0u);

    EXPECT_FALSE(RendererTextureUploadRequiredBytes(RendererTextureFormat::RGBA8Unorm, 0, 8, requiredBytes));
    EXPECT_EQ(requiredBytes, 0u);

    EXPECT_FALSE(RendererTextureUploadRequiredBytes(static_cast<RendererTextureFormat>(255), 4, 8, requiredBytes));
    EXPECT_EQ(requiredBytes, 0u);

    EXPECT_FALSE(RendererTextureUploadRequiredBytes(RendererTextureFormat::RGBA8Unorm,
                                                    std::numeric_limits<uint32_t>::max(),
                                                    std::numeric_limits<uint32_t>::max(),
                                                    requiredBytes));
    EXPECT_EQ(requiredBytes, 0u);
}

TEST(RendererApiTest, TextureUploadValidationReportsDescriptorFailures) {
    const uint32_t pixels[4] = {};
    RendererTextureUploadDesc texture;
    texture.slot = RendererTextureSlot::BaseColor;
    texture.format = RendererTextureFormat::RGBA8Unorm;
    texture.width = 2;
    texture.height = 2;
    texture.pixels = pixels;
    texture.pixelBytes = sizeof(pixels);

    RendererTextureUploadValidation validation = ValidateRendererTextureUploadDesc(texture);
    EXPECT_TRUE(validation);
    EXPECT_EQ(validation.error, RendererTextureUploadValidationError::None);
    EXPECT_EQ(validation.requiredBytes, sizeof(pixels));
    EXPECT_STREQ(RendererTextureUploadValidationErrorName(validation.error), "none");

    texture.slot = static_cast<RendererTextureSlot>(255);
    validation = ValidateRendererTextureUploadDesc(texture);
    EXPECT_FALSE(validation);
    EXPECT_EQ(validation.error, RendererTextureUploadValidationError::InvalidSlot);
    EXPECT_STREQ(RendererTextureUploadValidationErrorName(validation.error), "invalid_slot");
    texture.slot = RendererTextureSlot::BaseColor;

    texture.pixels = nullptr;
    validation = ValidateRendererTextureUploadDesc(texture);
    EXPECT_FALSE(validation);
    EXPECT_EQ(validation.error, RendererTextureUploadValidationError::MissingPixels);
    texture.pixels = pixels;

    texture.width = 0;
    validation = ValidateRendererTextureUploadDesc(texture);
    EXPECT_FALSE(validation);
    EXPECT_EQ(validation.error, RendererTextureUploadValidationError::InvalidDimensions);
    texture.width = 2;

    texture.format = RendererTextureFormat::Unknown;
    validation = ValidateRendererTextureUploadDesc(texture);
    EXPECT_FALSE(validation);
    EXPECT_EQ(validation.error, RendererTextureUploadValidationError::UnsupportedFormat);
    texture.format = RendererTextureFormat::RGBA8Unorm;

    texture.width = std::numeric_limits<uint32_t>::max();
    texture.height = std::numeric_limits<uint32_t>::max();
    validation = ValidateRendererTextureUploadDesc(texture);
    EXPECT_FALSE(validation);
    EXPECT_EQ(validation.error, RendererTextureUploadValidationError::ByteSizeOverflow);
    texture.width = 2;
    texture.height = 2;

    texture.pixelBytes = sizeof(pixels) - 1;
    validation = ValidateRendererTextureUploadDesc(texture);
    EXPECT_FALSE(validation);
    EXPECT_EQ(validation.error, RendererTextureUploadValidationError::InsufficientPixels);
    EXPECT_EQ(validation.requiredBytes, sizeof(pixels));
    EXPECT_STREQ(RendererTextureUploadValidationErrorName(static_cast<RendererTextureUploadValidationError>(255)),
                 "unknown");
}

TEST(RendererApiTest, TextureUploadStatusNamesCoverPublicStatuses) {
    EXPECT_STREQ(RendererTextureUploadStatusName(RendererTextureUploadStatus::Unknown), "unknown");
    EXPECT_STREQ(RendererTextureUploadStatusName(RendererTextureUploadStatus::Pending), "pending");
    EXPECT_STREQ(RendererTextureUploadStatusName(RendererTextureUploadStatus::Completed), "completed");
    EXPECT_STREQ(RendererTextureUploadStatusName(RendererTextureUploadStatus::Failed), "failed");
    EXPECT_STREQ(RendererTextureUploadStatusName(static_cast<RendererTextureUploadStatus>(255)), "unknown");
}

TEST(RendererApiTest, FrameDescReportsDebugCellClampAndPlaceholderState) {
    RendererDebugCell cell;
    EXPECT_FALSE(cell.IsPlaceholder());
    cell.flags = kRendererDebugCellPlaceholder;
    EXPECT_TRUE(cell.IsPlaceholder());

    RendererFrameDesc frame;
    EXPECT_EQ(frame.DebugCellCount(), 0u);
    EXPECT_EQ(frame.RenderedDebugCellCount(), 0u);
    EXPECT_EQ(frame.DebugCellOverflowCount(), 0u);
    EXPECT_EQ(frame.PlaceholderDebugCellCount(), 0u);
    EXPECT_EQ(frame.RenderedPlaceholderDebugCellCount(), 0u);
    EXPECT_FALSE(frame.HasDebugCells());
    EXPECT_FALSE(frame.HasRenderedDebugCells());
    EXPECT_FALSE(frame.HasDebugCellOverflow());
    EXPECT_FALSE(frame.HasPlaceholderDebugCells());
    EXPECT_FALSE(frame.HasRenderedPlaceholderDebugCells());
    EXPECT_FALSE(frame.HasDebugCellActivity());
    RendererFrameDebugStats stats = frame.GetDebugStats();
    EXPECT_EQ(stats.submittedDebugCellCount, 0u);
    EXPECT_EQ(stats.renderedDebugCellCount, 0u);
    EXPECT_EQ(stats.overflowDebugCellCount, 0u);
    EXPECT_EQ(stats.placeholderDebugCellCount, 0u);
    EXPECT_EQ(stats.renderedPlaceholderDebugCellCount, 0u);
    EXPECT_FALSE(stats.HasDebugCells());
    EXPECT_FALSE(stats.HasRenderedDebugCells());
    EXPECT_FALSE(stats.HasDebugCellOverflow());
    EXPECT_FALSE(stats.HasPlaceholderDebugCells());
    EXPECT_FALSE(stats.HasRenderedPlaceholderDebugCells());
    EXPECT_FALSE(stats.HasDebugCellActivity());

    frame.debugCells.resize(kMaxRendererDebugCells);
    frame.debugCells[0].flags = kRendererDebugCellPlaceholder;
    EXPECT_EQ(frame.DebugCellCount(), kMaxRendererDebugCells);
    EXPECT_EQ(frame.RenderedDebugCellCount(), kMaxRendererDebugCells);
    EXPECT_EQ(frame.DebugCellOverflowCount(), 0u);
    EXPECT_EQ(frame.PlaceholderDebugCellCount(), 1u);
    EXPECT_EQ(frame.RenderedPlaceholderDebugCellCount(), 1u);
    EXPECT_TRUE(frame.HasDebugCells());
    EXPECT_TRUE(frame.HasRenderedDebugCells());
    EXPECT_FALSE(frame.HasDebugCellOverflow());
    EXPECT_TRUE(frame.HasPlaceholderDebugCells());
    EXPECT_TRUE(frame.HasRenderedPlaceholderDebugCells());
    EXPECT_TRUE(frame.HasDebugCellActivity());

    frame.debugCells.push_back(cell);
    EXPECT_EQ(frame.DebugCellCount(), kMaxRendererDebugCells + 1u);
    EXPECT_EQ(frame.RenderedDebugCellCount(), kMaxRendererDebugCells);
    EXPECT_EQ(frame.DebugCellOverflowCount(), 1u);
    EXPECT_EQ(frame.PlaceholderDebugCellCount(), 2u);
    EXPECT_EQ(frame.RenderedPlaceholderDebugCellCount(), 1u);
    EXPECT_TRUE(frame.HasDebugCells());
    EXPECT_TRUE(frame.HasDebugCellOverflow());
    EXPECT_TRUE(frame.debugCells.back().IsPlaceholder());
    stats = frame.GetDebugStats();
    EXPECT_EQ(stats.submittedDebugCellCount, kMaxRendererDebugCells + 1u);
    EXPECT_EQ(stats.renderedDebugCellCount, kMaxRendererDebugCells);
    EXPECT_EQ(stats.overflowDebugCellCount, 1u);
    EXPECT_EQ(stats.placeholderDebugCellCount, 2u);
    EXPECT_EQ(stats.renderedPlaceholderDebugCellCount, 1u);
    EXPECT_TRUE(stats.HasDebugCells());
    EXPECT_TRUE(stats.HasRenderedDebugCells());
    EXPECT_TRUE(stats.HasDebugCellOverflow());
    EXPECT_TRUE(stats.HasPlaceholderDebugCells());
    EXPECT_TRUE(stats.HasRenderedPlaceholderDebugCells());
    EXPECT_TRUE(stats.HasDebugCellActivity());
}

TEST(RendererApiTest, MaterialDescStoresOneTexturePerMaterialSlot) {
    RendererMaterialDesc material;
    EXPECT_FALSE(material.HasSourceAsset());
    EXPECT_EQ(material.BoundTextureCount(), 0u);
    EXPECT_FALSE(material.HasAnyTexture());
    EXPECT_FALSE(material.HasTexture(RendererTextureSlot::BaseColor));
    EXPECT_FALSE(material.HasBaseColorTexture());
    EXPECT_FALSE(material.HasNormalTexture());
    EXPECT_FALSE(material.HasMetallicRoughnessTexture());
    EXPECT_FALSE(material.HasEmissiveTexture());
    EXPECT_FALSE(material.HasOcclusionTexture());
    EXPECT_FALSE(material.HasCompleteTextureSet());
    EXPECT_TRUE(material.HasValidBaseColorFactor());
    EXPECT_TRUE(material.HasValidRoughness());
    EXPECT_TRUE(material.HasValidMetallic());
    EXPECT_TRUE(material.HasValidExposure());
    EXPECT_TRUE(material.HasValidParameters());

    for (size_t i = 0; i < kMaterialSlots.size(); ++i) {
        material.SetTexture(kMaterialSlots[i], RendererTextureHandle{100 + i});
        EXPECT_TRUE(material.HasTexture(kMaterialSlots[i]));
        EXPECT_TRUE(material.HasAnyTexture());
        EXPECT_EQ(material.BoundTextureCount(), i + 1u);
    }

    for (size_t i = 0; i < kMaterialSlots.size(); ++i) {
        EXPECT_EQ(material.GetTexture(kMaterialSlots[i]).id, 100u + i);
    }
    material.sourceAssetId = 19;
    EXPECT_TRUE(material.HasSourceAsset());
    EXPECT_TRUE(material.HasBaseColorTexture());
    EXPECT_TRUE(material.HasNormalTexture());
    EXPECT_TRUE(material.HasMetallicRoughnessTexture());
    EXPECT_TRUE(material.HasEmissiveTexture());
    EXPECT_TRUE(material.HasOcclusionTexture());
    EXPECT_EQ(material.BoundTextureCount(), kMaterialSlots.size());
    EXPECT_TRUE(material.HasCompleteTextureSet());
}

TEST(RendererApiTest, MaterialDescValidationReportsInvalidPbrParameters) {
    RendererMaterialDesc material;
    RendererMaterialValidation validation = ValidateRendererMaterialDesc(material);
    EXPECT_TRUE(validation);
    EXPECT_EQ(validation.error, RendererMaterialValidationError::None);
    EXPECT_STREQ(RendererMaterialValidationErrorName(validation.error), "none");

    material.baseColorFactor[2] = -0.01f;
    validation = ValidateRendererMaterialDesc(material);
    EXPECT_FALSE(validation);
    EXPECT_EQ(validation.error, RendererMaterialValidationError::BaseColorFactorOutOfRange);
    EXPECT_EQ(validation.baseColorFactorIndex, 2u);
    EXPECT_FALSE(material.HasValidBaseColorFactor());
    EXPECT_FALSE(material.HasValidParameters());
    EXPECT_STREQ(RendererMaterialValidationErrorName(validation.error), "base-color-factor-out-of-range");

    material.baseColorFactor[2] = std::numeric_limits<float>::quiet_NaN();
    validation = ValidateRendererMaterialDesc(material);
    EXPECT_FALSE(validation);
    EXPECT_EQ(validation.error, RendererMaterialValidationError::NonFiniteBaseColorFactor);
    EXPECT_EQ(validation.baseColorFactorIndex, 2u);

    material = {};
    material.roughness = 1.1f;
    validation = ValidateRendererMaterialDesc(material);
    EXPECT_FALSE(validation);
    EXPECT_EQ(validation.error, RendererMaterialValidationError::RoughnessOutOfRange);
    EXPECT_FALSE(material.HasValidRoughness());

    material = {};
    material.metallic = -0.1f;
    validation = ValidateRendererMaterialDesc(material);
    EXPECT_FALSE(validation);
    EXPECT_EQ(validation.error, RendererMaterialValidationError::MetallicOutOfRange);
    EXPECT_FALSE(material.HasValidMetallic());

    material = {};
    material.exposure = -0.01f;
    validation = ValidateRendererMaterialDesc(material);
    EXPECT_FALSE(validation);
    EXPECT_EQ(validation.error, RendererMaterialValidationError::ExposureOutOfRange);
    EXPECT_FALSE(material.HasValidExposure());

    material.exposure = std::numeric_limits<float>::infinity();
    validation = ValidateRendererMaterialDesc(material);
    EXPECT_FALSE(validation);
    EXPECT_EQ(validation.error, RendererMaterialValidationError::NonFiniteExposure);
}

TEST(RendererApiTest, MaterialDescIgnoresUnknownTextureSlot) {
    RendererMaterialDesc material;
    material.SetTexture(RendererTextureSlot::BaseColor, RendererTextureHandle{41});

    const RendererTextureSlot unknownSlot = static_cast<RendererTextureSlot>(255);
    material.SetTexture(unknownSlot, RendererTextureHandle{99});

    EXPECT_EQ(material.GetTexture(RendererTextureSlot::BaseColor).id, 41u);
    EXPECT_TRUE(material.HasTexture(RendererTextureSlot::BaseColor));
    EXPECT_TRUE(material.HasBaseColorTexture());
    EXPECT_TRUE(material.HasAnyTexture());
    EXPECT_FALSE(material.GetTexture(unknownSlot));
    EXPECT_FALSE(material.HasTexture(unknownSlot));
    EXPECT_EQ(material.BoundTextureCount(), 1u);
    EXPECT_FALSE(material.HasCompleteTextureSet());
}

TEST(RendererApiTest, MaterialInfoHelpersExposeHandleSourceAndBoundTextureState) {
    RendererMaterialInfo info;
    EXPECT_FALSE(info.HasMaterial());
    EXPECT_FALSE(info.HasSourceAsset());
    EXPECT_FALSE(info.HasTexture(RendererTextureSlot::Normal));
    EXPECT_EQ(info.BoundTextureCount(), 0u);
    EXPECT_FALSE(info.HasBoundTextures());
    EXPECT_FALSE(info.HasBaseColorTexture());
    EXPECT_FALSE(info.HasNormalTexture());
    EXPECT_FALSE(info.HasMetallicRoughnessTexture());
    EXPECT_FALSE(info.HasEmissiveTexture());
    EXPECT_FALSE(info.HasOcclusionTexture());
    EXPECT_FALSE(info.HasCompleteTextureSet());
    EXPECT_TRUE(info.HasValidParameters());
    EXPECT_FALSE(info.HasActiveMaterial());
    EXPECT_FALSE(info.HasMaterialActivity());

    info.material = RendererMaterialHandle{77};
    info.desc.sourceAssetId = 12;
    info.desc.SetTexture(RendererTextureSlot::Normal, RendererTextureHandle{34});

    EXPECT_TRUE(info.HasMaterial());
    EXPECT_TRUE(info.HasSourceAsset());
    EXPECT_FALSE(info.HasTexture(RendererTextureSlot::BaseColor));
    EXPECT_TRUE(info.HasTexture(RendererTextureSlot::Normal));
    EXPECT_FALSE(info.HasBaseColorTexture());
    EXPECT_TRUE(info.HasNormalTexture());
    EXPECT_FALSE(info.HasCompleteTextureSet());
    EXPECT_EQ(info.BoundTextureCount(), 1u);
    EXPECT_TRUE(info.HasBoundTextures());
    EXPECT_FALSE(info.HasActiveMaterial());
    EXPECT_TRUE(info.HasMaterialActivity());

    info.desc.SetTexture(RendererTextureSlot::BaseColor, RendererTextureHandle{35});
    info.desc.SetTexture(RendererTextureSlot::MetallicRoughness, RendererTextureHandle{36});
    info.desc.SetTexture(RendererTextureSlot::Emissive, RendererTextureHandle{37});
    info.desc.SetTexture(RendererTextureSlot::Occlusion, RendererTextureHandle{38});
    EXPECT_TRUE(info.HasBaseColorTexture());
    EXPECT_TRUE(info.HasMetallicRoughnessTexture());
    EXPECT_TRUE(info.HasEmissiveTexture());
    EXPECT_TRUE(info.HasOcclusionTexture());
    EXPECT_TRUE(info.HasCompleteTextureSet());
    EXPECT_TRUE(info.HasValidParameters());

    info.active = true;
    EXPECT_TRUE(info.HasActiveMaterial());
    EXPECT_TRUE(info.HasMaterialActivity());

    info.material = {};
    EXPECT_FALSE(info.HasMaterial());
    EXPECT_FALSE(info.HasActiveMaterial());
    EXPECT_TRUE(info.HasMaterialActivity());
}

TEST(RendererApiTest, MaterialStatsExposeTableCapacityAndActiveRecord) {
    RendererMaterialStats stats;
    EXPECT_FALSE(stats.ready);
    EXPECT_EQ(stats.materialCapacity, 0u);
    EXPECT_EQ(stats.materialCount, 0u);
    EXPECT_FALSE(stats.activeMaterial);
    EXPECT_EQ(stats.activeMaterialIndex, 0u);
    EXPECT_EQ(stats.activeMaterialBoundTextureCount, 0u);
    EXPECT_FALSE(stats.activeMaterialCompleteTextureSet);
    EXPECT_FALSE(stats.activeMaterialParametersValid);
    EXPECT_EQ(stats.shaderVisibleTextureCount, 0u);
    EXPECT_EQ(stats.shaderVisibleSamplerCount, 0u);
    EXPECT_EQ(stats.fallbackTextureCount, 0u);
    EXPECT_EQ(stats.shaderResourceGroupBindingCount, 0u);
    EXPECT_EQ(stats.shaderResourceGroupLayoutValidation.error,
              RHI::ShaderResourceGroupLayoutError::None);
    EXPECT_STREQ(RendererMaterialBindingSourceName(RendererMaterialBindingSource::Missing), "missing");
    EXPECT_STREQ(RendererMaterialBindingSourceName(RendererMaterialBindingSource::MaterialTexture),
                 "materialTexture");
    EXPECT_STREQ(RendererMaterialBindingSourceName(RendererMaterialBindingSource::ActiveSlotTexture),
                 "activeSlotTexture");
    EXPECT_STREQ(RendererMaterialBindingSourceName(RendererMaterialBindingSource::NeutralTexture),
                 "neutralTexture");
    EXPECT_STREQ(RendererMaterialBindingSourceName(static_cast<RendererMaterialBindingSource>(255)),
                 "missing");
    RendererMaterialBindingInfo binding = stats.GetActiveMaterialBinding(RendererTextureSlot::Normal);
    EXPECT_EQ(binding.slot, RendererTextureSlot::Normal);
    EXPECT_FALSE(binding.HasRequestedTexture());
    EXPECT_FALSE(binding.HasBoundTexture());
    EXPECT_FALSE(binding.HasFormat());
    EXPECT_FALSE(binding.HasSourceAsset());
    EXPECT_FALSE(binding.HasDimensions());
    EXPECT_EQ(binding.textureBindingIndex, kRendererInvalidBindingIndex);
    EXPECT_EQ(binding.samplerBindingIndex, kRendererInvalidBindingIndex);
    EXPECT_EQ(binding.uniformBufferIndex, kRendererInvalidBindingIndex);
    EXPECT_FALSE(binding.HasTextureBindingIndex());
    EXPECT_FALSE(binding.HasSamplerBindingIndex());
    EXPECT_FALSE(binding.HasUniformBufferIndex());
    EXPECT_FALSE(binding.HasBindingIndices());
    EXPECT_FALSE(binding.HasTextureReady());
    EXPECT_FALSE(binding.HasSamplerReady());
    EXPECT_FALSE(binding.UsesMaterialTexture());
    EXPECT_FALSE(binding.UsesActiveSlotTexture());
    EXPECT_FALSE(binding.UsesNeutralTexture());
    EXPECT_FALSE(binding.UsesFallbackTexture());
    EXPECT_FALSE(binding.IsShaderVisible());
    EXPECT_FALSE(binding.HasBindingActivity());
    EXPECT_FALSE(stats.HasMaterialCapacity());
    EXPECT_FALSE(stats.HasMaterials());
    EXPECT_FALSE(stats.HasActiveMaterial());
    EXPECT_FALSE(stats.HasActiveMaterialSlot());
    EXPECT_FALSE(stats.HasFreeMaterialSlots());
    EXPECT_FALSE(stats.IsMaterialTableFull());
    EXPECT_FALSE(stats.HasActiveBoundTextures());
    EXPECT_FALSE(stats.HasCompleteActiveTextureSet());
    EXPECT_FALSE(stats.HasValidActiveParameters());
    EXPECT_FALSE(stats.HasShaderVisibleTextures());
    EXPECT_FALSE(stats.HasShaderVisibleSamplers());
    EXPECT_FALSE(stats.HasFallbackTextures());
    EXPECT_FALSE(stats.HasCompleteShaderBindings());
    EXPECT_EQ(stats.bindingLayout.vertexBufferIndex, kRendererGeometryVertexBufferIndex);
    EXPECT_EQ(stats.bindingLayout.uniformBufferIndex, kRendererMaterialUniformBufferIndex);
    EXPECT_TRUE(stats.HasMaterialBindingLayout());
    EXPECT_FALSE(stats.HasShaderResourceGroupLayout());
    EXPECT_FALSE(stats.HasCompleteShaderResourceGroupBindingTable());
    EXPECT_FALSE(stats.HasValidShaderResourceGroupLayout());
    EXPECT_FALSE(stats.HasCompleteShaderResourceGroupResources());
    EXPECT_FALSE(stats.HasShaderResourceGroupArgumentEncoder());
    EXPECT_FALSE(stats.HasShaderResourceGroupArguments());
    EXPECT_FALSE(stats.HasShaderResourceGroupEncodedLength());
    EXPECT_FALSE(stats.HasShaderResourceGroupEncodedStride());
    EXPECT_FALSE(stats.HasReadyShaderResourceGroupArgumentEncoder());
    EXPECT_FALSE(stats.HasShaderResourceGroupArgumentBuffer());
    EXPECT_FALSE(stats.HasShaderResourceGroupArgumentBufferBytes());
    EXPECT_FALSE(stats.HasShaderResourceGroupArgumentBufferDrawCapacity());
    EXPECT_FALSE(stats.HasEncodedShaderResourceGroupDraws());
    EXPECT_FALSE(stats.HasShaderResourceGroupArgumentBufferBindingIndex());
    EXPECT_FALSE(stats.HasBoundShaderResourceGroupArgumentBuffer());
    EXPECT_FALSE(stats.HasShaderResourceGroupArgumentBufferBindings());
    EXPECT_FALSE(stats.HasEncodedShaderResourceGroupResources());
    EXPECT_EQ(stats.RequiredShaderResourceGroupEncodedResourceCount(), 0u);
    EXPECT_FALSE(stats.HasCompleteShaderResourceGroupEncoding());
    EXPECT_FALSE(stats.HasCompleteShaderResourceGroupArgumentBufferBinding());
    EXPECT_EQ(stats.GetShaderResourceGroupBinding(0), nullptr);
    EXPECT_TRUE(stats.bindingLayout.HasValidShaderResourceGroupLayout());
    const RHI::ShaderResourceGroupLayoutDesc materialLayout =
        stats.bindingLayout.ToShaderResourceGroupLayoutDesc("renderer_material_layout");
    EXPECT_EQ(materialLayout.bindingCount, kRendererMaterialShaderResourceGroupBindingCount);
    ASSERT_NE(materialLayout.GetBinding(0), nullptr);
    EXPECT_EQ(materialLayout.GetBinding(0)->type, RHI::ShaderResourceBindingType::ConstantBuffer);
    EXPECT_TRUE(RHI::HasShaderStage(materialLayout.GetBinding(0)->shaderStages, RHI::ShaderStage::Vertex));
    EXPECT_TRUE(RHI::HasShaderStage(materialLayout.GetBinding(0)->shaderStages, RHI::ShaderStage::Fragment));
    EXPECT_EQ(materialLayout.GetBinding(0)->bindingIndex, kRendererMaterialUniformBufferIndex);
    ASSERT_NE(materialLayout.GetBinding(1), nullptr);
    EXPECT_EQ(materialLayout.GetBinding(1)->type, RHI::ShaderResourceBindingType::Texture);
    EXPECT_EQ(materialLayout.GetBinding(1)->bindingIndex, kRendererMaterialTextureBindingBaseIndex);
    EXPECT_EQ(materialLayout.GetBinding(1)->bindingCount, kRendererTextureSlotCount);
    ASSERT_NE(materialLayout.GetBinding(2), nullptr);
    EXPECT_EQ(materialLayout.GetBinding(2)->type, RHI::ShaderResourceBindingType::Sampler);
    EXPECT_EQ(materialLayout.GetBinding(2)->bindingIndex, kRendererMaterialSamplerBindingBaseIndex);
    EXPECT_EQ(materialLayout.GetBinding(2)->bindingCount, kRendererTextureSlotCount);
    EXPECT_TRUE(RHI::ValidateShaderResourceGroupLayoutDesc(materialLayout));
    RendererMaterialStats layoutStats;
    layoutStats.SetShaderResourceGroupLayout(materialLayout);
    EXPECT_EQ(layoutStats.shaderResourceGroupBindingCount, kRendererMaterialShaderResourceGroupBindingCount);
    EXPECT_TRUE(layoutStats.HasShaderResourceGroupLayout());
    EXPECT_TRUE(layoutStats.HasCompleteShaderResourceGroupBindingTable());
    EXPECT_TRUE(layoutStats.HasValidShaderResourceGroupLayout());
    EXPECT_FALSE(layoutStats.HasCompleteShaderResourceGroupResources());
    EXPECT_FALSE(layoutStats.HasReadyShaderResourceGroupArgumentEncoder());
    EXPECT_EQ(layoutStats.RequiredShaderResourceGroupEncodedResourceCount(),
              1u + static_cast<uint32_t>(kRendererTextureSlotCount * 2u));
    EXPECT_FALSE(layoutStats.HasCompleteShaderResourceGroupEncoding());
    ASSERT_NE(layoutStats.GetShaderResourceGroupBinding(0), nullptr);
    EXPECT_TRUE(layoutStats.GetShaderResourceGroupBinding(0)->IsConstantBuffer());
    EXPECT_TRUE(layoutStats.GetShaderResourceGroupBinding(0)->HasVertexStage());
    EXPECT_TRUE(layoutStats.GetShaderResourceGroupBinding(0)->HasFragmentStage());
    EXPECT_FALSE(layoutStats.GetShaderResourceGroupBinding(0)->HasComputeStage());
    EXPECT_EQ(layoutStats.GetShaderResourceGroupBinding(0)->bindingIndex,
              kRendererMaterialUniformBufferIndex);
    EXPECT_EQ(layoutStats.GetShaderResourceGroupBinding(0)->bindingCount, 1u);
    EXPECT_EQ(layoutStats.GetShaderResourceGroupBinding(0)->boundResourceCount, 0u);
    EXPECT_FALSE(layoutStats.GetShaderResourceGroupBinding(0)->HasBoundResources());
    EXPECT_TRUE(layoutStats.GetShaderResourceGroupBinding(0)->HasMissingBoundResources());
    EXPECT_TRUE(layoutStats.GetShaderResourceGroupBinding(0)->ContainsBindingIndex(
        kRendererMaterialUniformBufferIndex));
    EXPECT_STREQ(layoutStats.GetShaderResourceGroupBinding(0)->GetDebugName(), "material_uniforms");
    ASSERT_NE(layoutStats.GetShaderResourceGroupBinding(1), nullptr);
    EXPECT_TRUE(layoutStats.GetShaderResourceGroupBinding(1)->IsTexture());
    EXPECT_TRUE(layoutStats.GetShaderResourceGroupBinding(1)->HasFragmentStage());
    EXPECT_EQ(layoutStats.GetShaderResourceGroupBinding(1)->bindingIndex,
              kRendererMaterialTextureBindingBaseIndex);
    EXPECT_EQ(layoutStats.GetShaderResourceGroupBinding(1)->bindingCount,
              kRendererTextureSlotCount);
    EXPECT_TRUE(layoutStats.GetShaderResourceGroupBinding(1)->HasMissingBoundResources());
    EXPECT_EQ(layoutStats.GetShaderResourceGroupBinding(1)->LastBindingIndex(), 4u);
    ASSERT_NE(layoutStats.GetShaderResourceGroupBinding(2), nullptr);
    EXPECT_TRUE(layoutStats.GetShaderResourceGroupBinding(2)->IsSampler());
    EXPECT_TRUE(layoutStats.GetShaderResourceGroupBinding(2)->HasReadyBinding());
    EXPECT_TRUE(layoutStats.GetShaderResourceGroupBinding(2)->HasMissingBoundResources());
    EXPECT_EQ(layoutStats.GetShaderResourceGroupBinding(2)->bindingIndex,
              kRendererMaterialSamplerBindingBaseIndex);
    EXPECT_EQ(layoutStats.GetShaderResourceGroupBinding(3), nullptr);
    layoutStats.SetShaderResourceGroupBoundResourceCount(0, 1);
    layoutStats.SetShaderResourceGroupBoundResourceCount(1, static_cast<uint32_t>(kRendererTextureSlotCount));
    layoutStats.SetShaderResourceGroupBoundResourceCount(2, static_cast<uint32_t>(kRendererTextureSlotCount));
    EXPECT_TRUE(layoutStats.GetShaderResourceGroupBinding(0)->HasCompleteBoundResources());
    EXPECT_TRUE(layoutStats.GetShaderResourceGroupBinding(1)->HasCompleteBoundResources());
    EXPECT_TRUE(layoutStats.GetShaderResourceGroupBinding(2)->HasCompleteBoundResources());
    EXPECT_TRUE(layoutStats.HasCompleteShaderResourceGroupResources());
    layoutStats.shaderResourceGroupArgumentEncoderReady = true;
    layoutStats.shaderResourceGroupArgumentCount = kRendererMaterialShaderResourceGroupBindingCount;
    layoutStats.shaderResourceGroupEncodedLength = 256;
    layoutStats.shaderResourceGroupEncodedStride = 256;
    EXPECT_TRUE(layoutStats.HasShaderResourceGroupArgumentEncoder());
    EXPECT_TRUE(layoutStats.HasShaderResourceGroupArguments());
    EXPECT_TRUE(layoutStats.HasShaderResourceGroupEncodedLength());
    EXPECT_TRUE(layoutStats.HasShaderResourceGroupEncodedStride());
    EXPECT_TRUE(layoutStats.HasReadyShaderResourceGroupArgumentEncoder());
    EXPECT_FALSE(layoutStats.HasCompleteShaderResourceGroupEncoding());
    layoutStats.shaderResourceGroupArgumentBufferReady = true;
    layoutStats.shaderResourceGroupArgumentBufferBytes = layoutStats.shaderResourceGroupEncodedStride * 2u;
    layoutStats.shaderResourceGroupArgumentBufferDrawCapacity = 2;
    layoutStats.shaderResourceGroupEncodedDrawCount = 2;
    layoutStats.shaderResourceGroupArgumentBufferBindingIndex =
        kRendererMaterialShaderResourceGroupArgumentBufferIndex;
    layoutStats.shaderResourceGroupEncodedResourceCount =
        layoutStats.RequiredShaderResourceGroupEncodedResourceCount() *
        layoutStats.shaderResourceGroupEncodedDrawCount;
    EXPECT_TRUE(layoutStats.HasShaderResourceGroupArgumentBuffer());
    EXPECT_TRUE(layoutStats.HasShaderResourceGroupArgumentBufferBytes());
    EXPECT_TRUE(layoutStats.HasShaderResourceGroupArgumentBufferDrawCapacity());
    EXPECT_TRUE(layoutStats.HasEncodedShaderResourceGroupDraws());
    EXPECT_TRUE(layoutStats.HasShaderResourceGroupArgumentBufferBindingIndex());
    EXPECT_TRUE(layoutStats.HasEncodedShaderResourceGroupResources());
    EXPECT_TRUE(layoutStats.HasCompleteShaderResourceGroupEncoding());
    EXPECT_FALSE(layoutStats.HasCompleteShaderResourceGroupArgumentBufferBinding());
    layoutStats.shaderResourceGroupArgumentBufferBound = true;
    layoutStats.shaderResourceGroupArgumentBufferBindCount = 2;
    EXPECT_TRUE(layoutStats.HasBoundShaderResourceGroupArgumentBuffer());
    EXPECT_TRUE(layoutStats.HasShaderResourceGroupArgumentBufferBindings());
    EXPECT_TRUE(layoutStats.HasCompleteShaderResourceGroupArgumentBufferBinding());
    EXPECT_FALSE(stats.HasMaterialActivity());

    stats.materialCapacity = 16;
    stats.materialCount = 4;
    stats.activeMaterial = RendererMaterialHandle{9};
    stats.activeMaterialIndex = 3;
    stats.activeMaterialBoundTextureCount = 2;
    stats.activeMaterialParametersValid = true;
    stats.shaderVisibleTextureCount = 5;
    stats.shaderVisibleSamplerCount = 5;
    stats.fallbackTextureCount = 1;
    stats.SetShaderResourceGroupLayout(materialLayout);
    stats.SetShaderResourceGroupBoundResourceCount(0, 1);
    stats.SetShaderResourceGroupBoundResourceCount(1, stats.shaderVisibleTextureCount);
    stats.SetShaderResourceGroupBoundResourceCount(2, stats.shaderVisibleSamplerCount);
    stats.shaderResourceGroupArgumentEncoderReady = true;
    stats.shaderResourceGroupArgumentCount = kRendererMaterialShaderResourceGroupBindingCount;
    stats.shaderResourceGroupEncodedLength = 256;
    stats.shaderResourceGroupEncodedStride = 256;
    stats.shaderResourceGroupArgumentBufferReady = true;
    stats.shaderResourceGroupArgumentBufferBytes = 256 * 4;
    stats.shaderResourceGroupArgumentBufferDrawCapacity = 4;
    stats.shaderResourceGroupEncodedDrawCount = 4;
    stats.shaderResourceGroupArgumentBufferBindingIndex =
        kRendererMaterialShaderResourceGroupArgumentBufferIndex;
    stats.shaderResourceGroupArgumentBufferBound = true;
    stats.shaderResourceGroupArgumentBufferBindCount = 4;
    stats.shaderResourceGroupEncodedResourceCount =
        (1u + static_cast<uint32_t>(kRendererTextureSlotCount * 2u)) *
        stats.shaderResourceGroupEncodedDrawCount;
    RendererMaterialBindingInfo& normalBinding =
        stats.activeMaterialBindings[RendererTextureSlotIndex(RendererTextureSlot::Normal)];
    normalBinding.slot = RendererTextureSlot::Normal;
    normalBinding.source = RendererMaterialBindingSource::NeutralTexture;
    normalBinding.requestedTexture = RendererTextureHandle{19};
    normalBinding.format = RendererTextureFormat::RGBA8Unorm;
    normalBinding.width = 1;
    normalBinding.height = 1;
    normalBinding.textureBindingIndex = RendererMaterialTextureBindingIndex(RendererTextureSlot::Normal);
    normalBinding.samplerBindingIndex = RendererMaterialSamplerBindingIndex(RendererTextureSlot::Normal);
    normalBinding.uniformBufferIndex = kRendererMaterialUniformBufferIndex;
    normalBinding.textureReady = true;
    normalBinding.samplerReady = true;
    stats.ready = true;
    EXPECT_TRUE(stats.HasMaterialCapacity());
    EXPECT_TRUE(stats.HasMaterials());
    EXPECT_TRUE(stats.HasActiveMaterial());
    EXPECT_TRUE(stats.HasActiveMaterialSlot());
    EXPECT_TRUE(stats.HasFreeMaterialSlots());
    EXPECT_FALSE(stats.IsMaterialTableFull());
    EXPECT_TRUE(stats.HasActiveBoundTextures());
    EXPECT_FALSE(stats.HasCompleteActiveTextureSet());
    EXPECT_TRUE(stats.HasValidActiveParameters());
    EXPECT_TRUE(stats.HasShaderVisibleTextures());
    EXPECT_TRUE(stats.HasShaderVisibleSamplers());
    EXPECT_TRUE(stats.HasFallbackTextures());
    EXPECT_TRUE(stats.HasCompleteShaderBindings());
    EXPECT_TRUE(stats.HasShaderResourceGroupLayout());
    EXPECT_TRUE(stats.HasCompleteShaderResourceGroupBindingTable());
    EXPECT_TRUE(stats.HasValidShaderResourceGroupLayout());
    EXPECT_TRUE(stats.HasCompleteShaderResourceGroupResources());
    EXPECT_TRUE(stats.HasReadyShaderResourceGroupArgumentEncoder());
    EXPECT_TRUE(stats.HasCompleteShaderResourceGroupEncoding());
    EXPECT_TRUE(stats.HasCompleteShaderResourceGroupArgumentBufferBinding());
    EXPECT_TRUE(stats.HasMaterialActivity());
    binding = stats.GetActiveMaterialBinding(RendererTextureSlot::Normal);
    EXPECT_TRUE(binding.HasRequestedTexture());
    EXPECT_FALSE(binding.HasBoundTexture());
    EXPECT_TRUE(binding.HasFormat());
    EXPECT_FALSE(binding.HasSourceAsset());
    EXPECT_TRUE(binding.HasDimensions());
    EXPECT_EQ(binding.textureBindingIndex, RendererMaterialTextureBindingIndex(RendererTextureSlot::Normal));
    EXPECT_EQ(binding.samplerBindingIndex, RendererMaterialSamplerBindingIndex(RendererTextureSlot::Normal));
    EXPECT_EQ(binding.uniformBufferIndex, kRendererMaterialUniformBufferIndex);
    EXPECT_TRUE(binding.HasTextureBindingIndex());
    EXPECT_TRUE(binding.HasSamplerBindingIndex());
    EXPECT_TRUE(binding.HasUniformBufferIndex());
    EXPECT_TRUE(binding.HasBindingIndices());
    EXPECT_TRUE(binding.HasTextureReady());
    EXPECT_TRUE(binding.HasSamplerReady());
    EXPECT_FALSE(binding.UsesMaterialTexture());
    EXPECT_FALSE(binding.UsesActiveSlotTexture());
    EXPECT_TRUE(binding.UsesNeutralTexture());
    EXPECT_TRUE(binding.UsesFallbackTexture());
    EXPECT_TRUE(binding.IsShaderVisible());
    EXPECT_TRUE(binding.HasBindingActivity());

    stats.materialCount = stats.materialCapacity;
    stats.activeMaterialBoundTextureCount = static_cast<uint32_t>(kRendererTextureSlotCount);
    stats.activeMaterialCompleteTextureSet = true;
    EXPECT_FALSE(stats.HasFreeMaterialSlots());
    EXPECT_TRUE(stats.IsMaterialTableFull());
    EXPECT_TRUE(stats.HasCompleteActiveTextureSet());
}

TEST(RendererApiTest, TextureInfoHelpersExposeIdentityAndActiveState) {
    RendererTextureInfo info;
    EXPECT_FALSE(info.HasTexture());
    EXPECT_FALSE(info.HasFormat());
    EXPECT_FALSE(info.HasSourceAsset());
    EXPECT_FALSE(info.HasDimensions());
    EXPECT_FALSE(info.HasPendingUpload());
    EXPECT_FALSE(info.HasReadyTexture());
    EXPECT_FALSE(info.HasActiveTexture());
    EXPECT_FALSE(info.HasTextureActivity());

    info.texture = RendererTextureHandle{51};
    info.format = RendererTextureFormat::RGBA8Unorm;
    info.sourceAssetId = 91;
    info.width = 16;
    info.height = 8;
    info.uploadPending = true;
    EXPECT_TRUE(info.HasTexture());
    EXPECT_TRUE(info.HasFormat());
    EXPECT_TRUE(info.HasSourceAsset());
    EXPECT_TRUE(info.HasDimensions());
    EXPECT_TRUE(info.HasPendingUpload());
    EXPECT_FALSE(info.HasReadyTexture());
    EXPECT_FALSE(info.HasActiveTexture());
    EXPECT_TRUE(info.HasTextureActivity());

    info.active = true;
    info.uploadPending = false;
    EXPECT_TRUE(info.HasReadyTexture());
    EXPECT_TRUE(info.HasActiveTexture());
    EXPECT_TRUE(info.HasTextureActivity());

    info.width = 0;
    EXPECT_FALSE(info.HasDimensions());
    EXPECT_FALSE(info.HasReadyTexture());
    EXPECT_FALSE(info.HasActiveTexture());
}

TEST(RendererApiTest, ActiveTextureSlotInfoHelpersExposeResidentSlotState) {
    RendererActiveTextureSlotInfo info;
    EXPECT_FALSE(info.HasTexture());
    EXPECT_FALSE(info.HasSourceAsset());
    EXPECT_FALSE(info.HasDimensions());
    EXPECT_FALSE(info.HasReadyTexture());
    EXPECT_FALSE(info.HasActiveTexture());

    info.texture = RendererTextureHandle{52};
    info.width = 4;
    info.height = 4;
    info.active = true;
    EXPECT_TRUE(info.HasTexture());
    EXPECT_FALSE(info.HasSourceAsset());
    EXPECT_TRUE(info.HasDimensions());
    EXPECT_TRUE(info.HasReadyTexture());
    EXPECT_TRUE(info.HasActiveTexture());

    info.sourceAssetId = 92;
    EXPECT_TRUE(info.HasSourceAsset());

    info.active = false;
    EXPECT_TRUE(info.HasReadyTexture());
    EXPECT_FALSE(info.HasActiveTexture());
}

TEST(RendererApiTest, TextureUploadStatsExposeActiveTexturePerMaterialSlot) {
    RendererTextureUploadStats stats;
    EXPECT_EQ(stats.FinishedUploadCount(), 0u);
    EXPECT_FALSE(stats.HasQueuedUploads());
    EXPECT_FALSE(stats.HasCompletedUploads());
    EXPECT_FALSE(stats.HasFailedUploads());
    EXPECT_FALSE(stats.HasFinishedUploads());
    EXPECT_FALSE(stats.HasLastQueuedUpload());
    EXPECT_FALSE(stats.HasLastCompletedUpload());
    EXPECT_FALSE(stats.HasLastFailedUpload());
    EXPECT_FALSE(stats.HasLastQueuedTexture());
    EXPECT_FALSE(stats.HasLastCompletedTexture());
    EXPECT_FALSE(stats.HasLastFailedTexture());
    EXPECT_FALSE(stats.HasPendingMaterialTextureUploads());
    EXPECT_FALSE(stats.HasPendingBaseColorUpload());
    EXPECT_FALSE(stats.HasPendingUploads());
    EXPECT_FALSE(stats.HasActiveMaterialTextures());
    EXPECT_FALSE(stats.HasActiveBaseColorTexture());
    EXPECT_FALSE(stats.HasActiveBaseColorSourceAsset());
    EXPECT_FALSE(stats.HasTextureUploadActivity());

    stats.baseColorUploadPending = true;
    EXPECT_TRUE(stats.HasPendingBaseColorUpload());
    EXPECT_TRUE(stats.HasPendingUploads());
    EXPECT_TRUE(stats.HasTextureUploadActivity());
    stats.baseColorUploadPending = false;

    stats.queuedUploads = 5;
    stats.completedUploads = 3;
    stats.failedUploads = 1;
    stats.lastQueuedUpload = 5;
    stats.lastCompletedUpload = 4;
    stats.lastFailedUpload = 2;
    stats.lastQueuedTexture = RendererTextureHandle{55};
    stats.lastCompletedTexture = RendererTextureHandle{44};
    stats.lastFailedTexture = RendererTextureHandle{22};
    stats.materialTextureUploadPending = true;
    stats.activeMaterialTextureCount = static_cast<uint32_t>(kMaterialSlots.size());
    stats.activeBaseColorTexture = RendererTextureHandle{99};
    stats.activeBaseColorSourceAssetId = 199;
    stats.activeBaseColorWidth = 16;
    stats.activeBaseColorHeight = 16;
    EXPECT_EQ(stats.FinishedUploadCount(), 4u);
    EXPECT_TRUE(stats.HasQueuedUploads());
    EXPECT_TRUE(stats.HasCompletedUploads());
    EXPECT_TRUE(stats.HasFailedUploads());
    EXPECT_TRUE(stats.HasFinishedUploads());
    EXPECT_TRUE(stats.HasLastQueuedUpload());
    EXPECT_TRUE(stats.HasLastCompletedUpload());
    EXPECT_TRUE(stats.HasLastFailedUpload());
    EXPECT_TRUE(stats.HasLastQueuedTexture());
    EXPECT_TRUE(stats.HasLastCompletedTexture());
    EXPECT_TRUE(stats.HasLastFailedTexture());
    EXPECT_TRUE(stats.HasPendingMaterialTextureUploads());
    EXPECT_TRUE(stats.HasPendingUploads());
    EXPECT_TRUE(stats.HasActiveMaterialTextures());
    EXPECT_TRUE(stats.HasActiveBaseColorTexture());
    EXPECT_TRUE(stats.HasActiveBaseColorSourceAsset());
    EXPECT_TRUE(stats.HasTextureUploadActivity());

    for (size_t i = 0; i < kMaterialSlots.size(); ++i) {
        RendererActiveTextureSlotInfo& info = stats.activeMaterialTextures[i];
        info.texture = RendererTextureHandle{200 + i};
        info.sourceAssetId = 300 + i;
        info.width = static_cast<uint32_t>(4 + i);
        info.height = static_cast<uint32_t>(8 + i);
        info.active = true;
    }

    for (size_t i = 0; i < kMaterialSlots.size(); ++i) {
        const RendererActiveTextureSlotInfo info = stats.GetActiveTexture(kMaterialSlots[i]);
        EXPECT_TRUE(info.active);
        EXPECT_TRUE(info.HasTexture());
        EXPECT_TRUE(info.HasSourceAsset());
        EXPECT_TRUE(info.HasDimensions());
        EXPECT_TRUE(info.HasActiveTexture());
        EXPECT_EQ(info.texture.id, 200u + i);
        EXPECT_EQ(info.sourceAssetId, 300u + i);
        EXPECT_EQ(info.width, 4u + i);
        EXPECT_EQ(info.height, 8u + i);
    }
}

TEST(RendererApiTest, TextureUploadStatsReturnEmptyInfoForUnknownTextureSlot) {
    RendererTextureUploadStats stats;
    stats.activeMaterialTextures[0].texture = RendererTextureHandle{1};
    stats.activeMaterialTextures[0].active = true;

    const RendererTextureSlot unknownSlot = static_cast<RendererTextureSlot>(255);
    const RendererActiveTextureSlotInfo info = stats.GetActiveTexture(unknownSlot);

    EXPECT_FALSE(info.active);
    EXPECT_FALSE(info.HasTexture());
    EXPECT_FALSE(info.HasSourceAsset());
    EXPECT_FALSE(info.HasDimensions());
    EXPECT_FALSE(info.HasActiveTexture());
    EXPECT_FALSE(info.texture);
    EXPECT_EQ(info.sourceAssetId, 0u);
    EXPECT_EQ(info.width, 0u);
    EXPECT_EQ(info.height, 0u);
}

TEST(RendererApiTest, ResourcePoolStatsExposeBufferTextureAggregates) {
    RendererResourcePoolStats stats;
    EXPECT_EQ(stats.buffers.resourceType, RHI::ResourceType::Buffer);
    EXPECT_EQ(stats.textures.resourceType, RHI::ResourceType::Texture);
    EXPECT_EQ(stats.FindPoolStats(RHI::ResourceType::Buffer), &stats.buffers);
    EXPECT_EQ(stats.FindPoolStats(RHI::ResourceType::Texture), &stats.textures);
    EXPECT_EQ(stats.FindPoolStats(RHI::ResourceType::Sampler), nullptr);
    EXPECT_FALSE(stats.HasLiveResources());
    EXPECT_FALSE(stats.HasPeakResources());
    EXPECT_FALSE(stats.HasMemoryBudgets());
    EXPECT_FALSE(stats.IsAnyOverBudget());
    EXPECT_FALSE(stats.HasFailedAllocations());
    EXPECT_FALSE(stats.HasPoolActivity());

    const RendererResourcePoolStats& constStats = stats;
    EXPECT_EQ(constStats.FindPoolStats(RHI::ResourceType::Texture),
              static_cast<const RHI::ResourcePoolStats*>(&stats.textures));

    stats.buffers.liveResourceCount = 2;
    stats.buffers.liveBytes = 4096;
    stats.buffers.peakResourceCount = 4;
    stats.buffers.peakBytes = 8192;
    stats.buffers.failedAllocationCount = 1;
    stats.buffers.failedAllocationBytes = 512;

    stats.textures.liveResourceCount = 3;
    stats.textures.liveBytes = 16384;
    stats.textures.peakResourceCount = 5;
    stats.textures.peakBytes = 32768;
    stats.textures.failedAllocationCount = 2;
    stats.textures.failedAllocationBytes = 1024;

    EXPECT_EQ(stats.TotalLiveResourceCount(), 5u);
    EXPECT_EQ(stats.TotalLiveBytes(), 20480u);
    EXPECT_EQ(stats.TotalPeakResourceCount(), 9u);
    EXPECT_EQ(stats.TotalPeakBytes(), 40960u);
    EXPECT_EQ(stats.TotalFailedAllocationCount(), 3u);
    EXPECT_EQ(stats.TotalFailedAllocationBytes(), 1536u);
    EXPECT_TRUE(stats.HasLiveResources());
    EXPECT_TRUE(stats.HasPeakResources());
    EXPECT_TRUE(stats.HasFailedAllocations());
    EXPECT_TRUE(stats.HasPoolActivity());

    stats.buffers.liveResourceCount = std::numeric_limits<size_t>::max() - 1u;
    stats.textures.liveResourceCount = 4;
    stats.buffers.liveBytes = std::numeric_limits<uint64_t>::max() - 8u;
    stats.textures.liveBytes = 16;
    EXPECT_EQ(stats.TotalLiveResourceCount(), std::numeric_limits<size_t>::max());
    EXPECT_EQ(stats.TotalLiveBytes(), std::numeric_limits<uint64_t>::max());
}

TEST(RendererApiTest, ResourcePoolStatsExposeTypedMemoryBucketQueries) {
    RendererResourcePoolStats stats;

    RHI::ResourcePoolMemoryStats* mutableTextureShared =
        stats.FindMemoryStats(RHI::ResourceType::Texture, RHI::ResourceMemory::Shared);
    ASSERT_NE(mutableTextureShared, nullptr);
    mutableTextureShared->liveResourceCount = 2;
    mutableTextureShared->liveBytes = 3072;
    mutableTextureShared->budgetBytes = 4096;
    mutableTextureShared->failedAllocationCount = 1;

    const RendererResourcePoolStats& constStats = stats;
    const RHI::ResourcePoolMemoryStats* textureShared =
        constStats.FindMemoryStats(RHI::ResourceType::Texture, RHI::ResourceMemory::Shared);
    ASSERT_NE(textureShared, nullptr);
    EXPECT_EQ(textureShared->liveResourceCount, 2u);
    EXPECT_EQ(textureShared->liveBytes, 3072u);
    EXPECT_EQ(textureShared->budgetBytes, 4096u);
    EXPECT_EQ(textureShared->failedAllocationCount, 1u);
    EXPECT_EQ(textureShared, static_cast<const RHI::ResourcePoolMemoryStats*>(&stats.textures.shared));

    EXPECT_EQ(stats.FindMemoryStats(RHI::ResourceType::Sampler, RHI::ResourceMemory::Shared), nullptr);
    EXPECT_EQ(stats.FindMemoryStats(RHI::ResourceType::Texture, static_cast<RHI::ResourceMemory>(255)), nullptr);
    EXPECT_TRUE(stats.HasMemoryBudget(RHI::ResourceType::Texture, RHI::ResourceMemory::Shared));
    EXPECT_TRUE(stats.HasMemoryBudgets());
    EXPECT_FALSE(stats.HasMemoryBudget(RHI::ResourceType::Buffer, RHI::ResourceMemory::Shared));
    EXPECT_FALSE(stats.HasMemoryBudget(RHI::ResourceType::Sampler, RHI::ResourceMemory::Shared));
    EXPECT_FALSE(stats.IsAnyOverBudget());

    uint64_t remainingBytes = 99;
    EXPECT_TRUE(stats.BudgetRemaining(RHI::ResourceType::Texture, RHI::ResourceMemory::Shared, remainingBytes));
    EXPECT_EQ(remainingBytes, 1024u);
    EXPECT_FALSE(stats.IsOverBudget(RHI::ResourceType::Texture, RHI::ResourceMemory::Shared));

    mutableTextureShared->liveBytes = 8192;
    remainingBytes = 99;
    EXPECT_TRUE(stats.BudgetRemaining(RHI::ResourceType::Texture, RHI::ResourceMemory::Shared, remainingBytes));
    EXPECT_EQ(remainingBytes, 0u);
    EXPECT_TRUE(stats.IsOverBudget(RHI::ResourceType::Texture, RHI::ResourceMemory::Shared));
    EXPECT_TRUE(stats.IsAnyOverBudget());

    remainingBytes = 99;
    EXPECT_FALSE(stats.BudgetRemaining(RHI::ResourceType::Buffer, RHI::ResourceMemory::Shared, remainingBytes));
    EXPECT_EQ(remainingBytes, 0u);
    EXPECT_FALSE(stats.IsOverBudget(RHI::ResourceType::Buffer, RHI::ResourceMemory::Shared));

    remainingBytes = 99;
    EXPECT_FALSE(stats.BudgetRemaining(RHI::ResourceType::Sampler, RHI::ResourceMemory::Shared, remainingBytes));
    EXPECT_EQ(remainingBytes, 0u);
    EXPECT_FALSE(stats.IsOverBudget(RHI::ResourceType::Sampler, RHI::ResourceMemory::Shared));
}

TEST(RendererApiTest, ResourcePoolBudgetDescTargetsTypedMemoryBuckets) {
    RendererResourcePoolBudgetDesc budget;
    EXPECT_EQ(budget.resourceType, RHI::ResourceType::Unknown);
    EXPECT_EQ(budget.memory, RHI::ResourceMemory::DeviceLocal);
    EXPECT_EQ(budget.budgetBytes, 0u);

    budget.resourceType = RHI::ResourceType::Texture;
    budget.memory = RHI::ResourceMemory::Shared;
    budget.budgetBytes = 4096;

    EXPECT_EQ(budget.resourceType, RHI::ResourceType::Texture);
    EXPECT_EQ(budget.memory, RHI::ResourceMemory::Shared);
    EXPECT_EQ(budget.budgetBytes, 4096u);
}

} // namespace testing
} // namespace Next
