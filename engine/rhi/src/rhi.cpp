#include "next/rhi/rhi.h"

#include <cmath>
#include <cstring>
#include <limits>

namespace Next {
namespace RHI {
namespace {

bool BufferDescSupportsInitialState(ResourceState state) {
    switch (state) {
        case ResourceState::Undefined:
        case ResourceState::Common:
        case ResourceState::CopySource:
        case ResourceState::CopyDestination:
        case ResourceState::VertexBuffer:
        case ResourceState::IndexBuffer:
        case ResourceState::ConstantBuffer:
        case ResourceState::ShaderRead:
        case ResourceState::ShaderWrite:
            return true;
        case ResourceState::RenderTarget:
        case ResourceState::DepthWrite:
        case ResourceState::Present:
        default:
            return false;
    }
}

bool TextureDescSupportsInitialState(ResourceState state) {
    switch (state) {
        case ResourceState::Undefined:
        case ResourceState::Common:
        case ResourceState::CopySource:
        case ResourceState::CopyDestination:
        case ResourceState::RenderTarget:
        case ResourceState::DepthWrite:
        case ResourceState::ShaderRead:
        case ResourceState::ShaderWrite:
        case ResourceState::Present:
            return true;
        case ResourceState::VertexBuffer:
        case ResourceState::IndexBuffer:
        case ResourceState::ConstantBuffer:
        default:
            return false;
    }
}

bool ResourceMemoryIsValid(ResourceMemory memory) {
    switch (memory) {
        case ResourceMemory::DeviceLocal:
        case ResourceMemory::Shared:
        case ResourceMemory::Upload:
        case ResourceMemory::Readback:
            return true;
        default:
            return false;
    }
}

bool SampleCountIsValid(uint32_t sampleCount) {
    switch (sampleCount) {
        case 1:
        case 2:
        case 4:
        case 8:
        case 16:
            return true;
        default:
            return false;
    }
}

void MarkDescriptorError(ResourceDescriptorValidation& validation,
                         ResourceDescriptorError error,
                         ResourceMemory memory,
                         ResourceState initialState) {
    validation.error = error;
    validation.memory = memory;
    validation.initialState = initialState;
}

bool StringEquals(const char* lhs, const char* rhs) {
    if (lhs == rhs) {
        return true;
    }
    if (!lhs || !rhs) {
        return false;
    }
    return std::strcmp(lhs, rhs) == 0;
}

void HashCombine(uint64_t& hash, uint64_t value) {
    constexpr uint64_t kFnvPrime = 1099511628211ull;
    for (size_t i = 0; i < sizeof(value); ++i) {
        hash ^= (value >> (i * 8)) & 0xffu;
        hash *= kFnvPrime;
    }
}

void HashString(uint64_t& hash, const char* value) {
    constexpr uint64_t kFnvPrime = 1099511628211ull;
    if (!value) {
        HashCombine(hash, 0);
        return;
    }

    HashCombine(hash, 1);
    for (const char* cursor = value; *cursor != '\0'; ++cursor) {
        hash ^= static_cast<uint8_t>(*cursor);
        hash *= kFnvPrime;
    }
}

void HashFloat32(uint64_t& hash, float value) {
    const float normalized = value == 0.0f ? 0.0f : value;
    uint32_t bits = 0;
    std::memcpy(&bits, &normalized, sizeof(bits));
    HashCombine(hash, bits);
}

bool HasText(const char* value) {
    return value && value[0] != '\0';
}

bool PrimitiveTopologyIsValid(PrimitiveTopology topology) {
    switch (topology) {
        case PrimitiveTopology::TriangleList:
        case PrimitiveTopology::TriangleStrip:
        case PrimitiveTopology::LineList:
        case PrimitiveTopology::PointList:
            return true;
        default:
            return false;
    }
}

bool FillModeIsValid(FillMode mode) {
    switch (mode) {
        case FillMode::Solid:
        case FillMode::Wireframe:
            return true;
        default:
            return false;
    }
}

bool CullModeIsValid(CullMode mode) {
    switch (mode) {
        case CullMode::None:
        case CullMode::Front:
        case CullMode::Back:
            return true;
        default:
            return false;
    }
}

bool FrontFaceWindingIsValid(FrontFaceWinding winding) {
    switch (winding) {
        case FrontFaceWinding::CounterClockwise:
        case FrontFaceWinding::Clockwise:
            return true;
        default:
            return false;
    }
}

bool RasterDepthBiasIsValid(const RasterStateDesc& rasterState) {
    return std::isfinite(rasterState.depthBiasClamp) &&
        std::isfinite(rasterState.depthBiasSlopeScale);
}

bool BlendConstantIsValid(const DrawIndexedDesc& draw) {
    for (float component : draw.blendConstant) {
        if (!std::isfinite(component)) {
            return false;
        }
    }
    return true;
}

bool VertexFormatIsValid(VertexFormat format) {
    switch (format) {
        case VertexFormat::Float32:
        case VertexFormat::Float32x2:
        case VertexFormat::Float32x3:
        case VertexFormat::Float32x4:
            return true;
        case VertexFormat::Unknown:
        default:
            return false;
    }
}

uint32_t VertexFormatByteSize(VertexFormat format) {
    switch (format) {
        case VertexFormat::Float32:   return 4;
        case VertexFormat::Float32x2: return 8;
        case VertexFormat::Float32x3: return 12;
        case VertexFormat::Float32x4: return 16;
        default:                      return 0;
    }
}

bool VertexStepFunctionIsValid(VertexStepFunction stepFunction) {
    switch (stepFunction) {
        case VertexStepFunction::PerVertex:
        case VertexStepFunction::PerInstance:
            return true;
        default:
            return false;
    }
}

bool IndexFormatIsValid(IndexFormat format) {
    switch (format) {
        case IndexFormat::Uint16:
        case IndexFormat::Uint32:
            return true;
        case IndexFormat::Unknown:
        default:
            return false;
    }
}

bool CompareFunctionIsValid(CompareFunction function) {
    switch (function) {
        case CompareFunction::Never:
        case CompareFunction::Less:
        case CompareFunction::Equal:
        case CompareFunction::LessEqual:
        case CompareFunction::Greater:
        case CompareFunction::NotEqual:
        case CompareFunction::GreaterEqual:
        case CompareFunction::Always:
            return true;
        default:
            return false;
    }
}

bool StencilOperationIsValid(StencilOperation operation) {
    switch (operation) {
        case StencilOperation::Keep:
        case StencilOperation::Zero:
        case StencilOperation::Replace:
        case StencilOperation::IncrementClamp:
        case StencilOperation::DecrementClamp:
        case StencilOperation::Invert:
        case StencilOperation::IncrementWrap:
        case StencilOperation::DecrementWrap:
            return true;
        default:
            return false;
    }
}

bool BlendFactorIsValid(BlendFactor factor) {
    switch (factor) {
        case BlendFactor::Zero:
        case BlendFactor::One:
        case BlendFactor::SourceColor:
        case BlendFactor::OneMinusSourceColor:
        case BlendFactor::SourceAlpha:
        case BlendFactor::OneMinusSourceAlpha:
        case BlendFactor::DestinationColor:
        case BlendFactor::OneMinusDestinationColor:
        case BlendFactor::DestinationAlpha:
        case BlendFactor::OneMinusDestinationAlpha:
        case BlendFactor::SourceAlphaSaturated:
        case BlendFactor::BlendColor:
        case BlendFactor::OneMinusBlendColor:
        case BlendFactor::BlendAlpha:
        case BlendFactor::OneMinusBlendAlpha:
            return true;
        default:
            return false;
    }
}

bool BlendOperationIsValid(BlendOperation operation) {
    switch (operation) {
        case BlendOperation::Add:
        case BlendOperation::Subtract:
        case BlendOperation::ReverseSubtract:
        case BlendOperation::Min:
        case BlendOperation::Max:
            return true;
        default:
            return false;
    }
}

bool ColorWriteMaskIsValid(ColorWriteMaskFlags mask) {
    return (mask & ~ColorWriteMaskFlag(ColorWriteMask::All)) == 0;
}

bool AttachmentLoadActionIsValid(AttachmentLoadAction action) {
    switch (action) {
        case AttachmentLoadAction::Load:
        case AttachmentLoadAction::Clear:
        case AttachmentLoadAction::DontCare:
            return true;
        default:
            return false;
    }
}

bool AttachmentStoreActionIsValid(AttachmentStoreAction action) {
    switch (action) {
        case AttachmentStoreAction::Store:
        case AttachmentStoreAction::DontCare:
            return true;
        default:
            return false;
    }
}

bool ShaderStageFlagsAreValid(ShaderStageFlags stages) {
    constexpr ShaderStageFlags kKnownShaderStages =
        ShaderStageFlag(ShaderStage::Vertex) |
        ShaderStageFlag(ShaderStage::Fragment) |
        ShaderStageFlag(ShaderStage::Compute);
    return stages != 0 && (stages & ~kKnownShaderStages) == 0;
}

bool ShaderResourceBindingTypeIsValid(ShaderResourceBindingType type) {
    switch (type) {
        case ShaderResourceBindingType::ConstantBuffer:
        case ShaderResourceBindingType::ReadOnlyBuffer:
        case ShaderResourceBindingType::ReadWriteBuffer:
        case ShaderResourceBindingType::Texture:
        case ShaderResourceBindingType::StorageTexture:
        case ShaderResourceBindingType::Sampler:
            return true;
        default:
            return false;
    }
}

PipelineDescriptorValidation PipelineDescriptorErrorResult(PipelineDescriptorError error) {
    PipelineDescriptorValidation validation;
    validation.error = error;
    return validation;
}

PipelineDescriptorValidation PipelineDescriptorShaderError(PipelineDescriptorError error, ShaderStage stage) {
    PipelineDescriptorValidation validation;
    validation.error = error;
    validation.shaderStage = stage;
    return validation;
}

PipelineDescriptorValidation PipelineDescriptorFormatError(PipelineDescriptorError error,
                                                           uint32_t colorAttachmentIndex,
                                                           Format format) {
    PipelineDescriptorValidation validation;
    validation.error = error;
    validation.colorAttachmentIndex = colorAttachmentIndex;
    validation.format = format;
    return validation;
}

PipelineDescriptorValidation PipelineDescriptorVertexBufferError(PipelineDescriptorError error,
                                                                 uint32_t bufferIndex) {
    PipelineDescriptorValidation validation;
    validation.error = error;
    validation.vertexBufferIndex = bufferIndex;
    return validation;
}

PipelineDescriptorValidation PipelineDescriptorVertexAttributeError(PipelineDescriptorError error,
                                                                    uint32_t attributeIndex,
                                                                    uint32_t bufferIndex) {
    PipelineDescriptorValidation validation;
    validation.error = error;
    validation.vertexAttributeIndex = attributeIndex;
    validation.vertexBufferIndex = bufferIndex;
    return validation;
}

SwapchainDescriptorValidation SwapchainDescriptorErrorResult(SwapchainDescriptorError error) {
    SwapchainDescriptorValidation validation;
    validation.error = error;
    return validation;
}

SwapchainDescriptorValidation SwapchainDescriptorFormatError(SwapchainDescriptorError error, Format format) {
    SwapchainDescriptorValidation validation;
    validation.error = error;
    validation.format = format;
    return validation;
}

RenderPassDescriptorValidation RenderPassDescriptorErrorResult(RenderPassDescriptorError error) {
    RenderPassDescriptorValidation validation;
    validation.error = error;
    return validation;
}

RenderPassDescriptorValidation RenderPassDescriptorAttachmentError(RenderPassDescriptorError error,
                                                                   uint32_t attachmentIndex,
                                                                   Format format = Format::Unknown) {
    RenderPassDescriptorValidation validation;
    validation.error = error;
    validation.attachmentIndex = attachmentIndex;
    validation.format = format;
    return validation;
}

ShaderResourceGroupLayoutValidation ShaderResourceGroupLayoutErrorResult(
    ShaderResourceGroupLayoutError error,
    uint32_t bindingIndex,
    const ShaderResourceBindingDesc& binding) {
    ShaderResourceGroupLayoutValidation validation;
    validation.error = error;
    validation.bindingIndex = bindingIndex;
    validation.bindingType = binding.type;
    validation.shaderStages = binding.shaderStages;
    validation.registerIndex = binding.bindingIndex;
    validation.bindingCount = binding.bindingCount;
    return validation;
}

ShaderResourceGroupLayoutValidation ShaderResourceGroupLayoutOverlapError(
    uint32_t bindingIndex,
    uint32_t conflictBindingIndex,
    const ShaderResourceBindingDesc& binding) {
    ShaderResourceGroupLayoutValidation validation =
        ShaderResourceGroupLayoutErrorResult(ShaderResourceGroupLayoutError::OverlappingBindingRange,
                                             bindingIndex,
                                             binding);
    validation.conflictBindingIndex = conflictBindingIndex;
    return validation;
}

bool ShaderResourceBindingRangesOverlap(const ShaderResourceBindingDesc& lhs,
                                        const ShaderResourceBindingDesc& rhs) {
    return lhs.type == rhs.type &&
        (lhs.shaderStages & rhs.shaderStages) != 0 &&
        lhs.bindingIndex <= rhs.LastBindingIndex() &&
        rhs.bindingIndex <= lhs.LastBindingIndex();
}

bool DescriptorFloatEquals(float lhs, float rhs) {
    return lhs == rhs || (std::isnan(lhs) && std::isnan(rhs));
}

void RecordPoolMemoryAllocation(ResourcePoolMemoryStats& stats, uint64_t sizeBytes) {
    if (stats.liveResourceCount < std::numeric_limits<size_t>::max()) {
        ++stats.liveResourceCount;
    }

    const uint64_t maxBytes = std::numeric_limits<uint64_t>::max();
    stats.liveBytes = sizeBytes > maxBytes - stats.liveBytes ? maxBytes : stats.liveBytes + sizeBytes;
    if (stats.liveResourceCount > stats.peakResourceCount) {
        stats.peakResourceCount = stats.liveResourceCount;
    }
    if (stats.liveBytes > stats.peakBytes) {
        stats.peakBytes = stats.liveBytes;
    }
}

void RecordPoolMemoryRelease(ResourcePoolMemoryStats& stats, uint64_t sizeBytes) {
    if (stats.liveResourceCount > 0) {
        --stats.liveResourceCount;
    }
    stats.liveBytes = stats.liveBytes > sizeBytes ? stats.liveBytes - sizeBytes : 0;
}

void RecordPoolMemoryAllocationFailure(ResourcePoolMemoryStats& stats, uint64_t sizeBytes) {
    if (stats.failedAllocationCount < std::numeric_limits<uint64_t>::max()) {
        ++stats.failedAllocationCount;
    }

    const uint64_t maxBytes = std::numeric_limits<uint64_t>::max();
    stats.failedAllocationBytes =
        sizeBytes > maxBytes - stats.failedAllocationBytes ? maxBytes : stats.failedAllocationBytes + sizeBytes;
}

} // namespace

const char* BackendName(Backend backend) {
    switch (backend) {
        case Backend::Null:  return "null";
        case Backend::DX12:  return "dx12";
        case Backend::Metal: return "metal";
        default:             return "unknown";
    }
}

const char* QueueClassName(QueueClass queueClass) {
    switch (queueClass) {
        case QueueClass::Graphics: return "graphics";
        case QueueClass::Compute:  return "compute";
        case QueueClass::Copy:     return "copy";
        default:                   return "unknown";
    }
}

const char* FormatName(Format format) {
    switch (format) {
        case Format::Unknown:              return "unknown";
        case Format::BGRA8Unorm:           return "bgra8unorm";
        case Format::RGBA8Unorm:           return "rgba8unorm";
        case Format::Depth32Float:         return "depth32float";
        case Format::Depth32FloatStencil8: return "depth32float_stencil8";
        default:                           return "unknown";
    }
}

uint32_t FormatBytesPerPixel(Format format) {
    switch (format) {
        case Format::BGRA8Unorm:
        case Format::RGBA8Unorm:
        case Format::Depth32Float:
            return 4;
        case Format::Depth32FloatStencil8:
            return 8;
        case Format::Unknown:
        default:
            return 0;
    }
}

bool FormatSupportsColorAttachment(Format format) {
    switch (format) {
        case Format::BGRA8Unorm:
        case Format::RGBA8Unorm:
            return true;
        case Format::Unknown:
        case Format::Depth32Float:
        case Format::Depth32FloatStencil8:
        default:
            return false;
    }
}

bool FormatSupportsDepthStencil(Format format) {
    return FormatHasDepth(format) || FormatHasStencil(format);
}

bool FormatHasDepth(Format format) {
    switch (format) {
        case Format::Depth32Float:
        case Format::Depth32FloatStencil8:
            return true;
        default:
            return false;
    }
}

bool FormatHasStencil(Format format) {
    return format == Format::Depth32FloatStencil8;
}

const char* ResourceTypeName(ResourceType type) {
    switch (type) {
        case ResourceType::Unknown:  return "unknown";
        case ResourceType::Buffer:   return "buffer";
        case ResourceType::Texture:  return "texture";
        case ResourceType::Sampler:  return "sampler";
        case ResourceType::Pipeline: return "pipeline";
        default:                     return "unknown";
    }
}

const char* ResourceMemoryName(ResourceMemory memory) {
    switch (memory) {
        case ResourceMemory::DeviceLocal: return "device_local";
        case ResourceMemory::Shared:      return "shared";
        case ResourceMemory::Upload:      return "upload";
        case ResourceMemory::Readback:    return "readback";
        default:                          return "unknown";
    }
}

const char* SamplerFilterName(SamplerFilter filter) {
    switch (filter) {
        case SamplerFilter::Nearest: return "nearest";
        case SamplerFilter::Linear:  return "linear";
        default:                     return "unknown";
    }
}

const char* SamplerMipFilterName(SamplerMipFilter filter) {
    switch (filter) {
        case SamplerMipFilter::NotMipmapped: return "not_mipmapped";
        case SamplerMipFilter::Nearest:      return "nearest";
        case SamplerMipFilter::Linear:       return "linear";
        default:                             return "unknown";
    }
}

const char* SamplerAddressModeName(SamplerAddressMode addressMode) {
    switch (addressMode) {
        case SamplerAddressMode::Repeat:        return "repeat";
        case SamplerAddressMode::ClampToEdge:   return "clamp_to_edge";
        case SamplerAddressMode::MirrorRepeat:  return "mirror_repeat";
        case SamplerAddressMode::ClampToBorder: return "clamp_to_border";
        default:                                return "unknown";
    }
}

const char* SamplerBorderColorName(SamplerBorderColor color) {
    switch (color) {
        case SamplerBorderColor::OpaqueBlack:      return "opaque_black";
        case SamplerBorderColor::TransparentBlack: return "transparent_black";
        case SamplerBorderColor::OpaqueWhite:      return "opaque_white";
        default:                                   return "unknown";
    }
}

const char* CompareFunctionName(CompareFunction function) {
    switch (function) {
        case CompareFunction::Never:        return "never";
        case CompareFunction::Less:         return "less";
        case CompareFunction::Equal:        return "equal";
        case CompareFunction::LessEqual:    return "less_equal";
        case CompareFunction::Greater:      return "greater";
        case CompareFunction::NotEqual:     return "not_equal";
        case CompareFunction::GreaterEqual: return "greater_equal";
        case CompareFunction::Always:       return "always";
        default:                            return "unknown";
    }
}

const char* StencilOperationName(StencilOperation operation) {
    switch (operation) {
        case StencilOperation::Keep:           return "keep";
        case StencilOperation::Zero:           return "zero";
        case StencilOperation::Replace:        return "replace";
        case StencilOperation::IncrementClamp: return "increment_clamp";
        case StencilOperation::DecrementClamp: return "decrement_clamp";
        case StencilOperation::Invert:         return "invert";
        case StencilOperation::IncrementWrap:  return "increment_wrap";
        case StencilOperation::DecrementWrap:  return "decrement_wrap";
        default:                               return "unknown";
    }
}

const char* BlendFactorName(BlendFactor factor) {
    switch (factor) {
        case BlendFactor::Zero:                   return "zero";
        case BlendFactor::One:                    return "one";
        case BlendFactor::SourceColor:            return "source_color";
        case BlendFactor::OneMinusSourceColor:    return "one_minus_source_color";
        case BlendFactor::SourceAlpha:            return "source_alpha";
        case BlendFactor::OneMinusSourceAlpha:    return "one_minus_source_alpha";
        case BlendFactor::DestinationColor:       return "destination_color";
        case BlendFactor::OneMinusDestinationColor:
            return "one_minus_destination_color";
        case BlendFactor::DestinationAlpha:       return "destination_alpha";
        case BlendFactor::OneMinusDestinationAlpha:
            return "one_minus_destination_alpha";
        case BlendFactor::SourceAlphaSaturated:   return "source_alpha_saturated";
        case BlendFactor::BlendColor:             return "blend_color";
        case BlendFactor::OneMinusBlendColor:     return "one_minus_blend_color";
        case BlendFactor::BlendAlpha:             return "blend_alpha";
        case BlendFactor::OneMinusBlendAlpha:     return "one_minus_blend_alpha";
        default:                                  return "unknown";
    }
}

const char* BlendOperationName(BlendOperation operation) {
    switch (operation) {
        case BlendOperation::Add:             return "add";
        case BlendOperation::Subtract:        return "subtract";
        case BlendOperation::ReverseSubtract: return "reverse_subtract";
        case BlendOperation::Min:             return "min";
        case BlendOperation::Max:             return "max";
        default:                              return "unknown";
    }
}

const char* ColorWriteMaskName(ColorWriteMask mask) {
    switch (mask) {
        case ColorWriteMask::None:  return "none";
        case ColorWriteMask::Red:   return "red";
        case ColorWriteMask::Green: return "green";
        case ColorWriteMask::Blue:  return "blue";
        case ColorWriteMask::Alpha: return "alpha";
        case ColorWriteMask::All:   return "all";
        default:                    return "unknown";
    }
}

const char* VertexFormatName(VertexFormat format) {
    switch (format) {
        case VertexFormat::Unknown:   return "unknown";
        case VertexFormat::Float32:   return "float32";
        case VertexFormat::Float32x2: return "float32x2";
        case VertexFormat::Float32x3: return "float32x3";
        case VertexFormat::Float32x4: return "float32x4";
        default:                      return "unknown";
    }
}

const char* VertexStepFunctionName(VertexStepFunction stepFunction) {
    switch (stepFunction) {
        case VertexStepFunction::PerVertex:   return "per_vertex";
        case VertexStepFunction::PerInstance: return "per_instance";
        default:                              return "unknown";
    }
}

const char* IndexFormatName(IndexFormat format) {
    switch (format) {
        case IndexFormat::Unknown: return "unknown";
        case IndexFormat::Uint16:  return "uint16";
        case IndexFormat::Uint32:  return "uint32";
        default:                   return "unknown";
    }
}

uint32_t IndexFormatByteSize(IndexFormat format) {
    switch (format) {
        case IndexFormat::Uint16: return 2;
        case IndexFormat::Uint32: return 4;
        case IndexFormat::Unknown:
        default:                  return 0;
    }
}

const char* ShaderStageName(ShaderStage stage) {
    switch (stage) {
        case ShaderStage::Vertex:   return "vertex";
        case ShaderStage::Fragment: return "fragment";
        case ShaderStage::Compute:  return "compute";
        default:                    return "unknown";
    }
}

const char* PrimitiveTopologyName(PrimitiveTopology topology) {
    switch (topology) {
        case PrimitiveTopology::TriangleList:  return "triangle_list";
        case PrimitiveTopology::TriangleStrip: return "triangle_strip";
        case PrimitiveTopology::LineList:      return "line_list";
        case PrimitiveTopology::PointList:     return "point_list";
        default:                               return "unknown";
    }
}

const char* FillModeName(FillMode mode) {
    switch (mode) {
        case FillMode::Solid:     return "solid";
        case FillMode::Wireframe: return "wireframe";
        default:                  return "unknown";
    }
}

const char* CullModeName(CullMode mode) {
    switch (mode) {
        case CullMode::None:  return "none";
        case CullMode::Front: return "front";
        case CullMode::Back:  return "back";
        default:              return "unknown";
    }
}

const char* FrontFaceWindingName(FrontFaceWinding winding) {
    switch (winding) {
        case FrontFaceWinding::CounterClockwise: return "counter_clockwise";
        case FrontFaceWinding::Clockwise:        return "clockwise";
        default:                                 return "unknown";
    }
}

GraphicsPipelineDesc MakeGraphicsPipelineDesc(const char* vertexEntryPoint,
                                              const char* fragmentEntryPoint,
                                              Format colorFormat,
                                              Format depthStencilFormat,
                                              const char* debugName) {
    GraphicsPipelineDesc desc;
    desc.debugName = debugName;
    desc.vertexShader.entryPoint = vertexEntryPoint;
    desc.fragmentShader.entryPoint = fragmentEntryPoint;
    desc.colorAttachments[0].format = colorFormat;
    desc.depthStencilFormat = depthStencilFormat;
    return desc;
}

const char* PipelineDescriptorErrorName(PipelineDescriptorError error) {
    switch (error) {
        case PipelineDescriptorError::None:
            return "none";
        case PipelineDescriptorError::InvalidVertexShaderStage:
            return "invalid_vertex_shader_stage";
        case PipelineDescriptorError::InvalidFragmentShaderStage:
            return "invalid_fragment_shader_stage";
        case PipelineDescriptorError::MissingVertexShader:
            return "missing_vertex_shader";
        case PipelineDescriptorError::MissingFragmentShader:
            return "missing_fragment_shader";
        case PipelineDescriptorError::MissingColorAttachment:
            return "missing_color_attachment";
        case PipelineDescriptorError::TooManyColorAttachments:
            return "too_many_color_attachments";
        case PipelineDescriptorError::UnsupportedColorFormat:
            return "unsupported_color_format";
        case PipelineDescriptorError::UnsupportedDepthFormat:
            return "unsupported_depth_format";
        case PipelineDescriptorError::UnsupportedStencilFormat:
            return "unsupported_stencil_format";
        case PipelineDescriptorError::InvalidPrimitiveTopology:
            return "invalid_primitive_topology";
        case PipelineDescriptorError::InvalidCullMode:
            return "invalid_cull_mode";
        case PipelineDescriptorError::InvalidFrontFaceWinding:
            return "invalid_front_face_winding";
        case PipelineDescriptorError::InvalidDepthCompareFunction:
            return "invalid_depth_compare_function";
        case PipelineDescriptorError::InvalidStencilCompareFunction:
            return "invalid_stencil_compare_function";
        case PipelineDescriptorError::InvalidStencilOperation:
            return "invalid_stencil_operation";
        case PipelineDescriptorError::InvalidBlendFactor:
            return "invalid_blend_factor";
        case PipelineDescriptorError::InvalidBlendOperation:
            return "invalid_blend_operation";
        case PipelineDescriptorError::InvalidColorWriteMask:
            return "invalid_color_write_mask";
        case PipelineDescriptorError::InvalidSampleCount:
            return "invalid_sample_count";
        case PipelineDescriptorError::TooManyVertexBuffers:
            return "too_many_vertex_buffers";
        case PipelineDescriptorError::TooManyVertexAttributes:
            return "too_many_vertex_attributes";
        case PipelineDescriptorError::InvalidVertexBufferReference:
            return "invalid_vertex_buffer_reference";
        case PipelineDescriptorError::InvalidVertexAttributeLocation:
            return "invalid_vertex_attribute_location";
        case PipelineDescriptorError::InvalidVertexFormat:
            return "invalid_vertex_format";
        case PipelineDescriptorError::InvalidVertexStepFunction:
            return "invalid_vertex_step_function";
        case PipelineDescriptorError::InvalidVertexStepRate:
            return "invalid_vertex_step_rate";
        case PipelineDescriptorError::InvalidVertexStride:
            return "invalid_vertex_stride";
        case PipelineDescriptorError::InvalidFillMode:
            return "invalid_fill_mode";
        case PipelineDescriptorError::InvalidDepthBias:
            return "invalid_depth_bias";
        default:
            return "unknown";
    }
}

const char* AttachmentLoadActionName(AttachmentLoadAction action) {
    switch (action) {
        case AttachmentLoadAction::Load:
            return "load";
        case AttachmentLoadAction::Clear:
            return "clear";
        case AttachmentLoadAction::DontCare:
            return "dont_care";
        default:
            return "unknown";
    }
}

const char* AttachmentStoreActionName(AttachmentStoreAction action) {
    switch (action) {
        case AttachmentStoreAction::Store:
            return "store";
        case AttachmentStoreAction::DontCare:
            return "dont_care";
        default:
            return "unknown";
    }
}

const char* RenderPassDescriptorErrorName(RenderPassDescriptorError error) {
    switch (error) {
        case RenderPassDescriptorError::None:
            return "none";
        case RenderPassDescriptorError::MissingAttachment:
            return "missing_attachment";
        case RenderPassDescriptorError::TooManyColorAttachments:
            return "too_many_color_attachments";
        case RenderPassDescriptorError::UnsupportedColorFormat:
            return "unsupported_color_format";
        case RenderPassDescriptorError::UnsupportedDepthFormat:
            return "unsupported_depth_format";
        case RenderPassDescriptorError::InvalidLoadAction:
            return "invalid_load_action";
        case RenderPassDescriptorError::InvalidStoreAction:
            return "invalid_store_action";
        case RenderPassDescriptorError::InvalidDepthClearValue:
            return "invalid_depth_clear_value";
        case RenderPassDescriptorError::InvalidStencilClearValue:
            return "invalid_stencil_clear_value";
        default:
            return "unknown";
    }
}

const char* ShaderResourceBindingTypeName(ShaderResourceBindingType type) {
    switch (type) {
        case ShaderResourceBindingType::ConstantBuffer:
            return "constant_buffer";
        case ShaderResourceBindingType::ReadOnlyBuffer:
            return "read_only_buffer";
        case ShaderResourceBindingType::ReadWriteBuffer:
            return "read_write_buffer";
        case ShaderResourceBindingType::Texture:
            return "texture";
        case ShaderResourceBindingType::StorageTexture:
            return "storage_texture";
        case ShaderResourceBindingType::Sampler:
            return "sampler";
        default:
            return "unknown";
    }
}

const char* ShaderResourceGroupLayoutErrorName(ShaderResourceGroupLayoutError error) {
    switch (error) {
        case ShaderResourceGroupLayoutError::None:
            return "none";
        case ShaderResourceGroupLayoutError::MissingBinding:
            return "missing_binding";
        case ShaderResourceGroupLayoutError::TooManyBindings:
            return "too_many_bindings";
        case ShaderResourceGroupLayoutError::InvalidBindingType:
            return "invalid_binding_type";
        case ShaderResourceGroupLayoutError::MissingShaderStage:
            return "missing_shader_stage";
        case ShaderResourceGroupLayoutError::InvalidShaderStage:
            return "invalid_shader_stage";
        case ShaderResourceGroupLayoutError::InvalidBindingIndex:
            return "invalid_binding_index";
        case ShaderResourceGroupLayoutError::EmptyBindingRange:
            return "empty_binding_range";
        case ShaderResourceGroupLayoutError::BindingRangeOverflow:
            return "binding_range_overflow";
        case ShaderResourceGroupLayoutError::OverlappingBindingRange:
            return "overlapping_binding_range";
        default:
            return "unknown";
    }
}

const char* SwapchainDescriptorErrorName(SwapchainDescriptorError error) {
    switch (error) {
        case SwapchainDescriptorError::None:
            return "none";
        case SwapchainDescriptorError::EmptyDrawableSize:
            return "empty_drawable_size";
        case SwapchainDescriptorError::UnsupportedColorFormat:
            return "unsupported_color_format";
        case SwapchainDescriptorError::UnsupportedDepthFormat:
            return "unsupported_depth_format";
        default:
            return "unknown";
    }
}

PipelineDescriptorValidation ValidateGraphicsPipelineDesc(const GraphicsPipelineDesc& desc) {
    if (desc.vertexShader.stage != ShaderStage::Vertex) {
        return PipelineDescriptorShaderError(PipelineDescriptorError::InvalidVertexShaderStage,
                                             desc.vertexShader.stage);
    }
    if (desc.fragmentShader.stage != ShaderStage::Fragment) {
        return PipelineDescriptorShaderError(PipelineDescriptorError::InvalidFragmentShaderStage,
                                             desc.fragmentShader.stage);
    }
    if (!HasText(desc.vertexShader.entryPoint)) {
        return PipelineDescriptorShaderError(PipelineDescriptorError::MissingVertexShader,
                                             ShaderStage::Vertex);
    }
    if (!HasText(desc.fragmentShader.entryPoint)) {
        return PipelineDescriptorShaderError(PipelineDescriptorError::MissingFragmentShader,
                                             ShaderStage::Fragment);
    }
    if (desc.vertexInput.bufferCount > kMaxGraphicsPipelineVertexBuffers) {
        return PipelineDescriptorErrorResult(PipelineDescriptorError::TooManyVertexBuffers);
    }
    if (desc.vertexInput.attributeCount > kMaxGraphicsPipelineVertexAttributes) {
        return PipelineDescriptorErrorResult(PipelineDescriptorError::TooManyVertexAttributes);
    }
    for (uint32_t i = 0; i < desc.vertexInput.bufferCount; ++i) {
        const VertexBufferLayoutDesc& buffer = desc.vertexInput.buffers[i];
        if (buffer.stride == 0) {
            return PipelineDescriptorVertexBufferError(PipelineDescriptorError::InvalidVertexStride, i);
        }
        if (!VertexStepFunctionIsValid(buffer.stepFunction)) {
            return PipelineDescriptorVertexBufferError(PipelineDescriptorError::InvalidVertexStepFunction, i);
        }
        if (buffer.stepRate == 0) {
            return PipelineDescriptorVertexBufferError(PipelineDescriptorError::InvalidVertexStepRate, i);
        }
    }
    for (uint32_t i = 0; i < desc.vertexInput.attributeCount; ++i) {
        const VertexAttributeDesc& attribute = desc.vertexInput.attributes[i];
        if (attribute.location >= kMaxGraphicsPipelineVertexAttributes) {
            return PipelineDescriptorVertexAttributeError(PipelineDescriptorError::InvalidVertexAttributeLocation,
                                                          i,
                                                          attribute.bufferIndex);
        }
        for (uint32_t previous = 0; previous < i; ++previous) {
            if (desc.vertexInput.attributes[previous].location == attribute.location) {
                return PipelineDescriptorVertexAttributeError(PipelineDescriptorError::InvalidVertexAttributeLocation,
                                                              i,
                                                              attribute.bufferIndex);
            }
        }
        if (attribute.bufferIndex >= desc.vertexInput.bufferCount) {
            return PipelineDescriptorVertexAttributeError(PipelineDescriptorError::InvalidVertexBufferReference,
                                                          i,
                                                          attribute.bufferIndex);
        }
        if (!VertexFormatIsValid(attribute.format)) {
            return PipelineDescriptorVertexAttributeError(PipelineDescriptorError::InvalidVertexFormat,
                                                          i,
                                                          attribute.bufferIndex);
        }
        const uint32_t formatByteSize = VertexFormatByteSize(attribute.format);
        const uint32_t stride = desc.vertexInput.buffers[attribute.bufferIndex].stride;
        if (formatByteSize == 0 || attribute.offset > stride || formatByteSize > stride - attribute.offset) {
            return PipelineDescriptorVertexAttributeError(PipelineDescriptorError::InvalidVertexStride,
                                                          i,
                                                          attribute.bufferIndex);
        }
    }
    if (!PrimitiveTopologyIsValid(desc.rasterState.primitiveTopology)) {
        return PipelineDescriptorErrorResult(PipelineDescriptorError::InvalidPrimitiveTopology);
    }
    if (!FillModeIsValid(desc.rasterState.fillMode)) {
        return PipelineDescriptorErrorResult(PipelineDescriptorError::InvalidFillMode);
    }
    if (!CullModeIsValid(desc.rasterState.cullMode)) {
        return PipelineDescriptorErrorResult(PipelineDescriptorError::InvalidCullMode);
    }
    if (!FrontFaceWindingIsValid(desc.rasterState.frontFace)) {
        return PipelineDescriptorErrorResult(PipelineDescriptorError::InvalidFrontFaceWinding);
    }
    if (!RasterDepthBiasIsValid(desc.rasterState)) {
        return PipelineDescriptorErrorResult(PipelineDescriptorError::InvalidDepthBias);
    }
    if (!SampleCountIsValid(desc.multisampleState.sampleCount)) {
        return PipelineDescriptorErrorResult(PipelineDescriptorError::InvalidSampleCount);
    }
    if (desc.colorAttachmentCount == 0) {
        return PipelineDescriptorErrorResult(PipelineDescriptorError::MissingColorAttachment);
    }
    if (desc.colorAttachmentCount > kMaxGraphicsPipelineColorAttachments) {
        return PipelineDescriptorErrorResult(PipelineDescriptorError::TooManyColorAttachments);
    }
    for (uint32_t i = 0; i < desc.colorAttachmentCount; ++i) {
        const ColorAttachmentDesc& attachment = desc.colorAttachments[i];
        if (!FormatSupportsColorAttachment(attachment.format)) {
            return PipelineDescriptorFormatError(PipelineDescriptorError::UnsupportedColorFormat,
                                                  i,
                                                  attachment.format);
        }
        if (!ColorWriteMaskIsValid(attachment.writeMask)) {
            return PipelineDescriptorFormatError(PipelineDescriptorError::InvalidColorWriteMask,
                                                 i,
                                                 attachment.format);
        }
        if (attachment.blendEnabled) {
            if (!BlendFactorIsValid(attachment.sourceColorBlendFactor) ||
                !BlendFactorIsValid(attachment.destinationColorBlendFactor) ||
                !BlendFactorIsValid(attachment.sourceAlphaBlendFactor) ||
                !BlendFactorIsValid(attachment.destinationAlphaBlendFactor)) {
                return PipelineDescriptorFormatError(PipelineDescriptorError::InvalidBlendFactor,
                                                     i,
                                                     attachment.format);
            }
            if (!BlendOperationIsValid(attachment.colorBlendOperation) ||
                !BlendOperationIsValid(attachment.alphaBlendOperation)) {
                return PipelineDescriptorFormatError(PipelineDescriptorError::InvalidBlendOperation,
                                                     i,
                                                     attachment.format);
            }
        }
    }

    const bool usesDepthStencil = desc.depthStencilFormat != Format::Unknown ||
        desc.depthStencilState.depthTestEnabled ||
        desc.depthStencilState.depthWriteEnabled ||
        desc.depthStencilState.stencilTestEnabled;
    if (usesDepthStencil && !FormatSupportsDepthStencil(desc.depthStencilFormat)) {
        return PipelineDescriptorFormatError(PipelineDescriptorError::UnsupportedDepthFormat,
                                             0,
                                             desc.depthStencilFormat);
    }
    if (desc.depthStencilState.stencilTestEnabled &&
        !FormatHasStencil(desc.depthStencilFormat)) {
        return PipelineDescriptorFormatError(PipelineDescriptorError::UnsupportedStencilFormat,
                                             0,
                                             desc.depthStencilFormat);
    }
    if (usesDepthStencil && !CompareFunctionIsValid(desc.depthStencilState.depthCompare)) {
        return PipelineDescriptorErrorResult(PipelineDescriptorError::InvalidDepthCompareFunction);
    }
    if (desc.depthStencilState.stencilTestEnabled) {
        if (!CompareFunctionIsValid(desc.depthStencilState.frontStencil.compare) ||
            !CompareFunctionIsValid(desc.depthStencilState.backStencil.compare)) {
            return PipelineDescriptorErrorResult(PipelineDescriptorError::InvalidStencilCompareFunction);
        }
        if (!StencilOperationIsValid(desc.depthStencilState.frontStencil.stencilFailOp) ||
            !StencilOperationIsValid(desc.depthStencilState.frontStencil.depthFailOp) ||
            !StencilOperationIsValid(desc.depthStencilState.frontStencil.passOp) ||
            !StencilOperationIsValid(desc.depthStencilState.backStencil.stencilFailOp) ||
            !StencilOperationIsValid(desc.depthStencilState.backStencil.depthFailOp) ||
            !StencilOperationIsValid(desc.depthStencilState.backStencil.passOp)) {
            return PipelineDescriptorErrorResult(PipelineDescriptorError::InvalidStencilOperation);
        }
    }

    return {};
}

const char* DrawDescriptorErrorName(DrawDescriptorError error) {
    switch (error) {
        case DrawDescriptorError::None:
            return "none";
        case DrawDescriptorError::MissingIndexCount:
            return "missing_index_count";
        case DrawDescriptorError::MissingInstanceCount:
            return "missing_instance_count";
        case DrawDescriptorError::InvalidIndexFormat:
            return "invalid_index_format";
        case DrawDescriptorError::MisalignedIndexBufferOffset:
            return "misaligned_index_buffer_offset";
        case DrawDescriptorError::IndexBufferOffsetOverflow:
            return "index_buffer_offset_overflow";
        case DrawDescriptorError::InvalidBlendConstant:
            return "invalid_blend_constant";
        default:
            return "unknown";
    }
}

const char* ViewportDescriptorErrorName(ViewportDescriptorError error) {
    switch (error) {
        case ViewportDescriptorError::None:
            return "none";
        case ViewportDescriptorError::EmptyViewport:
            return "empty_viewport";
        case ViewportDescriptorError::InvalidDepthRange:
            return "invalid_depth_range";
        default:
            return "unknown";
    }
}

const char* ScissorDescriptorErrorName(ScissorDescriptorError error) {
    switch (error) {
        case ScissorDescriptorError::None:
            return "none";
        case ScissorDescriptorError::EmptyScissor:
            return "empty_scissor";
        default:
            return "unknown";
    }
}

DrawIndexedValidation ValidateDrawIndexedDesc(const DrawIndexedDesc& draw,
                                              const IndexBufferViewDesc& indexBuffer) {
    DrawIndexedValidation validation;
    validation.indexFormat = indexBuffer.format;

    if (draw.indexCount == 0) {
        validation.error = DrawDescriptorError::MissingIndexCount;
        return validation;
    }
    if (draw.instanceCount == 0) {
        validation.error = DrawDescriptorError::MissingInstanceCount;
        return validation;
    }
    if (!BlendConstantIsValid(draw)) {
        validation.error = DrawDescriptorError::InvalidBlendConstant;
        return validation;
    }
    if (!IndexFormatIsValid(indexBuffer.format)) {
        validation.error = DrawDescriptorError::InvalidIndexFormat;
        return validation;
    }

    const uint32_t indexSize = IndexFormatByteSize(indexBuffer.format);
    if (indexSize == 0 || (indexBuffer.byteOffset % indexSize) != 0) {
        validation.error = DrawDescriptorError::MisalignedIndexBufferOffset;
        return validation;
    }

    uint64_t byteOffset = 0;
    if (!DrawIndexedIndexBufferOffsetBytes(draw, indexBuffer, byteOffset)) {
        validation.error = DrawDescriptorError::IndexBufferOffsetOverflow;
        return validation;
    }

    return validation;
}

bool DrawIndexedIndexBufferOffsetBytes(const DrawIndexedDesc& draw,
                                       const IndexBufferViewDesc& indexBuffer,
                                       uint64_t& outByteOffset) {
    outByteOffset = 0;
    const uint32_t indexSize = IndexFormatByteSize(indexBuffer.format);
    if (indexSize == 0) {
        return false;
    }

    const uint64_t indexOffsetBytes = static_cast<uint64_t>(draw.indexOffset) * indexSize;
    const uint64_t maxBytes = std::numeric_limits<uint64_t>::max();
    if (indexBuffer.byteOffset > maxBytes - indexOffsetBytes) {
        return false;
    }

    outByteOffset = indexBuffer.byteOffset + indexOffsetBytes;
    return true;
}

ViewportDescriptorValidation ValidateViewportDesc(const ViewportDesc& viewport) {
    ViewportDescriptorValidation validation;
    if (!(viewport.maxX > viewport.minX) || !(viewport.maxY > viewport.minY)) {
        validation.error = ViewportDescriptorError::EmptyViewport;
        return validation;
    }
    if (!(viewport.minZ >= 0.0) || !(viewport.maxZ <= 1.0) || !(viewport.maxZ >= viewport.minZ)) {
        validation.error = ViewportDescriptorError::InvalidDepthRange;
        return validation;
    }
    return validation;
}

ScissorDescriptorValidation ValidateScissorRectDesc(const ScissorRectDesc& scissor) {
    ScissorDescriptorValidation validation;
    if (scissor.maxX <= scissor.minX || scissor.maxY <= scissor.minY) {
        validation.error = ScissorDescriptorError::EmptyScissor;
        return validation;
    }
    return validation;
}

RenderPassDescriptorValidation ValidateRenderPassDesc(const RenderPassDesc& desc) {
    if (desc.colorAttachmentCount == 0 && !desc.hasDepthStencil) {
        return RenderPassDescriptorErrorResult(RenderPassDescriptorError::MissingAttachment);
    }
    if (desc.colorAttachmentCount > kMaxRenderPassColorAttachments) {
        return RenderPassDescriptorErrorResult(RenderPassDescriptorError::TooManyColorAttachments);
    }

    for (uint32_t i = 0; i < desc.colorAttachmentCount; ++i) {
        const RenderPassColorAttachmentDesc& attachment = desc.colorAttachments[i];
        if (!FormatSupportsColorAttachment(attachment.format)) {
            return RenderPassDescriptorAttachmentError(RenderPassDescriptorError::UnsupportedColorFormat,
                                                       i,
                                                       attachment.format);
        }
        if (!AttachmentLoadActionIsValid(attachment.loadAction)) {
            return RenderPassDescriptorAttachmentError(RenderPassDescriptorError::InvalidLoadAction, i);
        }
        if (!AttachmentStoreActionIsValid(attachment.storeAction)) {
            return RenderPassDescriptorAttachmentError(RenderPassDescriptorError::InvalidStoreAction, i);
        }
    }

    if (desc.hasDepthStencil) {
        const RenderPassDepthStencilAttachmentDesc& attachment = desc.depthStencilAttachment;
        if (!FormatSupportsDepthStencil(attachment.format)) {
            return RenderPassDescriptorAttachmentError(RenderPassDescriptorError::UnsupportedDepthFormat,
                                                       desc.colorAttachmentCount,
                                                       attachment.format);
        }
        if (!AttachmentLoadActionIsValid(attachment.loadAction)) {
            return RenderPassDescriptorAttachmentError(RenderPassDescriptorError::InvalidLoadAction,
                                                       desc.colorAttachmentCount);
        }
        if (!AttachmentStoreActionIsValid(attachment.storeAction)) {
            return RenderPassDescriptorAttachmentError(RenderPassDescriptorError::InvalidStoreAction,
                                                       desc.colorAttachmentCount);
        }
        if (attachment.loadAction == AttachmentLoadAction::Clear &&
            (attachment.clearDepth < 0.0 || attachment.clearDepth > 1.0)) {
            return RenderPassDescriptorAttachmentError(RenderPassDescriptorError::InvalidDepthClearValue,
                                                       desc.colorAttachmentCount);
        }
        if (FormatHasStencil(attachment.format)) {
            if (!AttachmentLoadActionIsValid(attachment.stencilLoadAction)) {
                return RenderPassDescriptorAttachmentError(RenderPassDescriptorError::InvalidLoadAction,
                                                           desc.colorAttachmentCount);
            }
            if (!AttachmentStoreActionIsValid(attachment.stencilStoreAction)) {
                return RenderPassDescriptorAttachmentError(RenderPassDescriptorError::InvalidStoreAction,
                                                           desc.colorAttachmentCount);
            }
            if (attachment.stencilLoadAction == AttachmentLoadAction::Clear &&
                attachment.clearStencil > 255u) {
                return RenderPassDescriptorAttachmentError(RenderPassDescriptorError::InvalidStencilClearValue,
                                                           desc.colorAttachmentCount);
            }
        }
    }

    return {};
}

ShaderResourceGroupLayoutValidation ValidateShaderResourceGroupLayoutDesc(
    const ShaderResourceGroupLayoutDesc& desc) {
    if (desc.bindingCount == 0) {
        return ShaderResourceGroupLayoutErrorResult(ShaderResourceGroupLayoutError::MissingBinding,
                                                    0,
                                                    desc.bindings[0]);
    }
    if (desc.bindingCount > kMaxShaderResourceGroupBindings) {
        return ShaderResourceGroupLayoutErrorResult(ShaderResourceGroupLayoutError::TooManyBindings,
                                                    desc.bindingCount,
                                                    desc.bindings[0]);
    }

    for (uint32_t i = 0; i < desc.bindingCount; ++i) {
        const ShaderResourceBindingDesc& binding = desc.bindings[i];
        if (!ShaderResourceBindingTypeIsValid(binding.type)) {
            return ShaderResourceGroupLayoutErrorResult(ShaderResourceGroupLayoutError::InvalidBindingType,
                                                        i,
                                                        binding);
        }
        if (binding.shaderStages == 0) {
            return ShaderResourceGroupLayoutErrorResult(ShaderResourceGroupLayoutError::MissingShaderStage,
                                                        i,
                                                        binding);
        }
        if (!ShaderStageFlagsAreValid(binding.shaderStages)) {
            return ShaderResourceGroupLayoutErrorResult(ShaderResourceGroupLayoutError::InvalidShaderStage,
                                                        i,
                                                        binding);
        }
        if (binding.bindingIndex == kInvalidShaderResourceBindingIndex ||
            binding.bindingIndex > kMaxShaderResourceBindingIndex) {
            return ShaderResourceGroupLayoutErrorResult(ShaderResourceGroupLayoutError::InvalidBindingIndex,
                                                        i,
                                                        binding);
        }
        if (binding.bindingCount == 0) {
            return ShaderResourceGroupLayoutErrorResult(ShaderResourceGroupLayoutError::EmptyBindingRange,
                                                        i,
                                                        binding);
        }
        if (binding.bindingCount - 1 > kMaxShaderResourceBindingIndex - binding.bindingIndex) {
            return ShaderResourceGroupLayoutErrorResult(ShaderResourceGroupLayoutError::BindingRangeOverflow,
                                                        i,
                                                        binding);
        }

        for (uint32_t previousIndex = 0; previousIndex < i; ++previousIndex) {
            const ShaderResourceBindingDesc& previous = desc.bindings[previousIndex];
            if (ShaderResourceBindingRangesOverlap(binding, previous)) {
                return ShaderResourceGroupLayoutOverlapError(i, previousIndex, binding);
            }
        }
    }

    return {};
}

SwapchainDescriptorValidation ValidateSwapchainDesc(const SwapchainDesc& desc) {
    if (desc.drawableSize.width == 0 || desc.drawableSize.height == 0) {
        return SwapchainDescriptorErrorResult(SwapchainDescriptorError::EmptyDrawableSize);
    }
    if (!FormatSupportsColorAttachment(desc.colorFormat)) {
        return SwapchainDescriptorFormatError(SwapchainDescriptorError::UnsupportedColorFormat,
                                              desc.colorFormat);
    }
    if (!FormatSupportsDepthStencil(desc.depthFormat)) {
        return SwapchainDescriptorFormatError(SwapchainDescriptorError::UnsupportedDepthFormat,
                                              desc.depthFormat);
    }

    return {};
}

bool GraphicsPipelineDescEquals(const GraphicsPipelineDesc& lhs, const GraphicsPipelineDesc& rhs) {
    if (lhs.vertexShader.stage != rhs.vertexShader.stage ||
        lhs.fragmentShader.stage != rhs.fragmentShader.stage ||
        !StringEquals(lhs.vertexShader.entryPoint, rhs.vertexShader.entryPoint) ||
        !StringEquals(lhs.fragmentShader.entryPoint, rhs.fragmentShader.entryPoint) ||
        lhs.vertexInput.bufferCount != rhs.vertexInput.bufferCount ||
        lhs.vertexInput.attributeCount != rhs.vertexInput.attributeCount ||
        lhs.rasterState.primitiveTopology != rhs.rasterState.primitiveTopology ||
        lhs.rasterState.fillMode != rhs.rasterState.fillMode ||
        lhs.rasterState.cullMode != rhs.rasterState.cullMode ||
        lhs.rasterState.frontFace != rhs.rasterState.frontFace ||
        lhs.rasterState.depthBias != rhs.rasterState.depthBias ||
        lhs.rasterState.depthBiasClamp != rhs.rasterState.depthBiasClamp ||
        lhs.rasterState.depthBiasSlopeScale != rhs.rasterState.depthBiasSlopeScale ||
        lhs.rasterState.depthClipEnabled != rhs.rasterState.depthClipEnabled ||
        lhs.multisampleState.sampleCount != rhs.multisampleState.sampleCount ||
        lhs.multisampleState.alphaToCoverageEnabled != rhs.multisampleState.alphaToCoverageEnabled ||
        lhs.depthStencilState.depthTestEnabled != rhs.depthStencilState.depthTestEnabled ||
        lhs.depthStencilState.depthWriteEnabled != rhs.depthStencilState.depthWriteEnabled ||
        lhs.depthStencilState.depthCompare != rhs.depthStencilState.depthCompare ||
        lhs.depthStencilState.stencilTestEnabled != rhs.depthStencilState.stencilTestEnabled ||
        lhs.depthStencilState.stencilReadMask != rhs.depthStencilState.stencilReadMask ||
        lhs.depthStencilState.stencilWriteMask != rhs.depthStencilState.stencilWriteMask ||
        lhs.depthStencilState.frontStencil.compare != rhs.depthStencilState.frontStencil.compare ||
        lhs.depthStencilState.frontStencil.stencilFailOp != rhs.depthStencilState.frontStencil.stencilFailOp ||
        lhs.depthStencilState.frontStencil.depthFailOp != rhs.depthStencilState.frontStencil.depthFailOp ||
        lhs.depthStencilState.frontStencil.passOp != rhs.depthStencilState.frontStencil.passOp ||
        lhs.depthStencilState.backStencil.compare != rhs.depthStencilState.backStencil.compare ||
        lhs.depthStencilState.backStencil.stencilFailOp != rhs.depthStencilState.backStencil.stencilFailOp ||
        lhs.depthStencilState.backStencil.depthFailOp != rhs.depthStencilState.backStencil.depthFailOp ||
        lhs.depthStencilState.backStencil.passOp != rhs.depthStencilState.backStencil.passOp ||
        lhs.colorAttachmentCount != rhs.colorAttachmentCount ||
        lhs.depthStencilFormat != rhs.depthStencilFormat) {
        return false;
    }
    if (lhs.vertexInput.bufferCount > kMaxGraphicsPipelineVertexBuffers ||
        lhs.vertexInput.attributeCount > kMaxGraphicsPipelineVertexAttributes ||
        lhs.colorAttachmentCount > kMaxGraphicsPipelineColorAttachments) {
        return false;
    }

    for (uint32_t i = 0; i < lhs.vertexInput.bufferCount; ++i) {
        if (lhs.vertexInput.buffers[i].stride != rhs.vertexInput.buffers[i].stride ||
            lhs.vertexInput.buffers[i].stepFunction != rhs.vertexInput.buffers[i].stepFunction ||
            lhs.vertexInput.buffers[i].stepRate != rhs.vertexInput.buffers[i].stepRate) {
            return false;
        }
    }
    for (uint32_t i = 0; i < lhs.vertexInput.attributeCount; ++i) {
        if (lhs.vertexInput.attributes[i].location != rhs.vertexInput.attributes[i].location ||
            lhs.vertexInput.attributes[i].bufferIndex != rhs.vertexInput.attributes[i].bufferIndex ||
            lhs.vertexInput.attributes[i].format != rhs.vertexInput.attributes[i].format ||
            lhs.vertexInput.attributes[i].offset != rhs.vertexInput.attributes[i].offset) {
            return false;
        }
    }

    for (uint32_t i = 0; i < lhs.colorAttachmentCount; ++i) {
        if (lhs.colorAttachments[i].format != rhs.colorAttachments[i].format ||
            lhs.colorAttachments[i].blendEnabled != rhs.colorAttachments[i].blendEnabled ||
            lhs.colorAttachments[i].sourceColorBlendFactor != rhs.colorAttachments[i].sourceColorBlendFactor ||
            lhs.colorAttachments[i].destinationColorBlendFactor != rhs.colorAttachments[i].destinationColorBlendFactor ||
            lhs.colorAttachments[i].colorBlendOperation != rhs.colorAttachments[i].colorBlendOperation ||
            lhs.colorAttachments[i].sourceAlphaBlendFactor != rhs.colorAttachments[i].sourceAlphaBlendFactor ||
            lhs.colorAttachments[i].destinationAlphaBlendFactor != rhs.colorAttachments[i].destinationAlphaBlendFactor ||
            lhs.colorAttachments[i].alphaBlendOperation != rhs.colorAttachments[i].alphaBlendOperation ||
            lhs.colorAttachments[i].writeMask != rhs.colorAttachments[i].writeMask) {
            return false;
        }
    }

    return true;
}

size_t GraphicsPipelineDescHash(const GraphicsPipelineDesc& desc) {
    uint64_t hash = 14695981039346656037ull;

    HashCombine(hash, static_cast<uint64_t>(desc.vertexShader.stage));
    HashString(hash, desc.vertexShader.entryPoint);
    HashCombine(hash, static_cast<uint64_t>(desc.fragmentShader.stage));
    HashString(hash, desc.fragmentShader.entryPoint);
    HashCombine(hash, desc.vertexInput.bufferCount);
    const uint32_t hashedVertexBufferCount =
        desc.vertexInput.bufferCount < kMaxGraphicsPipelineVertexBuffers
            ? desc.vertexInput.bufferCount
            : static_cast<uint32_t>(kMaxGraphicsPipelineVertexBuffers);
    for (uint32_t i = 0; i < hashedVertexBufferCount; ++i) {
        HashCombine(hash, desc.vertexInput.buffers[i].stride);
        HashCombine(hash, static_cast<uint64_t>(desc.vertexInput.buffers[i].stepFunction));
        HashCombine(hash, desc.vertexInput.buffers[i].stepRate);
    }
    HashCombine(hash, desc.vertexInput.attributeCount);
    const uint32_t hashedVertexAttributeCount =
        desc.vertexInput.attributeCount < kMaxGraphicsPipelineVertexAttributes
            ? desc.vertexInput.attributeCount
            : static_cast<uint32_t>(kMaxGraphicsPipelineVertexAttributes);
    for (uint32_t i = 0; i < hashedVertexAttributeCount; ++i) {
        HashCombine(hash, desc.vertexInput.attributes[i].location);
        HashCombine(hash, desc.vertexInput.attributes[i].bufferIndex);
        HashCombine(hash, static_cast<uint64_t>(desc.vertexInput.attributes[i].format));
        HashCombine(hash, desc.vertexInput.attributes[i].offset);
    }
    HashCombine(hash, static_cast<uint64_t>(desc.rasterState.primitiveTopology));
    HashCombine(hash, static_cast<uint64_t>(desc.rasterState.fillMode));
    HashCombine(hash, static_cast<uint64_t>(desc.rasterState.cullMode));
    HashCombine(hash, static_cast<uint64_t>(desc.rasterState.frontFace));
    HashCombine(hash, static_cast<uint64_t>(desc.rasterState.depthBias));
    HashFloat32(hash, desc.rasterState.depthBiasClamp);
    HashFloat32(hash, desc.rasterState.depthBiasSlopeScale);
    HashCombine(hash, desc.rasterState.depthClipEnabled ? 1 : 0);
    HashCombine(hash, desc.multisampleState.sampleCount);
    HashCombine(hash, desc.multisampleState.alphaToCoverageEnabled ? 1 : 0);
    HashCombine(hash, desc.depthStencilState.depthTestEnabled ? 1 : 0);
    HashCombine(hash, desc.depthStencilState.depthWriteEnabled ? 1 : 0);
    HashCombine(hash, static_cast<uint64_t>(desc.depthStencilState.depthCompare));
    HashCombine(hash, desc.depthStencilState.stencilTestEnabled ? 1 : 0);
    HashCombine(hash, desc.depthStencilState.stencilReadMask);
    HashCombine(hash, desc.depthStencilState.stencilWriteMask);
    HashCombine(hash, static_cast<uint64_t>(desc.depthStencilState.frontStencil.compare));
    HashCombine(hash, static_cast<uint64_t>(desc.depthStencilState.frontStencil.stencilFailOp));
    HashCombine(hash, static_cast<uint64_t>(desc.depthStencilState.frontStencil.depthFailOp));
    HashCombine(hash, static_cast<uint64_t>(desc.depthStencilState.frontStencil.passOp));
    HashCombine(hash, static_cast<uint64_t>(desc.depthStencilState.backStencil.compare));
    HashCombine(hash, static_cast<uint64_t>(desc.depthStencilState.backStencil.stencilFailOp));
    HashCombine(hash, static_cast<uint64_t>(desc.depthStencilState.backStencil.depthFailOp));
    HashCombine(hash, static_cast<uint64_t>(desc.depthStencilState.backStencil.passOp));
    HashCombine(hash, desc.colorAttachmentCount);
    const uint32_t hashedColorAttachmentCount =
        desc.colorAttachmentCount < kMaxGraphicsPipelineColorAttachments
            ? desc.colorAttachmentCount
            : static_cast<uint32_t>(kMaxGraphicsPipelineColorAttachments);
    for (uint32_t i = 0; i < hashedColorAttachmentCount; ++i) {
        HashCombine(hash, static_cast<uint64_t>(desc.colorAttachments[i].format));
        HashCombine(hash, desc.colorAttachments[i].blendEnabled ? 1 : 0);
        HashCombine(hash, static_cast<uint64_t>(desc.colorAttachments[i].sourceColorBlendFactor));
        HashCombine(hash, static_cast<uint64_t>(desc.colorAttachments[i].destinationColorBlendFactor));
        HashCombine(hash, static_cast<uint64_t>(desc.colorAttachments[i].colorBlendOperation));
        HashCombine(hash, static_cast<uint64_t>(desc.colorAttachments[i].sourceAlphaBlendFactor));
        HashCombine(hash, static_cast<uint64_t>(desc.colorAttachments[i].destinationAlphaBlendFactor));
        HashCombine(hash, static_cast<uint64_t>(desc.colorAttachments[i].alphaBlendOperation));
        HashCombine(hash, desc.colorAttachments[i].writeMask);
    }
    HashCombine(hash, static_cast<uint64_t>(desc.depthStencilFormat));

    return static_cast<size_t>(hash);
}

bool SamplerDescEquals(const SamplerDesc& lhs, const SamplerDesc& rhs) {
    return lhs.minFilter == rhs.minFilter &&
        lhs.magFilter == rhs.magFilter &&
        lhs.mipFilter == rhs.mipFilter &&
        lhs.addressU == rhs.addressU &&
        lhs.addressV == rhs.addressV &&
        lhs.addressW == rhs.addressW &&
        lhs.borderColor == rhs.borderColor &&
        lhs.compareFunction == rhs.compareFunction &&
        lhs.anisotropyEnabled == rhs.anisotropyEnabled &&
        lhs.maxAnisotropy == rhs.maxAnisotropy &&
        DescriptorFloatEquals(lhs.minLod, rhs.minLod) &&
        DescriptorFloatEquals(lhs.maxLod, rhs.maxLod) &&
        DescriptorFloatEquals(lhs.mipLodBias, rhs.mipLodBias);
}

bool TextureDescEstimatedBytes(const TextureDesc& desc, uint64_t& outBytes) {
    outBytes = 0;
    const uint32_t bytesPerPixel = FormatBytesPerPixel(desc.format);
    if (bytesPerPixel == 0 ||
        desc.extent.width == 0 ||
        desc.extent.height == 0 ||
        !SampleCountIsValid(desc.sampleCount)) {
        return false;
    }

    const uint64_t maxBytes = std::numeric_limits<uint64_t>::max();
    if (desc.extent.width > maxBytes / bytesPerPixel) {
        return false;
    }

    const uint64_t rowBytes = static_cast<uint64_t>(desc.extent.width) * bytesPerPixel;
    if (desc.extent.height > maxBytes / rowBytes) {
        return false;
    }

    const uint64_t textureBytes = rowBytes * static_cast<uint64_t>(desc.extent.height);
    if (desc.sampleCount > maxBytes / textureBytes) {
        return false;
    }

    outBytes = textureBytes * desc.sampleCount;
    return true;
}

const char* ResourceDescriptorErrorName(ResourceDescriptorError error) {
    switch (error) {
        case ResourceDescriptorError::None:                    return "none";
        case ResourceDescriptorError::EmptyBuffer:             return "empty_buffer";
        case ResourceDescriptorError::EmptyTextureExtent:      return "empty_texture_extent";
        case ResourceDescriptorError::MissingUsage:            return "missing_usage";
        case ResourceDescriptorError::UnknownTextureFormat:    return "unknown_texture_format";
        case ResourceDescriptorError::TextureSizeOverflow:     return "texture_size_overflow";
        case ResourceDescriptorError::UnsupportedMemory:       return "unsupported_memory";
        case ResourceDescriptorError::UnsupportedRenderTargetFormat:
            return "unsupported_render_target_format";
        case ResourceDescriptorError::UnsupportedDepthStencilFormat:
            return "unsupported_depth_stencil_format";
        case ResourceDescriptorError::UnsupportedInitialState: return "unsupported_initial_state";
        case ResourceDescriptorError::UnsupportedSampleCount:  return "unsupported_sample_count";
        default:                                               return "unknown";
    }
}

const char* ResourceStateName(ResourceState state) {
    switch (state) {
        case ResourceState::Undefined:       return "undefined";
        case ResourceState::Common:          return "common";
        case ResourceState::CopySource:      return "copy_source";
        case ResourceState::CopyDestination: return "copy_destination";
        case ResourceState::VertexBuffer:    return "vertex_buffer";
        case ResourceState::IndexBuffer:     return "index_buffer";
        case ResourceState::ConstantBuffer:  return "constant_buffer";
        case ResourceState::RenderTarget:    return "render_target";
        case ResourceState::DepthWrite:      return "depth_write";
        case ResourceState::ShaderRead:      return "shader_read";
        case ResourceState::ShaderWrite:     return "shader_write";
        case ResourceState::Present:         return "present";
        default:                             return "unknown";
    }
}

const char* ResourceUsageName(ResourceUsage usage) {
    switch (usage) {
        case ResourceUsage::None:            return "none";
        case ResourceUsage::VertexBuffer:    return "vertex_buffer";
        case ResourceUsage::IndexBuffer:     return "index_buffer";
        case ResourceUsage::ConstantBuffer:  return "constant_buffer";
        case ResourceUsage::ShaderRead:      return "shader_read";
        case ResourceUsage::ShaderWrite:     return "shader_write";
        case ResourceUsage::RenderTarget:    return "render_target";
        case ResourceUsage::DepthStencil:    return "depth_stencil";
        case ResourceUsage::CopySource:      return "copy_source";
        case ResourceUsage::CopyDestination: return "copy_destination";
        case ResourceUsage::Present:         return "present";
        default:                             return "unknown";
    }
}

bool ResourceUsageSupportsState(ResourceUsageFlags usage, ResourceState state) {
    switch (state) {
        case ResourceState::Undefined:
        case ResourceState::Common:
            return true;
        case ResourceState::CopySource:
            return HasResourceUsage(usage, ResourceUsage::CopySource);
        case ResourceState::CopyDestination:
            return HasResourceUsage(usage, ResourceUsage::CopyDestination);
        case ResourceState::VertexBuffer:
            return HasResourceUsage(usage, ResourceUsage::VertexBuffer);
        case ResourceState::IndexBuffer:
            return HasResourceUsage(usage, ResourceUsage::IndexBuffer);
        case ResourceState::ConstantBuffer:
            return HasResourceUsage(usage, ResourceUsage::ConstantBuffer);
        case ResourceState::RenderTarget:
            return HasResourceUsage(usage, ResourceUsage::RenderTarget);
        case ResourceState::DepthWrite:
            return HasResourceUsage(usage, ResourceUsage::DepthStencil);
        case ResourceState::ShaderRead:
            return HasResourceUsage(usage, ResourceUsage::ShaderRead);
        case ResourceState::ShaderWrite:
            return HasResourceUsage(usage, ResourceUsage::ShaderWrite);
        case ResourceState::Present:
            return HasResourceUsage(usage, ResourceUsage::Present);
        default:
            return false;
    }
}

ResourceDescriptorValidation ValidateBufferDesc(const BufferDesc& desc) {
    if (desc.sizeBytes == 0) {
        ResourceDescriptorValidation validation;
        MarkDescriptorError(validation, ResourceDescriptorError::EmptyBuffer, desc.memory, desc.initialState);
        return validation;
    }

    if (!ResourceMemoryIsValid(desc.memory)) {
        ResourceDescriptorValidation validation;
        MarkDescriptorError(validation, ResourceDescriptorError::UnsupportedMemory, desc.memory, desc.initialState);
        return validation;
    }

    if (desc.usage == ResourceUsageFlag(ResourceUsage::None)) {
        ResourceDescriptorValidation validation;
        MarkDescriptorError(validation, ResourceDescriptorError::MissingUsage, desc.memory, desc.initialState);
        return validation;
    }

    if (!BufferDescSupportsInitialState(desc.initialState) ||
        !ResourceUsageSupportsState(desc.usage, desc.initialState)) {
        ResourceDescriptorValidation validation;
        MarkDescriptorError(validation,
                            ResourceDescriptorError::UnsupportedInitialState,
                            desc.memory,
                            desc.initialState);
        return validation;
    }

    ResourceDescriptorValidation validation;
    validation.memory = desc.memory;
    validation.initialState = desc.initialState;
    return validation;
}

ResourceDescriptorValidation ValidateTextureDesc(const TextureDesc& desc) {
    if (desc.extent.width == 0 || desc.extent.height == 0) {
        ResourceDescriptorValidation validation;
        MarkDescriptorError(validation, ResourceDescriptorError::EmptyTextureExtent, desc.memory, desc.initialState);
        return validation;
    }

    if (FormatBytesPerPixel(desc.format) == 0) {
        ResourceDescriptorValidation validation;
        MarkDescriptorError(validation, ResourceDescriptorError::UnknownTextureFormat, desc.memory, desc.initialState);
        return validation;
    }

    if (!ResourceMemoryIsValid(desc.memory)) {
        ResourceDescriptorValidation validation;
        MarkDescriptorError(validation, ResourceDescriptorError::UnsupportedMemory, desc.memory, desc.initialState);
        return validation;
    }

    if (desc.usage == ResourceUsageFlag(ResourceUsage::None)) {
        ResourceDescriptorValidation validation;
        MarkDescriptorError(validation, ResourceDescriptorError::MissingUsage, desc.memory, desc.initialState);
        return validation;
    }

    if (!SampleCountIsValid(desc.sampleCount)) {
        ResourceDescriptorValidation validation;
        MarkDescriptorError(validation,
                            ResourceDescriptorError::UnsupportedSampleCount,
                            desc.memory,
                            desc.initialState);
        return validation;
    }

    if (desc.sampleCount > 1 &&
        !HasResourceUsage(desc.usage, ResourceUsage::RenderTarget) &&
        !HasResourceUsage(desc.usage, ResourceUsage::DepthStencil)) {
        ResourceDescriptorValidation validation;
        MarkDescriptorError(validation,
                            ResourceDescriptorError::UnsupportedSampleCount,
                            desc.memory,
                            desc.initialState);
        return validation;
    }

    if (desc.sampleCount > 1 && desc.memory != ResourceMemory::DeviceLocal) {
        ResourceDescriptorValidation validation;
        MarkDescriptorError(validation, ResourceDescriptorError::UnsupportedMemory, desc.memory, desc.initialState);
        return validation;
    }

    uint64_t estimatedBytes = 0;
    if (!TextureDescEstimatedBytes(desc, estimatedBytes)) {
        ResourceDescriptorValidation validation;
        MarkDescriptorError(validation, ResourceDescriptorError::TextureSizeOverflow, desc.memory, desc.initialState);
        return validation;
    }

    if (HasResourceUsage(desc.usage, ResourceUsage::RenderTarget) &&
        !FormatSupportsColorAttachment(desc.format)) {
        ResourceDescriptorValidation validation;
        MarkDescriptorError(validation,
                            ResourceDescriptorError::UnsupportedRenderTargetFormat,
                            desc.memory,
                            desc.initialState);
        return validation;
    }

    if (HasResourceUsage(desc.usage, ResourceUsage::DepthStencil) &&
        !FormatSupportsDepthStencil(desc.format)) {
        ResourceDescriptorValidation validation;
        MarkDescriptorError(validation,
                            ResourceDescriptorError::UnsupportedDepthStencilFormat,
                            desc.memory,
                            desc.initialState);
        return validation;
    }

    if (!TextureDescSupportsInitialState(desc.initialState) ||
        !ResourceUsageSupportsState(desc.usage, desc.initialState)) {
        ResourceDescriptorValidation validation;
        MarkDescriptorError(validation,
                            ResourceDescriptorError::UnsupportedInitialState,
                            desc.memory,
                            desc.initialState);
        return validation;
    }

    ResourceDescriptorValidation validation;
    validation.memory = desc.memory;
    validation.initialState = desc.initialState;
    return validation;
}

ResourcePoolMemoryStats* FindResourcePoolMemoryStats(ResourcePoolStats& stats, ResourceMemory memory) {
    switch (memory) {
        case ResourceMemory::DeviceLocal:
            return &stats.deviceLocal;
        case ResourceMemory::Shared:
            return &stats.shared;
        case ResourceMemory::Upload:
            return &stats.upload;
        case ResourceMemory::Readback:
            return &stats.readback;
        default:
            return nullptr;
    }
}

const ResourcePoolMemoryStats* FindResourcePoolMemoryStats(const ResourcePoolStats& stats, ResourceMemory memory) {
    switch (memory) {
        case ResourceMemory::DeviceLocal:
            return &stats.deviceLocal;
        case ResourceMemory::Shared:
            return &stats.shared;
        case ResourceMemory::Upload:
            return &stats.upload;
        case ResourceMemory::Readback:
            return &stats.readback;
        default:
            return nullptr;
    }
}

bool SetResourcePoolMemoryBudget(ResourcePoolStats& stats, ResourceMemory memory, uint64_t budgetBytes) {
    ResourcePoolMemoryStats* memoryStats = FindResourcePoolMemoryStats(stats, memory);
    if (!memoryStats) {
        return false;
    }

    memoryStats->budgetBytes = budgetBytes;
    return true;
}

bool ResourcePoolCanAllocate(const ResourcePoolStats& stats, ResourceMemory memory, uint64_t sizeBytes) {
    const ResourcePoolMemoryStats* memoryStats = FindResourcePoolMemoryStats(stats, memory);
    if (!memoryStats) {
        return false;
    }

    const uint64_t maxBytes = std::numeric_limits<uint64_t>::max();
    if (sizeBytes > maxBytes - memoryStats->liveBytes) {
        return false;
    }

    if (memoryStats->budgetBytes == 0) {
        return true;
    }

    if (memoryStats->liveBytes > memoryStats->budgetBytes) {
        return false;
    }

    return sizeBytes <= memoryStats->budgetBytes - memoryStats->liveBytes;
}

bool ResourcePoolMemoryBudgetRemaining(const ResourcePoolStats& stats,
                                       ResourceMemory memory,
                                       uint64_t& outBytes) {
    outBytes = 0;
    const ResourcePoolMemoryStats* memoryStats = FindResourcePoolMemoryStats(stats, memory);
    if (!memoryStats || memoryStats->budgetBytes == 0) {
        return false;
    }

    if (memoryStats->liveBytes >= memoryStats->budgetBytes) {
        return true;
    }

    outBytes = memoryStats->budgetBytes - memoryStats->liveBytes;
    return true;
}

bool ResourcePoolMemoryIsOverBudget(const ResourcePoolStats& stats, ResourceMemory memory) {
    const ResourcePoolMemoryStats* memoryStats = FindResourcePoolMemoryStats(stats, memory);
    return memoryStats && memoryStats->budgetBytes != 0 && memoryStats->liveBytes > memoryStats->budgetBytes;
}

void RecordResourcePoolAllocation(ResourcePoolStats& stats, uint64_t sizeBytes) {
    ResourcePoolMemoryStats aggregate;
    aggregate.liveResourceCount = stats.liveResourceCount;
    aggregate.liveBytes = stats.liveBytes;
    aggregate.peakResourceCount = stats.peakResourceCount;
    aggregate.peakBytes = stats.peakBytes;
    RecordPoolMemoryAllocation(aggregate, sizeBytes);
    stats.liveResourceCount = aggregate.liveResourceCount;
    stats.liveBytes = aggregate.liveBytes;
    stats.peakResourceCount = aggregate.peakResourceCount;
    stats.peakBytes = aggregate.peakBytes;
}

void RecordResourcePoolAllocation(ResourcePoolStats& stats, ResourceMemory memory, uint64_t sizeBytes) {
    RecordResourcePoolAllocation(stats, sizeBytes);
    if (ResourcePoolMemoryStats* memoryStats = FindResourcePoolMemoryStats(stats, memory)) {
        RecordPoolMemoryAllocation(*memoryStats, sizeBytes);
    }
}

void RecordResourcePoolRelease(ResourcePoolStats& stats, uint64_t sizeBytes) {
    ResourcePoolMemoryStats aggregate;
    aggregate.liveResourceCount = stats.liveResourceCount;
    aggregate.liveBytes = stats.liveBytes;
    aggregate.peakResourceCount = stats.peakResourceCount;
    aggregate.peakBytes = stats.peakBytes;
    RecordPoolMemoryRelease(aggregate, sizeBytes);
    stats.liveResourceCount = aggregate.liveResourceCount;
    stats.liveBytes = aggregate.liveBytes;
}

void RecordResourcePoolRelease(ResourcePoolStats& stats, ResourceMemory memory, uint64_t sizeBytes) {
    RecordResourcePoolRelease(stats, sizeBytes);
    if (ResourcePoolMemoryStats* memoryStats = FindResourcePoolMemoryStats(stats, memory)) {
        RecordPoolMemoryRelease(*memoryStats, sizeBytes);
    }
}

void RecordResourcePoolAllocationFailure(ResourcePoolStats& stats, uint64_t sizeBytes) {
    if (stats.failedAllocationCount < std::numeric_limits<uint64_t>::max()) {
        ++stats.failedAllocationCount;
    }

    const uint64_t maxBytes = std::numeric_limits<uint64_t>::max();
    stats.failedAllocationBytes =
        sizeBytes > maxBytes - stats.failedAllocationBytes ? maxBytes : stats.failedAllocationBytes + sizeBytes;
}

void RecordResourcePoolAllocationFailure(ResourcePoolStats& stats, ResourceMemory memory, uint64_t sizeBytes) {
    RecordResourcePoolAllocationFailure(stats, sizeBytes);
    if (ResourcePoolMemoryStats* memoryStats = FindResourcePoolMemoryStats(stats, memory)) {
        RecordPoolMemoryAllocationFailure(*memoryStats, sizeBytes);
    }
}

bool ResourceStateSupportedOnQueue(ResourceState state, QueueClass queueClass) {
    switch (state) {
        case ResourceState::Undefined:
        case ResourceState::Common:
        case ResourceState::CopySource:
        case ResourceState::CopyDestination:
            switch (queueClass) {
                case QueueClass::Graphics:
                case QueueClass::Compute:
                case QueueClass::Copy:
                    return true;
                default:
                    return false;
            }
        case ResourceState::ConstantBuffer:
        case ResourceState::ShaderRead:
        case ResourceState::ShaderWrite:
            return queueClass == QueueClass::Graphics || queueClass == QueueClass::Compute;
        case ResourceState::VertexBuffer:
        case ResourceState::IndexBuffer:
        case ResourceState::RenderTarget:
        case ResourceState::DepthWrite:
        case ResourceState::Present:
            return queueClass == QueueClass::Graphics;
        default:
            return false;
    }
}

const char* ResourceTransitionErrorName(ResourceTransitionError error) {
    switch (error) {
        case ResourceTransitionError::None:
            return "none";
        case ResourceTransitionError::NullTransitionList:
            return "null_transition_list";
        case ResourceTransitionError::NullResource:
            return "null_resource";
        case ResourceTransitionError::StaleBeforeState:
            return "stale_before_state";
        case ResourceTransitionError::UnsupportedTargetState:
            return "unsupported_target_state";
        case ResourceTransitionError::UnsupportedQueueClass:
            return "unsupported_queue_class";
        case ResourceTransitionError::DuplicateResource:
            return "duplicate_resource";
        default:
            return "unknown";
    }
}

ResourceTransition MakeResourceTransition(Resource& resource, ResourceState after) {
    return MakeResourceTransition(resource, after, QueueClass::Graphics);
}

ResourceTransition MakeResourceTransition(Resource& resource, ResourceState after, QueueClass queueClass) {
    ResourceTransition transition;
    transition.resource = &resource;
    transition.before = resource.GetCurrentState();
    transition.after = after;
    transition.queueClass = queueClass;
    return transition;
}

ResourceTransition MakeResourceTransition(Resource& resource,
                                          ResourceState after,
                                          const CommandContext& context) {
    return MakeResourceTransition(resource, after, context.GetQueueClass());
}

ResourceTransitionValidation ValidateResourceTransition(const ResourceTransition& transition) {
    if (!transition.resource) {
        ResourceTransitionValidation validation;
        validation.error = ResourceTransitionError::NullResource;
        return validation;
    }

    if (transition.resource->GetCurrentState() != transition.before) {
        ResourceTransitionValidation validation;
        validation.error = ResourceTransitionError::StaleBeforeState;
        validation.current = transition.resource->GetCurrentState();
        return validation;
    }

    if (!ResourceUsageSupportsState(transition.resource->GetUsageFlags(), transition.after)) {
        ResourceTransitionValidation validation;
        validation.error = ResourceTransitionError::UnsupportedTargetState;
        validation.current = transition.resource->GetCurrentState();
        return validation;
    }

    if (!ResourceStateSupportedOnQueue(transition.after, transition.queueClass)) {
        ResourceTransitionValidation validation;
        validation.error = ResourceTransitionError::UnsupportedQueueClass;
        validation.current = transition.resource->GetCurrentState();
        return validation;
    }

    return {};
}

ResourceTransitionValidation ValidateResourceTransitions(const ResourceTransition* transitions, size_t count) {
    if (count == 0) {
        return {};
    }
    if (!transitions) {
        ResourceTransitionValidation validation;
        validation.error = ResourceTransitionError::NullTransitionList;
        return validation;
    }

    for (size_t i = 0; i < count; ++i) {
        ResourceTransitionValidation validation = ValidateResourceTransition(transitions[i]);
        if (!validation) {
            validation.index = i;
            return validation;
        }

        for (size_t j = 0; j < i; ++j) {
            if (transitions[i].resource == transitions[j].resource) {
                validation.error = ResourceTransitionError::DuplicateResource;
                validation.index = i;
                validation.conflictIndex = j;
                validation.current = transitions[i].resource->GetCurrentState();
                return validation;
            }
        }
    }

    return {};
}

bool ApplyResourceTransition(const ResourceTransition& transition) {
    if (!ValidateResourceTransition(transition)) {
        return false;
    }

    transition.resource->SetCurrentState(transition.after);
    return true;
}

bool ApplyResourceTransitions(const ResourceTransition* transitions, size_t count) {
    if (!ValidateResourceTransitions(transitions, count)) {
        return false;
    }

    for (size_t i = 0; i < count; ++i) {
        transitions[i].resource->SetCurrentState(transitions[i].after);
    }
    return true;
}

bool TransitionResource(Resource& resource, ResourceState after) {
    return ApplyResourceTransition(MakeResourceTransition(resource, after));
}

bool TransitionResource(Resource& resource, ResourceState after, const CommandContext& context) {
    return ApplyResourceTransition(MakeResourceTransition(resource, after, context));
}

namespace {

bool FrameGraphAccessTypeIsValid(FrameGraphAccessType access) {
    switch (access) {
        case FrameGraphAccessType::Read:
        case FrameGraphAccessType::Write:
        case FrameGraphAccessType::ReadWrite:
            return true;
        default:
            return false;
    }
}

bool FrameGraphShaderStagesSupportedOnQueue(ShaderStageFlags stages, QueueClass queueClass) {
    if (stages == 0) {
        return true;
    }

    switch (queueClass) {
        case QueueClass::Graphics:
            return !HasShaderStage(stages, ShaderStage::Compute);
        case QueueClass::Compute:
            return !HasShaderStage(stages, ShaderStage::Vertex) &&
                !HasShaderStage(stages, ShaderStage::Fragment);
        case QueueClass::Copy:
        default:
            return false;
    }
}

FrameGraphDescriptorValidation MakeFrameGraphError(FrameGraphDescriptorError error,
                                                   uint32_t resourceIndex = kInvalidFrameGraphResourceIndex,
                                                   uint32_t passIndex = 0,
                                                   uint32_t accessIndex = 0) {
    FrameGraphDescriptorValidation validation;
    validation.error = error;
    validation.resourceIndex = resourceIndex;
    validation.passIndex = passIndex;
    validation.accessIndex = accessIndex;
    return validation;
}

FrameGraphDescriptorValidation MakeFrameGraphDependencyError(FrameGraphDescriptorError error,
                                                             uint32_t passIndex,
                                                             uint32_t dependencyIndex,
                                                             uint32_t dependencyPassIndex) {
    FrameGraphDescriptorValidation validation =
        MakeFrameGraphError(error, kInvalidFrameGraphResourceIndex, passIndex);
    validation.dependencyIndex = dependencyIndex;
    validation.dependencyPassIndex = dependencyPassIndex;
    return validation;
}

} // namespace

const char* FrameGraphAccessTypeName(FrameGraphAccessType access) {
    switch (access) {
        case FrameGraphAccessType::Read:
            return "read";
        case FrameGraphAccessType::Write:
            return "write";
        case FrameGraphAccessType::ReadWrite:
            return "read_write";
        default:
            return "unknown";
    }
}

const char* FrameGraphDescriptorErrorName(FrameGraphDescriptorError error) {
    switch (error) {
        case FrameGraphDescriptorError::None:
            return "none";
        case FrameGraphDescriptorError::MissingResource:
            return "missing_resource";
        case FrameGraphDescriptorError::TooManyResources:
            return "too_many_resources";
        case FrameGraphDescriptorError::MissingPass:
            return "missing_pass";
        case FrameGraphDescriptorError::TooManyPasses:
            return "too_many_passes";
        case FrameGraphDescriptorError::MissingPassAccess:
            return "missing_pass_access";
        case FrameGraphDescriptorError::TooManyPassAccesses:
            return "too_many_pass_accesses";
        case FrameGraphDescriptorError::TooManyPassDependencies:
            return "too_many_pass_dependencies";
        case FrameGraphDescriptorError::InvalidPassDependency:
            return "invalid_pass_dependency";
        case FrameGraphDescriptorError::DuplicatePassDependency:
            return "duplicate_pass_dependency";
        case FrameGraphDescriptorError::InvalidResourceIndex:
            return "invalid_resource_index";
        case FrameGraphDescriptorError::InvalidResourceType:
            return "invalid_resource_type";
        case FrameGraphDescriptorError::DuplicateResourceAccess:
            return "duplicate_resource_access";
        case FrameGraphDescriptorError::UnsupportedTargetState:
            return "unsupported_target_state";
        case FrameGraphDescriptorError::UnsupportedQueueClass:
            return "unsupported_queue_class";
        case FrameGraphDescriptorError::InvalidAccessType:
            return "invalid_access_type";
        case FrameGraphDescriptorError::InvalidShaderStage:
            return "invalid_shader_stage";
        case FrameGraphDescriptorError::UnsupportedShaderStage:
            return "unsupported_shader_stage";
        case FrameGraphDescriptorError::TransitionCapacityExceeded:
            return "transition_capacity_exceeded";
        default:
            return "unknown";
    }
}

const char* FrameGraphExecutionErrorName(FrameGraphExecutionError error) {
    switch (error) {
        case FrameGraphExecutionError::None:
            return "none";
        case FrameGraphExecutionError::InvalidCompileResult:
            return "invalid_compile_result";
        case FrameGraphExecutionError::InvalidPassIndex:
            return "invalid_pass_index";
        case FrameGraphExecutionError::MissingResourceBinding:
            return "missing_resource_binding";
        case FrameGraphExecutionError::ResourceTypeMismatch:
            return "resource_type_mismatch";
        case FrameGraphExecutionError::TransitionCapacityExceeded:
            return "transition_capacity_exceeded";
        case FrameGraphExecutionError::InvalidResourceTransition:
            return "invalid_resource_transition";
        default:
            return "unknown";
    }
}

FrameGraphResourceHandle MakeFrameGraphResourceHandle(uint32_t resourceIndex) {
    return FrameGraphResourceHandle{resourceIndex};
}

FrameGraphPassResourceAccess MakeFrameGraphPassResourceAccess(FrameGraphResourceHandle resource,
                                                              ResourceState state,
                                                              FrameGraphAccessType access,
                                                              ShaderStageFlags shaderStages) {
    FrameGraphPassResourceAccess passAccess;
    passAccess.resource = resource;
    passAccess.state = state;
    passAccess.access = access;
    passAccess.shaderStages = shaderStages;
    return passAccess;
}

FrameGraphPassDependency MakeFrameGraphPassDependency(uint32_t passIndex) {
    return FrameGraphPassDependency{passIndex};
}

bool AddFrameGraphResource(FrameGraphDesc& graph,
                           const FrameGraphResourceDesc& resource,
                           FrameGraphResourceHandle* outHandle) {
    if (graph.resourceCount >= graph.resources.size()) {
        if (outHandle) {
            *outHandle = {};
        }
        return false;
    }

    const uint32_t index = graph.resourceCount++;
    graph.resources[index] = resource;
    if (outHandle) {
        *outHandle = MakeFrameGraphResourceHandle(index);
    }
    return true;
}

bool AddFrameGraphPassAccess(FrameGraphPassDesc& pass, const FrameGraphPassResourceAccess& access) {
    if (pass.accessCount >= pass.accesses.size()) {
        return false;
    }

    pass.accesses[pass.accessCount++] = access;
    return true;
}

bool AddFrameGraphPassDependency(FrameGraphPassDesc& pass, const FrameGraphPassDependency& dependency) {
    if (pass.dependencyCount >= pass.dependencies.size()) {
        return false;
    }

    pass.dependencies[pass.dependencyCount++] = dependency;
    return true;
}

bool AddFrameGraphPass(FrameGraphDesc& graph, const FrameGraphPassDesc& pass) {
    if (graph.passCount >= graph.passes.size()) {
        return false;
    }

    graph.passes[graph.passCount++] = pass;
    return true;
}

FrameGraphDescriptorValidation ValidateFrameGraphDesc(const FrameGraphDesc& graph) {
    if (graph.resourceCount == 0) {
        return MakeFrameGraphError(FrameGraphDescriptorError::MissingResource);
    }
    if (graph.resourceCount > graph.resources.size()) {
        return MakeFrameGraphError(FrameGraphDescriptorError::TooManyResources);
    }
    if (graph.passCount == 0) {
        return MakeFrameGraphError(FrameGraphDescriptorError::MissingPass);
    }
    if (graph.passCount > graph.passes.size()) {
        return MakeFrameGraphError(FrameGraphDescriptorError::TooManyPasses);
    }

    for (uint32_t resourceIndex = 0; resourceIndex < graph.resourceCount; ++resourceIndex) {
        if (!graph.resources[resourceIndex].HasKnownType()) {
            return MakeFrameGraphError(FrameGraphDescriptorError::InvalidResourceType,
                                       resourceIndex);
        }
    }

    for (uint32_t passIndex = 0; passIndex < graph.passCount; ++passIndex) {
        const FrameGraphPassDesc& pass = graph.passes[passIndex];
        if (pass.accessCount == 0) {
            return MakeFrameGraphError(FrameGraphDescriptorError::MissingPassAccess,
                                       kInvalidFrameGraphResourceIndex,
                                       passIndex);
        }
        if (pass.accessCount > pass.accesses.size()) {
            return MakeFrameGraphError(FrameGraphDescriptorError::TooManyPassAccesses,
                                       kInvalidFrameGraphResourceIndex,
                                       passIndex);
        }
        if (pass.dependencyCount > pass.dependencies.size()) {
            return MakeFrameGraphDependencyError(FrameGraphDescriptorError::TooManyPassDependencies,
                                                passIndex,
                                                pass.dependencyCount,
                                                kInvalidFrameGraphPassIndex);
        }

        for (uint32_t dependencyIndex = 0; dependencyIndex < pass.dependencyCount; ++dependencyIndex) {
            const FrameGraphPassDependency& dependency = pass.dependencies[dependencyIndex];
            if (!dependency.IsValid() || dependency.passIndex >= graph.passCount ||
                dependency.passIndex >= passIndex) {
                return MakeFrameGraphDependencyError(FrameGraphDescriptorError::InvalidPassDependency,
                                                    passIndex,
                                                    dependencyIndex,
                                                    dependency.passIndex);
            }

            for (uint32_t previousDependency = 0; previousDependency < dependencyIndex; ++previousDependency) {
                if (pass.dependencies[previousDependency].passIndex == dependency.passIndex) {
                    FrameGraphDescriptorValidation validation =
                        MakeFrameGraphDependencyError(FrameGraphDescriptorError::DuplicatePassDependency,
                                                     passIndex,
                                                     dependencyIndex,
                                                     dependency.passIndex);
                    validation.conflictDependencyIndex = previousDependency;
                    return validation;
                }
            }
        }

        for (uint32_t accessIndex = 0; accessIndex < pass.accessCount; ++accessIndex) {
            const FrameGraphPassResourceAccess& access = pass.accesses[accessIndex];
            if (!FrameGraphAccessTypeIsValid(access.access)) {
                return MakeFrameGraphError(FrameGraphDescriptorError::InvalidAccessType,
                                           access.resource.index,
                                           passIndex,
                                           accessIndex);
            }
            if (access.HasShaderStages() && !ShaderStageFlagsAreValid(access.shaderStages)) {
                FrameGraphDescriptorValidation validation =
                    MakeFrameGraphError(FrameGraphDescriptorError::InvalidShaderStage,
                                        access.resource.index,
                                        passIndex,
                                        accessIndex);
                validation.shaderStages = access.shaderStages;
                validation.state = access.state;
                validation.queueClass = pass.queueClass;
                return validation;
            }
            if (!FrameGraphShaderStagesSupportedOnQueue(access.shaderStages, pass.queueClass)) {
                FrameGraphDescriptorValidation validation =
                    MakeFrameGraphError(FrameGraphDescriptorError::UnsupportedShaderStage,
                                        access.resource.index,
                                        passIndex,
                                        accessIndex);
                validation.shaderStages = access.shaderStages;
                validation.state = access.state;
                validation.queueClass = pass.queueClass;
                return validation;
            }
            if (!access.resource.IsValid() || access.resource.index >= graph.resourceCount) {
                return MakeFrameGraphError(FrameGraphDescriptorError::InvalidResourceIndex,
                                           access.resource.index,
                                           passIndex,
                                           accessIndex);
            }

            const FrameGraphResourceDesc& resource = graph.resources[access.resource.index];
            if (!ResourceUsageSupportsState(resource.usage, access.state)) {
                FrameGraphDescriptorValidation validation =
                    MakeFrameGraphError(FrameGraphDescriptorError::UnsupportedTargetState,
                                        access.resource.index,
                                        passIndex,
                                        accessIndex);
                validation.state = access.state;
                validation.queueClass = pass.queueClass;
                return validation;
            }
            if (!ResourceStateSupportedOnQueue(access.state, pass.queueClass)) {
                FrameGraphDescriptorValidation validation =
                    MakeFrameGraphError(FrameGraphDescriptorError::UnsupportedQueueClass,
                                        access.resource.index,
                                        passIndex,
                                        accessIndex);
                validation.state = access.state;
                validation.queueClass = pass.queueClass;
                return validation;
            }

            for (uint32_t previousAccess = 0; previousAccess < accessIndex; ++previousAccess) {
                if (pass.accesses[previousAccess].resource.index == access.resource.index) {
                    FrameGraphDescriptorValidation validation =
                        MakeFrameGraphError(FrameGraphDescriptorError::DuplicateResourceAccess,
                                            access.resource.index,
                                            passIndex,
                                            accessIndex);
                    validation.conflictAccessIndex = previousAccess;
                    validation.state = access.state;
                    validation.queueClass = pass.queueClass;
                    return validation;
                }
            }
        }
    }

    return {};
}

FrameGraphCompileResult CompileFrameGraphTransitions(const FrameGraphDesc& graph) {
    FrameGraphCompileResult result;
    result.validation = ValidateFrameGraphDesc(graph);
    if (!result.validation) {
        return result;
    }

    result.resourceCount = graph.resourceCount;
    for (uint32_t resourceIndex = 0; resourceIndex < graph.resourceCount; ++resourceIndex) {
        result.resourceTypes[resourceIndex] = graph.resources[resourceIndex].type;
        result.finalResourceStates[resourceIndex] = graph.resources[resourceIndex].initialState;
    }

    result.passCount = graph.passCount;
    for (uint32_t passIndex = 0; passIndex < graph.passCount; ++passIndex) {
        const FrameGraphPassDesc& pass = graph.passes[passIndex];
        FrameGraphPassCompileInfo& passInfo = result.passes[passIndex];
        passInfo.dependencyOffset = result.dependencyCount;
        passInfo.dependencyCount = pass.dependencyCount;
        passInfo.transitionOffset = result.transitionCount;
        passInfo.accessOffset = result.accessCount;
        passInfo.accessCount = pass.accessCount;
        passInfo.queueClass = pass.queueClass;

        for (uint32_t dependencyIndex = 0; dependencyIndex < pass.dependencyCount; ++dependencyIndex) {
            FrameGraphCompiledPassDependency& compiledDependency =
                result.dependencies[result.dependencyCount++];
            compiledDependency.passIndex = passIndex;
            compiledDependency.dependencyIndex = dependencyIndex;
            compiledDependency.dependencyPassIndex = pass.dependencies[dependencyIndex].passIndex;
        }

        for (uint32_t accessIndex = 0; accessIndex < pass.accessCount; ++accessIndex) {
            const FrameGraphPassResourceAccess& access = pass.accesses[accessIndex];
            const uint32_t resourceIndex = access.resource.index;
            FrameGraphCompiledAccess& compiledAccess = result.accesses[result.accessCount++];
            compiledAccess.passIndex = passIndex;
            compiledAccess.accessIndex = accessIndex;
            compiledAccess.resourceIndex = resourceIndex;
            compiledAccess.state = access.state;
            compiledAccess.queueClass = pass.queueClass;
            compiledAccess.access = access.access;
            compiledAccess.shaderStages = access.shaderStages;

            const ResourceState before = result.finalResourceStates[resourceIndex];
            if (before == access.state) {
                continue;
            }
            if (result.transitionCount >= result.transitions.size()) {
                result.validation = MakeFrameGraphError(FrameGraphDescriptorError::TransitionCapacityExceeded,
                                                        resourceIndex,
                                                        passIndex,
                                                        accessIndex);
                result.validation.state = access.state;
                result.validation.queueClass = pass.queueClass;
                result.dependencyCount = 0;
                result.accessCount = 0;
                result.transitionCount = 0;
                result.passCount = 0;
                result.resourceCount = 0;
                result.resourceTypes = {};
                result.finalResourceStates = {};
                return result;
            }

            FrameGraphTransition& transition = result.transitions[result.transitionCount++];
            transition.passIndex = passIndex;
            transition.accessIndex = accessIndex;
            transition.resourceIndex = resourceIndex;
            transition.before = before;
            transition.after = access.state;
            transition.queueClass = pass.queueClass;
            transition.access = access.access;
            transition.shaderStages = access.shaderStages;
            result.finalResourceStates[resourceIndex] = access.state;
        }
        passInfo.transitionCount = result.transitionCount - passInfo.transitionOffset;
    }

    return result;
}

namespace {

void SetFrameGraphTransitionExecutionValidation(FrameGraphExecutionValidation& validation,
                                                FrameGraphExecutionError error,
                                                uint32_t transitionIndex,
                                                const FrameGraphTransition& transition) {
    validation.error = error;
    validation.transitionIndex = transitionIndex;
    validation.resourceIndex = transition.resourceIndex;
    validation.passIndex = transition.passIndex;
    validation.accessIndex = transition.accessIndex;
    validation.before = transition.before;
    validation.after = transition.after;
    validation.queueClass = transition.queueClass;
    validation.shaderStages = transition.shaderStages;
}

FrameGraphResourceTransitionPlan BuildFrameGraphResourceTransitionPlanRange(
    const FrameGraphCompileResult& compileResult,
    uint32_t transitionOffset,
    uint32_t transitionCount,
    const std::array<Resource*, kMaxFrameGraphResources>& resources) {
    FrameGraphResourceTransitionPlan plan;
    if (!compileResult || !compileResult.HasCompleteCompileTables()) {
        plan.validation.error = FrameGraphExecutionError::InvalidCompileResult;
        plan.validation.frameGraphValidation = compileResult.validation;
        return plan;
    }
    if (transitionOffset > compileResult.transitionCount ||
        transitionCount > compileResult.transitionCount - transitionOffset) {
        plan.validation.error = FrameGraphExecutionError::InvalidCompileResult;
        plan.validation.frameGraphValidation = compileResult.validation;
        return plan;
    }
    if (transitionCount > plan.transitions.size()) {
        plan.validation.error = FrameGraphExecutionError::TransitionCapacityExceeded;
        return plan;
    }

    for (uint32_t i = 0; i < transitionCount; ++i) {
        const uint32_t transitionIndex = transitionOffset + i;
        const FrameGraphTransition& frameGraphTransition = compileResult.transitions[transitionIndex];
        if (frameGraphTransition.resourceIndex >= resources.size() ||
            !resources[frameGraphTransition.resourceIndex]) {
            SetFrameGraphTransitionExecutionValidation(plan.validation,
                                                       FrameGraphExecutionError::MissingResourceBinding,
                                                       transitionIndex,
                                                       frameGraphTransition);
            return plan;
        }
        Resource* resource = resources[frameGraphTransition.resourceIndex];
        const ResourceType expectedResourceType =
            compileResult.GetResourceType(frameGraphTransition.resourceIndex);
        const ResourceType actualResourceType = resource->GetResourceType();
        if (actualResourceType != expectedResourceType) {
            SetFrameGraphTransitionExecutionValidation(plan.validation,
                                                       FrameGraphExecutionError::ResourceTypeMismatch,
                                                       transitionIndex,
                                                       frameGraphTransition);
            plan.validation.expectedResourceType = expectedResourceType;
            plan.validation.actualResourceType = actualResourceType;
            return plan;
        }

        ResourceTransition& transition = plan.transitions[plan.transitionCount++];
        transition.resource = resource;
        transition.before = frameGraphTransition.before;
        transition.after = frameGraphTransition.after;
        transition.queueClass = frameGraphTransition.queueClass;
        transition.shaderStages = frameGraphTransition.shaderStages;
    }

    return plan;
}

bool ApplyFrameGraphResourceTransitionPlanRange(const FrameGraphCompileResult& compileResult,
                                                uint32_t transitionOffset,
                                                const FrameGraphResourceTransitionPlan& plan,
                                                FrameGraphExecutionValidation* outValidation) {
    for (uint32_t i = 0; i < plan.transitionCount; ++i) {
        const ResourceTransition& transition = plan.transitions[i];
        const ResourceTransitionValidation transitionValidation = ValidateResourceTransition(transition);
        if (!transitionValidation) {
            const uint32_t transitionIndex = transitionOffset + i;
            const FrameGraphTransition& frameGraphTransition = compileResult.transitions[transitionIndex];
            FrameGraphExecutionValidation validation;
            SetFrameGraphTransitionExecutionValidation(validation,
                                                       FrameGraphExecutionError::InvalidResourceTransition,
                                                       transitionIndex,
                                                       frameGraphTransition);
            validation.resourceTransitionValidation = transitionValidation;
            if (outValidation) {
                *outValidation = validation;
            }
            return false;
        }

        transition.resource->SetCurrentState(transition.after);
    }

    return true;
}

} // namespace

FrameGraphResourceTransitionPlan BuildFrameGraphResourceTransitionPlan(
    const FrameGraphCompileResult& compileResult,
    const std::array<Resource*, kMaxFrameGraphResources>& resources) {
    return BuildFrameGraphResourceTransitionPlanRange(
        compileResult,
        0,
        compileResult.transitionCount,
        resources);
}

FrameGraphResourceTransitionPlan BuildFrameGraphPassResourceTransitionPlan(
    const FrameGraphCompileResult& compileResult,
    uint32_t passIndex,
    const std::array<Resource*, kMaxFrameGraphResources>& resources) {
    FrameGraphResourceTransitionPlan plan;
    if (!compileResult || !compileResult.HasCompleteCompileTables()) {
        plan.validation.error = FrameGraphExecutionError::InvalidCompileResult;
        plan.validation.frameGraphValidation = compileResult.validation;
        return plan;
    }

    const FrameGraphPassCompileInfo* passInfo = compileResult.GetPass(passIndex);
    if (!passInfo) {
        plan.validation.error = FrameGraphExecutionError::InvalidPassIndex;
        plan.validation.passIndex = passIndex;
        return plan;
    }

    return BuildFrameGraphResourceTransitionPlanRange(
        compileResult,
        passInfo->transitionOffset,
        passInfo->transitionCount,
        resources);
}

bool ApplyFrameGraphResourceTransitionPlan(
    const FrameGraphCompileResult& compileResult,
    const std::array<Resource*, kMaxFrameGraphResources>& resources,
    FrameGraphExecutionValidation* outValidation,
    uint32_t* outTransitionCount) {
    if (outValidation) {
        *outValidation = {};
    }
    if (outTransitionCount) {
        *outTransitionCount = 0;
    }

    const FrameGraphResourceTransitionPlan plan =
        BuildFrameGraphResourceTransitionPlan(compileResult, resources);
    if (outTransitionCount) {
        *outTransitionCount = plan.transitionCount;
    }
    if (!plan) {
        if (outValidation) {
            *outValidation = plan.validation;
        }
        return false;
    }

    return ApplyFrameGraphResourceTransitionPlanRange(compileResult, 0, plan, outValidation);
}

bool ApplyFrameGraphPassResourceTransitionPlan(
    const FrameGraphCompileResult& compileResult,
    uint32_t passIndex,
    const std::array<Resource*, kMaxFrameGraphResources>& resources,
    FrameGraphExecutionValidation* outValidation,
    uint32_t* outTransitionCount) {
    if (outValidation) {
        *outValidation = {};
    }
    if (outTransitionCount) {
        *outTransitionCount = 0;
    }

    const FrameGraphResourceTransitionPlan plan =
        BuildFrameGraphPassResourceTransitionPlan(compileResult, passIndex, resources);
    if (outTransitionCount) {
        *outTransitionCount = plan.transitionCount;
    }
    if (!plan) {
        if (outValidation) {
            *outValidation = plan.validation;
        }
        return false;
    }

    const FrameGraphPassCompileInfo* passInfo = compileResult.GetPass(passIndex);
    if (!passInfo) {
        if (outValidation) {
            outValidation->error = FrameGraphExecutionError::InvalidPassIndex;
            outValidation->passIndex = passIndex;
        }
        return false;
    }

    return ApplyFrameGraphResourceTransitionPlanRange(
        compileResult,
        passInfo->transitionOffset,
        plan,
        outValidation);
}

} // namespace RHI
} // namespace Next
