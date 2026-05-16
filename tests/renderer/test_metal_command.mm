#include "metal_command.h"
#include "metal_device.h"

#include <gtest/gtest.h>

#import <Metal/Metal.h>

namespace Next {
namespace MetalBackend {
namespace testing {
namespace {

class MetalCommandContextTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!device.Initialize()) {
            GTEST_SKIP() << "Metal device unavailable";
        }
    }

    void TearDown() override {
        device.Shutdown();
    }

    id<MTLTexture> CreateColorTarget() {
        @autoreleasepool {
            MTLTextureDescriptor* desc =
                [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                    width:1
                                                                   height:1
                                                                mipmapped:NO];
            desc.usage = MTLTextureUsageRenderTarget;
            return [device.NativeDevice() newTextureWithDescriptor:desc];
        }
    }

    id<MTLTexture> CreateShaderTexture() {
        @autoreleasepool {
            MTLTextureDescriptor* desc =
                [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                    width:1
                                                                   height:1
                                                                mipmapped:NO];
            desc.usage = MTLTextureUsageShaderRead;
            return [device.NativeDevice() newTextureWithDescriptor:desc];
        }
    }

    id<MTLBuffer> CreateBuffer() {
        @autoreleasepool {
            return [device.NativeDevice() newBufferWithLength:16
                                                      options:MTLResourceStorageModeShared];
        }
    }

    MTLRenderPassDescriptor* CreateRenderPass(id<MTLTexture> colorTarget) {
        @autoreleasepool {
            MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
            pass.colorAttachments[0].texture = colorTarget;
            pass.colorAttachments[0].loadAction = MTLLoadActionClear;
            pass.colorAttachments[0].storeAction = MTLStoreActionStore;
            pass.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 1.0);
            return pass;
        }
    }

    MetalDevice device;
};

RHI::FrameGraphCompileResult MakeSwapchainFrameGraph() {
    RHI::FrameGraphDesc graph;

    RHI::FrameGraphResourceHandle colorHandle;
    RHI::FrameGraphResourceDesc colorResource;
    colorResource.debugName = "test color";
    colorResource.type = RHI::ResourceType::Texture;
    colorResource.usage = RHI::ResourceUsage::RenderTarget | RHI::ResourceUsage::Present;
    colorResource.initialState = RHI::ResourceState::Undefined;
    EXPECT_TRUE(RHI::AddFrameGraphResource(graph, colorResource, &colorHandle));

    RHI::FrameGraphResourceHandle depthHandle;
    RHI::FrameGraphResourceDesc depthResource;
    depthResource.debugName = "test depth";
    depthResource.type = RHI::ResourceType::Texture;
    depthResource.usage = RHI::ResourceUsageFlag(RHI::ResourceUsage::DepthStencil);
    depthResource.initialState = RHI::ResourceState::Undefined;
    EXPECT_TRUE(RHI::AddFrameGraphResource(graph, depthResource, &depthHandle));

    RHI::FrameGraphPassDesc renderPass;
    renderPass.debugName = "test render";
    renderPass.queueClass = RHI::QueueClass::Graphics;
    EXPECT_TRUE(RHI::AddFrameGraphPassAccess(
        renderPass,
        RHI::MakeFrameGraphPassResourceAccess(colorHandle,
                                              RHI::ResourceState::RenderTarget,
                                              RHI::FrameGraphAccessType::Write)));
    EXPECT_TRUE(RHI::AddFrameGraphPassAccess(
        renderPass,
        RHI::MakeFrameGraphPassResourceAccess(depthHandle,
                                              RHI::ResourceState::DepthWrite,
                                              RHI::FrameGraphAccessType::Write)));
    EXPECT_TRUE(RHI::AddFrameGraphPass(graph, renderPass));

    RHI::FrameGraphPassDesc presentPass;
    presentPass.debugName = "test present";
    presentPass.queueClass = RHI::QueueClass::Graphics;
    EXPECT_TRUE(RHI::AddFrameGraphPassDependency(presentPass, RHI::MakeFrameGraphPassDependency(0)));
    EXPECT_TRUE(RHI::AddFrameGraphPassAccess(
        presentPass,
        RHI::MakeFrameGraphPassResourceAccess(colorHandle,
                                              RHI::ResourceState::Present,
                                              RHI::FrameGraphAccessType::Read)));
    EXPECT_TRUE(RHI::AddFrameGraphPass(graph, presentPass));

    return RHI::CompileFrameGraphTransitions(graph);
}

RHI::FrameGraphCompileResult MakeAccessSummaryFrameGraph() {
    RHI::FrameGraphDesc graph;

    RHI::FrameGraphResourceHandle vertexHandle;
    RHI::FrameGraphResourceDesc vertexResource;
    vertexResource.debugName = "test vertex";
    vertexResource.type = RHI::ResourceType::Buffer;
    vertexResource.usage = RHI::ResourceUsageFlag(RHI::ResourceUsage::VertexBuffer);
    vertexResource.initialState = RHI::ResourceState::Undefined;
    EXPECT_TRUE(RHI::AddFrameGraphResource(graph, vertexResource, &vertexHandle));

    RHI::FrameGraphResourceHandle shaderHandle;
    RHI::FrameGraphResourceDesc shaderResource;
    shaderResource.debugName = "test shader";
    shaderResource.type = RHI::ResourceType::Texture;
    shaderResource.usage = RHI::ResourceUsageFlag(RHI::ResourceUsage::ShaderRead);
    shaderResource.initialState = RHI::ResourceState::Undefined;
    EXPECT_TRUE(RHI::AddFrameGraphResource(graph, shaderResource, &shaderHandle));

    RHI::FrameGraphResourceHandle copyHandle;
    RHI::FrameGraphResourceDesc copyResource;
    copyResource.debugName = "test copy";
    copyResource.type = RHI::ResourceType::Buffer;
    copyResource.usage = RHI::ResourceUsageFlag(RHI::ResourceUsage::CopyDestination);
    copyResource.initialState = RHI::ResourceState::Undefined;
    EXPECT_TRUE(RHI::AddFrameGraphResource(graph, copyResource, &copyHandle));

    RHI::FrameGraphResourceHandle commonHandle;
    RHI::FrameGraphResourceDesc commonResource;
    commonResource.debugName = "test common";
    commonResource.type = RHI::ResourceType::Buffer;
    commonResource.usage = RHI::ResourceUsageFlag(RHI::ResourceUsage::None);
    commonResource.initialState = RHI::ResourceState::Undefined;
    EXPECT_TRUE(RHI::AddFrameGraphResource(graph, commonResource, &commonHandle));

    RHI::FrameGraphPassDesc pass;
    pass.debugName = "test access summary";
    pass.queueClass = RHI::QueueClass::Graphics;
    EXPECT_TRUE(RHI::AddFrameGraphPassAccess(
        pass,
        RHI::MakeFrameGraphPassResourceAccess(vertexHandle,
                                              RHI::ResourceState::VertexBuffer,
                                              RHI::FrameGraphAccessType::Read)));
    EXPECT_TRUE(RHI::AddFrameGraphPassAccess(
        pass,
        RHI::MakeFrameGraphPassResourceAccess(shaderHandle,
                                              RHI::ResourceState::ShaderRead,
                                              RHI::FrameGraphAccessType::Read)));
    EXPECT_TRUE(RHI::AddFrameGraphPassAccess(
        pass,
        RHI::MakeFrameGraphPassResourceAccess(copyHandle,
                                              RHI::ResourceState::CopyDestination,
                                              RHI::FrameGraphAccessType::Write)));
    EXPECT_TRUE(RHI::AddFrameGraphPassAccess(
        pass,
        RHI::MakeFrameGraphPassResourceAccess(commonHandle,
                                              RHI::ResourceState::Common,
                                              RHI::FrameGraphAccessType::ReadWrite)));
    EXPECT_TRUE(RHI::AddFrameGraphPass(graph, pass));

    return RHI::CompileFrameGraphTransitions(graph);
}

struct RenderPassResourceUseFrameGraph {
    RHI::FrameGraphCompileResult compileResult;
    MetalFrameGraphResourceUsageTable resourceUsages;
};

RenderPassResourceUseFrameGraph MakeRenderPassResourceUseFrameGraph(id<MTLTexture> colorTarget,
                                                                    id<MTLBuffer> vertexBuffer,
                                                                    id<MTLBuffer> uniformBuffer,
                                                                    id<MTLTexture> shaderTexture) {
    RenderPassResourceUseFrameGraph result;
    RHI::FrameGraphDesc graph;

    RHI::FrameGraphResourceHandle colorHandle;
    RHI::FrameGraphResourceDesc colorResource;
    colorResource.debugName = "test native color";
    colorResource.type = RHI::ResourceType::Texture;
    colorResource.usage = RHI::ResourceUsageFlag(RHI::ResourceUsage::RenderTarget);
    colorResource.initialState = RHI::ResourceState::Undefined;
    EXPECT_TRUE(RHI::AddFrameGraphResource(graph, colorResource, &colorHandle));
    EXPECT_TRUE(result.resourceUsages.SetResource(colorHandle.index, colorTarget, RHI::ResourceType::Texture));

    RHI::FrameGraphResourceHandle vertexHandle;
    RHI::FrameGraphResourceDesc vertexResource;
    vertexResource.debugName = "test native vertex";
    vertexResource.type = RHI::ResourceType::Buffer;
    vertexResource.usage = RHI::ResourceUsageFlag(RHI::ResourceUsage::VertexBuffer);
    vertexResource.initialState = RHI::ResourceState::Undefined;
    EXPECT_TRUE(RHI::AddFrameGraphResource(graph, vertexResource, &vertexHandle));
    EXPECT_TRUE(result.resourceUsages.SetResource(vertexHandle.index, vertexBuffer, RHI::ResourceType::Buffer));

    RHI::FrameGraphResourceHandle uniformHandle;
    RHI::FrameGraphResourceDesc uniformResource;
    uniformResource.debugName = "test native uniform";
    uniformResource.type = RHI::ResourceType::Buffer;
    uniformResource.usage = RHI::ResourceUsageFlag(RHI::ResourceUsage::ConstantBuffer);
    uniformResource.initialState = RHI::ResourceState::Undefined;
    EXPECT_TRUE(RHI::AddFrameGraphResource(graph, uniformResource, &uniformHandle));
    EXPECT_TRUE(result.resourceUsages.SetResource(uniformHandle.index, uniformBuffer, RHI::ResourceType::Buffer));

    RHI::FrameGraphResourceHandle shaderHandle;
    RHI::FrameGraphResourceDesc shaderResource;
    shaderResource.debugName = "test native shader";
    shaderResource.type = RHI::ResourceType::Texture;
    shaderResource.usage = RHI::ResourceUsageFlag(RHI::ResourceUsage::ShaderRead);
    shaderResource.initialState = RHI::ResourceState::Undefined;
    EXPECT_TRUE(RHI::AddFrameGraphResource(graph, shaderResource, &shaderHandle));
    EXPECT_TRUE(result.resourceUsages.SetResource(shaderHandle.index, shaderTexture, RHI::ResourceType::Texture));

    RHI::FrameGraphPassDesc renderPass;
    renderPass.debugName = "test native render resources";
    renderPass.queueClass = RHI::QueueClass::Graphics;
    EXPECT_TRUE(RHI::AddFrameGraphPassAccess(
        renderPass,
        RHI::MakeFrameGraphPassResourceAccess(colorHandle,
                                              RHI::ResourceState::RenderTarget,
                                              RHI::FrameGraphAccessType::Write)));
    EXPECT_TRUE(RHI::AddFrameGraphPassAccess(
        renderPass,
        RHI::MakeFrameGraphPassResourceAccess(vertexHandle,
                                              RHI::ResourceState::VertexBuffer,
                                              RHI::FrameGraphAccessType::Read,
                                              RHI::ShaderStageFlag(RHI::ShaderStage::Vertex))));
    EXPECT_TRUE(RHI::AddFrameGraphPassAccess(
        renderPass,
        RHI::MakeFrameGraphPassResourceAccess(uniformHandle,
                                              RHI::ResourceState::ConstantBuffer,
                                              RHI::FrameGraphAccessType::Read,
                                              RHI::ShaderStageFlag(RHI::ShaderStage::Fragment))));
    EXPECT_TRUE(RHI::AddFrameGraphPassAccess(
        renderPass,
        RHI::MakeFrameGraphPassResourceAccess(shaderHandle,
                                              RHI::ResourceState::ShaderRead,
                                              RHI::FrameGraphAccessType::Read,
                                              RHI::ShaderStageFlag(RHI::ShaderStage::Fragment))));
    EXPECT_TRUE(RHI::AddFrameGraphPass(graph, renderPass));

    result.compileResult = RHI::CompileFrameGraphTransitions(graph);
    return result;
}

} // namespace

TEST_F(MetalCommandContextTest, TracksGraphicsRenderPassAndCommitStats) {
    @autoreleasepool {
        id<MTLTexture> colorTarget = CreateColorTarget();
        ASSERT_NE(colorTarget, nil);
        MTLRenderPassDescriptor* pass = CreateRenderPass(colorTarget);
        ASSERT_NE(pass, nil);

        MetalCommandContext context;
        const RHI::FrameGraphCompileResult frameGraph = MakeSwapchainFrameGraph();
        ASSERT_TRUE(frameGraph);
        ASSERT_TRUE(frameGraph.HasCompletePassTable());
        ASSERT_TRUE(context.Begin(device, RHI::QueueClass::Graphics));
        EXPECT_TRUE(context.IsRecording());
        EXPECT_TRUE(context.EncodeFrameGraphPassTransitions(frameGraph, 0));

        id<MTLRenderCommandEncoder> encoder = context.BeginRenderPass(pass);
        ASSERT_NE(encoder, nil);
        context.EndRenderPass(encoder);

        EXPECT_TRUE(context.EncodeFrameGraphPassTransitions(frameGraph, 1));
        EXPECT_TRUE(context.Commit());

        const MetalCommandContextStats stats = context.GetStats();
        EXPECT_FALSE(stats.recording);
        EXPECT_EQ(stats.queueClass, RHI::QueueClass::Graphics);
        EXPECT_EQ(stats.submittedFrameIndex, 1u);
        EXPECT_TRUE(stats.HasSubmittedFrames());
        EXPECT_FALSE(stats.HasFailures());
        EXPECT_EQ(stats.beginAttemptCount, 1u);
        EXPECT_EQ(stats.begunCommandBufferCount, 1u);
        EXPECT_EQ(stats.beginFailureCount, 0u);
        EXPECT_EQ(stats.renderPassAttemptCount, 1u);
        EXPECT_EQ(stats.renderPassBeginCount, 1u);
        EXPECT_EQ(stats.renderPassFailureCount, 0u);
        EXPECT_EQ(stats.renderPassEndCount, 1u);
        EXPECT_EQ(stats.presentAttemptCount, 0u);
        EXPECT_EQ(stats.presentedCommandBufferCount, 0u);
        EXPECT_EQ(stats.presentFailureCount, 0u);
        EXPECT_EQ(stats.frameGraphPassAttemptCount, 2u);
        EXPECT_EQ(stats.frameGraphPassEncodedCount, 2u);
        EXPECT_EQ(stats.frameGraphPassFailureCount, 0u);
        EXPECT_EQ(stats.frameGraphDependencyEncodedCount, 1u);
        EXPECT_EQ(stats.frameGraphAccessEncodedCount, 3u);
        EXPECT_EQ(stats.frameGraphTransitionEncodedCount, 3u);
        EXPECT_EQ(stats.frameGraphAttachmentTransitionCount, 2u);
        EXPECT_EQ(stats.frameGraphBufferTransitionCount, 0u);
        EXPECT_EQ(stats.frameGraphShaderTransitionCount, 0u);
        EXPECT_EQ(stats.frameGraphCopyTransitionCount, 0u);
        EXPECT_EQ(stats.frameGraphPresentTransitionCount, 1u);
        EXPECT_EQ(stats.frameGraphOtherTransitionCount, 0u);
        EXPECT_EQ(stats.frameGraphAttachmentAccessCount, 2u);
        EXPECT_EQ(stats.frameGraphBufferAccessCount, 0u);
        EXPECT_EQ(stats.frameGraphShaderAccessCount, 0u);
        EXPECT_EQ(stats.frameGraphCopyAccessCount, 0u);
        EXPECT_EQ(stats.frameGraphPresentAccessCount, 1u);
        EXPECT_EQ(stats.frameGraphOtherAccessCount, 0u);
        EXPECT_EQ(stats.frameGraphShaderStageHintAccessCount, 0u);
        EXPECT_EQ(stats.frameGraphVertexStageHintAccessCount, 0u);
        EXPECT_EQ(stats.frameGraphFragmentStageHintAccessCount, 0u);
        EXPECT_EQ(stats.frameGraphComputeStageHintAccessCount, 0u);
        EXPECT_EQ(stats.frameGraphResourceUseAttemptCount, 0u);
        EXPECT_EQ(stats.frameGraphResourceUseDeclaredCount, 0u);
        EXPECT_EQ(stats.frameGraphResourceUseSkippedCount, 0u);
        EXPECT_EQ(stats.frameGraphResourceUseFailureCount, 0u);
        EXPECT_EQ(stats.frameGraphBufferUseDeclaredCount, 0u);
        EXPECT_EQ(stats.frameGraphTextureUseDeclaredCount, 0u);
        EXPECT_EQ(stats.frameGraphVertexStageUseDeclaredCount, 0u);
        EXPECT_EQ(stats.frameGraphFragmentStageUseDeclaredCount, 0u);
        EXPECT_EQ(stats.lastFrameGraphResourceUsePassIndex, 0u);
        EXPECT_EQ(stats.lastFrameGraphResourceUseAccessOffset, 0u);
        EXPECT_EQ(stats.lastFrameGraphResourceUseAccessCount, 0u);
        EXPECT_EQ(stats.lastFrameGraphResourceUseDeclaredCount, 0u);
        EXPECT_EQ(stats.lastFrameGraphResourceUseSkippedCount, 0u);
        EXPECT_EQ(stats.lastFrameGraphPassIndex, 1u);
        EXPECT_EQ(stats.lastFrameGraphDependencyOffset, 0u);
        EXPECT_EQ(stats.lastFrameGraphDependencyCount, 1u);
        EXPECT_EQ(stats.lastFrameGraphTransitionOffset, 2u);
        EXPECT_EQ(stats.lastFrameGraphTransitionCount, 1u);
        EXPECT_EQ(stats.lastFrameGraphAccessOffset, 2u);
        EXPECT_EQ(stats.lastFrameGraphAccessCount, 1u);
        EXPECT_EQ(stats.lastFrameGraphPassQueueClass, RHI::QueueClass::Graphics);
        EXPECT_TRUE(stats.HasFrameGraphPassAttempts());
        EXPECT_TRUE(stats.HasFrameGraphPasses());
        EXPECT_TRUE(stats.HasFrameGraphDependencies());
        EXPECT_TRUE(stats.HasFrameGraphAccesses());
        EXPECT_TRUE(stats.HasFrameGraphTransitions());
        EXPECT_TRUE(stats.HasFrameGraphTransitionSummary());
        EXPECT_TRUE(stats.HasFrameGraphAccessSummary());
        EXPECT_FALSE(stats.HasFrameGraphShaderStageHints());
        EXPECT_FALSE(stats.HasFrameGraphResourceUseAttempts());
        EXPECT_FALSE(stats.HasFrameGraphResourceUses());
        EXPECT_FALSE(stats.HasFrameGraphStageResourceUses());
        EXPECT_EQ(stats.commitAttemptCount, 1u);
        EXPECT_EQ(stats.committedCommandBufferCount, 1u);
        EXPECT_EQ(stats.commitFailureCount, 0u);
    }
}

TEST_F(MetalCommandContextTest, RejectsPresentWithoutDrawableAndResets) {
    @autoreleasepool {
        MetalCommandContext context;
        ASSERT_TRUE(context.Begin(device, RHI::QueueClass::Graphics));
        EXPECT_TRUE(context.IsRecording());

        EXPECT_FALSE(context.PresentAndCommit(nil));

        const MetalCommandContextStats stats = context.GetStats();
        EXPECT_FALSE(stats.recording);
        EXPECT_EQ(stats.queueClass, RHI::QueueClass::Graphics);
        EXPECT_EQ(stats.submittedFrameIndex, 0u);
        EXPECT_EQ(stats.presentAttemptCount, 1u);
        EXPECT_EQ(stats.presentedCommandBufferCount, 0u);
        EXPECT_EQ(stats.presentFailureCount, 1u);
        EXPECT_EQ(stats.commitAttemptCount, 0u);
        EXPECT_EQ(stats.committedCommandBufferCount, 0u);
        EXPECT_EQ(stats.commitFailureCount, 0u);
        EXPECT_TRUE(stats.HasFailures());
    }
}

TEST_F(MetalCommandContextTest, TracksFailureStatsAndResetsAfterInvalidOperations) {
    @autoreleasepool {
        MetalCommandContext context;
        EXPECT_FALSE(context.EncodeFrameGraphPassTransitions(RHI::FrameGraphCompileResult{}, 0));
        EXPECT_EQ(context.BeginRenderPass(nil), nil);
        EXPECT_FALSE(context.Commit());
        EXPECT_FALSE(context.PresentAndCommit(nil));

        const RHI::FrameGraphCompileResult frameGraph = MakeSwapchainFrameGraph();
        ASSERT_TRUE(frameGraph);
        ASSERT_TRUE(context.Begin(device, RHI::QueueClass::Copy));
        EXPECT_FALSE(context.Begin(device, RHI::QueueClass::Copy));
        EXPECT_TRUE(context.IsRecording());
        EXPECT_FALSE(context.EncodeFrameGraphPassTransitions(frameGraph, 0));

        MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
        EXPECT_EQ(context.BeginRenderPass(pass), nil);
        EXPECT_FALSE(context.PresentAndCommit(nil));

        const MetalCommandContextStats stats = context.GetStats();
        EXPECT_FALSE(stats.recording);
        EXPECT_EQ(stats.queueClass, RHI::QueueClass::Graphics);
        EXPECT_EQ(stats.submittedFrameIndex, 0u);
        EXPECT_FALSE(stats.HasSubmittedFrames());
        EXPECT_TRUE(stats.HasFailures());
        EXPECT_EQ(stats.beginAttemptCount, 2u);
        EXPECT_EQ(stats.begunCommandBufferCount, 1u);
        EXPECT_EQ(stats.beginFailureCount, 1u);
        EXPECT_EQ(stats.renderPassAttemptCount, 2u);
        EXPECT_EQ(stats.renderPassBeginCount, 0u);
        EXPECT_EQ(stats.renderPassFailureCount, 2u);
        EXPECT_EQ(stats.renderPassEndCount, 0u);
        EXPECT_EQ(stats.commitAttemptCount, 1u);
        EXPECT_EQ(stats.committedCommandBufferCount, 0u);
        EXPECT_EQ(stats.commitFailureCount, 1u);
        EXPECT_EQ(stats.presentAttemptCount, 2u);
        EXPECT_EQ(stats.presentedCommandBufferCount, 0u);
        EXPECT_EQ(stats.presentFailureCount, 2u);
        EXPECT_EQ(stats.frameGraphPassAttemptCount, 2u);
        EXPECT_EQ(stats.frameGraphPassEncodedCount, 0u);
        EXPECT_EQ(stats.frameGraphPassFailureCount, 2u);
        EXPECT_EQ(stats.frameGraphDependencyEncodedCount, 0u);
        EXPECT_EQ(stats.frameGraphAccessEncodedCount, 0u);
        EXPECT_EQ(stats.frameGraphTransitionEncodedCount, 0u);
        EXPECT_EQ(stats.frameGraphAttachmentTransitionCount, 0u);
        EXPECT_EQ(stats.frameGraphBufferTransitionCount, 0u);
        EXPECT_EQ(stats.frameGraphShaderTransitionCount, 0u);
        EXPECT_EQ(stats.frameGraphCopyTransitionCount, 0u);
        EXPECT_EQ(stats.frameGraphPresentTransitionCount, 0u);
        EXPECT_EQ(stats.frameGraphOtherTransitionCount, 0u);
        EXPECT_EQ(stats.frameGraphAttachmentAccessCount, 0u);
        EXPECT_EQ(stats.frameGraphBufferAccessCount, 0u);
        EXPECT_EQ(stats.frameGraphShaderAccessCount, 0u);
        EXPECT_EQ(stats.frameGraphCopyAccessCount, 0u);
        EXPECT_EQ(stats.frameGraphPresentAccessCount, 0u);
        EXPECT_EQ(stats.frameGraphOtherAccessCount, 0u);
        EXPECT_EQ(stats.frameGraphShaderStageHintAccessCount, 0u);
        EXPECT_EQ(stats.frameGraphVertexStageHintAccessCount, 0u);
        EXPECT_EQ(stats.frameGraphFragmentStageHintAccessCount, 0u);
        EXPECT_EQ(stats.frameGraphComputeStageHintAccessCount, 0u);
        EXPECT_EQ(stats.frameGraphResourceUseAttemptCount, 0u);
        EXPECT_EQ(stats.frameGraphResourceUseDeclaredCount, 0u);
        EXPECT_EQ(stats.frameGraphResourceUseSkippedCount, 0u);
        EXPECT_EQ(stats.frameGraphResourceUseFailureCount, 0u);
        EXPECT_EQ(stats.frameGraphBufferUseDeclaredCount, 0u);
        EXPECT_EQ(stats.frameGraphTextureUseDeclaredCount, 0u);
        EXPECT_EQ(stats.frameGraphVertexStageUseDeclaredCount, 0u);
        EXPECT_EQ(stats.frameGraphFragmentStageUseDeclaredCount, 0u);
        EXPECT_EQ(stats.lastFrameGraphResourceUsePassIndex, 0u);
        EXPECT_EQ(stats.lastFrameGraphResourceUseAccessOffset, 0u);
        EXPECT_EQ(stats.lastFrameGraphResourceUseAccessCount, 0u);
        EXPECT_EQ(stats.lastFrameGraphResourceUseDeclaredCount, 0u);
        EXPECT_EQ(stats.lastFrameGraphResourceUseSkippedCount, 0u);
        EXPECT_EQ(stats.lastFrameGraphDependencyOffset, 0u);
        EXPECT_EQ(stats.lastFrameGraphDependencyCount, 0u);
        EXPECT_TRUE(stats.HasFrameGraphPassAttempts());
        EXPECT_FALSE(stats.HasFrameGraphPasses());
        EXPECT_FALSE(stats.HasFrameGraphDependencies());
        EXPECT_FALSE(stats.HasFrameGraphAccesses());
        EXPECT_FALSE(stats.HasFrameGraphTransitions());
        EXPECT_FALSE(stats.HasFrameGraphTransitionSummary());
        EXPECT_FALSE(stats.HasFrameGraphAccessSummary());
        EXPECT_FALSE(stats.HasFrameGraphShaderStageHints());
        EXPECT_FALSE(stats.HasFrameGraphResourceUseAttempts());
        EXPECT_FALSE(stats.HasFrameGraphResourceUses());
        EXPECT_FALSE(stats.HasFrameGraphStageResourceUses());
    }
}

TEST_F(MetalCommandContextTest, RejectsIncompleteFrameGraphCompileTables) {
    @autoreleasepool {
        const RHI::FrameGraphCompileResult frameGraph = MakeSwapchainFrameGraph();
        ASSERT_TRUE(frameGraph);
        ASSERT_TRUE(frameGraph.HasCompleteResourceTable());
        ASSERT_TRUE(frameGraph.HasCompleteDependencyTable());
        ASSERT_TRUE(frameGraph.HasCompleteAccessTable());
        ASSERT_TRUE(frameGraph.HasCompleteTransitionTable());
        ASSERT_TRUE(frameGraph.HasCompletePassTable());

        RHI::FrameGraphCompileResult incompleteResources = frameGraph;
        incompleteResources.resourceTypes[0] = RHI::ResourceType::Unknown;
        RHI::FrameGraphCompileResult incompleteDependencies = frameGraph;
        incompleteDependencies.dependencyCount =
            static_cast<uint32_t>(RHI::kMaxFrameGraphDependencies + 1);
        RHI::FrameGraphCompileResult incompleteAccesses = frameGraph;
        incompleteAccesses.accessCount =
            static_cast<uint32_t>(RHI::kMaxFrameGraphAccesses + 1);
        RHI::FrameGraphCompileResult incompleteTransitions = frameGraph;
        incompleteTransitions.transitionCount =
            static_cast<uint32_t>(RHI::kMaxFrameGraphTransitions + 1);

        MetalCommandContext context;
        ASSERT_TRUE(context.Begin(device, RHI::QueueClass::Graphics));
        EXPECT_FALSE(context.EncodeFrameGraphPassTransitions(incompleteResources, 0));
        EXPECT_FALSE(context.EncodeFrameGraphPassTransitions(incompleteDependencies, 1));
        EXPECT_FALSE(context.EncodeFrameGraphPassTransitions(incompleteAccesses, 0));
        EXPECT_FALSE(context.EncodeFrameGraphPassTransitions(incompleteTransitions, 0));
        EXPECT_TRUE(context.Commit());

        const MetalCommandContextStats stats = context.GetStats();
        EXPECT_EQ(stats.frameGraphPassAttemptCount, 4u);
        EXPECT_EQ(stats.frameGraphPassEncodedCount, 0u);
        EXPECT_EQ(stats.frameGraphPassFailureCount, 4u);
        EXPECT_EQ(stats.frameGraphDependencyEncodedCount, 0u);
        EXPECT_EQ(stats.frameGraphAccessEncodedCount, 0u);
        EXPECT_EQ(stats.frameGraphTransitionEncodedCount, 0u);
        EXPECT_TRUE(stats.HasFrameGraphPassAttempts());
        EXPECT_FALSE(stats.HasFrameGraphPasses());
        EXPECT_FALSE(stats.HasFrameGraphDependencies());
        EXPECT_FALSE(stats.HasFrameGraphAccesses());
        EXPECT_FALSE(stats.HasFrameGraphTransitions());
        EXPECT_TRUE(stats.HasFailures());
    }
}

TEST_F(MetalCommandContextTest, RejectsCorruptFrameGraphCompileTableRanges) {
    @autoreleasepool {
        const RHI::FrameGraphCompileResult frameGraph = MakeSwapchainFrameGraph();
        ASSERT_TRUE(frameGraph);
        ASSERT_TRUE(frameGraph.HasCompleteCompileTables());
        ASSERT_EQ(frameGraph.passCount, 2u);
        ASSERT_EQ(frameGraph.dependencyCount, 1u);
        ASSERT_EQ(frameGraph.accessCount, 3u);
        ASSERT_EQ(frameGraph.transitionCount, 3u);

        RHI::FrameGraphCompileResult dependencyOutsidePassRange = frameGraph;
        dependencyOutsidePassRange.dependencies[0].passIndex = 0;
        RHI::FrameGraphCompileResult dependencyLocalIndexOutsidePassRange = frameGraph;
        dependencyLocalIndexOutsidePassRange.dependencies[0].dependencyIndex =
            dependencyLocalIndexOutsidePassRange.passes[1].dependencyCount;
        RHI::FrameGraphCompileResult accessOutsidePassRange = frameGraph;
        accessOutsidePassRange.accesses[0].passIndex = 1;
        RHI::FrameGraphCompileResult accessLocalIndexOutsidePassRange = frameGraph;
        accessLocalIndexOutsidePassRange.accesses[0].accessIndex =
            accessLocalIndexOutsidePassRange.passes[0].accessCount;
        RHI::FrameGraphCompileResult accessQueueMismatch = frameGraph;
        accessQueueMismatch.accesses[0].queueClass = RHI::QueueClass::Copy;
        RHI::FrameGraphCompileResult transitionOutsidePassRange = frameGraph;
        transitionOutsidePassRange.transitions[0].passIndex = 1;
        RHI::FrameGraphCompileResult transitionAccessIndexOutsidePassRange = frameGraph;
        transitionAccessIndexOutsidePassRange.transitions[0].accessIndex =
            transitionAccessIndexOutsidePassRange.passes[0].accessCount;
        RHI::FrameGraphCompileResult transitionQueueMismatch = frameGraph;
        transitionQueueMismatch.transitions[0].queueClass = RHI::QueueClass::Copy;

        MetalCommandContext context;
        ASSERT_TRUE(context.Begin(device, RHI::QueueClass::Graphics));

        uint32_t rejectedCount = 0;
        auto expectRejected = [&](const RHI::FrameGraphCompileResult& compileResult,
                                  uint32_t passIndex) {
            EXPECT_FALSE(compileResult.HasCompleteCompileTables());
            EXPECT_FALSE(context.EncodeFrameGraphPassTransitions(compileResult, passIndex));
            ++rejectedCount;
        };

        expectRejected(dependencyOutsidePassRange, 1);
        expectRejected(dependencyLocalIndexOutsidePassRange, 1);
        expectRejected(accessOutsidePassRange, 0);
        expectRejected(accessLocalIndexOutsidePassRange, 0);
        expectRejected(accessQueueMismatch, 0);
        expectRejected(transitionOutsidePassRange, 0);
        expectRejected(transitionAccessIndexOutsidePassRange, 0);
        expectRejected(transitionQueueMismatch, 0);
        EXPECT_TRUE(context.Commit());

        const MetalCommandContextStats stats = context.GetStats();
        EXPECT_EQ(rejectedCount, 8u);
        EXPECT_EQ(stats.frameGraphPassAttemptCount, rejectedCount);
        EXPECT_EQ(stats.frameGraphPassEncodedCount, 0u);
        EXPECT_EQ(stats.frameGraphPassFailureCount, rejectedCount);
        EXPECT_EQ(stats.frameGraphDependencyEncodedCount, 0u);
        EXPECT_EQ(stats.frameGraphAccessEncodedCount, 0u);
        EXPECT_EQ(stats.frameGraphTransitionEncodedCount, 0u);
        EXPECT_TRUE(stats.HasFrameGraphPassAttempts());
        EXPECT_FALSE(stats.HasFrameGraphPasses());
        EXPECT_FALSE(stats.HasFrameGraphDependencies());
        EXPECT_FALSE(stats.HasFrameGraphAccesses());
        EXPECT_FALSE(stats.HasFrameGraphTransitions());
        EXPECT_TRUE(stats.HasFailures());
    }
}

TEST_F(MetalCommandContextTest, TracksFrameGraphAccessSummaryBuckets) {
    @autoreleasepool {
        const RHI::FrameGraphCompileResult frameGraph = MakeAccessSummaryFrameGraph();
        ASSERT_TRUE(frameGraph);
        ASSERT_EQ(frameGraph.transitionCount, 4u);

        MetalCommandContext context;
        ASSERT_TRUE(context.Begin(device, RHI::QueueClass::Graphics));
        EXPECT_TRUE(context.EncodeFrameGraphPassTransitions(frameGraph, 0));
        EXPECT_TRUE(context.Commit());

        const MetalCommandContextStats stats = context.GetStats();
        EXPECT_EQ(stats.frameGraphPassAttemptCount, 1u);
        EXPECT_EQ(stats.frameGraphPassEncodedCount, 1u);
        EXPECT_EQ(stats.frameGraphDependencyEncodedCount, 0u);
        EXPECT_EQ(stats.frameGraphAccessEncodedCount, 4u);
        EXPECT_EQ(stats.frameGraphTransitionEncodedCount, 4u);
        EXPECT_EQ(stats.frameGraphAttachmentTransitionCount, 0u);
        EXPECT_EQ(stats.frameGraphBufferTransitionCount, 1u);
        EXPECT_EQ(stats.frameGraphShaderTransitionCount, 1u);
        EXPECT_EQ(stats.frameGraphCopyTransitionCount, 1u);
        EXPECT_EQ(stats.frameGraphPresentTransitionCount, 0u);
        EXPECT_EQ(stats.frameGraphOtherTransitionCount, 1u);
        EXPECT_EQ(stats.frameGraphAttachmentAccessCount, 0u);
        EXPECT_EQ(stats.frameGraphBufferAccessCount, 1u);
        EXPECT_EQ(stats.frameGraphShaderAccessCount, 1u);
        EXPECT_EQ(stats.frameGraphCopyAccessCount, 1u);
        EXPECT_EQ(stats.frameGraphPresentAccessCount, 0u);
        EXPECT_EQ(stats.frameGraphOtherAccessCount, 1u);
        EXPECT_EQ(stats.frameGraphShaderStageHintAccessCount, 0u);
        EXPECT_EQ(stats.frameGraphVertexStageHintAccessCount, 0u);
        EXPECT_EQ(stats.frameGraphFragmentStageHintAccessCount, 0u);
        EXPECT_EQ(stats.frameGraphComputeStageHintAccessCount, 0u);
        EXPECT_EQ(stats.frameGraphResourceUseAttemptCount, 0u);
        EXPECT_EQ(stats.frameGraphResourceUseDeclaredCount, 0u);
        EXPECT_EQ(stats.frameGraphResourceUseSkippedCount, 0u);
        EXPECT_EQ(stats.frameGraphResourceUseFailureCount, 0u);
        EXPECT_EQ(stats.frameGraphBufferUseDeclaredCount, 0u);
        EXPECT_EQ(stats.frameGraphTextureUseDeclaredCount, 0u);
        EXPECT_EQ(stats.frameGraphVertexStageUseDeclaredCount, 0u);
        EXPECT_EQ(stats.frameGraphFragmentStageUseDeclaredCount, 0u);
        EXPECT_EQ(stats.lastFrameGraphResourceUsePassIndex, 0u);
        EXPECT_EQ(stats.lastFrameGraphResourceUseAccessOffset, 0u);
        EXPECT_EQ(stats.lastFrameGraphResourceUseAccessCount, 0u);
        EXPECT_EQ(stats.lastFrameGraphResourceUseDeclaredCount, 0u);
        EXPECT_EQ(stats.lastFrameGraphResourceUseSkippedCount, 0u);
        EXPECT_EQ(stats.lastFrameGraphDependencyOffset, 0u);
        EXPECT_EQ(stats.lastFrameGraphDependencyCount, 0u);
        EXPECT_TRUE(stats.HasFrameGraphAccesses());
        EXPECT_TRUE(stats.HasFrameGraphTransitionSummary());
        EXPECT_TRUE(stats.HasFrameGraphAccessSummary());
        EXPECT_EQ(stats.lastFrameGraphPassIndex, 0u);
        EXPECT_EQ(stats.lastFrameGraphTransitionOffset, 0u);
        EXPECT_EQ(stats.lastFrameGraphTransitionCount, 4u);
        EXPECT_EQ(stats.lastFrameGraphAccessOffset, 0u);
        EXPECT_EQ(stats.lastFrameGraphAccessCount, 4u);
        EXPECT_FALSE(stats.HasFrameGraphShaderStageHints());
        EXPECT_FALSE(stats.HasFrameGraphDependencies());
        EXPECT_FALSE(stats.HasFrameGraphResourceUseAttempts());
        EXPECT_FALSE(stats.HasFrameGraphResourceUses());
        EXPECT_FALSE(stats.HasFrameGraphStageResourceUses());
        EXPECT_FALSE(stats.HasFailures());
    }
}

TEST_F(MetalCommandContextTest, DeclaresFrameGraphRenderPassResourceUses) {
    @autoreleasepool {
        id<MTLTexture> colorTarget = CreateColorTarget();
        id<MTLBuffer> vertexBuffer = CreateBuffer();
        id<MTLBuffer> uniformBuffer = CreateBuffer();
        id<MTLTexture> shaderTexture = CreateShaderTexture();
        ASSERT_NE(colorTarget, nil);
        ASSERT_NE(vertexBuffer, nil);
        ASSERT_NE(uniformBuffer, nil);
        ASSERT_NE(shaderTexture, nil);

        MTLRenderPassDescriptor* pass = CreateRenderPass(colorTarget);
        ASSERT_NE(pass, nil);

        const RenderPassResourceUseFrameGraph frameGraph =
            MakeRenderPassResourceUseFrameGraph(colorTarget, vertexBuffer, uniformBuffer, shaderTexture);
        ASSERT_TRUE(frameGraph.compileResult);
        ASSERT_TRUE(frameGraph.compileResult.HasCompleteResourceTable());
        ASSERT_TRUE(frameGraph.compileResult.HasCompletePassTable());
        ASSERT_TRUE(frameGraph.resourceUsages.HasCompleteResourceTable());

        MetalCommandContext context;
        ASSERT_TRUE(context.Begin(device, RHI::QueueClass::Graphics));
        EXPECT_TRUE(context.EncodeFrameGraphPassTransitions(frameGraph.compileResult, 0));
        id<MTLRenderCommandEncoder> encoder = context.BeginRenderPass(pass);
        ASSERT_NE(encoder, nil);
        EXPECT_TRUE(context.EncodeFrameGraphRenderPassResourceUsages(
            encoder, frameGraph.compileResult, frameGraph.resourceUsages, 0));
        context.EndRenderPass(encoder);
        EXPECT_TRUE(context.Commit());

        const MetalCommandContextStats stats = context.GetStats();
        EXPECT_EQ(stats.frameGraphPassAttemptCount, 1u);
        EXPECT_EQ(stats.frameGraphPassEncodedCount, 1u);
        EXPECT_EQ(stats.frameGraphDependencyEncodedCount, 0u);
        EXPECT_EQ(stats.frameGraphAccessEncodedCount, 4u);
        EXPECT_EQ(stats.frameGraphTransitionEncodedCount, 4u);
        EXPECT_EQ(stats.frameGraphAttachmentTransitionCount, 1u);
        EXPECT_EQ(stats.frameGraphBufferTransitionCount, 2u);
        EXPECT_EQ(stats.frameGraphShaderTransitionCount, 1u);
        EXPECT_EQ(stats.frameGraphCopyTransitionCount, 0u);
        EXPECT_EQ(stats.frameGraphPresentTransitionCount, 0u);
        EXPECT_EQ(stats.frameGraphOtherTransitionCount, 0u);
        EXPECT_EQ(stats.frameGraphShaderStageHintAccessCount, 3u);
        EXPECT_EQ(stats.frameGraphVertexStageHintAccessCount, 1u);
        EXPECT_EQ(stats.frameGraphFragmentStageHintAccessCount, 2u);
        EXPECT_EQ(stats.frameGraphComputeStageHintAccessCount, 0u);
        EXPECT_EQ(stats.frameGraphResourceUseAttemptCount, 1u);
        EXPECT_EQ(stats.frameGraphResourceUseDeclaredCount, 3u);
        EXPECT_EQ(stats.frameGraphResourceUseSkippedCount, 1u);
        EXPECT_EQ(stats.frameGraphResourceUseFailureCount, 0u);
        EXPECT_EQ(stats.frameGraphBufferUseDeclaredCount, 2u);
        EXPECT_EQ(stats.frameGraphTextureUseDeclaredCount, 1u);
        EXPECT_EQ(stats.frameGraphVertexStageUseDeclaredCount, 1u);
        EXPECT_EQ(stats.frameGraphFragmentStageUseDeclaredCount, 2u);
        EXPECT_EQ(stats.lastFrameGraphResourceUsePassIndex, 0u);
        EXPECT_EQ(stats.lastFrameGraphResourceUseAccessOffset, 0u);
        EXPECT_EQ(stats.lastFrameGraphResourceUseAccessCount, 4u);
        EXPECT_EQ(stats.lastFrameGraphResourceUseDeclaredCount, 3u);
        EXPECT_EQ(stats.lastFrameGraphResourceUseSkippedCount, 1u);
        EXPECT_EQ(stats.lastFrameGraphPassIndex, 0u);
        EXPECT_EQ(stats.lastFrameGraphDependencyOffset, 0u);
        EXPECT_EQ(stats.lastFrameGraphDependencyCount, 0u);
        EXPECT_EQ(stats.lastFrameGraphTransitionOffset, 0u);
        EXPECT_EQ(stats.lastFrameGraphTransitionCount, 4u);
        EXPECT_EQ(stats.lastFrameGraphAccessOffset, 0u);
        EXPECT_EQ(stats.lastFrameGraphAccessCount, 4u);
        EXPECT_TRUE(stats.HasFrameGraphTransitionSummary());
        EXPECT_TRUE(stats.HasFrameGraphShaderStageHints());
        EXPECT_FALSE(stats.HasFrameGraphDependencies());
        EXPECT_TRUE(stats.HasFrameGraphResourceUseAttempts());
        EXPECT_TRUE(stats.HasFrameGraphResourceUses());
        EXPECT_TRUE(stats.HasFrameGraphSkippedResourceUses());
        EXPECT_TRUE(stats.HasFrameGraphStageResourceUses());
        EXPECT_FALSE(stats.HasFailures());
    }
}

TEST_F(MetalCommandContextTest, RejectsIncompleteResourceUseCompileTables) {
    @autoreleasepool {
        id<MTLTexture> colorTarget = CreateColorTarget();
        id<MTLBuffer> vertexBuffer = CreateBuffer();
        id<MTLBuffer> uniformBuffer = CreateBuffer();
        id<MTLTexture> shaderTexture = CreateShaderTexture();
        ASSERT_NE(colorTarget, nil);
        ASSERT_NE(vertexBuffer, nil);
        ASSERT_NE(uniformBuffer, nil);
        ASSERT_NE(shaderTexture, nil);

        MTLRenderPassDescriptor* pass = CreateRenderPass(colorTarget);
        ASSERT_NE(pass, nil);

        const RenderPassResourceUseFrameGraph frameGraph =
            MakeRenderPassResourceUseFrameGraph(colorTarget, vertexBuffer, uniformBuffer, shaderTexture);
        ASSERT_TRUE(frameGraph.compileResult);
        ASSERT_TRUE(frameGraph.compileResult.HasCompleteAccessTable());
        ASSERT_TRUE(frameGraph.resourceUsages.HasCompleteResourceTable());

        RHI::FrameGraphCompileResult incompleteAccesses = frameGraph.compileResult;
        incompleteAccesses.accessCount =
            static_cast<uint32_t>(RHI::kMaxFrameGraphAccesses + 1);

        MetalCommandContext context;
        ASSERT_TRUE(context.Begin(device, RHI::QueueClass::Graphics));
        id<MTLRenderCommandEncoder> encoder = context.BeginRenderPass(pass);
        ASSERT_NE(encoder, nil);
        EXPECT_FALSE(context.EncodeFrameGraphRenderPassResourceUsages(
            encoder, incompleteAccesses, frameGraph.resourceUsages, 0));
        context.EndRenderPass(encoder);
        EXPECT_TRUE(context.Commit());

        const MetalCommandContextStats stats = context.GetStats();
        EXPECT_EQ(stats.renderPassBeginCount, 1u);
        EXPECT_EQ(stats.renderPassEndCount, 1u);
        EXPECT_EQ(stats.frameGraphResourceUseAttemptCount, 1u);
        EXPECT_EQ(stats.frameGraphResourceUseDeclaredCount, 0u);
        EXPECT_EQ(stats.frameGraphResourceUseSkippedCount, 0u);
        EXPECT_EQ(stats.frameGraphResourceUseFailureCount, 1u);
        EXPECT_TRUE(stats.HasFrameGraphResourceUseAttempts());
        EXPECT_FALSE(stats.HasFrameGraphResourceUses());
        EXPECT_TRUE(stats.HasFailures());
    }
}

TEST_F(MetalCommandContextTest, RejectsIncompleteFrameGraphResourceUsageTables) {
    @autoreleasepool {
        id<MTLTexture> colorTarget = CreateColorTarget();
        id<MTLBuffer> vertexBuffer = CreateBuffer();
        id<MTLBuffer> uniformBuffer = CreateBuffer();
        id<MTLTexture> shaderTexture = CreateShaderTexture();
        ASSERT_NE(colorTarget, nil);
        ASSERT_NE(vertexBuffer, nil);
        ASSERT_NE(uniformBuffer, nil);
        ASSERT_NE(shaderTexture, nil);

        MTLRenderPassDescriptor* pass = CreateRenderPass(colorTarget);
        ASSERT_NE(pass, nil);

        const RenderPassResourceUseFrameGraph frameGraph =
            MakeRenderPassResourceUseFrameGraph(colorTarget, vertexBuffer, uniformBuffer, shaderTexture);
        ASSERT_TRUE(frameGraph.compileResult);
        ASSERT_TRUE(frameGraph.compileResult.HasCompleteResourceTable());
        ASSERT_TRUE(frameGraph.compileResult.HasCompletePassTable());
        ASSERT_TRUE(frameGraph.resourceUsages.HasCompleteResourceTable());
        ASSERT_TRUE(frameGraph.resourceUsages.MatchesCompileResult(frameGraph.compileResult));

        MetalFrameGraphResourceUsageTable incompleteResourceUsages = frameGraph.resourceUsages;
        incompleteResourceUsages.resources[1] = {};
        EXPECT_FALSE(incompleteResourceUsages.HasCompleteResourceTable());

        MetalCommandContext context;
        ASSERT_TRUE(context.Begin(device, RHI::QueueClass::Graphics));
        id<MTLRenderCommandEncoder> encoder = context.BeginRenderPass(pass);
        ASSERT_NE(encoder, nil);
        EXPECT_FALSE(context.EncodeFrameGraphRenderPassResourceUsages(
            encoder, frameGraph.compileResult, incompleteResourceUsages, 0));
        context.EndRenderPass(encoder);
        EXPECT_TRUE(context.Commit());

        const MetalCommandContextStats stats = context.GetStats();
        EXPECT_EQ(stats.renderPassBeginCount, 1u);
        EXPECT_EQ(stats.renderPassEndCount, 1u);
        EXPECT_EQ(stats.frameGraphResourceUseAttemptCount, 1u);
        EXPECT_EQ(stats.frameGraphResourceUseDeclaredCount, 0u);
        EXPECT_EQ(stats.frameGraphResourceUseSkippedCount, 0u);
        EXPECT_EQ(stats.frameGraphResourceUseFailureCount, 1u);
        EXPECT_TRUE(stats.HasFrameGraphResourceUseAttempts());
        EXPECT_FALSE(stats.HasFrameGraphResourceUses());
        EXPECT_TRUE(stats.HasFailures());
    }
}

TEST_F(MetalCommandContextTest, RejectsFrameGraphResourceUsageCountMismatch) {
    @autoreleasepool {
        id<MTLTexture> colorTarget = CreateColorTarget();
        id<MTLBuffer> vertexBuffer = CreateBuffer();
        id<MTLBuffer> uniformBuffer = CreateBuffer();
        id<MTLTexture> shaderTexture = CreateShaderTexture();
        id<MTLBuffer> extraBuffer = CreateBuffer();
        ASSERT_NE(colorTarget, nil);
        ASSERT_NE(vertexBuffer, nil);
        ASSERT_NE(uniformBuffer, nil);
        ASSERT_NE(shaderTexture, nil);
        ASSERT_NE(extraBuffer, nil);

        MTLRenderPassDescriptor* pass = CreateRenderPass(colorTarget);
        ASSERT_NE(pass, nil);

        const RenderPassResourceUseFrameGraph frameGraph =
            MakeRenderPassResourceUseFrameGraph(colorTarget, vertexBuffer, uniformBuffer, shaderTexture);
        ASSERT_TRUE(frameGraph.compileResult);
        ASSERT_TRUE(frameGraph.compileResult.HasCompleteResourceTable());
        ASSERT_TRUE(frameGraph.compileResult.HasCompletePassTable());
        ASSERT_TRUE(frameGraph.resourceUsages.HasCompleteResourceTable());
        ASSERT_TRUE(frameGraph.resourceUsages.MatchesCompileResult(frameGraph.compileResult));

        MetalFrameGraphResourceUsageTable extraResourceUsages = frameGraph.resourceUsages;
        ASSERT_TRUE(extraResourceUsages.SetResource(frameGraph.compileResult.resourceCount,
                                                    extraBuffer,
                                                    RHI::ResourceType::Buffer));
        EXPECT_TRUE(extraResourceUsages.HasCompleteResourceTable());
        EXPECT_FALSE(extraResourceUsages.MatchesCompileResult(frameGraph.compileResult));

        MetalCommandContext context;
        ASSERT_TRUE(context.Begin(device, RHI::QueueClass::Graphics));
        id<MTLRenderCommandEncoder> encoder = context.BeginRenderPass(pass);
        ASSERT_NE(encoder, nil);
        EXPECT_FALSE(context.EncodeFrameGraphRenderPassResourceUsages(
            encoder, frameGraph.compileResult, extraResourceUsages, 0));
        context.EndRenderPass(encoder);
        EXPECT_TRUE(context.Commit());

        const MetalCommandContextStats stats = context.GetStats();
        EXPECT_EQ(stats.renderPassBeginCount, 1u);
        EXPECT_EQ(stats.renderPassEndCount, 1u);
        EXPECT_EQ(stats.frameGraphResourceUseAttemptCount, 1u);
        EXPECT_EQ(stats.frameGraphResourceUseDeclaredCount, 0u);
        EXPECT_EQ(stats.frameGraphResourceUseSkippedCount, 0u);
        EXPECT_EQ(stats.frameGraphResourceUseFailureCount, 1u);
        EXPECT_TRUE(stats.HasFrameGraphResourceUseAttempts());
        EXPECT_FALSE(stats.HasFrameGraphResourceUses());
        EXPECT_TRUE(stats.HasFailures());
    }
}

TEST_F(MetalCommandContextTest, RejectsFrameGraphResourceUsageTypeMismatchForSkippedResources) {
    @autoreleasepool {
        id<MTLTexture> colorTarget = CreateColorTarget();
        id<MTLBuffer> vertexBuffer = CreateBuffer();
        id<MTLBuffer> uniformBuffer = CreateBuffer();
        id<MTLTexture> shaderTexture = CreateShaderTexture();
        ASSERT_NE(colorTarget, nil);
        ASSERT_NE(vertexBuffer, nil);
        ASSERT_NE(uniformBuffer, nil);
        ASSERT_NE(shaderTexture, nil);

        MTLRenderPassDescriptor* pass = CreateRenderPass(colorTarget);
        ASSERT_NE(pass, nil);

        const RenderPassResourceUseFrameGraph frameGraph =
            MakeRenderPassResourceUseFrameGraph(colorTarget, vertexBuffer, uniformBuffer, shaderTexture);
        ASSERT_TRUE(frameGraph.compileResult);
        ASSERT_TRUE(frameGraph.compileResult.HasCompleteResourceTable());
        ASSERT_TRUE(frameGraph.resourceUsages.HasCompleteResourceTable());
        ASSERT_TRUE(frameGraph.resourceUsages.MatchesCompileResult(frameGraph.compileResult));
        ASSERT_EQ(frameGraph.compileResult.GetResourceType(0u), RHI::ResourceType::Texture);

        MetalFrameGraphResourceUsageTable mismatchedResourceUsages = frameGraph.resourceUsages;
        mismatchedResourceUsages.resources[0].resource = vertexBuffer;
        mismatchedResourceUsages.resources[0].type = RHI::ResourceType::Buffer;
        EXPECT_TRUE(mismatchedResourceUsages.HasCompleteResourceTable());
        EXPECT_FALSE(mismatchedResourceUsages.MatchesCompileResult(frameGraph.compileResult));

        MetalCommandContext context;
        ASSERT_TRUE(context.Begin(device, RHI::QueueClass::Graphics));
        id<MTLRenderCommandEncoder> encoder = context.BeginRenderPass(pass);
        ASSERT_NE(encoder, nil);
        EXPECT_FALSE(context.EncodeFrameGraphRenderPassResourceUsages(
            encoder, frameGraph.compileResult, mismatchedResourceUsages, 0));
        context.EndRenderPass(encoder);
        EXPECT_TRUE(context.Commit());

        const MetalCommandContextStats stats = context.GetStats();
        EXPECT_EQ(stats.renderPassBeginCount, 1u);
        EXPECT_EQ(stats.renderPassEndCount, 1u);
        EXPECT_EQ(stats.frameGraphResourceUseAttemptCount, 1u);
        EXPECT_EQ(stats.frameGraphResourceUseDeclaredCount, 0u);
        EXPECT_EQ(stats.frameGraphResourceUseSkippedCount, 0u);
        EXPECT_EQ(stats.frameGraphResourceUseFailureCount, 1u);
        EXPECT_TRUE(stats.HasFrameGraphResourceUseAttempts());
        EXPECT_FALSE(stats.HasFrameGraphResourceUses());
        EXPECT_TRUE(stats.HasFailures());
    }
}

TEST_F(MetalCommandContextTest, RejectsFrameGraphResourceUsageCompiledTypeMismatch) {
    @autoreleasepool {
        id<MTLTexture> colorTarget = CreateColorTarget();
        id<MTLBuffer> vertexBuffer = CreateBuffer();
        id<MTLBuffer> uniformBuffer = CreateBuffer();
        id<MTLTexture> shaderTexture = CreateShaderTexture();
        id<MTLTexture> wrongTexture = CreateShaderTexture();
        ASSERT_NE(colorTarget, nil);
        ASSERT_NE(vertexBuffer, nil);
        ASSERT_NE(uniformBuffer, nil);
        ASSERT_NE(shaderTexture, nil);
        ASSERT_NE(wrongTexture, nil);

        MTLRenderPassDescriptor* pass = CreateRenderPass(colorTarget);
        ASSERT_NE(pass, nil);

        const RenderPassResourceUseFrameGraph frameGraph =
            MakeRenderPassResourceUseFrameGraph(colorTarget, vertexBuffer, uniformBuffer, shaderTexture);
        ASSERT_TRUE(frameGraph.compileResult);
        ASSERT_TRUE(frameGraph.compileResult.HasCompleteResourceTable());
        ASSERT_TRUE(frameGraph.compileResult.HasCompletePassTable());
        ASSERT_EQ(frameGraph.compileResult.GetResourceType(1u), RHI::ResourceType::Buffer);
        ASSERT_TRUE(frameGraph.resourceUsages.HasCompleteResourceTable());

        MetalFrameGraphResourceUsageTable mismatchedResourceUsages = frameGraph.resourceUsages;
        ASSERT_TRUE(mismatchedResourceUsages.SetResource(1, wrongTexture, RHI::ResourceType::Texture));
        EXPECT_TRUE(mismatchedResourceUsages.HasCompleteResourceTable());
        EXPECT_FALSE(mismatchedResourceUsages.MatchesCompileResult(frameGraph.compileResult));

        MetalCommandContext context;
        ASSERT_TRUE(context.Begin(device, RHI::QueueClass::Graphics));
        id<MTLRenderCommandEncoder> encoder = context.BeginRenderPass(pass);
        ASSERT_NE(encoder, nil);
        EXPECT_FALSE(context.EncodeFrameGraphRenderPassResourceUsages(
            encoder, frameGraph.compileResult, mismatchedResourceUsages, 0));
        context.EndRenderPass(encoder);
        EXPECT_TRUE(context.Commit());

        const MetalCommandContextStats stats = context.GetStats();
        EXPECT_EQ(stats.renderPassBeginCount, 1u);
        EXPECT_EQ(stats.renderPassEndCount, 1u);
        EXPECT_EQ(stats.frameGraphResourceUseAttemptCount, 1u);
        EXPECT_EQ(stats.frameGraphResourceUseDeclaredCount, 0u);
        EXPECT_EQ(stats.frameGraphResourceUseSkippedCount, 0u);
        EXPECT_EQ(stats.frameGraphResourceUseFailureCount, 1u);
        EXPECT_TRUE(stats.HasFrameGraphResourceUseAttempts());
        EXPECT_FALSE(stats.HasFrameGraphResourceUses());
        EXPECT_TRUE(stats.HasFailures());
    }
}

TEST_F(MetalCommandContextTest, RejectsMismatchedFrameGraphResourceUsageNativeTypes) {
    @autoreleasepool {
        id<MTLTexture> colorTarget = CreateColorTarget();
        id<MTLBuffer> vertexBuffer = CreateBuffer();
        id<MTLBuffer> uniformBuffer = CreateBuffer();
        id<MTLTexture> shaderTexture = CreateShaderTexture();
        ASSERT_NE(colorTarget, nil);
        ASSERT_NE(vertexBuffer, nil);
        ASSERT_NE(uniformBuffer, nil);
        ASSERT_NE(shaderTexture, nil);

        MTLRenderPassDescriptor* pass = CreateRenderPass(colorTarget);
        ASSERT_NE(pass, nil);

        const RenderPassResourceUseFrameGraph frameGraph =
            MakeRenderPassResourceUseFrameGraph(colorTarget, vertexBuffer, uniformBuffer, shaderTexture);
        ASSERT_TRUE(frameGraph.compileResult);
        ASSERT_TRUE(frameGraph.compileResult.HasCompletePassTable());
        ASSERT_TRUE(frameGraph.resourceUsages.HasCompleteResourceTable());

        MetalFrameGraphResourceUsageTable mismatchedResourceUsages = frameGraph.resourceUsages;
        EXPECT_FALSE(mismatchedResourceUsages.SetResource(1,
                                                          shaderTexture,
                                                          RHI::ResourceType::Buffer));
        mismatchedResourceUsages.resources[1].resource = shaderTexture;
        EXPECT_FALSE(mismatchedResourceUsages.HasCompleteResourceTable());
        EXPECT_TRUE(mismatchedResourceUsages.MatchesCompileResult(frameGraph.compileResult));

        MetalCommandContext context;
        ASSERT_TRUE(context.Begin(device, RHI::QueueClass::Graphics));
        id<MTLRenderCommandEncoder> encoder = context.BeginRenderPass(pass);
        ASSERT_NE(encoder, nil);
        EXPECT_FALSE(context.EncodeFrameGraphRenderPassResourceUsages(
            encoder, frameGraph.compileResult, mismatchedResourceUsages, 0));
        context.EndRenderPass(encoder);
        EXPECT_TRUE(context.Commit());

        const MetalCommandContextStats stats = context.GetStats();
        EXPECT_EQ(stats.renderPassBeginCount, 1u);
        EXPECT_EQ(stats.renderPassEndCount, 1u);
        EXPECT_EQ(stats.frameGraphResourceUseAttemptCount, 1u);
        EXPECT_EQ(stats.frameGraphResourceUseDeclaredCount, 0u);
        EXPECT_EQ(stats.frameGraphResourceUseSkippedCount, 0u);
        EXPECT_EQ(stats.frameGraphResourceUseFailureCount, 1u);
        EXPECT_TRUE(stats.HasFrameGraphResourceUseAttempts());
        EXPECT_FALSE(stats.HasFrameGraphResourceUses());
        EXPECT_TRUE(stats.HasFailures());
    }
}

} // namespace testing
} // namespace MetalBackend
} // namespace Next
