#include "metal_resource.h"

#include "metal_conversions.h"
#include "metal_shader_library.h"
#include "next/foundation/logger.h"
#include "next/jobsystem/job_system.h"
#include "next/rhi/frame_graph.h"
#include "next/renderer/renderer.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <vector>

namespace Next {
namespace MetalBackend {
namespace {

constexpr MetalVertex kCubeVertices[] = {
    {{-1.0f, -1.0f, -1.0f}, { 0.0f,  0.0f, -1.0f}, {0.0f, 1.0f}, {0.90f, 0.18f, 0.16f}},
    {{ 1.0f, -1.0f, -1.0f}, { 0.0f,  0.0f, -1.0f}, {1.0f, 1.0f}, {0.90f, 0.18f, 0.16f}},
    {{ 1.0f,  1.0f, -1.0f}, { 0.0f,  0.0f, -1.0f}, {1.0f, 0.0f}, {0.90f, 0.18f, 0.16f}},
    {{-1.0f,  1.0f, -1.0f}, { 0.0f,  0.0f, -1.0f}, {0.0f, 0.0f}, {0.90f, 0.18f, 0.16f}},

    {{-1.0f, -1.0f,  1.0f}, { 0.0f,  0.0f,  1.0f}, {0.0f, 1.0f}, {0.18f, 0.78f, 0.80f}},
    {{ 1.0f, -1.0f,  1.0f}, { 0.0f,  0.0f,  1.0f}, {1.0f, 1.0f}, {0.18f, 0.78f, 0.80f}},
    {{ 1.0f,  1.0f,  1.0f}, { 0.0f,  0.0f,  1.0f}, {1.0f, 0.0f}, {0.18f, 0.78f, 0.80f}},
    {{-1.0f,  1.0f,  1.0f}, { 0.0f,  0.0f,  1.0f}, {0.0f, 0.0f}, {0.18f, 0.78f, 0.80f}},

    {{-1.0f, -1.0f, -1.0f}, { 0.0f, -1.0f,  0.0f}, {0.0f, 1.0f}, {0.96f, 0.62f, 0.16f}},
    {{ 1.0f, -1.0f, -1.0f}, { 0.0f, -1.0f,  0.0f}, {1.0f, 1.0f}, {0.96f, 0.62f, 0.16f}},
    {{ 1.0f, -1.0f,  1.0f}, { 0.0f, -1.0f,  0.0f}, {1.0f, 0.0f}, {0.96f, 0.62f, 0.16f}},
    {{-1.0f, -1.0f,  1.0f}, { 0.0f, -1.0f,  0.0f}, {0.0f, 0.0f}, {0.96f, 0.62f, 0.16f}},

    {{-1.0f,  1.0f, -1.0f}, { 0.0f,  1.0f,  0.0f}, {0.0f, 1.0f}, {0.22f, 0.72f, 0.30f}},
    {{ 1.0f,  1.0f, -1.0f}, { 0.0f,  1.0f,  0.0f}, {1.0f, 1.0f}, {0.22f, 0.72f, 0.30f}},
    {{ 1.0f,  1.0f,  1.0f}, { 0.0f,  1.0f,  0.0f}, {1.0f, 0.0f}, {0.22f, 0.72f, 0.30f}},
    {{-1.0f,  1.0f,  1.0f}, { 0.0f,  1.0f,  0.0f}, {0.0f, 0.0f}, {0.22f, 0.72f, 0.30f}},

    {{-1.0f, -1.0f, -1.0f}, {-1.0f,  0.0f,  0.0f}, {0.0f, 1.0f}, {0.70f, 0.38f, 0.95f}},
    {{-1.0f,  1.0f, -1.0f}, {-1.0f,  0.0f,  0.0f}, {1.0f, 1.0f}, {0.70f, 0.38f, 0.95f}},
    {{-1.0f,  1.0f,  1.0f}, {-1.0f,  0.0f,  0.0f}, {1.0f, 0.0f}, {0.70f, 0.38f, 0.95f}},
    {{-1.0f, -1.0f,  1.0f}, {-1.0f,  0.0f,  0.0f}, {0.0f, 0.0f}, {0.70f, 0.38f, 0.95f}},

    {{ 1.0f, -1.0f, -1.0f}, { 1.0f,  0.0f,  0.0f}, {0.0f, 1.0f}, {0.92f, 0.92f, 0.32f}},
    {{ 1.0f,  1.0f, -1.0f}, { 1.0f,  0.0f,  0.0f}, {1.0f, 1.0f}, {0.92f, 0.92f, 0.32f}},
    {{ 1.0f,  1.0f,  1.0f}, { 1.0f,  0.0f,  0.0f}, {1.0f, 0.0f}, {0.92f, 0.92f, 0.32f}},
    {{ 1.0f, -1.0f,  1.0f}, { 1.0f,  0.0f,  0.0f}, {0.0f, 0.0f}, {0.92f, 0.92f, 0.32f}},
};

constexpr size_t kMaterialSrgArgumentBufferDrawCapacity = kMaxRendererDebugCells + 1;

size_t AlignUp(size_t value, size_t alignment) {
    if (alignment <= 1) {
        return value;
    }
    const size_t remainder = value % alignment;
    return remainder == 0 ? value : value + (alignment - remainder);
}

constexpr uint16_t kCubeIndices[] = {
    0, 2, 1, 0, 3, 2,
    4, 5, 6, 4, 6, 7,
    8, 9, 10, 8, 10, 11,
    12, 15, 14, 12, 14, 13,
    16, 18, 17, 16, 19, 18,
    20, 21, 22, 20, 22, 23,
};

RHI::VertexInputStateDesc MakeDemoVertexInputState() {
    RHI::VertexInputStateDesc vertexInput;
    vertexInput.bufferCount = 1;
    vertexInput.buffers[0].stride = sizeof(MetalVertex);
    vertexInput.buffers[0].stepFunction = RHI::VertexStepFunction::PerVertex;
    vertexInput.buffers[0].stepRate = 1;

    vertexInput.attributeCount = 4;
    vertexInput.attributes[0].location = 0;
    vertexInput.attributes[0].bufferIndex = kRendererGeometryVertexBufferIndex;
    vertexInput.attributes[0].format = RHI::VertexFormat::Float32x3;
    vertexInput.attributes[0].offset = offsetof(MetalVertex, position);
    vertexInput.attributes[1].location = 1;
    vertexInput.attributes[1].bufferIndex = kRendererGeometryVertexBufferIndex;
    vertexInput.attributes[1].format = RHI::VertexFormat::Float32x3;
    vertexInput.attributes[1].offset = offsetof(MetalVertex, normal);
    vertexInput.attributes[2].location = 2;
    vertexInput.attributes[2].bufferIndex = kRendererGeometryVertexBufferIndex;
    vertexInput.attributes[2].format = RHI::VertexFormat::Float32x2;
    vertexInput.attributes[2].offset = offsetof(MetalVertex, texcoord);
    vertexInput.attributes[3].location = 3;
    vertexInput.attributes[3].bufferIndex = kRendererGeometryVertexBufferIndex;
    vertexInput.attributes[3].format = RHI::VertexFormat::Float32x3;
    vertexInput.attributes[3].offset = offsetof(MetalVertex, albedo);
    return vertexInput;
}

bool CreateProceduralBaseColorTexture(MetalTexturePool& texturePool,
                                       MetalUploadQueue& uploadQueue,
                                       MetalTexture& texture) {
    constexpr int kTextureSize = 128;
    constexpr int kBytesPerPixel = 4;
    std::vector<uint8_t> pixels(kTextureSize * kTextureSize * kBytesPerPixel);

    for (int y = 0; y < kTextureSize; ++y) {
        for (int x = 0; x < kTextureSize; ++x) {
            const bool checker = (((x / 16) ^ (y / 16)) & 1) != 0;
            const bool grid = (x % 16 == 0) || (y % 16 == 0);
            const uint8_t grain = static_cast<uint8_t>((x * 17 + y * 29) & 0x0f);

            uint8_t r = checker ? 212 : 84;
            uint8_t g = checker ? 224 : 100;
            uint8_t b = checker ? 210 : 132;

            if (grid) {
                r = static_cast<uint8_t>(std::min<int>(255, r + 34));
                g = static_cast<uint8_t>(std::min<int>(255, g + 34));
                b = static_cast<uint8_t>(std::min<int>(255, b + 34));
            }

            const int offset = (y * kTextureSize + x) * kBytesPerPixel;
            pixels[offset + 0] = static_cast<uint8_t>(std::min<int>(255, r + grain));
            pixels[offset + 1] = static_cast<uint8_t>(std::min<int>(255, g + grain));
            pixels[offset + 2] = static_cast<uint8_t>(std::min<int>(255, b + grain));
            pixels[offset + 3] = 255;
        }
    }

    RHI::TextureDesc textureDesc;
    textureDesc.extent.width = kTextureSize;
    textureDesc.extent.height = kTextureSize;
    textureDesc.format = RHI::Format::RGBA8Unorm;
    textureDesc.memory = RHI::ResourceMemory::DeviceLocal;
    textureDesc.usage = RHI::ResourceUsage::ShaderRead | RHI::ResourceUsage::CopyDestination;
    textureDesc.initialState = RHI::ResourceState::CopyDestination;
    textureDesc.debugName = "NEXT procedural base color";

    if (!texturePool.CreateTexture(texture, textureDesc)) {
        return false;
    }

    return uploadQueue.EnqueueTexture2DUpload(texture,
                                              pixels.data(),
                                              pixels.size(),
                                              kTextureSize,
                                              kTextureSize,
                                              kBytesPerPixel);
}

bool CreateNeutralNormalTexture(MetalTexturePool& texturePool,
                                MetalUploadQueue& uploadQueue,
                                MetalTexture& texture) {
    constexpr uint32_t kTextureSize = 1;
    constexpr uint32_t kBytesPerPixel = 4;
    const std::array<uint8_t, kTextureSize * kTextureSize * kBytesPerPixel> pixels = {128, 128, 255, 255};

    RHI::TextureDesc textureDesc;
    textureDesc.extent.width = kTextureSize;
    textureDesc.extent.height = kTextureSize;
    textureDesc.format = RHI::Format::RGBA8Unorm;
    textureDesc.memory = RHI::ResourceMemory::DeviceLocal;
    textureDesc.usage = RHI::ResourceUsage::ShaderRead | RHI::ResourceUsage::CopyDestination;
    textureDesc.initialState = RHI::ResourceState::CopyDestination;
    textureDesc.debugName = "NEXT neutral normal";

    if (!texturePool.CreateTexture(texture, textureDesc)) {
        return false;
    }

    return uploadQueue.EnqueueTexture2DUpload(texture,
                                              pixels.data(),
                                              pixels.size(),
                                              kTextureSize,
                                              kTextureSize,
                                              kBytesPerPixel);
}

bool CreateNeutralMetallicRoughnessTexture(MetalTexturePool& texturePool,
                                           MetalUploadQueue& uploadQueue,
                                           MetalTexture& texture) {
    constexpr uint32_t kTextureSize = 1;
    constexpr uint32_t kBytesPerPixel = 4;
    const std::array<uint8_t, kTextureSize * kTextureSize * kBytesPerPixel> pixels = {0, 0, 0, 0};

    RHI::TextureDesc textureDesc;
    textureDesc.extent.width = kTextureSize;
    textureDesc.extent.height = kTextureSize;
    textureDesc.format = RHI::Format::RGBA8Unorm;
    textureDesc.memory = RHI::ResourceMemory::DeviceLocal;
    textureDesc.usage = RHI::ResourceUsage::ShaderRead | RHI::ResourceUsage::CopyDestination;
    textureDesc.initialState = RHI::ResourceState::CopyDestination;
    textureDesc.debugName = "NEXT neutral metallic roughness";

    if (!texturePool.CreateTexture(texture, textureDesc)) {
        return false;
    }

    return uploadQueue.EnqueueTexture2DUpload(texture,
                                              pixels.data(),
                                              pixels.size(),
                                              kTextureSize,
                                              kTextureSize,
                                              kBytesPerPixel);
}

bool CreateNeutralEmissiveTexture(MetalTexturePool& texturePool,
                                  MetalUploadQueue& uploadQueue,
                                  MetalTexture& texture) {
    constexpr uint32_t kTextureSize = 1;
    constexpr uint32_t kBytesPerPixel = 4;
    const std::array<uint8_t, kTextureSize * kTextureSize * kBytesPerPixel> pixels = {0, 0, 0, 255};

    RHI::TextureDesc textureDesc;
    textureDesc.extent.width = kTextureSize;
    textureDesc.extent.height = kTextureSize;
    textureDesc.format = RHI::Format::RGBA8Unorm;
    textureDesc.memory = RHI::ResourceMemory::DeviceLocal;
    textureDesc.usage = RHI::ResourceUsage::ShaderRead | RHI::ResourceUsage::CopyDestination;
    textureDesc.initialState = RHI::ResourceState::CopyDestination;
    textureDesc.debugName = "NEXT neutral emissive";

    if (!texturePool.CreateTexture(texture, textureDesc)) {
        return false;
    }

    return uploadQueue.EnqueueTexture2DUpload(texture,
                                              pixels.data(),
                                              pixels.size(),
                                              kTextureSize,
                                              kTextureSize,
                                              kBytesPerPixel);
}

bool CreateNeutralOcclusionTexture(MetalTexturePool& texturePool,
                                   MetalUploadQueue& uploadQueue,
                                   MetalTexture& texture) {
    constexpr uint32_t kTextureSize = 1;
    constexpr uint32_t kBytesPerPixel = 4;
    const std::array<uint8_t, kTextureSize * kTextureSize * kBytesPerPixel> pixels = {255, 255, 255, 255};

    RHI::TextureDesc textureDesc;
    textureDesc.extent.width = kTextureSize;
    textureDesc.extent.height = kTextureSize;
    textureDesc.format = RHI::Format::RGBA8Unorm;
    textureDesc.memory = RHI::ResourceMemory::DeviceLocal;
    textureDesc.usage = RHI::ResourceUsage::ShaderRead | RHI::ResourceUsage::CopyDestination;
    textureDesc.initialState = RHI::ResourceState::CopyDestination;
    textureDesc.debugName = "NEXT neutral occlusion";

    if (!texturePool.CreateTexture(texture, textureDesc)) {
        return false;
    }

    return uploadQueue.EnqueueTexture2DUpload(texture,
                                              pixels.data(),
                                              pixels.size(),
                                              kTextureSize,
                                              kTextureSize,
                                              kBytesPerPixel);
}

void QueueForRelease(MetalDevice* device, id object, uint64_t submittedFrameIndex) {
    if (device && object) {
        device->QueueForRelease(object, submittedFrameIndex);
    }
}

RHI::Format ToRHIFormat(RendererTextureFormat format) {
    switch (format) {
        case RendererTextureFormat::RGBA8Unorm:
            return RHI::Format::RGBA8Unorm;
        case RendererTextureFormat::Unknown:
        default:
            return RHI::Format::Unknown;
    }
}

const char* RendererTextureDebugName(RendererTextureSlot slot) {
    switch (slot) {
        case RendererTextureSlot::BaseColor:
            return "NEXT streamed base color";
        case RendererTextureSlot::Normal:
            return "NEXT streamed normal";
        case RendererTextureSlot::MetallicRoughness:
            return "NEXT streamed metallic roughness";
        case RendererTextureSlot::Emissive:
            return "NEXT streamed emissive";
        case RendererTextureSlot::Occlusion:
            return "NEXT streamed occlusion";
        default:
            return "NEXT streamed material texture";
    }
}

const char* RendererSamplerDebugName(RendererTextureSlot slot) {
    switch (slot) {
        case RendererTextureSlot::BaseColor:
            return "NEXT base color sampler";
        case RendererTextureSlot::Normal:
            return "NEXT normal sampler";
        case RendererTextureSlot::MetallicRoughness:
            return "NEXT metallic roughness sampler";
        case RendererTextureSlot::Emissive:
            return "NEXT emissive sampler";
        case RendererTextureSlot::Occlusion:
            return "NEXT occlusion sampler";
        default:
            return "NEXT material sampler";
    }
}

struct MetalFrameGraphResourceRequest {
    RHI::Resource* resource = nullptr;
    RHI::ResourceState state = RHI::ResourceState::Undefined;
    RHI::FrameGraphAccessType access = RHI::FrameGraphAccessType::Read;
};

bool TransitionMetalResourcesFromFrameGraph(const MetalFrameGraphResourceRequest* requests,
                                            size_t requestCount,
                                            const char* passName,
                                            RHI::QueueClass queueClass,
                                            RHI::FrameGraphDescriptorValidation& outValidation,
                                            uint32_t& outTransitionCount,
                                            uint32_t& outPassCount,
                                            uint32_t& outReadyPassIndex,
                                            uint32_t& outReadyPassTransitionCount) {
    outValidation = {};
    outTransitionCount = 0;
    outPassCount = 0;
    outReadyPassIndex = RHI::kInvalidFrameGraphPassIndex;
    outReadyPassTransitionCount = 0;
    if (!requests || requestCount == 0) {
        outValidation.error = RHI::FrameGraphDescriptorError::MissingResource;
        NEXT_LOG_ERROR("Metal frame graph resource plan '%s' is empty", passName ? passName : "<unnamed>");
        return false;
    }

    RHI::FrameGraphDesc graph;
    RHI::FrameGraphPassDesc pass;
    pass.debugName = passName;
    pass.queueClass = queueClass;
    std::array<RHI::Resource*, RHI::kMaxFrameGraphResources> resourceMap{};

    for (size_t i = 0; i < requestCount; ++i) {
        RHI::Resource* resource = requests[i].resource;
        if (!resource || i >= resourceMap.size()) {
            outValidation.error = RHI::FrameGraphDescriptorError::InvalidResourceIndex;
            outValidation.resourceIndex = static_cast<uint32_t>(i);
            NEXT_LOG_ERROR("Metal frame graph resource plan '%s' has invalid resource index %zu",
                           passName ? passName : "<unnamed>",
                           i);
            return false;
        }

        RHI::FrameGraphResourceDesc resourceDesc;
        resourceDesc.debugName = resource->GetDebugName();
        resourceDesc.type = resource->GetResourceType();
        resourceDesc.usage = resource->GetUsageFlags();
        resourceDesc.initialState = resource->GetCurrentState();
        resourceDesc.imported = true;

        RHI::FrameGraphResourceHandle handle;
        if (!RHI::AddFrameGraphResource(graph, resourceDesc, &handle)) {
            outValidation.error = RHI::FrameGraphDescriptorError::TooManyResources;
            outValidation.resourceIndex = static_cast<uint32_t>(i);
            return false;
        }
        resourceMap[handle.index] = resource;

        if (!RHI::AddFrameGraphPassAccess(
                pass,
                RHI::MakeFrameGraphPassResourceAccess(handle, requests[i].state, requests[i].access))) {
            outValidation.error = RHI::FrameGraphDescriptorError::TooManyPassAccesses;
            outValidation.resourceIndex = handle.index;
            outValidation.accessIndex = static_cast<uint32_t>(i);
            return false;
        }
    }

    if (!RHI::AddFrameGraphPass(graph, pass)) {
        outValidation.error = RHI::FrameGraphDescriptorError::TooManyPasses;
        return false;
    }

    const RHI::FrameGraphCompileResult compileResult = RHI::CompileFrameGraphTransitions(graph);
    outValidation = compileResult.validation;
    outTransitionCount = compileResult.transitionCount;
    outPassCount = compileResult.passCount;
    if (!compileResult) {
        NEXT_LOG_ERROR("Metal frame graph resource plan '%s' failed: %s (pass=%u access=%u resource=%u state=%s queue=%s)",
                       passName ? passName : "<unnamed>",
                       RHI::FrameGraphDescriptorErrorName(compileResult.validation.error),
                       compileResult.validation.passIndex,
                       compileResult.validation.accessIndex,
                       compileResult.validation.resourceIndex,
                       RHI::ResourceStateName(compileResult.validation.state),
                       RHI::QueueClassName(compileResult.validation.queueClass));
        return false;
    }

    RHI::FrameGraphExecutionValidation executionValidation;
    constexpr uint32_t kResourceReadyPassIndex = 0;
    outReadyPassIndex = kResourceReadyPassIndex;
    if (!RHI::ApplyFrameGraphPassResourceTransitionPlan(compileResult,
                                                       kResourceReadyPassIndex,
                                                       resourceMap,
                                                       &executionValidation,
                                                       &outReadyPassTransitionCount)) {
        NEXT_LOG_ERROR("Metal frame graph resource plan '%s' execution failed: %s "
                       "(transition=%u pass=%u access=%u resource=%u %s->%s queue=%s "
                       "stages=0x%02x resourceError=%s current=%s)",
                       passName ? passName : "<unnamed>",
                       RHI::FrameGraphExecutionErrorName(executionValidation.error),
                       executionValidation.transitionIndex,
                       executionValidation.passIndex,
                       executionValidation.accessIndex,
                       executionValidation.resourceIndex,
                       RHI::ResourceStateName(executionValidation.before),
                       RHI::ResourceStateName(executionValidation.after),
                       RHI::QueueClassName(executionValidation.queueClass),
                       static_cast<unsigned int>(executionValidation.shaderStages),
                       RHI::ResourceTransitionErrorName(
                           executionValidation.resourceTransitionValidation.error),
                       RHI::ResourceStateName(
                           executionValidation.resourceTransitionValidation.current));
        if (executionValidation.error == RHI::FrameGraphExecutionError::MissingResourceBinding) {
            outValidation.error = RHI::FrameGraphDescriptorError::InvalidResourceIndex;
            outValidation.resourceIndex = executionValidation.resourceIndex;
        } else if (executionValidation.error == RHI::FrameGraphExecutionError::TransitionCapacityExceeded) {
            outValidation.error = RHI::FrameGraphDescriptorError::TransitionCapacityExceeded;
            outValidation.resourceIndex = executionValidation.resourceIndex;
        }
        return false;
    }
    outTransitionCount = compileResult.transitionCount;

    return true;
}

bool TransitionMetalResourceFromFrameGraph(RHI::Resource& resource,
                                           RHI::ResourceState state,
                                           RHI::FrameGraphAccessType access,
                                           RHI::QueueClass queueClass,
                                           const char* passName) {
    const MetalFrameGraphResourceRequest request[] = {
        {&resource, state, access},
    };
    RHI::FrameGraphDescriptorValidation validation;
    uint32_t transitionCount = 0;
    uint32_t passCount = 0;
    uint32_t passIndex = RHI::kInvalidFrameGraphPassIndex;
    uint32_t passTransitionCount = 0;
    return TransitionMetalResourcesFromFrameGraph(request,
                                                 sizeof(request) / sizeof(request[0]),
                                                 passName,
                                                 queueClass,
                                                 validation,
                                                 transitionCount,
                                                 passCount,
                                                 passIndex,
                                                 passTransitionCount);
}

bool AppendMetalFrameGraphResource(RHI::FrameGraphDesc& graph,
                                   RHI::FrameGraphPassDesc& pass,
                                   const RHI::Resource& resource,
                                   id<MTLResource> nativeResource,
                                   RHI::ResourceState state,
                                   RHI::FrameGraphAccessType access,
                                   RHI::ShaderStageFlags shaderStages,
                                   MetalFrameGraphResourceUsageTable* resourceUsages,
                                   RHI::FrameGraphDescriptorValidation& outValidation) {
    RHI::FrameGraphResourceDesc resourceDesc;
    resourceDesc.debugName = resource.GetDebugName();
    resourceDesc.type = resource.GetResourceType();
    resourceDesc.usage = resource.GetUsageFlags();
    resourceDesc.initialState = resource.GetCurrentState();
    resourceDesc.imported = true;

    RHI::FrameGraphResourceHandle handle;
    if (!RHI::AddFrameGraphResource(graph, resourceDesc, &handle)) {
        outValidation.error = RHI::FrameGraphDescriptorError::TooManyResources;
        outValidation.resourceIndex = graph.resourceCount;
        return false;
    }
    if (resourceUsages &&
        !resourceUsages->SetResource(handle.index, nativeResource, resource.GetResourceType())) {
        outValidation.error = RHI::FrameGraphDescriptorError::MissingResource;
        outValidation.resourceIndex = handle.index;
        return false;
    }

    if (!RHI::AddFrameGraphPassAccess(
            pass,
            RHI::MakeFrameGraphPassResourceAccess(handle, state, access, shaderStages))) {
        outValidation.error = RHI::FrameGraphDescriptorError::TooManyPassAccesses;
        outValidation.resourceIndex = handle.index;
        outValidation.accessIndex = pass.accessCount;
        return false;
    }

    return true;
}

} // namespace

id<MTLSamplerState> MetalSamplerCache::GetOrCreate(MetalDevice& device, const RHI::SamplerDesc& desc) {
    for (const Entry& entry : entries_) {
        if (RHI::SamplerDescEquals(entry.desc, desc)) {
            return entry.sampler;
        }
    }

    if (!device.NativeDevice()) {
        return nil;
    }

    MTLSamplerDescriptor* samplerDesc = [[MTLSamplerDescriptor alloc] init];
    ConfigureMetalSamplerDescriptor(desc, samplerDesc);
    id<MTLSamplerState> sampler = [device.NativeDevice() newSamplerStateWithDescriptor:samplerDesc];
    if (sampler) {
        entries_.push_back(Entry{desc, sampler});
    }
    return sampler;
}

void MetalSamplerCache::Shutdown(MetalDevice* device, uint64_t submittedFrameIndex) {
    for (const Entry& entry : entries_) {
        QueueForRelease(device, entry.sampler, submittedFrameIndex);
    }
    entries_.clear();
}

bool MetalSceneResources::Initialize(MetalDevice& device,
                                     MetalBufferPool& bufferPool,
                                     MetalTexturePool& texturePool,
                                     MetalUploadQueue& uploadQueue,
                                     RHI::Format colorFormat,
                                     RHI::Format depthFormat) {
    if (!device.NativeDevice()) {
        return false;
    }

    @autoreleasepool {
        Shutdown();

        shaderLibraryDesc_ = DemoForwardShaderLibraryDesc();
        if (shaderLibraryDesc_.requiredArgumentBufferTier != RHI::ArgumentBufferTier::Unsupported &&
            !device.GetFeatures().SupportsArgumentBufferTier(shaderLibraryDesc_.requiredArgumentBufferTier)) {
            NEXT_LOG_ERROR("Metal shader manifest '%s' requires argument buffers %s or newer (device tier=%s)",
                           shaderLibraryDesc_.manifestPath.empty()
                               ? "<unknown>"
                               : shaderLibraryDesc_.manifestPath.c_str(),
                           RHI::ArgumentBufferTierName(shaderLibraryDesc_.requiredArgumentBufferTier),
                           RHI::ArgumentBufferTierName(device.GetFeatures().argumentBufferTier));
            return false;
        }
        if (shaderLibraryDesc_.materialShaderResourceGroupArgumentBufferIndex !=
                kMetalShaderManifestInvalidBindingIndex &&
            shaderLibraryDesc_.materialShaderResourceGroupArgumentBufferIndex !=
                kRendererMaterialShaderResourceGroupArgumentBufferIndex) {
            NEXT_LOG_ERROR("Metal shader manifest '%s' requires material SRG argument buffer index %u "
                           "(renderer index=%u)",
                           shaderLibraryDesc_.manifestPath.empty()
                               ? "<unknown>"
                               : shaderLibraryDesc_.manifestPath.c_str(),
                           shaderLibraryDesc_.materialShaderResourceGroupArgumentBufferIndex,
                           kRendererMaterialShaderResourceGroupArgumentBufferIndex);
            return false;
        }
        if (shaderLibraryDesc_.materialShaderResourceGroupUniformArgumentIndex !=
                kMetalDemoForwardMaterialShaderResourceGroupUniformArgumentIndex ||
            shaderLibraryDesc_.materialShaderResourceGroupTextureArgumentBaseIndex !=
                kMetalDemoForwardMaterialShaderResourceGroupTextureArgumentBaseIndex ||
            shaderLibraryDesc_.materialShaderResourceGroupSamplerArgumentBaseIndex !=
                kMetalDemoForwardMaterialShaderResourceGroupSamplerArgumentBaseIndex) {
            NEXT_LOG_ERROR("Metal shader manifest '%s' requires material SRG argument layout "
                           "uniform=%u textureBase=%u samplerBase=%u (renderer layout=%u/%u/%u)",
                           shaderLibraryDesc_.manifestPath.empty()
                               ? "<unknown>"
                               : shaderLibraryDesc_.manifestPath.c_str(),
                           shaderLibraryDesc_.materialShaderResourceGroupUniformArgumentIndex,
                           shaderLibraryDesc_.materialShaderResourceGroupTextureArgumentBaseIndex,
                           shaderLibraryDesc_.materialShaderResourceGroupSamplerArgumentBaseIndex,
                           kMetalDemoForwardMaterialShaderResourceGroupUniformArgumentIndex,
                           kMetalDemoForwardMaterialShaderResourceGroupTextureArgumentBaseIndex,
                           kMetalDemoForwardMaterialShaderResourceGroupSamplerArgumentBaseIndex);
            return false;
        }

        bufferPool_ = &bufferPool;
        texturePool_ = &texturePool;

        shaderLibrary_ = CreateShaderLibrary(device, shaderLibraryDesc_, &shaderLibraryInput_);
        if (!shaderLibrary_) {
            return false;
        }

        RHI::GraphicsPipelineDesc graphicsPipelineDesc = RHI::MakeGraphicsPipelineDesc(
            shaderLibraryDesc_.vertexEntryPoint.c_str(),
            shaderLibraryDesc_.fragmentEntryPoint.c_str(),
            colorFormat,
            depthFormat,
            "NEXT demo forward pipeline");
        graphicsPipelineDesc.vertexInput = MakeDemoVertexInputState();
        renderPipeline_ = pipelineCache_.FindOrCreate(device, shaderLibrary_, graphicsPipelineDesc);
        if (!renderPipeline_) {
            return false;
        }

        RHI::BufferDesc vertexDesc;
        vertexDesc.sizeBytes = sizeof(kCubeVertices);
        vertexDesc.memory = RHI::ResourceMemory::DeviceLocal;
        vertexDesc.usage = RHI::ResourceUsage::VertexBuffer | RHI::ResourceUsage::CopyDestination;
        vertexDesc.initialState = RHI::ResourceState::CopyDestination;
        vertexDesc.debugName = "NEXT cube vertices";

        RHI::BufferDesc indexDesc;
        indexDesc.sizeBytes = sizeof(kCubeIndices);
        indexDesc.memory = RHI::ResourceMemory::DeviceLocal;
        indexDesc.usage = RHI::ResourceUsage::IndexBuffer | RHI::ResourceUsage::CopyDestination;
        indexDesc.initialState = RHI::ResourceState::CopyDestination;
        indexDesc.debugName = "NEXT cube indices";
        cubeIndexBufferView_.format = RHI::IndexFormat::Uint16;
        cubeIndexBufferView_.byteOffset = 0;
        cubeDraw_.indexCount = sizeof(kCubeIndices) / sizeof(kCubeIndices[0]);
        cubeDraw_.instanceCount = 1;
        cubeDraw_.indexOffset = 0;
        cubeDraw_.vertexOffset = 0;
        cubeDraw_.instanceOffset = 0;
        cubeDraw_.stencilReference = 0;
        cubeDraw_.blendConstant = {0.0f, 0.0f, 0.0f, 0.0f};
        const RHI::DrawIndexedValidation drawValidation =
            RHI::ValidateDrawIndexedDesc(cubeDraw_, cubeIndexBufferView_);
        if (!drawValidation) {
            NEXT_LOG_ERROR("Invalid Metal cube draw descriptor: %s (indexFormat=%s)",
                           RHI::DrawDescriptorErrorName(drawValidation.error),
                           RHI::IndexFormatName(drawValidation.indexFormat));
            return false;
        }

        RHI::BufferDesc uniformDesc;
        uniformDesc.sizeBytes = kMetalUniformStride * (kMaxRendererDebugCells + 1);
        uniformDesc.memory = RHI::ResourceMemory::DeviceLocal;
        uniformDesc.usage = RHI::ResourceUsage::ConstantBuffer | RHI::ResourceUsage::CopyDestination;
        uniformDesc.initialState = RHI::ResourceState::CopyDestination;
        uniformDesc.debugName = "NEXT frame uniforms";

        if (!bufferPool.CreateBuffer(vertexBuffer_, vertexDesc) ||
            !bufferPool.CreateBuffer(indexBuffer_, indexDesc) ||
            !bufferPool.CreateBuffer(uniformBuffer_, uniformDesc)) {
            NEXT_LOG_ERROR("Failed to create Metal demo buffers");
            return false;
        }

        if (!uploadQueue.EnqueueBufferUpload(vertexBuffer_, kCubeVertices, sizeof(kCubeVertices)) ||
            !uploadQueue.EnqueueBufferUpload(indexBuffer_, kCubeIndices, sizeof(kCubeIndices))) {
            NEXT_LOG_ERROR("Failed to stage Metal demo mesh buffers");
            return false;
        }

        for (MaterialTextureSlotState& slot : materialTextureSlots_) {
            slot.records = {};
            slot.active = 0;
            slot.pending = kNoPendingMaterialTexture;
            slot.pendingUpload = {};
        }
        nextRendererTextureId_ = 1;
        textureUploadStats_ = {};
        rendererMaterialRecords_ = {};
        activeRendererMaterial_ = 0;
        nextRendererMaterialId_ = 1;
        const size_t baseColorSlot = RendererTextureSlotIndex(RendererTextureSlot::BaseColor);
        MaterialTextureSlotState& baseColorState = materialTextureSlots_[baseColorSlot];
        if (!CreateProceduralBaseColorTexture(texturePool, uploadQueue, baseColorState.textures[baseColorState.active])) {
            NEXT_LOG_ERROR("Failed to create Metal material texture");
            return false;
        }
        if (!CreateNeutralNormalTexture(texturePool, uploadQueue, neutralNormalTexture_)) {
            NEXT_LOG_ERROR("Failed to create Metal neutral normal texture");
            return false;
        }
        if (!CreateNeutralMetallicRoughnessTexture(texturePool, uploadQueue, neutralMetallicRoughnessTexture_)) {
            NEXT_LOG_ERROR("Failed to create Metal neutral metallic roughness texture");
            return false;
        }
        if (!CreateNeutralEmissiveTexture(texturePool, uploadQueue, neutralEmissiveTexture_)) {
            NEXT_LOG_ERROR("Failed to create Metal neutral emissive texture");
            return false;
        }
        if (!CreateNeutralOcclusionTexture(texturePool, uploadQueue, neutralOcclusionTexture_)) {
            NEXT_LOG_ERROR("Failed to create Metal neutral occlusion texture");
            return false;
        }

        const MetalUploadHandle initialUpload = uploadQueue.SubmitUploads();
        if (!initialUpload) {
            NEXT_LOG_ERROR("Failed to upload Metal demo resources");
            return false;
        }
        JobHandle initialUploadWait = uploadQueue.WaitForUploadAsync(initialUpload);
        if (initialUploadWait.IsValid()) {
            JobSystem::Instance().Wait(initialUploadWait);
        }
        const MetalUploadStatus initialUploadStatus = uploadQueue.WaitForUploadStatus(initialUpload);
        if (initialUploadStatus != MetalUploadStatus::Completed) {
            NEXT_LOG_ERROR("Failed to wait for Metal demo resource upload: %s",
                           MetalUploadStatusName(initialUploadStatus));
            return false;
        }
        const MetalFrameGraphResourceRequest readyResources[] = {
            {&vertexBuffer_, RHI::ResourceState::VertexBuffer, RHI::FrameGraphAccessType::Read},
            {&indexBuffer_, RHI::ResourceState::IndexBuffer, RHI::FrameGraphAccessType::Read},
            {&uniformBuffer_, RHI::ResourceState::ConstantBuffer, RHI::FrameGraphAccessType::Read},
            {&baseColorState.textures[baseColorState.active], RHI::ResourceState::ShaderRead, RHI::FrameGraphAccessType::Read},
            {&neutralNormalTexture_, RHI::ResourceState::ShaderRead, RHI::FrameGraphAccessType::Read},
            {&neutralMetallicRoughnessTexture_, RHI::ResourceState::ShaderRead, RHI::FrameGraphAccessType::Read},
            {&neutralEmissiveTexture_, RHI::ResourceState::ShaderRead, RHI::FrameGraphAccessType::Read},
            {&neutralOcclusionTexture_, RHI::ResourceState::ShaderRead, RHI::FrameGraphAccessType::Read},
        };
        if (!TransitionMetalResourcesFromFrameGraph(readyResources,
                                                    sizeof(readyResources) / sizeof(readyResources[0]),
                                                    "NEXT demo resource ready pass",
                                                    RHI::QueueClass::Graphics,
                                                    resourceFrameGraphValidation_,
                                                    resourceFrameGraphTransitionCount_,
                                                    resourceFrameGraphPassCount_,
                                                    resourceFrameGraphReadyPassIndex_,
                                                    resourceFrameGraphReadyPassTransitionCount_)) {
            return false;
        }

        for (size_t slotIndex = 0; slotIndex < materialSamplers_.size(); ++slotIndex) {
            const RendererTextureSlot slot = static_cast<RendererTextureSlot>(slotIndex);
            RHI::SamplerDesc materialSamplerDesc;
            materialSamplerDesc.anisotropyEnabled = true;
            materialSamplerDesc.maxAnisotropy = 4;
            materialSamplerDesc.debugName = RendererSamplerDebugName(slot);
            materialSamplers_[slotIndex] = samplerCache_.GetOrCreate(device, materialSamplerDesc);
        }
        const bool materialSamplersReady = std::all_of(
            materialSamplers_.begin(),
            materialSamplers_.end(),
            [](id<MTLSamplerState> sampler) {
                return sampler != nil;
            });
        if (!baseColorState.textures[baseColorState.active].IsReady() ||
            !neutralNormalTexture_.IsReady() ||
            !neutralMetallicRoughnessTexture_.IsReady() ||
            !neutralEmissiveTexture_.IsReady() ||
            !neutralOcclusionTexture_.IsReady() ||
            !materialSamplersReady) {
            NEXT_LOG_ERROR("Failed to create Metal material resources");
            return false;
        }
        const RHI::ShaderResourceGroupLayoutDesc materialSrgLayout =
            RendererMaterialBindingLayoutInfo{}.ToShaderResourceGroupLayoutDesc("NEXT material layout");
        NSArray<MTLArgumentDescriptor*>* materialArgumentDescs =
            MakeMetalArgumentDescriptors(materialSrgLayout);
        if (!materialArgumentDescs) {
            NEXT_LOG_ERROR("Failed to create Metal material argument descriptors");
            return false;
        }
        materialArgumentEncoder_ = [device.NativeDevice() newArgumentEncoderWithArguments:materialArgumentDescs];
        if (!materialArgumentEncoder_) {
            NEXT_LOG_ERROR("Failed to create Metal material argument encoder");
            return false;
        }
        materialArgumentEncoder_.label = @"NEXT material SRG argument encoder";
        materialArgumentCount_ = static_cast<uint32_t>(materialArgumentDescs.count);
        materialArgumentEncodedLength_ = static_cast<uint64_t>(materialArgumentEncoder_.encodedLength);
        if (materialArgumentEncodedLength_ == 0) {
            NEXT_LOG_ERROR("Metal material argument encoder reported zero encoded length");
            return false;
        }
        const size_t materialArgumentAlignment =
            std::max<size_t>(1, static_cast<size_t>(materialArgumentEncoder_.alignment));
        materialArgumentEncodedStride_ = static_cast<uint64_t>(
            AlignUp(static_cast<size_t>(materialArgumentEncodedLength_), materialArgumentAlignment));
        if (materialArgumentEncodedStride_ == 0 ||
            materialArgumentEncodedStride_ >
                std::numeric_limits<size_t>::max() / kMaterialSrgArgumentBufferDrawCapacity) {
            NEXT_LOG_ERROR("Metal material argument buffer size overflow");
            return false;
        }
        materialArgumentBufferDrawCapacity_ = static_cast<uint32_t>(kMaterialSrgArgumentBufferDrawCapacity);
        RHI::BufferDesc materialArgumentBufferDesc;
        materialArgumentBufferDesc.sizeBytes =
            static_cast<size_t>(materialArgumentEncodedStride_) * kMaterialSrgArgumentBufferDrawCapacity;
        materialArgumentBufferDesc.memory = RHI::ResourceMemory::Shared;
        materialArgumentBufferDesc.usage = RHI::ResourceUsageFlag(RHI::ResourceUsage::ShaderRead);
        materialArgumentBufferDesc.initialState = RHI::ResourceState::ShaderRead;
        materialArgumentBufferDesc.debugName = "NEXT material SRG argument buffer";
        if (!bufferPool.CreateBuffer(materialArgumentBuffer_, materialArgumentBufferDesc)) {
            NEXT_LOG_ERROR("Failed to create Metal material argument buffer");
            return false;
        }
        const RHI::TextureDesc& baseColorDesc = baseColorState.textures[baseColorState.active].GetDesc();
        baseColorState.records[baseColorState.active] = MaterialTextureRecord{
            RendererTextureHandle{nextRendererTextureId_++},
            RendererTextureSlot::BaseColor,
            RendererTextureFormat::RGBA8Unorm,
            0,
            baseColorDesc.extent.width,
            baseColorDesc.extent.height,
            true};
        RendererMaterialDesc defaultMaterial;
        defaultMaterial.SetTexture(RendererTextureSlot::BaseColor,
                                   baseColorState.records[baseColorState.active].texture);
        rendererMaterialRecords_[activeRendererMaterial_] = RendererMaterialRecord{
            RendererMaterialHandle{nextRendererMaterialId_++},
            defaultMaterial,
            true};
        if (!EncodeMaterialArgumentBuffers(1)) {
            NEXT_LOG_ERROR("Failed to encode Metal material argument buffer");
            return false;
        }

        return true;
    }
}

void MetalSceneResources::Shutdown(MetalDevice* device, uint64_t submittedFrameIndex) {
    samplerCache_.Shutdown(device, submittedFrameIndex);
    if (texturePool_) {
        for (MaterialTextureSlotState& slot : materialTextureSlots_) {
            for (MetalTexture& texture : slot.textures) {
                texturePool_->ReleaseTexture(texture, submittedFrameIndex);
            }
        }
    } else {
        for (MaterialTextureSlotState& slot : materialTextureSlots_) {
            for (MetalTexture& texture : slot.textures) {
                texture.Shutdown(device, submittedFrameIndex);
            }
        }
    }
    if (bufferPool_) {
        bufferPool_->ReleaseBuffer(materialArgumentBuffer_, submittedFrameIndex);
        bufferPool_->ReleaseBuffer(uniformBuffer_, submittedFrameIndex);
        bufferPool_->ReleaseBuffer(indexBuffer_, submittedFrameIndex);
        bufferPool_->ReleaseBuffer(vertexBuffer_, submittedFrameIndex);
    } else {
        materialArgumentBuffer_.Shutdown(device, submittedFrameIndex);
        uniformBuffer_.Shutdown(device, submittedFrameIndex);
        indexBuffer_.Shutdown(device, submittedFrameIndex);
        vertexBuffer_.Shutdown(device, submittedFrameIndex);
    }
    if (texturePool_) {
        texturePool_->ReleaseTexture(neutralNormalTexture_, submittedFrameIndex);
        texturePool_->ReleaseTexture(neutralMetallicRoughnessTexture_, submittedFrameIndex);
        texturePool_->ReleaseTexture(neutralEmissiveTexture_, submittedFrameIndex);
        texturePool_->ReleaseTexture(neutralOcclusionTexture_, submittedFrameIndex);
    } else {
        neutralNormalTexture_.Shutdown(device, submittedFrameIndex);
        neutralMetallicRoughnessTexture_.Shutdown(device, submittedFrameIndex);
        neutralEmissiveTexture_.Shutdown(device, submittedFrameIndex);
        neutralOcclusionTexture_.Shutdown(device, submittedFrameIndex);
    }
    pipelineCache_.Shutdown(device, submittedFrameIndex);
    renderPipeline_ = nullptr;
    materialArgumentEncoder_ = nil;
    materialArgumentCount_ = 0;
    materialArgumentEncodedLength_ = 0;
    materialArgumentEncodedStride_ = 0;
    materialArgumentBufferDrawCapacity_ = 0;
    materialArgumentEncodedDrawCount_ = 0;
    materialArgumentEncodedResourceCount_ = 0;
    materialArgumentBufferBindCount_ = 0;
    QueueForRelease(device, shaderLibrary_, submittedFrameIndex);

    materialSamplers_.fill(nil);
    shaderLibrary_ = nil;
    shaderLibraryDesc_ = {};
    shaderLibraryInput_ = {};
    bufferPool_ = nullptr;
    texturePool_ = nullptr;
    resourceFrameGraphValidation_ = {};
    resourceFrameGraphTransitionCount_ = 0;
    resourceFrameGraphPassCount_ = 0;
    resourceFrameGraphReadyPassIndex_ = RHI::kInvalidFrameGraphPassIndex;
    resourceFrameGraphReadyPassTransitionCount_ = 0;
    for (MaterialTextureSlotState& slot : materialTextureSlots_) {
        slot.records = {};
        slot.active = 0;
        slot.pending = kNoPendingMaterialTexture;
        slot.pendingUpload = {};
    }
    nextRendererTextureId_ = 1;
    textureUploadStats_ = {};
    rendererMaterialRecords_ = {};
    activeRendererMaterial_ = 0;
    nextRendererMaterialId_ = 1;
    cubeIndexBufferView_ = {};
    cubeDraw_ = {};
}

bool MetalSceneResources::IsReady() const {
    const size_t baseColorSlot = RendererTextureSlotIndex(RendererTextureSlot::BaseColor);
    const MaterialTextureSlotState& baseColorState = materialTextureSlots_[baseColorSlot];
    const bool materialSamplersReady = std::all_of(
        materialSamplers_.begin(),
        materialSamplers_.end(),
        [](id<MTLSamplerState> sampler) {
            return sampler != nil;
        });
    return renderPipeline_ && renderPipeline_->IsReady() && vertexBuffer_.IsReady() && indexBuffer_.IsReady() &&
        uniformBuffer_.IsReady() && materialArgumentEncoder_ &&
        materialArgumentBuffer_.IsReady() && materialArgumentEncodedDrawCount_ != 0 &&
        materialArgumentEncodedResourceCount_ != 0 &&
        baseColorState.textures[baseColorState.active].IsReady() &&
        neutralNormalTexture_.IsReady() && neutralMetallicRoughnessTexture_.IsReady() &&
        neutralEmissiveTexture_.IsReady() && neutralOcclusionTexture_.IsReady() && materialSamplersReady;
}

bool MetalSceneResources::UploadUniforms(MetalDevice& device,
                                         MetalUploadQueue& uploadQueue,
                                         const void* data,
                                         size_t dataBytes) {
    if (!data || dataBytes == 0 || dataBytes > uniformBuffer_.GetDesc().sizeBytes) {
        return false;
    }

    const RHI::ResourceState previousUniformState = uniformBuffer_.GetCurrentState();
    if (!TransitionMetalResourceFromFrameGraph(uniformBuffer_,
                                              RHI::ResourceState::CopyDestination,
                                              RHI::FrameGraphAccessType::Write,
                                              uploadQueue.GetQueueClass(),
                                              "NEXT frame uniforms upload pass")) {
        return false;
    }
    if (!device.NativeDevice() || !uploadQueue.EnqueueBufferUpload(uniformBuffer_, data, dataBytes)) {
        uniformBuffer_.SetCurrentState(previousUniformState);
        return false;
    }

    const MetalUploadHandle frameUpload = uploadQueue.SubmitUploads();
    if (!frameUpload) {
        uniformBuffer_.SetCurrentState(previousUniformState);
        return false;
    }

    JobHandle frameUploadWait = uploadQueue.WaitForUploadAsync(frameUpload);
    if (frameUploadWait.IsValid()) {
        JobSystem::Instance().Wait(frameUploadWait);
        const MetalUploadStatus status = uploadQueue.WaitForUploadStatus(frameUpload);
        if (status != MetalUploadStatus::Completed) {
            NEXT_LOG_ERROR("Failed to upload Metal frame uniforms: %s", MetalUploadStatusName(status));
            uniformBuffer_.SetCurrentState(previousUniformState);
            return false;
        }
        return TransitionMetalResourceFromFrameGraph(uniformBuffer_,
                                                    RHI::ResourceState::ConstantBuffer,
                                                    RHI::FrameGraphAccessType::Read,
                                                    RHI::QueueClass::Graphics,
                                                    "NEXT frame uniforms ready pass");
    }

    const MetalUploadStatus status = uploadQueue.WaitForUploadStatus(frameUpload);
    if (status != MetalUploadStatus::Completed) {
        NEXT_LOG_ERROR("Failed to upload Metal frame uniforms: %s", MetalUploadStatusName(status));
        uniformBuffer_.SetCurrentState(previousUniformState);
        return false;
    }
    return TransitionMetalResourceFromFrameGraph(uniformBuffer_,
                                                RHI::ResourceState::ConstantBuffer,
                                                RHI::FrameGraphAccessType::Read,
                                                RHI::QueueClass::Graphics,
                                                "NEXT frame uniforms ready pass");
}

RendererTextureUploadHandle MetalSceneResources::UploadMaterialTexture(MetalDevice& device,
                                                                       MetalUploadQueue& uploadQueue,
                                                                       const RendererTextureUploadDesc& texture,
                                                                       uint64_t submittedFrameIndex) {
    if (!device.NativeDevice() || !texturePool_) {
        return {};
    }

    const RendererTextureUploadValidation validation = ValidateRendererTextureUploadDesc(texture);
    if (!validation) {
        NEXT_LOG_WARNING("Metal texture upload rejected: %s (slot=%s format=%s %ux%u bytes=%zu required=%zu)",
                         RendererTextureUploadValidationErrorName(validation.error),
                         RendererTextureSlotName(texture.slot),
                         RendererTextureFormatName(texture.format),
                         texture.width,
                         texture.height,
                         texture.pixelBytes,
                         validation.requiredBytes);
        return {};
    }

    const uint32_t bytesPerPixel = RendererTextureFormatBytesPerPixel(texture.format);
    const RHI::Format rhiFormat = ToRHIFormat(texture.format);
    if (rhiFormat == RHI::Format::Unknown) {
        NEXT_LOG_WARNING("Metal texture upload rejected: unsupported renderer texture format");
        return {};
    }

    const size_t slotIndex = RendererTextureSlotIndex(texture.slot);
    PromoteCompletedMaterialTextureUploads(uploadQueue, submittedFrameIndex);

    MaterialTextureSlotState& slotState = materialTextureSlots_[slotIndex];
    if (slotState.pending != kNoPendingMaterialTexture) {
        NEXT_LOG_WARNING("Metal texture upload rejected: %s upload already pending",
                         RendererTextureSlotName(texture.slot));
        return {};
    }

    RHI::TextureDesc textureDesc;
    textureDesc.extent.width = texture.width;
    textureDesc.extent.height = texture.height;
    textureDesc.format = rhiFormat;
    textureDesc.memory = RHI::ResourceMemory::DeviceLocal;
    textureDesc.usage = RHI::ResourceUsage::ShaderRead | RHI::ResourceUsage::CopyDestination;
    textureDesc.initialState = RHI::ResourceState::CopyDestination;
    textureDesc.debugName = RendererTextureDebugName(texture.slot);

    const size_t targetTexture = slotState.active == 0 ? 1 : 0;
    if (slotState.textures[targetTexture].IsReady()) {
        texturePool_->ReleaseTexture(slotState.textures[targetTexture], submittedFrameIndex);
        slotState.records[targetTexture] = {};
    }
    if (!texturePool_->CreateTexture(slotState.textures[targetTexture], textureDesc)) {
        return {};
    }

    if (!uploadQueue.EnqueueTexture2DUpload(slotState.textures[targetTexture],
                                            texture.pixels,
                                            texture.pixelBytes,
                                            texture.width,
                                            texture.height,
                                            bytesPerPixel)) {
        texturePool_->ReleaseTexture(slotState.textures[targetTexture], submittedFrameIndex);
        return {};
    }

    const MetalUploadHandle textureUpload = uploadQueue.SubmitUploads();
    if (!textureUpload) {
        texturePool_->ReleaseTexture(slotState.textures[targetTexture], submittedFrameIndex);
        return {};
    }

    slotState.pending = targetTexture;
    slotState.pendingUpload = textureUpload;
    const RendererTextureHandle rendererTexture{nextRendererTextureId_++};
    slotState.records[targetTexture] = MaterialTextureRecord{
        rendererTexture,
        texture.slot,
        texture.format,
        texture.sourceAssetId,
        texture.width,
        texture.height,
        true};
    textureUploadStats_.queuedUploads++;
    textureUploadStats_.lastQueuedUpload = textureUpload.serial;
    textureUploadStats_.lastQueuedTexture = rendererTexture;
    textureUploadStats_.lastQueuedSlot = texture.slot;
    textureUploadStats_.materialTextureUploadPending = true;
    if (texture.slot == RendererTextureSlot::BaseColor) {
        textureUploadStats_.baseColorUploadPending = true;
    }
    return RendererTextureUploadHandle{textureUpload.serial, rendererTexture};
}

void MetalSceneResources::PromoteCompletedMaterialTextureUploads(MetalUploadQueue& uploadQueue,
                                                                 uint64_t submittedFrameIndex) {
    bool anyPending = false;
    bool materialArgumentsNeedEncode = false;
    for (size_t slotIndex = 0; slotIndex < materialTextureSlots_.size(); ++slotIndex) {
        MaterialTextureSlotState& slotState = materialTextureSlots_[slotIndex];
        if (slotState.pending == kNoPendingMaterialTexture || !slotState.pendingUpload) {
            continue;
        }

        const MetalUploadStatus uploadStatus = uploadQueue.GetUploadStatus(slotState.pendingUpload);
        if (uploadStatus == MetalUploadStatus::Pending) {
            anyPending = true;
            continue;
        }

        const RendererTextureSlot slot = slotState.records[slotState.pending].slot;
        if (uploadStatus != MetalUploadStatus::Completed) {
            const uint64_t failedUpload = slotState.pendingUpload.serial;
            const RendererTextureHandle failedTexture = slotState.records[slotState.pending].texture;
            NEXT_LOG_WARNING("Metal %s texture upload ended with %s; keeping previous texture",
                             RendererTextureSlotName(slot),
                             MetalUploadStatusName(uploadStatus));
            if (texturePool_) {
                texturePool_->ReleaseTexture(slotState.textures[slotState.pending], submittedFrameIndex);
            }
            slotState.records[slotState.pending] = {};
            slotState.pending = kNoPendingMaterialTexture;
            slotState.pendingUpload = {};
            textureUploadStats_.failedUploads++;
            textureUploadStats_.lastFailedUpload = failedUpload;
            textureUploadStats_.lastFailedTexture = failedTexture;
            textureUploadStats_.lastFailedSlot = slot;
            if (slot == RendererTextureSlot::BaseColor) {
                textureUploadStats_.baseColorUploadPending = false;
            }
            materialArgumentsNeedEncode = true;
            continue;
        }

        const uint64_t completedUpload = slotState.pendingUpload.serial;
        const RendererTextureHandle completedTexture = slotState.records[slotState.pending].texture;
        const size_t previousTexture = slotState.active;
        const MetalFrameGraphResourceRequest readyTexture[] = {
            {&slotState.textures[slotState.pending],
             RHI::ResourceState::ShaderRead,
             RHI::FrameGraphAccessType::Read},
        };
        RHI::FrameGraphDescriptorValidation readyValidation;
        uint32_t readyTransitionCount = 0;
        uint32_t readyPassCount = 0;
        uint32_t readyPassIndex = RHI::kInvalidFrameGraphPassIndex;
        uint32_t readyPassTransitionCount = 0;
        if (!TransitionMetalResourcesFromFrameGraph(readyTexture,
                                                    sizeof(readyTexture) / sizeof(readyTexture[0]),
                                                    "NEXT material texture ready pass",
                                                    RHI::QueueClass::Graphics,
                                                    readyValidation,
                                                    readyTransitionCount,
                                                    readyPassCount,
                                                    readyPassIndex,
                                                    readyPassTransitionCount)) {
            const uint64_t failedUpload = slotState.pendingUpload.serial;
            const RendererTextureHandle failedTexture = slotState.records[slotState.pending].texture;
            if (texturePool_) {
                texturePool_->ReleaseTexture(slotState.textures[slotState.pending], submittedFrameIndex);
            }
            slotState.records[slotState.pending] = {};
            slotState.pending = kNoPendingMaterialTexture;
            slotState.pendingUpload = {};
            textureUploadStats_.failedUploads++;
            textureUploadStats_.lastFailedUpload = failedUpload;
            textureUploadStats_.lastFailedTexture = failedTexture;
            textureUploadStats_.lastFailedSlot = slot;
            continue;
        }
        slotState.active = slotState.pending;
        slotState.pending = kNoPendingMaterialTexture;
        slotState.pendingUpload = {};
        textureUploadStats_.completedUploads++;
        textureUploadStats_.lastCompletedUpload = completedUpload;
        textureUploadStats_.lastCompletedTexture = completedTexture;
        textureUploadStats_.lastCompletedSlot = slot;
        if (slot == RendererTextureSlot::BaseColor) {
            textureUploadStats_.baseColorUploadPending = false;
        }
        materialArgumentsNeedEncode = true;

        if (texturePool_ && previousTexture != slotState.active) {
            texturePool_->ReleaseTexture(slotState.textures[previousTexture], submittedFrameIndex);
            slotState.records[previousTexture] = {};
        }

        const RHI::TextureDesc& desc = slotState.textures[slotState.active].GetDesc();
        NEXT_LOG_INFO("Metal material texture upload completed: slot=%s %ux%u",
                      RendererTextureSlotName(slot),
                      desc.extent.width,
                      desc.extent.height);
    }

    textureUploadStats_.materialTextureUploadPending = anyPending;
    if (materialArgumentsNeedEncode && materialArgumentBuffer_.IsReady() &&
        !EncodeMaterialArgumentBuffers(std::max<size_t>(1, materialArgumentEncodedDrawCount_))) {
        NEXT_LOG_WARNING("Failed to refresh Metal material argument buffer after texture upload promotion");
    }
}

RendererTextureUploadStats MetalSceneResources::GetTextureUploadStats() const {
    RendererTextureUploadStats stats = textureUploadStats_;
    stats.baseColorUploadPending = false;
    stats.materialTextureUploadPending = false;
    stats.activeMaterialTextureCount = 0;
    stats.activeMaterialTextures = {};

    for (size_t slotIndex = 0; slotIndex < materialTextureSlots_.size(); ++slotIndex) {
        const MaterialTextureSlotState& slotState = materialTextureSlots_[slotIndex];
        const bool slotPending = slotState.pending != kNoPendingMaterialTexture;
        stats.materialTextureUploadPending = stats.materialTextureUploadPending || slotPending;
        if (slotIndex == RendererTextureSlotIndex(RendererTextureSlot::BaseColor)) {
            stats.baseColorUploadPending = slotPending;
        }

        if (slotState.active >= slotState.textures.size() || !slotState.textures[slotState.active].IsReady()) {
            continue;
        }

        const MaterialTextureRecord& record = slotState.records[slotState.active];
        if (!record.valid) {
            continue;
        }

        stats.activeMaterialTextureCount++;
        const RHI::TextureDesc& desc = slotState.textures[slotState.active].GetDesc();
        const size_t recordSlotIndex = RendererTextureSlotIndex(record.slot);
        if (recordSlotIndex < stats.activeMaterialTextures.size()) {
            RendererActiveTextureSlotInfo& activeInfo = stats.activeMaterialTextures[recordSlotIndex];
            activeInfo.texture = record.texture;
            activeInfo.sourceAssetId = record.sourceAssetId;
            activeInfo.width = desc.extent.width;
            activeInfo.height = desc.extent.height;
            activeInfo.active = true;
        }

        if (record.slot == RendererTextureSlot::BaseColor) {
            stats.activeBaseColorTexture = record.texture;
            stats.activeBaseColorSourceAssetId = record.sourceAssetId;
            stats.activeBaseColorWidth = desc.extent.width;
            stats.activeBaseColorHeight = desc.extent.height;
        }
    }

    return stats;
}

bool MetalSceneResources::GetTextureInfo(RendererTextureHandle texture, RendererTextureInfo& outInfo) const {
    outInfo = {};
    if (!texture) {
        return false;
    }

    for (const MaterialTextureSlotState& slotState : materialTextureSlots_) {
        for (size_t textureIndex = 0; textureIndex < slotState.records.size(); ++textureIndex) {
            const MaterialTextureRecord& record = slotState.records[textureIndex];
            if (!record.valid || record.texture.id != texture.id) {
                continue;
            }

            outInfo.texture = record.texture;
            outInfo.slot = record.slot;
            outInfo.format = record.format;
            outInfo.sourceAssetId = record.sourceAssetId;
            outInfo.width = record.width;
            outInfo.height = record.height;
            outInfo.active = textureIndex == slotState.active;
            outInfo.uploadPending = textureIndex == slotState.pending;
            return true;
        }
    }

    return false;
}

MetalPipelineCacheStats MetalSceneResources::GetPipelineCacheStats() const {
    return pipelineCache_.GetStats();
}

RendererSamplerStats MetalSceneResources::GetSamplerStats() const {
    RendererSamplerStats stats;
    stats.cachedSamplerCount = samplerCache_.GetCachedSamplerCount();
    stats.materialSamplerSlotCount = static_cast<uint32_t>(materialSamplers_.size());
    for (id<MTLSamplerState> sampler : materialSamplers_) {
        if (sampler != nil) {
            ++stats.boundMaterialSamplerCount;
        }
    }
    stats.ready = stats.HasCachedSamplers() && stats.HasCompleteMaterialSamplerTable();
    return stats;
}

RendererMaterialStats MetalSceneResources::GetMaterialStats() const {
    RendererMaterialStats stats;
    stats.materialCapacity = static_cast<uint32_t>(rendererMaterialRecords_.size());
    const RHI::ShaderResourceGroupLayoutDesc shaderResourceGroupLayout =
        stats.bindingLayout.ToShaderResourceGroupLayoutDesc("NEXT material layout");
    stats.SetShaderResourceGroupLayout(shaderResourceGroupLayout);
    stats.shaderResourceGroupArgumentEncoderReady = materialArgumentEncoder_ != nil;
    stats.shaderResourceGroupArgumentCount = materialArgumentCount_;
    stats.shaderResourceGroupEncodedLength = materialArgumentEncodedLength_;
    stats.shaderResourceGroupEncodedStride = materialArgumentEncodedStride_;
    stats.shaderResourceGroupArgumentBufferReady = materialArgumentBuffer_.IsReady();
    stats.shaderResourceGroupArgumentBufferBytes = materialArgumentBuffer_.GetDesc().sizeBytes;
    stats.shaderResourceGroupArgumentBufferDrawCapacity = materialArgumentBufferDrawCapacity_;
    stats.shaderResourceGroupEncodedDrawCount = materialArgumentEncodedDrawCount_;
    stats.shaderResourceGroupArgumentBufferBindingIndex =
        shaderLibraryDesc_.materialShaderResourceGroupArgumentBufferIndex != kMetalShaderManifestInvalidBindingIndex
            ? shaderLibraryDesc_.materialShaderResourceGroupArgumentBufferIndex
            : kRendererMaterialShaderResourceGroupArgumentBufferIndex;
    stats.shaderResourceGroupArgumentBufferBound = materialArgumentBufferBindCount_ != 0;
    stats.shaderResourceGroupArgumentBufferBindCount = materialArgumentBufferBindCount_;
    stats.shaderResourceGroupEncodedResourceCount = materialArgumentEncodedResourceCount_;
    for (size_t index = 0; index < rendererMaterialRecords_.size(); ++index) {
        const RendererMaterialRecord& record = rendererMaterialRecords_[index];
        if (!record.valid) {
            continue;
        }

        ++stats.materialCount;
        if (index == activeRendererMaterial_) {
            stats.activeMaterial = record.material;
            stats.activeMaterialIndex = static_cast<uint32_t>(index);
            stats.activeMaterialBoundTextureCount = static_cast<uint32_t>(record.desc.BoundTextureCount());
            stats.activeMaterialCompleteTextureSet = record.desc.HasCompleteTextureSet();
            stats.activeMaterialParametersValid = record.desc.HasValidParameters();
            for (size_t slotIndex = 0; slotIndex < stats.activeMaterialBindings.size(); ++slotIndex) {
                const RendererTextureSlot slot = static_cast<RendererTextureSlot>(slotIndex);
                RendererMaterialBindingInfo binding = ResolveMaterialBinding(slot, record.desc.GetTexture(slot));
                stats.activeMaterialBindings[slotIndex] = binding;
                if (binding.textureReady) {
                    ++stats.shaderVisibleTextureCount;
                }
                if (binding.samplerReady) {
                    ++stats.shaderVisibleSamplerCount;
                }
                if (binding.UsesFallbackTexture()) {
                    ++stats.fallbackTextureCount;
                }
            }
        }
    }
    stats.SetShaderResourceGroupBoundResourceCount(0, uniformBuffer_.IsReady() ? 1u : 0u);
    stats.SetShaderResourceGroupBoundResourceCount(1, stats.shaderVisibleTextureCount);
    stats.SetShaderResourceGroupBoundResourceCount(2, stats.shaderVisibleSamplerCount);
    stats.ready = IsReady() && stats.HasActiveMaterial() && stats.HasValidShaderResourceGroupLayout() &&
        stats.HasCompleteShaderResourceGroupResources() && stats.HasCompleteShaderResourceGroupEncoding();
    return stats;
}

RendererResourceStateStats MetalSceneResources::GetResourceStateStats() const {
    RendererResourceStateStats stats;
    stats.frameGraphValidation = resourceFrameGraphValidation_;
    stats.frameGraphTransitionCount = resourceFrameGraphTransitionCount_;
    stats.frameGraphPassCount = resourceFrameGraphPassCount_;
    stats.frameGraphReadyPassIndex = resourceFrameGraphReadyPassIndex_;
    stats.frameGraphReadyPassTransitionCount = resourceFrameGraphReadyPassTransitionCount_;

    auto recordResource = [&stats](RendererResourceStateKind kind,
                                   const RHI::Resource& resource,
                                   RHI::ResourceState expectedState,
                                   uint32_t bindingIndex,
                                   RendererTextureSlot textureSlot = RendererTextureSlot::BaseColor) {
        if (stats.resourceCount >= stats.resources.size()) {
            return;
        }

        RendererResourceStateInfo& info = stats.resources[stats.resourceCount];
        info.active = true;
        info.index = stats.resourceCount;
        info.kind = kind;
        info.resourceType = resource.GetResourceType();
        info.usage = resource.GetUsageFlags();
        info.currentState = resource.GetCurrentState();
        info.expectedState = expectedState;
        info.bindingIndex = bindingIndex;
        info.textureSlot = textureSlot;
        info.SetDebugName(resource.GetDebugName());
        ++stats.resourceCount;
        if (info.IsBuffer()) {
            ++stats.bufferResourceCount;
        }
        if (info.IsTexture()) {
            ++stats.textureResourceCount;
        }
        if (info.MatchesExpectedState()) {
            ++stats.expectedStateMatchCount;
        }
    };

    if (vertexBuffer_.IsReady()) {
        recordResource(RendererResourceStateKind::VertexBuffer,
                       vertexBuffer_,
                       RHI::ResourceState::VertexBuffer,
                       kRendererGeometryVertexBufferIndex);
    }
    if (indexBuffer_.IsReady()) {
        recordResource(RendererResourceStateKind::IndexBuffer,
                       indexBuffer_,
                       RHI::ResourceState::IndexBuffer,
                       kRendererInvalidBindingIndex);
    }
    if (uniformBuffer_.IsReady()) {
        recordResource(RendererResourceStateKind::UniformBuffer,
                       uniformBuffer_,
                       RHI::ResourceState::ConstantBuffer,
                       kRendererMaterialUniformBufferIndex);
    }

    const RendererMaterialDesc activeMaterial = GetActiveMaterialDesc();
    for (size_t slotIndex = 0; slotIndex < kRendererTextureSlotCount; ++slotIndex) {
        const RendererTextureSlot slot = static_cast<RendererTextureSlot>(slotIndex);
        const MetalTexture* texture = ResolveMaterialTexture(slot, activeMaterial.GetTexture(slot));
        if (!texture) {
            switch (slot) {
                case RendererTextureSlot::Normal:
                    texture = neutralNormalTexture_.IsReady() ? &neutralNormalTexture_ : nullptr;
                    break;
                case RendererTextureSlot::MetallicRoughness:
                    texture = neutralMetallicRoughnessTexture_.IsReady() ? &neutralMetallicRoughnessTexture_ : nullptr;
                    break;
                case RendererTextureSlot::Emissive:
                    texture = neutralEmissiveTexture_.IsReady() ? &neutralEmissiveTexture_ : nullptr;
                    break;
                case RendererTextureSlot::Occlusion:
                    texture = neutralOcclusionTexture_.IsReady() ? &neutralOcclusionTexture_ : nullptr;
                    break;
                case RendererTextureSlot::BaseColor:
                default:
                    break;
            }
        }
        if (texture && texture->IsReady()) {
            recordResource(RendererResourceStateKind::MaterialTexture,
                           *texture,
                           RHI::ResourceState::ShaderRead,
                           RendererMaterialTextureBindingIndex(slot),
                           slot);
        }
    }

    stats.ready = IsReady() && stats.HasAllExpectedStates();
    return stats;
}

bool MetalSceneResources::AppendDrawFrameGraphResources(
    RHI::FrameGraphDesc& graph,
    RHI::FrameGraphPassDesc& renderPass,
    MetalFrameGraphResourceUsageTable* resourceUsages,
    RHI::FrameGraphDescriptorValidation& outValidation) const {
    outValidation = {};
    if (!IsReady()) {
        outValidation.error = RHI::FrameGraphDescriptorError::MissingResource;
        return false;
    }

    if (!AppendMetalFrameGraphResource(graph,
                                       renderPass,
                                       vertexBuffer_,
                                       vertexBuffer_.NativeBuffer(),
                                       RHI::ResourceState::VertexBuffer,
                                       RHI::FrameGraphAccessType::Read,
                                       RHI::ShaderStageFlag(RHI::ShaderStage::Vertex),
                                       resourceUsages,
                                       outValidation) ||
        !AppendMetalFrameGraphResource(graph,
                                       renderPass,
                                       indexBuffer_,
                                       indexBuffer_.NativeBuffer(),
                                       RHI::ResourceState::IndexBuffer,
                                       RHI::FrameGraphAccessType::Read,
                                       RHI::ShaderStageFlag(RHI::ShaderStage::Vertex),
                                       resourceUsages,
                                       outValidation) ||
        !AppendMetalFrameGraphResource(graph,
                                       renderPass,
                                       uniformBuffer_,
                                       uniformBuffer_.NativeBuffer(),
                                       RHI::ResourceState::ConstantBuffer,
                                       RHI::FrameGraphAccessType::Read,
                                       RHI::ShaderStage::Vertex | RHI::ShaderStage::Fragment,
                                       resourceUsages,
                                       outValidation) ||
        !AppendMetalFrameGraphResource(graph,
                                       renderPass,
                                       materialArgumentBuffer_,
                                       materialArgumentBuffer_.NativeBuffer(),
                                       RHI::ResourceState::ShaderRead,
                                       RHI::FrameGraphAccessType::Read,
                                       RHI::ShaderStageFlag(RHI::ShaderStage::Fragment),
                                       resourceUsages,
                                       outValidation)) {
        return false;
    }

    std::array<const MetalTexture*, kRendererTextureSlotCount> materialTextures;
    if (!ResolveActiveMaterialTextures(materialTextures)) {
        outValidation.error = RHI::FrameGraphDescriptorError::MissingResource;
        outValidation.resourceIndex = graph.resourceCount;
        return false;
    }
    for (const MetalTexture* texture : materialTextures) {
        if (!texture ||
            !AppendMetalFrameGraphResource(graph,
                                           renderPass,
                                           *texture,
                                           texture->NativeTexture(),
                                           RHI::ResourceState::ShaderRead,
                                           RHI::FrameGraphAccessType::Read,
                                           RHI::ShaderStageFlag(RHI::ShaderStage::Fragment),
                                           resourceUsages,
                                           outValidation)) {
            return false;
        }
    }

    return true;
}

RendererGeometryStats MetalSceneResources::GetGeometryStats() const {
    RendererGeometryStats stats;
    stats.vertexBufferReady = vertexBuffer_.IsReady();
    stats.indexBufferReady = indexBuffer_.IsReady();
    stats.vertexBufferIndex = kRendererGeometryVertexBufferIndex;
    stats.vertexBufferBytes = vertexBuffer_.GetDesc().sizeBytes;
    stats.indexBufferBytes = indexBuffer_.GetDesc().sizeBytes;
    stats.indexFormat = cubeIndexBufferView_.format;
    stats.indexBufferByteOffset = cubeIndexBufferView_.byteOffset;
    stats.indexCount = cubeDraw_.indexCount;
    stats.instanceCount = cubeDraw_.instanceCount;
    stats.indexOffset = cubeDraw_.indexOffset;
    stats.vertexOffset = cubeDraw_.vertexOffset;
    stats.instanceOffset = cubeDraw_.instanceOffset;
    stats.stencilReference = cubeDraw_.stencilReference;
    stats.blendConstant = cubeDraw_.blendConstant;
    if (const RHI::GraphicsPipelineDesc* pipelineDesc = GetRenderPipelineDesc()) {
        if (pipelineDesc->vertexInput.bufferCount > 0) {
            stats.vertexStride = pipelineDesc->vertexInput.buffers[0].stride;
        }
    }

    uint64_t resolvedOffset = 0;
    const bool validDraw = RHI::ValidateDrawIndexedDesc(cubeDraw_, cubeIndexBufferView_) &&
        RHI::DrawIndexedIndexBufferOffsetBytes(cubeDraw_, cubeIndexBufferView_, resolvedOffset);
    stats.resolvedIndexBufferByteOffset = resolvedOffset;
    stats.ready = stats.vertexBufferReady && stats.indexBufferReady && validDraw;
    return stats;
}

const RHI::GraphicsPipelineDesc* MetalSceneResources::GetRenderPipelineDesc() const {
    return renderPipeline_ ? &renderPipeline_->GetDesc() : nullptr;
}

const char* MetalSceneResources::GetRenderPipelineDebugName() const {
    const RHI::GraphicsPipelineDesc* desc = GetRenderPipelineDesc();
    if (!desc || !desc->debugName) {
        return "";
    }
    return desc->debugName;
}

RendererTextureUploadStatus MetalSceneResources::GetTextureUploadStatus(RendererTextureUploadHandle handle,
                                                                        const MetalUploadQueue& uploadQueue) const {
    if (!handle) {
        return RendererTextureUploadStatus::Unknown;
    }

    for (const MaterialTextureSlotState& slotState : materialTextureSlots_) {
        if (slotState.pendingUpload && slotState.pendingUpload.serial == handle.id) {
            return RendererTextureUploadStatus::Pending;
        }
    }

    const MetalUploadStatus uploadStatus = uploadQueue.GetUploadStatus(MetalUploadHandle{handle.id});
    switch (uploadStatus) {
        case MetalUploadStatus::Pending:
            return RendererTextureUploadStatus::Pending;
        case MetalUploadStatus::Completed:
            return RendererTextureUploadStatus::Completed;
        case MetalUploadStatus::Failed:
            return RendererTextureUploadStatus::Failed;
        case MetalUploadStatus::Unknown:
        default:
            break;
    }

    if (textureUploadStats_.lastFailedUpload == handle.id) {
        return RendererTextureUploadStatus::Failed;
    }

    if (textureUploadStats_.lastCompletedUpload == handle.id) {
        return RendererTextureUploadStatus::Completed;
    }

    return RendererTextureUploadStatus::Unknown;
}

RendererMaterialHandle MetalSceneResources::CreateMaterial(const RendererMaterialDesc& material) {
    const RendererMaterialValidation validation = ValidateRendererMaterialDesc(material);
    if (!validation) {
        NEXT_LOG_WARNING("Metal material creation rejected: %s (baseColorIndex=%zu roughness=%.3f "
                         "metallic=%.3f exposure=%.3f)",
                         RendererMaterialValidationErrorName(validation.error),
                         validation.baseColorFactorIndex,
                         material.roughness,
                         material.metallic,
                         material.exposure);
        return {};
    }

    RendererMaterialDesc desc = material;
    if (!desc.GetTexture(RendererTextureSlot::BaseColor)) {
        const size_t baseColorSlot = RendererTextureSlotIndex(RendererTextureSlot::BaseColor);
        const MaterialTextureSlotState& baseColorState = materialTextureSlots_[baseColorSlot];
        if (baseColorState.records[baseColorState.active].valid) {
            desc.SetTexture(RendererTextureSlot::BaseColor,
                            baseColorState.records[baseColorState.active].texture);
        }
    }
    if (!ValidateMaterialTextureBindings(desc, "creation")) {
        return {};
    }

    for (RendererMaterialRecord& record : rendererMaterialRecords_) {
        if (record.valid) {
            continue;
        }

        record.material = RendererMaterialHandle{nextRendererMaterialId_++};
        record.desc = desc;
        record.valid = true;
        return record.material;
    }

    NEXT_LOG_WARNING("Metal material creation rejected: renderer material table is full");
    return {};
}

bool MetalSceneResources::UpdateMaterial(RendererMaterialHandle handle, const RendererMaterialDesc& material) {
    if (!handle) {
        return false;
    }

    for (size_t index = 0; index < rendererMaterialRecords_.size(); ++index) {
        RendererMaterialRecord& record = rendererMaterialRecords_[index];
        if (!record.valid || record.material.id != handle.id) {
            continue;
        }

        const RendererMaterialValidation validation = ValidateRendererMaterialDesc(material);
        if (!validation) {
            NEXT_LOG_WARNING("Metal material update rejected: %s (baseColorIndex=%zu roughness=%.3f "
                             "metallic=%.3f exposure=%.3f)",
                             RendererMaterialValidationErrorName(validation.error),
                             validation.baseColorFactorIndex,
                             material.roughness,
                             material.metallic,
                             material.exposure);
            return false;
        }
        if (!ValidateMaterialTextureBindings(material, "update")) {
            return false;
        }

        const RendererMaterialDesc previousDesc = record.desc;
        record.desc = material;
        if (index == activeRendererMaterial_ && materialArgumentBuffer_.IsReady() &&
            !EncodeMaterialArgumentBuffers(std::max<size_t>(1, materialArgumentEncodedDrawCount_))) {
            record.desc = previousDesc;
            EncodeMaterialArgumentBuffers(std::max<size_t>(1, materialArgumentEncodedDrawCount_));
            return false;
        }
        return true;
    }

    return false;
}

bool MetalSceneResources::SetActiveMaterial(RendererMaterialHandle handle) {
    if (!handle) {
        return false;
    }

    for (size_t index = 0; index < rendererMaterialRecords_.size(); ++index) {
        const RendererMaterialRecord& record = rendererMaterialRecords_[index];
        if (record.valid && record.material.id == handle.id) {
            const size_t previousActiveMaterial = activeRendererMaterial_;
            activeRendererMaterial_ = index;
            if (materialArgumentBuffer_.IsReady() &&
                !EncodeMaterialArgumentBuffers(std::max<size_t>(1, materialArgumentEncodedDrawCount_))) {
                activeRendererMaterial_ = previousActiveMaterial;
                EncodeMaterialArgumentBuffers(std::max<size_t>(1, materialArgumentEncodedDrawCount_));
                return false;
            }
            return true;
        }
    }

    return false;
}

bool MetalSceneResources::GetMaterialInfo(RendererMaterialHandle handle, RendererMaterialInfo& outInfo) const {
    outInfo = {};
    if (!handle) {
        return false;
    }

    for (size_t index = 0; index < rendererMaterialRecords_.size(); ++index) {
        const RendererMaterialRecord& record = rendererMaterialRecords_[index];
        if (!record.valid || record.material.id != handle.id) {
            continue;
        }

        outInfo.material = record.material;
        outInfo.desc = record.desc;
        outInfo.active = index == activeRendererMaterial_;
        return true;
    }

    return false;
}

RendererMaterialDesc MetalSceneResources::GetActiveMaterialDesc() const {
    if (activeRendererMaterial_ < rendererMaterialRecords_.size()) {
        const RendererMaterialRecord& record = rendererMaterialRecords_[activeRendererMaterial_];
        if (record.valid) {
            return record.desc;
        }
    }

    RendererMaterialDesc desc;
    const size_t baseColorSlot = RendererTextureSlotIndex(RendererTextureSlot::BaseColor);
    const MaterialTextureSlotState& baseColorState = materialTextureSlots_[baseColorSlot];
    if (baseColorState.records[baseColorState.active].valid) {
        desc.SetTexture(RendererTextureSlot::BaseColor,
                        baseColorState.records[baseColorState.active].texture);
    }
    return desc;
}

bool MetalSceneResources::FindMaterialTextureSlot(RendererTextureHandle texture, RendererTextureSlot& outSlot) const {
    if (!texture) {
        return false;
    }

    for (const MaterialTextureSlotState& slotState : materialTextureSlots_) {
        for (const MaterialTextureRecord& record : slotState.records) {
            if (record.valid && record.texture.id == texture.id) {
                outSlot = record.slot;
                return true;
            }
        }
    }

    return false;
}

bool MetalSceneResources::ValidateMaterialTextureBindings(const RendererMaterialDesc& material,
                                                          const char* operationName) const {
    const RendererTextureSlot slots[] = {
        RendererTextureSlot::BaseColor,
        RendererTextureSlot::Normal,
        RendererTextureSlot::MetallicRoughness,
        RendererTextureSlot::Emissive,
        RendererTextureSlot::Occlusion,
    };

    for (RendererTextureSlot slot : slots) {
        const RendererTextureHandle texture = material.GetTexture(slot);
        if (!texture) {
            continue;
        }

        RendererTextureSlot registeredSlot = RendererTextureSlot::BaseColor;
        if (!FindMaterialTextureSlot(texture, registeredSlot)) {
            NEXT_LOG_WARNING("Metal material %s rejected: %s texture handle %llu is not registered",
                             operationName,
                             RendererTextureSlotName(slot),
                             static_cast<unsigned long long>(texture.id));
            return false;
        }
        if (registeredSlot != slot) {
            NEXT_LOG_WARNING("Metal material %s rejected: %s texture handle %llu belongs to %s slot",
                             operationName,
                             RendererTextureSlotName(slot),
                             static_cast<unsigned long long>(texture.id),
                             RendererTextureSlotName(registeredSlot));
            return false;
        }
    }

    return true;
}

RendererMaterialBindingInfo MetalSceneResources::ResolveMaterialBinding(RendererTextureSlot slot,
                                                                        RendererTextureHandle texture) const {
    RendererMaterialBindingInfo binding;
    binding.slot = slot;
    binding.requestedTexture = texture;
    const size_t slotIndex = RendererTextureSlotIndex(slot);
    if (slotIndex < kRendererTextureSlotCount) {
        binding.textureBindingIndex = RendererMaterialTextureBindingIndex(slot);
        binding.samplerBindingIndex = RendererMaterialSamplerBindingIndex(slot);
        binding.uniformBufferIndex = kRendererMaterialUniformBufferIndex;
    }
    if (slotIndex < materialSamplers_.size()) {
        binding.samplerReady = materialSamplers_[slotIndex] != nil;
    }

    auto fillFromMaterialTexture = [&binding](const MaterialTextureRecord& record,
                                              const MetalTexture& texture,
                                              RendererMaterialBindingSource source) {
        const RHI::TextureDesc& desc = texture.GetDesc();
        binding.source = source;
        binding.boundTexture = record.texture;
        binding.format = record.format;
        binding.sourceAssetId = record.sourceAssetId;
        binding.width = desc.extent.width;
        binding.height = desc.extent.height;
        binding.textureReady = texture.IsReady();
    };

    if (slotIndex < materialTextureSlots_.size()) {
        const MaterialTextureSlotState& slotState = materialTextureSlots_[slotIndex];
        if (texture) {
            for (size_t textureIndex = 0; textureIndex < slotState.records.size(); ++textureIndex) {
                const MaterialTextureRecord& record = slotState.records[textureIndex];
                if (record.valid && record.texture.id == texture.id && textureIndex != slotState.pending &&
                    slotState.textures[textureIndex].IsReady()) {
                    fillFromMaterialTexture(record,
                                            slotState.textures[textureIndex],
                                            RendererMaterialBindingSource::MaterialTexture);
                    return binding;
                }
            }
        }

        if (slotState.active < slotState.textures.size() && slotState.textures[slotState.active].IsReady()) {
            const MaterialTextureRecord& record = slotState.records[slotState.active];
            if (record.valid) {
                fillFromMaterialTexture(record,
                                        slotState.textures[slotState.active],
                                        RendererMaterialBindingSource::ActiveSlotTexture);
                return binding;
            }
        }
    }

    const MetalTexture* neutralTexture = nullptr;
    switch (slot) {
        case RendererTextureSlot::Normal:
            neutralTexture = &neutralNormalTexture_;
            break;
        case RendererTextureSlot::MetallicRoughness:
            neutralTexture = &neutralMetallicRoughnessTexture_;
            break;
        case RendererTextureSlot::Emissive:
            neutralTexture = &neutralEmissiveTexture_;
            break;
        case RendererTextureSlot::Occlusion:
            neutralTexture = &neutralOcclusionTexture_;
            break;
        case RendererTextureSlot::BaseColor:
        default:
            break;
    }

    if (neutralTexture && neutralTexture->IsReady()) {
        const RHI::TextureDesc& desc = neutralTexture->GetDesc();
        binding.source = RendererMaterialBindingSource::NeutralTexture;
        binding.format = RendererTextureFormat::RGBA8Unorm;
        binding.width = desc.extent.width;
        binding.height = desc.extent.height;
        binding.textureReady = true;
    }

    return binding;
}

const MetalTexture* MetalSceneResources::ResolveMaterialTexture(RendererTextureSlot slot,
                                                                RendererTextureHandle texture) const {
    const size_t slotIndex = RendererTextureSlotIndex(slot);
    if (slotIndex < materialTextureSlots_.size()) {
        const MaterialTextureSlotState& slotState = materialTextureSlots_[slotIndex];
        if (texture) {
            for (size_t textureIndex = 0; textureIndex < slotState.records.size(); ++textureIndex) {
                const MaterialTextureRecord& record = slotState.records[textureIndex];
                if (record.valid && record.texture.id == texture.id && textureIndex != slotState.pending &&
                    slotState.textures[textureIndex].IsReady()) {
                    return &slotState.textures[textureIndex];
                }
            }
        }

        if (slotState.active < slotState.textures.size() && slotState.textures[slotState.active].IsReady()) {
            return &slotState.textures[slotState.active];
        }
    }

    if (slot == RendererTextureSlot::BaseColor) {
        const size_t baseColorSlot = RendererTextureSlotIndex(RendererTextureSlot::BaseColor);
        const MaterialTextureSlotState& baseColorState = materialTextureSlots_[baseColorSlot];
        if (baseColorState.textures[baseColorState.active].IsReady()) {
            return &baseColorState.textures[baseColorState.active];
        }
    }

    return nullptr;
}

bool MetalSceneResources::ResolveActiveMaterialTextures(
    std::array<const MetalTexture*, kRendererTextureSlotCount>& outTextures) const {
    outTextures = {};
    const RendererMaterialDesc activeMaterial = GetActiveMaterialDesc();
    for (size_t slotIndex = 0; slotIndex < outTextures.size(); ++slotIndex) {
        const RendererTextureSlot slot = static_cast<RendererTextureSlot>(slotIndex);
        const MetalTexture* texture = ResolveMaterialTexture(slot, activeMaterial.GetTexture(slot));
        if (!texture) {
            switch (slot) {
                case RendererTextureSlot::Normal:
                    texture = neutralNormalTexture_.IsReady() ? &neutralNormalTexture_ : nullptr;
                    break;
                case RendererTextureSlot::MetallicRoughness:
                    texture = neutralMetallicRoughnessTexture_.IsReady() ? &neutralMetallicRoughnessTexture_ : nullptr;
                    break;
                case RendererTextureSlot::Emissive:
                    texture = neutralEmissiveTexture_.IsReady() ? &neutralEmissiveTexture_ : nullptr;
                    break;
                case RendererTextureSlot::Occlusion:
                    texture = neutralOcclusionTexture_.IsReady() ? &neutralOcclusionTexture_ : nullptr;
                    break;
                case RendererTextureSlot::BaseColor:
                default:
                    break;
            }
        }

        if (!texture || !texture->IsReady()) {
            return false;
        }
        outTextures[slotIndex] = texture;
    }
    return true;
}

bool MetalSceneResources::EncodeMaterialArgumentBuffers(size_t drawCount) {
    materialArgumentEncodedResourceCount_ = 0;
    materialArgumentEncodedDrawCount_ = 0;
    if (drawCount == 0 || drawCount > materialArgumentBufferDrawCapacity_) {
        return false;
    }

    for (size_t drawIndex = 0; drawIndex < drawCount; ++drawIndex) {
        const size_t uniformOffset = drawIndex * kMetalUniformStride;
        if (!EncodeMaterialArgumentBufferRegion(drawIndex, uniformOffset)) {
            materialArgumentEncodedResourceCount_ = 0;
            materialArgumentEncodedDrawCount_ = 0;
            return false;
        }
    }
    materialArgumentEncodedDrawCount_ = static_cast<uint32_t>(drawCount);
    return true;
}

bool MetalSceneResources::EncodeMaterialArgumentBufferRegion(size_t drawIndex, size_t uniformOffset) {
    if (!materialArgumentEncoder_ || !materialArgumentBuffer_.IsReady() || !uniformBuffer_.IsReady() ||
        materialArgumentEncodedStride_ == 0 || drawIndex >= materialArgumentBufferDrawCapacity_) {
        return false;
    }

    std::array<const MetalTexture*, kRendererTextureSlotCount> materialTextures;
    if (!ResolveActiveMaterialTextures(materialTextures)) {
        return false;
    }

    id<MTLTexture> textureArguments[kRendererTextureSlotCount] = {};
    id<MTLSamplerState> samplerArguments[kRendererTextureSlotCount] = {};
    for (size_t slotIndex = 0; slotIndex < kRendererTextureSlotCount; ++slotIndex) {
        if (slotIndex >= materialSamplers_.size() || materialSamplers_[slotIndex] == nil) {
            return false;
        }
        textureArguments[slotIndex] = materialTextures[slotIndex]->NativeTexture();
        samplerArguments[slotIndex] = materialSamplers_[slotIndex];
    }

    const NSUInteger argumentOffset =
        static_cast<NSUInteger>(drawIndex * static_cast<size_t>(materialArgumentEncodedStride_));
    [materialArgumentEncoder_ setArgumentBuffer:materialArgumentBuffer_.NativeBuffer()
                                         offset:argumentOffset];
    [materialArgumentEncoder_ setBuffer:uniformBuffer_.NativeBuffer()
                                 offset:static_cast<NSUInteger>(uniformOffset)
                                atIndex:shaderLibraryDesc_.materialShaderResourceGroupUniformArgumentIndex];
    [materialArgumentEncoder_ setTextures:textureArguments
                                withRange:NSMakeRange(
                                              shaderLibraryDesc_.materialShaderResourceGroupTextureArgumentBaseIndex,
                                              kRendererTextureSlotCount)];
    [materialArgumentEncoder_ setSamplerStates:samplerArguments
                                     withRange:NSMakeRange(
                                                   shaderLibraryDesc_.materialShaderResourceGroupSamplerArgumentBaseIndex,
                                                   kRendererTextureSlotCount)];
    materialArgumentEncodedResourceCount_ += 1u + static_cast<uint32_t>(kRendererTextureSlotCount * 2u);
    return true;
}

bool MetalSceneResources::BindDrawState(id<MTLRenderCommandEncoder> encoder,
                                        NSUInteger uniformOffset,
                                        bool declareArgumentResources) const {
    if (!encoder || !IsReady()) {
        return false;
    }

    std::array<const MetalTexture*, kRendererTextureSlotCount> materialTextures;
    if (!ResolveActiveMaterialTextures(materialTextures)) {
        return false;
    }
    if (uniformOffset % kMetalUniformStride != 0 || materialArgumentEncodedStride_ == 0) {
        return false;
    }

    const size_t drawIndex = static_cast<size_t>(uniformOffset / kMetalUniformStride);
    if (drawIndex >= materialArgumentEncodedDrawCount_ ||
        drawIndex >= materialArgumentBufferDrawCapacity_) {
        return false;
    }
    const NSUInteger argumentBufferOffset =
        static_cast<NSUInteger>(drawIndex * static_cast<size_t>(materialArgumentEncodedStride_));
    if (shaderLibraryDesc_.materialShaderResourceGroupArgumentBufferIndex ==
        kMetalShaderManifestInvalidBindingIndex) {
        return false;
    }

    renderPipeline_->Bind(encoder);
    [encoder setVertexBuffer:vertexBuffer_.NativeBuffer() offset:0 atIndex:kRendererGeometryVertexBufferIndex];
    [encoder setVertexBuffer:uniformBuffer_.NativeBuffer()
                      offset:uniformOffset
                     atIndex:kRendererMaterialUniformBufferIndex];
    [encoder setFragmentBuffer:materialArgumentBuffer_.NativeBuffer()
                        offset:argumentBufferOffset
                       atIndex:shaderLibraryDesc_.materialShaderResourceGroupArgumentBufferIndex];
    if (declareArgumentResources) {
        [encoder useResource:materialArgumentBuffer_.NativeBuffer()
                       usage:MTLResourceUsageRead
                      stages:MTLRenderStageFragment];
        [encoder useResource:uniformBuffer_.NativeBuffer()
                       usage:MTLResourceUsageRead
                      stages:MTLRenderStageVertex | MTLRenderStageFragment];
        for (size_t slotIndex = 0; slotIndex < materialTextures.size(); ++slotIndex) {
            [encoder useResource:materialTextures[slotIndex]->NativeTexture()
                           usage:MTLResourceUsageRead
                          stages:MTLRenderStageFragment];
        }
    }
    ++materialArgumentBufferBindCount_;
    return true;
}

bool MetalSceneResources::DrawCube(id<MTLRenderCommandEncoder> encoder) const {
    if (!encoder || !indexBuffer_.IsReady()) {
        return false;
    }

    uint64_t indexBufferOffset = 0;
    if (!RHI::ValidateDrawIndexedDesc(cubeDraw_, cubeIndexBufferView_) ||
        !RHI::DrawIndexedIndexBufferOffsetBytes(cubeDraw_, cubeIndexBufferView_, indexBufferOffset)) {
        return false;
    }

    const MTLPrimitiveType primitiveType = renderPipeline_ ? renderPipeline_->PrimitiveType() : MTLPrimitiveTypeTriangle;
    [encoder setStencilReferenceValue:cubeDraw_.stencilReference];
    [encoder setBlendColorRed:cubeDraw_.blendConstant[0]
                         green:cubeDraw_.blendConstant[1]
                          blue:cubeDraw_.blendConstant[2]
                         alpha:cubeDraw_.blendConstant[3]];
    [encoder drawIndexedPrimitives:primitiveType
                        indexCount:cubeDraw_.indexCount
                         indexType:ToMetalIndexType(cubeIndexBufferView_.format)
                       indexBuffer:indexBuffer_.NativeBuffer()
                 indexBufferOffset:static_cast<NSUInteger>(indexBufferOffset)
                      instanceCount:cubeDraw_.instanceCount
                         baseVertex:cubeDraw_.vertexOffset
                       baseInstance:cubeDraw_.instanceOffset];
    return true;
}

} // namespace MetalBackend
} // namespace Next
