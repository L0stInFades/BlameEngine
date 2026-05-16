#include "metal_conversions.h"

#include <gtest/gtest.h>

#include <limits>

namespace Next {
namespace MetalBackend {
namespace testing {
namespace {

TEST(MetalConversionsTest, MapsFormatsMemoryAndTextureUsage) {
    EXPECT_EQ(ToMetalPixelFormat(RHI::Format::BGRA8Unorm), MTLPixelFormatBGRA8Unorm);
    EXPECT_EQ(ToMetalPixelFormat(RHI::Format::RGBA8Unorm), MTLPixelFormatRGBA8Unorm);
    EXPECT_EQ(ToMetalPixelFormat(RHI::Format::Depth32Float), MTLPixelFormatDepth32Float);
    EXPECT_EQ(ToMetalPixelFormat(RHI::Format::Depth32FloatStencil8),
              MTLPixelFormatDepth32Float_Stencil8);
    EXPECT_EQ(ToMetalPixelFormat(RHI::Format::Unknown), MTLPixelFormatInvalid);

    EXPECT_EQ(ToMetalStorageMode(RHI::ResourceMemory::DeviceLocal), MTLStorageModePrivate);
    EXPECT_EQ(ToMetalStorageMode(RHI::ResourceMemory::Shared), MTLStorageModeShared);
    EXPECT_EQ(ToMetalStorageMode(RHI::ResourceMemory::Upload), MTLStorageModeShared);
    EXPECT_EQ(ToMetalStorageMode(RHI::ResourceMemory::Readback), MTLStorageModeShared);
    EXPECT_EQ(ToMetalResourceOptions(RHI::ResourceMemory::DeviceLocal), MTLResourceStorageModePrivate);
    EXPECT_EQ(ToMetalResourceOptions(RHI::ResourceMemory::Shared), MTLResourceStorageModeShared);

    EXPECT_EQ(ToMetalTextureUsage(RHI::ResourceUsageFlag(RHI::ResourceUsage::None)), MTLTextureUsageUnknown);
    MTLTextureUsage usage = ToMetalTextureUsage(RHI::ResourceUsage::ShaderRead |
                                                RHI::ResourceUsage::ShaderWrite |
                                                RHI::ResourceUsage::RenderTarget);
    EXPECT_NE(usage & MTLTextureUsageShaderRead, 0u);
    EXPECT_NE(usage & MTLTextureUsageShaderWrite, 0u);
    EXPECT_NE(usage & MTLTextureUsageRenderTarget, 0u);

    usage = ToMetalTextureUsage(RHI::ResourceUsageFlag(RHI::ResourceUsage::DepthStencil));
    EXPECT_EQ(usage, MTLTextureUsageRenderTarget);
}

TEST(MetalConversionsTest, MapsPipelineRasterAndCompareEnums) {
    EXPECT_EQ(ToMetalPrimitiveType(RHI::PrimitiveTopology::TriangleList), MTLPrimitiveTypeTriangle);
    EXPECT_EQ(ToMetalPrimitiveType(RHI::PrimitiveTopology::TriangleStrip), MTLPrimitiveTypeTriangleStrip);
    EXPECT_EQ(ToMetalPrimitiveType(RHI::PrimitiveTopology::LineList), MTLPrimitiveTypeLine);
    EXPECT_EQ(ToMetalPrimitiveType(RHI::PrimitiveTopology::PointList), MTLPrimitiveTypePoint);
    EXPECT_EQ(ToMetalTriangleFillMode(RHI::FillMode::Solid), MTLTriangleFillModeFill);
    EXPECT_EQ(ToMetalTriangleFillMode(RHI::FillMode::Wireframe), MTLTriangleFillModeLines);
    EXPECT_EQ(ToMetalDepthClipMode(true), MTLDepthClipModeClip);
    EXPECT_EQ(ToMetalDepthClipMode(false), MTLDepthClipModeClamp);

    EXPECT_EQ(ToMetalCullMode(RHI::CullMode::None), MTLCullModeNone);
    EXPECT_EQ(ToMetalCullMode(RHI::CullMode::Front), MTLCullModeFront);
    EXPECT_EQ(ToMetalCullMode(RHI::CullMode::Back), MTLCullModeBack);
    EXPECT_EQ(ToMetalWinding(RHI::FrontFaceWinding::CounterClockwise), MTLWindingCounterClockwise);
    EXPECT_EQ(ToMetalWinding(RHI::FrontFaceWinding::Clockwise), MTLWindingClockwise);

    EXPECT_EQ(ToMetalCompareFunction(RHI::CompareFunction::Never), MTLCompareFunctionNever);
    EXPECT_EQ(ToMetalCompareFunction(RHI::CompareFunction::Less), MTLCompareFunctionLess);
    EXPECT_EQ(ToMetalCompareFunction(RHI::CompareFunction::Equal), MTLCompareFunctionEqual);
    EXPECT_EQ(ToMetalCompareFunction(RHI::CompareFunction::LessEqual), MTLCompareFunctionLessEqual);
    EXPECT_EQ(ToMetalCompareFunction(RHI::CompareFunction::Greater), MTLCompareFunctionGreater);
    EXPECT_EQ(ToMetalCompareFunction(RHI::CompareFunction::NotEqual), MTLCompareFunctionNotEqual);
    EXPECT_EQ(ToMetalCompareFunction(RHI::CompareFunction::GreaterEqual), MTLCompareFunctionGreaterEqual);
    EXPECT_EQ(ToMetalCompareFunction(RHI::CompareFunction::Always), MTLCompareFunctionAlways);

    EXPECT_EQ(ToMetalStencilOperation(RHI::StencilOperation::Keep), MTLStencilOperationKeep);
    EXPECT_EQ(ToMetalStencilOperation(RHI::StencilOperation::Zero), MTLStencilOperationZero);
    EXPECT_EQ(ToMetalStencilOperation(RHI::StencilOperation::Replace), MTLStencilOperationReplace);
    EXPECT_EQ(ToMetalStencilOperation(RHI::StencilOperation::IncrementClamp),
              MTLStencilOperationIncrementClamp);
    EXPECT_EQ(ToMetalStencilOperation(RHI::StencilOperation::DecrementClamp),
              MTLStencilOperationDecrementClamp);
    EXPECT_EQ(ToMetalStencilOperation(RHI::StencilOperation::Invert), MTLStencilOperationInvert);
    EXPECT_EQ(ToMetalStencilOperation(RHI::StencilOperation::IncrementWrap),
              MTLStencilOperationIncrementWrap);
    EXPECT_EQ(ToMetalStencilOperation(RHI::StencilOperation::DecrementWrap),
              MTLStencilOperationDecrementWrap);
}

TEST(MetalConversionsTest, MapsViewportAndScissorRects) {
    RHI::ViewportDesc viewport;
    viewport.minX = 10.0;
    viewport.minY = 20.0;
    viewport.maxX = 210.0;
    viewport.maxY = 120.0;
    viewport.minZ = 0.25;
    viewport.maxZ = 0.75;

    const MTLViewport metalViewport = ToMetalViewport(viewport);
    EXPECT_DOUBLE_EQ(metalViewport.originX, 10.0);
    EXPECT_DOUBLE_EQ(metalViewport.originY, 20.0);
    EXPECT_DOUBLE_EQ(metalViewport.width, 200.0);
    EXPECT_DOUBLE_EQ(metalViewport.height, 100.0);
    EXPECT_DOUBLE_EQ(metalViewport.znear, 0.25);
    EXPECT_DOUBLE_EQ(metalViewport.zfar, 0.75);

    const RHI::ScissorRectDesc scissor{4, 8, 132, 72};
    const MTLScissorRect metalScissor = ToMetalScissorRect(scissor);
    EXPECT_EQ(metalScissor.x, 4u);
    EXPECT_EQ(metalScissor.y, 8u);
    EXPECT_EQ(metalScissor.width, 128u);
    EXPECT_EQ(metalScissor.height, 64u);
}

TEST(MetalConversionsTest, MapsVertexInputDescriptorFields) {
    EXPECT_EQ(ToMetalVertexFormat(RHI::VertexFormat::Unknown), MTLVertexFormatInvalid);
    EXPECT_EQ(ToMetalVertexFormat(RHI::VertexFormat::Float32), MTLVertexFormatFloat);
    EXPECT_EQ(ToMetalVertexFormat(RHI::VertexFormat::Float32x2), MTLVertexFormatFloat2);
    EXPECT_EQ(ToMetalVertexFormat(RHI::VertexFormat::Float32x3), MTLVertexFormatFloat3);
    EXPECT_EQ(ToMetalVertexFormat(RHI::VertexFormat::Float32x4), MTLVertexFormatFloat4);
    EXPECT_EQ(ToMetalVertexStepFunction(RHI::VertexStepFunction::PerVertex),
              MTLVertexStepFunctionPerVertex);
    EXPECT_EQ(ToMetalVertexStepFunction(RHI::VertexStepFunction::PerInstance),
              MTLVertexStepFunctionPerInstance);
    EXPECT_EQ(ToMetalIndexType(RHI::IndexFormat::Uint16), MTLIndexTypeUInt16);
    EXPECT_EQ(ToMetalIndexType(RHI::IndexFormat::Uint32), MTLIndexTypeUInt32);

    @autoreleasepool {
        RHI::VertexInputStateDesc desc;
        desc.bufferCount = 2;
        desc.buffers[0].stride = 24;
        desc.buffers[0].stepFunction = RHI::VertexStepFunction::PerVertex;
        desc.buffers[0].stepRate = 1;
        desc.buffers[1].stride = 64;
        desc.buffers[1].stepFunction = RHI::VertexStepFunction::PerInstance;
        desc.buffers[1].stepRate = 2;
        desc.attributeCount = 2;
        desc.attributes[0].location = 0;
        desc.attributes[0].bufferIndex = 0;
        desc.attributes[0].format = RHI::VertexFormat::Float32x2;
        desc.attributes[0].offset = 0;
        desc.attributes[1].location = 3;
        desc.attributes[1].bufferIndex = 1;
        desc.attributes[1].format = RHI::VertexFormat::Float32x4;
        desc.attributes[1].offset = 16;

        MTLVertexDescriptor* vertexDesc = [[MTLVertexDescriptor alloc] init];
        ConfigureMetalVertexDescriptor(desc, vertexDesc);

        EXPECT_EQ(vertexDesc.attributes[0].format, MTLVertexFormatFloat2);
        EXPECT_EQ(vertexDesc.attributes[0].offset, 0u);
        EXPECT_EQ(vertexDesc.attributes[0].bufferIndex, 0u);
        EXPECT_EQ(vertexDesc.attributes[3].format, MTLVertexFormatFloat4);
        EXPECT_EQ(vertexDesc.attributes[3].offset, 16u);
        EXPECT_EQ(vertexDesc.attributes[3].bufferIndex, 1u);
        EXPECT_EQ(vertexDesc.layouts[0].stride, 24u);
        EXPECT_EQ(vertexDesc.layouts[0].stepFunction, MTLVertexStepFunctionPerVertex);
        EXPECT_EQ(vertexDesc.layouts[0].stepRate, 1u);
        EXPECT_EQ(vertexDesc.layouts[1].stride, 64u);
        EXPECT_EQ(vertexDesc.layouts[1].stepFunction, MTLVertexStepFunctionPerInstance);
        EXPECT_EQ(vertexDesc.layouts[1].stepRate, 2u);

        ConfigureMetalVertexDescriptor(desc, nil);
    }
}

TEST(MetalConversionsTest, MapsBlendStateFields) {
    EXPECT_EQ(ToMetalBlendFactor(RHI::BlendFactor::Zero), MTLBlendFactorZero);
    EXPECT_EQ(ToMetalBlendFactor(RHI::BlendFactor::One), MTLBlendFactorOne);
    EXPECT_EQ(ToMetalBlendFactor(RHI::BlendFactor::SourceColor), MTLBlendFactorSourceColor);
    EXPECT_EQ(ToMetalBlendFactor(RHI::BlendFactor::OneMinusSourceColor),
              MTLBlendFactorOneMinusSourceColor);
    EXPECT_EQ(ToMetalBlendFactor(RHI::BlendFactor::SourceAlpha), MTLBlendFactorSourceAlpha);
    EXPECT_EQ(ToMetalBlendFactor(RHI::BlendFactor::OneMinusSourceAlpha),
              MTLBlendFactorOneMinusSourceAlpha);
    EXPECT_EQ(ToMetalBlendFactor(RHI::BlendFactor::DestinationColor),
              MTLBlendFactorDestinationColor);
    EXPECT_EQ(ToMetalBlendFactor(RHI::BlendFactor::OneMinusDestinationColor),
              MTLBlendFactorOneMinusDestinationColor);
    EXPECT_EQ(ToMetalBlendFactor(RHI::BlendFactor::DestinationAlpha),
              MTLBlendFactorDestinationAlpha);
    EXPECT_EQ(ToMetalBlendFactor(RHI::BlendFactor::OneMinusDestinationAlpha),
              MTLBlendFactorOneMinusDestinationAlpha);
    EXPECT_EQ(ToMetalBlendFactor(RHI::BlendFactor::SourceAlphaSaturated),
              MTLBlendFactorSourceAlphaSaturated);
    EXPECT_EQ(ToMetalBlendFactor(RHI::BlendFactor::BlendColor), MTLBlendFactorBlendColor);
    EXPECT_EQ(ToMetalBlendFactor(RHI::BlendFactor::OneMinusBlendColor),
              MTLBlendFactorOneMinusBlendColor);
    EXPECT_EQ(ToMetalBlendFactor(RHI::BlendFactor::BlendAlpha), MTLBlendFactorBlendAlpha);
    EXPECT_EQ(ToMetalBlendFactor(RHI::BlendFactor::OneMinusBlendAlpha),
              MTLBlendFactorOneMinusBlendAlpha);

    EXPECT_EQ(ToMetalBlendOperation(RHI::BlendOperation::Add), MTLBlendOperationAdd);
    EXPECT_EQ(ToMetalBlendOperation(RHI::BlendOperation::Subtract), MTLBlendOperationSubtract);
    EXPECT_EQ(ToMetalBlendOperation(RHI::BlendOperation::ReverseSubtract),
              MTLBlendOperationReverseSubtract);
    EXPECT_EQ(ToMetalBlendOperation(RHI::BlendOperation::Min), MTLBlendOperationMin);
    EXPECT_EQ(ToMetalBlendOperation(RHI::BlendOperation::Max), MTLBlendOperationMax);

    EXPECT_EQ(ToMetalColorWriteMask(RHI::ColorWriteMaskFlag(RHI::ColorWriteMask::None)),
              MTLColorWriteMaskNone);
    EXPECT_EQ(ToMetalColorWriteMask(RHI::ColorWriteMaskFlag(RHI::ColorWriteMask::All)),
              MTLColorWriteMaskAll);
    const RHI::ColorWriteMaskFlags writeMask =
        RHI::ColorWriteMask::Red | RHI::ColorWriteMask::Blue | RHI::ColorWriteMask::Alpha;
    const MTLColorWriteMask expectedWriteMask = static_cast<MTLColorWriteMask>(
        MTLColorWriteMaskRed | MTLColorWriteMaskBlue | MTLColorWriteMaskAlpha);
    EXPECT_EQ(ToMetalColorWriteMask(writeMask), expectedWriteMask);

    @autoreleasepool {
        RHI::ColorAttachmentDesc desc;
        desc.format = RHI::Format::RGBA8Unorm;
        desc.blendEnabled = true;
        desc.sourceColorBlendFactor = RHI::BlendFactor::SourceAlpha;
        desc.destinationColorBlendFactor = RHI::BlendFactor::OneMinusSourceAlpha;
        desc.colorBlendOperation = RHI::BlendOperation::ReverseSubtract;
        desc.sourceAlphaBlendFactor = RHI::BlendFactor::One;
        desc.destinationAlphaBlendFactor = RHI::BlendFactor::DestinationAlpha;
        desc.alphaBlendOperation = RHI::BlendOperation::Max;
        desc.writeMask = writeMask;

        MTLRenderPipelineDescriptor* pipelineDesc = [[MTLRenderPipelineDescriptor alloc] init];
        ConfigureMetalColorAttachmentDescriptor(desc, pipelineDesc.colorAttachments[0]);

        EXPECT_EQ(pipelineDesc.colorAttachments[0].pixelFormat, MTLPixelFormatRGBA8Unorm);
        EXPECT_TRUE(pipelineDesc.colorAttachments[0].blendingEnabled);
        EXPECT_EQ(pipelineDesc.colorAttachments[0].sourceRGBBlendFactor, MTLBlendFactorSourceAlpha);
        EXPECT_EQ(pipelineDesc.colorAttachments[0].destinationRGBBlendFactor,
                  MTLBlendFactorOneMinusSourceAlpha);
        EXPECT_EQ(pipelineDesc.colorAttachments[0].rgbBlendOperation,
                  MTLBlendOperationReverseSubtract);
        EXPECT_EQ(pipelineDesc.colorAttachments[0].sourceAlphaBlendFactor, MTLBlendFactorOne);
        EXPECT_EQ(pipelineDesc.colorAttachments[0].destinationAlphaBlendFactor,
                  MTLBlendFactorDestinationAlpha);
        EXPECT_EQ(pipelineDesc.colorAttachments[0].alphaBlendOperation, MTLBlendOperationMax);
        EXPECT_EQ(pipelineDesc.colorAttachments[0].writeMask, expectedWriteMask);

        ConfigureMetalColorAttachmentDescriptor(desc, nil);
    }
}

TEST(MetalConversionsTest, MapsMultisampleDescriptorFields) {
    @autoreleasepool {
        RHI::MultisampleStateDesc desc;
        desc.sampleCount = 4;
        desc.alphaToCoverageEnabled = true;

        MTLRenderPipelineDescriptor* pipelineDesc = [[MTLRenderPipelineDescriptor alloc] init];
        ConfigureMetalMultisampleDescriptor(desc, pipelineDesc);

#if defined(__IPHONE_16_0) || defined(__MAC_13_0)
        EXPECT_EQ(pipelineDesc.rasterSampleCount, 4u);
#else
        EXPECT_EQ(pipelineDesc.sampleCount, 4u);
#endif
        EXPECT_TRUE(pipelineDesc.alphaToCoverageEnabled);

        ConfigureMetalMultisampleDescriptor(desc, nil);
    }
}

TEST(MetalConversionsTest, MapsRenderPassLoadStoreActions) {
    EXPECT_EQ(ToMetalLoadAction(RHI::AttachmentLoadAction::Load), MTLLoadActionLoad);
    EXPECT_EQ(ToMetalLoadAction(RHI::AttachmentLoadAction::Clear), MTLLoadActionClear);
    EXPECT_EQ(ToMetalLoadAction(RHI::AttachmentLoadAction::DontCare), MTLLoadActionDontCare);
    EXPECT_EQ(ToMetalLoadAction(static_cast<RHI::AttachmentLoadAction>(255)), MTLLoadActionDontCare);

    EXPECT_EQ(ToMetalStoreAction(RHI::AttachmentStoreAction::Store), MTLStoreActionStore);
    EXPECT_EQ(ToMetalStoreAction(RHI::AttachmentStoreAction::DontCare), MTLStoreActionDontCare);
    EXPECT_EQ(ToMetalStoreAction(static_cast<RHI::AttachmentStoreAction>(255)), MTLStoreActionDontCare);
}

TEST(MetalConversionsTest, MapsSamplerDescriptorFields) {
    @autoreleasepool {
        RHI::SamplerDesc desc;
        desc.minFilter = RHI::SamplerFilter::Nearest;
        desc.magFilter = RHI::SamplerFilter::Linear;
        desc.mipFilter = RHI::SamplerMipFilter::Linear;
        desc.addressU = RHI::SamplerAddressMode::ClampToEdge;
        desc.addressV = RHI::SamplerAddressMode::MirrorRepeat;
        desc.addressW = RHI::SamplerAddressMode::ClampToBorder;
        desc.borderColor = RHI::SamplerBorderColor::OpaqueWhite;
        desc.compareFunction = RHI::CompareFunction::GreaterEqual;
        desc.anisotropyEnabled = true;
        desc.maxAnisotropy = 8;
        desc.minLod = 0.5f;
        desc.maxLod = 12.0f;
        desc.debugName = "test sampler descriptor";

        MTLSamplerDescriptor* samplerDesc = [[MTLSamplerDescriptor alloc] init];
        ConfigureMetalSamplerDescriptor(desc, samplerDesc);

        EXPECT_EQ(samplerDesc.minFilter, MTLSamplerMinMagFilterNearest);
        EXPECT_EQ(samplerDesc.magFilter, MTLSamplerMinMagFilterLinear);
        EXPECT_EQ(samplerDesc.mipFilter, MTLSamplerMipFilterLinear);
        EXPECT_EQ(samplerDesc.sAddressMode, MTLSamplerAddressModeClampToEdge);
        EXPECT_EQ(samplerDesc.tAddressMode, MTLSamplerAddressModeMirrorRepeat);
        EXPECT_EQ(samplerDesc.rAddressMode, MTLSamplerAddressModeClampToBorderColor);
        EXPECT_EQ(samplerDesc.borderColor, MTLSamplerBorderColorOpaqueWhite);
        EXPECT_EQ(samplerDesc.compareFunction, MTLCompareFunctionGreaterEqual);
        EXPECT_EQ(samplerDesc.maxAnisotropy, 8u);
        EXPECT_FLOAT_EQ(samplerDesc.lodMinClamp, 0.5f);
        EXPECT_FLOAT_EQ(samplerDesc.lodMaxClamp, 12.0f);
        ASSERT_NE(samplerDesc.label, nil);
        EXPECT_STREQ([samplerDesc.label UTF8String], "test sampler descriptor");
    }
}

TEST(MetalConversionsTest, MapsShaderResourceGroupLayoutToArgumentDescriptors) {
    EXPECT_EQ(ToMetalArgumentDataType(RHI::ShaderResourceBindingType::ConstantBuffer),
              MTLDataTypePointer);
    EXPECT_EQ(ToMetalArgumentDataType(RHI::ShaderResourceBindingType::ReadOnlyBuffer),
              MTLDataTypePointer);
    EXPECT_EQ(ToMetalArgumentDataType(RHI::ShaderResourceBindingType::ReadWriteBuffer),
              MTLDataTypePointer);
    EXPECT_EQ(ToMetalArgumentDataType(RHI::ShaderResourceBindingType::Texture),
              MTLDataTypeTexture);
    EXPECT_EQ(ToMetalArgumentDataType(RHI::ShaderResourceBindingType::StorageTexture),
              MTLDataTypeTexture);
    EXPECT_EQ(ToMetalArgumentDataType(RHI::ShaderResourceBindingType::Sampler),
              MTLDataTypeSampler);
    EXPECT_EQ(ToMetalArgumentAccess(RHI::ShaderResourceBindingType::ReadWriteBuffer),
              MTLBindingAccessReadWrite);
    EXPECT_EQ(ToMetalArgumentAccess(RHI::ShaderResourceBindingType::StorageTexture),
              MTLBindingAccessReadWrite);
    EXPECT_EQ(ToMetalArgumentAccess(RHI::ShaderResourceBindingType::Texture),
              MTLBindingAccessReadOnly);

    @autoreleasepool {
        RHI::ShaderResourceGroupLayoutDesc layout;
        layout.bindingCount = 3;
        layout.bindings[0].type = RHI::ShaderResourceBindingType::ConstantBuffer;
        layout.bindings[0].shaderStages = RHI::ShaderStage::Vertex | RHI::ShaderStage::Fragment;
        layout.bindings[0].bindingIndex = 7;
        layout.bindings[0].bindingCount = 1;
        layout.bindings[1].type = RHI::ShaderResourceBindingType::Texture;
        layout.bindings[1].shaderStages = RHI::ShaderStageFlag(RHI::ShaderStage::Fragment);
        layout.bindings[1].bindingIndex = 0;
        layout.bindings[1].bindingCount = 5;
        layout.bindings[2].type = RHI::ShaderResourceBindingType::Sampler;
        layout.bindings[2].shaderStages = RHI::ShaderStageFlag(RHI::ShaderStage::Fragment);
        layout.bindings[2].bindingIndex = 0;
        layout.bindings[2].bindingCount = 5;

        NSArray<MTLArgumentDescriptor*>* arguments = MakeMetalArgumentDescriptors(layout);
        ASSERT_NE(arguments, nil);
        ASSERT_EQ(arguments.count, 3u);

        MTLArgumentDescriptor* uniformArgument = arguments[0];
        EXPECT_EQ(uniformArgument.index, 0u);
        EXPECT_EQ(uniformArgument.dataType, MTLDataTypePointer);
        EXPECT_EQ(uniformArgument.access, MTLBindingAccessReadOnly);
        EXPECT_EQ(uniformArgument.arrayLength, 1u);

        MTLArgumentDescriptor* textureArgument = arguments[1];
        EXPECT_EQ(textureArgument.index, 1u);
        EXPECT_EQ(textureArgument.dataType, MTLDataTypeTexture);
        EXPECT_EQ(textureArgument.access, MTLBindingAccessReadOnly);
        EXPECT_EQ(textureArgument.textureType, MTLTextureType2D);
        EXPECT_EQ(textureArgument.arrayLength, 5u);

        MTLArgumentDescriptor* samplerArgument = arguments[2];
        EXPECT_EQ(samplerArgument.index, 6u);
        EXPECT_EQ(samplerArgument.dataType, MTLDataTypeSampler);
        EXPECT_EQ(samplerArgument.access, MTLBindingAccessReadOnly);
        EXPECT_EQ(samplerArgument.arrayLength, 5u);

        RHI::ShaderResourceBindingDesc storageTexture;
        storageTexture.type = RHI::ShaderResourceBindingType::StorageTexture;
        storageTexture.shaderStages = RHI::ShaderStageFlag(RHI::ShaderStage::Compute);
        storageTexture.bindingIndex = 4;
        storageTexture.bindingCount = 2;
        MTLArgumentDescriptor* storageArgument = [MTLArgumentDescriptor argumentDescriptor];
        EXPECT_TRUE(ConfigureMetalArgumentDescriptor(storageTexture, 9, storageArgument));
        EXPECT_EQ(storageArgument.index, 9u);
        EXPECT_EQ(storageArgument.dataType, MTLDataTypeTexture);
        EXPECT_EQ(storageArgument.access, MTLBindingAccessReadWrite);
        EXPECT_EQ(storageArgument.arrayLength, 2u);
    }
}

TEST(MetalConversionsTest, RejectsInvalidShaderResourceGroupArgumentDescriptors) {
    RHI::ShaderResourceBindingDesc binding;
    binding.type = RHI::ShaderResourceBindingType::Texture;
    binding.shaderStages = RHI::ShaderStageFlag(RHI::ShaderStage::Fragment);
    binding.bindingIndex = 0;
    binding.bindingCount = 1;
    EXPECT_FALSE(ConfigureMetalArgumentDescriptor(binding, 0, nil));

    @autoreleasepool {
        MTLArgumentDescriptor* argument = [MTLArgumentDescriptor argumentDescriptor];
        binding.shaderStages = 0;
        EXPECT_FALSE(ConfigureMetalArgumentDescriptor(binding, 0, argument));
        binding.shaderStages = RHI::ShaderStageFlag(RHI::ShaderStage::Fragment);
        binding.bindingCount = 0;
        EXPECT_FALSE(ConfigureMetalArgumentDescriptor(binding, 0, argument));

        RHI::ShaderResourceGroupLayoutDesc emptyLayout;
        EXPECT_EQ(MakeMetalArgumentDescriptors(emptyLayout), nil);

        RHI::ShaderResourceGroupLayoutDesc overlappingLayout;
        overlappingLayout.bindingCount = 2;
        overlappingLayout.bindings[0].type = RHI::ShaderResourceBindingType::Texture;
        overlappingLayout.bindings[0].shaderStages = RHI::ShaderStageFlag(RHI::ShaderStage::Fragment);
        overlappingLayout.bindings[0].bindingIndex = 0;
        overlappingLayout.bindings[0].bindingCount = 2;
        overlappingLayout.bindings[1].type = RHI::ShaderResourceBindingType::Texture;
        overlappingLayout.bindings[1].shaderStages = RHI::ShaderStageFlag(RHI::ShaderStage::Fragment);
        overlappingLayout.bindings[1].bindingIndex = 1;
        overlappingLayout.bindings[1].bindingCount = 1;
        EXPECT_EQ(MakeMetalArgumentDescriptors(overlappingLayout), nil);
    }
}

TEST(MetalConversionsTest, MapsDepthStencilDescriptorFields) {
    @autoreleasepool {
        RHI::DepthStencilStateDesc desc;
        desc.depthTestEnabled = false;
        desc.depthWriteEnabled = true;
        desc.depthCompare = RHI::CompareFunction::GreaterEqual;
        desc.stencilTestEnabled = true;
        desc.stencilReadMask = 0x3f;
        desc.stencilWriteMask = 0x7f;
        desc.frontStencil.compare = RHI::CompareFunction::Equal;
        desc.frontStencil.stencilFailOp = RHI::StencilOperation::Replace;
        desc.frontStencil.depthFailOp = RHI::StencilOperation::IncrementClamp;
        desc.frontStencil.passOp = RHI::StencilOperation::IncrementWrap;
        desc.backStencil.compare = RHI::CompareFunction::NotEqual;
        desc.backStencil.stencilFailOp = RHI::StencilOperation::Zero;
        desc.backStencil.depthFailOp = RHI::StencilOperation::DecrementClamp;
        desc.backStencil.passOp = RHI::StencilOperation::DecrementWrap;

        MTLDepthStencilDescriptor* depthStencilDesc = [[MTLDepthStencilDescriptor alloc] init];
        ConfigureMetalDepthStencilDescriptor(desc, depthStencilDesc);

        EXPECT_EQ(depthStencilDesc.depthCompareFunction, MTLCompareFunctionAlways);
        EXPECT_FALSE(depthStencilDesc.depthWriteEnabled);
        EXPECT_EQ(depthStencilDesc.frontFaceStencil.readMask, 0x3fu);
        EXPECT_EQ(depthStencilDesc.frontFaceStencil.writeMask, 0x7fu);
        EXPECT_EQ(depthStencilDesc.frontFaceStencil.stencilCompareFunction, MTLCompareFunctionEqual);
        EXPECT_EQ(depthStencilDesc.frontFaceStencil.stencilFailureOperation, MTLStencilOperationReplace);
        EXPECT_EQ(depthStencilDesc.frontFaceStencil.depthFailureOperation,
                  MTLStencilOperationIncrementClamp);
        EXPECT_EQ(depthStencilDesc.frontFaceStencil.depthStencilPassOperation,
                  MTLStencilOperationIncrementWrap);
        EXPECT_EQ(depthStencilDesc.backFaceStencil.stencilCompareFunction, MTLCompareFunctionNotEqual);
        EXPECT_EQ(depthStencilDesc.backFaceStencil.stencilFailureOperation, MTLStencilOperationZero);
        EXPECT_EQ(depthStencilDesc.backFaceStencil.depthFailureOperation,
                  MTLStencilOperationDecrementClamp);
        EXPECT_EQ(depthStencilDesc.backFaceStencil.depthStencilPassOperation,
                  MTLStencilOperationDecrementWrap);

        desc.depthTestEnabled = true;
        ConfigureMetalDepthStencilDescriptor(desc, depthStencilDesc);
        EXPECT_EQ(depthStencilDesc.depthCompareFunction, MTLCompareFunctionGreaterEqual);
        EXPECT_TRUE(depthStencilDesc.depthWriteEnabled);
    }
}

TEST(MetalConversionsTest, SamplerDescriptorHandlesNullAndAnisotropyFallback) {
    RHI::SamplerDesc desc;
    desc.anisotropyEnabled = true;
    desc.maxAnisotropy = 0;
    ConfigureMetalSamplerDescriptor(desc, nil);

    @autoreleasepool {
        MTLSamplerDescriptor* samplerDesc = [[MTLSamplerDescriptor alloc] init];
        ConfigureMetalSamplerDescriptor(desc, samplerDesc);
        EXPECT_EQ(samplerDesc.maxAnisotropy, 1u);

        desc.anisotropyEnabled = true;
        desc.maxAnisotropy = 255;
        ConfigureMetalSamplerDescriptor(desc, samplerDesc);
        EXPECT_EQ(samplerDesc.maxAnisotropy, 16u);

        desc.anisotropyEnabled = false;
        desc.maxAnisotropy = 16;
        ConfigureMetalSamplerDescriptor(desc, samplerDesc);
        EXPECT_EQ(samplerDesc.maxAnisotropy, 1u);
    }
}

TEST(MetalConversionsTest, SamplerDescriptorSanitizesLodClampRange) {
    @autoreleasepool {
        RHI::SamplerDesc desc;
        MTLSamplerDescriptor* samplerDesc = [[MTLSamplerDescriptor alloc] init];

        desc.minLod = -2.0f;
        desc.maxLod = -1.0f;
        ConfigureMetalSamplerDescriptor(desc, samplerDesc);
        EXPECT_FLOAT_EQ(samplerDesc.lodMinClamp, 0.0f);
        EXPECT_FLOAT_EQ(samplerDesc.lodMaxClamp, 0.0f);

        desc.minLod = 4.0f;
        desc.maxLod = 2.0f;
        ConfigureMetalSamplerDescriptor(desc, samplerDesc);
        EXPECT_FLOAT_EQ(samplerDesc.lodMinClamp, 4.0f);
        EXPECT_FLOAT_EQ(samplerDesc.lodMaxClamp, 4.0f);

        desc.minLod = std::numeric_limits<float>::quiet_NaN();
        desc.maxLod = std::numeric_limits<float>::quiet_NaN();
        ConfigureMetalSamplerDescriptor(desc, samplerDesc);
        EXPECT_FLOAT_EQ(samplerDesc.lodMinClamp, 0.0f);
        EXPECT_FLOAT_EQ(samplerDesc.lodMaxClamp, 0.0f);
    }
}

} // namespace
} // namespace testing
} // namespace MetalBackend
} // namespace Next
