#pragma once

#include "next/rhi/render_pass.h"

#include <array>
#include <cstdint>

#import <Metal/Metal.h>

namespace Next {
namespace MetalBackend {

enum class MetalRenderPassBuildError : uint8_t {
    None = 0,
    InvalidDescriptor,
    MissingColorTexture,
    MissingDepthStencilTexture,
    TextureFormatMismatch,
    TextureMissingRenderTargetUsage,
    TextureSizeMismatch,
    TextureSampleCountMismatch,
    ResolveSourceSampleCountInvalid,
    ResolveTextureSampleCountInvalid,
};

struct MetalRenderPassAttachments {
    std::array<id<MTLTexture>, RHI::kMaxRenderPassColorAttachments> colorTextures{};
    std::array<id<MTLTexture>, RHI::kMaxRenderPassColorAttachments> colorResolveTextures{};
    id<MTLTexture> depthStencilTexture = nil;
    id<MTLTexture> depthStencilResolveTexture = nil;
};

struct MetalRenderPassBuildResult {
    MTLRenderPassDescriptor* descriptor = nil;
    RHI::RenderPassDescriptorValidation validation;
    MetalRenderPassBuildError error = MetalRenderPassBuildError::None;
    uint32_t attachmentIndex = 0;

    explicit operator bool() const {
        return descriptor != nil && validation && error == MetalRenderPassBuildError::None;
    }
};

const char* MetalRenderPassBuildErrorName(MetalRenderPassBuildError error);
MetalRenderPassBuildResult BuildMetalRenderPassDescriptor(const RHI::RenderPassDesc& desc,
                                                          const MetalRenderPassAttachments& attachments);

} // namespace MetalBackend
} // namespace Next
