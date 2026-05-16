#pragma once

#include "metal_device.h"
#include "metal_gpu_resource.h"
#include "next/rhi/resource.h"

#include <cstddef>
#include <cstdint>

#import <Metal/Metal.h>

namespace Next {
namespace MetalBackend {

class MetalBufferPool final {
public:
    bool Initialize(MetalDevice& device);
    void Shutdown();

    bool CreateBuffer(MetalBuffer& buffer,
                      const RHI::BufferDesc& desc,
                      uint64_t submittedFrameIndex = 0);
    void ReleaseBuffer(MetalBuffer& buffer, uint64_t submittedFrameIndex = 0);

    size_t GetLiveBufferCount() const { return stats_.liveResourceCount; }
    uint64_t GetLiveBytes() const { return stats_.liveBytes; }
    RHI::ResourcePoolStats GetStats() const { return stats_; }
    bool SetMemoryBudget(RHI::ResourceMemory memory, uint64_t budgetBytes) {
        return RHI::SetResourcePoolMemoryBudget(stats_, memory, budgetBytes);
    }

private:
    MetalDevice* device_ = nullptr;
    RHI::ResourcePoolStats stats_{RHI::ResourceType::Buffer};
};

class MetalTexturePool final {
public:
    bool Initialize(MetalDevice& device);
    void Shutdown();

    bool CreateTexture(MetalTexture& texture,
                       const RHI::TextureDesc& desc,
                       uint64_t submittedFrameIndex = 0);
    void ReleaseTexture(MetalTexture& texture, uint64_t submittedFrameIndex = 0);

    size_t GetLiveTextureCount() const { return stats_.liveResourceCount; }
    uint64_t GetLiveBytes() const { return stats_.liveBytes; }
    RHI::ResourcePoolStats GetStats() const { return stats_; }
    bool SetMemoryBudget(RHI::ResourceMemory memory, uint64_t budgetBytes) {
        return RHI::SetResourcePoolMemoryBudget(stats_, memory, budgetBytes);
    }

private:
    MetalDevice* device_ = nullptr;
    RHI::ResourcePoolStats stats_{RHI::ResourceType::Texture};
};

} // namespace MetalBackend
} // namespace Next
