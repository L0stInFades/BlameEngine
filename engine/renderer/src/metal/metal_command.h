#pragma once

#include "metal_frame_graph.h"
#include "next/rhi/command.h"

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

namespace Next {
namespace MetalBackend {

class MetalDevice;

struct MetalCommandContextStats {
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
    bool HasFailures() const {
        return beginFailureCount != 0 || renderPassFailureCount != 0 || commitFailureCount != 0 ||
            presentFailureCount != 0 || frameGraphPassFailureCount != 0 ||
            frameGraphResourceUseFailureCount != 0;
    }
};

class MetalCommandContext final : public RHI::CommandContext {
public:
    bool Begin(MetalDevice& device, RHI::QueueClass queueClass = RHI::QueueClass::Graphics);
    bool EncodeFrameGraphPassTransitions(const RHI::FrameGraphCompileResult& compileResult,
                                         uint32_t passIndex);
    bool EncodeFrameGraphRenderPassResourceUsages(
        id<MTLRenderCommandEncoder> encoder,
        const RHI::FrameGraphCompileResult& compileResult,
        const MetalFrameGraphResourceUsageTable& resourceUsages,
        uint32_t passIndex);
    id<MTLRenderCommandEncoder> BeginRenderPass(MTLRenderPassDescriptor* passDescriptor);
    void EndRenderPass(id<MTLRenderCommandEncoder> encoder);
    bool Commit();
    bool PresentAndCommit(id<CAMetalDrawable> drawable);
    void Reset();

    RHI::QueueClass GetQueueClass() const override { return queueClass_; }
    bool IsRecording() const override { return recording_; }
    uint64_t GetSubmittedFrameIndex() const override { return submittedFrameIndex_; }

    id<MTLCommandBuffer> NativeCommandBuffer() const { return commandBuffer_; }
    MetalCommandContextStats GetStats() const;

private:
    bool Begin(id<MTLCommandQueue> queue, RHI::QueueClass queueClass);

    id<MTLCommandBuffer> commandBuffer_ = nil;
    uint64_t submittedFrameIndex_ = 0;
    RHI::QueueClass queueClass_ = RHI::QueueClass::Graphics;
    bool recording_ = false;
    MetalCommandContextStats stats_;
};

} // namespace MetalBackend
} // namespace Next
