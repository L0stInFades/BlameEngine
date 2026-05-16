#include "metal_resource_pool.h"

#include "next/foundation/logger.h"

#include <algorithm>
#include <limits>

namespace Next {
namespace MetalBackend {
bool MetalBufferPool::Initialize(MetalDevice& device) {
    Shutdown();

    if (!device.NativeDevice()) {
        NEXT_LOG_ERROR("Cannot initialize Metal buffer pool without a device");
        return false;
    }

    device_ = &device;
    return true;
}

void MetalBufferPool::Shutdown() {
    if (stats_.liveResourceCount != 0) {
        NEXT_LOG_WARNING("Metal buffer pool shutdown with %zu live buffers (%llu bytes tracked)",
                         stats_.liveResourceCount,
                         static_cast<unsigned long long>(stats_.liveBytes));
    }

    device_ = nullptr;
    stats_ = {RHI::ResourceType::Buffer};
}

bool MetalBufferPool::CreateBuffer(MetalBuffer& buffer,
                                   const RHI::BufferDesc& desc,
                                   uint64_t submittedFrameIndex) {
    if (!device_) {
        NEXT_LOG_ERROR("Cannot create Metal buffer without an initialized buffer pool");
        return false;
    }

    const RHI::ResourceDescriptorValidation validation = RHI::ValidateBufferDesc(desc);
    if (!validation) {
        NEXT_LOG_ERROR("Metal buffer descriptor rejected: %s (size=%llu initial=%s)",
                       RHI::ResourceDescriptorErrorName(validation.error),
                       static_cast<unsigned long long>(desc.sizeBytes),
                       RHI::ResourceStateName(desc.initialState));
        return false;
    }

    if (desc.sizeBytes > static_cast<uint64_t>(std::numeric_limits<NSUInteger>::max())) {
        NEXT_LOG_ERROR("Metal buffer descriptor rejected: size %llu exceeds platform limit",
                       static_cast<unsigned long long>(desc.sizeBytes));
        return false;
    }

    RHI::ResourcePoolStats allocationStats = stats_;
    if (buffer.IsReady()) {
        const RHI::BufferDesc& oldDesc = buffer.GetDesc();
        RHI::RecordResourcePoolRelease(allocationStats, oldDesc.memory, oldDesc.sizeBytes);
    }

    if (!RHI::ResourcePoolCanAllocate(allocationStats, desc.memory, desc.sizeBytes)) {
        RHI::RecordResourcePoolAllocationFailure(stats_, desc.memory, desc.sizeBytes);
        const RHI::ResourcePoolMemoryStats* memoryStats =
            RHI::FindResourcePoolMemoryStats(allocationStats, desc.memory);
        NEXT_LOG_ERROR("Metal buffer descriptor rejected: %s memory budget exceeded by %llu bytes "
                       "(live=%llu budget=%llu)",
                       RHI::ResourceMemoryName(desc.memory),
                       static_cast<unsigned long long>(desc.sizeBytes),
                       static_cast<unsigned long long>(memoryStats ? memoryStats->liveBytes : 0),
                       static_cast<unsigned long long>(memoryStats ? memoryStats->budgetBytes : 0));
        return false;
    }

    if (buffer.IsReady()) {
        ReleaseBuffer(buffer, submittedFrameIndex);
    }

    if (!buffer.Initialize(*device_, desc)) {
        RHI::RecordResourcePoolAllocationFailure(stats_, desc.memory, desc.sizeBytes);
        return false;
    }

    RHI::RecordResourcePoolAllocation(stats_, desc.memory, desc.sizeBytes);
    return true;
}

void MetalBufferPool::ReleaseBuffer(MetalBuffer& buffer, uint64_t submittedFrameIndex) {
    if (!buffer.IsReady()) {
        return;
    }

    const uint64_t sizeBytes = buffer.GetDesc().sizeBytes;
    const RHI::ResourceMemory memory = buffer.GetDesc().memory;
    buffer.Shutdown(device_, submittedFrameIndex);
    RHI::RecordResourcePoolRelease(stats_, memory, sizeBytes);
}

bool MetalTexturePool::Initialize(MetalDevice& device) {
    Shutdown();

    if (!device.NativeDevice()) {
        NEXT_LOG_ERROR("Cannot initialize Metal texture pool without a device");
        return false;
    }

    device_ = &device;
    return true;
}

void MetalTexturePool::Shutdown() {
    if (stats_.liveResourceCount != 0) {
        NEXT_LOG_WARNING("Metal texture pool shutdown with %zu live textures (%llu bytes tracked)",
                         stats_.liveResourceCount,
                         static_cast<unsigned long long>(stats_.liveBytes));
    }

    device_ = nullptr;
    stats_ = {RHI::ResourceType::Texture};
}

bool MetalTexturePool::CreateTexture(MetalTexture& texture,
                                     const RHI::TextureDesc& desc,
                                     uint64_t submittedFrameIndex) {
    if (!device_) {
        NEXT_LOG_ERROR("Cannot create Metal texture without an initialized texture pool");
        return false;
    }

    const RHI::ResourceDescriptorValidation validation = RHI::ValidateTextureDesc(desc);
    if (!validation) {
        NEXT_LOG_ERROR("Metal texture descriptor rejected: %s (%ux%u %s initial=%s)",
                       RHI::ResourceDescriptorErrorName(validation.error),
                       desc.extent.width,
                       desc.extent.height,
                       RHI::FormatName(desc.format),
                       RHI::ResourceStateName(desc.initialState));
        return false;
    }

    uint64_t sizeBytes = 0;
    RHI::TextureDescEstimatedBytes(desc, sizeBytes);
    RHI::ResourcePoolStats allocationStats = stats_;
    if (texture.IsReady()) {
        uint64_t oldSizeBytes = 0;
        RHI::TextureDescEstimatedBytes(texture.GetDesc(), oldSizeBytes);
        RHI::RecordResourcePoolRelease(allocationStats, texture.GetDesc().memory, oldSizeBytes);
    }

    if (!RHI::ResourcePoolCanAllocate(allocationStats, desc.memory, sizeBytes)) {
        RHI::RecordResourcePoolAllocationFailure(stats_, desc.memory, sizeBytes);
        const RHI::ResourcePoolMemoryStats* memoryStats =
            RHI::FindResourcePoolMemoryStats(allocationStats, desc.memory);
        NEXT_LOG_ERROR("Metal texture descriptor rejected: %s memory budget exceeded by %llu bytes "
                       "(live=%llu budget=%llu)",
                       RHI::ResourceMemoryName(desc.memory),
                       static_cast<unsigned long long>(sizeBytes),
                       static_cast<unsigned long long>(memoryStats ? memoryStats->liveBytes : 0),
                       static_cast<unsigned long long>(memoryStats ? memoryStats->budgetBytes : 0));
        return false;
    }

    if (texture.IsReady()) {
        ReleaseTexture(texture, submittedFrameIndex);
    }

    if (!texture.Initialize(*device_, desc)) {
        RHI::RecordResourcePoolAllocationFailure(stats_, desc.memory, sizeBytes);
        return false;
    }

    RHI::RecordResourcePoolAllocation(stats_, desc.memory, sizeBytes);
    return true;
}

void MetalTexturePool::ReleaseTexture(MetalTexture& texture, uint64_t submittedFrameIndex) {
    if (!texture.IsReady()) {
        return;
    }

    uint64_t sizeBytes = 0;
    RHI::TextureDescEstimatedBytes(texture.GetDesc(), sizeBytes);
    const RHI::ResourceMemory memory = texture.GetDesc().memory;
    texture.Shutdown(device_, submittedFrameIndex);
    RHI::RecordResourcePoolRelease(stats_, memory, sizeBytes);
}

} // namespace MetalBackend
} // namespace Next
