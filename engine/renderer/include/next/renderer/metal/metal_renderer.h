#pragma once

#include "next/renderer/renderer.h"

namespace Next {

class MetalRenderer final : public Renderer {
public:
    MetalRenderer();
    ~MetalRenderer() override;

    bool Initialize(Window* window) override;
    void Shutdown() override;

    const char* GetBackendName() const override { return "metal"; }
    RendererDeviceInfo GetDeviceInfo() const override;
    RendererLifetimeStats GetLifetimeStats() const override;
    RendererCommandStats GetCommandStats() const override;
    RendererRenderPassStats GetRenderPassStats() const override;
    RendererUploadQueueStats GetUploadQueueStats() const override;
    RendererPipelineStats GetPipelineStats() const override;
    RendererGeometryStats GetGeometryStats() const override;
    RendererDrawStateStats GetDrawStateStats() const override;
    RendererDrawSubmissionStats GetDrawSubmissionStats() const override;
    RendererDrawItemStats GetDrawItemStats() const override;
    RendererSwapchainStats GetSwapchainStats() const override;
    RendererSamplerStats GetSamplerStats() const override;
    RendererMaterialStats GetMaterialStats() const override;
    RendererResourceStateStats GetResourceStateStats() const override;

    void SetFrameDesc(const RendererFrameDesc& frame) override;
    RendererTextureUploadHandle UploadTexture2D(const RendererTextureUploadDesc& texture) override;
    RendererTextureUploadStats GetTextureUploadStats() override;
    RendererResourcePoolStats GetResourcePoolStats() override;
    bool SetResourcePoolBudget(const RendererResourcePoolBudgetDesc& budget) override;
    RendererTextureUploadStatus GetTextureUploadStatus(RendererTextureUploadHandle handle) override;
    bool GetTextureInfo(RendererTextureHandle texture, RendererTextureInfo& outInfo) override;
    RendererMaterialHandle CreateMaterial(const RendererMaterialDesc& material) override;
    bool UpdateMaterial(RendererMaterialHandle handle, const RendererMaterialDesc& material) override;
    bool SetActiveMaterial(RendererMaterialHandle handle) override;
    bool GetMaterialInfo(RendererMaterialHandle handle, RendererMaterialInfo& outInfo) override;
    void BeginFrame() override;
    void EndFrame() override;
    void Render() override;
    void Resize(int width, int height) override;

private:
    struct Impl;

    Impl* impl_;
    Window* window_;
    int width_;
    int height_;
    bool initialized_;
    bool frameActive_;
    float time_;
    RendererFrameDesc frameDesc_;
};

} // namespace Next
