#pragma once

#include "next/rhi/types.h"

#include <array>
#include <cstdint>

namespace Next {
namespace RHI {

class CommandContext {
public:
    virtual ~CommandContext() = default;

    virtual QueueClass GetQueueClass() const = 0;
    virtual bool IsRecording() const = 0;
    virtual uint64_t GetSubmittedFrameIndex() const = 0;
};

struct IndexBufferViewDesc {
    IndexFormat format = IndexFormat::Unknown;
    uint64_t byteOffset = 0;
};

struct DrawIndexedDesc {
    uint32_t indexCount = 0;
    uint32_t instanceCount = 1;
    uint32_t indexOffset = 0;
    int32_t vertexOffset = 0;
    uint32_t instanceOffset = 0;
    uint8_t stencilReference = 0;
    std::array<float, 4> blendConstant{0.0f, 0.0f, 0.0f, 0.0f};
};

enum class DrawDescriptorError : uint8_t {
    None = 0,
    MissingIndexCount,
    MissingInstanceCount,
    InvalidIndexFormat,
    MisalignedIndexBufferOffset,
    IndexBufferOffsetOverflow,
    InvalidBlendConstant,
};

struct DrawIndexedValidation {
    DrawDescriptorError error = DrawDescriptorError::None;
    IndexFormat indexFormat = IndexFormat::Unknown;

    explicit operator bool() const { return error == DrawDescriptorError::None; }
};

struct ViewportDesc {
    double minX = 0.0;
    double minY = 0.0;
    double maxX = 0.0;
    double maxY = 0.0;
    double minZ = 0.0;
    double maxZ = 1.0;
};

struct ScissorRectDesc {
    uint32_t minX = 0;
    uint32_t minY = 0;
    uint32_t maxX = 0;
    uint32_t maxY = 0;
};

enum class ViewportDescriptorError : uint8_t {
    None = 0,
    EmptyViewport,
    InvalidDepthRange,
};

enum class ScissorDescriptorError : uint8_t {
    None = 0,
    EmptyScissor,
};

struct ViewportDescriptorValidation {
    ViewportDescriptorError error = ViewportDescriptorError::None;

    explicit operator bool() const { return error == ViewportDescriptorError::None; }
};

struct ScissorDescriptorValidation {
    ScissorDescriptorError error = ScissorDescriptorError::None;

    explicit operator bool() const { return error == ScissorDescriptorError::None; }
};

const char* DrawDescriptorErrorName(DrawDescriptorError error);
const char* ViewportDescriptorErrorName(ViewportDescriptorError error);
const char* ScissorDescriptorErrorName(ScissorDescriptorError error);
DrawIndexedValidation ValidateDrawIndexedDesc(const DrawIndexedDesc& draw,
                                              const IndexBufferViewDesc& indexBuffer);
bool DrawIndexedIndexBufferOffsetBytes(const DrawIndexedDesc& draw,
                                       const IndexBufferViewDesc& indexBuffer,
                                       uint64_t& outByteOffset);
ViewportDescriptorValidation ValidateViewportDesc(const ViewportDesc& viewport);
ScissorDescriptorValidation ValidateScissorRectDesc(const ScissorRectDesc& scissor);

} // namespace RHI
} // namespace Next
