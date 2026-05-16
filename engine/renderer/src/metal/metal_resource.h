#pragma once

#include "metal_device.h"
#include "metal_frame_graph.h"
#include "metal_pipeline.h"
#include "metal_resource_pool.h"
#include "metal_shader_library.h"
#include "metal_upload_queue.h"
#include "next/rhi/command.h"
#include "next/rhi/frame_graph.h"
#include "next/rhi/resource.h"
#include "next/renderer/renderer.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#import <Metal/Metal.h>

namespace Next {
namespace MetalBackend {

struct MetalVertex {
    float position[3];
    float normal[3];
    float texcoord[2];
    float albedo[3];
};

struct MetalUniforms {
    float mvp[16];
    float model[16];
    float lightDirection[4];
    float cameraPosition[4];
    float material[4];
    float ambientColor[4];
    float debugTint[4];
};

constexpr size_t kMetalUniformAlignment = 256;
constexpr size_t kMetalUniformStride =
    ((sizeof(MetalUniforms) + kMetalUniformAlignment - 1) / kMetalUniformAlignment) * kMetalUniformAlignment;

class MetalSamplerCache final {
public:
    id<MTLSamplerState> GetOrCreate(MetalDevice& device, const RHI::SamplerDesc& desc);
    void Shutdown(MetalDevice* device = nullptr, uint64_t submittedFrameIndex = 0);

    size_t GetCachedSamplerCount() const { return entries_.size(); }

private:
    struct Entry {
        RHI::SamplerDesc desc;
        id<MTLSamplerState> sampler = nil;
    };

    std::vector<Entry> entries_;
};

class MetalSceneResources final {
public:
    bool Initialize(MetalDevice& device,
                    MetalBufferPool& bufferPool,
                    MetalTexturePool& texturePool,
                    MetalUploadQueue& uploadQueue,
                    RHI::Format colorFormat,
                    RHI::Format depthFormat);
    void Shutdown(MetalDevice* device = nullptr, uint64_t submittedFrameIndex = 0);

    bool IsReady() const;
    bool UploadUniforms(MetalDevice& device,
                        MetalUploadQueue& uploadQueue,
                        const void* data,
                        size_t dataBytes);
    RendererTextureUploadHandle UploadMaterialTexture(MetalDevice& device,
                                                      MetalUploadQueue& uploadQueue,
                                                      const RendererTextureUploadDesc& texture,
                                                      uint64_t submittedFrameIndex);
    void PromoteCompletedMaterialTextureUploads(MetalUploadQueue& uploadQueue, uint64_t submittedFrameIndex);
    RendererTextureUploadStats GetTextureUploadStats() const;
    RendererTextureUploadStatus GetTextureUploadStatus(RendererTextureUploadHandle handle,
                                                       const MetalUploadQueue& uploadQueue) const;
    MetalPipelineCacheStats GetPipelineCacheStats() const;
    RendererSamplerStats GetSamplerStats() const;
    RendererMaterialStats GetMaterialStats() const;
    RendererResourceStateStats GetResourceStateStats() const;
    RendererGeometryStats GetGeometryStats() const;
    bool AppendDrawFrameGraphResources(RHI::FrameGraphDesc& graph,
                                       RHI::FrameGraphPassDesc& renderPass,
                                       MetalFrameGraphResourceUsageTable* resourceUsages,
                                       RHI::FrameGraphDescriptorValidation& outValidation) const;
    bool AppendDrawFrameGraphResources(RHI::FrameGraphDesc& graph,
                                       RHI::FrameGraphPassDesc& renderPass,
                                       RHI::FrameGraphDescriptorValidation& outValidation) const {
        return AppendDrawFrameGraphResources(graph, renderPass, nullptr, outValidation);
    }
    const RHI::GraphicsPipelineDesc* GetRenderPipelineDesc() const;
    const char* GetRenderPipelineDebugName() const;
    MetalShaderLibraryInput GetShaderLibraryInput() const { return shaderLibraryInput_; }
    const MetalShaderLibraryDesc& GetShaderLibraryDesc() const { return shaderLibraryDesc_; }
    bool GetTextureInfo(RendererTextureHandle texture, RendererTextureInfo& outInfo) const;
    RendererMaterialHandle CreateMaterial(const RendererMaterialDesc& material);
    bool UpdateMaterial(RendererMaterialHandle handle, const RendererMaterialDesc& material);
    bool SetActiveMaterial(RendererMaterialHandle handle);
    bool GetMaterialInfo(RendererMaterialHandle handle, RendererMaterialInfo& outInfo) const;
    RendererMaterialDesc GetActiveMaterialDesc() const;

    bool EncodeMaterialArgumentBuffers(size_t drawCount);
    bool BindDrawState(id<MTLRenderCommandEncoder> encoder,
                       NSUInteger uniformOffset,
                       bool declareArgumentResources = true) const;
    bool DrawCube(id<MTLRenderCommandEncoder> encoder) const;

private:
    bool FindMaterialTextureSlot(RendererTextureHandle texture, RendererTextureSlot& outSlot) const;
    bool ValidateMaterialTextureBindings(const RendererMaterialDesc& material, const char* operationName) const;
    bool ResolveActiveMaterialTextures(std::array<const MetalTexture*, kRendererTextureSlotCount>& outTextures) const;
    bool EncodeMaterialArgumentBufferRegion(size_t drawIndex, size_t uniformOffset);
    RendererMaterialBindingInfo ResolveMaterialBinding(RendererTextureSlot slot,
                                                       RendererTextureHandle texture) const;
    const MetalTexture* ResolveMaterialTexture(RendererTextureSlot slot, RendererTextureHandle texture) const;

    MetalBufferPool* bufferPool_ = nullptr;
    MetalTexturePool* texturePool_ = nullptr;
    RHI::FrameGraphDescriptorValidation resourceFrameGraphValidation_;
    uint32_t resourceFrameGraphTransitionCount_ = 0;
    uint32_t resourceFrameGraphPassCount_ = 0;
    uint32_t resourceFrameGraphReadyPassIndex_ = RHI::kInvalidFrameGraphPassIndex;
    uint32_t resourceFrameGraphReadyPassTransitionCount_ = 0;
    id<MTLLibrary> shaderLibrary_ = nil;
    MetalShaderLibraryDesc shaderLibraryDesc_;
    MetalShaderLibraryInput shaderLibraryInput_;
    MetalPipelineCache pipelineCache_;
    const MetalRenderPipeline* renderPipeline_ = nullptr;
    MetalBuffer vertexBuffer_;
    MetalBuffer indexBuffer_;
    RHI::IndexBufferViewDesc cubeIndexBufferView_;
    RHI::DrawIndexedDesc cubeDraw_;
    MetalBuffer uniformBuffer_;
    id<MTLArgumentEncoder> materialArgumentEncoder_ = nil;
    MetalBuffer materialArgumentBuffer_;
    uint32_t materialArgumentCount_ = 0;
    uint64_t materialArgumentEncodedLength_ = 0;
    uint64_t materialArgumentEncodedStride_ = 0;
    uint32_t materialArgumentBufferDrawCapacity_ = 0;
    uint32_t materialArgumentEncodedDrawCount_ = 0;
    uint32_t materialArgumentEncodedResourceCount_ = 0;
    mutable uint64_t materialArgumentBufferBindCount_ = 0;
    MetalTexture neutralNormalTexture_;
    MetalTexture neutralMetallicRoughnessTexture_;
    MetalTexture neutralEmissiveTexture_;
    MetalTexture neutralOcclusionTexture_;
    static constexpr size_t kMaterialTextureVersionsPerSlot = 2;
    static constexpr size_t kNoPendingMaterialTexture = static_cast<size_t>(-1);
    struct MaterialTextureRecord {
        RendererTextureHandle texture;
        RendererTextureSlot slot = RendererTextureSlot::BaseColor;
        RendererTextureFormat format = RendererTextureFormat::Unknown;
        uint64_t sourceAssetId = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        bool valid = false;
    };
    struct MaterialTextureSlotState {
        std::array<MetalTexture, kMaterialTextureVersionsPerSlot> textures;
        std::array<MaterialTextureRecord, kMaterialTextureVersionsPerSlot> records;
        size_t active = 0;
        size_t pending = kNoPendingMaterialTexture;
        MetalUploadHandle pendingUpload;
    };
    std::array<MaterialTextureSlotState, kRendererTextureSlotCount> materialTextureSlots_;
    RendererTextureUploadStats textureUploadStats_;
    static constexpr size_t kRendererMaterialSlots = 16;
    struct RendererMaterialRecord {
        RendererMaterialHandle material;
        RendererMaterialDesc desc;
        bool valid = false;
    };
    std::array<RendererMaterialRecord, kRendererMaterialSlots> rendererMaterialRecords_;
    size_t activeRendererMaterial_ = 0;
    uint64_t nextRendererMaterialId_ = 1;
    MetalSamplerCache samplerCache_;
    std::array<id<MTLSamplerState>, kRendererTextureSlotCount> materialSamplers_ = {};
    uint64_t nextRendererTextureId_ = 1;
};

} // namespace MetalBackend
} // namespace Next
