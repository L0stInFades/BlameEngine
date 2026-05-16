#include "next/renderer/renderer.h"
#include "next/foundation/logger.h"
#include "next/platform/window.h"
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <sstream>

#ifdef _WIN32
#include "next/renderer/dx12/dx12_renderer.h"
#endif
#ifdef __APPLE__
#include "next/renderer/metal/metal_renderer.h"
#endif

namespace {

#ifdef _WIN32
class NullRenderer : public Next::Renderer {
public:
    explicit NullRenderer(const char* reason) : reason_(reason ? reason : "No renderer backend available") {}

    const char* GetBackendName() const override { return "null"; }

    bool Initialize(Next::Window*) override {
        NEXT_LOG_ERROR("%s", reason_);
        return false;
    }

    void Shutdown() override {}
    void BeginFrame() override {}
    void EndFrame() override {}
    void Render() override {}
    void Resize(int, int) override {}

private:
    const char* reason_;
};
#else
class NullRenderer : public Next::Renderer {
public:
    explicit NullRenderer(const char* reason) : reason_(reason ? reason : "No renderer implementation available for this platform") {}

    const char* GetBackendName() const override { return "null"; }

    bool Initialize(Next::Window*) override {
        NEXT_LOG_ERROR("%s", reason_);
        return false;
    }

    void Shutdown() override {}
    void BeginFrame() override {}
    void EndFrame() override {}
    void Render() override {}
    void Resize(int, int) override {}

private:
    const char* reason_;
};
#endif

} // namespace

namespace {

bool EqualsIgnoreCase(const char* a, const char* b) {
    while (*a && *b) {
        if (std::tolower(static_cast<unsigned char>(*a)) != std::tolower(static_cast<unsigned char>(*b))) {
            return false;
        }
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

const char* RendererBoolName(bool value) {
    return value ? "true" : "false";
}

} // namespace

namespace Next {

std::string RendererRenderPassStats::BuildLogSummary() const {
    const RendererRenderPassColorAttachmentInfo* firstColorAttachment = GetColorAttachment(0);
    const RendererRenderPassDepthStencilAttachmentInfo& depthStencil = depthStencilAttachment;

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);
    oss << "Renderer render pass stats: ready=" << RendererBoolName(ready)
        << " name=\"" << GetDebugName() << "\""
        << " descriptor=" << RHI::RenderPassDescriptorErrorName(descriptorError)
        << " descriptorAttachment=" << descriptorErrorAttachmentIndex
        << " descriptorFormat=" << RHI::FormatName(descriptorErrorFormat)
        << " frameGraph=" << RHI::FrameGraphDescriptorErrorName(frameGraphValidation.error)
        << " transitions=" << frameGraphTransitionCount
        << " colors=" << colorAttachmentCount
        << " colorCapacity=" << static_cast<unsigned int>(colorAttachments.size())
        << " depthStencil=" << RendererBoolName(hasDepthStencil)
        << " firstColor=" << RendererBoolName(firstColorAttachment != nullptr) << "/"
        << RHI::FormatName(firstColorAttachment ? firstColorAttachment->format
                                                : RHI::Format::Unknown) << "/"
        << RHI::AttachmentLoadActionName(firstColorAttachment ? firstColorAttachment->loadAction
                                                              : RHI::AttachmentLoadAction::DontCare) << "/"
        << RHI::AttachmentStoreActionName(firstColorAttachment ? firstColorAttachment->storeAction
                                                               : RHI::AttachmentStoreAction::DontCare)
        << "/clear=" << (firstColorAttachment ? firstColorAttachment->clearColor.r : 0.0)
        << "," << (firstColorAttachment ? firstColorAttachment->clearColor.g : 0.0)
        << "," << (firstColorAttachment ? firstColorAttachment->clearColor.b : 0.0)
        << "," << (firstColorAttachment ? firstColorAttachment->clearColor.a : 0.0)
        << "/resolve=" << RendererBoolName(firstColorAttachment && firstColorAttachment->resolve)
        << " depth=" << RendererBoolName(depthStencil.active) << "/"
        << RHI::FormatName(depthStencil.format) << "/"
        << RHI::AttachmentLoadActionName(depthStencil.loadAction) << "/"
        << RHI::AttachmentStoreActionName(depthStencil.storeAction)
        << "/clear=" << depthStencil.clearDepth
        << " stencil=" << RHI::AttachmentLoadActionName(depthStencil.stencilLoadAction) << "/"
        << RHI::AttachmentStoreActionName(depthStencil.stencilStoreAction)
        << "/clear=" << depthStencil.clearStencil
        << " resolve=" << RendererBoolName(depthStencil.resolve)
        << " hasName=" << RendererBoolName(HasDebugName())
        << " hasValidDescriptor=" << RendererBoolName(HasValidDescriptor())
        << " hasFrameGraph=" << RendererBoolName(HasValidFrameGraphPassPlan())
        << " hasFrameGraphTransitions=" << RendererBoolName(HasFrameGraphTransitions())
        << " hasColors=" << RendererBoolName(HasColorAttachments())
        << " hasMultipleColors=" << RendererBoolName(HasMultipleColorAttachments())
        << " completeColors=" << RendererBoolName(HasCompleteColorAttachmentTable())
        << " hasDepthStencil=" << RendererBoolName(HasDepthStencilAttachment())
        << " hasAnyClear=" << RendererBoolName(HasAnyClear())
        << " hasAnyStore=" << RendererBoolName(HasAnyStore())
        << " hasAnyResolve=" << RendererBoolName(HasAnyResolve())
        << " hasReadyPass=" << RendererBoolName(HasReadyRenderPass());
    return oss.str();
}

RendererBackend Renderer::ParseBackend(const char* name) {
    if (!name || !*name) {
        return RendererBackend::Auto;
    }

    if (EqualsIgnoreCase(name, "dx12")) {
        return RendererBackend::DX12;
    }
    if (EqualsIgnoreCase(name, "metal")) {
        return RendererBackend::Metal;
    }
    if (EqualsIgnoreCase(name, "null")) {
        return RendererBackend::Null;
    }
    if (EqualsIgnoreCase(name, "auto")) {
        return RendererBackend::Auto;
    }
    return RendererBackend::Auto;
}

const char* Renderer::BackendToString(RendererBackend backend) {
    switch (backend) {
        case RendererBackend::DX12:
            return "dx12";
        case RendererBackend::Metal:
            return "metal";
        case RendererBackend::Null:
            return "null";
        case RendererBackend::Auto:
        default:
            return "auto";
    }
}

Renderer* Renderer::Create(RendererBackend preferredBackend) {
    RendererBackend backend = preferredBackend;

    if (backend == RendererBackend::Auto) {
        if (const char* envBackend = std::getenv("NEXT_RENDERER_BACKEND")) {
            backend = ParseBackend(envBackend);
            if (backend != RendererBackend::Auto) {
                NEXT_LOG_INFO("Renderer backend override from NEXT_RENDERER_BACKEND=%s", envBackend);
            }
        }
    }

    if (backend == RendererBackend::Auto) {
#ifdef _WIN32
        backend = RendererBackend::DX12;
#elif defined(__APPLE__)
        backend = RendererBackend::Metal;
#else
        backend = RendererBackend::Null;
#endif
    }

    switch (backend) {
        case RendererBackend::DX12:
#ifdef _WIN32
            return new DX12Renderer();
#else
            NEXT_LOG_ERROR("DX12 backend requested but not supported on this platform");
            return new NullRenderer("DX12 requested on unsupported platform");
#endif
        case RendererBackend::Metal:
#ifdef __APPLE__
            return new MetalRenderer();
#else
            NEXT_LOG_ERROR("Metal backend requested but this platform is not macOS");
            return new NullRenderer("Metal requested on unsupported platform");
#endif
        case RendererBackend::Null:
            return new NullRenderer("Null renderer selected");
        case RendererBackend::Auto:
        default:
            return new NullRenderer("No renderer backend available");
    }
}

} // namespace Next
