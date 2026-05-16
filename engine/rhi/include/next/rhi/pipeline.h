#pragma once

#include "next/rhi/types.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace Next {
namespace RHI {

static constexpr size_t kMaxGraphicsPipelineColorAttachments = 4;
static constexpr size_t kMaxGraphicsPipelineVertexBuffers = 4;
static constexpr size_t kMaxGraphicsPipelineVertexAttributes = 8;

struct PipelineShaderDesc {
    ShaderStage stage = ShaderStage::Vertex;
    const char* entryPoint = nullptr;
    const char* debugName = nullptr;
};

struct RasterStateDesc {
    PrimitiveTopology primitiveTopology = PrimitiveTopology::TriangleList;
    FillMode fillMode = FillMode::Solid;
    CullMode cullMode = CullMode::None;
    FrontFaceWinding frontFace = FrontFaceWinding::CounterClockwise;
    int32_t depthBias = 0;
    float depthBiasClamp = 0.0f;
    float depthBiasSlopeScale = 0.0f;
    bool depthClipEnabled = true;
};

struct VertexBufferLayoutDesc {
    uint32_t stride = 0;
    VertexStepFunction stepFunction = VertexStepFunction::PerVertex;
    uint32_t stepRate = 1;
};

struct VertexAttributeDesc {
    uint32_t location = 0;
    uint32_t bufferIndex = 0;
    VertexFormat format = VertexFormat::Unknown;
    uint32_t offset = 0;
};

struct VertexInputStateDesc {
    std::array<VertexBufferLayoutDesc, kMaxGraphicsPipelineVertexBuffers> buffers{};
    uint32_t bufferCount = 0;
    std::array<VertexAttributeDesc, kMaxGraphicsPipelineVertexAttributes> attributes{};
    uint32_t attributeCount = 0;
};

struct MultisampleStateDesc {
    uint32_t sampleCount = 1;
    bool alphaToCoverageEnabled = false;
};

struct StencilFaceStateDesc {
    CompareFunction compare = CompareFunction::Always;
    StencilOperation stencilFailOp = StencilOperation::Keep;
    StencilOperation depthFailOp = StencilOperation::Keep;
    StencilOperation passOp = StencilOperation::Keep;
};

struct DepthStencilStateDesc {
    bool depthTestEnabled = true;
    bool depthWriteEnabled = true;
    CompareFunction depthCompare = CompareFunction::Less;
    bool stencilTestEnabled = false;
    uint8_t stencilReadMask = 0xff;
    uint8_t stencilWriteMask = 0xff;
    StencilFaceStateDesc frontStencil;
    StencilFaceStateDesc backStencil;
};

struct ColorAttachmentDesc {
    Format format = Format::Unknown;
    bool blendEnabled = false;
    BlendFactor sourceColorBlendFactor = BlendFactor::One;
    BlendFactor destinationColorBlendFactor = BlendFactor::Zero;
    BlendOperation colorBlendOperation = BlendOperation::Add;
    BlendFactor sourceAlphaBlendFactor = BlendFactor::One;
    BlendFactor destinationAlphaBlendFactor = BlendFactor::Zero;
    BlendOperation alphaBlendOperation = BlendOperation::Add;
    ColorWriteMaskFlags writeMask = ColorWriteMaskFlag(ColorWriteMask::All);
};

struct GraphicsPipelineDesc {
    const char* debugName = nullptr;
    PipelineShaderDesc vertexShader{ShaderStage::Vertex, nullptr, nullptr};
    PipelineShaderDesc fragmentShader{ShaderStage::Fragment, nullptr, nullptr};
    VertexInputStateDesc vertexInput;
    RasterStateDesc rasterState;
    MultisampleStateDesc multisampleState;
    DepthStencilStateDesc depthStencilState;
    std::array<ColorAttachmentDesc, kMaxGraphicsPipelineColorAttachments> colorAttachments{};
    uint32_t colorAttachmentCount = 1;
    Format depthStencilFormat = Format::Unknown;
};

enum class PipelineDescriptorError : uint8_t {
    None = 0,
    InvalidVertexShaderStage,
    InvalidFragmentShaderStage,
    MissingVertexShader,
    MissingFragmentShader,
    MissingColorAttachment,
    TooManyColorAttachments,
    UnsupportedColorFormat,
    UnsupportedDepthFormat,
    UnsupportedStencilFormat,
    InvalidPrimitiveTopology,
    InvalidCullMode,
    InvalidFrontFaceWinding,
    InvalidDepthCompareFunction,
    InvalidStencilCompareFunction,
    InvalidStencilOperation,
    InvalidBlendFactor,
    InvalidBlendOperation,
    InvalidColorWriteMask,
    InvalidSampleCount,
    TooManyVertexBuffers,
    TooManyVertexAttributes,
    InvalidVertexBufferReference,
    InvalidVertexAttributeLocation,
    InvalidVertexFormat,
    InvalidVertexStepFunction,
    InvalidVertexStepRate,
    InvalidVertexStride,
    InvalidFillMode,
    InvalidDepthBias,
};

struct PipelineDescriptorValidation {
    PipelineDescriptorError error = PipelineDescriptorError::None;
    ShaderStage shaderStage = ShaderStage::Vertex;
    uint32_t colorAttachmentIndex = 0;
    uint32_t vertexBufferIndex = 0;
    uint32_t vertexAttributeIndex = 0;
    Format format = Format::Unknown;

    explicit operator bool() const { return error == PipelineDescriptorError::None; }
};

GraphicsPipelineDesc MakeGraphicsPipelineDesc(const char* vertexEntryPoint,
                                              const char* fragmentEntryPoint,
                                              Format colorFormat,
                                              Format depthStencilFormat,
                                              const char* debugName = nullptr);
const char* PipelineDescriptorErrorName(PipelineDescriptorError error);
PipelineDescriptorValidation ValidateGraphicsPipelineDesc(const GraphicsPipelineDesc& desc);
bool GraphicsPipelineDescEquals(const GraphicsPipelineDesc& lhs, const GraphicsPipelineDesc& rhs);
size_t GraphicsPipelineDescHash(const GraphicsPipelineDesc& desc);

} // namespace RHI
} // namespace Next
