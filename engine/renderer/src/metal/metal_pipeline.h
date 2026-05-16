#pragma once

#include "metal_device.h"
#include "next/rhi/pipeline.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#import <Metal/Metal.h>

namespace Next {
namespace MetalBackend {

class MetalRenderPipeline final {
public:
    bool Initialize(MetalDevice& device, id<MTLLibrary> library, const RHI::GraphicsPipelineDesc& desc);
    void Shutdown(MetalDevice* device = nullptr, uint64_t submittedFrameIndex = 0);

    bool IsReady() const { return pipelineState_ && depthState_; }
    void Bind(id<MTLRenderCommandEncoder> encoder) const;
    MTLPrimitiveType PrimitiveType() const;
    const RHI::GraphicsPipelineDesc& GetDesc() const { return desc_; }
    id<MTLRenderPipelineState> NativePipelineState() const { return pipelineState_; }
    id<MTLDepthStencilState> NativeDepthStencilState() const { return depthState_; }

private:
    RHI::GraphicsPipelineDesc desc_;
    id<MTLRenderPipelineState> pipelineState_ = nil;
    id<MTLDepthStencilState> depthState_ = nil;
};

struct MetalPipelineCacheStats {
    size_t cachedPipelineCount = 0;
    uint64_t requestCount = 0;
    uint64_t hitCount = 0;
    uint64_t missCount = 0;
    uint64_t failedCreateCount = 0;

    bool HasCachedPipelines() const { return cachedPipelineCount != 0; }
};

class MetalPipelineCache final {
public:
    const MetalRenderPipeline* FindOrCreate(MetalDevice& device,
                                            id<MTLLibrary> library,
                                            const RHI::GraphicsPipelineDesc& desc);
    void Shutdown(MetalDevice* device = nullptr, uint64_t submittedFrameIndex = 0);
    MetalPipelineCacheStats GetStats() const;

private:
    struct Entry {
        size_t hash = 0;
        std::unique_ptr<MetalRenderPipeline> pipeline;
    };

    std::vector<Entry> entries_;
    MetalPipelineCacheStats stats_;
};

} // namespace MetalBackend
} // namespace Next
