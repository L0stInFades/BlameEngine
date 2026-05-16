#include "metal_render_pass.h"

#include "metal_conversions.h"
#include "next/foundation/logger.h"

namespace Next {
namespace MetalBackend {
namespace {

MetalRenderPassBuildResult BuildError(MetalRenderPassBuildError error, uint32_t attachmentIndex) {
    MetalRenderPassBuildResult result;
    result.error = error;
    result.attachmentIndex = attachmentIndex;
    return result;
}

struct AttachmentShape {
    NSUInteger width = 0;
    NSUInteger height = 0;
    NSUInteger sampleCount = 0;
    uint32_t attachmentIndex = 0;
    bool initialized = false;
};

MTLClearColor ToMetalClearColor(const RHI::ClearColor& color) {
    return MTLClearColorMake(color.r, color.g, color.b, color.a);
}

bool TextureHasRenderTargetUsage(id<MTLTexture> texture) {
    return texture && (texture.usage & MTLTextureUsageRenderTarget) != 0;
}

MetalRenderPassBuildResult ValidateTextureForAttachment(id<MTLTexture> texture,
                                                        RHI::Format expectedFormat,
                                                        uint32_t attachmentIndex) {
    const MTLPixelFormat expectedPixelFormat = ToMetalPixelFormat(expectedFormat);
    if (texture.pixelFormat != expectedPixelFormat) {
        NEXT_LOG_ERROR("Metal render pass descriptor rejected: texture format mismatch "
                       "(attachment=%u expected=%lu actual=%lu)",
                       attachmentIndex,
                       static_cast<unsigned long>(expectedPixelFormat),
                       static_cast<unsigned long>(texture.pixelFormat));
        return BuildError(MetalRenderPassBuildError::TextureFormatMismatch, attachmentIndex);
    }
    if (!TextureHasRenderTargetUsage(texture)) {
        NEXT_LOG_ERROR("Metal render pass descriptor rejected: texture missing render-target usage "
                       "(attachment=%u usage=%lu)",
                       attachmentIndex,
                       static_cast<unsigned long>(texture.usage));
        return BuildError(MetalRenderPassBuildError::TextureMissingRenderTargetUsage, attachmentIndex);
    }
    return {};
}

MetalRenderPassBuildResult ValidateAttachmentShape(id<MTLTexture> texture,
                                                   uint32_t attachmentIndex,
                                                   AttachmentShape& shape) {
    if (!shape.initialized) {
        shape.width = texture.width;
        shape.height = texture.height;
        shape.sampleCount = texture.sampleCount;
        shape.attachmentIndex = attachmentIndex;
        shape.initialized = true;
        return {};
    }

    if (texture.width != shape.width || texture.height != shape.height) {
        NEXT_LOG_ERROR("Metal render pass descriptor rejected: texture size mismatch "
                       "(attachment=%u expected=%lux%lu from attachment=%u actual=%lux%lu)",
                       attachmentIndex,
                       static_cast<unsigned long>(shape.width),
                       static_cast<unsigned long>(shape.height),
                       shape.attachmentIndex,
                       static_cast<unsigned long>(texture.width),
                       static_cast<unsigned long>(texture.height));
        return BuildError(MetalRenderPassBuildError::TextureSizeMismatch, attachmentIndex);
    }
    if (texture.sampleCount != shape.sampleCount) {
        NEXT_LOG_ERROR("Metal render pass descriptor rejected: texture sample count mismatch "
                       "(attachment=%u expected=%lu from attachment=%u actual=%lu)",
                       attachmentIndex,
                       static_cast<unsigned long>(shape.sampleCount),
                       shape.attachmentIndex,
                       static_cast<unsigned long>(texture.sampleCount));
        return BuildError(MetalRenderPassBuildError::TextureSampleCountMismatch, attachmentIndex);
    }
    return {};
}

MetalRenderPassBuildResult ValidateResolveTexture(id<MTLTexture> sourceTexture,
                                                  id<MTLTexture> resolveTexture,
                                                  RHI::Format expectedFormat,
                                                  uint32_t attachmentIndex) {
    MetalRenderPassBuildResult textureValidation =
        ValidateTextureForAttachment(resolveTexture, expectedFormat, attachmentIndex);
    if (textureValidation.error != MetalRenderPassBuildError::None) {
        return textureValidation;
    }
    if (sourceTexture.width != resolveTexture.width || sourceTexture.height != resolveTexture.height) {
        NEXT_LOG_ERROR("Metal render pass descriptor rejected: resolve texture size mismatch "
                       "(attachment=%u source=%lux%lu resolve=%lux%lu)",
                       attachmentIndex,
                       static_cast<unsigned long>(sourceTexture.width),
                       static_cast<unsigned long>(sourceTexture.height),
                       static_cast<unsigned long>(resolveTexture.width),
                       static_cast<unsigned long>(resolveTexture.height));
        return BuildError(MetalRenderPassBuildError::TextureSizeMismatch, attachmentIndex);
    }
    if (sourceTexture.sampleCount <= 1) {
        NEXT_LOG_ERROR("Metal render pass descriptor rejected: resolve source is not multisampled "
                       "(attachment=%u sourceSamples=%lu)",
                       attachmentIndex,
                       static_cast<unsigned long>(sourceTexture.sampleCount));
        return BuildError(MetalRenderPassBuildError::ResolveSourceSampleCountInvalid,
                          attachmentIndex);
    }
    if (resolveTexture.sampleCount != 1) {
        NEXT_LOG_ERROR("Metal render pass descriptor rejected: resolve texture must be single-sampled "
                       "(attachment=%u resolveSamples=%lu)",
                       attachmentIndex,
                       static_cast<unsigned long>(resolveTexture.sampleCount));
        return BuildError(MetalRenderPassBuildError::ResolveTextureSampleCountInvalid,
                          attachmentIndex);
    }
    return {};
}

MTLStoreAction ToMetalResolveStoreAction(RHI::AttachmentStoreAction action) {
    switch (action) {
        case RHI::AttachmentStoreAction::Store:
            return MTLStoreActionStoreAndMultisampleResolve;
        case RHI::AttachmentStoreAction::DontCare:
        default:
            return MTLStoreActionMultisampleResolve;
    }
}

} // namespace

const char* MetalRenderPassBuildErrorName(MetalRenderPassBuildError error) {
    switch (error) {
        case MetalRenderPassBuildError::None:
            return "none";
        case MetalRenderPassBuildError::InvalidDescriptor:
            return "invalid_descriptor";
        case MetalRenderPassBuildError::MissingColorTexture:
            return "missing_color_texture";
        case MetalRenderPassBuildError::MissingDepthStencilTexture:
            return "missing_depth_stencil_texture";
        case MetalRenderPassBuildError::TextureFormatMismatch:
            return "texture_format_mismatch";
        case MetalRenderPassBuildError::TextureMissingRenderTargetUsage:
            return "texture_missing_render_target_usage";
        case MetalRenderPassBuildError::TextureSizeMismatch:
            return "texture_size_mismatch";
        case MetalRenderPassBuildError::TextureSampleCountMismatch:
            return "texture_sample_count_mismatch";
        case MetalRenderPassBuildError::ResolveSourceSampleCountInvalid:
            return "resolve_source_sample_count_invalid";
        case MetalRenderPassBuildError::ResolveTextureSampleCountInvalid:
            return "resolve_texture_sample_count_invalid";
        default:
            return "unknown";
    }
}

MetalRenderPassBuildResult BuildMetalRenderPassDescriptor(const RHI::RenderPassDesc& desc,
                                                          const MetalRenderPassAttachments& attachments) {
    const RHI::RenderPassDescriptorValidation validation = RHI::ValidateRenderPassDesc(desc);
    if (!validation) {
        MetalRenderPassBuildResult result = BuildError(MetalRenderPassBuildError::InvalidDescriptor,
                                                       validation.attachmentIndex);
        result.validation = validation;
        NEXT_LOG_ERROR("Metal render pass descriptor rejected: %s (attachment=%u format=%s)",
                       RHI::RenderPassDescriptorErrorName(validation.error),
                       validation.attachmentIndex,
                       RHI::FormatName(validation.format));
        return result;
    }

    AttachmentShape attachmentShape;
    for (uint32_t i = 0; i < desc.colorAttachmentCount; ++i) {
        if (!attachments.colorTextures[i]) {
            NEXT_LOG_ERROR("Metal render pass descriptor rejected: missing color texture %u", i);
            return BuildError(MetalRenderPassBuildError::MissingColorTexture, i);
        }
        MetalRenderPassBuildResult textureValidation =
            ValidateTextureForAttachment(attachments.colorTextures[i],
                                         desc.colorAttachments[i].format,
                                         i);
        if (textureValidation.error != MetalRenderPassBuildError::None) {
            textureValidation.validation = validation;
            return textureValidation;
        }
        MetalRenderPassBuildResult shapeValidation =
            ValidateAttachmentShape(attachments.colorTextures[i], i, attachmentShape);
        if (shapeValidation.error != MetalRenderPassBuildError::None) {
            shapeValidation.validation = validation;
            return shapeValidation;
        }
        if (attachments.colorResolveTextures[i]) {
            MetalRenderPassBuildResult resolveValidation =
                ValidateResolveTexture(attachments.colorTextures[i],
                                       attachments.colorResolveTextures[i],
                                       desc.colorAttachments[i].format,
                                       i);
            if (resolveValidation.error != MetalRenderPassBuildError::None) {
                resolveValidation.validation = validation;
                return resolveValidation;
            }
        }
    }
    if (desc.hasDepthStencil && !attachments.depthStencilTexture) {
        NEXT_LOG_ERROR("Metal render pass descriptor rejected: missing depth-stencil texture");
        return BuildError(MetalRenderPassBuildError::MissingDepthStencilTexture,
                          desc.colorAttachmentCount);
    }
    if (desc.hasDepthStencil) {
        MetalRenderPassBuildResult textureValidation =
            ValidateTextureForAttachment(attachments.depthStencilTexture,
                                         desc.depthStencilAttachment.format,
                                         desc.colorAttachmentCount);
        if (textureValidation.error != MetalRenderPassBuildError::None) {
            textureValidation.validation = validation;
            return textureValidation;
        }
        MetalRenderPassBuildResult shapeValidation =
            ValidateAttachmentShape(attachments.depthStencilTexture,
                                    desc.colorAttachmentCount,
                                    attachmentShape);
        if (shapeValidation.error != MetalRenderPassBuildError::None) {
            shapeValidation.validation = validation;
            return shapeValidation;
        }
        if (attachments.depthStencilResolveTexture) {
            MetalRenderPassBuildResult resolveValidation =
                ValidateResolveTexture(attachments.depthStencilTexture,
                                       attachments.depthStencilResolveTexture,
                                       desc.depthStencilAttachment.format,
                                       desc.colorAttachmentCount);
            if (resolveValidation.error != MetalRenderPassBuildError::None) {
                resolveValidation.validation = validation;
                return resolveValidation;
            }
        }
    }

    @autoreleasepool {
        MTLRenderPassDescriptor* passDescriptor = [MTLRenderPassDescriptor renderPassDescriptor];
        for (uint32_t i = 0; i < desc.colorAttachmentCount; ++i) {
            const RHI::RenderPassColorAttachmentDesc& attachment = desc.colorAttachments[i];
            MTLRenderPassColorAttachmentDescriptor* metalAttachment = passDescriptor.colorAttachments[i];
            metalAttachment.texture = attachments.colorTextures[i];
            metalAttachment.loadAction = ToMetalLoadAction(attachment.loadAction);
            if (attachments.colorResolveTextures[i]) {
                metalAttachment.resolveTexture = attachments.colorResolveTextures[i];
                metalAttachment.storeAction = ToMetalResolveStoreAction(attachment.storeAction);
            } else {
                metalAttachment.storeAction = ToMetalStoreAction(attachment.storeAction);
            }
            if (attachment.loadAction == RHI::AttachmentLoadAction::Clear) {
                metalAttachment.clearColor = ToMetalClearColor(attachment.clearColor);
            }
        }

        if (desc.hasDepthStencil) {
            const RHI::RenderPassDepthStencilAttachmentDesc& attachment = desc.depthStencilAttachment;
            if (RHI::FormatHasDepth(attachment.format)) {
                MTLRenderPassDepthAttachmentDescriptor* metalAttachment = passDescriptor.depthAttachment;
                metalAttachment.texture = attachments.depthStencilTexture;
                metalAttachment.loadAction = ToMetalLoadAction(attachment.loadAction);
                if (attachments.depthStencilResolveTexture) {
                    metalAttachment.resolveTexture = attachments.depthStencilResolveTexture;
                    metalAttachment.storeAction = ToMetalResolveStoreAction(attachment.storeAction);
                } else {
                    metalAttachment.storeAction = ToMetalStoreAction(attachment.storeAction);
                }
                if (attachment.loadAction == RHI::AttachmentLoadAction::Clear) {
                    metalAttachment.clearDepth = attachment.clearDepth;
                }
            }
            if (RHI::FormatHasStencil(attachment.format)) {
                MTLRenderPassStencilAttachmentDescriptor* metalAttachment = passDescriptor.stencilAttachment;
                metalAttachment.texture = attachments.depthStencilTexture;
                metalAttachment.loadAction = ToMetalLoadAction(attachment.stencilLoadAction);
                if (attachments.depthStencilResolveTexture) {
                    metalAttachment.resolveTexture = attachments.depthStencilResolveTexture;
                    metalAttachment.storeAction = ToMetalResolveStoreAction(attachment.stencilStoreAction);
                } else {
                    metalAttachment.storeAction = ToMetalStoreAction(attachment.stencilStoreAction);
                }
                if (attachment.stencilLoadAction == RHI::AttachmentLoadAction::Clear) {
                    metalAttachment.clearStencil = attachment.clearStencil;
                }
            }
        }

        MetalRenderPassBuildResult result;
        result.descriptor = passDescriptor;
        result.validation = validation;
        return result;
    }
}

} // namespace MetalBackend
} // namespace Next
