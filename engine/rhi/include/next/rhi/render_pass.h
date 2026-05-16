#pragma once

#include "next/rhi/types.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace Next {
namespace RHI {

static constexpr size_t kMaxRenderPassColorAttachments = 4;

enum class AttachmentLoadAction : uint8_t {
    Load = 0,
    Clear,
    DontCare,
};

enum class AttachmentStoreAction : uint8_t {
    Store = 0,
    DontCare,
};

struct RenderPassColorAttachmentDesc {
    Format format = Format::Unknown;
    AttachmentLoadAction loadAction = AttachmentLoadAction::Clear;
    AttachmentStoreAction storeAction = AttachmentStoreAction::Store;
    ClearColor clearColor;
};

struct RenderPassDepthStencilAttachmentDesc {
    Format format = Format::Unknown;
    AttachmentLoadAction loadAction = AttachmentLoadAction::Clear;
    AttachmentStoreAction storeAction = AttachmentStoreAction::DontCare;
    double clearDepth = 1.0;
    AttachmentLoadAction stencilLoadAction = AttachmentLoadAction::DontCare;
    AttachmentStoreAction stencilStoreAction = AttachmentStoreAction::DontCare;
    uint32_t clearStencil = 0;
};

struct RenderPassDesc {
    const char* debugName = nullptr;
    std::array<RenderPassColorAttachmentDesc, kMaxRenderPassColorAttachments> colorAttachments{};
    uint32_t colorAttachmentCount = 0;
    bool hasDepthStencil = false;
    RenderPassDepthStencilAttachmentDesc depthStencilAttachment;
};

enum class RenderPassDescriptorError : uint8_t {
    None = 0,
    MissingAttachment,
    TooManyColorAttachments,
    UnsupportedColorFormat,
    UnsupportedDepthFormat,
    InvalidLoadAction,
    InvalidStoreAction,
    InvalidDepthClearValue,
    InvalidStencilClearValue,
};

struct RenderPassDescriptorValidation {
    RenderPassDescriptorError error = RenderPassDescriptorError::None;
    uint32_t attachmentIndex = 0;
    Format format = Format::Unknown;

    explicit operator bool() const { return error == RenderPassDescriptorError::None; }
};

const char* AttachmentLoadActionName(AttachmentLoadAction action);
const char* AttachmentStoreActionName(AttachmentStoreAction action);
const char* RenderPassDescriptorErrorName(RenderPassDescriptorError error);
RenderPassDescriptorValidation ValidateRenderPassDesc(const RenderPassDesc& desc);

} // namespace RHI
} // namespace Next
