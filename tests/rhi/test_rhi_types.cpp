#include "next/rhi/rhi.h"

#include <gtest/gtest.h>

#include <limits>

namespace Next {
namespace testing {
namespace {

class TestResource final : public RHI::Resource {
public:
    explicit TestResource(RHI::ResourceUsageFlags usage = RHI::ResourceUsageFlag(RHI::ResourceUsage::None),
                          RHI::ResourceType type = RHI::ResourceType::Buffer)
        : usage_(usage),
          type_(type) {}

    RHI::ResourceType GetResourceType() const override { return type_; }
    const char* GetDebugName() const override { return "test"; }
    RHI::ResourceUsageFlags GetUsageFlags() const override { return usage_; }
    RHI::ResourceState GetCurrentState() const override { return state_; }
    void SetCurrentState(RHI::ResourceState state) override { state_ = state; }

private:
    RHI::ResourceUsageFlags usage_ = RHI::ResourceUsageFlag(RHI::ResourceUsage::None);
    RHI::ResourceType type_ = RHI::ResourceType::Buffer;
    RHI::ResourceState state_ = RHI::ResourceState::Undefined;
};

class TestCommandContext final : public RHI::CommandContext {
public:
    explicit TestCommandContext(RHI::QueueClass queueClass)
        : queueClass_(queueClass) {}

    RHI::QueueClass GetQueueClass() const override { return queueClass_; }
    bool IsRecording() const override { return true; }
    uint64_t GetSubmittedFrameIndex() const override { return 7; }

private:
    RHI::QueueClass queueClass_ = RHI::QueueClass::Graphics;
};

RHI::GraphicsPipelineDesc MakeValidVertexInputPipeline() {
    RHI::GraphicsPipelineDesc pipeline =
        RHI::MakeGraphicsPipelineDesc("vs", "fs", RHI::Format::RGBA8Unorm, RHI::Format::Depth32Float);
    pipeline.vertexInput.bufferCount = 1;
    pipeline.vertexInput.buffers[0].stride = 24;
    pipeline.vertexInput.buffers[0].stepFunction = RHI::VertexStepFunction::PerVertex;
    pipeline.vertexInput.buffers[0].stepRate = 1;
    pipeline.vertexInput.attributeCount = 2;
    pipeline.vertexInput.attributes[0].location = 0;
    pipeline.vertexInput.attributes[0].bufferIndex = 0;
    pipeline.vertexInput.attributes[0].format = RHI::VertexFormat::Float32x2;
    pipeline.vertexInput.attributes[0].offset = 0;
    pipeline.vertexInput.attributes[1].location = 1;
    pipeline.vertexInput.attributes[1].bufferIndex = 0;
    pipeline.vertexInput.attributes[1].format = RHI::VertexFormat::Float32x4;
    pipeline.vertexInput.attributes[1].offset = 8;
    return pipeline;
}

RHI::ShaderResourceGroupLayoutDesc MakeValidShaderResourceGroupLayout() {
    RHI::ShaderResourceGroupLayoutDesc layout;
    layout.debugName = "test_srg";
    layout.bindingCount = 3;
    layout.bindings[0].type = RHI::ShaderResourceBindingType::ConstantBuffer;
    layout.bindings[0].shaderStages = RHI::ShaderStage::Vertex | RHI::ShaderStage::Fragment;
    layout.bindings[0].bindingIndex = 1;
    layout.bindings[0].bindingCount = 1;
    layout.bindings[0].debugName = "material_constants";
    layout.bindings[1].type = RHI::ShaderResourceBindingType::Texture;
    layout.bindings[1].shaderStages = RHI::ShaderStageFlag(RHI::ShaderStage::Fragment);
    layout.bindings[1].bindingIndex = 0;
    layout.bindings[1].bindingCount = 5;
    layout.bindings[1].debugName = "material_textures";
    layout.bindings[2].type = RHI::ShaderResourceBindingType::Sampler;
    layout.bindings[2].shaderStages = RHI::ShaderStageFlag(RHI::ShaderStage::Fragment);
    layout.bindings[2].bindingIndex = 0;
    layout.bindings[2].bindingCount = 5;
    layout.bindings[2].debugName = "material_samplers";
    return layout;
}

} // namespace

TEST(RHITypesTest, NamesCoverPublicEnums) {
    EXPECT_STREQ(RHI::BackendName(RHI::Backend::Metal), "metal");
    EXPECT_STREQ(RHI::QueueClassName(RHI::QueueClass::Copy), "copy");
    EXPECT_STREQ(RHI::FormatName(RHI::Format::Depth32Float), "depth32float");
    EXPECT_STREQ(RHI::FormatName(RHI::Format::Depth32FloatStencil8), "depth32float_stencil8");
    EXPECT_STREQ(RHI::ResourceTypeName(RHI::ResourceType::Texture), "texture");
    EXPECT_STREQ(RHI::ResourceMemoryName(RHI::ResourceMemory::Upload), "upload");
    EXPECT_STREQ(RHI::SamplerFilterName(RHI::SamplerFilter::Linear), "linear");
    EXPECT_STREQ(RHI::SamplerMipFilterName(RHI::SamplerMipFilter::NotMipmapped), "not_mipmapped");
    EXPECT_STREQ(RHI::SamplerAddressModeName(RHI::SamplerAddressMode::MirrorRepeat), "mirror_repeat");
    EXPECT_STREQ(RHI::SamplerBorderColorName(RHI::SamplerBorderColor::TransparentBlack), "transparent_black");
    EXPECT_STREQ(RHI::CompareFunctionName(RHI::CompareFunction::GreaterEqual), "greater_equal");
    EXPECT_STREQ(RHI::StencilOperationName(RHI::StencilOperation::IncrementWrap), "increment_wrap");
    EXPECT_STREQ(RHI::BlendFactorName(RHI::BlendFactor::OneMinusSourceAlpha), "one_minus_source_alpha");
    EXPECT_STREQ(RHI::BlendOperationName(RHI::BlendOperation::ReverseSubtract), "reverse_subtract");
    EXPECT_STREQ(RHI::ColorWriteMaskName(RHI::ColorWriteMask::All), "all");
    EXPECT_STREQ(RHI::VertexFormatName(RHI::VertexFormat::Float32x3), "float32x3");
    EXPECT_STREQ(RHI::VertexStepFunctionName(RHI::VertexStepFunction::PerInstance), "per_instance");
    EXPECT_STREQ(RHI::IndexFormatName(RHI::IndexFormat::Uint32), "uint32");
    EXPECT_STREQ(RHI::AttachmentLoadActionName(RHI::AttachmentLoadAction::Clear), "clear");
    EXPECT_STREQ(RHI::AttachmentStoreActionName(RHI::AttachmentStoreAction::DontCare), "dont_care");
    EXPECT_STREQ(RHI::ShaderStageName(RHI::ShaderStage::Fragment), "fragment");
    EXPECT_TRUE(RHI::HasShaderStage(RHI::ShaderStage::Vertex | RHI::ShaderStage::Fragment,
                                    RHI::ShaderStage::Vertex));
    EXPECT_STREQ(RHI::ShaderResourceBindingTypeName(RHI::ShaderResourceBindingType::Texture),
                 "texture");
    EXPECT_STREQ(RHI::ShaderResourceBindingTypeName(RHI::ShaderResourceBindingType::ReadWriteBuffer),
                 "read_write_buffer");
    EXPECT_STREQ(RHI::PrimitiveTopologyName(RHI::PrimitiveTopology::TriangleStrip), "triangle_strip");
    EXPECT_STREQ(RHI::FillModeName(RHI::FillMode::Wireframe), "wireframe");
    EXPECT_STREQ(RHI::CullModeName(RHI::CullMode::Back), "back");
    EXPECT_STREQ(RHI::FrontFaceWindingName(RHI::FrontFaceWinding::Clockwise), "clockwise");
    EXPECT_STREQ(RHI::ResourceStateName(RHI::ResourceState::CopyDestination), "copy_destination");
    EXPECT_STREQ(RHI::ResourceStateName(RHI::ResourceState::VertexBuffer), "vertex_buffer");
    EXPECT_STREQ(RHI::ResourceStateName(RHI::ResourceState::IndexBuffer), "index_buffer");
    EXPECT_STREQ(RHI::ResourceStateName(RHI::ResourceState::ConstantBuffer), "constant_buffer");
    EXPECT_STREQ(RHI::ResourceUsageName(RHI::ResourceUsage::RenderTarget), "render_target");
    EXPECT_STREQ(RHI::ResourceDescriptorErrorName(RHI::ResourceDescriptorError::UnsupportedInitialState),
                 "unsupported_initial_state");
    EXPECT_STREQ(RHI::ResourceDescriptorErrorName(RHI::ResourceDescriptorError::UnsupportedMemory),
                 "unsupported_memory");
    EXPECT_STREQ(RHI::ResourceDescriptorErrorName(RHI::ResourceDescriptorError::MissingUsage),
                 "missing_usage");
    EXPECT_STREQ(RHI::ResourceDescriptorErrorName(RHI::ResourceDescriptorError::TextureSizeOverflow),
                 "texture_size_overflow");
    EXPECT_STREQ(RHI::ResourceDescriptorErrorName(RHI::ResourceDescriptorError::UnsupportedRenderTargetFormat),
                 "unsupported_render_target_format");
    EXPECT_STREQ(RHI::ResourceDescriptorErrorName(RHI::ResourceDescriptorError::UnsupportedDepthStencilFormat),
                 "unsupported_depth_stencil_format");
    EXPECT_STREQ(RHI::ResourceDescriptorErrorName(RHI::ResourceDescriptorError::UnsupportedSampleCount),
                 "unsupported_sample_count");
    EXPECT_STREQ(RHI::DrawDescriptorErrorName(RHI::DrawDescriptorError::MisalignedIndexBufferOffset),
                 "misaligned_index_buffer_offset");
    EXPECT_STREQ(RHI::DrawDescriptorErrorName(RHI::DrawDescriptorError::InvalidBlendConstant),
                 "invalid_blend_constant");
    EXPECT_STREQ(RHI::ViewportDescriptorErrorName(RHI::ViewportDescriptorError::InvalidDepthRange),
                 "invalid_depth_range");
    EXPECT_STREQ(RHI::ScissorDescriptorErrorName(RHI::ScissorDescriptorError::EmptyScissor),
                 "empty_scissor");
    EXPECT_STREQ(RHI::ResourceTransitionErrorName(RHI::ResourceTransitionError::DuplicateResource), "duplicate_resource");
    EXPECT_STREQ(RHI::ResourceTransitionErrorName(RHI::ResourceTransitionError::UnsupportedQueueClass), "unsupported_queue_class");
    EXPECT_STREQ(RHI::PipelineDescriptorErrorName(RHI::PipelineDescriptorError::UnsupportedColorFormat),
                 "unsupported_color_format");
    EXPECT_STREQ(RHI::PipelineDescriptorErrorName(RHI::PipelineDescriptorError::UnsupportedStencilFormat),
                 "unsupported_stencil_format");
    EXPECT_STREQ(RHI::PipelineDescriptorErrorName(RHI::PipelineDescriptorError::InvalidStencilOperation),
                 "invalid_stencil_operation");
    EXPECT_STREQ(RHI::PipelineDescriptorErrorName(RHI::PipelineDescriptorError::InvalidBlendFactor),
                 "invalid_blend_factor");
    EXPECT_STREQ(RHI::PipelineDescriptorErrorName(RHI::PipelineDescriptorError::InvalidBlendOperation),
                 "invalid_blend_operation");
    EXPECT_STREQ(RHI::PipelineDescriptorErrorName(RHI::PipelineDescriptorError::InvalidColorWriteMask),
                 "invalid_color_write_mask");
    EXPECT_STREQ(RHI::PipelineDescriptorErrorName(RHI::PipelineDescriptorError::InvalidSampleCount),
                 "invalid_sample_count");
    EXPECT_STREQ(RHI::PipelineDescriptorErrorName(RHI::PipelineDescriptorError::InvalidVertexFormat),
                 "invalid_vertex_format");
    EXPECT_STREQ(RHI::PipelineDescriptorErrorName(RHI::PipelineDescriptorError::InvalidVertexStepFunction),
                 "invalid_vertex_step_function");
    EXPECT_STREQ(RHI::PipelineDescriptorErrorName(RHI::PipelineDescriptorError::InvalidVertexStepRate),
                 "invalid_vertex_step_rate");
    EXPECT_STREQ(RHI::PipelineDescriptorErrorName(RHI::PipelineDescriptorError::InvalidFillMode),
                 "invalid_fill_mode");
    EXPECT_STREQ(RHI::PipelineDescriptorErrorName(RHI::PipelineDescriptorError::InvalidDepthBias),
                 "invalid_depth_bias");
    EXPECT_STREQ(RHI::RenderPassDescriptorErrorName(RHI::RenderPassDescriptorError::InvalidDepthClearValue),
                 "invalid_depth_clear_value");
    EXPECT_STREQ(RHI::RenderPassDescriptorErrorName(RHI::RenderPassDescriptorError::InvalidStencilClearValue),
                 "invalid_stencil_clear_value");
    EXPECT_STREQ(RHI::ShaderResourceGroupLayoutErrorName(
                     RHI::ShaderResourceGroupLayoutError::OverlappingBindingRange),
                 "overlapping_binding_range");
    EXPECT_STREQ(RHI::ShaderResourceGroupLayoutErrorName(
                     RHI::ShaderResourceGroupLayoutError::BindingRangeOverflow),
                 "binding_range_overflow");
    EXPECT_STREQ(RHI::FrameGraphAccessTypeName(RHI::FrameGraphAccessType::ReadWrite), "read_write");
    EXPECT_STREQ(RHI::FrameGraphDescriptorErrorName(
                     RHI::FrameGraphDescriptorError::DuplicateResourceAccess),
                 "duplicate_resource_access");
    EXPECT_STREQ(RHI::FrameGraphDescriptorErrorName(
                     RHI::FrameGraphDescriptorError::DuplicatePassDependency),
                 "duplicate_pass_dependency");
    EXPECT_STREQ(RHI::FrameGraphDescriptorErrorName(
                     RHI::FrameGraphDescriptorError::InvalidResourceType),
                 "invalid_resource_type");
    EXPECT_STREQ(RHI::FrameGraphDescriptorErrorName(
                     RHI::FrameGraphDescriptorError::TransitionCapacityExceeded),
                 "transition_capacity_exceeded");
    EXPECT_STREQ(RHI::FrameGraphExecutionErrorName(
                     RHI::FrameGraphExecutionError::ResourceTypeMismatch),
                 "resource_type_mismatch");
    EXPECT_STREQ(RHI::SwapchainDescriptorErrorName(RHI::SwapchainDescriptorError::EmptyDrawableSize),
                 "empty_drawable_size");
}

TEST(RHITypesTest, FormatBytesPerPixelCoversPublicFormats) {
    EXPECT_EQ(RHI::FormatBytesPerPixel(RHI::Format::Unknown), 0u);
    EXPECT_EQ(RHI::FormatBytesPerPixel(RHI::Format::BGRA8Unorm), 4u);
    EXPECT_EQ(RHI::FormatBytesPerPixel(RHI::Format::RGBA8Unorm), 4u);
    EXPECT_EQ(RHI::FormatBytesPerPixel(RHI::Format::Depth32Float), 4u);
    EXPECT_EQ(RHI::FormatBytesPerPixel(RHI::Format::Depth32FloatStencil8), 8u);
    EXPECT_EQ(RHI::FormatBytesPerPixel(static_cast<RHI::Format>(65535)), 0u);
}

TEST(RHITypesTest, IndexFormatByteSizeCoversPublicFormats) {
    EXPECT_EQ(RHI::IndexFormatByteSize(RHI::IndexFormat::Unknown), 0u);
    EXPECT_EQ(RHI::IndexFormatByteSize(RHI::IndexFormat::Uint16), 2u);
    EXPECT_EQ(RHI::IndexFormatByteSize(RHI::IndexFormat::Uint32), 4u);
    EXPECT_EQ(RHI::IndexFormatByteSize(static_cast<RHI::IndexFormat>(255)), 0u);
}

TEST(RHITypesTest, FormatAttachmentCapabilitiesCoverPublicFormats) {
    EXPECT_TRUE(RHI::FormatSupportsColorAttachment(RHI::Format::BGRA8Unorm));
    EXPECT_TRUE(RHI::FormatSupportsColorAttachment(RHI::Format::RGBA8Unorm));
    EXPECT_FALSE(RHI::FormatSupportsColorAttachment(RHI::Format::Unknown));
    EXPECT_FALSE(RHI::FormatSupportsColorAttachment(RHI::Format::Depth32Float));
    EXPECT_FALSE(RHI::FormatSupportsColorAttachment(RHI::Format::Depth32FloatStencil8));
    EXPECT_FALSE(RHI::FormatSupportsColorAttachment(static_cast<RHI::Format>(65535)));

    EXPECT_TRUE(RHI::FormatSupportsDepthStencil(RHI::Format::Depth32Float));
    EXPECT_TRUE(RHI::FormatSupportsDepthStencil(RHI::Format::Depth32FloatStencil8));
    EXPECT_FALSE(RHI::FormatSupportsDepthStencil(RHI::Format::Unknown));
    EXPECT_FALSE(RHI::FormatSupportsDepthStencil(RHI::Format::BGRA8Unorm));
    EXPECT_FALSE(RHI::FormatSupportsDepthStencil(RHI::Format::RGBA8Unorm));
    EXPECT_FALSE(RHI::FormatSupportsDepthStencil(static_cast<RHI::Format>(65535)));

    EXPECT_TRUE(RHI::FormatHasDepth(RHI::Format::Depth32Float));
    EXPECT_TRUE(RHI::FormatHasDepth(RHI::Format::Depth32FloatStencil8));
    EXPECT_FALSE(RHI::FormatHasDepth(RHI::Format::RGBA8Unorm));
    EXPECT_FALSE(RHI::FormatHasDepth(static_cast<RHI::Format>(65535)));
    EXPECT_FALSE(RHI::FormatHasStencil(RHI::Format::Depth32Float));
    EXPECT_TRUE(RHI::FormatHasStencil(RHI::Format::Depth32FloatStencil8));
    EXPECT_FALSE(RHI::FormatHasStencil(RHI::Format::RGBA8Unorm));
    EXPECT_FALSE(RHI::FormatHasStencil(static_cast<RHI::Format>(65535)));
}

TEST(RHITypesTest, DrawIndexedDescriptorValidationAndOffsetMath) {
    RHI::IndexBufferViewDesc indexBuffer;
    indexBuffer.format = RHI::IndexFormat::Uint16;
    indexBuffer.byteOffset = 4;

    RHI::DrawIndexedDesc draw;
    draw.indexCount = 36;
    draw.instanceCount = 2;
    draw.indexOffset = 3;
    draw.vertexOffset = -2;
    draw.instanceOffset = 1;
    EXPECT_EQ(draw.stencilReference, 0u);
    EXPECT_FLOAT_EQ(draw.blendConstant[0], 0.0f);
    EXPECT_FLOAT_EQ(draw.blendConstant[1], 0.0f);
    EXPECT_FLOAT_EQ(draw.blendConstant[2], 0.0f);
    EXPECT_FLOAT_EQ(draw.blendConstant[3], 0.0f);

    EXPECT_TRUE(RHI::ValidateDrawIndexedDesc(draw, indexBuffer));
    uint64_t byteOffset = 0;
    ASSERT_TRUE(RHI::DrawIndexedIndexBufferOffsetBytes(draw, indexBuffer, byteOffset));
    EXPECT_EQ(byteOffset, 10u);

    draw.stencilReference = 7;
    draw.blendConstant = {0.25f, 0.5f, 0.75f, 1.0f};
    EXPECT_TRUE(RHI::ValidateDrawIndexedDesc(draw, indexBuffer));
    ASSERT_TRUE(RHI::DrawIndexedIndexBufferOffsetBytes(draw, indexBuffer, byteOffset));
    EXPECT_EQ(byteOffset, 10u);

    draw.blendConstant[2] = std::numeric_limits<float>::infinity();
    RHI::DrawIndexedValidation validation = RHI::ValidateDrawIndexedDesc(draw, indexBuffer);
    EXPECT_EQ(validation.error, RHI::DrawDescriptorError::InvalidBlendConstant);
    draw.blendConstant = {0.25f, 0.5f, 0.75f, 1.0f};

    draw.indexCount = 0;
    validation = RHI::ValidateDrawIndexedDesc(draw, indexBuffer);
    EXPECT_EQ(validation.error, RHI::DrawDescriptorError::MissingIndexCount);
    EXPECT_EQ(validation.indexFormat, RHI::IndexFormat::Uint16);

    draw.indexCount = 36;
    draw.instanceCount = 0;
    validation = RHI::ValidateDrawIndexedDesc(draw, indexBuffer);
    EXPECT_EQ(validation.error, RHI::DrawDescriptorError::MissingInstanceCount);

    draw.instanceCount = 1;
    indexBuffer.format = RHI::IndexFormat::Unknown;
    validation = RHI::ValidateDrawIndexedDesc(draw, indexBuffer);
    EXPECT_EQ(validation.error, RHI::DrawDescriptorError::InvalidIndexFormat);
    EXPECT_EQ(validation.indexFormat, RHI::IndexFormat::Unknown);

    indexBuffer.format = RHI::IndexFormat::Uint32;
    indexBuffer.byteOffset = 2;
    validation = RHI::ValidateDrawIndexedDesc(draw, indexBuffer);
    EXPECT_EQ(validation.error, RHI::DrawDescriptorError::MisalignedIndexBufferOffset);

    indexBuffer.byteOffset = std::numeric_limits<uint64_t>::max() - 3;
    draw.indexOffset = 1;
    validation = RHI::ValidateDrawIndexedDesc(draw, indexBuffer);
    EXPECT_EQ(validation.error, RHI::DrawDescriptorError::IndexBufferOffsetOverflow);
    EXPECT_FALSE(RHI::DrawIndexedIndexBufferOffsetBytes(draw, indexBuffer, byteOffset));
    EXPECT_EQ(byteOffset, 0u);
}

TEST(RHITypesTest, ViewportAndScissorValidationReportsErrors) {
    RHI::ViewportDesc viewport;
    viewport.maxX = 1280.0;
    viewport.maxY = 720.0;
    EXPECT_TRUE(RHI::ValidateViewportDesc(viewport));

    viewport.maxX = 0.0;
    RHI::ViewportDescriptorValidation viewportValidation = RHI::ValidateViewportDesc(viewport);
    EXPECT_EQ(viewportValidation.error, RHI::ViewportDescriptorError::EmptyViewport);

    viewport.maxX = 1280.0;
    viewport.minZ = -0.01;
    viewportValidation = RHI::ValidateViewportDesc(viewport);
    EXPECT_EQ(viewportValidation.error, RHI::ViewportDescriptorError::InvalidDepthRange);

    viewport.minZ = 0.75;
    viewport.maxZ = 0.25;
    viewportValidation = RHI::ValidateViewportDesc(viewport);
    EXPECT_EQ(viewportValidation.error, RHI::ViewportDescriptorError::InvalidDepthRange);

    RHI::ScissorRectDesc scissor;
    scissor.maxX = 1280;
    scissor.maxY = 720;
    EXPECT_TRUE(RHI::ValidateScissorRectDesc(scissor));

    scissor.maxY = 0;
    RHI::ScissorDescriptorValidation scissorValidation = RHI::ValidateScissorRectDesc(scissor);
    EXPECT_EQ(scissorValidation.error, RHI::ScissorDescriptorError::EmptyScissor);
}

TEST(RHITypesTest, ResourceUsageFlagsComposeAndQuery) {
    RHI::ResourceUsageFlags flags =
        RHI::ResourceUsage::ShaderRead | RHI::ResourceUsage::CopyDestination;
    flags = flags | RHI::ResourceUsage::RenderTarget;

    EXPECT_TRUE(RHI::HasResourceUsage(flags, RHI::ResourceUsage::ShaderRead));
    EXPECT_TRUE(RHI::HasResourceUsage(flags, RHI::ResourceUsage::CopyDestination));
    EXPECT_TRUE(RHI::HasResourceUsage(flags, RHI::ResourceUsage::RenderTarget));
    EXPECT_FALSE(RHI::HasResourceUsage(flags, RHI::ResourceUsage::DepthStencil));
}

TEST(RHITypesTest, ResourceUsageFlagWrapsSingleEnumValue) {
    const RHI::ResourceUsageFlags flags = RHI::ResourceUsageFlag(RHI::ResourceUsage::CopySource);

    EXPECT_TRUE(RHI::HasResourceUsage(flags, RHI::ResourceUsage::CopySource));
    EXPECT_FALSE(RHI::HasResourceUsage(flags, RHI::ResourceUsage::CopyDestination));
}

TEST(RHITypesTest, ColorWriteMaskFlagsComposeAndQuery) {
    RHI::ColorWriteMaskFlags flags = RHI::ColorWriteMask::Red | RHI::ColorWriteMask::Blue;
    flags = flags | RHI::ColorWriteMask::Alpha;

    EXPECT_TRUE(RHI::HasColorWriteMask(flags, RHI::ColorWriteMask::Red));
    EXPECT_FALSE(RHI::HasColorWriteMask(flags, RHI::ColorWriteMask::Green));
    EXPECT_TRUE(RHI::HasColorWriteMask(flags, RHI::ColorWriteMask::Blue));
    EXPECT_TRUE(RHI::HasColorWriteMask(flags, RHI::ColorWriteMask::Alpha));
    EXPECT_EQ(RHI::ColorWriteMaskFlag(RHI::ColorWriteMask::All), 0x0fu);
}

TEST(RHITypesTest, QueueClassFlagsComposeAndQuery) {
    RHI::QueueClassFlags flags = RHI::QueueClass::Graphics | RHI::QueueClass::Copy;

    EXPECT_TRUE(RHI::HasQueueClass(flags, RHI::QueueClass::Graphics));
    EXPECT_TRUE(RHI::HasQueueClass(flags, RHI::QueueClass::Copy));
    EXPECT_FALSE(RHI::HasQueueClass(flags, RHI::QueueClass::Compute));
    EXPECT_FALSE(RHI::HasQueueClass(flags, static_cast<RHI::QueueClass>(255)));

    flags = flags | RHI::QueueClass::Compute;
    EXPECT_TRUE(RHI::HasQueueClass(flags, RHI::QueueClass::Compute));
}

TEST(RHITypesTest, DeviceFeaturesExposeQueueClassCapabilities) {
    RHI::DeviceFeatures features;
    features.supportedQueueClasses = RHI::QueueClass::Graphics | RHI::QueueClass::Copy;
    features.dedicatedQueueClasses = RHI::QueueClassFlag(RHI::QueueClass::Graphics);

    EXPECT_STREQ(RHI::ArgumentBufferTierName(RHI::ArgumentBufferTier::Unsupported), "unsupported");
    EXPECT_STREQ(RHI::ArgumentBufferTierName(RHI::ArgumentBufferTier::Tier1), "tier1");
    EXPECT_STREQ(RHI::ArgumentBufferTierName(RHI::ArgumentBufferTier::Tier2), "tier2");
    EXPECT_STREQ(RHI::ArgumentBufferTierName(static_cast<RHI::ArgumentBufferTier>(255)), "unsupported");
    EXPECT_FALSE(features.SupportsArgumentBufferTier(RHI::ArgumentBufferTier::Tier1));

    EXPECT_TRUE(features.SupportsQueueClass(RHI::QueueClass::Graphics));
    EXPECT_TRUE(features.SupportsQueueClass(RHI::QueueClass::Copy));
    EXPECT_FALSE(features.SupportsQueueClass(RHI::QueueClass::Compute));
    EXPECT_TRUE(features.HasDedicatedQueueClass(RHI::QueueClass::Graphics));
    EXPECT_FALSE(features.HasDedicatedQueueClass(RHI::QueueClass::Copy));

    features.argumentBufferTier = RHI::ArgumentBufferTier::Tier2;
    EXPECT_TRUE(features.SupportsArgumentBufferTier(RHI::ArgumentBufferTier::Tier1));
    EXPECT_TRUE(features.SupportsArgumentBufferTier(RHI::ArgumentBufferTier::Tier2));
}

TEST(RHITypesTest, ResourceDescriptorsExposeUsageAndInitialState) {
    RHI::BufferDesc buffer;
    buffer.sizeBytes = 4096;
    buffer.usage = RHI::ResourceUsage::VertexBuffer | RHI::ResourceUsage::CopyDestination;
    buffer.initialState = RHI::ResourceState::CopyDestination;

    EXPECT_EQ(buffer.sizeBytes, 4096u);
    EXPECT_TRUE(RHI::HasResourceUsage(buffer.usage, RHI::ResourceUsage::VertexBuffer));
    EXPECT_EQ(buffer.initialState, RHI::ResourceState::CopyDestination);

    RHI::TextureDesc texture;
    texture.extent = RHI::Extent2D{128, 64};
    texture.format = RHI::Format::RGBA8Unorm;
    texture.usage = RHI::ResourceUsage::ShaderRead | RHI::ResourceUsage::CopyDestination;
    texture.initialState = RHI::ResourceState::ShaderRead;

    EXPECT_EQ(texture.extent.width, 128u);
    EXPECT_EQ(texture.extent.height, 64u);
    EXPECT_EQ(texture.format, RHI::Format::RGBA8Unorm);
    EXPECT_EQ(texture.sampleCount, 1u);
    EXPECT_TRUE(RHI::HasResourceUsage(texture.usage, RHI::ResourceUsage::ShaderRead));
    EXPECT_EQ(texture.initialState, RHI::ResourceState::ShaderRead);
}

TEST(RHITypesTest, BufferDescriptorValidationRejectsEmptyAndUnsupportedInitialState) {
    RHI::BufferDesc buffer;
    buffer.sizeBytes = 4096;
    buffer.usage = RHI::ResourceUsage::VertexBuffer | RHI::ResourceUsage::CopyDestination;
    buffer.initialState = RHI::ResourceState::CopyDestination;

    EXPECT_TRUE(RHI::ValidateBufferDesc(buffer));

    buffer.sizeBytes = 0;
    RHI::ResourceDescriptorValidation validation = RHI::ValidateBufferDesc(buffer);
    EXPECT_EQ(validation.error, RHI::ResourceDescriptorError::EmptyBuffer);
    EXPECT_EQ(validation.initialState, RHI::ResourceState::CopyDestination);

    buffer.sizeBytes = 4096;
    buffer.memory = static_cast<RHI::ResourceMemory>(255);
    validation = RHI::ValidateBufferDesc(buffer);
    EXPECT_EQ(validation.error, RHI::ResourceDescriptorError::UnsupportedMemory);
    EXPECT_EQ(validation.memory, static_cast<RHI::ResourceMemory>(255));

    buffer.memory = RHI::ResourceMemory::DeviceLocal;
    buffer.usage = RHI::ResourceUsageFlag(RHI::ResourceUsage::None);
    buffer.initialState = RHI::ResourceState::Common;
    validation = RHI::ValidateBufferDesc(buffer);
    EXPECT_EQ(validation.error, RHI::ResourceDescriptorError::MissingUsage);
    EXPECT_EQ(validation.initialState, RHI::ResourceState::Common);

    buffer.usage = RHI::ResourceUsage::VertexBuffer | RHI::ResourceUsage::CopyDestination;
    buffer.initialState = RHI::ResourceState::ShaderRead;
    validation = RHI::ValidateBufferDesc(buffer);
    EXPECT_EQ(validation.error, RHI::ResourceDescriptorError::UnsupportedInitialState);
    EXPECT_EQ(validation.initialState, RHI::ResourceState::ShaderRead);

    buffer.usage = RHI::ResourceUsageFlag(RHI::ResourceUsage::RenderTarget);
    buffer.initialState = RHI::ResourceState::RenderTarget;
    validation = RHI::ValidateBufferDesc(buffer);
    EXPECT_EQ(validation.error, RHI::ResourceDescriptorError::UnsupportedInitialState);
    EXPECT_EQ(validation.initialState, RHI::ResourceState::RenderTarget);
}

TEST(RHITypesTest, TextureDescriptorValidationRejectsInvalidShapeFormatAndInitialState) {
    RHI::TextureDesc texture;
    texture.extent = RHI::Extent2D{128, 64};
    texture.format = RHI::Format::RGBA8Unorm;
    texture.usage = RHI::ResourceUsage::ShaderRead | RHI::ResourceUsage::CopyDestination;
    texture.initialState = RHI::ResourceState::CopyDestination;

    EXPECT_TRUE(RHI::ValidateTextureDesc(texture));

    texture.extent.width = 0;
    RHI::ResourceDescriptorValidation validation = RHI::ValidateTextureDesc(texture);
    EXPECT_EQ(validation.error, RHI::ResourceDescriptorError::EmptyTextureExtent);

    texture.extent = RHI::Extent2D{128, 64};
    texture.format = RHI::Format::Unknown;
    validation = RHI::ValidateTextureDesc(texture);
    EXPECT_EQ(validation.error, RHI::ResourceDescriptorError::UnknownTextureFormat);

    texture.format = RHI::Format::RGBA8Unorm;
    texture.memory = static_cast<RHI::ResourceMemory>(255);
    validation = RHI::ValidateTextureDesc(texture);
    EXPECT_EQ(validation.error, RHI::ResourceDescriptorError::UnsupportedMemory);
    EXPECT_EQ(validation.memory, static_cast<RHI::ResourceMemory>(255));

    texture.memory = RHI::ResourceMemory::DeviceLocal;
    texture.usage = RHI::ResourceUsageFlag(RHI::ResourceUsage::None);
    texture.initialState = RHI::ResourceState::Common;
    validation = RHI::ValidateTextureDesc(texture);
    EXPECT_EQ(validation.error, RHI::ResourceDescriptorError::MissingUsage);
    EXPECT_EQ(validation.initialState, RHI::ResourceState::Common);

    texture.usage = RHI::ResourceUsage::ShaderRead | RHI::ResourceUsage::CopyDestination;
    texture.initialState = RHI::ResourceState::CopyDestination;
    texture.extent = RHI::Extent2D{std::numeric_limits<uint32_t>::max(),
                                   std::numeric_limits<uint32_t>::max()};
    validation = RHI::ValidateTextureDesc(texture);
    EXPECT_EQ(validation.error, RHI::ResourceDescriptorError::TextureSizeOverflow);

    texture.extent = RHI::Extent2D{128, 64};
    texture.sampleCount = 3;
    validation = RHI::ValidateTextureDesc(texture);
    EXPECT_EQ(validation.error, RHI::ResourceDescriptorError::UnsupportedSampleCount);

    texture.sampleCount = 4;
    texture.usage = RHI::ResourceUsageFlag(RHI::ResourceUsage::ShaderRead);
    texture.initialState = RHI::ResourceState::ShaderRead;
    validation = RHI::ValidateTextureDesc(texture);
    EXPECT_EQ(validation.error, RHI::ResourceDescriptorError::UnsupportedSampleCount);

    texture.usage = RHI::ResourceUsageFlag(RHI::ResourceUsage::RenderTarget);
    texture.initialState = RHI::ResourceState::RenderTarget;
    texture.memory = RHI::ResourceMemory::Shared;
    validation = RHI::ValidateTextureDesc(texture);
    EXPECT_EQ(validation.error, RHI::ResourceDescriptorError::UnsupportedMemory);
    EXPECT_EQ(validation.memory, RHI::ResourceMemory::Shared);

    texture.memory = RHI::ResourceMemory::DeviceLocal;
    EXPECT_TRUE(RHI::ValidateTextureDesc(texture));

    texture.sampleCount = 1;
    texture.extent = RHI::Extent2D{128, 64};
    texture.format = RHI::Format::Depth32Float;
    texture.usage = RHI::ResourceUsageFlag(RHI::ResourceUsage::RenderTarget);
    texture.initialState = RHI::ResourceState::RenderTarget;
    validation = RHI::ValidateTextureDesc(texture);
    EXPECT_EQ(validation.error, RHI::ResourceDescriptorError::UnsupportedRenderTargetFormat);
    EXPECT_EQ(validation.initialState, RHI::ResourceState::RenderTarget);

    texture.format = RHI::Format::RGBA8Unorm;
    texture.usage = RHI::ResourceUsageFlag(RHI::ResourceUsage::DepthStencil);
    texture.initialState = RHI::ResourceState::DepthWrite;
    validation = RHI::ValidateTextureDesc(texture);
    EXPECT_EQ(validation.error, RHI::ResourceDescriptorError::UnsupportedDepthStencilFormat);
    EXPECT_EQ(validation.initialState, RHI::ResourceState::DepthWrite);

    texture.format = RHI::Format::Depth32FloatStencil8;
    validation = RHI::ValidateTextureDesc(texture);
    EXPECT_TRUE(validation);

    texture.usage = RHI::ResourceUsage::ShaderRead | RHI::ResourceUsage::CopyDestination;
    texture.initialState = RHI::ResourceState::DepthWrite;
    validation = RHI::ValidateTextureDesc(texture);
    EXPECT_EQ(validation.error, RHI::ResourceDescriptorError::UnsupportedInitialState);
    EXPECT_EQ(validation.initialState, RHI::ResourceState::DepthWrite);

    texture.usage = RHI::ResourceUsageFlag(RHI::ResourceUsage::VertexBuffer);
    texture.initialState = RHI::ResourceState::VertexBuffer;
    validation = RHI::ValidateTextureDesc(texture);
    EXPECT_EQ(validation.error, RHI::ResourceDescriptorError::UnsupportedInitialState);
    EXPECT_EQ(validation.initialState, RHI::ResourceState::VertexBuffer);
}

TEST(RHITypesTest, TextureDescriptorEstimatedBytesUsesFormatAndExtent) {
    RHI::TextureDesc texture;
    texture.extent = RHI::Extent2D{128, 64};
    texture.format = RHI::Format::RGBA8Unorm;

    uint64_t estimatedBytes = 0;
    EXPECT_TRUE(RHI::TextureDescEstimatedBytes(texture, estimatedBytes));
    EXPECT_EQ(estimatedBytes, 32768u);

    texture.sampleCount = 4;
    EXPECT_TRUE(RHI::TextureDescEstimatedBytes(texture, estimatedBytes));
    EXPECT_EQ(estimatedBytes, 131072u);

    texture.sampleCount = 3;
    EXPECT_FALSE(RHI::TextureDescEstimatedBytes(texture, estimatedBytes));
    EXPECT_EQ(estimatedBytes, 0u);

    texture.sampleCount = 1;
    estimatedBytes = 99;
    texture.format = RHI::Format::Unknown;
    EXPECT_FALSE(RHI::TextureDescEstimatedBytes(texture, estimatedBytes));
    EXPECT_EQ(estimatedBytes, 0u);

    texture.format = RHI::Format::RGBA8Unorm;
    texture.extent.width = 0;
    EXPECT_FALSE(RHI::TextureDescEstimatedBytes(texture, estimatedBytes));
    EXPECT_EQ(estimatedBytes, 0u);

    texture.extent = RHI::Extent2D{std::numeric_limits<uint32_t>::max(),
                                   std::numeric_limits<uint32_t>::max()};
    EXPECT_FALSE(RHI::TextureDescEstimatedBytes(texture, estimatedBytes));
    EXPECT_EQ(estimatedBytes, 0u);
}

TEST(RHITypesTest, ResourcePoolStatsTrackLiveAndPeakUsage) {
    RHI::ResourcePoolStats stats;
    stats.resourceType = RHI::ResourceType::Buffer;

    RHI::RecordResourcePoolAllocation(stats, RHI::ResourceMemory::DeviceLocal, 4096);
    EXPECT_EQ(stats.resourceType, RHI::ResourceType::Buffer);
    EXPECT_EQ(stats.liveResourceCount, 1u);
    EXPECT_EQ(stats.liveBytes, 4096u);
    EXPECT_EQ(stats.peakResourceCount, 1u);
    EXPECT_EQ(stats.peakBytes, 4096u);
    ASSERT_NE(RHI::FindResourcePoolMemoryStats(stats, RHI::ResourceMemory::DeviceLocal), nullptr);
    EXPECT_EQ(RHI::FindResourcePoolMemoryStats(stats, RHI::ResourceMemory::DeviceLocal)->liveBytes, 4096u);
    EXPECT_EQ(RHI::FindResourcePoolMemoryStats(stats, RHI::ResourceMemory::Shared)->liveBytes, 0u);

    RHI::RecordResourcePoolAllocation(stats, RHI::ResourceMemory::Shared, 1024);
    EXPECT_EQ(stats.liveResourceCount, 2u);
    EXPECT_EQ(stats.liveBytes, 5120u);
    EXPECT_EQ(stats.peakResourceCount, 2u);
    EXPECT_EQ(stats.peakBytes, 5120u);
    EXPECT_EQ(RHI::FindResourcePoolMemoryStats(stats, RHI::ResourceMemory::Shared)->liveResourceCount, 1u);
    EXPECT_EQ(RHI::FindResourcePoolMemoryStats(stats, RHI::ResourceMemory::Shared)->liveBytes, 1024u);

    RHI::RecordResourcePoolRelease(stats, RHI::ResourceMemory::DeviceLocal, 4096);
    EXPECT_EQ(stats.liveResourceCount, 1u);
    EXPECT_EQ(stats.liveBytes, 1024u);
    EXPECT_EQ(stats.peakResourceCount, 2u);
    EXPECT_EQ(stats.peakBytes, 5120u);
    EXPECT_EQ(RHI::FindResourcePoolMemoryStats(stats, RHI::ResourceMemory::DeviceLocal)->liveBytes, 0u);
    EXPECT_EQ(RHI::FindResourcePoolMemoryStats(stats, RHI::ResourceMemory::DeviceLocal)->peakBytes, 4096u);

    RHI::RecordResourcePoolRelease(stats, RHI::ResourceMemory::Shared, 2048);
    EXPECT_EQ(stats.liveResourceCount, 0u);
    EXPECT_EQ(stats.liveBytes, 0u);
    EXPECT_EQ(stats.peakResourceCount, 2u);
    EXPECT_EQ(stats.peakBytes, 5120u);
    EXPECT_EQ(RHI::FindResourcePoolMemoryStats(stats, RHI::ResourceMemory::Shared)->liveBytes, 0u);
    EXPECT_EQ(RHI::FindResourcePoolMemoryStats(stats, static_cast<RHI::ResourceMemory>(255)), nullptr);
}

TEST(RHITypesTest, ResourcePoolStatsSupportOptionalMemoryBudgets) {
    RHI::ResourcePoolStats stats;
    stats.resourceType = RHI::ResourceType::Buffer;
    uint64_t remainingBytes = 99;

    EXPECT_TRUE(RHI::ResourcePoolCanAllocate(stats, RHI::ResourceMemory::DeviceLocal, 128));
    EXPECT_FALSE(RHI::ResourcePoolMemoryBudgetRemaining(stats,
                                                        RHI::ResourceMemory::DeviceLocal,
                                                        remainingBytes));
    EXPECT_EQ(remainingBytes, 0u);
    EXPECT_FALSE(RHI::ResourcePoolMemoryIsOverBudget(stats, RHI::ResourceMemory::DeviceLocal));
    EXPECT_FALSE(RHI::ResourcePoolCanAllocate(stats, static_cast<RHI::ResourceMemory>(255), 128));
    EXPECT_FALSE(RHI::SetResourcePoolMemoryBudget(stats, static_cast<RHI::ResourceMemory>(255), 1024));

    EXPECT_TRUE(RHI::SetResourcePoolMemoryBudget(stats, RHI::ResourceMemory::Shared, 4096));
    ASSERT_NE(RHI::FindResourcePoolMemoryStats(stats, RHI::ResourceMemory::Shared), nullptr);
    EXPECT_EQ(RHI::FindResourcePoolMemoryStats(stats, RHI::ResourceMemory::Shared)->budgetBytes, 4096u);
    EXPECT_TRUE(RHI::ResourcePoolCanAllocate(stats, RHI::ResourceMemory::Shared, 4096));
    EXPECT_TRUE(RHI::ResourcePoolMemoryBudgetRemaining(stats, RHI::ResourceMemory::Shared, remainingBytes));
    EXPECT_EQ(remainingBytes, 4096u);

    RHI::RecordResourcePoolAllocation(stats, RHI::ResourceMemory::Shared, 3072);
    EXPECT_TRUE(RHI::ResourcePoolCanAllocate(stats, RHI::ResourceMemory::Shared, 1024));
    EXPECT_FALSE(RHI::ResourcePoolCanAllocate(stats, RHI::ResourceMemory::Shared, 1025));
    EXPECT_TRUE(RHI::ResourcePoolMemoryBudgetRemaining(stats, RHI::ResourceMemory::Shared, remainingBytes));
    EXPECT_EQ(remainingBytes, 1024u);
    EXPECT_FALSE(RHI::ResourcePoolMemoryIsOverBudget(stats, RHI::ResourceMemory::Shared));

    RHI::RecordResourcePoolAllocation(stats, RHI::ResourceMemory::DeviceLocal, 512);
    EXPECT_TRUE(RHI::ResourcePoolCanAllocate(stats, RHI::ResourceMemory::DeviceLocal, 4096));

    RHI::RecordResourcePoolAllocation(stats, RHI::ResourceMemory::Shared, 2048);
    EXPECT_FALSE(RHI::ResourcePoolCanAllocate(stats, RHI::ResourceMemory::Shared, 1));
    EXPECT_TRUE(RHI::ResourcePoolMemoryBudgetRemaining(stats, RHI::ResourceMemory::Shared, remainingBytes));
    EXPECT_EQ(remainingBytes, 0u);
    EXPECT_TRUE(RHI::ResourcePoolMemoryIsOverBudget(stats, RHI::ResourceMemory::Shared));

    stats.upload.liveBytes = std::numeric_limits<uint64_t>::max() - 4u;
    EXPECT_FALSE(RHI::ResourcePoolCanAllocate(stats, RHI::ResourceMemory::Upload, 8));
}

TEST(RHITypesTest, ResourcePoolStatsTrackAllocationFailuresByMemoryBucket) {
    RHI::ResourcePoolStats stats;
    stats.resourceType = RHI::ResourceType::Texture;

    RHI::RecordResourcePoolAllocationFailure(stats, RHI::ResourceMemory::DeviceLocal, 4096);
    EXPECT_EQ(stats.failedAllocationCount, 1u);
    EXPECT_EQ(stats.failedAllocationBytes, 4096u);
    EXPECT_EQ(stats.deviceLocal.failedAllocationCount, 1u);
    EXPECT_EQ(stats.deviceLocal.failedAllocationBytes, 4096u);
    EXPECT_EQ(stats.shared.failedAllocationCount, 0u);

    RHI::RecordResourcePoolAllocationFailure(stats, RHI::ResourceMemory::Shared, 1024);
    EXPECT_EQ(stats.failedAllocationCount, 2u);
    EXPECT_EQ(stats.failedAllocationBytes, 5120u);
    EXPECT_EQ(stats.shared.failedAllocationCount, 1u);
    EXPECT_EQ(stats.shared.failedAllocationBytes, 1024u);

    stats.failedAllocationBytes = std::numeric_limits<uint64_t>::max() - 4u;
    RHI::RecordResourcePoolAllocationFailure(stats, RHI::ResourceMemory::DeviceLocal, 8);
    EXPECT_EQ(stats.failedAllocationBytes, std::numeric_limits<uint64_t>::max());

    stats.failedAllocationBytes = 5120u;
    stats.upload.failedAllocationBytes = std::numeric_limits<uint64_t>::max() - 4u;
    RHI::RecordResourcePoolAllocationFailure(stats, RHI::ResourceMemory::Upload, 8);
    EXPECT_EQ(stats.upload.failedAllocationBytes, std::numeric_limits<uint64_t>::max());

    const uint64_t failedCount = stats.failedAllocationCount;
    RHI::RecordResourcePoolAllocationFailure(stats, static_cast<RHI::ResourceMemory>(255), 64);
    EXPECT_EQ(stats.failedAllocationCount, failedCount + 1u);
    EXPECT_EQ(stats.failedAllocationBytes, 5192u);
}

TEST(RHITypesTest, ResourcePoolStatsSaturateByteOverflow) {
    RHI::ResourcePoolStats stats;
    stats.resourceType = RHI::ResourceType::Texture;
    stats.liveBytes = std::numeric_limits<uint64_t>::max() - 8u;
    stats.upload.liveBytes = std::numeric_limits<uint64_t>::max() - 8u;

    RHI::RecordResourcePoolAllocation(stats, RHI::ResourceMemory::Upload, 16);
    EXPECT_EQ(stats.resourceType, RHI::ResourceType::Texture);
    EXPECT_EQ(stats.liveResourceCount, 1u);
    EXPECT_EQ(stats.liveBytes, std::numeric_limits<uint64_t>::max());
    EXPECT_EQ(stats.peakResourceCount, 1u);
    EXPECT_EQ(stats.peakBytes, std::numeric_limits<uint64_t>::max());
    EXPECT_EQ(stats.upload.liveResourceCount, 1u);
    EXPECT_EQ(stats.upload.liveBytes, std::numeric_limits<uint64_t>::max());
    EXPECT_EQ(stats.upload.peakBytes, std::numeric_limits<uint64_t>::max());
}

TEST(RHITypesTest, SamplerDescriptorDefaultsMatchMaterialSampling) {
    RHI::SamplerDesc sampler;

    EXPECT_EQ(sampler.minFilter, RHI::SamplerFilter::Linear);
    EXPECT_EQ(sampler.magFilter, RHI::SamplerFilter::Linear);
    EXPECT_EQ(sampler.mipFilter, RHI::SamplerMipFilter::NotMipmapped);
    EXPECT_EQ(sampler.addressU, RHI::SamplerAddressMode::Repeat);
    EXPECT_EQ(sampler.addressV, RHI::SamplerAddressMode::Repeat);
    EXPECT_EQ(sampler.addressW, RHI::SamplerAddressMode::Repeat);
    EXPECT_EQ(sampler.borderColor, RHI::SamplerBorderColor::OpaqueBlack);
    EXPECT_EQ(sampler.compareFunction, RHI::CompareFunction::Never);
    EXPECT_FALSE(sampler.anisotropyEnabled);
    EXPECT_EQ(sampler.maxAnisotropy, 1u);
    EXPECT_FLOAT_EQ(sampler.minLod, 0.0f);
    EXPECT_FLOAT_EQ(sampler.maxLod, 1000.0f);
    EXPECT_FLOAT_EQ(sampler.mipLodBias, 0.0f);

    sampler.addressV = RHI::SamplerAddressMode::ClampToEdge;
    sampler.borderColor = RHI::SamplerBorderColor::OpaqueWhite;
    sampler.compareFunction = RHI::CompareFunction::LessEqual;
    sampler.anisotropyEnabled = true;
    sampler.maxAnisotropy = 4;
    sampler.mipLodBias = -1.5f;
    EXPECT_EQ(sampler.addressV, RHI::SamplerAddressMode::ClampToEdge);
    EXPECT_EQ(sampler.borderColor, RHI::SamplerBorderColor::OpaqueWhite);
    EXPECT_EQ(sampler.compareFunction, RHI::CompareFunction::LessEqual);
    EXPECT_TRUE(sampler.anisotropyEnabled);
    EXPECT_EQ(sampler.maxAnisotropy, 4u);
    EXPECT_FLOAT_EQ(sampler.mipLodBias, -1.5f);
}

TEST(RHITypesTest, SamplerDescriptorEqualityIgnoresDebugName) {
    RHI::SamplerDesc sampler;
    RHI::SamplerDesc matching = sampler;

    EXPECT_TRUE(RHI::SamplerDescEquals(sampler, matching));

    matching.debugName = "cache labels are not sampler state";
    EXPECT_TRUE(RHI::SamplerDescEquals(sampler, matching));

    matching.maxAnisotropy = 4;
    EXPECT_FALSE(RHI::SamplerDescEquals(sampler, matching));

    matching = sampler;
    matching.addressU = RHI::SamplerAddressMode::ClampToEdge;
    EXPECT_FALSE(RHI::SamplerDescEquals(sampler, matching));

    matching = sampler;
    matching.mipLodBias = 0.5f;
    EXPECT_FALSE(RHI::SamplerDescEquals(sampler, matching));

    sampler.minLod = std::numeric_limits<float>::quiet_NaN();
    sampler.maxLod = std::numeric_limits<float>::quiet_NaN();
    sampler.mipLodBias = std::numeric_limits<float>::quiet_NaN();
    matching = sampler;
    EXPECT_TRUE(RHI::SamplerDescEquals(sampler, matching));

    matching.mipLodBias = 0.0f;
    EXPECT_FALSE(RHI::SamplerDescEquals(sampler, matching));
}

TEST(RHITypesTest, GraphicsPipelineDescriptorFactoryBuildsDemoForwardShape) {
    const RHI::GraphicsPipelineDesc pipeline =
        RHI::MakeGraphicsPipelineDesc("vertex_main",
                                      "fragment_main",
                                      RHI::Format::BGRA8Unorm,
                                      RHI::Format::Depth32Float,
                                      "demo");

    EXPECT_STREQ(pipeline.debugName, "demo");
    EXPECT_EQ(pipeline.vertexShader.stage, RHI::ShaderStage::Vertex);
    EXPECT_STREQ(pipeline.vertexShader.entryPoint, "vertex_main");
    EXPECT_EQ(pipeline.fragmentShader.stage, RHI::ShaderStage::Fragment);
    EXPECT_STREQ(pipeline.fragmentShader.entryPoint, "fragment_main");
    EXPECT_EQ(pipeline.vertexInput.bufferCount, 0u);
    EXPECT_EQ(pipeline.vertexInput.attributeCount, 0u);
    EXPECT_EQ(pipeline.colorAttachmentCount, 1u);
    EXPECT_EQ(pipeline.colorAttachments[0].format, RHI::Format::BGRA8Unorm);
    EXPECT_FALSE(pipeline.colorAttachments[0].blendEnabled);
    EXPECT_EQ(pipeline.colorAttachments[0].sourceColorBlendFactor, RHI::BlendFactor::One);
    EXPECT_EQ(pipeline.colorAttachments[0].destinationColorBlendFactor, RHI::BlendFactor::Zero);
    EXPECT_EQ(pipeline.colorAttachments[0].colorBlendOperation, RHI::BlendOperation::Add);
    EXPECT_EQ(pipeline.colorAttachments[0].sourceAlphaBlendFactor, RHI::BlendFactor::One);
    EXPECT_EQ(pipeline.colorAttachments[0].destinationAlphaBlendFactor, RHI::BlendFactor::Zero);
    EXPECT_EQ(pipeline.colorAttachments[0].alphaBlendOperation, RHI::BlendOperation::Add);
    EXPECT_EQ(pipeline.colorAttachments[0].writeMask,
              RHI::ColorWriteMaskFlag(RHI::ColorWriteMask::All));
    EXPECT_EQ(pipeline.multisampleState.sampleCount, 1u);
    EXPECT_FALSE(pipeline.multisampleState.alphaToCoverageEnabled);
    EXPECT_EQ(pipeline.depthStencilFormat, RHI::Format::Depth32Float);
    EXPECT_TRUE(pipeline.depthStencilState.depthTestEnabled);
    EXPECT_TRUE(pipeline.depthStencilState.depthWriteEnabled);
    EXPECT_EQ(pipeline.depthStencilState.depthCompare, RHI::CompareFunction::Less);
    EXPECT_FALSE(pipeline.depthStencilState.stencilTestEnabled);
    EXPECT_EQ(pipeline.depthStencilState.stencilReadMask, 0xffu);
    EXPECT_EQ(pipeline.depthStencilState.stencilWriteMask, 0xffu);
    EXPECT_EQ(pipeline.depthStencilState.frontStencil.compare, RHI::CompareFunction::Always);
    EXPECT_EQ(pipeline.depthStencilState.frontStencil.stencilFailOp, RHI::StencilOperation::Keep);
    EXPECT_EQ(pipeline.depthStencilState.frontStencil.depthFailOp, RHI::StencilOperation::Keep);
    EXPECT_EQ(pipeline.depthStencilState.frontStencil.passOp, RHI::StencilOperation::Keep);
    EXPECT_EQ(pipeline.rasterState.primitiveTopology, RHI::PrimitiveTopology::TriangleList);
    EXPECT_EQ(pipeline.rasterState.fillMode, RHI::FillMode::Solid);
    EXPECT_EQ(pipeline.rasterState.depthBias, 0);
    EXPECT_FLOAT_EQ(pipeline.rasterState.depthBiasClamp, 0.0f);
    EXPECT_FLOAT_EQ(pipeline.rasterState.depthBiasSlopeScale, 0.0f);
    EXPECT_TRUE(pipeline.rasterState.depthClipEnabled);
    EXPECT_TRUE(RHI::ValidateGraphicsPipelineDesc(pipeline));
}

TEST(RHITypesTest, SwapchainDescriptorValidationReportsPreciseErrors) {
    RHI::SwapchainDesc swapchain;
    swapchain.drawableSize = RHI::Extent2D{1280, 720};
    swapchain.colorFormat = RHI::Format::BGRA8Unorm;
    swapchain.depthFormat = RHI::Format::Depth32Float;

    EXPECT_TRUE(RHI::ValidateSwapchainDesc(swapchain));

    swapchain.drawableSize.width = 0;
    RHI::SwapchainDescriptorValidation validation = RHI::ValidateSwapchainDesc(swapchain);
    EXPECT_EQ(validation.error, RHI::SwapchainDescriptorError::EmptyDrawableSize);

    swapchain.drawableSize = RHI::Extent2D{1280, 720};
    swapchain.colorFormat = RHI::Format::Depth32Float;
    validation = RHI::ValidateSwapchainDesc(swapchain);
    EXPECT_EQ(validation.error, RHI::SwapchainDescriptorError::UnsupportedColorFormat);
    EXPECT_EQ(validation.format, RHI::Format::Depth32Float);

    swapchain.colorFormat = RHI::Format::RGBA8Unorm;
    swapchain.depthFormat = RHI::Format::Unknown;
    validation = RHI::ValidateSwapchainDesc(swapchain);
    EXPECT_EQ(validation.error, RHI::SwapchainDescriptorError::UnsupportedDepthFormat);
    EXPECT_EQ(validation.format, RHI::Format::Unknown);
}

TEST(RHITypesTest, RenderPassDescriptorValidationReportsPreciseErrors) {
    RHI::RenderPassDesc pass;
    RHI::RenderPassDescriptorValidation validation = RHI::ValidateRenderPassDesc(pass);
    EXPECT_EQ(validation.error, RHI::RenderPassDescriptorError::MissingAttachment);

    pass.colorAttachmentCount = 1;
    pass.colorAttachments[0].format = RHI::Format::BGRA8Unorm;
    pass.colorAttachments[0].loadAction = RHI::AttachmentLoadAction::Clear;
    pass.colorAttachments[0].storeAction = RHI::AttachmentStoreAction::Store;
    EXPECT_TRUE(RHI::ValidateRenderPassDesc(pass));

    pass.colorAttachments[0].format = RHI::Format::Depth32Float;
    validation = RHI::ValidateRenderPassDesc(pass);
    EXPECT_EQ(validation.error, RHI::RenderPassDescriptorError::UnsupportedColorFormat);
    EXPECT_EQ(validation.attachmentIndex, 0u);
    EXPECT_EQ(validation.format, RHI::Format::Depth32Float);

    pass.colorAttachments[0].format = RHI::Format::RGBA8Unorm;
    pass.colorAttachments[0].loadAction = static_cast<RHI::AttachmentLoadAction>(255);
    validation = RHI::ValidateRenderPassDesc(pass);
    EXPECT_EQ(validation.error, RHI::RenderPassDescriptorError::InvalidLoadAction);
    EXPECT_EQ(validation.attachmentIndex, 0u);

    pass.colorAttachments[0].loadAction = RHI::AttachmentLoadAction::Load;
    pass.colorAttachments[0].storeAction = static_cast<RHI::AttachmentStoreAction>(255);
    validation = RHI::ValidateRenderPassDesc(pass);
    EXPECT_EQ(validation.error, RHI::RenderPassDescriptorError::InvalidStoreAction);
    EXPECT_EQ(validation.attachmentIndex, 0u);

    pass.colorAttachments[0].storeAction = RHI::AttachmentStoreAction::Store;
    pass.colorAttachmentCount = static_cast<uint32_t>(RHI::kMaxRenderPassColorAttachments + 1);
    validation = RHI::ValidateRenderPassDesc(pass);
    EXPECT_EQ(validation.error, RHI::RenderPassDescriptorError::TooManyColorAttachments);

    pass.colorAttachmentCount = 0;
    pass.hasDepthStencil = true;
    pass.depthStencilAttachment.format = RHI::Format::Depth32Float;
    pass.depthStencilAttachment.loadAction = RHI::AttachmentLoadAction::Clear;
    pass.depthStencilAttachment.storeAction = RHI::AttachmentStoreAction::DontCare;
    pass.depthStencilAttachment.clearDepth = 1.0;
    EXPECT_TRUE(RHI::ValidateRenderPassDesc(pass));

    pass.depthStencilAttachment.format = RHI::Format::RGBA8Unorm;
    validation = RHI::ValidateRenderPassDesc(pass);
    EXPECT_EQ(validation.error, RHI::RenderPassDescriptorError::UnsupportedDepthFormat);
    EXPECT_EQ(validation.attachmentIndex, 0u);
    EXPECT_EQ(validation.format, RHI::Format::RGBA8Unorm);

    pass.depthStencilAttachment.format = RHI::Format::Depth32Float;
    pass.depthStencilAttachment.clearDepth = 1.5;
    validation = RHI::ValidateRenderPassDesc(pass);
    EXPECT_EQ(validation.error, RHI::RenderPassDescriptorError::InvalidDepthClearValue);
    EXPECT_EQ(validation.attachmentIndex, 0u);

    pass.depthStencilAttachment.format = RHI::Format::Depth32FloatStencil8;
    pass.depthStencilAttachment.clearDepth = 0.25;
    pass.depthStencilAttachment.stencilLoadAction = RHI::AttachmentLoadAction::Clear;
    pass.depthStencilAttachment.stencilStoreAction = RHI::AttachmentStoreAction::Store;
    pass.depthStencilAttachment.clearStencil = 7;
    EXPECT_TRUE(RHI::ValidateRenderPassDesc(pass));

    pass.depthStencilAttachment.stencilLoadAction = static_cast<RHI::AttachmentLoadAction>(255);
    validation = RHI::ValidateRenderPassDesc(pass);
    EXPECT_EQ(validation.error, RHI::RenderPassDescriptorError::InvalidLoadAction);
    EXPECT_EQ(validation.attachmentIndex, 0u);

    pass.depthStencilAttachment.stencilLoadAction = RHI::AttachmentLoadAction::Clear;
    pass.depthStencilAttachment.stencilStoreAction = static_cast<RHI::AttachmentStoreAction>(255);
    validation = RHI::ValidateRenderPassDesc(pass);
    EXPECT_EQ(validation.error, RHI::RenderPassDescriptorError::InvalidStoreAction);
    EXPECT_EQ(validation.attachmentIndex, 0u);

    pass.depthStencilAttachment.stencilStoreAction = RHI::AttachmentStoreAction::Store;
    pass.depthStencilAttachment.clearStencil = 256;
    validation = RHI::ValidateRenderPassDesc(pass);
    EXPECT_EQ(validation.error, RHI::RenderPassDescriptorError::InvalidStencilClearValue);
    EXPECT_EQ(validation.attachmentIndex, 0u);
}

TEST(RHITypesTest, GraphicsPipelineDescriptorValidationReportsPreciseErrors) {
    RHI::GraphicsPipelineDesc pipeline;
    RHI::PipelineDescriptorValidation validation = RHI::ValidateGraphicsPipelineDesc(pipeline);
    EXPECT_EQ(validation.error, RHI::PipelineDescriptorError::MissingVertexShader);
    EXPECT_EQ(validation.shaderStage, RHI::ShaderStage::Vertex);

    pipeline = RHI::MakeGraphicsPipelineDesc("vs", "", RHI::Format::RGBA8Unorm, RHI::Format::Depth32Float);
    validation = RHI::ValidateGraphicsPipelineDesc(pipeline);
    EXPECT_EQ(validation.error, RHI::PipelineDescriptorError::MissingFragmentShader);
    EXPECT_EQ(validation.shaderStage, RHI::ShaderStage::Fragment);

    pipeline = RHI::MakeGraphicsPipelineDesc("vs", "fs", RHI::Format::Depth32Float, RHI::Format::Depth32Float);
    validation = RHI::ValidateGraphicsPipelineDesc(pipeline);
    EXPECT_EQ(validation.error, RHI::PipelineDescriptorError::UnsupportedColorFormat);
    EXPECT_EQ(validation.colorAttachmentIndex, 0u);
    EXPECT_EQ(validation.format, RHI::Format::Depth32Float);

    pipeline = RHI::MakeGraphicsPipelineDesc("vs", "fs", RHI::Format::RGBA8Unorm, RHI::Format::Unknown);
    validation = RHI::ValidateGraphicsPipelineDesc(pipeline);
    EXPECT_EQ(validation.error, RHI::PipelineDescriptorError::UnsupportedDepthFormat);
    EXPECT_EQ(validation.format, RHI::Format::Unknown);

    pipeline = RHI::MakeGraphicsPipelineDesc("vs", "fs", RHI::Format::RGBA8Unorm, RHI::Format::Depth32Float);
    pipeline.colorAttachmentCount = static_cast<uint32_t>(RHI::kMaxGraphicsPipelineColorAttachments + 1);
    validation = RHI::ValidateGraphicsPipelineDesc(pipeline);
    EXPECT_EQ(validation.error, RHI::PipelineDescriptorError::TooManyColorAttachments);

    pipeline = RHI::MakeGraphicsPipelineDesc("vs", "fs", RHI::Format::RGBA8Unorm, RHI::Format::Depth32Float);
    pipeline.multisampleState.sampleCount = 4;
    pipeline.multisampleState.alphaToCoverageEnabled = true;
    EXPECT_TRUE(RHI::ValidateGraphicsPipelineDesc(pipeline));

    pipeline.multisampleState.sampleCount = 0;
    validation = RHI::ValidateGraphicsPipelineDesc(pipeline);
    EXPECT_EQ(validation.error, RHI::PipelineDescriptorError::InvalidSampleCount);

    pipeline.multisampleState.sampleCount = 3;
    validation = RHI::ValidateGraphicsPipelineDesc(pipeline);
    EXPECT_EQ(validation.error, RHI::PipelineDescriptorError::InvalidSampleCount);

    pipeline.multisampleState.sampleCount = 1;
    pipeline.rasterState.fillMode = static_cast<RHI::FillMode>(255);
    validation = RHI::ValidateGraphicsPipelineDesc(pipeline);
    EXPECT_EQ(validation.error, RHI::PipelineDescriptorError::InvalidFillMode);

    pipeline = RHI::MakeGraphicsPipelineDesc("vs", "fs", RHI::Format::RGBA8Unorm, RHI::Format::Depth32Float);
    pipeline.rasterState.depthBias = -2;
    pipeline.rasterState.depthBiasClamp = 4.0f;
    pipeline.rasterState.depthBiasSlopeScale = 1.25f;
    EXPECT_TRUE(RHI::ValidateGraphicsPipelineDesc(pipeline));

    pipeline.rasterState.depthBiasClamp = std::numeric_limits<float>::infinity();
    validation = RHI::ValidateGraphicsPipelineDesc(pipeline);
    EXPECT_EQ(validation.error, RHI::PipelineDescriptorError::InvalidDepthBias);

    pipeline.rasterState.depthBiasClamp = 4.0f;
    pipeline.rasterState.depthBiasSlopeScale = std::numeric_limits<float>::quiet_NaN();
    validation = RHI::ValidateGraphicsPipelineDesc(pipeline);
    EXPECT_EQ(validation.error, RHI::PipelineDescriptorError::InvalidDepthBias);

    pipeline = RHI::MakeGraphicsPipelineDesc("vs", "fs", RHI::Format::RGBA8Unorm, RHI::Format::Depth32Float);
    pipeline.colorAttachments[0].blendEnabled = true;
    pipeline.colorAttachments[0].sourceColorBlendFactor = RHI::BlendFactor::SourceAlpha;
    pipeline.colorAttachments[0].destinationColorBlendFactor = RHI::BlendFactor::OneMinusSourceAlpha;
    pipeline.colorAttachments[0].colorBlendOperation = RHI::BlendOperation::Add;
    pipeline.colorAttachments[0].sourceAlphaBlendFactor = RHI::BlendFactor::One;
    pipeline.colorAttachments[0].destinationAlphaBlendFactor = RHI::BlendFactor::OneMinusSourceAlpha;
    pipeline.colorAttachments[0].alphaBlendOperation = RHI::BlendOperation::Max;
    pipeline.colorAttachments[0].writeMask = RHI::ColorWriteMask::Red | RHI::ColorWriteMask::Alpha;
    EXPECT_TRUE(RHI::ValidateGraphicsPipelineDesc(pipeline));

    pipeline.colorAttachments[0].sourceColorBlendFactor = static_cast<RHI::BlendFactor>(255);
    validation = RHI::ValidateGraphicsPipelineDesc(pipeline);
    EXPECT_EQ(validation.error, RHI::PipelineDescriptorError::InvalidBlendFactor);
    EXPECT_EQ(validation.colorAttachmentIndex, 0u);
    EXPECT_EQ(validation.format, RHI::Format::RGBA8Unorm);

    pipeline.colorAttachments[0].sourceColorBlendFactor = RHI::BlendFactor::SourceAlpha;
    pipeline.colorAttachments[0].colorBlendOperation = static_cast<RHI::BlendOperation>(255);
    validation = RHI::ValidateGraphicsPipelineDesc(pipeline);
    EXPECT_EQ(validation.error, RHI::PipelineDescriptorError::InvalidBlendOperation);
    EXPECT_EQ(validation.colorAttachmentIndex, 0u);

    pipeline.colorAttachments[0].colorBlendOperation = RHI::BlendOperation::Add;
    pipeline.colorAttachments[0].writeMask = 0x10;
    validation = RHI::ValidateGraphicsPipelineDesc(pipeline);
    EXPECT_EQ(validation.error, RHI::PipelineDescriptorError::InvalidColorWriteMask);
    EXPECT_EQ(validation.colorAttachmentIndex, 0u);

    pipeline = RHI::MakeGraphicsPipelineDesc("vs", "fs", RHI::Format::RGBA8Unorm, RHI::Format::Depth32Float);
    pipeline.depthStencilState.stencilTestEnabled = true;
    validation = RHI::ValidateGraphicsPipelineDesc(pipeline);
    EXPECT_EQ(validation.error, RHI::PipelineDescriptorError::UnsupportedStencilFormat);
    EXPECT_EQ(validation.format, RHI::Format::Depth32Float);

    pipeline = RHI::MakeGraphicsPipelineDesc("vs",
                                             "fs",
                                             RHI::Format::RGBA8Unorm,
                                             RHI::Format::Depth32FloatStencil8);
    pipeline.depthStencilState.stencilTestEnabled = true;
    pipeline.depthStencilState.frontStencil.compare = RHI::CompareFunction::Always;
    pipeline.depthStencilState.backStencil.compare = RHI::CompareFunction::LessEqual;
    pipeline.depthStencilState.frontStencil.stencilFailOp = RHI::StencilOperation::Replace;
    pipeline.depthStencilState.backStencil.passOp = RHI::StencilOperation::IncrementWrap;
    EXPECT_TRUE(RHI::ValidateGraphicsPipelineDesc(pipeline));

    pipeline.depthStencilState.frontStencil.compare = static_cast<RHI::CompareFunction>(255);
    validation = RHI::ValidateGraphicsPipelineDesc(pipeline);
    EXPECT_EQ(validation.error, RHI::PipelineDescriptorError::InvalidStencilCompareFunction);

    pipeline.depthStencilState.frontStencil.compare = RHI::CompareFunction::Always;
    pipeline.depthStencilState.backStencil.passOp = static_cast<RHI::StencilOperation>(255);
    validation = RHI::ValidateGraphicsPipelineDesc(pipeline);
    EXPECT_EQ(validation.error, RHI::PipelineDescriptorError::InvalidStencilOperation);
}

TEST(RHITypesTest, GraphicsPipelineVertexInputValidationReportsPreciseErrors) {
    RHI::GraphicsPipelineDesc pipeline = MakeValidVertexInputPipeline();
    EXPECT_TRUE(RHI::ValidateGraphicsPipelineDesc(pipeline));

    pipeline.vertexInput.bufferCount = static_cast<uint32_t>(RHI::kMaxGraphicsPipelineVertexBuffers + 1);
    RHI::PipelineDescriptorValidation validation = RHI::ValidateGraphicsPipelineDesc(pipeline);
    EXPECT_EQ(validation.error, RHI::PipelineDescriptorError::TooManyVertexBuffers);

    pipeline = MakeValidVertexInputPipeline();
    pipeline.vertexInput.attributeCount = static_cast<uint32_t>(RHI::kMaxGraphicsPipelineVertexAttributes + 1);
    validation = RHI::ValidateGraphicsPipelineDesc(pipeline);
    EXPECT_EQ(validation.error, RHI::PipelineDescriptorError::TooManyVertexAttributes);

    pipeline = MakeValidVertexInputPipeline();
    pipeline.vertexInput.buffers[0].stride = 0;
    validation = RHI::ValidateGraphicsPipelineDesc(pipeline);
    EXPECT_EQ(validation.error, RHI::PipelineDescriptorError::InvalidVertexStride);
    EXPECT_EQ(validation.vertexBufferIndex, 0u);

    pipeline = MakeValidVertexInputPipeline();
    pipeline.vertexInput.buffers[0].stepFunction = static_cast<RHI::VertexStepFunction>(255);
    validation = RHI::ValidateGraphicsPipelineDesc(pipeline);
    EXPECT_EQ(validation.error, RHI::PipelineDescriptorError::InvalidVertexStepFunction);
    EXPECT_EQ(validation.vertexBufferIndex, 0u);

    pipeline = MakeValidVertexInputPipeline();
    pipeline.vertexInput.buffers[0].stepRate = 0;
    validation = RHI::ValidateGraphicsPipelineDesc(pipeline);
    EXPECT_EQ(validation.error, RHI::PipelineDescriptorError::InvalidVertexStepRate);
    EXPECT_EQ(validation.vertexBufferIndex, 0u);

    pipeline = MakeValidVertexInputPipeline();
    pipeline.vertexInput.attributes[1].location = 0;
    validation = RHI::ValidateGraphicsPipelineDesc(pipeline);
    EXPECT_EQ(validation.error, RHI::PipelineDescriptorError::InvalidVertexAttributeLocation);
    EXPECT_EQ(validation.vertexAttributeIndex, 1u);

    pipeline = MakeValidVertexInputPipeline();
    pipeline.vertexInput.attributes[1].location = RHI::kMaxGraphicsPipelineVertexAttributes;
    validation = RHI::ValidateGraphicsPipelineDesc(pipeline);
    EXPECT_EQ(validation.error, RHI::PipelineDescriptorError::InvalidVertexAttributeLocation);
    EXPECT_EQ(validation.vertexAttributeIndex, 1u);

    pipeline = MakeValidVertexInputPipeline();
    pipeline.vertexInput.attributes[1].bufferIndex = 1;
    validation = RHI::ValidateGraphicsPipelineDesc(pipeline);
    EXPECT_EQ(validation.error, RHI::PipelineDescriptorError::InvalidVertexBufferReference);
    EXPECT_EQ(validation.vertexAttributeIndex, 1u);
    EXPECT_EQ(validation.vertexBufferIndex, 1u);

    pipeline = MakeValidVertexInputPipeline();
    pipeline.vertexInput.attributes[1].format = RHI::VertexFormat::Unknown;
    validation = RHI::ValidateGraphicsPipelineDesc(pipeline);
    EXPECT_EQ(validation.error, RHI::PipelineDescriptorError::InvalidVertexFormat);
    EXPECT_EQ(validation.vertexAttributeIndex, 1u);
    EXPECT_EQ(validation.vertexBufferIndex, 0u);

    pipeline = MakeValidVertexInputPipeline();
    pipeline.vertexInput.attributes[1].offset = 12;
    validation = RHI::ValidateGraphicsPipelineDesc(pipeline);
    EXPECT_EQ(validation.error, RHI::PipelineDescriptorError::InvalidVertexStride);
    EXPECT_EQ(validation.vertexAttributeIndex, 1u);
    EXPECT_EQ(validation.vertexBufferIndex, 0u);
}

TEST(RHITypesTest, GraphicsPipelineDescriptorEqualityIgnoresDebugLabels) {
    RHI::GraphicsPipelineDesc pipeline =
        RHI::MakeGraphicsPipelineDesc("vs", "fs", RHI::Format::RGBA8Unorm, RHI::Format::Depth32Float, "a");
    RHI::GraphicsPipelineDesc matching = pipeline;

    EXPECT_TRUE(RHI::GraphicsPipelineDescEquals(pipeline, matching));

    matching.debugName = "b";
    matching.vertexShader.debugName = "vertex label";
    matching.fragmentShader.debugName = "fragment label";
    EXPECT_TRUE(RHI::GraphicsPipelineDescEquals(pipeline, matching));

    matching = pipeline;
    matching.fragmentShader.entryPoint = "lighting_fs";
    EXPECT_FALSE(RHI::GraphicsPipelineDescEquals(pipeline, matching));

    matching = pipeline;
    matching.vertexInput.bufferCount = 1;
    matching.vertexInput.buffers[0].stride = 24;
    EXPECT_FALSE(RHI::GraphicsPipelineDescEquals(pipeline, matching));

    matching = pipeline;
    matching.vertexInput.bufferCount = 1;
    matching.vertexInput.buffers[0].stride = 24;
    matching.vertexInput.attributeCount = 1;
    matching.vertexInput.attributes[0].format = RHI::VertexFormat::Float32x3;
    EXPECT_FALSE(RHI::GraphicsPipelineDescEquals(pipeline, matching));

    matching = pipeline;
    matching.rasterState.fillMode = RHI::FillMode::Wireframe;
    EXPECT_FALSE(RHI::GraphicsPipelineDescEquals(pipeline, matching));

    matching = pipeline;
    matching.rasterState.depthBias = 2;
    EXPECT_FALSE(RHI::GraphicsPipelineDescEquals(pipeline, matching));

    matching = pipeline;
    matching.rasterState.depthBiasClamp = 4.0f;
    EXPECT_FALSE(RHI::GraphicsPipelineDescEquals(pipeline, matching));

    matching = pipeline;
    matching.rasterState.depthBiasSlopeScale = 1.25f;
    EXPECT_FALSE(RHI::GraphicsPipelineDescEquals(pipeline, matching));

    matching = pipeline;
    matching.rasterState.depthClipEnabled = false;
    EXPECT_FALSE(RHI::GraphicsPipelineDescEquals(pipeline, matching));

    matching = pipeline;
    matching.colorAttachments[0].format = RHI::Format::BGRA8Unorm;
    EXPECT_FALSE(RHI::GraphicsPipelineDescEquals(pipeline, matching));

    matching = pipeline;
    matching.colorAttachments[0].sourceColorBlendFactor = RHI::BlendFactor::SourceAlpha;
    EXPECT_FALSE(RHI::GraphicsPipelineDescEquals(pipeline, matching));

    matching = pipeline;
    matching.colorAttachments[0].colorBlendOperation = RHI::BlendOperation::ReverseSubtract;
    EXPECT_FALSE(RHI::GraphicsPipelineDescEquals(pipeline, matching));

    matching = pipeline;
    matching.colorAttachments[0].writeMask = RHI::ColorWriteMaskFlag(RHI::ColorWriteMask::None);
    EXPECT_FALSE(RHI::GraphicsPipelineDescEquals(pipeline, matching));

    matching = pipeline;
    matching.multisampleState.sampleCount = 4;
    EXPECT_FALSE(RHI::GraphicsPipelineDescEquals(pipeline, matching));

    matching = pipeline;
    matching.multisampleState.alphaToCoverageEnabled = true;
    EXPECT_FALSE(RHI::GraphicsPipelineDescEquals(pipeline, matching));

    matching = pipeline;
    matching.depthStencilState.depthWriteEnabled = false;
    EXPECT_FALSE(RHI::GraphicsPipelineDescEquals(pipeline, matching));

    matching = pipeline;
    matching.depthStencilState.stencilTestEnabled = true;
    EXPECT_FALSE(RHI::GraphicsPipelineDescEquals(pipeline, matching));

    matching = pipeline;
    matching.depthStencilState.frontStencil.passOp = RHI::StencilOperation::Replace;
    EXPECT_FALSE(RHI::GraphicsPipelineDescEquals(pipeline, matching));
}

TEST(RHITypesTest, GraphicsPipelineDescriptorEqualityRejectsOutOfRangeCounts) {
    RHI::GraphicsPipelineDesc pipeline =
        RHI::MakeGraphicsPipelineDesc("vs", "fs", RHI::Format::RGBA8Unorm, RHI::Format::Depth32Float, "a");
    RHI::GraphicsPipelineDesc matching = pipeline;

    pipeline.vertexInput.bufferCount = static_cast<uint32_t>(RHI::kMaxGraphicsPipelineVertexBuffers + 1);
    matching.vertexInput.bufferCount = pipeline.vertexInput.bufferCount;
    EXPECT_FALSE(RHI::GraphicsPipelineDescEquals(pipeline, matching));

    pipeline = RHI::MakeGraphicsPipelineDesc("vs", "fs", RHI::Format::RGBA8Unorm, RHI::Format::Depth32Float, "a");
    matching = pipeline;
    pipeline.vertexInput.attributeCount =
        static_cast<uint32_t>(RHI::kMaxGraphicsPipelineVertexAttributes + 1);
    matching.vertexInput.attributeCount = pipeline.vertexInput.attributeCount;
    EXPECT_FALSE(RHI::GraphicsPipelineDescEquals(pipeline, matching));

    pipeline = RHI::MakeGraphicsPipelineDesc("vs", "fs", RHI::Format::RGBA8Unorm, RHI::Format::Depth32Float, "a");
    matching = pipeline;
    pipeline.colorAttachmentCount = static_cast<uint32_t>(RHI::kMaxGraphicsPipelineColorAttachments + 1);
    matching.colorAttachmentCount = pipeline.colorAttachmentCount;
    EXPECT_FALSE(RHI::GraphicsPipelineDescEquals(pipeline, matching));
}

TEST(RHITypesTest, GraphicsPipelineDescriptorHashMatchesEqualityRules) {
    RHI::GraphicsPipelineDesc pipeline =
        RHI::MakeGraphicsPipelineDesc("vs", "fs", RHI::Format::RGBA8Unorm, RHI::Format::Depth32Float, "a");
    RHI::GraphicsPipelineDesc matching = pipeline;

    EXPECT_EQ(RHI::GraphicsPipelineDescHash(pipeline), RHI::GraphicsPipelineDescHash(matching));

    matching.debugName = "b";
    matching.vertexShader.debugName = "vertex label";
    matching.fragmentShader.debugName = "fragment label";
    EXPECT_TRUE(RHI::GraphicsPipelineDescEquals(pipeline, matching));
    EXPECT_EQ(RHI::GraphicsPipelineDescHash(pipeline), RHI::GraphicsPipelineDescHash(matching));

    matching = pipeline;
    matching.fragmentShader.entryPoint = "lighting_fs";
    EXPECT_FALSE(RHI::GraphicsPipelineDescEquals(pipeline, matching));
    EXPECT_NE(RHI::GraphicsPipelineDescHash(pipeline), RHI::GraphicsPipelineDescHash(matching));

    matching = pipeline;
    matching.vertexInput.bufferCount = 1;
    matching.vertexInput.buffers[0].stride = 24;
    EXPECT_FALSE(RHI::GraphicsPipelineDescEquals(pipeline, matching));
    EXPECT_NE(RHI::GraphicsPipelineDescHash(pipeline), RHI::GraphicsPipelineDescHash(matching));

    matching = pipeline;
    matching.vertexInput.bufferCount = 1;
    matching.vertexInput.buffers[0].stride = 24;
    matching.vertexInput.attributeCount = 1;
    matching.vertexInput.attributes[0].format = RHI::VertexFormat::Float32x3;
    EXPECT_FALSE(RHI::GraphicsPipelineDescEquals(pipeline, matching));
    EXPECT_NE(RHI::GraphicsPipelineDescHash(pipeline), RHI::GraphicsPipelineDescHash(matching));

    matching = pipeline;
    matching.colorAttachments[0].blendEnabled = true;
    EXPECT_FALSE(RHI::GraphicsPipelineDescEquals(pipeline, matching));
    EXPECT_NE(RHI::GraphicsPipelineDescHash(pipeline), RHI::GraphicsPipelineDescHash(matching));

    matching = pipeline;
    matching.colorAttachments[0].destinationColorBlendFactor = RHI::BlendFactor::OneMinusSourceAlpha;
    EXPECT_FALSE(RHI::GraphicsPipelineDescEquals(pipeline, matching));
    EXPECT_NE(RHI::GraphicsPipelineDescHash(pipeline), RHI::GraphicsPipelineDescHash(matching));

    matching = pipeline;
    matching.colorAttachments[0].alphaBlendOperation = RHI::BlendOperation::Max;
    EXPECT_FALSE(RHI::GraphicsPipelineDescEquals(pipeline, matching));
    EXPECT_NE(RHI::GraphicsPipelineDescHash(pipeline), RHI::GraphicsPipelineDescHash(matching));

    matching = pipeline;
    matching.colorAttachments[0].writeMask = RHI::ColorWriteMask::Red | RHI::ColorWriteMask::Blue;
    EXPECT_FALSE(RHI::GraphicsPipelineDescEquals(pipeline, matching));
    EXPECT_NE(RHI::GraphicsPipelineDescHash(pipeline), RHI::GraphicsPipelineDescHash(matching));

    matching = pipeline;
    matching.multisampleState.sampleCount = 4;
    EXPECT_FALSE(RHI::GraphicsPipelineDescEquals(pipeline, matching));
    EXPECT_NE(RHI::GraphicsPipelineDescHash(pipeline), RHI::GraphicsPipelineDescHash(matching));

    matching = pipeline;
    matching.multisampleState.alphaToCoverageEnabled = true;
    EXPECT_FALSE(RHI::GraphicsPipelineDescEquals(pipeline, matching));
    EXPECT_NE(RHI::GraphicsPipelineDescHash(pipeline), RHI::GraphicsPipelineDescHash(matching));

    matching = pipeline;
    matching.rasterState.cullMode = RHI::CullMode::Back;
    EXPECT_FALSE(RHI::GraphicsPipelineDescEquals(pipeline, matching));
    EXPECT_NE(RHI::GraphicsPipelineDescHash(pipeline), RHI::GraphicsPipelineDescHash(matching));

    matching = pipeline;
    matching.rasterState.fillMode = RHI::FillMode::Wireframe;
    EXPECT_FALSE(RHI::GraphicsPipelineDescEquals(pipeline, matching));
    EXPECT_NE(RHI::GraphicsPipelineDescHash(pipeline), RHI::GraphicsPipelineDescHash(matching));

    matching = pipeline;
    matching.rasterState.depthBias = 2;
    EXPECT_FALSE(RHI::GraphicsPipelineDescEquals(pipeline, matching));
    EXPECT_NE(RHI::GraphicsPipelineDescHash(pipeline), RHI::GraphicsPipelineDescHash(matching));

    matching = pipeline;
    matching.rasterState.depthBiasClamp = 4.0f;
    EXPECT_FALSE(RHI::GraphicsPipelineDescEquals(pipeline, matching));
    EXPECT_NE(RHI::GraphicsPipelineDescHash(pipeline), RHI::GraphicsPipelineDescHash(matching));

    matching = pipeline;
    matching.rasterState.depthBiasSlopeScale = 1.25f;
    EXPECT_FALSE(RHI::GraphicsPipelineDescEquals(pipeline, matching));
    EXPECT_NE(RHI::GraphicsPipelineDescHash(pipeline), RHI::GraphicsPipelineDescHash(matching));

    matching = pipeline;
    matching.rasterState.depthClipEnabled = false;
    EXPECT_FALSE(RHI::GraphicsPipelineDescEquals(pipeline, matching));
    EXPECT_NE(RHI::GraphicsPipelineDescHash(pipeline), RHI::GraphicsPipelineDescHash(matching));

    matching = pipeline;
    matching.depthStencilState.stencilTestEnabled = true;
    matching.depthStencilState.frontStencil.passOp = RHI::StencilOperation::Replace;
    EXPECT_FALSE(RHI::GraphicsPipelineDescEquals(pipeline, matching));
    EXPECT_NE(RHI::GraphicsPipelineDescHash(pipeline), RHI::GraphicsPipelineDescHash(matching));

    matching = pipeline;
    matching.colorAttachmentCount = static_cast<uint32_t>(RHI::kMaxGraphicsPipelineColorAttachments + 1);
    EXPECT_NE(RHI::GraphicsPipelineDescHash(pipeline), RHI::GraphicsPipelineDescHash(matching));
}

TEST(RHITypesTest, ResourceTracksCurrentStateContract) {
    TestResource resource;

    EXPECT_EQ(resource.GetCurrentState(), RHI::ResourceState::Undefined);

    resource.SetCurrentState(RHI::ResourceState::CopyDestination);
    EXPECT_EQ(resource.GetCurrentState(), RHI::ResourceState::CopyDestination);

    resource.SetCurrentState(RHI::ResourceState::ShaderRead);
    EXPECT_EQ(resource.GetCurrentState(), RHI::ResourceState::ShaderRead);
}

TEST(RHITypesTest, ResourceUsageSupportsExpectedStates) {
    const RHI::ResourceUsageFlags usage =
        RHI::ResourceUsage::ShaderRead | RHI::ResourceUsage::CopyDestination;

    EXPECT_TRUE(RHI::ResourceUsageSupportsState(usage, RHI::ResourceState::Undefined));
    EXPECT_TRUE(RHI::ResourceUsageSupportsState(usage, RHI::ResourceState::Common));
    EXPECT_TRUE(RHI::ResourceUsageSupportsState(usage, RHI::ResourceState::CopyDestination));
    EXPECT_TRUE(RHI::ResourceUsageSupportsState(usage, RHI::ResourceState::ShaderRead));
    EXPECT_FALSE(RHI::ResourceUsageSupportsState(usage, RHI::ResourceState::RenderTarget));
    EXPECT_FALSE(RHI::ResourceUsageSupportsState(usage, RHI::ResourceState::DepthWrite));
}

TEST(RHITypesTest, ResourceStateSupportDependsOnQueueClass) {
    EXPECT_TRUE(RHI::ResourceStateSupportedOnQueue(RHI::ResourceState::Undefined, RHI::QueueClass::Graphics));
    EXPECT_TRUE(RHI::ResourceStateSupportedOnQueue(RHI::ResourceState::Common, RHI::QueueClass::Compute));
    EXPECT_TRUE(RHI::ResourceStateSupportedOnQueue(RHI::ResourceState::CopySource, RHI::QueueClass::Copy));
    EXPECT_TRUE(RHI::ResourceStateSupportedOnQueue(RHI::ResourceState::CopyDestination, RHI::QueueClass::Compute));

    EXPECT_TRUE(RHI::ResourceStateSupportedOnQueue(RHI::ResourceState::ShaderRead, RHI::QueueClass::Graphics));
    EXPECT_TRUE(RHI::ResourceStateSupportedOnQueue(RHI::ResourceState::ShaderRead, RHI::QueueClass::Compute));
    EXPECT_FALSE(RHI::ResourceStateSupportedOnQueue(RHI::ResourceState::ShaderRead, RHI::QueueClass::Copy));
    EXPECT_TRUE(RHI::ResourceStateSupportedOnQueue(RHI::ResourceState::ShaderWrite, RHI::QueueClass::Compute));
    EXPECT_FALSE(RHI::ResourceStateSupportedOnQueue(RHI::ResourceState::ConstantBuffer, RHI::QueueClass::Copy));

    EXPECT_TRUE(RHI::ResourceStateSupportedOnQueue(RHI::ResourceState::VertexBuffer, RHI::QueueClass::Graphics));
    EXPECT_TRUE(RHI::ResourceStateSupportedOnQueue(RHI::ResourceState::IndexBuffer, RHI::QueueClass::Graphics));
    EXPECT_TRUE(RHI::ResourceStateSupportedOnQueue(RHI::ResourceState::RenderTarget, RHI::QueueClass::Graphics));
    EXPECT_TRUE(RHI::ResourceStateSupportedOnQueue(RHI::ResourceState::DepthWrite, RHI::QueueClass::Graphics));
    EXPECT_TRUE(RHI::ResourceStateSupportedOnQueue(RHI::ResourceState::Present, RHI::QueueClass::Graphics));
    EXPECT_FALSE(RHI::ResourceStateSupportedOnQueue(RHI::ResourceState::VertexBuffer, RHI::QueueClass::Compute));
    EXPECT_FALSE(RHI::ResourceStateSupportedOnQueue(RHI::ResourceState::RenderTarget, RHI::QueueClass::Copy));
    EXPECT_FALSE(RHI::ResourceStateSupportedOnQueue(RHI::ResourceState::Undefined, static_cast<RHI::QueueClass>(255)));
}

TEST(RHITypesTest, ResourceTransitionAppliesWhenUsageSupportsTargetState) {
    TestResource resource(RHI::ResourceUsage::ShaderRead | RHI::ResourceUsage::CopyDestination);

    EXPECT_TRUE(RHI::TransitionResource(resource, RHI::ResourceState::CopyDestination));
    EXPECT_EQ(resource.GetCurrentState(), RHI::ResourceState::CopyDestination);

    const RHI::ResourceTransition transition =
        RHI::MakeResourceTransition(resource, RHI::ResourceState::ShaderRead);
    EXPECT_EQ(transition.before, RHI::ResourceState::CopyDestination);
    EXPECT_EQ(transition.after, RHI::ResourceState::ShaderRead);
    EXPECT_EQ(transition.queueClass, RHI::QueueClass::Graphics);
    EXPECT_EQ(transition.shaderStages, 0u);
    EXPECT_FALSE(transition.HasShaderStages());

    EXPECT_TRUE(RHI::ValidateResourceTransition(transition));
    EXPECT_TRUE(RHI::ApplyResourceTransition(transition));
    EXPECT_EQ(resource.GetCurrentState(), RHI::ResourceState::ShaderRead);
}

TEST(RHITypesTest, ResourceTransitionCapturesExplicitQueueClass) {
    TestResource resource(RHI::ResourceUsage::CopySource | RHI::ResourceUsage::CopyDestination);

    const RHI::ResourceTransition transition =
        RHI::MakeResourceTransition(resource, RHI::ResourceState::CopyDestination, RHI::QueueClass::Copy);

    EXPECT_EQ(transition.queueClass, RHI::QueueClass::Copy);
    EXPECT_TRUE(RHI::ValidateResourceTransition(transition));
    EXPECT_TRUE(RHI::ApplyResourceTransition(transition));
    EXPECT_EQ(resource.GetCurrentState(), RHI::ResourceState::CopyDestination);
}

TEST(RHITypesTest, ResourceTransitionCanUseCommandContextQueueClass) {
    TestResource uploadResource(RHI::ResourceUsageFlag(RHI::ResourceUsage::CopyDestination));
    TestCommandContext copyContext(RHI::QueueClass::Copy);

    const RHI::ResourceTransition transition =
        RHI::MakeResourceTransition(uploadResource, RHI::ResourceState::CopyDestination, copyContext);
    EXPECT_EQ(transition.queueClass, RHI::QueueClass::Copy);

    EXPECT_TRUE(RHI::ApplyResourceTransition(transition));
    EXPECT_EQ(uploadResource.GetCurrentState(), RHI::ResourceState::CopyDestination);

    TestResource shaderResource(RHI::ResourceUsageFlag(RHI::ResourceUsage::ShaderRead));
    EXPECT_FALSE(RHI::TransitionResource(shaderResource, RHI::ResourceState::ShaderRead, copyContext));
    EXPECT_EQ(shaderResource.GetCurrentState(), RHI::ResourceState::Undefined);
}

TEST(RHITypesTest, ResourceTransitionRejectsUnsupportedTargetState) {
    TestResource resource(RHI::ResourceUsageFlag(RHI::ResourceUsage::ShaderRead));
    resource.SetCurrentState(RHI::ResourceState::ShaderRead);

    const RHI::ResourceTransition transition =
        RHI::MakeResourceTransition(resource, RHI::ResourceState::RenderTarget);
    const RHI::ResourceTransitionValidation validation = RHI::ValidateResourceTransition(transition);
    EXPECT_EQ(validation.error, RHI::ResourceTransitionError::UnsupportedTargetState);

    EXPECT_FALSE(RHI::ApplyResourceTransition(transition));
    EXPECT_EQ(resource.GetCurrentState(), RHI::ResourceState::ShaderRead);
}

TEST(RHITypesTest, ResourceTransitionRejectsUnsupportedQueueClass) {
    TestResource resource(RHI::ResourceUsageFlag(RHI::ResourceUsage::ShaderRead));

    const RHI::ResourceTransition transition =
        RHI::MakeResourceTransition(resource, RHI::ResourceState::ShaderRead, RHI::QueueClass::Copy);
    const RHI::ResourceTransitionValidation validation = RHI::ValidateResourceTransition(transition);
    EXPECT_EQ(validation.error, RHI::ResourceTransitionError::UnsupportedQueueClass);

    EXPECT_FALSE(RHI::ApplyResourceTransition(transition));
    EXPECT_EQ(resource.GetCurrentState(), RHI::ResourceState::Undefined);
}

TEST(RHITypesTest, ResourceTransitionRejectsStaleBeforeState) {
    TestResource resource(RHI::ResourceUsage::ShaderRead | RHI::ResourceUsage::CopyDestination);
    resource.SetCurrentState(RHI::ResourceState::CopyDestination);

    RHI::ResourceTransition transition =
        RHI::MakeResourceTransition(resource, RHI::ResourceState::ShaderRead);
    resource.SetCurrentState(RHI::ResourceState::Common);

    const RHI::ResourceTransitionValidation validation = RHI::ValidateResourceTransition(transition);
    EXPECT_EQ(validation.error, RHI::ResourceTransitionError::StaleBeforeState);
    EXPECT_EQ(validation.current, RHI::ResourceState::Common);

    EXPECT_FALSE(RHI::ApplyResourceTransition(transition));
    EXPECT_EQ(resource.GetCurrentState(), RHI::ResourceState::Common);
}

TEST(RHITypesTest, ResourceTransitionValidationReportsNullResource) {
    RHI::ResourceTransition transition;
    transition.after = RHI::ResourceState::ShaderRead;

    const RHI::ResourceTransitionValidation validation = RHI::ValidateResourceTransition(transition);

    EXPECT_EQ(validation.error, RHI::ResourceTransitionError::NullResource);
}

TEST(RHITypesTest, ResourceTransitionBatchAppliesAtomically) {
    TestResource texture(RHI::ResourceUsage::ShaderRead | RHI::ResourceUsage::CopyDestination);
    TestResource uniforms(RHI::ResourceUsage::ConstantBuffer | RHI::ResourceUsage::CopyDestination);
    texture.SetCurrentState(RHI::ResourceState::CopyDestination);
    uniforms.SetCurrentState(RHI::ResourceState::CopyDestination);

    const RHI::ResourceTransition transitions[] = {
        RHI::MakeResourceTransition(texture, RHI::ResourceState::ShaderRead),
        RHI::MakeResourceTransition(uniforms, RHI::ResourceState::ConstantBuffer),
    };

    EXPECT_TRUE(RHI::ValidateResourceTransitions(transitions, 2));
    EXPECT_TRUE(RHI::ApplyResourceTransitions(transitions, 2));
    EXPECT_EQ(texture.GetCurrentState(), RHI::ResourceState::ShaderRead);
    EXPECT_EQ(uniforms.GetCurrentState(), RHI::ResourceState::ConstantBuffer);
}

TEST(RHITypesTest, ResourceTransitionBatchRejectsUnsupportedTargetWithoutMutating) {
    TestResource texture(RHI::ResourceUsage::ShaderRead | RHI::ResourceUsage::CopyDestination);
    TestResource colorTarget(RHI::ResourceUsageFlag(RHI::ResourceUsage::ShaderRead));
    texture.SetCurrentState(RHI::ResourceState::CopyDestination);
    colorTarget.SetCurrentState(RHI::ResourceState::ShaderRead);

    const RHI::ResourceTransition transitions[] = {
        RHI::MakeResourceTransition(texture, RHI::ResourceState::ShaderRead),
        RHI::MakeResourceTransition(colorTarget, RHI::ResourceState::RenderTarget),
    };

    const RHI::ResourceTransitionValidation validation = RHI::ValidateResourceTransitions(transitions, 2);
    EXPECT_EQ(validation.error, RHI::ResourceTransitionError::UnsupportedTargetState);
    EXPECT_EQ(validation.index, 1u);

    EXPECT_FALSE(RHI::ApplyResourceTransitions(transitions, 2));
    EXPECT_EQ(texture.GetCurrentState(), RHI::ResourceState::CopyDestination);
    EXPECT_EQ(colorTarget.GetCurrentState(), RHI::ResourceState::ShaderRead);
}

TEST(RHITypesTest, ResourceTransitionBatchRejectsUnsupportedQueueWithoutMutating) {
    TestResource texture(RHI::ResourceUsage::ShaderRead | RHI::ResourceUsage::CopyDestination);
    TestResource uniforms(RHI::ResourceUsage::ConstantBuffer | RHI::ResourceUsage::CopyDestination);
    texture.SetCurrentState(RHI::ResourceState::CopyDestination);
    uniforms.SetCurrentState(RHI::ResourceState::CopyDestination);

    const RHI::ResourceTransition transitions[] = {
        RHI::MakeResourceTransition(texture, RHI::ResourceState::ShaderRead),
        RHI::MakeResourceTransition(uniforms, RHI::ResourceState::ConstantBuffer, RHI::QueueClass::Copy),
    };

    const RHI::ResourceTransitionValidation validation = RHI::ValidateResourceTransitions(transitions, 2);
    EXPECT_EQ(validation.error, RHI::ResourceTransitionError::UnsupportedQueueClass);
    EXPECT_EQ(validation.index, 1u);

    EXPECT_FALSE(RHI::ApplyResourceTransitions(transitions, 2));
    EXPECT_EQ(texture.GetCurrentState(), RHI::ResourceState::CopyDestination);
    EXPECT_EQ(uniforms.GetCurrentState(), RHI::ResourceState::CopyDestination);
}

TEST(RHITypesTest, ResourceTransitionBatchRejectsDuplicateResourcesWithoutMutating) {
    TestResource resource(RHI::ResourceUsage::ShaderRead | RHI::ResourceUsage::CopyDestination);
    resource.SetCurrentState(RHI::ResourceState::CopyDestination);

    const RHI::ResourceTransition transitions[] = {
        RHI::MakeResourceTransition(resource, RHI::ResourceState::ShaderRead),
        RHI::MakeResourceTransition(resource, RHI::ResourceState::Common),
    };

    const RHI::ResourceTransitionValidation validation = RHI::ValidateResourceTransitions(transitions, 2);
    EXPECT_EQ(validation.error, RHI::ResourceTransitionError::DuplicateResource);
    EXPECT_EQ(validation.index, 1u);
    EXPECT_EQ(validation.conflictIndex, 0u);

    EXPECT_FALSE(RHI::ApplyResourceTransitions(transitions, 2));
    EXPECT_EQ(resource.GetCurrentState(), RHI::ResourceState::CopyDestination);
}

TEST(RHITypesTest, ResourceTransitionBatchAcceptsEmptyBatch) {
    EXPECT_TRUE(RHI::ValidateResourceTransitions(nullptr, 0));
    EXPECT_TRUE(RHI::ApplyResourceTransitions(nullptr, 0));
}

TEST(RHITypesTest, ResourceTransitionBatchValidationReportsNullList) {
    const RHI::ResourceTransitionValidation validation = RHI::ValidateResourceTransitions(nullptr, 2);

    EXPECT_EQ(validation.error, RHI::ResourceTransitionError::NullTransitionList);
    EXPECT_FALSE(RHI::ApplyResourceTransitions(nullptr, 2));
}

TEST(RHITypesTest, FrameGraphHelpersBuildPassAccessDescriptors) {
    RHI::FrameGraphDesc graph;
    RHI::FrameGraphResourceDesc color;
    color.debugName = "color";
    color.type = RHI::ResourceType::Texture;
    color.usage = RHI::ResourceUsage::RenderTarget | RHI::ResourceUsage::ShaderRead;
    color.initialState = RHI::ResourceState::Common;
    color.imported = true;

    RHI::FrameGraphResourceHandle colorHandle;
    ASSERT_TRUE(RHI::AddFrameGraphResource(graph, color, &colorHandle));
    EXPECT_TRUE(colorHandle.IsValid());
    EXPECT_EQ(colorHandle.index, 0u);
    ASSERT_NE(graph.GetResource(colorHandle), nullptr);
    EXPECT_TRUE(graph.GetResource(colorHandle)->HasDebugName());
    EXPECT_TRUE(graph.GetResource(colorHandle)->HasKnownType());
    EXPECT_TRUE(graph.GetResource(colorHandle)->HasUsage());

    RHI::FrameGraphPassDesc pass;
    pass.debugName = "gbuffer";
    ASSERT_TRUE(RHI::AddFrameGraphPassAccess(
        pass,
        RHI::MakeFrameGraphPassResourceAccess(colorHandle,
                                              RHI::ResourceState::RenderTarget,
                                              RHI::FrameGraphAccessType::Write)));
    EXPECT_TRUE(pass.HasDebugName());
    EXPECT_TRUE(pass.HasAccesses());
    EXPECT_TRUE(pass.HasCompleteAccessTable());
    ASSERT_NE(pass.GetAccess(0), nullptr);
    EXPECT_TRUE(pass.GetAccess(0)->Writes());

    ASSERT_TRUE(RHI::AddFrameGraphPass(graph, pass));
    EXPECT_TRUE(graph.HasResources());
    EXPECT_TRUE(graph.HasPasses());
    EXPECT_TRUE(graph.HasCompleteResourceTable());
    EXPECT_TRUE(graph.HasCompletePassTable());
    EXPECT_TRUE(RHI::ValidateFrameGraphDesc(graph));
}

TEST(RHITypesTest, FrameGraphHelpersBuildPassDependencies) {
    RHI::FrameGraphDesc graph;
    RHI::FrameGraphResourceDesc color;
    color.debugName = "color";
    color.type = RHI::ResourceType::Texture;
    color.usage = RHI::ResourceUsage::RenderTarget | RHI::ResourceUsage::ShaderRead;
    color.initialState = RHI::ResourceState::Common;
    RHI::FrameGraphResourceHandle colorHandle;
    ASSERT_TRUE(RHI::AddFrameGraphResource(graph, color, &colorHandle));

    RHI::FrameGraphPassDesc gbuffer;
    gbuffer.debugName = "gbuffer";
    ASSERT_TRUE(RHI::AddFrameGraphPassAccess(
        gbuffer,
        RHI::MakeFrameGraphPassResourceAccess(colorHandle,
                                              RHI::ResourceState::RenderTarget,
                                              RHI::FrameGraphAccessType::Write)));
    ASSERT_TRUE(RHI::AddFrameGraphPass(graph, gbuffer));

    RHI::FrameGraphPassDesc lighting;
    lighting.debugName = "lighting";
    const RHI::FrameGraphPassDependency dependency = RHI::MakeFrameGraphPassDependency(0);
    EXPECT_TRUE(dependency.IsValid());
    ASSERT_TRUE(RHI::AddFrameGraphPassDependency(lighting, dependency));
    ASSERT_TRUE(RHI::AddFrameGraphPassAccess(
        lighting,
        RHI::MakeFrameGraphPassResourceAccess(colorHandle,
                                              RHI::ResourceState::ShaderRead,
                                              RHI::FrameGraphAccessType::Read)));
    EXPECT_TRUE(lighting.HasDependencies());
    EXPECT_TRUE(lighting.HasCompleteDependencyTable());
    ASSERT_NE(lighting.GetDependency(0), nullptr);
    EXPECT_EQ(lighting.GetDependency(0)->passIndex, 0u);
    EXPECT_EQ(lighting.GetDependency(1), nullptr);
    ASSERT_TRUE(RHI::AddFrameGraphPass(graph, lighting));

    RHI::FrameGraphPassDesc saturated;
    for (uint32_t i = 0; i < RHI::kMaxFrameGraphPassDependencies; ++i) {
        EXPECT_TRUE(RHI::AddFrameGraphPassDependency(saturated, dependency));
    }
    EXPECT_FALSE(RHI::AddFrameGraphPassDependency(saturated, dependency));

    EXPECT_TRUE(graph.HasCompletePassTable());
    EXPECT_TRUE(RHI::ValidateFrameGraphDesc(graph));
}

TEST(RHITypesTest, FrameGraphCompilePlansOrderedResourceTransitions) {
    RHI::FrameGraphDesc graph;
    RHI::FrameGraphResourceDesc color;
    color.debugName = "color";
    color.type = RHI::ResourceType::Texture;
    color.usage = RHI::ResourceUsage::RenderTarget | RHI::ResourceUsage::ShaderRead;
    color.initialState = RHI::ResourceState::Common;
    RHI::FrameGraphResourceHandle colorHandle;
    ASSERT_TRUE(RHI::AddFrameGraphResource(graph, color, &colorHandle));

    RHI::FrameGraphResourceDesc depth;
    depth.debugName = "depth";
    depth.type = RHI::ResourceType::Texture;
    depth.usage = RHI::ResourceUsageFlag(RHI::ResourceUsage::DepthStencil);
    depth.initialState = RHI::ResourceState::Undefined;
    RHI::FrameGraphResourceHandle depthHandle;
    ASSERT_TRUE(RHI::AddFrameGraphResource(graph, depth, &depthHandle));

    RHI::FrameGraphPassDesc gbuffer;
    gbuffer.debugName = "gbuffer";
    ASSERT_TRUE(RHI::AddFrameGraphPassAccess(
        gbuffer,
        RHI::MakeFrameGraphPassResourceAccess(colorHandle,
                                              RHI::ResourceState::RenderTarget,
                                              RHI::FrameGraphAccessType::Write)));
    ASSERT_TRUE(RHI::AddFrameGraphPassAccess(
        gbuffer,
        RHI::MakeFrameGraphPassResourceAccess(depthHandle,
                                              RHI::ResourceState::DepthWrite,
                                              RHI::FrameGraphAccessType::Write)));
    ASSERT_TRUE(RHI::AddFrameGraphPass(graph, gbuffer));

    RHI::FrameGraphPassDesc lighting;
    lighting.debugName = "lighting";
    ASSERT_TRUE(RHI::AddFrameGraphPassAccess(
        lighting,
        RHI::MakeFrameGraphPassResourceAccess(colorHandle,
                                              RHI::ResourceState::ShaderRead,
                                              RHI::FrameGraphAccessType::Read)));
    ASSERT_TRUE(RHI::AddFrameGraphPassDependency(lighting, RHI::MakeFrameGraphPassDependency(0)));
    ASSERT_TRUE(RHI::AddFrameGraphPass(graph, lighting));

    RHI::FrameGraphPassDesc post;
    post.debugName = "post";
    ASSERT_TRUE(RHI::AddFrameGraphPassAccess(
        post,
        RHI::MakeFrameGraphPassResourceAccess(colorHandle,
                                              RHI::ResourceState::ShaderRead,
                                              RHI::FrameGraphAccessType::Read)));
    ASSERT_TRUE(RHI::AddFrameGraphPassDependency(post, RHI::MakeFrameGraphPassDependency(1)));
    ASSERT_TRUE(RHI::AddFrameGraphPass(graph, post));

    const RHI::FrameGraphCompileResult result = RHI::CompileFrameGraphTransitions(graph);
    ASSERT_TRUE(result);
    EXPECT_TRUE(result.HasResources());
    EXPECT_TRUE(result.HasCompleteResourceTable());
    EXPECT_TRUE(result.HasDependencies());
    EXPECT_TRUE(result.HasCompleteDependencyTable());
    EXPECT_TRUE(result.HasAccesses());
    EXPECT_TRUE(result.HasCompleteAccessTable());
    EXPECT_TRUE(result.HasTransitions());
    EXPECT_TRUE(result.HasCompleteTransitionTable());
    EXPECT_TRUE(result.HasPasses());
    EXPECT_TRUE(result.HasCompletePassTable());
    EXPECT_TRUE(result.HasCompleteCompileTables());
    ASSERT_EQ(result.transitionCount, 3u);
    ASSERT_EQ(result.dependencyCount, 2u);
    ASSERT_EQ(result.passCount, 3u);
    ASSERT_EQ(result.resourceCount, 2u);
    EXPECT_EQ(result.GetResourceType(colorHandle), RHI::ResourceType::Texture);
    EXPECT_EQ(result.GetResourceType(depthHandle), RHI::ResourceType::Texture);
    EXPECT_EQ(result.GetResourceType(result.resourceCount), RHI::ResourceType::Unknown);

    const RHI::FrameGraphPassCompileInfo* gbufferInfo = result.GetPass(0);
    ASSERT_NE(gbufferInfo, nullptr);
    EXPECT_FALSE(gbufferInfo->HasDependencies());
    EXPECT_TRUE(gbufferInfo->HasAccesses());
    EXPECT_TRUE(gbufferInfo->HasTransitions());
    EXPECT_EQ(gbufferInfo->dependencyOffset, 0u);
    EXPECT_EQ(gbufferInfo->dependencyCount, 0u);
    EXPECT_EQ(gbufferInfo->DependencyEndOffset(), 0u);
    EXPECT_EQ(gbufferInfo->transitionOffset, 0u);
    EXPECT_EQ(gbufferInfo->transitionCount, 2u);
    EXPECT_EQ(gbufferInfo->TransitionEndOffset(), 2u);
    EXPECT_EQ(gbufferInfo->accessOffset, 0u);
    EXPECT_EQ(gbufferInfo->accessCount, 2u);
    EXPECT_EQ(gbufferInfo->AccessEndOffset(), 2u);
    EXPECT_EQ(gbufferInfo->queueClass, RHI::QueueClass::Graphics);

    const RHI::FrameGraphPassCompileInfo* lightingInfo = result.GetPass(1);
    ASSERT_NE(lightingInfo, nullptr);
    EXPECT_TRUE(lightingInfo->HasDependencies());
    EXPECT_EQ(lightingInfo->dependencyOffset, 0u);
    EXPECT_EQ(lightingInfo->dependencyCount, 1u);
    EXPECT_EQ(lightingInfo->DependencyEndOffset(), 1u);
    EXPECT_EQ(lightingInfo->transitionOffset, 2u);
    EXPECT_EQ(lightingInfo->transitionCount, 1u);
    EXPECT_EQ(lightingInfo->TransitionEndOffset(), 3u);
    EXPECT_EQ(lightingInfo->accessOffset, 2u);
    EXPECT_EQ(lightingInfo->accessCount, 1u);
    EXPECT_EQ(lightingInfo->AccessEndOffset(), 3u);

    const RHI::FrameGraphPassCompileInfo* postInfo = result.GetPass(2);
    ASSERT_NE(postInfo, nullptr);
    EXPECT_TRUE(postInfo->HasDependencies());
    EXPECT_TRUE(postInfo->HasAccesses());
    EXPECT_FALSE(postInfo->HasTransitions());
    EXPECT_EQ(postInfo->dependencyOffset, 1u);
    EXPECT_EQ(postInfo->dependencyCount, 1u);
    EXPECT_EQ(postInfo->DependencyEndOffset(), 2u);
    EXPECT_EQ(postInfo->transitionOffset, 3u);
    EXPECT_EQ(postInfo->transitionCount, 0u);
    EXPECT_EQ(postInfo->TransitionEndOffset(), 3u);
    EXPECT_EQ(postInfo->accessOffset, 3u);
    EXPECT_EQ(postInfo->accessCount, 1u);
    EXPECT_EQ(postInfo->AccessEndOffset(), 4u);
    EXPECT_EQ(result.GetPass(3), nullptr);
    ASSERT_EQ(result.accessCount, 4u);

    const RHI::FrameGraphCompiledPassDependency* firstDependency = result.GetDependency(0);
    ASSERT_NE(firstDependency, nullptr);
    EXPECT_TRUE(firstDependency->IsValid());
    EXPECT_EQ(firstDependency->passIndex, 1u);
    EXPECT_EQ(firstDependency->dependencyIndex, 0u);
    EXPECT_EQ(firstDependency->dependencyPassIndex, 0u);
    const RHI::FrameGraphCompiledPassDependency* secondDependency = result.GetDependency(1);
    ASSERT_NE(secondDependency, nullptr);
    EXPECT_EQ(secondDependency->passIndex, 2u);
    EXPECT_EQ(secondDependency->dependencyIndex, 0u);
    EXPECT_EQ(secondDependency->dependencyPassIndex, 1u);
    EXPECT_EQ(result.GetDependency(2), nullptr);

    const RHI::FrameGraphCompiledAccess* firstAccess = result.GetAccess(0);
    ASSERT_NE(firstAccess, nullptr);
    EXPECT_EQ(firstAccess->passIndex, 0u);
    EXPECT_EQ(firstAccess->accessIndex, 0u);
    EXPECT_EQ(firstAccess->resourceIndex, colorHandle.index);
    EXPECT_EQ(firstAccess->state, RHI::ResourceState::RenderTarget);
    EXPECT_EQ(firstAccess->queueClass, RHI::QueueClass::Graphics);
    EXPECT_EQ(firstAccess->access, RHI::FrameGraphAccessType::Write);
    EXPECT_TRUE(firstAccess->Writes());

    const RHI::FrameGraphCompiledAccess* postAccess = result.GetAccess(3);
    ASSERT_NE(postAccess, nullptr);
    EXPECT_EQ(postAccess->passIndex, 2u);
    EXPECT_EQ(postAccess->accessIndex, 0u);
    EXPECT_EQ(postAccess->resourceIndex, colorHandle.index);
    EXPECT_EQ(postAccess->state, RHI::ResourceState::ShaderRead);
    EXPECT_EQ(postAccess->access, RHI::FrameGraphAccessType::Read);
    EXPECT_FALSE(postAccess->Writes());
    EXPECT_EQ(result.GetAccess(4), nullptr);

    const RHI::FrameGraphTransition* first = result.GetTransition(0);
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first->passIndex, 0u);
    EXPECT_EQ(first->accessIndex, 0u);
    EXPECT_EQ(first->resourceIndex, colorHandle.index);
    EXPECT_EQ(first->before, RHI::ResourceState::Common);
    EXPECT_EQ(first->after, RHI::ResourceState::RenderTarget);
    EXPECT_EQ(first->queueClass, RHI::QueueClass::Graphics);
    EXPECT_EQ(first->access, RHI::FrameGraphAccessType::Write);

    const RHI::FrameGraphTransition* second = result.GetTransition(1);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(second->resourceIndex, depthHandle.index);
    EXPECT_EQ(second->before, RHI::ResourceState::Undefined);
    EXPECT_EQ(second->after, RHI::ResourceState::DepthWrite);

    const RHI::FrameGraphTransition* third = result.GetTransition(2);
    ASSERT_NE(third, nullptr);
    EXPECT_EQ(third->passIndex, 1u);
    EXPECT_EQ(third->resourceIndex, colorHandle.index);
    EXPECT_EQ(third->before, RHI::ResourceState::RenderTarget);
    EXPECT_EQ(third->after, RHI::ResourceState::ShaderRead);

    EXPECT_EQ(result.GetFinalResourceState(colorHandle), RHI::ResourceState::ShaderRead);
    EXPECT_EQ(result.GetFinalResourceState(depthHandle), RHI::ResourceState::DepthWrite);
    EXPECT_EQ(result.GetFinalResourceState({RHI::kInvalidFrameGraphResourceIndex}),
              RHI::ResourceState::Undefined);
    EXPECT_EQ(result.GetFinalResourceState({result.resourceCount}), RHI::ResourceState::Undefined);

    RHI::FrameGraphCompileResult invalidResource = result;
    invalidResource.resourceTypes[0] = RHI::ResourceType::Unknown;
    EXPECT_FALSE(invalidResource.HasCompleteResourceTable());
    EXPECT_FALSE(invalidResource.HasCompleteCompileTables());

    RHI::FrameGraphCompileResult invalidFinalResourceState = result;
    invalidFinalResourceState.finalResourceStates[0] =
        static_cast<RHI::ResourceState>(255);
    EXPECT_FALSE(invalidFinalResourceState.HasCompleteResourceTable());
    EXPECT_FALSE(invalidFinalResourceState.HasCompleteCompileTables());

    RHI::FrameGraphCompileResult invalidDependency = result;
    invalidDependency.dependencies[0].dependencyPassIndex =
        RHI::kInvalidFrameGraphPassIndex;
    EXPECT_FALSE(invalidDependency.HasCompleteDependencyTable());
    EXPECT_FALSE(invalidDependency.HasCompleteCompileTables());

    RHI::FrameGraphCompileResult dependencyOutsidePassRange = result;
    dependencyOutsidePassRange.dependencies[0].passIndex = 2;
    EXPECT_FALSE(dependencyOutsidePassRange.HasCompleteDependencyTable());
    EXPECT_FALSE(dependencyOutsidePassRange.HasCompleteCompileTables());

    RHI::FrameGraphCompileResult dependencyLocalIndexOutsidePassRange = result;
    dependencyLocalIndexOutsidePassRange.dependencies[0].dependencyIndex =
        dependencyLocalIndexOutsidePassRange.passes[1].dependencyCount;
    EXPECT_FALSE(dependencyLocalIndexOutsidePassRange.HasCompleteDependencyTable());
    EXPECT_FALSE(dependencyLocalIndexOutsidePassRange.HasCompleteCompileTables());

    RHI::FrameGraphCompileResult invalidAccess = result;
    invalidAccess.accesses[0].resourceIndex = RHI::kInvalidFrameGraphResourceIndex;
    EXPECT_FALSE(invalidAccess.HasCompleteAccessTable());
    EXPECT_FALSE(invalidAccess.HasCompleteCompileTables());

    RHI::FrameGraphCompileResult danglingAccess = result;
    danglingAccess.accesses[0].resourceIndex = result.resourceCount;
    EXPECT_FALSE(danglingAccess.HasCompleteAccessTable());
    EXPECT_FALSE(danglingAccess.HasCompleteCompileTables());

    RHI::FrameGraphCompileResult accessOutsidePassRange = result;
    accessOutsidePassRange.accesses[0].passIndex = 1;
    EXPECT_FALSE(accessOutsidePassRange.HasCompleteAccessTable());
    EXPECT_FALSE(accessOutsidePassRange.HasCompleteCompileTables());

    RHI::FrameGraphCompileResult accessLocalIndexOutsidePassRange = result;
    accessLocalIndexOutsidePassRange.accesses[0].accessIndex =
        accessLocalIndexOutsidePassRange.passes[0].accessCount;
    EXPECT_FALSE(accessLocalIndexOutsidePassRange.HasCompleteAccessTable());
    EXPECT_FALSE(accessLocalIndexOutsidePassRange.HasCompleteCompileTables());

    RHI::FrameGraphCompileResult accessQueueMismatch = result;
    accessQueueMismatch.accesses[0].queueClass = RHI::QueueClass::Compute;
    EXPECT_FALSE(accessQueueMismatch.HasCompleteAccessTable());
    EXPECT_FALSE(accessQueueMismatch.HasCompleteCompileTables());

    RHI::FrameGraphCompileResult invalidTransition = result;
    invalidTransition.transitions[0].resourceIndex = RHI::kInvalidFrameGraphResourceIndex;
    EXPECT_FALSE(invalidTransition.HasCompleteTransitionTable());
    EXPECT_FALSE(invalidTransition.HasCompleteCompileTables());

    RHI::FrameGraphCompileResult danglingTransition = result;
    danglingTransition.transitions[0].resourceIndex = result.resourceCount;
    EXPECT_FALSE(danglingTransition.HasCompleteTransitionTable());
    EXPECT_FALSE(danglingTransition.HasCompleteCompileTables());

    RHI::FrameGraphCompileResult transitionOutsidePassRange = result;
    transitionOutsidePassRange.transitions[0].passIndex = 1;
    EXPECT_FALSE(transitionOutsidePassRange.HasCompleteTransitionTable());
    EXPECT_FALSE(transitionOutsidePassRange.HasCompleteCompileTables());

    RHI::FrameGraphCompileResult transitionAccessIndexOutsidePassRange = result;
    transitionAccessIndexOutsidePassRange.transitions[0].accessIndex =
        transitionAccessIndexOutsidePassRange.passes[0].accessCount;
    EXPECT_FALSE(transitionAccessIndexOutsidePassRange.HasCompleteTransitionTable());
    EXPECT_FALSE(transitionAccessIndexOutsidePassRange.HasCompleteCompileTables());

    RHI::FrameGraphCompileResult transitionQueueMismatch = result;
    transitionQueueMismatch.transitions[0].queueClass = RHI::QueueClass::Compute;
    EXPECT_FALSE(transitionQueueMismatch.HasCompleteTransitionTable());
    EXPECT_FALSE(transitionQueueMismatch.HasCompleteCompileTables());

    RHI::FrameGraphCompileResult invalidPassRange = result;
    invalidPassRange.passes[1].accessOffset = invalidPassRange.passes[1].accessOffset + 1;
    EXPECT_FALSE(invalidPassRange.HasCompletePassTable());
    EXPECT_FALSE(invalidPassRange.HasCompleteCompileTables());

    RHI::FrameGraphCompileResult extraAccess = result;
    ++extraAccess.accessCount;
    EXPECT_FALSE(extraAccess.HasCompleteAccessTable());
    EXPECT_FALSE(extraAccess.HasCompletePassTable());
    EXPECT_FALSE(extraAccess.HasCompleteCompileTables());
}

TEST(RHITypesTest, FrameGraphPassCompileInfoSaturatesOverflowingRanges) {
    const uint32_t maxOffset = std::numeric_limits<uint32_t>::max();
    EXPECT_EQ(RHI::FrameGraphRangeEndOffset(12, 4), 16u);
    EXPECT_EQ(RHI::FrameGraphRangeEndOffset(maxOffset - 3, 3), maxOffset);
    EXPECT_EQ(RHI::FrameGraphRangeEndOffset(maxOffset - 3, 4), maxOffset);

    RHI::FrameGraphPassCompileInfo pass;
    pass.dependencyOffset = maxOffset - 1;
    pass.dependencyCount = 4;
    pass.transitionOffset = maxOffset;
    pass.transitionCount = 1;
    pass.accessOffset = maxOffset - 5;
    pass.accessCount = 6;

    EXPECT_EQ(pass.DependencyEndOffset(), maxOffset);
    EXPECT_EQ(pass.TransitionEndOffset(), maxOffset);
    EXPECT_EQ(pass.AccessEndOffset(), maxOffset);
}

TEST(RHITypesTest, FrameGraphPassTableRejectsAggregateCountsBeyondStorage) {
    RHI::FrameGraphCompileResult result;
    result.passCount = 1;
    result.dependencyCount = static_cast<uint32_t>(RHI::kMaxFrameGraphDependencies + 1);
    result.passes[0].dependencyCount = result.dependencyCount;
    EXPECT_FALSE(result.HasCompletePassTable());

    result = {};
    result.passCount = 1;
    result.transitionCount = static_cast<uint32_t>(RHI::kMaxFrameGraphTransitions + 1);
    result.passes[0].transitionCount = result.transitionCount;
    EXPECT_FALSE(result.HasCompletePassTable());

    result = {};
    result.passCount = 1;
    result.accessCount = static_cast<uint32_t>(RHI::kMaxFrameGraphAccesses + 1);
    result.passes[0].accessCount = result.accessCount;
    EXPECT_FALSE(result.HasCompletePassTable());
}

TEST(RHITypesTest, FrameGraphPassAccessCarriesShaderStages) {
    RHI::FrameGraphDesc graph;
    RHI::FrameGraphResourceDesc texture;
    texture.debugName = "sampled";
    texture.type = RHI::ResourceType::Texture;
    texture.usage = RHI::ResourceUsageFlag(RHI::ResourceUsage::ShaderRead);
    texture.initialState = RHI::ResourceState::Common;
    RHI::FrameGraphResourceHandle textureHandle;
    ASSERT_TRUE(RHI::AddFrameGraphResource(graph, texture, &textureHandle));

    RHI::FrameGraphPassDesc lighting;
    lighting.debugName = "lighting";
    const RHI::ShaderStageFlags shaderStages = RHI::ShaderStageFlag(RHI::ShaderStage::Fragment);
    ASSERT_TRUE(RHI::AddFrameGraphPassAccess(
        lighting,
        RHI::MakeFrameGraphPassResourceAccess(textureHandle,
                                              RHI::ResourceState::ShaderRead,
                                              RHI::FrameGraphAccessType::Read,
                                              shaderStages)));
    ASSERT_TRUE(RHI::AddFrameGraphPass(graph, lighting));

    const RHI::FrameGraphCompileResult result = RHI::CompileFrameGraphTransitions(graph);
    ASSERT_TRUE(result);
    ASSERT_EQ(result.accessCount, 1u);
    ASSERT_EQ(result.transitionCount, 1u);
    const RHI::FrameGraphCompiledAccess* access = result.GetAccess(0);
    ASSERT_NE(access, nullptr);
    EXPECT_TRUE(access->HasShaderStages());
    EXPECT_EQ(access->shaderStages, shaderStages);
    const RHI::FrameGraphTransition* transition = result.GetTransition(0);
    ASSERT_NE(transition, nullptr);
    EXPECT_EQ(transition->shaderStages, shaderStages);
}

TEST(RHITypesTest, FrameGraphPassAccessAcceptsComputeStageOnComputeQueue) {
    RHI::FrameGraphDesc graph;
    RHI::FrameGraphResourceDesc buffer;
    buffer.debugName = "compute buffer";
    buffer.type = RHI::ResourceType::Buffer;
    buffer.usage = RHI::ResourceUsageFlag(RHI::ResourceUsage::ShaderRead);
    buffer.initialState = RHI::ResourceState::Common;
    RHI::FrameGraphResourceHandle bufferHandle;
    ASSERT_TRUE(RHI::AddFrameGraphResource(graph, buffer, &bufferHandle));

    RHI::FrameGraphPassDesc computePass;
    computePass.debugName = "compute";
    computePass.queueClass = RHI::QueueClass::Compute;
    const RHI::ShaderStageFlags shaderStages = RHI::ShaderStageFlag(RHI::ShaderStage::Compute);
    ASSERT_TRUE(RHI::AddFrameGraphPassAccess(
        computePass,
        RHI::MakeFrameGraphPassResourceAccess(bufferHandle,
                                              RHI::ResourceState::ShaderRead,
                                              RHI::FrameGraphAccessType::Read,
                                              shaderStages)));
    ASSERT_TRUE(RHI::AddFrameGraphPass(graph, computePass));

    const RHI::FrameGraphCompileResult result = RHI::CompileFrameGraphTransitions(graph);
    ASSERT_TRUE(result);
    ASSERT_EQ(result.accessCount, 1u);
    const RHI::FrameGraphCompiledAccess* access = result.GetAccess(0);
    ASSERT_NE(access, nullptr);
    EXPECT_EQ(access->queueClass, RHI::QueueClass::Compute);
    EXPECT_EQ(access->shaderStages, shaderStages);
}

TEST(RHITypesTest, FrameGraphResourceTransitionPlanAppliesLiveResourcesSequentially) {
    RHI::FrameGraphDesc graph;
    RHI::FrameGraphResourceDesc color;
    color.debugName = "color";
    color.type = RHI::ResourceType::Texture;
    color.usage = RHI::ResourceUsage::RenderTarget | RHI::ResourceUsage::ShaderRead;
    color.initialState = RHI::ResourceState::Common;
    RHI::FrameGraphResourceHandle colorHandle;
    ASSERT_TRUE(RHI::AddFrameGraphResource(graph, color, &colorHandle));

    RHI::FrameGraphResourceDesc depth;
    depth.debugName = "depth";
    depth.type = RHI::ResourceType::Texture;
    depth.usage = RHI::ResourceUsageFlag(RHI::ResourceUsage::DepthStencil);
    depth.initialState = RHI::ResourceState::Undefined;
    RHI::FrameGraphResourceHandle depthHandle;
    ASSERT_TRUE(RHI::AddFrameGraphResource(graph, depth, &depthHandle));

    RHI::FrameGraphPassDesc renderPass;
    renderPass.debugName = "render";
    ASSERT_TRUE(RHI::AddFrameGraphPassAccess(
        renderPass,
        RHI::MakeFrameGraphPassResourceAccess(colorHandle,
                                              RHI::ResourceState::RenderTarget,
                                              RHI::FrameGraphAccessType::Write)));
    ASSERT_TRUE(RHI::AddFrameGraphPassAccess(
        renderPass,
        RHI::MakeFrameGraphPassResourceAccess(depthHandle,
                                              RHI::ResourceState::DepthWrite,
                                              RHI::FrameGraphAccessType::Write)));
    ASSERT_TRUE(RHI::AddFrameGraphPass(graph, renderPass));

    RHI::FrameGraphPassDesc lightingPass;
    lightingPass.debugName = "lighting";
    ASSERT_TRUE(RHI::AddFrameGraphPassAccess(
        lightingPass,
        RHI::MakeFrameGraphPassResourceAccess(colorHandle,
                                              RHI::ResourceState::ShaderRead,
                                              RHI::FrameGraphAccessType::Read,
                                              RHI::ShaderStageFlag(RHI::ShaderStage::Fragment))));
    ASSERT_TRUE(RHI::AddFrameGraphPass(graph, lightingPass));

    const RHI::FrameGraphCompileResult compileResult = RHI::CompileFrameGraphTransitions(graph);
    ASSERT_TRUE(compileResult);
    ASSERT_EQ(compileResult.transitionCount, 3u);

    TestResource colorResource(RHI::ResourceUsage::RenderTarget | RHI::ResourceUsage::ShaderRead,
                               RHI::ResourceType::Texture);
    colorResource.SetCurrentState(RHI::ResourceState::Common);
    TestResource depthResource(RHI::ResourceUsageFlag(RHI::ResourceUsage::DepthStencil),
                               RHI::ResourceType::Texture);
    std::array<RHI::Resource*, RHI::kMaxFrameGraphResources> resources{};
    resources[colorHandle.index] = &colorResource;
    resources[depthHandle.index] = &depthResource;

    const RHI::FrameGraphResourceTransitionPlan plan =
        RHI::BuildFrameGraphResourceTransitionPlan(compileResult, resources);
    ASSERT_TRUE(plan);
    EXPECT_TRUE(plan.HasTransitions());
    EXPECT_TRUE(plan.HasCompleteTransitionTable());
    ASSERT_EQ(plan.transitionCount, 3u);
    ASSERT_NE(plan.GetTransition(0), nullptr);
    EXPECT_EQ(plan.GetTransition(0)->resource, &colorResource);
    EXPECT_EQ(plan.GetTransition(0)->shaderStages, 0u);
    EXPECT_FALSE(plan.GetTransition(0)->HasShaderStages());
    ASSERT_NE(plan.GetTransition(2), nullptr);
    EXPECT_EQ(plan.GetTransition(2)->resource, &colorResource);
    EXPECT_EQ(plan.GetTransition(2)->shaderStages,
              RHI::ShaderStageFlag(RHI::ShaderStage::Fragment));
    EXPECT_TRUE(plan.GetTransition(2)->HasShaderStages());

    TestResource passColorResource(RHI::ResourceUsage::RenderTarget | RHI::ResourceUsage::ShaderRead,
                                   RHI::ResourceType::Texture);
    passColorResource.SetCurrentState(RHI::ResourceState::Common);
    TestResource passDepthResource(RHI::ResourceUsageFlag(RHI::ResourceUsage::DepthStencil),
                                   RHI::ResourceType::Texture);
    std::array<RHI::Resource*, RHI::kMaxFrameGraphResources> passResources{};
    passResources[colorHandle.index] = &passColorResource;
    passResources[depthHandle.index] = &passDepthResource;

    RHI::FrameGraphResourceTransitionPlan renderPassPlan =
        RHI::BuildFrameGraphPassResourceTransitionPlan(compileResult, 0, passResources);
    ASSERT_TRUE(renderPassPlan);
    ASSERT_EQ(renderPassPlan.transitionCount, 2u);
    ASSERT_NE(renderPassPlan.GetTransition(0), nullptr);
    EXPECT_EQ(renderPassPlan.GetTransition(0)->resource, &passColorResource);
    ASSERT_NE(renderPassPlan.GetTransition(1), nullptr);
    EXPECT_EQ(renderPassPlan.GetTransition(1)->resource, &passDepthResource);

    RHI::FrameGraphExecutionValidation passValidation;
    uint32_t passTransitionCount = 0;
    EXPECT_TRUE(RHI::ApplyFrameGraphPassResourceTransitionPlan(
        compileResult,
        0,
        passResources,
        &passValidation,
        &passTransitionCount));
    EXPECT_TRUE(passValidation);
    EXPECT_EQ(passTransitionCount, 2u);
    EXPECT_EQ(passColorResource.GetCurrentState(), RHI::ResourceState::RenderTarget);
    EXPECT_EQ(passDepthResource.GetCurrentState(), RHI::ResourceState::DepthWrite);

    RHI::FrameGraphResourceTransitionPlan lightingPassPlan =
        RHI::BuildFrameGraphPassResourceTransitionPlan(compileResult, 1, passResources);
    ASSERT_TRUE(lightingPassPlan);
    ASSERT_EQ(lightingPassPlan.transitionCount, 1u);
    ASSERT_NE(lightingPassPlan.GetTransition(0), nullptr);
    EXPECT_EQ(lightingPassPlan.GetTransition(0)->resource, &passColorResource);
    EXPECT_EQ(lightingPassPlan.GetTransition(0)->shaderStages,
              RHI::ShaderStageFlag(RHI::ShaderStage::Fragment));

    EXPECT_TRUE(RHI::ApplyFrameGraphPassResourceTransitionPlan(
        compileResult,
        1,
        passResources,
        &passValidation,
        &passTransitionCount));
    EXPECT_TRUE(passValidation);
    EXPECT_EQ(passTransitionCount, 1u);
    EXPECT_EQ(passColorResource.GetCurrentState(), RHI::ResourceState::ShaderRead);

    RHI::FrameGraphExecutionValidation executionValidation;
    uint32_t appliedTransitionCount = 0;
    EXPECT_TRUE(RHI::ApplyFrameGraphResourceTransitionPlan(
        compileResult,
        resources,
        &executionValidation,
        &appliedTransitionCount));
    EXPECT_TRUE(executionValidation);
    EXPECT_EQ(appliedTransitionCount, 3u);
    EXPECT_EQ(colorResource.GetCurrentState(), RHI::ResourceState::ShaderRead);
    EXPECT_EQ(depthResource.GetCurrentState(), RHI::ResourceState::DepthWrite);
}

TEST(RHITypesTest, FrameGraphResourceTransitionPlanReportsExecutionErrors) {
    RHI::FrameGraphDesc graph;
    RHI::FrameGraphResourceDesc color;
    color.debugName = "color";
    color.type = RHI::ResourceType::Texture;
    color.usage = RHI::ResourceUsageFlag(RHI::ResourceUsage::RenderTarget);
    color.initialState = RHI::ResourceState::Common;
    RHI::FrameGraphResourceHandle colorHandle;
    ASSERT_TRUE(RHI::AddFrameGraphResource(graph, color, &colorHandle));

    RHI::FrameGraphPassDesc pass;
    ASSERT_TRUE(RHI::AddFrameGraphPassAccess(
        pass,
        RHI::MakeFrameGraphPassResourceAccess(colorHandle,
                                              RHI::ResourceState::RenderTarget,
                                              RHI::FrameGraphAccessType::Write)));
    ASSERT_TRUE(RHI::AddFrameGraphPass(graph, pass));

    const RHI::FrameGraphCompileResult compileResult = RHI::CompileFrameGraphTransitions(graph);
    ASSERT_TRUE(compileResult);

    std::array<RHI::Resource*, RHI::kMaxFrameGraphResources> resources{};
    RHI::FrameGraphResourceTransitionPlan plan =
        RHI::BuildFrameGraphResourceTransitionPlan(compileResult, resources);
    EXPECT_FALSE(plan);
    EXPECT_EQ(plan.validation.error, RHI::FrameGraphExecutionError::MissingResourceBinding);
    EXPECT_EQ(plan.validation.resourceIndex, colorHandle.index);
    EXPECT_EQ(plan.validation.passIndex, 0u);
    EXPECT_EQ(plan.validation.accessIndex, 0u);
    EXPECT_EQ(plan.validation.before, RHI::ResourceState::Common);
    EXPECT_EQ(plan.validation.after, RHI::ResourceState::RenderTarget);
    EXPECT_EQ(plan.validation.queueClass, RHI::QueueClass::Graphics);
    EXPECT_EQ(plan.validation.shaderStages, 0u);
    EXPECT_FALSE(plan.validation.HasShaderStages());
    EXPECT_STREQ(RHI::FrameGraphExecutionErrorName(plan.validation.error),
                 "missing_resource_binding");

    TestResource wrongTypeResource(RHI::ResourceUsageFlag(RHI::ResourceUsage::RenderTarget),
                                   RHI::ResourceType::Buffer);
    resources[colorHandle.index] = &wrongTypeResource;
    plan = RHI::BuildFrameGraphResourceTransitionPlan(compileResult, resources);
    EXPECT_FALSE(plan);
    EXPECT_EQ(plan.transitionCount, 0u);
    EXPECT_EQ(plan.validation.error, RHI::FrameGraphExecutionError::ResourceTypeMismatch);
    EXPECT_EQ(plan.validation.resourceIndex, colorHandle.index);
    EXPECT_EQ(plan.validation.passIndex, 0u);
    EXPECT_EQ(plan.validation.accessIndex, 0u);
    EXPECT_EQ(plan.validation.expectedResourceType, RHI::ResourceType::Texture);
    EXPECT_EQ(plan.validation.actualResourceType, RHI::ResourceType::Buffer);
    EXPECT_STREQ(RHI::FrameGraphExecutionErrorName(plan.validation.error),
                 "resource_type_mismatch");

    TestResource colorResource(RHI::ResourceUsageFlag(RHI::ResourceUsage::RenderTarget),
                               RHI::ResourceType::Texture);
    resources[colorHandle.index] = &colorResource;

    plan = RHI::BuildFrameGraphPassResourceTransitionPlan(
        compileResult,
        compileResult.passCount,
        resources);
    EXPECT_FALSE(plan);
    EXPECT_EQ(plan.validation.error, RHI::FrameGraphExecutionError::InvalidPassIndex);
    EXPECT_EQ(plan.validation.passIndex, compileResult.passCount);
    EXPECT_STREQ(RHI::FrameGraphExecutionErrorName(plan.validation.error),
                 "invalid_pass_index");

    RHI::FrameGraphExecutionValidation executionValidation;
    uint32_t appliedTransitionCount = 0;
    EXPECT_FALSE(RHI::ApplyFrameGraphResourceTransitionPlan(
        compileResult,
        resources,
        &executionValidation,
        &appliedTransitionCount));
    EXPECT_EQ(appliedTransitionCount, 1u);
    EXPECT_EQ(executionValidation.error, RHI::FrameGraphExecutionError::InvalidResourceTransition);
    EXPECT_EQ(executionValidation.resourceIndex, colorHandle.index);
    EXPECT_EQ(executionValidation.passIndex, 0u);
    EXPECT_EQ(executionValidation.accessIndex, 0u);
    EXPECT_EQ(executionValidation.before, RHI::ResourceState::Common);
    EXPECT_EQ(executionValidation.after, RHI::ResourceState::RenderTarget);
    EXPECT_EQ(executionValidation.queueClass, RHI::QueueClass::Graphics);
    EXPECT_EQ(executionValidation.shaderStages, 0u);
    EXPECT_FALSE(executionValidation.HasShaderStages());
    EXPECT_EQ(executionValidation.resourceTransitionValidation.error,
              RHI::ResourceTransitionError::StaleBeforeState);
    EXPECT_EQ(executionValidation.resourceTransitionValidation.current,
              RHI::ResourceState::Undefined);

    const RHI::FrameGraphCompileResult invalidCompileResult =
        RHI::CompileFrameGraphTransitions(RHI::FrameGraphDesc{});
    plan = RHI::BuildFrameGraphResourceTransitionPlan(invalidCompileResult, resources);
    EXPECT_FALSE(plan);
    EXPECT_EQ(plan.validation.error, RHI::FrameGraphExecutionError::InvalidCompileResult);
    EXPECT_EQ(plan.validation.frameGraphValidation.error,
              RHI::FrameGraphDescriptorError::MissingResource);

    RHI::FrameGraphCompileResult corruptCompileResult = compileResult;
    corruptCompileResult.accesses[0].resourceIndex = RHI::kInvalidFrameGraphResourceIndex;
    EXPECT_FALSE(corruptCompileResult.HasCompleteCompileTables());

    colorResource.SetCurrentState(RHI::ResourceState::Common);
    plan = RHI::BuildFrameGraphResourceTransitionPlan(corruptCompileResult, resources);
    EXPECT_FALSE(plan);
    EXPECT_EQ(plan.transitionCount, 0u);
    EXPECT_EQ(plan.validation.error, RHI::FrameGraphExecutionError::InvalidCompileResult);
    EXPECT_EQ(plan.validation.frameGraphValidation.error,
              RHI::FrameGraphDescriptorError::None);

    plan = RHI::BuildFrameGraphPassResourceTransitionPlan(
        corruptCompileResult,
        0,
        resources);
    EXPECT_FALSE(plan);
    EXPECT_EQ(plan.transitionCount, 0u);
    EXPECT_EQ(plan.validation.error, RHI::FrameGraphExecutionError::InvalidCompileResult);
    EXPECT_EQ(plan.validation.frameGraphValidation.error,
              RHI::FrameGraphDescriptorError::None);

    appliedTransitionCount = 99;
    executionValidation = {};
    EXPECT_FALSE(RHI::ApplyFrameGraphResourceTransitionPlan(
        corruptCompileResult,
        resources,
        &executionValidation,
        &appliedTransitionCount));
    EXPECT_EQ(appliedTransitionCount, 0u);
    EXPECT_EQ(executionValidation.error, RHI::FrameGraphExecutionError::InvalidCompileResult);
    EXPECT_EQ(executionValidation.frameGraphValidation.error,
              RHI::FrameGraphDescriptorError::None);
    EXPECT_EQ(colorResource.GetCurrentState(), RHI::ResourceState::Common);

    appliedTransitionCount = 99;
    executionValidation = {};
    EXPECT_FALSE(RHI::ApplyFrameGraphPassResourceTransitionPlan(
        corruptCompileResult,
        0,
        resources,
        &executionValidation,
        &appliedTransitionCount));
    EXPECT_EQ(appliedTransitionCount, 0u);
    EXPECT_EQ(executionValidation.error, RHI::FrameGraphExecutionError::InvalidCompileResult);
    EXPECT_EQ(executionValidation.frameGraphValidation.error,
              RHI::FrameGraphDescriptorError::None);
    EXPECT_EQ(colorResource.GetCurrentState(), RHI::ResourceState::Common);
}

TEST(RHITypesTest, FrameGraphValidationReportsDescriptorErrors) {
    RHI::FrameGraphDesc graph;
    RHI::FrameGraphDescriptorValidation validation = RHI::ValidateFrameGraphDesc(graph);
    EXPECT_EQ(validation.error, RHI::FrameGraphDescriptorError::MissingResource);

    RHI::FrameGraphDesc invalidResourceGraph;
    RHI::FrameGraphResourceDesc unknownResource;
    unknownResource.debugName = "unknown";
    unknownResource.usage = RHI::ResourceUsageFlag(RHI::ResourceUsage::ShaderRead);
    unknownResource.initialState = RHI::ResourceState::Common;
    RHI::FrameGraphResourceHandle unknownResourceHandle;
    ASSERT_TRUE(RHI::AddFrameGraphResource(invalidResourceGraph,
                                           unknownResource,
                                           &unknownResourceHandle));
    RHI::FrameGraphPassDesc unknownResourcePass;
    ASSERT_TRUE(RHI::AddFrameGraphPassAccess(
        unknownResourcePass,
        RHI::MakeFrameGraphPassResourceAccess(unknownResourceHandle,
                                              RHI::ResourceState::ShaderRead,
                                              RHI::FrameGraphAccessType::Read)));
    ASSERT_TRUE(RHI::AddFrameGraphPass(invalidResourceGraph, unknownResourcePass));
    EXPECT_FALSE(invalidResourceGraph.HasCompleteResourceTable());
    validation = RHI::ValidateFrameGraphDesc(invalidResourceGraph);
    EXPECT_EQ(validation.error, RHI::FrameGraphDescriptorError::InvalidResourceType);
    EXPECT_EQ(validation.resourceIndex, 0u);

    RHI::FrameGraphResourceDesc texture;
    texture.debugName = "texture";
    texture.type = RHI::ResourceType::Texture;
    texture.usage = RHI::ResourceUsage::ShaderRead | RHI::ResourceUsage::CopySource;
    texture.initialState = RHI::ResourceState::Common;
    RHI::FrameGraphResourceHandle textureHandle;
    ASSERT_TRUE(RHI::AddFrameGraphResource(graph, texture, &textureHandle));

    validation = RHI::ValidateFrameGraphDesc(graph);
    EXPECT_EQ(validation.error, RHI::FrameGraphDescriptorError::MissingPass);

    RHI::FrameGraphPassDesc pass;
    ASSERT_TRUE(RHI::AddFrameGraphPass(graph, pass));
    validation = RHI::ValidateFrameGraphDesc(graph);
    EXPECT_EQ(validation.error, RHI::FrameGraphDescriptorError::MissingPassAccess);
    EXPECT_EQ(validation.passIndex, 0u);

    graph.passes[0].accessCount = 1;
    graph.passes[0].accesses[0] = RHI::MakeFrameGraphPassResourceAccess(
        RHI::MakeFrameGraphResourceHandle(99),
        RHI::ResourceState::ShaderRead,
        RHI::FrameGraphAccessType::Read);
    validation = RHI::ValidateFrameGraphDesc(graph);
    EXPECT_EQ(validation.error, RHI::FrameGraphDescriptorError::InvalidResourceIndex);
    EXPECT_EQ(validation.resourceIndex, 99u);

    graph.passes[0].accesses[0] = RHI::MakeFrameGraphPassResourceAccess(
        textureHandle,
        RHI::ResourceState::RenderTarget,
        RHI::FrameGraphAccessType::Write);
    validation = RHI::ValidateFrameGraphDesc(graph);
    EXPECT_EQ(validation.error, RHI::FrameGraphDescriptorError::UnsupportedTargetState);
    EXPECT_EQ(validation.resourceIndex, textureHandle.index);
    EXPECT_EQ(validation.state, RHI::ResourceState::RenderTarget);

    graph.passes[0].queueClass = RHI::QueueClass::Copy;
    graph.passes[0].accesses[0] = RHI::MakeFrameGraphPassResourceAccess(
        textureHandle,
        RHI::ResourceState::ShaderRead,
        RHI::FrameGraphAccessType::Read);
    validation = RHI::ValidateFrameGraphDesc(graph);
    EXPECT_EQ(validation.error, RHI::FrameGraphDescriptorError::UnsupportedQueueClass);
    EXPECT_EQ(validation.queueClass, RHI::QueueClass::Copy);

    graph.passes[0].queueClass = RHI::QueueClass::Graphics;
    graph.passes[0].accesses[0].access = static_cast<RHI::FrameGraphAccessType>(255);
    validation = RHI::ValidateFrameGraphDesc(graph);
    EXPECT_EQ(validation.error, RHI::FrameGraphDescriptorError::InvalidAccessType);

    graph.passes[0].accesses[0] = RHI::MakeFrameGraphPassResourceAccess(
        textureHandle,
        RHI::ResourceState::ShaderRead,
        RHI::FrameGraphAccessType::Read,
        static_cast<RHI::ShaderStageFlags>(0x80));
    validation = RHI::ValidateFrameGraphDesc(graph);
    EXPECT_EQ(validation.error, RHI::FrameGraphDescriptorError::InvalidShaderStage);
    EXPECT_EQ(validation.shaderStages, static_cast<RHI::ShaderStageFlags>(0x80));
    EXPECT_STREQ(RHI::FrameGraphDescriptorErrorName(validation.error), "invalid_shader_stage");

    graph.passes[0].accesses[0] = RHI::MakeFrameGraphPassResourceAccess(
        textureHandle,
        RHI::ResourceState::ShaderRead,
        RHI::FrameGraphAccessType::Read,
        RHI::ShaderStageFlag(RHI::ShaderStage::Compute));
    validation = RHI::ValidateFrameGraphDesc(graph);
    EXPECT_EQ(validation.error, RHI::FrameGraphDescriptorError::UnsupportedShaderStage);
    EXPECT_EQ(validation.shaderStages, RHI::ShaderStageFlag(RHI::ShaderStage::Compute));
    EXPECT_EQ(validation.queueClass, RHI::QueueClass::Graphics);
    EXPECT_STREQ(RHI::FrameGraphDescriptorErrorName(validation.error), "unsupported_shader_stage");

    graph.passes[0].accesses[0] = RHI::MakeFrameGraphPassResourceAccess(
        textureHandle,
        RHI::ResourceState::ShaderRead,
        RHI::FrameGraphAccessType::Read);
    ASSERT_TRUE(RHI::AddFrameGraphPassAccess(
        graph.passes[0],
        RHI::MakeFrameGraphPassResourceAccess(textureHandle,
                                              RHI::ResourceState::CopySource,
                                              RHI::FrameGraphAccessType::Read)));
    validation = RHI::ValidateFrameGraphDesc(graph);
    EXPECT_EQ(validation.error, RHI::FrameGraphDescriptorError::DuplicateResourceAccess);
    EXPECT_EQ(validation.accessIndex, 1u);
    EXPECT_EQ(validation.conflictAccessIndex, 0u);

    graph.resourceCount = static_cast<uint32_t>(RHI::kMaxFrameGraphResources + 1);
    validation = RHI::ValidateFrameGraphDesc(graph);
    EXPECT_EQ(validation.error, RHI::FrameGraphDescriptorError::TooManyResources);
}

TEST(RHITypesTest, FrameGraphValidationReportsDependencyErrors) {
    RHI::FrameGraphDesc graph;
    RHI::FrameGraphResourceDesc texture;
    texture.debugName = "texture";
    texture.type = RHI::ResourceType::Texture;
    texture.usage = RHI::ResourceUsage::RenderTarget | RHI::ResourceUsage::ShaderRead;
    texture.initialState = RHI::ResourceState::Common;
    RHI::FrameGraphResourceHandle textureHandle;
    ASSERT_TRUE(RHI::AddFrameGraphResource(graph, texture, &textureHandle));

    RHI::FrameGraphPassDesc pass;
    ASSERT_TRUE(RHI::AddFrameGraphPassAccess(
        pass,
        RHI::MakeFrameGraphPassResourceAccess(textureHandle,
                                              RHI::ResourceState::RenderTarget,
                                              RHI::FrameGraphAccessType::Write)));
    pass.dependencyCount = static_cast<uint32_t>(RHI::kMaxFrameGraphPassDependencies + 1);
    ASSERT_TRUE(RHI::AddFrameGraphPass(graph, pass));
    RHI::FrameGraphDescriptorValidation validation = RHI::ValidateFrameGraphDesc(graph);
    EXPECT_EQ(validation.error, RHI::FrameGraphDescriptorError::TooManyPassDependencies);
    EXPECT_EQ(validation.passIndex, 0u);
    EXPECT_EQ(validation.dependencyIndex, RHI::kMaxFrameGraphPassDependencies + 1u);
    EXPECT_EQ(validation.dependencyPassIndex, RHI::kInvalidFrameGraphPassIndex);
    EXPECT_STREQ(RHI::FrameGraphDescriptorErrorName(validation.error), "too_many_pass_dependencies");

    graph = {};
    ASSERT_TRUE(RHI::AddFrameGraphResource(graph, texture, &textureHandle));
    pass = {};
    ASSERT_TRUE(RHI::AddFrameGraphPassDependency(pass, RHI::MakeFrameGraphPassDependency(0)));
    ASSERT_TRUE(RHI::AddFrameGraphPassAccess(
        pass,
        RHI::MakeFrameGraphPassResourceAccess(textureHandle,
                                              RHI::ResourceState::RenderTarget,
                                              RHI::FrameGraphAccessType::Write)));
    ASSERT_TRUE(RHI::AddFrameGraphPass(graph, pass));
    validation = RHI::ValidateFrameGraphDesc(graph);
    EXPECT_EQ(validation.error, RHI::FrameGraphDescriptorError::InvalidPassDependency);
    EXPECT_EQ(validation.passIndex, 0u);
    EXPECT_EQ(validation.dependencyIndex, 0u);
    EXPECT_EQ(validation.dependencyPassIndex, 0u);
    EXPECT_STREQ(RHI::FrameGraphDescriptorErrorName(validation.error), "invalid_pass_dependency");

    graph = {};
    ASSERT_TRUE(RHI::AddFrameGraphResource(graph, texture, &textureHandle));
    RHI::FrameGraphPassDesc producer;
    ASSERT_TRUE(RHI::AddFrameGraphPassAccess(
        producer,
        RHI::MakeFrameGraphPassResourceAccess(textureHandle,
                                              RHI::ResourceState::RenderTarget,
                                              RHI::FrameGraphAccessType::Write)));
    ASSERT_TRUE(RHI::AddFrameGraphPass(graph, producer));

    RHI::FrameGraphPassDesc consumer;
    ASSERT_TRUE(RHI::AddFrameGraphPassDependency(consumer, RHI::MakeFrameGraphPassDependency(0)));
    ASSERT_TRUE(RHI::AddFrameGraphPassDependency(consumer, RHI::MakeFrameGraphPassDependency(0)));
    ASSERT_TRUE(RHI::AddFrameGraphPassAccess(
        consumer,
        RHI::MakeFrameGraphPassResourceAccess(textureHandle,
                                              RHI::ResourceState::ShaderRead,
                                              RHI::FrameGraphAccessType::Read)));
    ASSERT_TRUE(RHI::AddFrameGraphPass(graph, consumer));
    validation = RHI::ValidateFrameGraphDesc(graph);
    EXPECT_EQ(validation.error, RHI::FrameGraphDescriptorError::DuplicatePassDependency);
    EXPECT_EQ(validation.passIndex, 1u);
    EXPECT_EQ(validation.dependencyIndex, 1u);
    EXPECT_EQ(validation.conflictDependencyIndex, 0u);
    EXPECT_EQ(validation.dependencyPassIndex, 0u);
}

TEST(RHITypesTest, FrameGraphCompileReportsTransitionCapacityExceeded) {
    RHI::FrameGraphDesc graph;
    for (uint32_t resourceIndex = 0; resourceIndex < 16; ++resourceIndex) {
        RHI::FrameGraphResourceDesc resource;
        resource.debugName = "ping_pong";
        resource.type = RHI::ResourceType::Texture;
        resource.usage = RHI::ResourceUsage::ShaderRead | RHI::ResourceUsage::CopySource;
        resource.initialState = RHI::ResourceState::Common;
        ASSERT_TRUE(RHI::AddFrameGraphResource(graph, resource));
    }

    for (uint32_t passIndex = 0; passIndex < RHI::kMaxFrameGraphPasses; ++passIndex) {
        RHI::FrameGraphPassDesc pass;
        pass.debugName = "capacity";
        const RHI::ResourceState state =
            (passIndex % 2) == 0 ? RHI::ResourceState::ShaderRead : RHI::ResourceState::CopySource;
        for (uint32_t resourceIndex = 0; resourceIndex < 16; ++resourceIndex) {
            ASSERT_TRUE(RHI::AddFrameGraphPassAccess(
                pass,
                RHI::MakeFrameGraphPassResourceAccess(RHI::MakeFrameGraphResourceHandle(resourceIndex),
                                                      state,
                                                      RHI::FrameGraphAccessType::Read)));
        }
        ASSERT_TRUE(RHI::AddFrameGraphPass(graph, pass));
    }

    const RHI::FrameGraphCompileResult result = RHI::CompileFrameGraphTransitions(graph);
    EXPECT_FALSE(result);
    EXPECT_EQ(result.validation.error, RHI::FrameGraphDescriptorError::TransitionCapacityExceeded);
    EXPECT_EQ(result.dependencyCount, 0u);
    EXPECT_EQ(result.accessCount, 0u);
    EXPECT_EQ(result.transitionCount, 0u);
    EXPECT_EQ(result.passCount, 0u);
    EXPECT_EQ(result.resourceCount, 0u);
    EXPECT_FALSE(result.HasCompleteResourceTable());
}

TEST(RHITypesTest, ShaderResourceGroupLayoutExposesBindingRanges) {
    RHI::ShaderResourceBindingDesc emptyBinding;
    EXPECT_TRUE(emptyBinding.HasShaderStages());
    EXPECT_FALSE(emptyBinding.HasBindingRange());
    EXPECT_EQ(emptyBinding.LastBindingIndex(), RHI::kInvalidShaderResourceBindingIndex);
    EXPECT_FALSE(emptyBinding.ContainsBindingIndex(0));

    RHI::ShaderResourceGroupLayoutDesc layout = MakeValidShaderResourceGroupLayout();
    layout.bindingCount = 4;
    layout.bindings[3].type = RHI::ShaderResourceBindingType::Texture;
    layout.bindings[3].shaderStages = RHI::ShaderStageFlag(RHI::ShaderStage::Vertex);
    layout.bindings[3].bindingIndex = 0;
    layout.bindings[3].bindingCount = 1;
    layout.bindings[3].debugName = "vertex_texture_zero";

    EXPECT_TRUE(layout.HasBindings());
    EXPECT_TRUE(layout.HasCompleteBindingTable());
    ASSERT_NE(layout.GetBinding(1), nullptr);
    EXPECT_EQ(layout.GetBinding(1)->LastBindingIndex(), 4u);
    EXPECT_TRUE(layout.GetBinding(1)->ContainsBindingIndex(3));
    EXPECT_FALSE(layout.GetBinding(1)->ContainsBindingIndex(5));
    EXPECT_EQ(layout.GetBinding(layout.bindingCount), nullptr);
    EXPECT_TRUE(RHI::ValidateShaderResourceGroupLayoutDesc(layout));
}

TEST(RHITypesTest, ShaderResourceGroupLayoutValidationReportsDescriptorErrors) {
    RHI::ShaderResourceGroupLayoutDesc layout;

    RHI::ShaderResourceGroupLayoutValidation validation =
        RHI::ValidateShaderResourceGroupLayoutDesc(layout);
    EXPECT_EQ(validation.error, RHI::ShaderResourceGroupLayoutError::MissingBinding);

    layout = MakeValidShaderResourceGroupLayout();
    layout.bindingCount = static_cast<uint32_t>(RHI::kMaxShaderResourceGroupBindings + 1);
    validation = RHI::ValidateShaderResourceGroupLayoutDesc(layout);
    EXPECT_EQ(validation.error, RHI::ShaderResourceGroupLayoutError::TooManyBindings);

    layout = MakeValidShaderResourceGroupLayout();
    layout.bindings[1].type = static_cast<RHI::ShaderResourceBindingType>(255);
    validation = RHI::ValidateShaderResourceGroupLayoutDesc(layout);
    EXPECT_EQ(validation.error, RHI::ShaderResourceGroupLayoutError::InvalidBindingType);
    EXPECT_EQ(validation.bindingIndex, 1u);

    layout = MakeValidShaderResourceGroupLayout();
    layout.bindings[1].shaderStages = 0;
    validation = RHI::ValidateShaderResourceGroupLayoutDesc(layout);
    EXPECT_EQ(validation.error, RHI::ShaderResourceGroupLayoutError::MissingShaderStage);
    EXPECT_EQ(validation.bindingIndex, 1u);

    layout = MakeValidShaderResourceGroupLayout();
    layout.bindings[1].shaderStages = 0x80;
    validation = RHI::ValidateShaderResourceGroupLayoutDesc(layout);
    EXPECT_EQ(validation.error, RHI::ShaderResourceGroupLayoutError::InvalidShaderStage);
    EXPECT_EQ(validation.shaderStages, 0x80);

    layout = MakeValidShaderResourceGroupLayout();
    layout.bindings[1].bindingIndex = RHI::kInvalidShaderResourceBindingIndex;
    validation = RHI::ValidateShaderResourceGroupLayoutDesc(layout);
    EXPECT_EQ(validation.error, RHI::ShaderResourceGroupLayoutError::InvalidBindingIndex);

    layout = MakeValidShaderResourceGroupLayout();
    layout.bindings[1].bindingIndex = RHI::kMaxShaderResourceBindingIndex + 1;
    validation = RHI::ValidateShaderResourceGroupLayoutDesc(layout);
    EXPECT_EQ(validation.error, RHI::ShaderResourceGroupLayoutError::InvalidBindingIndex);

    layout = MakeValidShaderResourceGroupLayout();
    layout.bindings[1].bindingCount = 0;
    validation = RHI::ValidateShaderResourceGroupLayoutDesc(layout);
    EXPECT_EQ(validation.error, RHI::ShaderResourceGroupLayoutError::EmptyBindingRange);

    layout = MakeValidShaderResourceGroupLayout();
    layout.bindings[1].bindingIndex = RHI::kMaxShaderResourceBindingIndex;
    layout.bindings[1].bindingCount = 2;
    validation = RHI::ValidateShaderResourceGroupLayoutDesc(layout);
    EXPECT_EQ(validation.error, RHI::ShaderResourceGroupLayoutError::BindingRangeOverflow);

    layout = MakeValidShaderResourceGroupLayout();
    layout.bindingCount = 4;
    layout.bindings[3].type = RHI::ShaderResourceBindingType::Texture;
    layout.bindings[3].shaderStages = RHI::ShaderStageFlag(RHI::ShaderStage::Fragment);
    layout.bindings[3].bindingIndex = 4;
    layout.bindings[3].bindingCount = 1;
    validation = RHI::ValidateShaderResourceGroupLayoutDesc(layout);
    EXPECT_EQ(validation.error, RHI::ShaderResourceGroupLayoutError::OverlappingBindingRange);
    EXPECT_EQ(validation.bindingIndex, 3u);
    EXPECT_EQ(validation.conflictBindingIndex, 1u);
    EXPECT_EQ(validation.bindingType, RHI::ShaderResourceBindingType::Texture);
    EXPECT_EQ(validation.registerIndex, 4u);
    EXPECT_EQ(validation.bindingCount, 1u);
}

} // namespace testing
} // namespace Next
