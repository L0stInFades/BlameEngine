#pragma once

#include <cstdint>

namespace Next {
namespace RHI {

enum class Backend : uint8_t {
    Null = 0,
    DX12,
    Metal,
};

enum class QueueClass : uint8_t {
    Graphics = 0,
    Compute,
    Copy,
};

using QueueClassFlags = uint8_t;

constexpr QueueClassFlags QueueClassFlag(QueueClass queueClass) {
    switch (queueClass) {
        case QueueClass::Graphics: return 1u << 0;
        case QueueClass::Compute:  return 1u << 1;
        case QueueClass::Copy:     return 1u << 2;
        default:                   return 0;
    }
}

constexpr QueueClassFlags operator|(QueueClass lhs, QueueClass rhs) {
    return QueueClassFlag(lhs) | QueueClassFlag(rhs);
}

constexpr QueueClassFlags operator|(QueueClassFlags lhs, QueueClass rhs) {
    return lhs | QueueClassFlag(rhs);
}

constexpr bool HasQueueClass(QueueClassFlags flags, QueueClass queueClass) {
    return (flags & QueueClassFlag(queueClass)) != 0;
}

enum class Format : uint16_t {
    Unknown = 0,
    BGRA8Unorm,
    RGBA8Unorm,
    Depth32Float,
    Depth32FloatStencil8,
};

enum class ResourceType : uint8_t {
    Unknown = 0,
    Buffer,
    Texture,
    Sampler,
    Pipeline,
};

enum class ResourceMemory : uint8_t {
    DeviceLocal = 0,
    Shared,
    Upload,
    Readback,
};

enum class SamplerFilter : uint8_t {
    Nearest = 0,
    Linear,
};

enum class SamplerMipFilter : uint8_t {
    NotMipmapped = 0,
    Nearest,
    Linear,
};

enum class SamplerAddressMode : uint8_t {
    Repeat = 0,
    ClampToEdge,
    MirrorRepeat,
    ClampToBorder,
};

enum class SamplerBorderColor : uint8_t {
    OpaqueBlack = 0,
    TransparentBlack,
    OpaqueWhite,
};

enum class CompareFunction : uint8_t {
    Never = 0,
    Less,
    Equal,
    LessEqual,
    Greater,
    NotEqual,
    GreaterEqual,
    Always,
};

enum class StencilOperation : uint8_t {
    Keep = 0,
    Zero,
    Replace,
    IncrementClamp,
    DecrementClamp,
    Invert,
    IncrementWrap,
    DecrementWrap,
};

enum class BlendFactor : uint8_t {
    Zero = 0,
    One,
    SourceColor,
    OneMinusSourceColor,
    SourceAlpha,
    OneMinusSourceAlpha,
    DestinationColor,
    OneMinusDestinationColor,
    DestinationAlpha,
    OneMinusDestinationAlpha,
    SourceAlphaSaturated,
    BlendColor,
    OneMinusBlendColor,
    BlendAlpha,
    OneMinusBlendAlpha,
};

enum class BlendOperation : uint8_t {
    Add = 0,
    Subtract,
    ReverseSubtract,
    Min,
    Max,
};

enum class ColorWriteMask : uint8_t {
    None = 0,
    Red = 1u << 0,
    Green = 1u << 1,
    Blue = 1u << 2,
    Alpha = 1u << 3,
    All = 0x0f,
};

enum class VertexFormat : uint8_t {
    Unknown = 0,
    Float32,
    Float32x2,
    Float32x3,
    Float32x4,
};

enum class VertexStepFunction : uint8_t {
    PerVertex = 0,
    PerInstance,
};

enum class IndexFormat : uint8_t {
    Unknown = 0,
    Uint16,
    Uint32,
};

using ColorWriteMaskFlags = uint8_t;

constexpr ColorWriteMaskFlags ColorWriteMaskFlag(ColorWriteMask mask) {
    return static_cast<ColorWriteMaskFlags>(mask);
}

constexpr ColorWriteMaskFlags operator|(ColorWriteMask lhs, ColorWriteMask rhs) {
    return ColorWriteMaskFlag(lhs) | ColorWriteMaskFlag(rhs);
}

constexpr ColorWriteMaskFlags operator|(ColorWriteMaskFlags lhs, ColorWriteMask rhs) {
    return lhs | ColorWriteMaskFlag(rhs);
}

constexpr bool HasColorWriteMask(ColorWriteMaskFlags flags, ColorWriteMask mask) {
    return (flags & ColorWriteMaskFlag(mask)) != 0;
}

enum class ShaderStage : uint8_t {
    Vertex = 0,
    Fragment,
    Compute,
};

using ShaderStageFlags = uint8_t;

constexpr ShaderStageFlags ShaderStageFlag(ShaderStage stage) {
    switch (stage) {
        case ShaderStage::Vertex:   return 1u << 0;
        case ShaderStage::Fragment: return 1u << 1;
        case ShaderStage::Compute:  return 1u << 2;
        default:                    return 0;
    }
}

constexpr ShaderStageFlags operator|(ShaderStage lhs, ShaderStage rhs) {
    return ShaderStageFlag(lhs) | ShaderStageFlag(rhs);
}

constexpr ShaderStageFlags operator|(ShaderStageFlags lhs, ShaderStage rhs) {
    return lhs | ShaderStageFlag(rhs);
}

constexpr bool HasShaderStage(ShaderStageFlags flags, ShaderStage stage) {
    return (flags & ShaderStageFlag(stage)) != 0;
}

enum class PrimitiveTopology : uint8_t {
    TriangleList = 0,
    TriangleStrip,
    LineList,
    PointList,
};

enum class FillMode : uint8_t {
    Solid = 0,
    Wireframe,
};

enum class CullMode : uint8_t {
    None = 0,
    Front,
    Back,
};

enum class FrontFaceWinding : uint8_t {
    CounterClockwise = 0,
    Clockwise,
};

enum class ResourceState : uint8_t {
    Undefined = 0,
    Common,
    CopySource,
    CopyDestination,
    VertexBuffer,
    IndexBuffer,
    ConstantBuffer,
    RenderTarget,
    DepthWrite,
    ShaderRead,
    ShaderWrite,
    Present,
};

enum class ResourceUsage : uint32_t {
    None = 0,
    VertexBuffer = 1u << 0,
    IndexBuffer = 1u << 1,
    ConstantBuffer = 1u << 2,
    ShaderRead = 1u << 3,
    ShaderWrite = 1u << 4,
    RenderTarget = 1u << 5,
    DepthStencil = 1u << 6,
    CopySource = 1u << 7,
    CopyDestination = 1u << 8,
    Present = 1u << 9,
};

using ResourceUsageFlags = uint32_t;

constexpr ResourceUsageFlags ResourceUsageFlag(ResourceUsage usage) {
    return static_cast<ResourceUsageFlags>(usage);
}

constexpr ResourceUsageFlags operator|(ResourceUsage lhs, ResourceUsage rhs) {
    return ResourceUsageFlag(lhs) | ResourceUsageFlag(rhs);
}

constexpr ResourceUsageFlags operator|(ResourceUsageFlags lhs, ResourceUsage rhs) {
    return lhs | ResourceUsageFlag(rhs);
}

constexpr bool HasResourceUsage(ResourceUsageFlags flags, ResourceUsage usage) {
    return (flags & ResourceUsageFlag(usage)) != 0;
}

struct Extent2D {
    uint32_t width = 0;
    uint32_t height = 0;
};

struct ClearColor {
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
    double a = 1.0;
};

const char* BackendName(Backend backend);
const char* QueueClassName(QueueClass queueClass);
const char* FormatName(Format format);
uint32_t FormatBytesPerPixel(Format format);
bool FormatSupportsColorAttachment(Format format);
bool FormatSupportsDepthStencil(Format format);
bool FormatHasDepth(Format format);
bool FormatHasStencil(Format format);
const char* ResourceTypeName(ResourceType type);
const char* ResourceMemoryName(ResourceMemory memory);
const char* SamplerFilterName(SamplerFilter filter);
const char* SamplerMipFilterName(SamplerMipFilter filter);
const char* SamplerAddressModeName(SamplerAddressMode addressMode);
const char* SamplerBorderColorName(SamplerBorderColor color);
const char* CompareFunctionName(CompareFunction function);
const char* StencilOperationName(StencilOperation operation);
const char* BlendFactorName(BlendFactor factor);
const char* BlendOperationName(BlendOperation operation);
const char* ColorWriteMaskName(ColorWriteMask mask);
const char* VertexFormatName(VertexFormat format);
const char* VertexStepFunctionName(VertexStepFunction stepFunction);
const char* IndexFormatName(IndexFormat format);
uint32_t IndexFormatByteSize(IndexFormat format);
const char* ShaderStageName(ShaderStage stage);
const char* PrimitiveTopologyName(PrimitiveTopology topology);
const char* FillModeName(FillMode mode);
const char* CullModeName(CullMode mode);
const char* FrontFaceWindingName(FrontFaceWinding winding);
const char* ResourceStateName(ResourceState state);
const char* ResourceUsageName(ResourceUsage usage);

} // namespace RHI
} // namespace Next
