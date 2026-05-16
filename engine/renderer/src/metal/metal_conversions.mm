#include "metal_conversions.h"

#include "next/foundation/logger.h"

#include <algorithm>
#include <cmath>

namespace Next {
namespace MetalBackend {

namespace {

float SanitizeMetalSamplerLodMin(float lod) {
    return (std::isfinite(lod) && lod >= 0.0f) ? lod : 0.0f;
}

float SanitizeMetalSamplerLodMax(float lod, float minLod) {
    return (std::isfinite(lod) && lod >= minLod) ? lod : minLod;
}

} // namespace

MTLPixelFormat ToMetalPixelFormat(RHI::Format format) {
    switch (format) {
        case RHI::Format::BGRA8Unorm:   return MTLPixelFormatBGRA8Unorm;
        case RHI::Format::RGBA8Unorm:   return MTLPixelFormatRGBA8Unorm;
        case RHI::Format::Depth32Float: return MTLPixelFormatDepth32Float;
        case RHI::Format::Depth32FloatStencil8:
            return MTLPixelFormatDepth32Float_Stencil8;
        case RHI::Format::Unknown:
        default:                        return MTLPixelFormatInvalid;
    }
}

MTLStorageMode ToMetalStorageMode(RHI::ResourceMemory memory) {
    switch (memory) {
        case RHI::ResourceMemory::DeviceLocal:
            return MTLStorageModePrivate;
        case RHI::ResourceMemory::Shared:
        case RHI::ResourceMemory::Upload:
        case RHI::ResourceMemory::Readback:
        default:
            return MTLStorageModeShared;
    }
}

MTLResourceOptions ToMetalResourceOptions(RHI::ResourceMemory memory) {
    switch (ToMetalStorageMode(memory)) {
        case MTLStorageModePrivate:
            return MTLResourceStorageModePrivate;
        case MTLStorageModeShared:
        default:
            return MTLResourceStorageModeShared;
    }
}

MTLTextureUsage ToMetalTextureUsage(RHI::ResourceUsageFlags usage) {
    MTLTextureUsage metalUsage = MTLTextureUsageUnknown;

    if (RHI::HasResourceUsage(usage, RHI::ResourceUsage::ShaderRead)) {
        metalUsage |= MTLTextureUsageShaderRead;
    }
    if (RHI::HasResourceUsage(usage, RHI::ResourceUsage::ShaderWrite)) {
        metalUsage |= MTLTextureUsageShaderWrite;
    }
    if (RHI::HasResourceUsage(usage, RHI::ResourceUsage::RenderTarget) ||
        RHI::HasResourceUsage(usage, RHI::ResourceUsage::DepthStencil)) {
        metalUsage |= MTLTextureUsageRenderTarget;
    }

    return metalUsage;
}

MTLPrimitiveType ToMetalPrimitiveType(RHI::PrimitiveTopology topology) {
    switch (topology) {
        case RHI::PrimitiveTopology::TriangleList:
            return MTLPrimitiveTypeTriangle;
        case RHI::PrimitiveTopology::TriangleStrip:
            return MTLPrimitiveTypeTriangleStrip;
        case RHI::PrimitiveTopology::LineList:
            return MTLPrimitiveTypeLine;
        case RHI::PrimitiveTopology::PointList:
            return MTLPrimitiveTypePoint;
        default:
            return MTLPrimitiveTypeTriangle;
    }
}

MTLTriangleFillMode ToMetalTriangleFillMode(RHI::FillMode mode) {
    switch (mode) {
        case RHI::FillMode::Solid:
            return MTLTriangleFillModeFill;
        case RHI::FillMode::Wireframe:
            return MTLTriangleFillModeLines;
        default:
            return MTLTriangleFillModeFill;
    }
}

MTLDepthClipMode ToMetalDepthClipMode(bool depthClipEnabled) {
    return depthClipEnabled ? MTLDepthClipModeClip : MTLDepthClipModeClamp;
}

MTLCullMode ToMetalCullMode(RHI::CullMode mode) {
    switch (mode) {
        case RHI::CullMode::None:
            return MTLCullModeNone;
        case RHI::CullMode::Front:
            return MTLCullModeFront;
        case RHI::CullMode::Back:
            return MTLCullModeBack;
        default:
            return MTLCullModeNone;
    }
}

MTLWinding ToMetalWinding(RHI::FrontFaceWinding winding) {
    switch (winding) {
        case RHI::FrontFaceWinding::CounterClockwise:
            return MTLWindingCounterClockwise;
        case RHI::FrontFaceWinding::Clockwise:
            return MTLWindingClockwise;
        default:
            return MTLWindingCounterClockwise;
    }
}

MTLVertexFormat ToMetalVertexFormat(RHI::VertexFormat format) {
    switch (format) {
        case RHI::VertexFormat::Float32:
            return MTLVertexFormatFloat;
        case RHI::VertexFormat::Float32x2:
            return MTLVertexFormatFloat2;
        case RHI::VertexFormat::Float32x3:
            return MTLVertexFormatFloat3;
        case RHI::VertexFormat::Float32x4:
            return MTLVertexFormatFloat4;
        case RHI::VertexFormat::Unknown:
        default:
            return MTLVertexFormatInvalid;
    }
}

MTLVertexStepFunction ToMetalVertexStepFunction(RHI::VertexStepFunction stepFunction) {
    switch (stepFunction) {
        case RHI::VertexStepFunction::PerVertex:
            return MTLVertexStepFunctionPerVertex;
        case RHI::VertexStepFunction::PerInstance:
            return MTLVertexStepFunctionPerInstance;
        default:
            return MTLVertexStepFunctionConstant;
    }
}

MTLIndexType ToMetalIndexType(RHI::IndexFormat format) {
    switch (format) {
        case RHI::IndexFormat::Uint16:
            return MTLIndexTypeUInt16;
        case RHI::IndexFormat::Uint32:
            return MTLIndexTypeUInt32;
        case RHI::IndexFormat::Unknown:
        default:
            return MTLIndexTypeUInt16;
    }
}

MTLViewport ToMetalViewport(const RHI::ViewportDesc& viewport) {
    MTLViewport metalViewport;
    metalViewport.originX = viewport.minX;
    metalViewport.originY = viewport.minY;
    metalViewport.width = viewport.maxX - viewport.minX;
    metalViewport.height = viewport.maxY - viewport.minY;
    metalViewport.znear = viewport.minZ;
    metalViewport.zfar = viewport.maxZ;
    return metalViewport;
}

MTLScissorRect ToMetalScissorRect(const RHI::ScissorRectDesc& scissor) {
    MTLScissorRect metalScissor;
    metalScissor.x = scissor.minX;
    metalScissor.y = scissor.minY;
    metalScissor.width = scissor.maxX - scissor.minX;
    metalScissor.height = scissor.maxY - scissor.minY;
    return metalScissor;
}

MTLLoadAction ToMetalLoadAction(RHI::AttachmentLoadAction action) {
    switch (action) {
        case RHI::AttachmentLoadAction::Load:
            return MTLLoadActionLoad;
        case RHI::AttachmentLoadAction::Clear:
            return MTLLoadActionClear;
        case RHI::AttachmentLoadAction::DontCare:
            return MTLLoadActionDontCare;
        default:
            return MTLLoadActionDontCare;
    }
}

MTLStoreAction ToMetalStoreAction(RHI::AttachmentStoreAction action) {
    switch (action) {
        case RHI::AttachmentStoreAction::Store:
            return MTLStoreActionStore;
        case RHI::AttachmentStoreAction::DontCare:
            return MTLStoreActionDontCare;
        default:
            return MTLStoreActionDontCare;
    }
}

MTLStencilOperation ToMetalStencilOperation(RHI::StencilOperation operation) {
    switch (operation) {
        case RHI::StencilOperation::Keep:
            return MTLStencilOperationKeep;
        case RHI::StencilOperation::Zero:
            return MTLStencilOperationZero;
        case RHI::StencilOperation::Replace:
            return MTLStencilOperationReplace;
        case RHI::StencilOperation::IncrementClamp:
            return MTLStencilOperationIncrementClamp;
        case RHI::StencilOperation::DecrementClamp:
            return MTLStencilOperationDecrementClamp;
        case RHI::StencilOperation::Invert:
            return MTLStencilOperationInvert;
        case RHI::StencilOperation::IncrementWrap:
            return MTLStencilOperationIncrementWrap;
        case RHI::StencilOperation::DecrementWrap:
            return MTLStencilOperationDecrementWrap;
        default:
            return MTLStencilOperationKeep;
    }
}

MTLBlendFactor ToMetalBlendFactor(RHI::BlendFactor factor) {
    switch (factor) {
        case RHI::BlendFactor::Zero:
            return MTLBlendFactorZero;
        case RHI::BlendFactor::One:
            return MTLBlendFactorOne;
        case RHI::BlendFactor::SourceColor:
            return MTLBlendFactorSourceColor;
        case RHI::BlendFactor::OneMinusSourceColor:
            return MTLBlendFactorOneMinusSourceColor;
        case RHI::BlendFactor::SourceAlpha:
            return MTLBlendFactorSourceAlpha;
        case RHI::BlendFactor::OneMinusSourceAlpha:
            return MTLBlendFactorOneMinusSourceAlpha;
        case RHI::BlendFactor::DestinationColor:
            return MTLBlendFactorDestinationColor;
        case RHI::BlendFactor::OneMinusDestinationColor:
            return MTLBlendFactorOneMinusDestinationColor;
        case RHI::BlendFactor::DestinationAlpha:
            return MTLBlendFactorDestinationAlpha;
        case RHI::BlendFactor::OneMinusDestinationAlpha:
            return MTLBlendFactorOneMinusDestinationAlpha;
        case RHI::BlendFactor::SourceAlphaSaturated:
            return MTLBlendFactorSourceAlphaSaturated;
        case RHI::BlendFactor::BlendColor:
            return MTLBlendFactorBlendColor;
        case RHI::BlendFactor::OneMinusBlendColor:
            return MTLBlendFactorOneMinusBlendColor;
        case RHI::BlendFactor::BlendAlpha:
            return MTLBlendFactorBlendAlpha;
        case RHI::BlendFactor::OneMinusBlendAlpha:
            return MTLBlendFactorOneMinusBlendAlpha;
        default:
            return MTLBlendFactorOne;
    }
}

MTLBlendOperation ToMetalBlendOperation(RHI::BlendOperation operation) {
    switch (operation) {
        case RHI::BlendOperation::Add:
            return MTLBlendOperationAdd;
        case RHI::BlendOperation::Subtract:
            return MTLBlendOperationSubtract;
        case RHI::BlendOperation::ReverseSubtract:
            return MTLBlendOperationReverseSubtract;
        case RHI::BlendOperation::Min:
            return MTLBlendOperationMin;
        case RHI::BlendOperation::Max:
            return MTLBlendOperationMax;
        default:
            return MTLBlendOperationAdd;
    }
}

MTLColorWriteMask ToMetalColorWriteMask(RHI::ColorWriteMaskFlags mask) {
    if (mask == RHI::ColorWriteMaskFlag(RHI::ColorWriteMask::All)) {
        return MTLColorWriteMaskAll;
    }

    MTLColorWriteMask metalMask = MTLColorWriteMaskNone;
    if (RHI::HasColorWriteMask(mask, RHI::ColorWriteMask::Red)) {
        metalMask = static_cast<MTLColorWriteMask>(metalMask | MTLColorWriteMaskRed);
    }
    if (RHI::HasColorWriteMask(mask, RHI::ColorWriteMask::Green)) {
        metalMask = static_cast<MTLColorWriteMask>(metalMask | MTLColorWriteMaskGreen);
    }
    if (RHI::HasColorWriteMask(mask, RHI::ColorWriteMask::Blue)) {
        metalMask = static_cast<MTLColorWriteMask>(metalMask | MTLColorWriteMaskBlue);
    }
    if (RHI::HasColorWriteMask(mask, RHI::ColorWriteMask::Alpha)) {
        metalMask = static_cast<MTLColorWriteMask>(metalMask | MTLColorWriteMaskAlpha);
    }
    return metalMask;
}

MTLSamplerMinMagFilter ToMetalSamplerFilter(RHI::SamplerFilter filter) {
    switch (filter) {
        case RHI::SamplerFilter::Nearest:
            return MTLSamplerMinMagFilterNearest;
        case RHI::SamplerFilter::Linear:
            return MTLSamplerMinMagFilterLinear;
        default:
            return MTLSamplerMinMagFilterNearest;
    }
}

MTLSamplerMipFilter ToMetalSamplerMipFilter(RHI::SamplerMipFilter filter) {
    switch (filter) {
        case RHI::SamplerMipFilter::NotMipmapped:
            return MTLSamplerMipFilterNotMipmapped;
        case RHI::SamplerMipFilter::Nearest:
            return MTLSamplerMipFilterNearest;
        case RHI::SamplerMipFilter::Linear:
            return MTLSamplerMipFilterLinear;
        default:
            return MTLSamplerMipFilterNotMipmapped;
    }
}

MTLSamplerAddressMode ToMetalSamplerAddressMode(RHI::SamplerAddressMode addressMode) {
    switch (addressMode) {
        case RHI::SamplerAddressMode::Repeat:
            return MTLSamplerAddressModeRepeat;
        case RHI::SamplerAddressMode::ClampToEdge:
            return MTLSamplerAddressModeClampToEdge;
        case RHI::SamplerAddressMode::MirrorRepeat:
            return MTLSamplerAddressModeMirrorRepeat;
        case RHI::SamplerAddressMode::ClampToBorder:
            return MTLSamplerAddressModeClampToBorderColor;
        default:
            return MTLSamplerAddressModeRepeat;
    }
}

MTLSamplerBorderColor ToMetalSamplerBorderColor(RHI::SamplerBorderColor color) {
    switch (color) {
        case RHI::SamplerBorderColor::OpaqueBlack:
            return MTLSamplerBorderColorOpaqueBlack;
        case RHI::SamplerBorderColor::TransparentBlack:
            return MTLSamplerBorderColorTransparentBlack;
        case RHI::SamplerBorderColor::OpaqueWhite:
            return MTLSamplerBorderColorOpaqueWhite;
        default:
            return MTLSamplerBorderColorOpaqueBlack;
    }
}

MTLCompareFunction ToMetalCompareFunction(RHI::CompareFunction function) {
    switch (function) {
        case RHI::CompareFunction::Never:
            return MTLCompareFunctionNever;
        case RHI::CompareFunction::Less:
            return MTLCompareFunctionLess;
        case RHI::CompareFunction::Equal:
            return MTLCompareFunctionEqual;
        case RHI::CompareFunction::LessEqual:
            return MTLCompareFunctionLessEqual;
        case RHI::CompareFunction::Greater:
            return MTLCompareFunctionGreater;
        case RHI::CompareFunction::NotEqual:
            return MTLCompareFunctionNotEqual;
        case RHI::CompareFunction::GreaterEqual:
            return MTLCompareFunctionGreaterEqual;
        case RHI::CompareFunction::Always:
            return MTLCompareFunctionAlways;
        default:
            return MTLCompareFunctionNever;
    }
}

MTLDataType ToMetalArgumentDataType(RHI::ShaderResourceBindingType type) {
    switch (type) {
        case RHI::ShaderResourceBindingType::ConstantBuffer:
        case RHI::ShaderResourceBindingType::ReadOnlyBuffer:
        case RHI::ShaderResourceBindingType::ReadWriteBuffer:
            return MTLDataTypePointer;
        case RHI::ShaderResourceBindingType::Texture:
        case RHI::ShaderResourceBindingType::StorageTexture:
            return MTLDataTypeTexture;
        case RHI::ShaderResourceBindingType::Sampler:
            return MTLDataTypeSampler;
        default:
            return MTLDataTypeNone;
    }
}

MTLBindingAccess ToMetalArgumentAccess(RHI::ShaderResourceBindingType type) {
    switch (type) {
        case RHI::ShaderResourceBindingType::ReadWriteBuffer:
        case RHI::ShaderResourceBindingType::StorageTexture:
            return MTLBindingAccessReadWrite;
        case RHI::ShaderResourceBindingType::ConstantBuffer:
        case RHI::ShaderResourceBindingType::ReadOnlyBuffer:
        case RHI::ShaderResourceBindingType::Texture:
        case RHI::ShaderResourceBindingType::Sampler:
        default:
            return MTLBindingAccessReadOnly;
    }
}

void ConfigureMetalColorAttachmentDescriptor(const RHI::ColorAttachmentDesc& desc,
                                             MTLRenderPipelineColorAttachmentDescriptor* colorDesc) {
    if (!colorDesc) {
        return;
    }

    colorDesc.pixelFormat = ToMetalPixelFormat(desc.format);
    colorDesc.writeMask = ToMetalColorWriteMask(desc.writeMask);
    colorDesc.blendingEnabled = desc.blendEnabled ? YES : NO;
    colorDesc.sourceRGBBlendFactor = ToMetalBlendFactor(desc.sourceColorBlendFactor);
    colorDesc.destinationRGBBlendFactor = ToMetalBlendFactor(desc.destinationColorBlendFactor);
    colorDesc.rgbBlendOperation = ToMetalBlendOperation(desc.colorBlendOperation);
    colorDesc.sourceAlphaBlendFactor = ToMetalBlendFactor(desc.sourceAlphaBlendFactor);
    colorDesc.destinationAlphaBlendFactor = ToMetalBlendFactor(desc.destinationAlphaBlendFactor);
    colorDesc.alphaBlendOperation = ToMetalBlendOperation(desc.alphaBlendOperation);
}

void ConfigureMetalVertexDescriptor(const RHI::VertexInputStateDesc& desc,
                                    MTLVertexDescriptor* vertexDesc) {
    if (!vertexDesc) {
        return;
    }

    for (uint32_t i = 0; i < desc.attributeCount; ++i) {
        const RHI::VertexAttributeDesc& attribute = desc.attributes[i];
        vertexDesc.attributes[attribute.location].offset = attribute.offset;
        vertexDesc.attributes[attribute.location].format = ToMetalVertexFormat(attribute.format);
        vertexDesc.attributes[attribute.location].bufferIndex = attribute.bufferIndex;
    }

    for (uint32_t i = 0; i < desc.bufferCount; ++i) {
        const RHI::VertexBufferLayoutDesc& buffer = desc.buffers[i];
        vertexDesc.layouts[i].stride = buffer.stride;
        vertexDesc.layouts[i].stepFunction = ToMetalVertexStepFunction(buffer.stepFunction);
        vertexDesc.layouts[i].stepRate = buffer.stepRate;
    }
}

void ConfigureMetalMultisampleDescriptor(const RHI::MultisampleStateDesc& desc,
                                         MTLRenderPipelineDescriptor* pipelineDesc) {
    if (!pipelineDesc) {
        return;
    }

#if defined(__IPHONE_16_0) || defined(__MAC_13_0)
    pipelineDesc.rasterSampleCount = desc.sampleCount;
#else
    pipelineDesc.sampleCount = desc.sampleCount;
#endif
    pipelineDesc.alphaToCoverageEnabled = desc.alphaToCoverageEnabled ? YES : NO;
}

void ConfigureMetalStencilDescriptor(const RHI::StencilFaceStateDesc& face,
                                     uint8_t readMask,
                                     uint8_t writeMask,
                                     MTLStencilDescriptor* stencilDesc) {
    if (!stencilDesc) {
        return;
    }

    stencilDesc.readMask = readMask;
    stencilDesc.writeMask = writeMask;
    stencilDesc.stencilCompareFunction = ToMetalCompareFunction(face.compare);
    stencilDesc.stencilFailureOperation = ToMetalStencilOperation(face.stencilFailOp);
    stencilDesc.depthFailureOperation = ToMetalStencilOperation(face.depthFailOp);
    stencilDesc.depthStencilPassOperation = ToMetalStencilOperation(face.passOp);
}

void ConfigureMetalDepthStencilDescriptor(const RHI::DepthStencilStateDesc& desc,
                                          MTLDepthStencilDescriptor* depthStencilDesc) {
    if (!depthStencilDesc) {
        return;
    }

    depthStencilDesc.depthCompareFunction = desc.depthTestEnabled
        ? ToMetalCompareFunction(desc.depthCompare)
        : MTLCompareFunctionAlways;
    depthStencilDesc.depthWriteEnabled = desc.depthTestEnabled && desc.depthWriteEnabled ? YES : NO;

    if (desc.stencilTestEnabled) {
        if (!depthStencilDesc.frontFaceStencil) {
            depthStencilDesc.frontFaceStencil = [[MTLStencilDescriptor alloc] init];
        }
        if (!depthStencilDesc.backFaceStencil) {
            depthStencilDesc.backFaceStencil = [[MTLStencilDescriptor alloc] init];
        }
        ConfigureMetalStencilDescriptor(desc.frontStencil,
                                        desc.stencilReadMask,
                                        desc.stencilWriteMask,
                                        depthStencilDesc.frontFaceStencil);
        ConfigureMetalStencilDescriptor(desc.backStencil,
                                        desc.stencilReadMask,
                                        desc.stencilWriteMask,
                                        depthStencilDesc.backFaceStencil);
    }
}

void ConfigureMetalSamplerDescriptor(const RHI::SamplerDesc& desc, MTLSamplerDescriptor* samplerDesc) {
    if (!samplerDesc) {
        return;
    }

    samplerDesc.minFilter = ToMetalSamplerFilter(desc.minFilter);
    samplerDesc.magFilter = ToMetalSamplerFilter(desc.magFilter);
    samplerDesc.mipFilter = ToMetalSamplerMipFilter(desc.mipFilter);
    samplerDesc.sAddressMode = ToMetalSamplerAddressMode(desc.addressU);
    samplerDesc.tAddressMode = ToMetalSamplerAddressMode(desc.addressV);
    samplerDesc.rAddressMode = ToMetalSamplerAddressMode(desc.addressW);
    samplerDesc.borderColor = ToMetalSamplerBorderColor(desc.borderColor);
    samplerDesc.compareFunction = ToMetalCompareFunction(desc.compareFunction);
    samplerDesc.maxAnisotropy = desc.anisotropyEnabled
        ? std::clamp<NSUInteger>(desc.maxAnisotropy, 1, 16)
        : 1;
    const float lodMinClamp = SanitizeMetalSamplerLodMin(desc.minLod);
    samplerDesc.lodMinClamp = lodMinClamp;
    samplerDesc.lodMaxClamp = SanitizeMetalSamplerLodMax(desc.maxLod, lodMinClamp);
    if (desc.mipLodBias != 0.0f) {
#if defined(__MAC_OS_X_VERSION_MAX_ALLOWED) && __MAC_OS_X_VERSION_MAX_ALLOWED >= 260000
        if (@available(macOS 26.0, *)) {
            samplerDesc.lodBias = desc.mipLodBias;
        } else
#endif
        {
            NEXT_LOG_WARNING("Metal sampler mip LOD bias %.3f ignored: requires macOS 26.0",
                             desc.mipLodBias);
        }
    }
    if (desc.debugName) {
        samplerDesc.label = [NSString stringWithUTF8String:desc.debugName];
    }
}

bool ConfigureMetalArgumentDescriptor(const RHI::ShaderResourceBindingDesc& desc,
                                      uint32_t argumentIndex,
                                      MTLArgumentDescriptor* argumentDesc) {
    if (!argumentDesc || !desc.HasShaderStages() || !desc.HasBindingRange()) {
        return false;
    }

    const MTLDataType dataType = ToMetalArgumentDataType(desc.type);
    if (dataType == MTLDataTypeNone) {
        return false;
    }

    argumentDesc.dataType = dataType;
    argumentDesc.index = argumentIndex;
    argumentDesc.arrayLength = desc.bindingCount;
    argumentDesc.access = ToMetalArgumentAccess(desc.type);
    if (dataType == MTLDataTypeTexture) {
        argumentDesc.textureType = MTLTextureType2D;
    }

    return true;
}

NSArray<MTLArgumentDescriptor*>* MakeMetalArgumentDescriptors(
    const RHI::ShaderResourceGroupLayoutDesc& layout) {
    if (!RHI::ValidateShaderResourceGroupLayoutDesc(layout)) {
        return nil;
    }

    NSMutableArray<MTLArgumentDescriptor*>* argumentDescs =
        [NSMutableArray arrayWithCapacity:layout.bindingCount];
    uint32_t argumentIndex = 0;
    for (uint32_t i = 0; i < layout.bindingCount; ++i) {
        MTLArgumentDescriptor* argumentDesc = [MTLArgumentDescriptor argumentDescriptor];
        if (!ConfigureMetalArgumentDescriptor(layout.bindings[i], argumentIndex, argumentDesc)) {
            return nil;
        }
        [argumentDescs addObject:argumentDesc];
        argumentIndex += layout.bindings[i].bindingCount;
    }

    return argumentDescs;
}

} // namespace MetalBackend
} // namespace Next
