#include "metal_command.h"

#include "metal_device.h"
#include "next/foundation/logger.h"

namespace Next {
namespace MetalBackend {
namespace {

void AccumulateFrameGraphTransition(MetalCommandContextStats& stats,
                                    const RHI::FrameGraphTransition& transition) {
    switch (transition.after) {
        case RHI::ResourceState::RenderTarget:
        case RHI::ResourceState::DepthWrite:
            ++stats.frameGraphAttachmentTransitionCount;
            break;
        case RHI::ResourceState::VertexBuffer:
        case RHI::ResourceState::IndexBuffer:
        case RHI::ResourceState::ConstantBuffer:
            ++stats.frameGraphBufferTransitionCount;
            break;
        case RHI::ResourceState::ShaderRead:
        case RHI::ResourceState::ShaderWrite:
            ++stats.frameGraphShaderTransitionCount;
            break;
        case RHI::ResourceState::CopySource:
        case RHI::ResourceState::CopyDestination:
            ++stats.frameGraphCopyTransitionCount;
            break;
        case RHI::ResourceState::Present:
            ++stats.frameGraphPresentTransitionCount;
            break;
        default:
            ++stats.frameGraphOtherTransitionCount;
            break;
    }
}

void AccumulateFrameGraphAccess(MetalCommandContextStats& stats,
                                const RHI::FrameGraphCompiledAccess& access) {
    switch (access.state) {
        case RHI::ResourceState::RenderTarget:
        case RHI::ResourceState::DepthWrite:
            ++stats.frameGraphAttachmentAccessCount;
            break;
        case RHI::ResourceState::VertexBuffer:
        case RHI::ResourceState::IndexBuffer:
        case RHI::ResourceState::ConstantBuffer:
            ++stats.frameGraphBufferAccessCount;
            break;
        case RHI::ResourceState::ShaderRead:
        case RHI::ResourceState::ShaderWrite:
            ++stats.frameGraphShaderAccessCount;
            break;
        case RHI::ResourceState::CopySource:
        case RHI::ResourceState::CopyDestination:
            ++stats.frameGraphCopyAccessCount;
            break;
        case RHI::ResourceState::Present:
            ++stats.frameGraphPresentAccessCount;
            break;
        default:
            ++stats.frameGraphOtherAccessCount;
            break;
    }

    if (access.HasShaderStages()) {
        ++stats.frameGraphShaderStageHintAccessCount;
        if (RHI::HasShaderStage(access.shaderStages, RHI::ShaderStage::Vertex)) {
            ++stats.frameGraphVertexStageHintAccessCount;
        }
        if (RHI::HasShaderStage(access.shaderStages, RHI::ShaderStage::Fragment)) {
            ++stats.frameGraphFragmentStageHintAccessCount;
        }
        if (RHI::HasShaderStage(access.shaderStages, RHI::ShaderStage::Compute)) {
            ++stats.frameGraphComputeStageHintAccessCount;
        }
    }
}

bool ShouldDeclareFrameGraphResourceUse(const RHI::FrameGraphCompiledAccess& access) {
    switch (access.state) {
        case RHI::ResourceState::RenderTarget:
        case RHI::ResourceState::DepthWrite:
        case RHI::ResourceState::Present:
            return false;
        default:
            return true;
    }
}

MTLResourceUsage ToMetalResourceUsage(RHI::FrameGraphAccessType access) {
    switch (access) {
        case RHI::FrameGraphAccessType::Write:
            return MTLResourceUsageWrite;
        case RHI::FrameGraphAccessType::ReadWrite:
            return MTLResourceUsageRead | MTLResourceUsageWrite;
        case RHI::FrameGraphAccessType::Read:
        default:
            return MTLResourceUsageRead;
    }
}

MTLRenderStages ToMetalRenderStages(const RHI::FrameGraphCompiledAccess& access) {
    if (access.HasShaderStages()) {
        MTLRenderStages stages = 0;
        if (RHI::HasShaderStage(access.shaderStages, RHI::ShaderStage::Vertex)) {
            stages |= MTLRenderStageVertex;
        }
        if (RHI::HasShaderStage(access.shaderStages, RHI::ShaderStage::Fragment)) {
            stages |= MTLRenderStageFragment;
        }
        if (stages != 0) {
            return stages;
        }
    }

    switch (access.state) {
        case RHI::ResourceState::VertexBuffer:
        case RHI::ResourceState::IndexBuffer:
            return MTLRenderStageVertex;
        case RHI::ResourceState::ShaderRead:
        case RHI::ResourceState::ShaderWrite:
            return MTLRenderStageFragment;
        case RHI::ResourceState::ConstantBuffer:
        default:
            return MTLRenderStageVertex | MTLRenderStageFragment;
    }
}

const char* FrameGraphCompileTableStateName(bool complete) {
    return complete ? "complete" : "incomplete";
}

bool HasCompleteFrameGraphCompileTables(const RHI::FrameGraphCompileResult& compileResult) {
    return compileResult.HasCompleteCompileTables();
}

void LogInvalidFrameGraphCompileResult(const char* scope,
                                       const RHI::FrameGraphCompileResult& compileResult) {
    NEXT_LOG_ERROR("%s: invalid compile result (%s resourceTable=%s dependencyTable=%s accessTable=%s transitionTable=%s passTable=%s)",
                   scope,
                   RHI::FrameGraphDescriptorErrorName(compileResult.validation.error),
                   FrameGraphCompileTableStateName(compileResult.HasCompleteResourceTable()),
                   FrameGraphCompileTableStateName(compileResult.HasCompleteDependencyTable()),
                   FrameGraphCompileTableStateName(compileResult.HasCompleteAccessTable()),
                   FrameGraphCompileTableStateName(compileResult.HasCompleteTransitionTable()),
                   FrameGraphCompileTableStateName(compileResult.HasCompletePassTable()));
}

void LogInvalidFrameGraphResourceUsageTable(
    const char* scope,
    const MetalFrameGraphResourceUsageTable& resourceUsages,
    const RHI::FrameGraphCompileResult& compileResult) {
    NEXT_LOG_ERROR("%s: invalid resource usage table (resourceTable=%s compileMatch=%s resources=%u compiledResources=%u capacity=%llu)",
                   scope,
                   FrameGraphCompileTableStateName(resourceUsages.HasCompleteResourceTable()),
                   FrameGraphCompileTableStateName(resourceUsages.MatchesCompileResult(compileResult)),
                   resourceUsages.resourceCount,
                   compileResult.resourceCount,
                   static_cast<unsigned long long>(resourceUsages.resources.size()));
}

} // namespace

bool MetalCommandContext::Begin(MetalDevice& device, RHI::QueueClass queueClass) {
    id<MTLCommandQueue> queue = device.QueueForClass(queueClass);
    if (!queue) {
        ++stats_.beginAttemptCount;
        ++stats_.beginFailureCount;
        NEXT_LOG_ERROR("Metal command context begin failed: no %s queue",
                       RHI::QueueClassName(queueClass));
        return false;
    }
    return Begin(queue, queueClass);
}

bool MetalCommandContext::Begin(id<MTLCommandQueue> queue, RHI::QueueClass queueClass) {
    ++stats_.beginAttemptCount;
    if (!queue || recording_) {
        ++stats_.beginFailureCount;
        return false;
    }

    commandBuffer_ = [queue commandBuffer];
    if (!commandBuffer_) {
        ++stats_.beginFailureCount;
        NEXT_LOG_ERROR("Failed to allocate Metal command buffer");
        return false;
    }

    queueClass_ = queueClass;
    recording_ = true;
    ++stats_.begunCommandBufferCount;
    return true;
}

bool MetalCommandContext::EncodeFrameGraphPassTransitions(
    const RHI::FrameGraphCompileResult& compileResult,
    uint32_t passIndex) {
    ++stats_.frameGraphPassAttemptCount;
    if (!recording_ || !commandBuffer_) {
        ++stats_.frameGraphPassFailureCount;
        return false;
    }
    if (!compileResult || !HasCompleteFrameGraphCompileTables(compileResult)) {
        ++stats_.frameGraphPassFailureCount;
        LogInvalidFrameGraphCompileResult("Metal frame graph pass encoding failed",
                                          compileResult);
        return false;
    }

    const RHI::FrameGraphPassCompileInfo* passInfo = compileResult.GetPass(passIndex);
    if (!passInfo) {
        ++stats_.frameGraphPassFailureCount;
        NEXT_LOG_ERROR("Metal frame graph pass encoding failed: invalid pass %u", passIndex);
        return false;
    }
    if (passInfo->queueClass != queueClass_) {
        ++stats_.frameGraphPassFailureCount;
        NEXT_LOG_ERROR("Metal frame graph pass %u requested on %s queue while recording %s queue",
                       passIndex,
                       RHI::QueueClassName(passInfo->queueClass),
                       RHI::QueueClassName(queueClass_));
        return false;
    }

    for (uint32_t i = 0; i < passInfo->dependencyCount; ++i) {
        const uint32_t dependencyIndex = passInfo->dependencyOffset + i;
        const RHI::FrameGraphCompiledPassDependency* dependency =
            compileResult.GetDependency(dependencyIndex);
        if (!dependency || dependency->passIndex != passIndex ||
            dependency->dependencyPassIndex >= passIndex) {
            ++stats_.frameGraphPassFailureCount;
            NEXT_LOG_ERROR("Metal frame graph pass %u has invalid dependency range at %u",
                           passIndex,
                           dependencyIndex);
            return false;
        }
    }
    for (uint32_t i = 0; i < passInfo->transitionCount; ++i) {
        const uint32_t transitionIndex = passInfo->transitionOffset + i;
        const RHI::FrameGraphTransition* transition = compileResult.GetTransition(transitionIndex);
        if (!transition || transition->passIndex != passIndex ||
            transition->queueClass != passInfo->queueClass) {
            ++stats_.frameGraphPassFailureCount;
            NEXT_LOG_ERROR("Metal frame graph pass %u has invalid transition range at %u",
                           passIndex,
                           transitionIndex);
            return false;
        }
    }
    for (uint32_t i = 0; i < passInfo->accessCount; ++i) {
        const uint32_t compiledAccessIndex = passInfo->accessOffset + i;
        const RHI::FrameGraphCompiledAccess* access = compileResult.GetAccess(compiledAccessIndex);
        if (!access || access->passIndex != passIndex ||
            access->queueClass != passInfo->queueClass) {
            ++stats_.frameGraphPassFailureCount;
            NEXT_LOG_ERROR("Metal frame graph pass %u has invalid access range at %u",
                           passIndex,
                           compiledAccessIndex);
            return false;
        }
    }

    ++stats_.frameGraphPassEncodedCount;
    stats_.frameGraphDependencyEncodedCount += passInfo->dependencyCount;
    stats_.frameGraphAccessEncodedCount += passInfo->accessCount;
    stats_.frameGraphTransitionEncodedCount += passInfo->transitionCount;
    for (uint32_t i = 0; i < passInfo->transitionCount; ++i) {
        const uint32_t transitionIndex = passInfo->transitionOffset + i;
        AccumulateFrameGraphTransition(stats_, *compileResult.GetTransition(transitionIndex));
    }
    for (uint32_t i = 0; i < passInfo->accessCount; ++i) {
        const uint32_t compiledAccessIndex = passInfo->accessOffset + i;
        AccumulateFrameGraphAccess(stats_, *compileResult.GetAccess(compiledAccessIndex));
    }
    stats_.lastFrameGraphPassIndex = passIndex;
    stats_.lastFrameGraphDependencyOffset = passInfo->dependencyOffset;
    stats_.lastFrameGraphDependencyCount = passInfo->dependencyCount;
    stats_.lastFrameGraphTransitionOffset = passInfo->transitionOffset;
    stats_.lastFrameGraphTransitionCount = passInfo->transitionCount;
    stats_.lastFrameGraphAccessOffset = passInfo->accessOffset;
    stats_.lastFrameGraphAccessCount = passInfo->accessCount;
    stats_.lastFrameGraphPassQueueClass = passInfo->queueClass;
    return true;
}

bool MetalCommandContext::EncodeFrameGraphRenderPassResourceUsages(
    id<MTLRenderCommandEncoder> encoder,
    const RHI::FrameGraphCompileResult& compileResult,
    const MetalFrameGraphResourceUsageTable& resourceUsages,
    uint32_t passIndex) {
    ++stats_.frameGraphResourceUseAttemptCount;
    if (!recording_ || !commandBuffer_ || !encoder) {
        ++stats_.frameGraphResourceUseFailureCount;
        return false;
    }
    if (!compileResult || !HasCompleteFrameGraphCompileTables(compileResult)) {
        ++stats_.frameGraphResourceUseFailureCount;
        LogInvalidFrameGraphCompileResult("Metal frame graph resource use failed",
                                          compileResult);
        return false;
    }
    if (!resourceUsages.HasCompleteResourceTable() ||
        !resourceUsages.MatchesCompileResult(compileResult)) {
        ++stats_.frameGraphResourceUseFailureCount;
        LogInvalidFrameGraphResourceUsageTable("Metal frame graph resource use failed",
                                               resourceUsages,
                                               compileResult);
        return false;
    }

    const RHI::FrameGraphPassCompileInfo* passInfo = compileResult.GetPass(passIndex);
    if (!passInfo) {
        ++stats_.frameGraphResourceUseFailureCount;
        NEXT_LOG_ERROR("Metal frame graph resource use failed: invalid pass %u", passIndex);
        return false;
    }
    if (passInfo->queueClass != queueClass_) {
        ++stats_.frameGraphResourceUseFailureCount;
        NEXT_LOG_ERROR("Metal frame graph resource use pass %u requested on %s queue while recording %s queue",
                       passIndex,
                       RHI::QueueClassName(passInfo->queueClass),
                       RHI::QueueClassName(queueClass_));
        return false;
    }

    uint64_t declaredCount = 0;
    uint64_t skippedCount = 0;
    uint64_t bufferCount = 0;
    uint64_t textureCount = 0;
    uint64_t vertexStageCount = 0;
    uint64_t fragmentStageCount = 0;
    for (uint32_t i = 0; i < passInfo->accessCount; ++i) {
        const uint32_t compiledAccessIndex = passInfo->accessOffset + i;
        const RHI::FrameGraphCompiledAccess* access = compileResult.GetAccess(compiledAccessIndex);
        if (!access || access->passIndex != passIndex ||
            access->queueClass != passInfo->queueClass) {
            ++stats_.frameGraphResourceUseFailureCount;
            NEXT_LOG_ERROR("Metal frame graph resource use pass %u has invalid access range at %u",
                           passIndex,
                           compiledAccessIndex);
            return false;
        }
        if (!ShouldDeclareFrameGraphResourceUse(*access)) {
            ++skippedCount;
            continue;
        }

        const MetalFrameGraphResourceUsage* resourceUse =
            resourceUsages.GetResource(access->resourceIndex);
        if (!resourceUse || !resourceUse->IsValid()) {
            ++stats_.frameGraphResourceUseFailureCount;
            NEXT_LOG_ERROR("Metal frame graph resource use pass %u missing native resource %u",
                           passIndex,
                           access->resourceIndex);
            return false;
        }
        if (!resourceUse->MatchesNativeType()) {
            ++stats_.frameGraphResourceUseFailureCount;
            NEXT_LOG_ERROR("Metal frame graph resource use pass %u native resource %u type mismatch: %s",
                           passIndex,
                           access->resourceIndex,
                           RHI::ResourceTypeName(resourceUse->type));
            return false;
        }
        const RHI::ResourceType compiledResourceType =
            compileResult.GetResourceType(access->resourceIndex);
        if (compiledResourceType != resourceUse->type) {
            ++stats_.frameGraphResourceUseFailureCount;
            NEXT_LOG_ERROR("Metal frame graph resource use pass %u compiled resource %u type mismatch: compiled=%s native=%s",
                           passIndex,
                           access->resourceIndex,
                           RHI::ResourceTypeName(compiledResourceType),
                           RHI::ResourceTypeName(resourceUse->type));
            return false;
        }

        const MTLRenderStages stages = ToMetalRenderStages(*access);
        [encoder useResource:resourceUse->resource
                       usage:ToMetalResourceUsage(access->access)
                      stages:stages];
        ++declaredCount;
        if (resourceUse->IsBuffer()) {
            ++bufferCount;
        } else if (resourceUse->IsTexture()) {
            ++textureCount;
        }
        if ((stages & MTLRenderStageVertex) != 0) {
            ++vertexStageCount;
        }
        if ((stages & MTLRenderStageFragment) != 0) {
            ++fragmentStageCount;
        }
    }

    stats_.frameGraphResourceUseDeclaredCount += declaredCount;
    stats_.frameGraphResourceUseSkippedCount += skippedCount;
    stats_.frameGraphBufferUseDeclaredCount += bufferCount;
    stats_.frameGraphTextureUseDeclaredCount += textureCount;
    stats_.frameGraphVertexStageUseDeclaredCount += vertexStageCount;
    stats_.frameGraphFragmentStageUseDeclaredCount += fragmentStageCount;
    stats_.lastFrameGraphResourceUsePassIndex = passIndex;
    stats_.lastFrameGraphResourceUseAccessOffset = passInfo->accessOffset;
    stats_.lastFrameGraphResourceUseAccessCount = passInfo->accessCount;
    stats_.lastFrameGraphResourceUseDeclaredCount = declaredCount;
    stats_.lastFrameGraphResourceUseSkippedCount = skippedCount;
    return true;
}

id<MTLRenderCommandEncoder> MetalCommandContext::BeginRenderPass(MTLRenderPassDescriptor* passDescriptor) {
    ++stats_.renderPassAttemptCount;
    if (!recording_ || !commandBuffer_ || !passDescriptor) {
        ++stats_.renderPassFailureCount;
        return nil;
    }
    if (queueClass_ != RHI::QueueClass::Graphics) {
        ++stats_.renderPassFailureCount;
        NEXT_LOG_ERROR("Metal render pass requested on %s queue", RHI::QueueClassName(queueClass_));
        return nil;
    }
    id<MTLRenderCommandEncoder> encoder = [commandBuffer_ renderCommandEncoderWithDescriptor:passDescriptor];
    if (!encoder) {
        ++stats_.renderPassFailureCount;
        return nil;
    }
    ++stats_.renderPassBeginCount;
    return encoder;
}

void MetalCommandContext::EndRenderPass(id<MTLRenderCommandEncoder> encoder) {
    if (encoder) {
        [encoder endEncoding];
        ++stats_.renderPassEndCount;
    }
}

bool MetalCommandContext::Commit() {
    ++stats_.commitAttemptCount;
    if (!commandBuffer_) {
        ++stats_.commitFailureCount;
        Reset();
        return false;
    }

    [commandBuffer_ commit];
    ++submittedFrameIndex_;
    ++stats_.committedCommandBufferCount;
    Reset();
    return true;
}

bool MetalCommandContext::PresentAndCommit(id<CAMetalDrawable> drawable) {
    ++stats_.presentAttemptCount;
    if (!commandBuffer_) {
        ++stats_.presentFailureCount;
        Reset();
        return false;
    }

    if (queueClass_ != RHI::QueueClass::Graphics) {
        ++stats_.presentFailureCount;
        NEXT_LOG_ERROR("Metal present requested on %s queue", RHI::QueueClassName(queueClass_));
        Reset();
        return false;
    }

    if (!drawable) {
        ++stats_.presentFailureCount;
        NEXT_LOG_ERROR("Metal present requested without a drawable");
        Reset();
        return false;
    }

    [commandBuffer_ presentDrawable:drawable];
    ++stats_.presentedCommandBufferCount;
    return Commit();
}

void MetalCommandContext::Reset() {
    commandBuffer_ = nil;
    queueClass_ = RHI::QueueClass::Graphics;
    recording_ = false;
}

MetalCommandContextStats MetalCommandContext::GetStats() const {
    MetalCommandContextStats stats = stats_;
    stats.recording = recording_;
    stats.queueClass = queueClass_;
    stats.submittedFrameIndex = submittedFrameIndex_;
    return stats;
}

} // namespace MetalBackend
} // namespace Next
