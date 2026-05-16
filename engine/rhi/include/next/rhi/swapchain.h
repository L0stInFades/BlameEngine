#pragma once

#include "next/rhi/types.h"

namespace Next {
namespace RHI {

struct SwapchainDesc {
    void* nativeWindow = nullptr;
    Extent2D drawableSize;
    Format colorFormat = Format::BGRA8Unorm;
    Format depthFormat = Format::Depth32Float;
    bool framebufferOnly = true;
    bool vsync = true;
};

enum class SwapchainDescriptorError : uint8_t {
    None = 0,
    EmptyDrawableSize,
    UnsupportedColorFormat,
    UnsupportedDepthFormat,
};

struct SwapchainDescriptorValidation {
    SwapchainDescriptorError error = SwapchainDescriptorError::None;
    Format format = Format::Unknown;

    explicit operator bool() const { return error == SwapchainDescriptorError::None; }
};

class Swapchain {
public:
    virtual ~Swapchain() = default;

    virtual Extent2D GetDrawableSize() const = 0;
    virtual Format GetColorFormat() const = 0;
    virtual Format GetDepthFormat() const = 0;
};

const char* SwapchainDescriptorErrorName(SwapchainDescriptorError error);
SwapchainDescriptorValidation ValidateSwapchainDesc(const SwapchainDesc& desc);

} // namespace RHI
} // namespace Next
