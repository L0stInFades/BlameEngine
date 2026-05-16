#include "metal_pipeline.h"

#include "metal_conversions.h"
#include "next/foundation/logger.h"

#include <utility>

namespace Next {
namespace MetalBackend {

namespace {

NSString* MakePipelineLabel(const char* debugName, const char* fallback) {
    const char* label = (debugName && debugName[0] != '\0') ? debugName : fallback;
    NSString* metalLabel = label ? [NSString stringWithUTF8String:label] : nil;
    if (!metalLabel && fallback) {
        metalLabel = [NSString stringWithUTF8String:fallback];
    }
    return metalLabel;
}

NSString* MakeDepthStencilLabel(const char* debugName) {
    NSString* baseLabel = MakePipelineLabel(debugName, "NEXT Metal render pipeline");
    return baseLabel ? [baseLabel stringByAppendingString:@" depth-stencil"] : nil;
}

NSString* MakeShaderEntryPointName(const char* entryPoint, const char* stageName) {
    NSString* name = entryPoint ? [NSString stringWithUTF8String:entryPoint] : nil;
    if (!name) {
        NEXT_LOG_ERROR("Metal %s shader entry point is not valid UTF-8", stageName);
    }
    return name;
}

} // namespace

bool MetalRenderPipeline::Initialize(MetalDevice& device,
                                     id<MTLLibrary> library,
                                     const RHI::GraphicsPipelineDesc& desc) {
    if (!device.NativeDevice() || !library) {
        return false;
    }

    const RHI::PipelineDescriptorValidation validation = RHI::ValidateGraphicsPipelineDesc(desc);
    if (!validation) {
        NEXT_LOG_ERROR("Invalid Metal graphics pipeline descriptor: %s "
                       "(colorIndex=%u vertexBufferIndex=%u vertexAttributeIndex=%u format=%s stage=%s)",
                       RHI::PipelineDescriptorErrorName(validation.error),
                       validation.colorAttachmentIndex,
                       validation.vertexBufferIndex,
                       validation.vertexAttributeIndex,
                       RHI::FormatName(validation.format),
                       RHI::ShaderStageName(validation.shaderStage));
        return false;
    }

    @autoreleasepool {
        Shutdown();

        NSString* vertexEntryPoint = MakeShaderEntryPointName(desc.vertexShader.entryPoint, "vertex");
        NSString* fragmentEntryPoint = MakeShaderEntryPointName(desc.fragmentShader.entryPoint, "fragment");
        if (!vertexEntryPoint || !fragmentEntryPoint) {
            return false;
        }
        id<MTLFunction> vertexFunction = [library newFunctionWithName:vertexEntryPoint];
        id<MTLFunction> fragmentFunction = [library newFunctionWithName:fragmentEntryPoint];
        if (!vertexFunction || !fragmentFunction) {
            NEXT_LOG_ERROR("Failed to resolve Metal shader entry points: vertex=%s fragment=%s",
                           desc.vertexShader.entryPoint,
                           desc.fragmentShader.entryPoint);
            return false;
        }

        MTLRenderPipelineDescriptor* pipelineDesc = [[MTLRenderPipelineDescriptor alloc] init];
        pipelineDesc.label = MakePipelineLabel(desc.debugName, "NEXT Metal render pipeline");
        pipelineDesc.vertexFunction = vertexFunction;
        pipelineDesc.fragmentFunction = fragmentFunction;
        if (desc.vertexInput.bufferCount > 0 || desc.vertexInput.attributeCount > 0) {
            MTLVertexDescriptor* vertexDesc = [[MTLVertexDescriptor alloc] init];
            ConfigureMetalVertexDescriptor(desc.vertexInput, vertexDesc);
            pipelineDesc.vertexDescriptor = vertexDesc;
        }
        for (uint32_t i = 0; i < desc.colorAttachmentCount; ++i) {
            ConfigureMetalColorAttachmentDescriptor(desc.colorAttachments[i],
                                                    pipelineDesc.colorAttachments[i]);
        }
        ConfigureMetalMultisampleDescriptor(desc.multisampleState, pipelineDesc);
        const MTLPixelFormat depthStencilPixelFormat = ToMetalPixelFormat(desc.depthStencilFormat);
        if (RHI::FormatHasDepth(desc.depthStencilFormat)) {
            pipelineDesc.depthAttachmentPixelFormat = depthStencilPixelFormat;
        }
        if (RHI::FormatHasStencil(desc.depthStencilFormat)) {
            pipelineDesc.stencilAttachmentPixelFormat = depthStencilPixelFormat;
        }

        NSError* pipelineError = nil;
        pipelineState_ = [device.NativeDevice() newRenderPipelineStateWithDescriptor:pipelineDesc
                                                                               error:&pipelineError];
        if (!pipelineState_) {
            NEXT_LOG_ERROR("Failed to create Metal render pipeline: %s",
                           pipelineError ? [[pipelineError localizedDescription] UTF8String] : "unknown error");
            return false;
        }

        MTLDepthStencilDescriptor* depthStencilDesc = [[MTLDepthStencilDescriptor alloc] init];
        depthStencilDesc.label = MakeDepthStencilLabel(desc.debugName);
        ConfigureMetalDepthStencilDescriptor(desc.depthStencilState, depthStencilDesc);
        depthState_ = [device.NativeDevice() newDepthStencilStateWithDescriptor:depthStencilDesc];
        if (!depthState_) {
            NEXT_LOG_ERROR("Failed to create Metal depth state");
            Shutdown();
            return false;
        }

        desc_ = desc;
        return true;
    }
}

void MetalRenderPipeline::Shutdown(MetalDevice* device, uint64_t submittedFrameIndex) {
    if (device && depthState_) {
        device->QueueForRelease(depthState_, submittedFrameIndex);
    }
    if (device && pipelineState_) {
        device->QueueForRelease(pipelineState_, submittedFrameIndex);
    }

    depthState_ = nil;
    pipelineState_ = nil;
    desc_ = {};
}

void MetalRenderPipeline::Bind(id<MTLRenderCommandEncoder> encoder) const {
    if (!encoder || !IsReady()) {
        return;
    }

    [encoder setRenderPipelineState:pipelineState_];
    [encoder setDepthStencilState:depthState_];
    [encoder setTriangleFillMode:ToMetalTriangleFillMode(desc_.rasterState.fillMode)];
    [encoder setCullMode:ToMetalCullMode(desc_.rasterState.cullMode)];
    [encoder setFrontFacingWinding:ToMetalWinding(desc_.rasterState.frontFace)];
    [encoder setDepthBias:static_cast<float>(desc_.rasterState.depthBias)
               slopeScale:desc_.rasterState.depthBiasSlopeScale
                    clamp:desc_.rasterState.depthBiasClamp];
    [encoder setDepthClipMode:ToMetalDepthClipMode(desc_.rasterState.depthClipEnabled)];
}

MTLPrimitiveType MetalRenderPipeline::PrimitiveType() const {
    return ToMetalPrimitiveType(desc_.rasterState.primitiveTopology);
}

const MetalRenderPipeline* MetalPipelineCache::FindOrCreate(MetalDevice& device,
                                                            id<MTLLibrary> library,
                                                            const RHI::GraphicsPipelineDesc& desc) {
    ++stats_.requestCount;

    const size_t hash = RHI::GraphicsPipelineDescHash(desc);
    for (const Entry& entry : entries_) {
        if (entry.hash == hash &&
            entry.pipeline &&
            RHI::GraphicsPipelineDescEquals(entry.pipeline->GetDesc(), desc)) {
            ++stats_.hitCount;
            return entry.pipeline.get();
        }
    }

    ++stats_.missCount;
    std::unique_ptr<MetalRenderPipeline> pipeline = std::make_unique<MetalRenderPipeline>();
    if (!pipeline->Initialize(device, library, desc)) {
        ++stats_.failedCreateCount;
        return nullptr;
    }

    MetalRenderPipeline* result = pipeline.get();
    entries_.push_back(Entry{hash, std::move(pipeline)});
    stats_.cachedPipelineCount = entries_.size();
    return result;
}

void MetalPipelineCache::Shutdown(MetalDevice* device, uint64_t submittedFrameIndex) {
    for (Entry& entry : entries_) {
        if (entry.pipeline) {
            entry.pipeline->Shutdown(device, submittedFrameIndex);
        }
    }
    entries_.clear();
    stats_.cachedPipelineCount = 0;
}

MetalPipelineCacheStats MetalPipelineCache::GetStats() const {
    MetalPipelineCacheStats stats = stats_;
    stats.cachedPipelineCount = entries_.size();
    return stats;
}

} // namespace MetalBackend
} // namespace Next
