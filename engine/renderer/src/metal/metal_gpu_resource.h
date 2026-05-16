#pragma once

#include "metal_device.h"
#include "next/rhi/resource.h"

#include <cstdint>

#import <Metal/Metal.h>

namespace Next {
namespace MetalBackend {

class MetalBuffer final : public RHI::Resource {
public:
    MetalBuffer() = default;
    ~MetalBuffer() override;

    MetalBuffer(const MetalBuffer&) = delete;
    MetalBuffer& operator=(const MetalBuffer&) = delete;

    bool Initialize(MetalDevice& device, const RHI::BufferDesc& desc);
    void Shutdown(MetalDevice* device = nullptr, uint64_t submittedFrameIndex = 0);

    RHI::ResourceType GetResourceType() const override { return RHI::ResourceType::Buffer; }
    const char* GetDebugName() const override { return debugName_; }
    RHI::ResourceUsageFlags GetUsageFlags() const override { return desc_.usage; }
    RHI::ResourceState GetCurrentState() const override { return currentState_; }
    void SetCurrentState(RHI::ResourceState state) override { currentState_ = state; }
    const RHI::BufferDesc& GetDesc() const { return desc_; }

    bool IsReady() const { return buffer_ != nil; }
    id<MTLBuffer> NativeBuffer() const { return buffer_; }
    void* Contents() const;

private:
    RHI::BufferDesc desc_;
    RHI::ResourceState currentState_ = RHI::ResourceState::Undefined;
    id<MTLBuffer> buffer_ = nil;
    char debugName_[128] = {};
};

class MetalTexture final : public RHI::Resource {
public:
    MetalTexture() = default;
    ~MetalTexture() override;

    MetalTexture(const MetalTexture&) = delete;
    MetalTexture& operator=(const MetalTexture&) = delete;

    bool Initialize(MetalDevice& device, const RHI::TextureDesc& desc);
    void Shutdown(MetalDevice* device = nullptr, uint64_t submittedFrameIndex = 0);

    RHI::ResourceType GetResourceType() const override { return RHI::ResourceType::Texture; }
    const char* GetDebugName() const override { return debugName_; }
    RHI::ResourceUsageFlags GetUsageFlags() const override { return desc_.usage; }
    RHI::ResourceState GetCurrentState() const override { return currentState_; }
    void SetCurrentState(RHI::ResourceState state) override { currentState_ = state; }
    const RHI::TextureDesc& GetDesc() const { return desc_; }

    bool IsReady() const { return texture_ != nil; }
    id<MTLTexture> NativeTexture() const { return texture_; }
    MTLTextureUsage Usage() const { return usage_; }

private:
    RHI::TextureDesc desc_;
    RHI::ResourceState currentState_ = RHI::ResourceState::Undefined;
    id<MTLTexture> texture_ = nil;
    MTLTextureUsage usage_ = MTLTextureUsageUnknown;
    char debugName_[128] = {};
};

} // namespace MetalBackend
} // namespace Next
