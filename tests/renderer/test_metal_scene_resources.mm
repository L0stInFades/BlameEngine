#include "metal_command.h"
#include "metal_resource.h"

#include <gtest/gtest.h>

#import <Metal/Metal.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace Next {
namespace MetalBackend {
namespace testing {
namespace {

class MetalSceneResourcesTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!device.Initialize()) {
            GTEST_SKIP() << "Metal device unavailable";
        }
        if (!device.GetFeatures().SupportsArgumentBufferTier(RHI::ArgumentBufferTier::Tier1)) {
            GTEST_SKIP() << "Metal argument buffers unavailable";
        }
        ASSERT_TRUE(bufferPool.Initialize(device));
        ASSERT_TRUE(texturePool.Initialize(device));
        ASSERT_TRUE(uploadQueue.Initialize(device, bufferPool));
        ASSERT_TRUE(resources.Initialize(device,
                                         bufferPool,
                                         texturePool,
                                         uploadQueue,
                                         RHI::Format::BGRA8Unorm,
                                         RHI::Format::Depth32Float));
    }

    void TearDown() override {
        resources.Shutdown(&device, 6);
        uploadQueue.Shutdown();
        texturePool.Shutdown();
        bufferPool.Shutdown();
        device.Shutdown();
    }

    MetalDevice device;
    MetalBufferPool bufferPool;
    MetalTexturePool texturePool;
    MetalUploadQueue uploadQueue;
    MetalSceneResources resources;
};

RendererTextureUploadDesc MakeTextureUpload(RendererTextureSlot slot,
                                            uint64_t sourceAssetId,
                                            const std::array<uint8_t, 16>& pixels) {
    RendererTextureUploadDesc desc;
    desc.slot = slot;
    desc.format = RendererTextureFormat::RGBA8Unorm;
    desc.sourceAssetId = sourceAssetId;
    desc.width = 2;
    desc.height = 2;
    desc.pixels = pixels.data();
    desc.pixelBytes = pixels.size();
    return desc;
}

std::array<uint8_t, 16> MakePixels(uint8_t seed) {
    return {
        static_cast<uint8_t>(seed + 0), static_cast<uint8_t>(seed + 1),
        static_cast<uint8_t>(seed + 2), 0xff,
        static_cast<uint8_t>(seed + 3), static_cast<uint8_t>(seed + 4),
        static_cast<uint8_t>(seed + 5), 0xff,
        static_cast<uint8_t>(seed + 6), static_cast<uint8_t>(seed + 7),
        static_cast<uint8_t>(seed + 8), 0xff,
        static_cast<uint8_t>(seed + 9), static_cast<uint8_t>(seed + 10),
        static_cast<uint8_t>(seed + 11), 0xff,
    };
}

id<MTLTexture> CreateRenderTarget(MetalDevice& device, MTLPixelFormat pixelFormat) {
    @autoreleasepool {
        MTLTextureDescriptor* desc =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:pixelFormat
                                                               width:4
                                                              height:4
                                                           mipmapped:NO];
        desc.usage = MTLTextureUsageRenderTarget;
        desc.storageMode = MTLStorageModePrivate;
        return [device.NativeDevice() newTextureWithDescriptor:desc];
    }
}

MTLRenderPassDescriptor* CreateSceneResourcesRenderPass(id<MTLTexture> colorTarget,
                                                        id<MTLTexture> depthTarget) {
    @autoreleasepool {
        MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = colorTarget;
        pass.colorAttachments[0].loadAction = MTLLoadActionClear;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        pass.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 1.0);
        pass.depthAttachment.texture = depthTarget;
        pass.depthAttachment.loadAction = MTLLoadActionClear;
        pass.depthAttachment.storeAction = MTLStoreActionDontCare;
        pass.depthAttachment.clearDepth = 1.0;
        return pass;
    }
}

} // namespace

TEST_F(MetalSceneResourcesTest, ExposesPipelineShaderAndCacheDiagnostics) {
    ASSERT_TRUE(resources.IsReady());
    const RHI::GraphicsPipelineDesc* pipelineDesc = resources.GetRenderPipelineDesc();
    ASSERT_NE(pipelineDesc, nullptr);
    EXPECT_STREQ(pipelineDesc->debugName, "NEXT demo forward pipeline");
    ASSERT_GT(pipelineDesc->colorAttachmentCount, 0u);
    EXPECT_EQ(pipelineDesc->colorAttachments[0].format, RHI::Format::BGRA8Unorm);
    EXPECT_EQ(pipelineDesc->depthStencilFormat, RHI::Format::Depth32Float);
    EXPECT_EQ(pipelineDesc->multisampleState.sampleCount, 1u);
    EXPECT_EQ(pipelineDesc->vertexInput.bufferCount, 1u);
    EXPECT_EQ(pipelineDesc->vertexInput.buffers[0].stride, sizeof(MetalVertex));
    EXPECT_EQ(pipelineDesc->vertexInput.buffers[0].stepFunction, RHI::VertexStepFunction::PerVertex);
    EXPECT_EQ(pipelineDesc->vertexInput.buffers[0].stepRate, 1u);
    EXPECT_EQ(pipelineDesc->vertexInput.attributeCount, 4u);
    EXPECT_EQ(pipelineDesc->vertexInput.attributes[0].location, 0u);
    EXPECT_EQ(pipelineDesc->vertexInput.attributes[0].bufferIndex, kRendererGeometryVertexBufferIndex);
    EXPECT_EQ(pipelineDesc->vertexInput.attributes[0].format, RHI::VertexFormat::Float32x3);
    EXPECT_EQ(pipelineDesc->vertexInput.attributes[0].offset, offsetof(MetalVertex, position));
    EXPECT_EQ(pipelineDesc->vertexInput.attributes[1].location, 1u);
    EXPECT_EQ(pipelineDesc->vertexInput.attributes[1].bufferIndex, kRendererGeometryVertexBufferIndex);
    EXPECT_EQ(pipelineDesc->vertexInput.attributes[1].format, RHI::VertexFormat::Float32x3);
    EXPECT_EQ(pipelineDesc->vertexInput.attributes[1].offset, offsetof(MetalVertex, normal));
    EXPECT_EQ(pipelineDesc->vertexInput.attributes[2].location, 2u);
    EXPECT_EQ(pipelineDesc->vertexInput.attributes[2].bufferIndex, kRendererGeometryVertexBufferIndex);
    EXPECT_EQ(pipelineDesc->vertexInput.attributes[2].format, RHI::VertexFormat::Float32x2);
    EXPECT_EQ(pipelineDesc->vertexInput.attributes[2].offset, offsetof(MetalVertex, texcoord));
    EXPECT_EQ(pipelineDesc->vertexInput.attributes[3].location, 3u);
    EXPECT_EQ(pipelineDesc->vertexInput.attributes[3].bufferIndex, kRendererGeometryVertexBufferIndex);
    EXPECT_EQ(pipelineDesc->vertexInput.attributes[3].format, RHI::VertexFormat::Float32x3);
    EXPECT_EQ(pipelineDesc->vertexInput.attributes[3].offset, offsetof(MetalVertex, albedo));
    EXPECT_STREQ(resources.GetRenderPipelineDebugName(), "NEXT demo forward pipeline");

    const MetalShaderLibraryDesc& shaderDesc = resources.GetShaderLibraryDesc();
    EXPECT_EQ(shaderDesc.manifestVersion, kMetalShaderManifestVersion);
    EXPECT_EQ(shaderDesc.debugName, "NEXT demo forward shader");
    EXPECT_EQ(shaderDesc.vertexEntryPoint, "vertex_main");
    EXPECT_EQ(shaderDesc.fragmentEntryPoint, "fragment_main_material_srg");
    EXPECT_EQ(shaderDesc.materialLayout, kMetalDemoForwardMaterialLayoutName);
    EXPECT_EQ(shaderDesc.pipelineLayout, kMetalDemoForwardPipelineLayoutName);
    EXPECT_EQ(shaderDesc.requiredArgumentBufferTier, kMetalDemoForwardRequiredArgumentBufferTier);
    EXPECT_EQ(shaderDesc.materialShaderResourceGroupArgumentBufferIndex,
              kMetalDemoForwardMaterialShaderResourceGroupArgumentBufferIndex);
    EXPECT_EQ(shaderDesc.materialShaderResourceGroupUniformArgumentIndex,
              kMetalDemoForwardMaterialShaderResourceGroupUniformArgumentIndex);
    EXPECT_EQ(shaderDesc.materialShaderResourceGroupTextureArgumentBaseIndex,
              kMetalDemoForwardMaterialShaderResourceGroupTextureArgumentBaseIndex);
    EXPECT_EQ(shaderDesc.materialShaderResourceGroupSamplerArgumentBaseIndex,
              kMetalDemoForwardMaterialShaderResourceGroupSamplerArgumentBaseIndex);
    EXPECT_FALSE(shaderDesc.manifestPath.empty());

    const MetalShaderLibraryInput shaderInput = resources.GetShaderLibraryInput();
    EXPECT_TRUE(shaderInput.IsValid());
    EXPECT_FALSE(shaderInput.path.empty());

    MetalPipelineCacheStats cacheStats = resources.GetPipelineCacheStats();
    EXPECT_EQ(cacheStats.cachedPipelineCount, 1u);
    EXPECT_EQ(cacheStats.requestCount, 1u);
    EXPECT_EQ(cacheStats.hitCount, 0u);
    EXPECT_EQ(cacheStats.missCount, 1u);
    EXPECT_EQ(cacheStats.failedCreateCount, 0u);
    EXPECT_TRUE(cacheStats.HasCachedPipelines());

    RendererSamplerStats samplerStats = resources.GetSamplerStats();
    EXPECT_TRUE(samplerStats.ready);
    EXPECT_EQ(samplerStats.cachedSamplerCount, 1u);
    EXPECT_EQ(samplerStats.materialSamplerSlotCount, kRendererTextureSlotCount);
    EXPECT_EQ(samplerStats.boundMaterialSamplerCount, kRendererTextureSlotCount);
    EXPECT_TRUE(samplerStats.HasCachedSamplers());
    EXPECT_TRUE(samplerStats.HasMaterialSamplerSlots());
    EXPECT_TRUE(samplerStats.HasBoundMaterialSamplers());
    EXPECT_TRUE(samplerStats.HasCompleteMaterialSamplerTable());
    EXPECT_TRUE(samplerStats.HasSamplerActivity());

    RendererGeometryStats geometryStats = resources.GetGeometryStats();
    EXPECT_TRUE(geometryStats.ready);
    EXPECT_TRUE(geometryStats.vertexBufferReady);
    EXPECT_TRUE(geometryStats.indexBufferReady);
    EXPECT_EQ(geometryStats.vertexBufferIndex, kRendererGeometryVertexBufferIndex);
    EXPECT_EQ(geometryStats.vertexStride, sizeof(MetalVertex));
    EXPECT_GT(geometryStats.vertexBufferBytes, 0u);
    EXPECT_GT(geometryStats.indexBufferBytes, 0u);
    EXPECT_EQ(geometryStats.indexFormat, RHI::IndexFormat::Uint16);
    EXPECT_EQ(geometryStats.indexBufferByteOffset, 0u);
    EXPECT_EQ(geometryStats.resolvedIndexBufferByteOffset, 0u);
    EXPECT_EQ(geometryStats.indexCount, 36u);
    EXPECT_EQ(geometryStats.instanceCount, 1u);
    EXPECT_EQ(geometryStats.indexOffset, 0u);
    EXPECT_EQ(geometryStats.vertexOffset, 0);
    EXPECT_EQ(geometryStats.instanceOffset, 0u);
    EXPECT_EQ(geometryStats.stencilReference, 0u);
    EXPECT_FALSE(geometryStats.HasBlendConstant());
    EXPECT_TRUE(geometryStats.HasVertexBuffer());
    EXPECT_TRUE(geometryStats.HasIndexBuffer());
    EXPECT_TRUE(geometryStats.HasVertexStride());
    EXPECT_TRUE(geometryStats.HasIndexFormat());
    EXPECT_TRUE(geometryStats.HasIndexCount());
    EXPECT_TRUE(geometryStats.HasInstances());
    EXPECT_TRUE(geometryStats.HasResolvedIndexBufferOffset());
    EXPECT_TRUE(geometryStats.HasIndexedDraw());
    EXPECT_TRUE(geometryStats.HasReadyGeometry());

    RendererResourceStateStats resourceStateStats = resources.GetResourceStateStats();
    EXPECT_TRUE(resourceStateStats.ready);
    EXPECT_EQ(resourceStateStats.frameGraphValidation.error, RHI::FrameGraphDescriptorError::None);
    EXPECT_EQ(resourceStateStats.frameGraphTransitionCount, kRendererResourceStateMaxCount);
    EXPECT_EQ(resourceStateStats.frameGraphPassCount, 1u);
    EXPECT_EQ(resourceStateStats.frameGraphReadyPassIndex, 0u);
    EXPECT_EQ(resourceStateStats.frameGraphReadyPassTransitionCount, kRendererResourceStateMaxCount);
    EXPECT_TRUE(resourceStateStats.HasValidFrameGraphResourcePlan());
    EXPECT_TRUE(resourceStateStats.HasFrameGraphTransitions());
    EXPECT_TRUE(resourceStateStats.HasFrameGraphPasses());
    EXPECT_TRUE(resourceStateStats.HasFrameGraphReadyPass());
    EXPECT_EQ(resourceStateStats.resourceCount, kRendererResourceStateMaxCount);
    EXPECT_EQ(resourceStateStats.bufferResourceCount, 3u);
    EXPECT_EQ(resourceStateStats.textureResourceCount, kRendererTextureSlotCount);
    EXPECT_EQ(resourceStateStats.expectedStateMatchCount, kRendererResourceStateMaxCount);
    EXPECT_TRUE(resourceStateStats.HasReadyResourceStates());
    ASSERT_NE(resourceStateStats.GetResource(0), nullptr);
    EXPECT_EQ(resourceStateStats.GetResource(0)->kind, RendererResourceStateKind::VertexBuffer);
    EXPECT_EQ(resourceStateStats.GetResource(0)->currentState, RHI::ResourceState::VertexBuffer);
    const RendererResourceStateInfo* baseColorState =
        resourceStateStats.GetMaterialTexture(RendererTextureSlot::BaseColor);
    ASSERT_NE(baseColorState, nullptr);
    EXPECT_EQ(baseColorState->currentState, RHI::ResourceState::ShaderRead);
    EXPECT_EQ(baseColorState->expectedState, RHI::ResourceState::ShaderRead);

    resources.Shutdown(&device, 6);
    EXPECT_EQ(resources.GetRenderPipelineDesc(), nullptr);
    EXPECT_STREQ(resources.GetRenderPipelineDebugName(), "");
    cacheStats = resources.GetPipelineCacheStats();
    EXPECT_EQ(cacheStats.cachedPipelineCount, 0u);
    EXPECT_FALSE(cacheStats.HasCachedPipelines());

    samplerStats = resources.GetSamplerStats();
    EXPECT_FALSE(samplerStats.ready);
    EXPECT_EQ(samplerStats.cachedSamplerCount, 0u);
    EXPECT_EQ(samplerStats.materialSamplerSlotCount, kRendererTextureSlotCount);
    EXPECT_EQ(samplerStats.boundMaterialSamplerCount, 0u);
    EXPECT_FALSE(samplerStats.HasCachedSamplers());
    EXPECT_TRUE(samplerStats.HasMaterialSamplerSlots());
    EXPECT_FALSE(samplerStats.HasBoundMaterialSamplers());
    EXPECT_FALSE(samplerStats.HasCompleteMaterialSamplerTable());
    EXPECT_FALSE(samplerStats.HasSamplerActivity());

    geometryStats = resources.GetGeometryStats();
    EXPECT_FALSE(geometryStats.ready);
    EXPECT_FALSE(geometryStats.vertexBufferReady);
    EXPECT_FALSE(geometryStats.indexBufferReady);
    EXPECT_FALSE(geometryStats.HasReadyGeometry());

    resourceStateStats = resources.GetResourceStateStats();
    EXPECT_FALSE(resourceStateStats.ready);
    EXPECT_EQ(resourceStateStats.frameGraphValidation.error, RHI::FrameGraphDescriptorError::None);
    EXPECT_EQ(resourceStateStats.frameGraphTransitionCount, 0u);
    EXPECT_EQ(resourceStateStats.frameGraphPassCount, 0u);
    EXPECT_EQ(resourceStateStats.frameGraphReadyPassIndex, RHI::kInvalidFrameGraphPassIndex);
    EXPECT_EQ(resourceStateStats.frameGraphReadyPassTransitionCount, 0u);
    EXPECT_FALSE(resourceStateStats.HasValidFrameGraphResourcePlan());
    EXPECT_FALSE(resourceStateStats.HasFrameGraphTransitions());
    EXPECT_FALSE(resourceStateStats.HasFrameGraphPasses());
    EXPECT_FALSE(resourceStateStats.HasFrameGraphReadyPass());
    EXPECT_FALSE(resourceStateStats.HasReadyResourceStates());
}

TEST_F(MetalSceneResourcesTest, AppendsDrawResourcesToFrameGraphRenderPass) {
    RHI::FrameGraphDesc graph;
    RHI::FrameGraphPassDesc renderPass;
    renderPass.debugName = "test draw resources";
    renderPass.queueClass = RHI::QueueClass::Graphics;
    MetalFrameGraphResourceUsageTable resourceUsages;
    RHI::FrameGraphDescriptorValidation appendValidation;

    ASSERT_TRUE(resources.AppendDrawFrameGraphResources(
        graph, renderPass, &resourceUsages, appendValidation));
    EXPECT_EQ(appendValidation.error, RHI::FrameGraphDescriptorError::None);
    EXPECT_EQ(graph.resourceCount, 4u + kRendererTextureSlotCount);
    EXPECT_EQ(renderPass.accessCount, 4u + kRendererTextureSlotCount);
    EXPECT_EQ(resourceUsages.resourceCount, graph.resourceCount);
    EXPECT_TRUE(resourceUsages.HasCompleteResourceTable());
    ASSERT_TRUE(RHI::AddFrameGraphPass(graph, renderPass));

    const RHI::FrameGraphCompileResult result = RHI::CompileFrameGraphTransitions(graph);
    ASSERT_TRUE(result);
    EXPECT_TRUE(result.HasAccesses());
    EXPECT_TRUE(result.HasCompleteAccessTable());
    EXPECT_FALSE(result.HasTransitions());
    EXPECT_EQ(result.accessCount, 4u + kRendererTextureSlotCount);
    EXPECT_EQ(result.transitionCount, 0u);
    ASSERT_EQ(result.passCount, 1u);

    const RHI::FrameGraphPassCompileInfo* passInfo = result.GetPass(0);
    ASSERT_NE(passInfo, nullptr);
    EXPECT_TRUE(passInfo->HasAccesses());
    EXPECT_FALSE(passInfo->HasTransitions());
    EXPECT_EQ(passInfo->accessOffset, 0u);
    EXPECT_EQ(passInfo->accessCount, 4u + kRendererTextureSlotCount);
    EXPECT_EQ(passInfo->AccessEndOffset(), 4u + kRendererTextureSlotCount);

    uint32_t bufferAccessCount = 0;
    uint32_t shaderAccessCount = 0;
    uint32_t nativeBufferUseCount = 0;
    uint32_t nativeTextureUseCount = 0;
    uint32_t vertexStageAccessCount = 0;
    uint32_t fragmentStageAccessCount = 0;
    for (uint32_t accessIndex = 0; accessIndex < result.accessCount; ++accessIndex) {
        const RHI::FrameGraphCompiledAccess* access = result.GetAccess(accessIndex);
        ASSERT_NE(access, nullptr);
        const MetalFrameGraphResourceUsage* resourceUse =
            resourceUsages.GetResource(access->resourceIndex);
        ASSERT_NE(resourceUse, nullptr);
        EXPECT_TRUE(resourceUse->IsValid());
        if (resourceUse->IsBuffer()) {
            ++nativeBufferUseCount;
        }
        if (resourceUse->IsTexture()) {
            ++nativeTextureUseCount;
        }
        if (access->state == RHI::ResourceState::VertexBuffer ||
            access->state == RHI::ResourceState::IndexBuffer ||
            access->state == RHI::ResourceState::ConstantBuffer) {
            ++bufferAccessCount;
        }
        if (access->state == RHI::ResourceState::ShaderRead) {
            ++shaderAccessCount;
        }
        if (RHI::HasShaderStage(access->shaderStages, RHI::ShaderStage::Vertex)) {
            ++vertexStageAccessCount;
        }
        if (RHI::HasShaderStage(access->shaderStages, RHI::ShaderStage::Fragment)) {
            ++fragmentStageAccessCount;
        }
    }
    EXPECT_EQ(bufferAccessCount, 3u);
    EXPECT_EQ(shaderAccessCount, 1u + kRendererTextureSlotCount);
    EXPECT_EQ(nativeBufferUseCount, 4u);
    EXPECT_EQ(nativeTextureUseCount, kRendererTextureSlotCount);
    EXPECT_EQ(vertexStageAccessCount, 3u);
    EXPECT_EQ(fragmentStageAccessCount, 2u + kRendererTextureSlotCount);
}

TEST_F(MetalSceneResourcesTest, UploadUniformsReturnsUniformBufferToConstantBufferState) {
    ASSERT_TRUE(resources.IsReady());

    MetalUniforms uniforms{};
    uniforms.mvp[0] = 1.0f;
    uniforms.model[0] = 1.0f;
    uniforms.lightDirection[2] = -1.0f;
    uniforms.material[0] = 1.0f;

    ASSERT_TRUE(resources.UploadUniforms(device, uploadQueue, &uniforms, sizeof(uniforms)));

    const RendererResourceStateStats resourceStateStats = resources.GetResourceStateStats();
    const RendererResourceStateInfo* uniformState = nullptr;
    for (size_t i = 0; i < resourceStateStats.resourceCount; ++i) {
        const RendererResourceStateInfo* resource = resourceStateStats.GetResource(i);
        ASSERT_NE(resource, nullptr);
        if (resource->kind == RendererResourceStateKind::UniformBuffer) {
            uniformState = resource;
            break;
        }
    }

    ASSERT_NE(uniformState, nullptr);
    EXPECT_EQ(uniformState->currentState, RHI::ResourceState::ConstantBuffer);
    EXPECT_EQ(uniformState->expectedState, RHI::ResourceState::ConstantBuffer);
    EXPECT_TRUE(uniformState->MatchesExpectedState());
    EXPECT_TRUE(uniformState->HasReadyResourceState());
    EXPECT_FALSE(uniformState->IsCopyDestination());
    EXPECT_TRUE(resourceStateStats.HasReadyResourceStates());
}

TEST_F(MetalSceneResourcesTest, BindDrawStateBindsMaterialShaderResourceGroupArgumentBuffer) {
    @autoreleasepool {
        id<MTLTexture> colorTarget = CreateRenderTarget(device, MTLPixelFormatBGRA8Unorm);
        id<MTLTexture> depthTarget = CreateRenderTarget(device, MTLPixelFormatDepth32Float);
        ASSERT_NE(colorTarget, nil);
        ASSERT_NE(depthTarget, nil);

        MTLRenderPassDescriptor* pass = CreateSceneResourcesRenderPass(colorTarget, depthTarget);
        ASSERT_NE(pass, nil);

        MetalCommandContext context;
        ASSERT_TRUE(context.Begin(device, RHI::QueueClass::Graphics));
        id<MTLRenderCommandEncoder> encoder = context.BeginRenderPass(pass);
        ASSERT_NE(encoder, nil);

        EXPECT_TRUE(resources.EncodeMaterialArgumentBuffers(2));
        EXPECT_TRUE(resources.BindDrawState(encoder, 0));
        EXPECT_TRUE(resources.DrawCube(encoder));
        EXPECT_TRUE(resources.BindDrawState(encoder, kMetalUniformStride, false));
        EXPECT_TRUE(resources.DrawCube(encoder));
        context.EndRenderPass(encoder);
        EXPECT_TRUE(context.Commit());

        const RendererMaterialStats materialStats = resources.GetMaterialStats();
        EXPECT_EQ(materialStats.shaderResourceGroupArgumentBufferBindingIndex,
                  kRendererMaterialShaderResourceGroupArgumentBufferIndex);
        EXPECT_EQ(materialStats.shaderResourceGroupArgumentBufferBindCount, 2u);
        EXPECT_EQ(materialStats.shaderResourceGroupEncodedDrawCount, 2u);
        EXPECT_EQ(materialStats.shaderResourceGroupEncodedResourceCount,
                  materialStats.RequiredShaderResourceGroupEncodedResourceCount() *
                      materialStats.shaderResourceGroupEncodedDrawCount);
        EXPECT_TRUE(materialStats.HasBoundShaderResourceGroupArgumentBuffer());
        EXPECT_TRUE(materialStats.HasShaderResourceGroupArgumentBufferBindings());
        EXPECT_TRUE(materialStats.HasCompleteShaderResourceGroupArgumentBufferBinding());
    }
}

TEST_F(MetalSceneResourcesTest, MaterialTextureUploadPromotesSlotInfoAndStats) {
    ASSERT_TRUE(resources.IsReady());

    RendererTextureUploadStats initialStats = resources.GetTextureUploadStats();
    ASSERT_TRUE(initialStats.activeBaseColorTexture);
    EXPECT_EQ(initialStats.activeMaterialTextureCount, 1u);
    EXPECT_FALSE(initialStats.materialTextureUploadPending);

    const std::array<uint8_t, 16> pixels = MakePixels(0x10);
    const RendererTextureUploadDesc upload =
        MakeTextureUpload(RendererTextureSlot::Normal, 42, pixels);
    const RendererTextureUploadHandle handle =
        resources.UploadMaterialTexture(device, uploadQueue, upload, 1);
    ASSERT_TRUE(handle);
    ASSERT_TRUE(handle.texture);

    RendererTextureInfo pendingInfo;
    ASSERT_TRUE(resources.GetTextureInfo(handle.texture, pendingInfo));
    EXPECT_EQ(pendingInfo.texture.id, handle.texture.id);
    EXPECT_EQ(pendingInfo.slot, RendererTextureSlot::Normal);
    EXPECT_EQ(pendingInfo.format, RendererTextureFormat::RGBA8Unorm);
    EXPECT_EQ(pendingInfo.sourceAssetId, 42u);
    EXPECT_EQ(pendingInfo.width, 2u);
    EXPECT_EQ(pendingInfo.height, 2u);
    EXPECT_FALSE(pendingInfo.active);
    EXPECT_TRUE(pendingInfo.uploadPending);
    EXPECT_TRUE(pendingInfo.HasTexture());
    EXPECT_TRUE(pendingInfo.HasSourceAsset());
    EXPECT_TRUE(pendingInfo.HasDimensions());
    EXPECT_FALSE(pendingInfo.HasActiveTexture());
    EXPECT_EQ(resources.GetTextureUploadStatus(handle, uploadQueue),
              RendererTextureUploadStatus::Pending);

    RendererTextureUploadStats stats = resources.GetTextureUploadStats();
    EXPECT_EQ(stats.queuedUploads, 1u);
    EXPECT_EQ(stats.completedUploads, 0u);
    EXPECT_TRUE(stats.materialTextureUploadPending);
    EXPECT_EQ(stats.lastQueuedUpload, handle.id);
    EXPECT_EQ(stats.lastQueuedTexture.id, handle.texture.id);
    EXPECT_EQ(stats.lastQueuedSlot, RendererTextureSlot::Normal);

    EXPECT_EQ(uploadQueue.WaitForUploadStatus(MetalUploadHandle{handle.id}),
              MetalUploadStatus::Completed);
    resources.PromoteCompletedMaterialTextureUploads(uploadQueue, 2);

    RendererTextureInfo activeInfo;
    ASSERT_TRUE(resources.GetTextureInfo(handle.texture, activeInfo));
    EXPECT_TRUE(activeInfo.active);
    EXPECT_FALSE(activeInfo.uploadPending);
    EXPECT_TRUE(activeInfo.HasTexture());
    EXPECT_TRUE(activeInfo.HasSourceAsset());
    EXPECT_TRUE(activeInfo.HasDimensions());
    EXPECT_TRUE(activeInfo.HasActiveTexture());
    EXPECT_EQ(resources.GetTextureUploadStatus(handle, uploadQueue),
              RendererTextureUploadStatus::Completed);

    const RendererResourceStateStats resourceStateStats = resources.GetResourceStateStats();
    const RendererResourceStateInfo* normalState =
        resourceStateStats.GetMaterialTexture(RendererTextureSlot::Normal);
    ASSERT_NE(normalState, nullptr);
    EXPECT_EQ(normalState->currentState, RHI::ResourceState::ShaderRead);
    EXPECT_EQ(normalState->expectedState, RHI::ResourceState::ShaderRead);
    EXPECT_TRUE(normalState->MatchesExpectedState());

    stats = resources.GetTextureUploadStats();
    EXPECT_EQ(stats.queuedUploads, 1u);
    EXPECT_EQ(stats.completedUploads, 1u);
    EXPECT_EQ(stats.failedUploads, 0u);
    EXPECT_FALSE(stats.materialTextureUploadPending);
    EXPECT_EQ(stats.activeMaterialTextureCount, 2u);
    EXPECT_EQ(stats.lastCompletedUpload, handle.id);
    EXPECT_EQ(stats.lastCompletedTexture.id, handle.texture.id);
    EXPECT_EQ(stats.lastCompletedSlot, RendererTextureSlot::Normal);

    const RendererActiveTextureSlotInfo normalInfo =
        stats.GetActiveTexture(RendererTextureSlot::Normal);
    EXPECT_TRUE(normalInfo.active);
    EXPECT_TRUE(normalInfo.HasTexture());
    EXPECT_TRUE(normalInfo.HasSourceAsset());
    EXPECT_TRUE(normalInfo.HasDimensions());
    EXPECT_TRUE(normalInfo.HasActiveTexture());
    EXPECT_EQ(normalInfo.texture.id, handle.texture.id);
    EXPECT_EQ(normalInfo.sourceAssetId, 42u);
    EXPECT_EQ(normalInfo.width, 2u);
    EXPECT_EQ(normalInfo.height, 2u);
}

TEST_F(MetalSceneResourcesTest, RejectsInvalidAndSameSlotPendingUploads) {
    const std::array<uint8_t, 16> pixels = MakePixels(0x30);
    RendererTextureUploadDesc invalid = MakeTextureUpload(RendererTextureSlot::Emissive, 7, pixels);
    invalid.pixels = nullptr;
    EXPECT_FALSE(resources.UploadMaterialTexture(device, uploadQueue, invalid, 1));
    EXPECT_EQ(resources.GetTextureUploadStats().queuedUploads, 0u);

    const RendererTextureUploadDesc upload =
        MakeTextureUpload(RendererTextureSlot::Emissive, 8, pixels);
    const RendererTextureUploadHandle first =
        resources.UploadMaterialTexture(device, uploadQueue, upload, 1);
    ASSERT_TRUE(first);

    const std::array<uint8_t, 16> secondPixels = MakePixels(0x50);
    const RendererTextureUploadDesc second =
        MakeTextureUpload(RendererTextureSlot::Emissive, 9, secondPixels);
    EXPECT_FALSE(resources.UploadMaterialTexture(device, uploadQueue, second, 1));
    EXPECT_EQ(resources.GetTextureUploadStats().queuedUploads, 1u);

    EXPECT_EQ(uploadQueue.WaitForUploadStatus(MetalUploadHandle{first.id}),
              MetalUploadStatus::Completed);
    resources.PromoteCompletedMaterialTextureUploads(uploadQueue, 2);
    EXPECT_EQ(resources.GetTextureUploadStats().completedUploads, 1u);
}

TEST_F(MetalSceneResourcesTest, MaterialTableCreatesUpdatesAndActivatesRecords) {
    const RendererTextureUploadStats initialStats = resources.GetTextureUploadStats();
    ASSERT_TRUE(initialStats.activeBaseColorTexture);

    RendererMaterialStats materialStats = resources.GetMaterialStats();
    EXPECT_TRUE(materialStats.ready);
    EXPECT_EQ(materialStats.materialCapacity, 16u);
    EXPECT_EQ(materialStats.materialCount, 1u);
    EXPECT_TRUE(materialStats.HasFreeMaterialSlots());
    EXPECT_FALSE(materialStats.IsMaterialTableFull());
    EXPECT_TRUE(materialStats.HasActiveMaterial());
    EXPECT_EQ(materialStats.activeMaterialIndex, 0u);
    EXPECT_EQ(materialStats.activeMaterialBoundTextureCount, 1u);
    EXPECT_FALSE(materialStats.HasCompleteActiveTextureSet());
    EXPECT_TRUE(materialStats.HasValidActiveParameters());
    EXPECT_EQ(materialStats.shaderVisibleTextureCount, kRendererTextureSlotCount);
    EXPECT_EQ(materialStats.shaderVisibleSamplerCount, kRendererTextureSlotCount);
    EXPECT_EQ(materialStats.fallbackTextureCount, 4u);
    EXPECT_TRUE(materialStats.HasCompleteShaderBindings());
    EXPECT_EQ(materialStats.bindingLayout.vertexBufferIndex, kRendererGeometryVertexBufferIndex);
    EXPECT_EQ(materialStats.bindingLayout.uniformBufferIndex, kRendererMaterialUniformBufferIndex);
    EXPECT_EQ(materialStats.bindingLayout.textureBindingCount, kRendererTextureSlotCount);
    EXPECT_EQ(materialStats.bindingLayout.samplerBindingCount, kRendererTextureSlotCount);
    EXPECT_TRUE(materialStats.HasMaterialBindingLayout());
    EXPECT_EQ(materialStats.shaderResourceGroupBindingCount,
              kRendererMaterialShaderResourceGroupBindingCount);
    EXPECT_TRUE(materialStats.HasShaderResourceGroupLayout());
    EXPECT_TRUE(materialStats.HasCompleteShaderResourceGroupBindingTable());
    EXPECT_TRUE(materialStats.HasValidShaderResourceGroupLayout());
    EXPECT_TRUE(materialStats.HasCompleteShaderResourceGroupResources());
    EXPECT_TRUE(materialStats.HasShaderResourceGroupArgumentEncoder());
    EXPECT_EQ(materialStats.shaderResourceGroupArgumentCount,
              kRendererMaterialShaderResourceGroupBindingCount);
    EXPECT_TRUE(materialStats.HasShaderResourceGroupEncodedLength());
    EXPECT_GE(materialStats.shaderResourceGroupEncodedStride,
              materialStats.shaderResourceGroupEncodedLength);
    EXPECT_TRUE(materialStats.HasReadyShaderResourceGroupArgumentEncoder());
    EXPECT_TRUE(materialStats.HasShaderResourceGroupArgumentBuffer());
    EXPECT_EQ(materialStats.shaderResourceGroupArgumentBufferBytes,
              materialStats.shaderResourceGroupEncodedStride *
                  materialStats.shaderResourceGroupArgumentBufferDrawCapacity);
    EXPECT_EQ(materialStats.shaderResourceGroupArgumentBufferDrawCapacity,
              kMaxRendererDebugCells + 1u);
    EXPECT_EQ(materialStats.shaderResourceGroupEncodedDrawCount, 1u);
    EXPECT_EQ(materialStats.shaderResourceGroupArgumentBufferBindingIndex,
              kRendererMaterialShaderResourceGroupArgumentBufferIndex);
    EXPECT_FALSE(materialStats.HasBoundShaderResourceGroupArgumentBuffer());
    EXPECT_EQ(materialStats.shaderResourceGroupArgumentBufferBindCount, 0u);
    EXPECT_EQ(materialStats.shaderResourceGroupEncodedResourceCount,
              materialStats.RequiredShaderResourceGroupEncodedResourceCount() *
                  materialStats.shaderResourceGroupEncodedDrawCount);
    EXPECT_TRUE(materialStats.HasCompleteShaderResourceGroupEncoding());
    EXPECT_FALSE(materialStats.HasCompleteShaderResourceGroupArgumentBufferBinding());
    EXPECT_EQ(materialStats.shaderResourceGroupLayoutValidation.error,
              RHI::ShaderResourceGroupLayoutError::None);
    ASSERT_NE(materialStats.GetShaderResourceGroupBinding(0), nullptr);
    EXPECT_TRUE(materialStats.GetShaderResourceGroupBinding(0)->IsConstantBuffer());
    EXPECT_TRUE(materialStats.GetShaderResourceGroupBinding(0)->HasVertexStage());
    EXPECT_TRUE(materialStats.GetShaderResourceGroupBinding(0)->HasFragmentStage());
    EXPECT_EQ(materialStats.GetShaderResourceGroupBinding(0)->bindingIndex,
              kRendererMaterialUniformBufferIndex);
    EXPECT_EQ(materialStats.GetShaderResourceGroupBinding(0)->boundResourceCount, 1u);
    EXPECT_TRUE(materialStats.GetShaderResourceGroupBinding(0)->HasCompleteBoundResources());
    ASSERT_NE(materialStats.GetShaderResourceGroupBinding(1), nullptr);
    EXPECT_TRUE(materialStats.GetShaderResourceGroupBinding(1)->IsTexture());
    EXPECT_EQ(materialStats.GetShaderResourceGroupBinding(1)->bindingCount,
              kRendererTextureSlotCount);
    EXPECT_EQ(materialStats.GetShaderResourceGroupBinding(1)->boundResourceCount,
              kRendererTextureSlotCount);
    EXPECT_TRUE(materialStats.GetShaderResourceGroupBinding(1)->HasCompleteBoundResources());
    ASSERT_NE(materialStats.GetShaderResourceGroupBinding(2), nullptr);
    EXPECT_TRUE(materialStats.GetShaderResourceGroupBinding(2)->IsSampler());
    EXPECT_EQ(materialStats.GetShaderResourceGroupBinding(2)->bindingCount,
              kRendererTextureSlotCount);
    EXPECT_EQ(materialStats.GetShaderResourceGroupBinding(2)->boundResourceCount,
              kRendererTextureSlotCount);
    EXPECT_TRUE(materialStats.GetShaderResourceGroupBinding(2)->HasCompleteBoundResources());
    const RendererMaterialBindingInfo initialBaseColor =
        materialStats.GetActiveMaterialBinding(RendererTextureSlot::BaseColor);
    EXPECT_TRUE(initialBaseColor.UsesMaterialTexture());
    EXPECT_TRUE(initialBaseColor.HasBoundTexture());
    EXPECT_TRUE(initialBaseColor.IsShaderVisible());
    EXPECT_EQ(initialBaseColor.textureBindingIndex,
              RendererMaterialTextureBindingIndex(RendererTextureSlot::BaseColor));
    EXPECT_EQ(initialBaseColor.samplerBindingIndex,
              RendererMaterialSamplerBindingIndex(RendererTextureSlot::BaseColor));
    EXPECT_EQ(initialBaseColor.uniformBufferIndex, kRendererMaterialUniformBufferIndex);
    EXPECT_TRUE(initialBaseColor.HasBindingIndices());
    const RendererMaterialBindingInfo initialNormal =
        materialStats.GetActiveMaterialBinding(RendererTextureSlot::Normal);
    EXPECT_TRUE(initialNormal.UsesNeutralTexture());
    EXPECT_TRUE(initialNormal.UsesFallbackTexture());
    EXPECT_TRUE(initialNormal.IsShaderVisible());
    EXPECT_EQ(initialNormal.textureBindingIndex,
              RendererMaterialTextureBindingIndex(RendererTextureSlot::Normal));
    EXPECT_EQ(initialNormal.samplerBindingIndex,
              RendererMaterialSamplerBindingIndex(RendererTextureSlot::Normal));
    EXPECT_EQ(initialNormal.uniformBufferIndex, kRendererMaterialUniformBufferIndex);
    EXPECT_TRUE(initialNormal.HasBindingIndices());

    RendererMaterialDesc material;
    material.sourceAssetId = 77;
    material.roughness = 0.7f;
    material.metallic = 0.2f;
    material.exposure = 1.5f;
    material.baseColorFactor[0] = 0.25f;
    material.baseColorFactor[1] = 0.5f;
    material.baseColorFactor[2] = 0.75f;
    material.baseColorFactor[3] = 1.0f;

    const RendererMaterialHandle handle = resources.CreateMaterial(material);
    ASSERT_TRUE(handle);

    RendererMaterialInfo info;
    ASSERT_TRUE(resources.GetMaterialInfo(handle, info));
    EXPECT_EQ(info.material.id, handle.id);
    EXPECT_FALSE(info.active);
    EXPECT_TRUE(info.HasMaterial());
    EXPECT_FALSE(info.HasActiveMaterial());
    EXPECT_TRUE(info.HasSourceAsset());
    EXPECT_TRUE(info.HasTexture(RendererTextureSlot::BaseColor));
    EXPECT_TRUE(info.HasBoundTextures());
    EXPECT_TRUE(info.HasValidParameters());
    EXPECT_EQ(info.BoundTextureCount(), 1u);
    EXPECT_EQ(info.desc.sourceAssetId, 77u);
    EXPECT_EQ(info.desc.baseColorTexture.id, initialStats.activeBaseColorTexture.id);
    EXPECT_FLOAT_EQ(info.desc.roughness, 0.7f);
    EXPECT_FLOAT_EQ(info.desc.metallic, 0.2f);

    const std::array<uint8_t, 16> normalPixels = MakePixels(0x70);
    const RendererTextureUploadHandle normalUpload =
        resources.UploadMaterialTexture(device,
                                        uploadQueue,
                                        MakeTextureUpload(RendererTextureSlot::Normal, 78, normalPixels),
                                        2);
    ASSERT_TRUE(normalUpload);
    const std::array<uint8_t, 16> occlusionPixels = MakePixels(0x90);
    const RendererTextureUploadHandle occlusionUpload =
        resources.UploadMaterialTexture(device,
                                        uploadQueue,
                                        MakeTextureUpload(RendererTextureSlot::Occlusion, 89, occlusionPixels),
                                        2);
    ASSERT_TRUE(occlusionUpload);

    RendererTextureInfo pendingNormalInfo;
    ASSERT_TRUE(resources.GetTextureInfo(normalUpload.texture, pendingNormalInfo));
    EXPECT_EQ(pendingNormalInfo.slot, RendererTextureSlot::Normal);
    EXPECT_TRUE(pendingNormalInfo.HasPendingUpload());

    RendererMaterialDesc updated = info.desc;
    updated.sourceAssetId = 88;
    updated.normalTexture = normalUpload.texture;
    updated.occlusionTexture = occlusionUpload.texture;
    updated.roughness = 0.33f;
    updated.metallic = 0.66f;
    ASSERT_TRUE(resources.UpdateMaterial(handle, updated));
    ASSERT_TRUE(resources.SetActiveMaterial(handle));
    materialStats = resources.GetMaterialStats();
    EXPECT_TRUE(materialStats.ready);
    EXPECT_EQ(materialStats.materialCount, 2u);
    EXPECT_EQ(materialStats.activeMaterial.id, handle.id);
    EXPECT_EQ(materialStats.activeMaterialIndex, 1u);
    EXPECT_EQ(materialStats.activeMaterialBoundTextureCount, 3u);
    EXPECT_TRUE(materialStats.HasActiveBoundTextures());
    EXPECT_TRUE(materialStats.HasFreeMaterialSlots());
    EXPECT_FALSE(materialStats.IsMaterialTableFull());
    EXPECT_FALSE(materialStats.HasCompleteActiveTextureSet());
    EXPECT_TRUE(materialStats.HasValidActiveParameters());
    EXPECT_EQ(materialStats.shaderVisibleTextureCount, kRendererTextureSlotCount);
    EXPECT_EQ(materialStats.shaderVisibleSamplerCount, kRendererTextureSlotCount);
    EXPECT_EQ(materialStats.fallbackTextureCount, 4u);
    EXPECT_TRUE(materialStats.HasCompleteShaderBindings());
    EXPECT_TRUE(materialStats.HasValidShaderResourceGroupLayout());
    const RendererMaterialBindingInfo pendingNormal =
        materialStats.GetActiveMaterialBinding(RendererTextureSlot::Normal);
    EXPECT_TRUE(pendingNormal.HasRequestedTexture());
    EXPECT_EQ(pendingNormal.requestedTexture.id, normalUpload.texture.id);
    EXPECT_TRUE(pendingNormal.UsesNeutralTexture());
    EXPECT_FALSE(pendingNormal.HasBoundTexture());
    EXPECT_TRUE(pendingNormal.IsShaderVisible());
    EXPECT_EQ(pendingNormal.textureBindingIndex,
              RendererMaterialTextureBindingIndex(RendererTextureSlot::Normal));
    EXPECT_EQ(pendingNormal.samplerBindingIndex,
              RendererMaterialSamplerBindingIndex(RendererTextureSlot::Normal));
    EXPECT_EQ(pendingNormal.uniformBufferIndex, kRendererMaterialUniformBufferIndex);
    EXPECT_TRUE(pendingNormal.HasBindingIndices());

    ASSERT_TRUE(resources.GetMaterialInfo(handle, info));
    EXPECT_TRUE(info.active);
    EXPECT_TRUE(info.HasMaterial());
    EXPECT_TRUE(info.HasActiveMaterial());
    EXPECT_TRUE(info.HasSourceAsset());
    EXPECT_TRUE(info.HasTexture(RendererTextureSlot::BaseColor));
    EXPECT_TRUE(info.HasTexture(RendererTextureSlot::Normal));
    EXPECT_TRUE(info.HasTexture(RendererTextureSlot::Occlusion));
    EXPECT_TRUE(info.HasValidParameters());
    EXPECT_EQ(info.BoundTextureCount(), 3u);
    EXPECT_EQ(info.desc.sourceAssetId, 88u);
    EXPECT_EQ(info.desc.normalTexture.id, normalUpload.texture.id);
    EXPECT_EQ(info.desc.occlusionTexture.id, occlusionUpload.texture.id);
    EXPECT_FLOAT_EQ(info.desc.roughness, 0.33f);
    EXPECT_FLOAT_EQ(info.desc.metallic, 0.66f);

    const RendererMaterialDesc active = resources.GetActiveMaterialDesc();
    EXPECT_EQ(active.sourceAssetId, 88u);
    EXPECT_EQ(active.normalTexture.id, normalUpload.texture.id);
    EXPECT_EQ(active.occlusionTexture.id, occlusionUpload.texture.id);

    EXPECT_EQ(uploadQueue.WaitForUploadStatus(MetalUploadHandle{normalUpload.id}),
              MetalUploadStatus::Completed);
    EXPECT_EQ(uploadQueue.WaitForUploadStatus(MetalUploadHandle{occlusionUpload.id}),
              MetalUploadStatus::Completed);
    resources.PromoteCompletedMaterialTextureUploads(uploadQueue, 3);
    materialStats = resources.GetMaterialStats();
    EXPECT_EQ(materialStats.shaderVisibleTextureCount, kRendererTextureSlotCount);
    EXPECT_EQ(materialStats.shaderVisibleSamplerCount, kRendererTextureSlotCount);
    EXPECT_EQ(materialStats.fallbackTextureCount, 2u);
    EXPECT_TRUE(materialStats.HasCompleteShaderBindings());
    const RendererMaterialBindingInfo promotedNormal =
        materialStats.GetActiveMaterialBinding(RendererTextureSlot::Normal);
    EXPECT_TRUE(promotedNormal.UsesMaterialTexture());
    EXPECT_EQ(promotedNormal.boundTexture.id, normalUpload.texture.id);
    EXPECT_EQ(promotedNormal.sourceAssetId, 78u);
    EXPECT_TRUE(promotedNormal.HasDimensions());
    EXPECT_TRUE(promotedNormal.IsShaderVisible());
    EXPECT_EQ(promotedNormal.textureBindingIndex,
              RendererMaterialTextureBindingIndex(RendererTextureSlot::Normal));
    EXPECT_EQ(promotedNormal.samplerBindingIndex,
              RendererMaterialSamplerBindingIndex(RendererTextureSlot::Normal));
    EXPECT_EQ(promotedNormal.uniformBufferIndex, kRendererMaterialUniformBufferIndex);
    EXPECT_TRUE(promotedNormal.HasBindingIndices());
    const RendererMaterialBindingInfo promotedOcclusion =
        materialStats.GetActiveMaterialBinding(RendererTextureSlot::Occlusion);
    EXPECT_TRUE(promotedOcclusion.UsesMaterialTexture());
    EXPECT_EQ(promotedOcclusion.boundTexture.id, occlusionUpload.texture.id);
    EXPECT_EQ(promotedOcclusion.sourceAssetId, 89u);
    EXPECT_EQ(promotedOcclusion.textureBindingIndex,
              RendererMaterialTextureBindingIndex(RendererTextureSlot::Occlusion));
    EXPECT_EQ(promotedOcclusion.samplerBindingIndex,
              RendererMaterialSamplerBindingIndex(RendererTextureSlot::Occlusion));
    EXPECT_EQ(promotedOcclusion.uniformBufferIndex, kRendererMaterialUniformBufferIndex);
    EXPECT_TRUE(promotedOcclusion.HasBindingIndices());

    EXPECT_FALSE(resources.UpdateMaterial(RendererMaterialHandle{}, updated));
    EXPECT_FALSE(resources.SetActiveMaterial(RendererMaterialHandle{9999}));
    RendererMaterialInfo missing;
    EXPECT_FALSE(resources.GetMaterialInfo(RendererMaterialHandle{9999}, missing));
    EXPECT_FALSE(missing.HasMaterial());
    EXPECT_FALSE(missing.HasSourceAsset());
    EXPECT_FALSE(missing.HasBoundTextures());
    EXPECT_FALSE(missing.HasActiveMaterial());
}

TEST_F(MetalSceneResourcesTest, MaterialStatsExposeFullTableAndShutdownState) {
    RendererMaterialStats stats = resources.GetMaterialStats();
    ASSERT_TRUE(stats.ready);
    ASSERT_EQ(stats.materialCount, 1u);
    ASSERT_EQ(stats.materialCapacity, 16u);

    RendererMaterialHandle lastMaterial;
    for (uint32_t i = stats.materialCount; i < stats.materialCapacity; ++i) {
        RendererMaterialDesc material;
        material.sourceAssetId = 100 + i;
        lastMaterial = resources.CreateMaterial(material);
        ASSERT_TRUE(lastMaterial);
    }

    stats = resources.GetMaterialStats();
    EXPECT_TRUE(stats.ready);
    EXPECT_EQ(stats.materialCount, stats.materialCapacity);
    EXPECT_FALSE(stats.HasFreeMaterialSlots());
    EXPECT_TRUE(stats.IsMaterialTableFull());
    EXPECT_FALSE(resources.CreateMaterial(RendererMaterialDesc{}));

    ASSERT_TRUE(resources.SetActiveMaterial(lastMaterial));
    stats = resources.GetMaterialStats();
    EXPECT_TRUE(stats.HasActiveMaterial());
    EXPECT_EQ(stats.activeMaterial.id, lastMaterial.id);
    EXPECT_EQ(stats.activeMaterialIndex, stats.materialCapacity - 1u);
    EXPECT_EQ(stats.activeMaterialBoundTextureCount, 1u);
    EXPECT_TRUE(stats.HasActiveMaterialSlot());
    EXPECT_TRUE(stats.HasActiveBoundTextures());
    EXPECT_TRUE(stats.HasValidActiveParameters());
    EXPECT_TRUE(stats.HasReadyShaderResourceGroupArgumentEncoder());
    EXPECT_TRUE(stats.HasCompleteShaderResourceGroupEncoding());
    EXPECT_FALSE(stats.HasCompleteShaderResourceGroupArgumentBufferBinding());
    EXPECT_TRUE(stats.HasMaterialActivity());

    resources.Shutdown(&device, 6);
    stats = resources.GetMaterialStats();
    EXPECT_FALSE(stats.ready);
    EXPECT_EQ(stats.materialCapacity, 16u);
    EXPECT_EQ(stats.materialCount, 0u);
    EXPECT_FALSE(stats.HasMaterials());
    EXPECT_FALSE(stats.HasActiveMaterial());
    EXPECT_TRUE(stats.HasFreeMaterialSlots());
    EXPECT_FALSE(stats.IsMaterialTableFull());
    EXPECT_EQ(stats.shaderVisibleTextureCount, 0u);
    EXPECT_EQ(stats.shaderVisibleSamplerCount, 0u);
    EXPECT_EQ(stats.fallbackTextureCount, 0u);
    EXPECT_FALSE(stats.HasCompleteShaderBindings());
    EXPECT_TRUE(stats.HasShaderResourceGroupLayout());
    EXPECT_TRUE(stats.HasCompleteShaderResourceGroupBindingTable());
    EXPECT_TRUE(stats.HasValidShaderResourceGroupLayout());
    EXPECT_FALSE(stats.HasCompleteShaderResourceGroupResources());
    EXPECT_FALSE(stats.HasShaderResourceGroupArgumentEncoder());
    EXPECT_EQ(stats.shaderResourceGroupArgumentCount, 0u);
    EXPECT_EQ(stats.shaderResourceGroupEncodedLength, 0u);
    EXPECT_EQ(stats.shaderResourceGroupEncodedStride, 0u);
    EXPECT_FALSE(stats.HasReadyShaderResourceGroupArgumentEncoder());
    EXPECT_FALSE(stats.HasShaderResourceGroupArgumentBuffer());
    EXPECT_EQ(stats.shaderResourceGroupArgumentBufferBytes, 0u);
    EXPECT_EQ(stats.shaderResourceGroupArgumentBufferDrawCapacity, 0u);
    EXPECT_EQ(stats.shaderResourceGroupEncodedDrawCount, 0u);
    EXPECT_EQ(stats.shaderResourceGroupArgumentBufferBindingIndex,
              kRendererMaterialShaderResourceGroupArgumentBufferIndex);
    EXPECT_FALSE(stats.HasBoundShaderResourceGroupArgumentBuffer());
    EXPECT_EQ(stats.shaderResourceGroupArgumentBufferBindCount, 0u);
    EXPECT_EQ(stats.shaderResourceGroupEncodedResourceCount, 0u);
    EXPECT_FALSE(stats.HasCompleteShaderResourceGroupEncoding());
    EXPECT_FALSE(stats.HasCompleteShaderResourceGroupArgumentBufferBinding());
    ASSERT_NE(stats.GetShaderResourceGroupBinding(0), nullptr);
    EXPECT_EQ(stats.GetShaderResourceGroupBinding(0)->boundResourceCount, 0u);
    ASSERT_NE(stats.GetShaderResourceGroupBinding(1), nullptr);
    EXPECT_EQ(stats.GetShaderResourceGroupBinding(1)->boundResourceCount, 0u);
    ASSERT_NE(stats.GetShaderResourceGroupBinding(2), nullptr);
    EXPECT_EQ(stats.GetShaderResourceGroupBinding(2)->boundResourceCount, 0u);
    EXPECT_FALSE(stats.HasMaterialActivity());
}

TEST_F(MetalSceneResourcesTest, MaterialTableRejectsInvalidParameters) {
    RendererMaterialDesc invalidCreate;
    invalidCreate.roughness = 1.5f;
    EXPECT_FALSE(invalidCreate.HasValidParameters());
    EXPECT_FALSE(resources.CreateMaterial(invalidCreate));

    RendererMaterialDesc material;
    const RendererMaterialHandle handle = resources.CreateMaterial(material);
    ASSERT_TRUE(handle);

    RendererMaterialInfo info;
    ASSERT_TRUE(resources.GetMaterialInfo(handle, info));
    EXPECT_TRUE(info.HasValidParameters());

    RendererMaterialDesc invalidUpdate = info.desc;
    invalidUpdate.baseColorFactor[1] = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(invalidUpdate.HasValidParameters());
    EXPECT_FALSE(resources.UpdateMaterial(handle, invalidUpdate));

    ASSERT_TRUE(resources.GetMaterialInfo(handle, info));
    EXPECT_TRUE(info.HasValidParameters());
    EXPECT_TRUE(info.HasBaseColorTexture());
}

TEST_F(MetalSceneResourcesTest, MaterialTableRejectsUnknownOrWrongSlotTextureHandles) {
    const std::array<uint8_t, 16> normalPixels = MakePixels(0xA0);
    const RendererTextureUploadHandle normalUpload =
        resources.UploadMaterialTexture(device,
                                        uploadQueue,
                                        MakeTextureUpload(RendererTextureSlot::Normal, 101, normalPixels),
                                        1);
    ASSERT_TRUE(normalUpload);

    RendererMaterialDesc wrongSlotCreate;
    wrongSlotCreate.SetTexture(RendererTextureSlot::BaseColor, normalUpload.texture);
    EXPECT_FALSE(resources.CreateMaterial(wrongSlotCreate));

    RendererMaterialDesc material;
    const RendererMaterialHandle handle = resources.CreateMaterial(material);
    ASSERT_TRUE(handle);

    RendererMaterialInfo info;
    ASSERT_TRUE(resources.GetMaterialInfo(handle, info));
    RendererMaterialDesc unknownUpdate = info.desc;
    unknownUpdate.SetTexture(RendererTextureSlot::Normal, RendererTextureHandle{999999});
    EXPECT_FALSE(resources.UpdateMaterial(handle, unknownUpdate));

    ASSERT_TRUE(resources.GetMaterialInfo(handle, info));
    EXPECT_TRUE(info.HasBaseColorTexture());
    EXPECT_FALSE(info.HasNormalTexture());
    EXPECT_EQ(info.BoundTextureCount(), 1u);

    RendererMaterialDesc wrongSlotUpdate = info.desc;
    wrongSlotUpdate.SetTexture(RendererTextureSlot::Emissive, normalUpload.texture);
    EXPECT_FALSE(resources.UpdateMaterial(handle, wrongSlotUpdate));

    ASSERT_TRUE(resources.GetMaterialInfo(handle, info));
    EXPECT_FALSE(info.HasEmissiveTexture());
    EXPECT_EQ(info.BoundTextureCount(), 1u);

    EXPECT_EQ(uploadQueue.WaitForUploadStatus(MetalUploadHandle{normalUpload.id}),
              MetalUploadStatus::Completed);
    resources.PromoteCompletedMaterialTextureUploads(uploadQueue, 2);
}

} // namespace testing
} // namespace MetalBackend
} // namespace Next
