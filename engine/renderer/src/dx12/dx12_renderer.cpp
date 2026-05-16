#include "next/renderer/dx12/dx12_renderer.h"
#include "next/foundation/logger.h"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <system_error>
#include <vector>

namespace Next {

namespace {
constexpr UINT kPbrTextureSlots = 5;
constexpr float kDescriptorHeapUsageWarnThreshold = 85.0f;

struct DebugCellVertex {
    float position[3];
    float color[4];
    float texcoord[2];
};

struct CubeConstants {
    Mat4 model;
    Mat4 view;
    Mat4 projection;
    float time;
    float padding[3];
};

struct CameraDataGPU {
    float position[3];
    float padding;
};

struct LightingSettingsGPU {
    float ambientColor[3];
    float ambientIntensity;
    float exposure;
    float gamma;
    int toneMapMode;
    int debugViewMode;
};

struct DirectionalLightGPU {
    float direction[3];
    float intensity;
    float color[3];
    int enabled;
};

struct PointLightGPU {
    float position[3];
    float intensity;
    float color[3];
    float radius;
    float constant;
    float linearAttenuation;
    float quadratic;
    int enabled;
};

struct LightingBufferGPU {
    CameraDataGPU camera;
    LightingSettingsGPU settings;
    DirectionalLightGPU directionalLight;
    PointLightGPU pointLights[LightingScene::MAX_POINT_LIGHTS];
    int numPointLights;
    int numSpotLights;
    int padding[2];
};

std::filesystem::path GetExecutableDirectory() {
    wchar_t path[MAX_PATH] = {};
    DWORD len = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        return {};
    }

    return std::filesystem::path(path).parent_path();
}

void PushUniquePath(std::vector<std::filesystem::path>& roots, const std::filesystem::path& path) {
    if (path.empty()) {
        return;
    }

    for (const auto& existing : roots) {
        if (existing == path) {
            return;
        }
    }
    roots.push_back(path);
}

std::filesystem::path ResolveRuntimeAssetPath(const std::filesystem::path& relativePath) {
    if (relativePath.empty() || relativePath.is_absolute()) {
        return relativePath;
    }

    std::vector<std::filesystem::path> roots;
    std::error_code ec;

    PushUniquePath(roots, std::filesystem::current_path(ec));
    std::filesystem::path exeDir = GetExecutableDirectory();
    std::filesystem::path probe = exeDir;
    for (int i = 0; i < 6 && !probe.empty(); ++i) {
        PushUniquePath(roots, probe);
        std::filesystem::path parent = probe.parent_path();
        if (parent == probe) {
            break;
        }
        probe = parent;
    }

    for (const auto& root : roots) {
        const std::filesystem::path candidate = root / relativePath;
        if (std::filesystem::exists(candidate, ec)) {
            const std::filesystem::path absoluteCandidate = std::filesystem::absolute(candidate, ec);
            return ec ? candidate : absoluteCandidate;
        }
    }

    return relativePath;
}

std::string ResolveRuntimeAssetPathUtf8(const char* relativePath) {
    return ResolveRuntimeAssetPath(std::filesystem::path(relativePath)).u8string();
}

std::wstring ResolveRuntimeAssetPathWide(const wchar_t* relativePath) {
    return ResolveRuntimeAssetPath(std::filesystem::path(relativePath)).wstring();
}

bool EqualsIgnoreCaseAscii(const char* a, const char* b) {
    if (!a || !b) {
        return false;
    }

    while (*a && *b) {
        char ca = *a;
        char cb = *b;
        if (ca >= 'A' && ca <= 'Z') {
            ca = static_cast<char>(ca - 'A' + 'a');
        }
        if (cb >= 'A' && cb <= 'Z') {
            cb = static_cast<char>(cb - 'A' + 'a');
        }
        if (ca != cb) {
            return false;
        }
        ++a;
        ++b;
    }

    return *a == '\0' && *b == '\0';
}

DebugViewMode ParseDebugViewMode(const char* value) {
    if (!value || !*value || EqualsIgnoreCaseAscii(value, "default")) {
        return DebugViewMode::Default;
    }
    if (EqualsIgnoreCaseAscii(value, "wireframe")) {
        return DebugViewMode::Wireframe;
    }
    if (EqualsIgnoreCaseAscii(value, "normals")) {
        return DebugViewMode::Normals;
    }
    if (EqualsIgnoreCaseAscii(value, "tangents")) {
        return DebugViewMode::Tangents;
    }
    if (EqualsIgnoreCaseAscii(value, "bitangents")) {
        return DebugViewMode::Bitangents;
    }
    if (EqualsIgnoreCaseAscii(value, "depth")) {
        return DebugViewMode::Depth;
    }
    if (EqualsIgnoreCaseAscii(value, "roughness")) {
        return DebugViewMode::Roughness;
    }
    if (EqualsIgnoreCaseAscii(value, "metallic")) {
        return DebugViewMode::Metallic;
    }
    if (EqualsIgnoreCaseAscii(value, "albedo")) {
        return DebugViewMode::Albedo;
    }
    if (EqualsIgnoreCaseAscii(value, "ao")) {
        return DebugViewMode::AO;
    }
    if (EqualsIgnoreCaseAscii(value, "motionvectors")) {
        return DebugViewMode::MotionVectors;
    }
    if (EqualsIgnoreCaseAscii(value, "uv")) {
        return DebugViewMode::UV;
    }
    if (EqualsIgnoreCaseAscii(value, "triangles") || EqualsIgnoreCaseAscii(value, "trianglecount")) {
        return DebugViewMode::TriangleCount;
    }
    if (EqualsIgnoreCaseAscii(value, "heatmap") || EqualsIgnoreCaseAscii(value, "heat")) {
        return DebugViewMode::Heatmap;
    }
    return DebugViewMode::Default;
}

bool ParseAOTypeEnv(const char* value, AOType& outType) {
    if (!value || !*value || EqualsIgnoreCaseAscii(value, "default") || EqualsIgnoreCaseAscii(value, "gtao")) {
        outType = AOType::GTAO;
        return true;
    }
    if (EqualsIgnoreCaseAscii(value, "hbao")) {
        outType = AOType::HBAO;
        return true;
    }
    if (EqualsIgnoreCaseAscii(value, "vxao")) {
        outType = AOType::VXAO;
        return true;
    }
    if (EqualsIgnoreCaseAscii(value, "none") || EqualsIgnoreCaseAscii(value, "off") || EqualsIgnoreCaseAscii(value, "disabled")) {
        outType = AOType::None;
        return true;
    }

    NEXT_LOG_WARNING("Unknown NEXT_AO_TECHNIQUE=%s; keeping default AO technique", value);
    return false;
}

bool ParseGITechniqueEnv(const char* value, GITechnique& outTechnique) {
    if (!value || !*value || EqualsIgnoreCaseAscii(value, "default") || EqualsIgnoreCaseAscii(value, "hybrid")) {
        outTechnique = GITechnique::Hybrid;
        return true;
    }
    if (EqualsIgnoreCaseAscii(value, "lightprobes") || EqualsIgnoreCaseAscii(value, "probes")) {
        outTechnique = GITechnique::LightProbes;
        return true;
    }
    if (EqualsIgnoreCaseAscii(value, "screenspacegi") ||
        EqualsIgnoreCaseAscii(value, "screen_space_gi") ||
        EqualsIgnoreCaseAscii(value, "ssgi")) {
        outTechnique = GITechnique::ScreenSpaceGI;
        return true;
    }
    if (EqualsIgnoreCaseAscii(value, "voxelgi") || EqualsIgnoreCaseAscii(value, "vxgi") || EqualsIgnoreCaseAscii(value, "voxel")) {
        outTechnique = GITechnique::VoxelGI;
        return true;
    }
    if (EqualsIgnoreCaseAscii(value, "none") || EqualsIgnoreCaseAscii(value, "off") || EqualsIgnoreCaseAscii(value, "disabled")) {
        outTechnique = GITechnique::None;
        return true;
    }

    NEXT_LOG_WARNING("Unknown NEXT_GI_TECHNIQUE=%s; keeping default GI technique", value);
    return false;
}

int ToPbrShaderDebugMode(DebugViewMode mode) {
    if (mode == DebugViewMode::Default || mode == DebugViewMode::Wireframe) {
        return static_cast<int>(DebugViewMode::Default);
    }
    if (mode == DebugViewMode::Heatmap) {
        return static_cast<int>(DebugViewMode::TriangleCount);
    }
    return static_cast<int>(mode);
}

bool SceneHandlesDebugView(RenderMode renderMode, DebugViewMode mode) {
    return mode == DebugViewMode::Default ||
           mode == DebugViewMode::Wireframe ||
           renderMode == RenderMode::PBR;
}

bool IsTruthyEnv(const char* name) {
    const char* value = std::getenv(name);
    return value && *value &&
           !EqualsIgnoreCaseAscii(value, "0") &&
           !EqualsIgnoreCaseAscii(value, "false") &&
           !EqualsIgnoreCaseAscii(value, "off") &&
           !EqualsIgnoreCaseAscii(value, "no");
}

D3D12_SHADING_RATE ParseShadingRateEnv(const char* value) {
    if (!value || !*value || EqualsIgnoreCaseAscii(value, "1x1")) {
        return D3D12_SHADING_RATE_1X1;
    }
    if (EqualsIgnoreCaseAscii(value, "1x2")) {
        return D3D12_SHADING_RATE_1X2;
    }
    if (EqualsIgnoreCaseAscii(value, "2x1")) {
        return D3D12_SHADING_RATE_2X1;
    }
    if (EqualsIgnoreCaseAscii(value, "2x2")) {
        return D3D12_SHADING_RATE_2X2;
    }
    NEXT_LOG_WARNING("Unknown NEXT_VRS_SHADING_RATE=%s; using 1x1", value);
    return D3D12_SHADING_RATE_1X1;
}
} // namespace

DX12Renderer::DX12Renderer()
    : srvHeap_(nullptr)
    , samplerHeap_(nullptr)
    , hwnd_(nullptr)
    , width_(0)
    , height_(0)
    , time_(0.0f)
    , deltaTime_(0.0)
    , renderMode_(RenderMode::PBR)
    , useGpuDriven_(true)
    , meshShaderDebugEnabled_(false)
    , samplerFeedbackDebugEnabled_(false)
    , samplerFeedbackDispatchLogged_(false)
    , conservativeGpuSync_(true)
    , frameRecording_(false)
    , initialized_(false) {
}

DX12Renderer::~DX12Renderer() {
    Shutdown();
}

void DX12Renderer::QueueResize(int width, int height) {
    // Ignore minimize (0,0) and bogus sizes.
    if (width <= 0 || height <= 0) {
        return;
    }
    pendingWidth_.store(static_cast<uint32_t>(width), std::memory_order_relaxed);
    pendingHeight_.store(static_cast<uint32_t>(height), std::memory_order_relaxed);
    pendingResize_.store(true, std::memory_order_release);
}

void DX12Renderer::ApplyPendingResizeIfAny() {
    if (!pendingResize_.load(std::memory_order_acquire)) {
        return;
    }
    pendingResize_.store(false, std::memory_order_release);

    const uint32_t w = pendingWidth_.load(std::memory_order_relaxed);
    const uint32_t h = pendingHeight_.load(std::memory_order_relaxed);
    if (w == 0 || h == 0) {
        return;
    }

    // Avoid expensive GPU flush if nothing changed.
    if (w == width_ && h == height_) {
        return;
    }
    Resize(static_cast<int>(w), static_cast<int>(h));
}

bool DX12Renderer::Initialize(Window* window) {
    NEXT_LOG_INFO("Initializing DX12 Renderer...");

    if (!window || !window->GetNativeHandle()) {
        NEXT_LOG_ERROR("Invalid window handle");
        return false;
    }

    window_ = window;
    hwnd_ = static_cast<HWND>(window->GetNativeHandle());
    width_ = window->GetWidth();
    height_ = window->GetHeight();
    time_ = 0.0f;
    deltaTime_ = 0.016;  // Initial estimate
    conservativeGpuSync_ = !IsTruthyEnv("NEXT_DX12_ALLOW_GPU_OVERLAP");
    if (conservativeGpuSync_) {
        NEXT_LOG_INFO("DX12 conservative GPU sync enabled for dynamic renderer resources");
    } else {
        NEXT_LOG_WARNING("NEXT_DX12_ALLOW_GPU_OVERLAP is set; dynamic renderer resources may require per-frame backing");
    }

    // Keep the swapchain/backbuffer matched to the Win32 client size. Not doing this causes blur
    // (stretch/blit) and mouse hit-testing mismatch for ImGui overlays.
    window_->SetResizeCallback([this](int w, int h) { QueueResize(w, h); });

    // Initialize timing
    lastFrameTime_ = std::chrono::steady_clock::now();

    // Create device resources
    if (!CreateDeviceResources()) {
        NEXT_LOG_ERROR("Failed to create device resources");
        Shutdown();
        return false;
    }

    // Create window resources (swapchain, etc.)
    if (!CreateWindowResources()) {
        NEXT_LOG_ERROR("Failed to create window resources");
        Shutdown();
        return false;
    }

    // Create depth buffer
    if (!CreateDepthBuffer()) {
        NEXT_LOG_ERROR("Failed to create depth buffer");
        Shutdown();
        return false;
    }

    // Create pipeline resources (shaders, PSO, vertex buffer)
    if (!CreatePipelineResources()) {
        NEXT_LOG_ERROR("Failed to create pipeline resources");
        Shutdown();
        return false;
    }

    if (!CreateMeshShaderResources()) {
        NEXT_LOG_ERROR("Failed to create mesh shader resources");
        Shutdown();
        return false;
    }

    if (!CreateSamplerFeedbackResources()) {
        NEXT_LOG_ERROR("Failed to create sampler feedback resources");
        Shutdown();
        return false;
    }

    if (!CreateSceneColorTarget()) {
        NEXT_LOG_ERROR("Failed to create scene color target");
        Shutdown();
        return false;
    }

    if (!CreateTemporalAATarget()) {
        NEXT_LOG_ERROR("Failed to create TAA target");
        Shutdown();
        return false;
    }

    if (!temporalAA_.Initialize(
            &device_,
            srvHeap_,
            taaSrvAllocation_.cpuHandle,
            taaSrvAllocation_.gpuHandle,
            width_,
            height_,
            swapchain_.GetFormat())) {
        NEXT_LOG_ERROR("Failed to initialize TAA");
        Shutdown();
        return false;
    }

    if (!postProcessing_.Initialize(
            &device_,
            srvHeap_,
            sceneColorSrvAllocation_.cpuHandle,
            sceneColorSrvAllocation_.gpuHandle,
            width_,
            height_,
            swapchain_.GetFormat())) {
        NEXT_LOG_ERROR("Failed to initialize post-processing");
        Shutdown();
        return false;
    }

    if (!debugViews_.Initialize(&device_)) {
        NEXT_LOG_ERROR("Failed to initialize debug views");
        Shutdown();
        return false;
    }
    if (const char* debugView = std::getenv("NEXT_DEBUG_VIEW")) {
        debugViews_.SetDebugMode(ParseDebugViewMode(debugView));
        NEXT_LOG_INFO("Debug view override from NEXT_DEBUG_VIEW=%s", debugView);
    }

    // Create PBR resources (optional - log warning if fails)
    if (!CreatePBRResources()) {
        NEXT_LOG_ERROR("Failed to create PBR resources");
        Shutdown();
        return false;
    }

    // Initialize advanced rendering features (GI system)
    if (!giManager_.Initialize(&device_, srvHeap_, &descriptorHeapManager_, width_, height_)) {
        if (IsTruthyEnv("NEXT_REQUIRE_DX12U")) {
            NEXT_LOG_ERROR("NEXT_REQUIRE_DX12U is set, but GI manager failed to initialize");
            Shutdown();
            return false;
        }
        NEXT_LOG_WARNING("Failed to initialize GI manager - advanced features disabled");
    } else {
        giManager_.SetRayTracingSceneGeometry(
            pbrVertexBuffer_.GetResource(),
            24,
            sizeof(float) * 11,
            pbrIndexBuffer_.GetResource(),
            NumCubeIndices,
            DXGI_FORMAT_R16_UINT);

        if (const char* giTechnique = std::getenv("NEXT_GI_TECHNIQUE")) {
            GITechnique parsedTechnique = GITechnique::Hybrid;
            if (ParseGITechniqueEnv(giTechnique, parsedTechnique)) {
                giManager_.SetGITechnique(parsedTechnique);
                NEXT_LOG_INFO("GI technique override from NEXT_GI_TECHNIQUE=%s", giTechnique);
            } else if (IsTruthyEnv("NEXT_REQUIRE_DX12U")) {
                NEXT_LOG_ERROR("NEXT_REQUIRE_DX12U is set, but NEXT_GI_TECHNIQUE is invalid");
                Shutdown();
                return false;
            }
        }

        if (const char* aoTechnique = std::getenv("NEXT_AO_TECHNIQUE")) {
            AOType parsedAO = AOType::GTAO;
            AmbientOcclusionManager* aoManager = giManager_.GetAOManager();
            if (ParseAOTypeEnv(aoTechnique, parsedAO) && aoManager && aoManager->SetAOType(parsedAO)) {
                NEXT_LOG_INFO("AO technique override from NEXT_AO_TECHNIQUE=%s", aoTechnique);
            } else if (IsTruthyEnv("NEXT_REQUIRE_DX12U")) {
                NEXT_LOG_ERROR("NEXT_REQUIRE_DX12U is set, but NEXT_AO_TECHNIQUE could not be applied");
                Shutdown();
                return false;
            }
        }
    }

    initialized_ = true;
    NEXT_LOG_INFO("DX12 Renderer initialized successfully (%ux%u)", width_, height_);
    return true;
}

void DX12Renderer::Shutdown() {
    if (!initialized_ && !device_.GetDevice() && !commandQueue_.GetQueue() && !window_) {
        return;
    }

    NEXT_LOG_INFO("Shutting down DX12 Renderer...");

    // Wait for GPU to finish
    if (commandQueue_.GetQueue()) {
        WaitForGPU();
    }

    // Detach from window callbacks to avoid invoking a dead renderer during teardown.
    if (window_) {
        window_->SetResizeCallback({});
        window_ = nullptr;
    }

    // Shutdown resources
    depthBuffer_.Reset();
    dsvHeap_.Shutdown();
    sceneColor_.Reset();
    sceneColorRTVHeap_.Shutdown();
    taaOutput_.Reset();
    taaRTVHeap_.Shutdown();
    constantBuffer_.Shutdown();
    debugCellFrameBuffer_.Shutdown();
    debugCellIndexBuffer_.Shutdown();
    debugCellVertexBuffer_.Shutdown();
    indexBuffer_.Shutdown();
    vertexBuffer_.Shutdown();
    pipelineStateWireframe_.Shutdown();
    pipelineState_.Shutdown();
    pixelShader_.Shutdown();
    vertexShader_.Shutdown();
    rootSignature_.Shutdown();

    // PBR resources
    pbrMVPBuffer_.Shutdown();
    pbrMaterialBuffer_.Shutdown();
    pbrLightingBuffer_.Shutdown();
    pbrIndexBuffer_.Shutdown();
    pbrVertexBuffer_.Shutdown();
    pbrPipelineStateWireframe_.Shutdown();
    pbrPipelineState_.Shutdown();
    pbrPixelShader_.Shutdown();
    pbrVertexShader_.Shutdown();
    pbrRootSignature_.Shutdown();
    pbrMaterial_.Shutdown();
    pbrSampler_.Shutdown();

    // Shutdown advanced rendering features
    giManager_.Shutdown();
    temporalAA_.Shutdown();
    postProcessing_.Shutdown();
    debugViews_.Shutdown();
    meshShaderDebugPass_.Shutdown();
    meshShaderDebugEnabled_ = false;

    texture_.Shutdown();
    sampler_.Shutdown();
    samplerFeedbackPSO_.Reset();
    samplerFeedbackMap_.Reset();
    samplerFeedbackShader_.Shutdown();
    samplerFeedbackRootSignature_.Shutdown();
    if (samplerFeedbackUavAllocation_.count != 0) {
        descriptorHeapManager_.Release(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, samplerFeedbackUavAllocation_);
    }
    samplerFeedbackUavAllocation_ = DescriptorAllocation();
    if (taaSrvAllocation_.count != 0) {
        descriptorHeapManager_.Release(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, taaSrvAllocation_);
    }
    taaSrvAllocation_ = DescriptorAllocation();
    if (sceneColorSrvAllocation_.count != 0) {
        descriptorHeapManager_.Release(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, sceneColorSrvAllocation_);
    }
    sceneColorSrvAllocation_ = DescriptorAllocation();
    samplerFeedbackDebugEnabled_ = false;
    samplerFeedbackDispatchLogged_ = false;
    srvHeap_ = nullptr;
    samplerHeap_ = nullptr;
    descriptorHeapManager_.Shutdown();
    commandList_.Shutdown();
    swapchain_.Shutdown();
    commandQueue_.Shutdown();
    device_.Shutdown();

    hwnd_ = nullptr;
    width_ = 0;
    height_ = 0;
    time_ = 0.0f;
    conservativeGpuSync_ = true;
    frameRecording_ = false;
    initialized_ = false;

    NEXT_LOG_INFO("DX12 Renderer shutdown complete");
}

void DX12Renderer::SetFrameDesc(const RendererFrameDesc& frame) {
    frameDesc_ = frame;
    lightingScene_.camera.position = Vec3(
        frameDesc_.cameraPosition[0],
        frameDesc_.cameraPosition[1],
        frameDesc_.cameraPosition[2]);
}

void DX12Renderer::BeginFrame() {
    frameRecording_ = false;

    if (!initialized_) {
        return;
    }

    if (!descriptorHeapHighWatermarkLogged_ && srvHeap_ && samplerHeap_) {
        DX12DescriptorAllocator::Statistics srvStats = {};
        DX12DescriptorAllocator::Statistics samplerStats = {};

        bool hasSrvStats = descriptorHeapManager_.GetStatistics(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, srvStats);
        bool hasSamplerStats = descriptorHeapManager_.GetStatistics(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, samplerStats);

        if (hasSrvStats && hasSamplerStats) {
            float srvUsage = srvStats.totalDescriptors > 0
                                 ? (static_cast<float>(srvStats.allocatedDescriptors) * 100.0f) /
                                       static_cast<float>(srvStats.totalDescriptors)
                                 : 0.0f;
            float samplerUsage = samplerStats.totalDescriptors > 0
                                    ? (static_cast<float>(samplerStats.allocatedDescriptors) * 100.0f) /
                                          static_cast<float>(samplerStats.totalDescriptors)
                                    : 0.0f;

            if (srvUsage >= kDescriptorHeapUsageWarnThreshold || samplerUsage >= kDescriptorHeapUsageWarnThreshold) {
                if (hasSrvStats) {
                    NEXT_LOG_WARNING("Descriptor heap usage high: SRV heap=%u/%u (%.1f%%)",
                                   srvStats.allocatedDescriptors,
                                   srvStats.totalDescriptors,
                                   srvUsage);
                }
                if (hasSamplerStats) {
                    NEXT_LOG_WARNING("Descriptor heap usage high: Sampler heap=%u/%u (%.1f%%)",
                                   samplerStats.allocatedDescriptors,
                                   samplerStats.totalDescriptors,
                                   samplerUsage);
                }
                descriptorHeapHighWatermarkLogged_ = true;
            }
        }
    }

    // Apply resize at a stable point (outside WndProc / mid-frame). This avoids doing ResizeBuffers()
    // from inside message dispatch and reduces chances of device removal.
    ApplyPendingResizeIfAny();

    // Begin frame-in-flight synchronization
    commandQueue_.BeginFrame();

    // Advance descriptor heap frame tracking
    descriptorHeapManager_.AdvanceFrame();

    // Reset command list
    if (!commandList_.Reset(commandQueue_.GetFrameIndex())) {
        commandQueue_.EndFrame();
        return;
    }

    frameRecording_ = true;

    if (srvHeap_ && samplerHeap_) {
        ID3D12DescriptorHeap* heaps[] = {
            srvHeap_->GetHeap(),
            samplerHeap_->GetHeap()
        };
        commandList_.SetDescriptorHeaps(2, heaps);
    }
}

void DX12Renderer::EndFrame() {
    if (!initialized_ || !frameRecording_) {
        return;
    }

    // Close command list
    if (!commandList_.Close()) {
        frameRecording_ = false;
        commandQueue_.EndFrame();
        return;
    }

    // Execute command list
    if (commandQueue_.ExecuteCommandList(&commandList_) == 0) {
        NEXT_LOG_ERROR("Failed to execute DX12 command list");
        frameRecording_ = false;
        commandQueue_.EndFrame();
        return;
    }

    // Present
    if (!swapchain_.Present(1, 0)) {
        frameRecording_ = false;
        commandQueue_.EndFrame();
        return;
    }

    if (conservativeGpuSync_) {
        commandQueue_.WaitForGPU();
    }

    // Update time with actual delta time
    auto currentTime = std::chrono::steady_clock::now();
    std::chrono::duration<double> diff = currentTime - lastFrameTime_;
    deltaTime_ = diff.count();
    time_ += static_cast<float>(deltaTime_);
    lastFrameTime_ = currentTime;

    // End frame-in-flight synchronization
    commandQueue_.EndFrame();
    frameRecording_ = false;

    // Conservative sync keeps global dynamic resources valid until they are split into per-frame backing stores.
}

void DX12Renderer::Render() {
    if (!initialized_ || !frameRecording_) {
        return;
    }

    // Update constant buffers
    if (!UpdateConstantBuffer(time_) || !UpdateLightingBuffers()) {
        NEXT_LOG_ERROR("Skipping DX12 render: failed to update frame constant buffers");
        return;
    }

    renderGraph_.Reset();

    ID3D12Resource* backBufferResource = swapchain_.GetCurrentRenderTarget();
    D3D12_CPU_DESCRIPTOR_HANDLE backBufferView = swapchain_.GetCurrentRenderTargetView();
    if (!backBufferResource || backBufferView.ptr == 0) {
        NEXT_LOG_ERROR("Skipping DX12 render: swapchain backbuffer is unavailable");
        return;
    }

    auto backBuffer = renderGraph_.ImportRenderTarget(
        "BackBuffer",
        backBufferResource,
        backBufferView,
        D3D12_RESOURCE_STATE_PRESENT);

    D3D12_CPU_DESCRIPTOR_HANDLE sceneColorView = sceneColorRTVHeap_.GetCPUDescriptorHandle(0);
    D3D12_CPU_DESCRIPTOR_HANDLE depthView = dsvHeap_.GetCPUDescriptorHandle(0);
    if (!sceneColor_ || sceneColorView.ptr == 0 || !depthBuffer_ || depthView.ptr == 0) {
        NEXT_LOG_ERROR("Skipping DX12 render: scene color or depth target is unavailable");
        return;
    }

    auto sceneColor = renderGraph_.ImportRenderTarget(
        "SceneColor",
        sceneColor_.Get(),
        sceneColorView,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    D3D12_CPU_DESCRIPTOR_HANDLE taaView = taaRTVHeap_.GetCPUDescriptorHandle(0);
    if (!taaOutput_ || taaView.ptr == 0 || !temporalAA_.IsInitialized()) {
        NEXT_LOG_ERROR("Skipping DX12 render: TAA target is unavailable");
        return;
    }

    auto taaColor = renderGraph_.ImportRenderTarget(
        "TAAColor",
        taaOutput_.Get(),
        taaView,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    auto depthBuffer = renderGraph_.ImportDepthTarget(
        "Depth",
        depthBuffer_.Get(),
        depthView,
        D3D12_RESOURCE_STATE_DEPTH_WRITE);

    renderGraph_.AddPass(
        "Main",
        [&](RenderGraphBuilder& builder) {
            builder.Write(sceneColor, D3D12_RESOURCE_STATE_RENDER_TARGET);
            builder.Write(depthBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        },
        [&](RenderGraphContext& ctx) {
            D3D12_CPU_DESCRIPTOR_HANDLE rtv = ctx.GetRTV(sceneColor);
            D3D12_CPU_DESCRIPTOR_HANDLE dsv = ctx.GetDSV(depthBuffer);

            float clearColor[4] = {0.39f, 0.58f, 0.93f, 1.0f}; // Cornflower blue
            commandList_.ClearRenderTargetView(rtv, clearColor);
            commandList_.ClearDepthStencilView(dsv, 1.0f, 0);

            D3D12_CPU_DESCRIPTOR_HANDLE rtvs[] = {rtv};
            commandList_.OMSetRenderTargets(1, rtvs, TRUE, dsv);

            if (device_.SupportsVRS()) {
                commandList_.RSSetShadingRate(
                    ParseShadingRateEnv(std::getenv("NEXT_VRS_SHADING_RATE")),
                    D3D12_SHADING_RATE_COMBINER_PASSTHROUGH);
            }

            if (renderMode_ == RenderMode::PBR) {
                RenderPBRCube();
            } else {
                RenderCube();
            }
            RenderDebugCells();
            RenderMeshShaderDebug();
            RenderSamplerFeedbackDebug();

        });

    renderGraph_.AddPass(
        "GlobalIllumination",
        [&](RenderGraphBuilder& builder) {
            builder.Read(sceneColor, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            builder.Read(depthBuffer, D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        },
        [&](RenderGraphContext& ctx) {
            ID3D12GraphicsCommandList* commandList = commandList_.GetCommandList();
            const float aspect = height_ > 0 ? static_cast<float>(width_) / static_cast<float>(height_) : 1.0f;
            giManager_.SetRayTracingCamera(
                Vec3(frameDesc_.cameraPosition[0], frameDesc_.cameraPosition[1], frameDesc_.cameraPosition[2]),
                Vec3(frameDesc_.cameraTarget[0], frameDesc_.cameraTarget[1], frameDesc_.cameraTarget[2]),
                Vec3(frameDesc_.cameraUp[0], frameDesc_.cameraUp[1], frameDesc_.cameraUp[2]),
                3.14159f / 4.0f,
                aspect);
            giManager_.Update(
                commandList,
                ctx.GetResource(depthBuffer),
                nullptr,
                ctx.GetResource(sceneColor));
            giManager_.Render(commandList, ctx.GetResource(sceneColor));
        });

    renderGraph_.AddPass(
        "TemporalAA",
        [&](RenderGraphBuilder& builder) {
            builder.Read(sceneColor, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            builder.Write(taaColor, D3D12_RESOURCE_STATE_RENDER_TARGET);
        },
        [&](RenderGraphContext& ctx) {
            temporalAA_.Resolve(
                commandList_.GetCommandList(),
                ctx.GetResource(sceneColor),
                ctx.GetResource(taaColor),
                ctx.GetRTV(taaColor));
        });

    renderGraph_.AddPass(
        "PostProcess",
        [&](RenderGraphBuilder& builder) {
            builder.Read(taaColor, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            builder.Write(backBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET);
        },
        [&](RenderGraphContext& ctx) {
            if (device_.SupportsVRS()) {
                commandList_.RSSetShadingRate(
                    D3D12_SHADING_RATE_1X1,
                    D3D12_SHADING_RATE_COMBINER_PASSTHROUGH);
            }

            postProcessing_.Process(
                commandList_.GetCommandList(),
                ctx.GetResource(taaColor),
                ctx.GetResource(backBuffer),
                ctx.GetRTV(backBuffer),
                giManager_.GetGIOutput());

            if (!SceneHandlesDebugView(renderMode_, debugViews_.GetDebugMode())) {
                debugViews_.RenderDebugOverlay(
                    commandList_.GetCommandList(),
                    ctx.GetRTV(backBuffer),
                    width_,
                    height_);
            }

            // Optional UI/debug overlay (e.g. editor ImGui). Runs after post processing/debug view, before present.
            if (overlayCallback_) {
                overlayCallback_(commandList_.GetCommandList());
            }
        });

    renderGraph_.AddPass(
        "Present",
        [&](RenderGraphBuilder& builder) {
            builder.Write(backBuffer, D3D12_RESOURCE_STATE_PRESENT);
            builder.Write(depthBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        },
        [](RenderGraphContext&) {});

    renderGraph_.Execute(&commandList_);
}

void DX12Renderer::Resize(int width, int height) {
    if (!initialized_) {
        return;
    }

    if (width <= 0 || height <= 0) {
        return;
    }

    if (static_cast<UINT>(width) == width_ && static_cast<UINT>(height) == height_) {
        return;
    }

    NEXT_LOG_INFO("Resizing renderer to %dx%d", width, height);

    // Wait for GPU to finish
    WaitForGPU();

    // Resize swapchain
    if (!swapchain_.Resize(width, height)) {
        NEXT_LOG_ERROR("Failed to resize swapchain");
        Shutdown();
        return;
    }

    width_ = width;
    height_ = height;

    viewport_.Width = static_cast<float>(width_);
    viewport_.Height = static_cast<float>(height_);
    scissorRect_.right = width_;
    scissorRect_.bottom = height_;

    // Recreate depth buffer for new size
    depthBuffer_.Reset();
    dsvHeap_.Shutdown();
    if (!CreateDepthBuffer()) {
        NEXT_LOG_ERROR("Failed to recreate depth buffer after resize");
        Shutdown();
        return;
    }

    if (!CreateSceneColorTarget()) {
        NEXT_LOG_ERROR("Failed to recreate scene color target after resize");
        Shutdown();
        return;
    }

    if (!CreateTemporalAATarget()) {
        NEXT_LOG_ERROR("Failed to recreate TAA target after resize");
        Shutdown();
        return;
    }

    if (!temporalAA_.Resize(width_, height_)) {
        NEXT_LOG_ERROR("Failed to resize TAA");
        Shutdown();
        return;
    }

    if (!postProcessing_.Resize(width_, height_)) {
        NEXT_LOG_ERROR("Failed to resize post-processing");
        Shutdown();
        return;
    }

    // Resize advanced render targets if present.
    if (!giManager_.Resize(width_, height_)) {
        NEXT_LOG_WARNING("Failed to resize GI manager; advanced GI remains unavailable for this size");
    }
}

bool DX12Renderer::CreateDeviceResources() {
    NEXT_LOG_DEBUG("Creating device resources...");

    // Initialize device
    if (!device_.Initialize()) {
        NEXT_LOG_ERROR("Failed to initialize DX12 device");
        return false;
    }

    if (IsTruthyEnv("NEXT_REQUIRE_DX12U")) {
        const DX12Features& features = device_.GetFeatures();
        if (!device_.IsDX12U() ||
            !features.meshShaders ||
            !features.raytracing ||
            !features.variableShading ||
            !features.samplerFeedback) {
            NEXT_LOG_ERROR("NEXT_REQUIRE_DX12U is set, but adapter '%s' is missing DX12U requirements: FL=0x%X Mesh=%s RT=%s VRS=%s SamplerFeedback=%s.",
                           device_.GetAdapterDescription().c_str(),
                           features.featureLevel,
                           features.meshShaders ? "Yes" : "No",
                           features.raytracing ? "Yes" : "No",
                           features.variableShading ? "Yes" : "No",
                           features.samplerFeedback ? "Yes" : "No");
            return false;
        }
    }

    // Initialize command queue
    if (!commandQueue_.Initialize(&device_, D3D12_COMMAND_LIST_TYPE_DIRECT)) {
        NEXT_LOG_ERROR("Failed to initialize command queue");
        return false;
    }

    // Initialize command list (one allocator per frame-in-flight to avoid resetting allocators while GPU is executing).
    if (!commandList_.Initialize(&device_, D3D12_COMMAND_LIST_TYPE_DIRECT, DX12CommandQueue::MAX_FRAME_IN_FLIGHT)) {
        NEXT_LOG_ERROR("Failed to initialize command list");
        return false;
    }

    // Initialize descriptor heap manager
    if (!descriptorHeapManager_.Initialize(&device_, 2)) {
        NEXT_LOG_ERROR("Failed to initialize descriptor heap manager");
        return false;
    }

    NEXT_LOG_DEBUG("Device resources created successfully");
    return true;
}

bool DX12Renderer::CreateWindowResources() {
    NEXT_LOG_DEBUG("Creating window resources...");

    // Initialize swapchain
    if (!swapchain_.Initialize(&device_, &commandQueue_, hwnd_, width_, height_)) {
        NEXT_LOG_ERROR("Failed to initialize swapchain");
        return false;
    }

    NEXT_LOG_DEBUG("Window resources created successfully");
    return true;
}

bool DX12Renderer::CreateDepthBuffer() {
    NEXT_LOG_INFO("Creating depth buffer...");

    // Create DSV heap
    if (!dsvHeap_.Initialize(&device_, 1)) {
        NEXT_LOG_ERROR("Failed to create DSV heap");
        return false;
    }

    // Create depth buffer resource
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Alignment = 0;
    desc.Width = width_;
    desc.Height = height_;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R32_TYPELESS;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = DXGI_FORMAT_D32_FLOAT;
    clearValue.DepthStencil.Depth = 1.0f;
    clearValue.DepthStencil.Stencil = 0;

    HRESULT hr = device_.GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &clearValue,
        IID_PPV_ARGS(&depthBuffer_)
    );

    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create depth buffer: 0x%X", hr);
        return false;
    }

    // Create DSV
    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Texture2D.MipSlice = 0;

    device_.GetDevice()->CreateDepthStencilView(
        depthBuffer_.Get(),
        &dsvDesc,
        dsvHeap_.GetCPUDescriptorHandle(0)
    );

    NEXT_LOG_INFO("Depth buffer created successfully");
    return true;
}

bool DX12Renderer::CreateSceneColorTarget() {
    if (!device_.GetDevice() || width_ == 0 || height_ == 0) {
        NEXT_LOG_ERROR("Invalid state for scene color creation");
        return false;
    }

    sceneColor_.Reset();
    sceneColorRTVHeap_.Shutdown();

    if (!sceneColorRTVHeap_.Initialize(&device_, 1)) {
        NEXT_LOG_ERROR("Failed to create scene color RTV heap");
        return false;
    }

    if (sceneColorSrvAllocation_.count < 2) {
        if (sceneColorSrvAllocation_.count != 0) {
            descriptorHeapManager_.Release(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, sceneColorSrvAllocation_);
            sceneColorSrvAllocation_ = DescriptorAllocation();
        }

        sceneColorSrvAllocation_ = descriptorHeapManager_.Allocate(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2);
        if (sceneColorSrvAllocation_.count < 2) {
            NEXT_LOG_ERROR("Failed to allocate scene color SRV descriptor");
            return false;
        }
    }

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width_;
    desc.Height = height_;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = swapchain_.GetFormat();
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = desc.Format;
    clearValue.Color[0] = 0.39f;
    clearValue.Color[1] = 0.58f;
    clearValue.Color[2] = 0.93f;
    clearValue.Color[3] = 1.0f;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    HRESULT hr = device_.GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        &clearValue,
        IID_PPV_ARGS(&sceneColor_));

    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create scene color target: 0x%X", hr);
        return false;
    }

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format = desc.Format;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    rtvDesc.Texture2D.MipSlice = 0;
    rtvDesc.Texture2D.PlaneSlice = 0;
    device_.GetDevice()->CreateRenderTargetView(
        sceneColor_.Get(),
        &rtvDesc,
        sceneColorRTVHeap_.GetCPUDescriptorHandle(0));

    NEXT_LOG_INFO("Scene color target created (%ux%u)", width_, height_);
    return true;
}

bool DX12Renderer::CreateTemporalAATarget() {
    if (!device_.GetDevice() || width_ == 0 || height_ == 0) {
        NEXT_LOG_ERROR("Invalid state for TAA target creation");
        return false;
    }

    taaOutput_.Reset();
    taaRTVHeap_.Shutdown();

    if (!taaRTVHeap_.Initialize(&device_, 1)) {
        NEXT_LOG_ERROR("Failed to create TAA RTV heap");
        return false;
    }

    if (taaSrvAllocation_.count < 3) {
        if (taaSrvAllocation_.count != 0) {
            descriptorHeapManager_.Release(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, taaSrvAllocation_);
            taaSrvAllocation_ = DescriptorAllocation();
        }

        taaSrvAllocation_ = descriptorHeapManager_.Allocate(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 3);
        if (taaSrvAllocation_.count < 3) {
            NEXT_LOG_ERROR("Failed to allocate TAA SRV descriptors");
            return false;
        }
    }

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width_;
    desc.Height = height_;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = swapchain_.GetFormat();
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = desc.Format;
    clearValue.Color[0] = 0.0f;
    clearValue.Color[1] = 0.0f;
    clearValue.Color[2] = 0.0f;
    clearValue.Color[3] = 1.0f;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    HRESULT hr = device_.GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        &clearValue,
        IID_PPV_ARGS(&taaOutput_));

    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create TAA output target: 0x%X", hr);
        return false;
    }

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format = desc.Format;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    rtvDesc.Texture2D.MipSlice = 0;
    rtvDesc.Texture2D.PlaneSlice = 0;
    const D3D12_CPU_DESCRIPTOR_HANDLE rtv = taaRTVHeap_.GetCPUDescriptorHandle(0);
    if (rtv.ptr == 0) {
        NEXT_LOG_ERROR("Invalid TAA RTV descriptor handle");
        return false;
    }
    device_.GetDevice()->CreateRenderTargetView(taaOutput_.Get(), &rtvDesc, rtv);

    NEXT_LOG_INFO("TAA output target created (%ux%u)", width_, height_);
    return true;
}

void DX12Renderer::WaitForGPU() {
    // Proper fence-based synchronization
    commandQueue_.WaitForGPU();
}


bool DX12Renderer::CreatePipelineResources() {
    NEXT_LOG_INFO("Creating pipeline resources for cube rendering...");

    viewport_.TopLeftX = 0.0f;
    viewport_.TopLeftY = 0.0f;
    viewport_.Width = static_cast<float>(width_);
    viewport_.Height = static_cast<float>(height_);
    viewport_.MinDepth = 0.0f;
    viewport_.MaxDepth = 1.0f;

    scissorRect_.left = 0;
    scissorRect_.top = 0;
    scissorRect_.right = width_;
    scissorRect_.bottom = height_;

    // Create SRV and Sampler descriptor heaps
    if (!descriptorHeapManager_.CreateHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 4096, true)) {
        NEXT_LOG_ERROR("Failed to create managed SRV heap");
        return false;
    }

    if (!descriptorHeapManager_.CreateHeap(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 256, true)) {
        NEXT_LOG_ERROR("Failed to create managed sampler heap");
        return false;
    }

    srvHeap_ = descriptorHeapManager_.GetHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    samplerHeap_ = descriptorHeapManager_.GetHeap(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    if (!srvHeap_ || !samplerHeap_) {
        NEXT_LOG_ERROR("Managed descriptor heaps not available");
        return false;
    }

    const std::string cubeVertexShaderPath = ResolveRuntimeAssetPathUtf8("engine/renderer/shaders/cube.vs.hlsl");
    if (!vertexShader_.LoadFromFile(&device_, cubeVertexShaderPath.c_str())) {
        NEXT_LOG_ERROR("Failed to load vertex shader: %s", cubeVertexShaderPath.c_str());
        return false;
    }

    const std::string cubePixelShaderPath = ResolveRuntimeAssetPathUtf8("engine/renderer/shaders/cube.ps.hlsl");
    if (!pixelShader_.LoadFromFile(&device_, cubePixelShaderPath.c_str())) {
        NEXT_LOG_ERROR("Failed to load pixel shader: %s", cubePixelShaderPath.c_str());
        return false;
    }

    // Create root signature with descriptor table for texture and sampler
    D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
    D3D12_ROOT_PARAMETER rootParameters[3] = {};

    // Parameter 0: CBV (constant buffer)
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[0].Descriptor.ShaderRegister = 0;
    rootParameters[0].Descriptor.RegisterSpace = 0;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // Parameter 1: Descriptor table for SRV (texture)
    D3D12_DESCRIPTOR_RANGE srvRange = {};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 1;
    srvRange.BaseShaderRegister = 0;
    srvRange.RegisterSpace = 0;
    srvRange.OffsetInDescriptorsFromTableStart = 0;

    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[1].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[1].DescriptorTable.pDescriptorRanges = &srvRange;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // Parameter 2: Descriptor table for Sampler
    D3D12_DESCRIPTOR_RANGE samplerRange = {};
    samplerRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    samplerRange.NumDescriptors = 1;
    samplerRange.BaseShaderRegister = 0;
    samplerRange.RegisterSpace = 0;
    samplerRange.OffsetInDescriptorsFromTableStart = 0;

    rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[2].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[2].DescriptorTable.pDescriptorRanges = &samplerRange;
    rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    rootSignatureDesc.NumParameters = 3;
    rootSignatureDesc.pParameters = rootParameters;
    rootSignatureDesc.NumStaticSamplers = 0;
    rootSignatureDesc.pStaticSamplers = nullptr;
    rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    Microsoft::WRL::ComPtr<ID3DBlob> signature;
    Microsoft::WRL::ComPtr<ID3DBlob> error;

    HRESULT hr = D3D12SerializeRootSignature(
        &rootSignatureDesc,
        D3D_ROOT_SIGNATURE_VERSION_1,
        &signature,
        &error
    );

    if (FAILED(hr)) {
        if (error) {
            NEXT_LOG_ERROR("Root signature serialization failed: %s", (const char*)error->GetBufferPointer());
        } else {
            NEXT_LOG_ERROR("Root signature serialization failed: 0x%X", hr);
        }
        return false;
    }

    Microsoft::WRL::ComPtr<ID3D12RootSignature> tempRootSig;
    hr = device_.GetDevice()->CreateRootSignature(
        0,
        signature->GetBufferPointer(),
        signature->GetBufferSize(),
        IID_PPV_ARGS(&tempRootSig)
    );

    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create root signature: 0x%X", hr);
        return false;
    }

    rootSignature_.SetRootSignature(tempRootSig.Get());

    // Update input layout to include texture coordinates
    std::vector<InputElementDesc> inputLayout = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 28, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
    };

    if (!pipelineState_.Initialize(
            &device_,
            &rootSignature_,
            &vertexShader_,
            &pixelShader_,
            inputLayout,
            DXGI_FORMAT_R8G8B8A8_UNORM,
            D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
            D3D12_FILL_MODE_SOLID,
            true)) {
        NEXT_LOG_ERROR("Failed to create PSO");
        return false;
    }

    if (!pipelineStateWireframe_.Initialize(
            &device_,
            &rootSignature_,
            &vertexShader_,
            &pixelShader_,
            inputLayout,
            DXGI_FORMAT_R8G8B8A8_UNORM,
            D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
            D3D12_FILL_MODE_WIREFRAME,
            true)) {
        NEXT_LOG_ERROR("Failed to create wireframe PSO");
        return false;
    }

    // Vertex structure with texture coordinates
    struct Vertex {
        float position[3];
        float color[4];
        float texcoord[2];
    };

    // Cube vertices with texture coordinates
    Vertex vertices[NumCubeVertices] = {
        {{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}},
        {{-1.0f,  1.0f, -1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}},
        {{ 1.0f,  1.0f, -1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {1.0f, 0.0f}},
        {{ 1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}},
        {{-1.0f, -1.0f,  1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}},
        {{-1.0f,  1.0f,  1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}},
        {{ 1.0f,  1.0f,   1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {1.0f, 0.0f}},
        {{ 1.0f, -1.0f,  1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}}
    };

    if (!vertexBuffer_.Initialize(&device_, sizeof(vertices), D3D12_HEAP_TYPE_UPLOAD)) {
        NEXT_LOG_ERROR("Failed to create vertex buffer");
        return false;
    }

    if (!vertexBuffer_.UploadData(vertices, sizeof(vertices))) {
        NEXT_LOG_ERROR("Failed to upload vertex data");
        return false;
    }

    UINT16 indices[NumCubeIndices] = {
        0, 1, 2, 0, 2, 3,
        5, 4, 6, 5, 6, 7,
        4, 1, 0, 4, 5, 1,
        3, 2,  6, 3, 6, 7,
        1, 5, 2, 5, 6, 2,
        4, 0, 3, 4, 3, 7
    };

    if (!indexBuffer_.Initialize(&device_, sizeof(indices), D3D12_HEAP_TYPE_UPLOAD)) {
        NEXT_LOG_ERROR("Failed to create index buffer");
        return false;
    }

    if (!indexBuffer_.UploadData(indices, sizeof(indices))) {
        NEXT_LOG_ERROR("Failed to upload index data");
        return false;
    }

    if (!constantBuffer_.Initialize(&device_, sizeof(CubeConstants), D3D12_HEAP_TYPE_UPLOAD)) {
        NEXT_LOG_ERROR("Failed to create constant buffer");
        return false;
    }

    if (!CreateDebugCellResources()) {
        return false;
    }

    // Initialize texture and sampler
    if (!texture_.Initialize(&device_, srvHeap_)) {
        NEXT_LOG_ERROR("Failed to initialize texture");
        return false;
    }

    if (!sampler_.Initialize(&device_, samplerHeap_)) {
        NEXT_LOG_ERROR("Failed to initialize sampler");
        return false;
    }

    // Create sampler
    DescriptorAllocation samplerAllocation = descriptorHeapManager_.Allocate(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 1);
    if (samplerAllocation.count == 0) {
        NEXT_LOG_ERROR("Failed to allocate sampler descriptor");
        return false;
    }

    sampler_.SetGPUDescriptorHandle(samplerAllocation.gpuHandle);
    if (!sampler_.Create(samplerAllocation.cpuHandle)) {
        NEXT_LOG_ERROR("Failed to create sampler");
        return false;
    }

    // Load texture (will create checkerboard if file not found)
    pbrTextureAllocation_ = descriptorHeapManager_.Allocate(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kPbrTextureSlots);
    if (pbrTextureAllocation_.count == 0) {
        NEXT_LOG_ERROR("Failed to allocate texture descriptors");
        return false;
    }

    const std::wstring checkerboardTexturePath = ResolveRuntimeAssetPathWide(L"engine/renderer/textures/checkerboard.png");
    bool textureLoaded = texture_.LoadFromFile(
        checkerboardTexturePath.c_str(),
        commandQueue_.GetQueue(),
        pbrTextureAllocation_.cpuHandle);

    if (!textureLoaded) {
        NEXT_LOG_ERROR("Failed to initialize renderer texture: %S", checkerboardTexturePath.c_str());
        return false;
    } else {
        UINT descriptorSize = device_.GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        for (UINT i = 1; i < kPbrTextureSlots; ++i) {
            D3D12_CPU_DESCRIPTOR_HANDLE dstHandle = pbrTextureAllocation_.cpuHandle;
            dstHandle.ptr += static_cast<SIZE_T>(i) * descriptorSize;
            device_.GetDevice()->CopyDescriptorsSimple(
                1,
                dstHandle,
                pbrTextureAllocation_.cpuHandle,
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }
    }

    NEXT_LOG_INFO("Pipeline resources created successfully");
    return true;
}

bool DX12Renderer::CreateMeshShaderResources() {
    meshShaderDebugEnabled_ = false;
    if (!IsTruthyEnv("NEXT_MESH_SHADER_DEBUG")) {
        return true;
    }

    if (!device_.SupportsMeshShaders()) {
        NEXT_LOG_ERROR("NEXT_MESH_SHADER_DEBUG is set, but the selected adapter does not support mesh shaders");
        return false;
    }

    if (!meshShaderDebugPass_.Initialize(&device_)) {
        return false;
    }

    const std::string meshShaderPath = ResolveRuntimeAssetPathUtf8("engine/renderer/shaders/mesh_debug.ms.hlsl");
    const std::string pixelShaderPath = ResolveRuntimeAssetPathUtf8("engine/renderer/shaders/mesh_debug.ps.hlsl");
    if (!meshShaderDebugPass_.LoadShaders(nullptr, meshShaderPath.c_str(), pixelShaderPath.c_str()) ||
        !meshShaderDebugPass_.CreatePipelineState()) {
        meshShaderDebugPass_.Shutdown();
        return false;
    }

    meshShaderDebugEnabled_ = true;
    NEXT_LOG_INFO("DX12U mesh shader debug pass enabled");
    return true;
}

bool DX12Renderer::CreateSamplerFeedbackResources() {
    samplerFeedbackDebugEnabled_ = false;
    samplerFeedbackDispatchLogged_ = false;
    if (!IsTruthyEnv("NEXT_SAMPLER_FEEDBACK_DEBUG")) {
        return true;
    }

    if (!device_.SupportsSamplerFeedback()) {
        NEXT_LOG_ERROR("NEXT_SAMPLER_FEEDBACK_DEBUG is set, but the selected adapter does not support sampler feedback");
        return false;
    }

    ID3D12Resource* pairedTexture = texture_.GetResource();
    if (!pairedTexture) {
        NEXT_LOG_ERROR("Cannot initialize sampler feedback debug pass without a paired texture");
        return false;
    }

    Microsoft::WRL::ComPtr<ID3D12Device8> device8;
    HRESULT hr = device_.GetDevice()->QueryInterface(IID_PPV_ARGS(&device8));
    if (FAILED(hr) || !device8) {
        NEXT_LOG_ERROR("Sampler feedback requires ID3D12Device8: 0x%X", hr);
        return false;
    }

    const D3D12_RESOURCE_DESC pairedDesc = pairedTexture->GetDesc();
    if (pairedDesc.Width < 8 || pairedDesc.Height < 8) {
        NEXT_LOG_ERROR("Sampler feedback debug pass requires a paired texture of at least 8x8 texels");
        return false;
    }

    auto chooseMipRegionDimension = [](UINT64 dimension) -> UINT {
        const UINT maxRegion = static_cast<UINT>(dimension / 2);
        UINT region = 4;
        while ((region * 2) <= maxRegion && region < 8) {
            region *= 2;
        }
        return region;
    };

    D3D12_RESOURCE_DESC1 feedbackDesc = {};
    feedbackDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    feedbackDesc.Width = pairedDesc.Width;
    feedbackDesc.Height = pairedDesc.Height;
    feedbackDesc.DepthOrArraySize = pairedDesc.DepthOrArraySize;
    feedbackDesc.MipLevels = pairedDesc.MipLevels;
    feedbackDesc.Format = DXGI_FORMAT_SAMPLER_FEEDBACK_MIN_MIP_OPAQUE;
    feedbackDesc.SampleDesc.Count = 1;
    feedbackDesc.SampleDesc.Quality = 0;
    feedbackDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    feedbackDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    feedbackDesc.SamplerFeedbackMipRegion = D3D12_MIP_REGION{
        chooseMipRegionDimension(pairedDesc.Width),
        chooseMipRegionDimension(pairedDesc.Height),
        1};

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    hr = device8->CreateCommittedResource2(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &feedbackDesc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        nullptr,
        nullptr,
        IID_PPV_ARGS(&samplerFeedbackMap_));
    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create sampler feedback map: 0x%X", hr);
        return false;
    }

    samplerFeedbackUavAllocation_ = descriptorHeapManager_.Allocate(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
    if (samplerFeedbackUavAllocation_.count == 0) {
        NEXT_LOG_ERROR("Failed to allocate sampler feedback UAV descriptor");
        return false;
    }
    device8->CreateSamplerFeedbackUnorderedAccessView(
        pairedTexture,
        samplerFeedbackMap_.Get(),
        samplerFeedbackUavAllocation_.cpuHandle);

    D3D12_DESCRIPTOR_RANGE ranges[2] = {};
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[0].NumDescriptors = 1;
    ranges[0].BaseShaderRegister = 0;
    ranges[0].RegisterSpace = 0;
    ranges[0].OffsetInDescriptorsFromTableStart = 0;
    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    ranges[1].NumDescriptors = 1;
    ranges[1].BaseShaderRegister = 0;
    ranges[1].RegisterSpace = 0;
    ranges[1].OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER rootParameters[2] = {};
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[0].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[0].DescriptorTable.pDescriptorRanges = &ranges[0];
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[1].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[1].DescriptorTable.pDescriptorRanges = &ranges[1];
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MipLODBias = 0.0f;
    sampler.MaxAnisotropy = 1;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
    sampler.MinLOD = 0.0f;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.RegisterSpace = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
    rootSignatureDesc.NumParameters = 2;
    rootSignatureDesc.pParameters = rootParameters;
    rootSignatureDesc.NumStaticSamplers = 1;
    rootSignatureDesc.pStaticSamplers = &sampler;
    rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    Microsoft::WRL::ComPtr<ID3DBlob> signature;
    Microsoft::WRL::ComPtr<ID3DBlob> error;
    hr = D3D12SerializeRootSignature(
        &rootSignatureDesc,
        D3D_ROOT_SIGNATURE_VERSION_1,
        &signature,
        &error);
    if (FAILED(hr)) {
        if (error) {
            NEXT_LOG_ERROR("Sampler feedback root signature serialization failed: %s",
                           static_cast<const char*>(error->GetBufferPointer()));
        } else {
            NEXT_LOG_ERROR("Sampler feedback root signature serialization failed: 0x%X", hr);
        }
        return false;
    }

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
    hr = device_.GetDevice()->CreateRootSignature(
        0,
        signature->GetBufferPointer(),
        signature->GetBufferSize(),
        IID_PPV_ARGS(&rootSignature));
    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create sampler feedback root signature: 0x%X", hr);
        return false;
    }
    samplerFeedbackRootSignature_.SetRootSignature(rootSignature.Get());

    const std::string shaderPath = ResolveRuntimeAssetPathUtf8("engine/renderer/shaders/sampler_feedback.cs.hlsl");
    if (!samplerFeedbackShader_.InitializeFromFile(&device_, shaderPath.c_str(), "main", "cs_6_5")) {
        NEXT_LOG_ERROR("Failed to load sampler feedback shader: %s", shaderPath.c_str());
        return false;
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = samplerFeedbackRootSignature_.GetRootSignature();
    psoDesc.CS = samplerFeedbackShader_.GetBytecode();
    hr = device_.GetDevice()->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&samplerFeedbackPSO_));
    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create sampler feedback compute PSO: 0x%X", hr);
        return false;
    }

    samplerFeedbackDebugEnabled_ = true;
    NEXT_LOG_INFO("DX12U sampler feedback debug pass enabled");
    return true;
}

bool DX12Renderer::UpdateConstantBuffer(float time) {
    // Calculate MVP matrix
    Mat4 model = Mat4::RotateX(time * 0.5f) * Mat4::RotateY(time * 0.7f);

    Vec3 eye(frameDesc_.cameraPosition[0], frameDesc_.cameraPosition[1], frameDesc_.cameraPosition[2]);
    Vec3 target(frameDesc_.cameraTarget[0], frameDesc_.cameraTarget[1], frameDesc_.cameraTarget[2]);
    Vec3 up(frameDesc_.cameraUp[0], frameDesc_.cameraUp[1], frameDesc_.cameraUp[2]);
    Mat4 view = Mat4::LookAt(eye, target, up);

    float aspect = static_cast<float>(width_) / static_cast<float>(height_);
    Mat4 projection = Mat4::Perspective(3.14159f / 4.0f, aspect, 0.1f, 2000.0f);

    CubeConstants constants;
    constants.model = model.Transpose();
    constants.view = view.Transpose();
    constants.projection = projection.Transpose();
    constants.time = time;
    constants.padding[0] = 0.0f;
    constants.padding[1] = 0.0f;
    constants.padding[2] = 0.0f;

    return constantBuffer_.UploadData(&constants, sizeof(CubeConstants));
}

void DX12Renderer::RenderCube() {
    // Set root signature
    commandList_.SetGraphicsRootSignature(rootSignature_.GetRootSignature());

    // Set constant buffer (root parameter 0)
    commandList_.GetCommandList()->SetGraphicsRootConstantBufferView(0, constantBuffer_.GetGPUVirtualAddress());

    // Set texture descriptor table (root parameter 1)
    commandList_.GetCommandList()->SetGraphicsRootDescriptorTable(1, texture_.GetGPUDescriptorHandle());

    // Set sampler descriptor table (root parameter 2)
    commandList_.GetCommandList()->SetGraphicsRootDescriptorTable(2, sampler_.GetGPUDescriptorHandle());

    // Set pipeline state
    const bool wireframe = debugViews_.GetDebugMode() == DebugViewMode::Wireframe;
    commandList_.SetPipelineState(wireframe ? pipelineStateWireframe_.GetPSO() : pipelineState_.GetPSO());

    // Set viewport and scissor rect
    commandList_.RSSetViewports(1, &viewport_);
    commandList_.RSSetScissorRects(1, &scissorRect_);

    // Set primitive topology
    commandList_.IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Set vertex and index buffers
    D3D12_VERTEX_BUFFER_VIEW vbv = vertexBuffer_.GetVertexBufferView(sizeof(float) * 9);  // 3 pos + 4 color + 2 texcoord
    commandList_.IASetVertexBuffers(0, 1, &vbv);

    D3D12_INDEX_BUFFER_VIEW ibv = indexBuffer_.GetIndexBufferView(DXGI_FORMAT_R16_UINT);
    commandList_.IASetIndexBuffer(&ibv, DXGI_FORMAT_R16_UINT);

    // Draw indexed cube
    commandList_.DrawIndexedInstanced(NumCubeIndices, 1, 0, 0, 0);
}

bool DX12Renderer::CreateDebugCellResources() {
    const size_t vertexBufferSize = kMaxRendererDebugCells * 4 * sizeof(DebugCellVertex);
    const size_t indexBufferSize = kMaxRendererDebugCells * 12 * sizeof(uint16_t);

    if (!debugCellVertexBuffer_.Initialize(&device_, vertexBufferSize, D3D12_HEAP_TYPE_UPLOAD)) {
        NEXT_LOG_ERROR("Failed to create debug cell vertex buffer");
        return false;
    }

    if (!debugCellIndexBuffer_.Initialize(&device_, indexBufferSize, D3D12_HEAP_TYPE_UPLOAD)) {
        NEXT_LOG_ERROR("Failed to create debug cell index buffer");
        return false;
    }

    if (!debugCellFrameBuffer_.Initialize(&device_, sizeof(CubeConstants))) {
        NEXT_LOG_ERROR("Failed to create debug cell constant buffer");
        return false;
    }

    return true;
}

bool DX12Renderer::CreatePBRResources() {
    NEXT_LOG_INFO("Creating PBR rendering resources...");

    // Initialize PBR material
    if (!pbrMaterial_.Initialize(&device_, &descriptorHeapManager_)) {
        NEXT_LOG_ERROR("Failed to initialize PBR material");
        return false;
    }

    // Set default PBR material properties
    pbrMaterial_.SetAlbedo(Vec3(0.8f, 0.2f, 0.2f));  // Red color
    pbrMaterial_.SetMetallic(0.5f);
    pbrMaterial_.SetRoughness(0.3f);
    pbrMaterial_.SetAO(1.0f);

    // Setup default lighting scene
    lightingScene_.camera.position = Vec3(0.0f, 0.0f, -5.0f);
    lightingScene_.settings.ambientColor = Vec3(0.1f, 0.1f, 0.1f);
    lightingScene_.settings.ambientIntensity = 0.1f;
    lightingScene_.settings.exposure = 1.0f;
    lightingScene_.settings.gamma = 2.2f;
    lightingScene_.settings.toneMapMode = 1;  // ACES

    // Add a point light for dynamic lighting
    PointLight pointLight;
    pointLight.position = Vec3(2.0f, 2.0f, -3.0f);
    pointLight.intensity = 2.0f;
    pointLight.color = Vec3(1.0f, 0.9f, 0.7f);  // Warm light
    pointLight.radius = 10.0f;
    pointLight.enabled = 1;
    lightingScene_.AddPointLight(pointLight);

    // Load PBR shaders (use simplified version for debugging)
    const std::string pbrVertexShaderPath = ResolveRuntimeAssetPathUtf8("engine/renderer/shaders/pbr.vs.hlsl");
    if (!pbrVertexShader_.LoadFromFile(&device_, pbrVertexShaderPath.c_str())) {
        NEXT_LOG_ERROR("Failed to load PBR vertex shader: %s", pbrVertexShaderPath.c_str());
        return false;
    }

    const std::string pbrPixelShaderPath = ResolveRuntimeAssetPathUtf8("engine/renderer/shaders/pbr.ps.hlsl");
    if (!pbrPixelShader_.LoadFromFile(&device_, pbrPixelShaderPath.c_str())) {
        NEXT_LOG_ERROR("Failed to load PBR pixel shader: %s", pbrPixelShaderPath.c_str());
        return false;
    }

    // Create PBR root signature with CBVs + texture/sampler tables
    D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
    D3D12_ROOT_PARAMETER rootParameters[5] = {};
    D3D12_DESCRIPTOR_RANGE srvRange = {};
    D3D12_DESCRIPTOR_RANGE samplerRange = {};

    // Parameter 0: MVP matrix CBV
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[0].Descriptor.ShaderRegister = 0;
    rootParameters[0].Descriptor.RegisterSpace = 0;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // Parameter 1: Material CBV
    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[1].Descriptor.ShaderRegister = 0;
    rootParameters[1].Descriptor.RegisterSpace = 1;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // Parameter 2: Lighting CBV
    rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[2].Descriptor.ShaderRegister = 1;
    rootParameters[2].Descriptor.RegisterSpace = 1;
    rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // Parameter 3: SRV descriptor table (t0-t4, space1)
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = kPbrTextureSlots;
    srvRange.BaseShaderRegister = 0;
    srvRange.RegisterSpace = 1;
    srvRange.OffsetInDescriptorsFromTableStart = 0;

    rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[3].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[3].DescriptorTable.pDescriptorRanges = &srvRange;
    rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // Parameter 4: Sampler descriptor table (s0, space1)
    samplerRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    samplerRange.NumDescriptors = 1;
    samplerRange.BaseShaderRegister = 0;
    samplerRange.RegisterSpace = 1;
    samplerRange.OffsetInDescriptorsFromTableStart = 0;

    rootParameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[4].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[4].DescriptorTable.pDescriptorRanges = &samplerRange;
    rootParameters[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    rootSignatureDesc.NumParameters = 5;
    rootSignatureDesc.pParameters = rootParameters;
    rootSignatureDesc.NumStaticSamplers = 0;
    rootSignatureDesc.pStaticSamplers = nullptr;
    rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    Microsoft::WRL::ComPtr<ID3DBlob> signature;
    Microsoft::WRL::ComPtr<ID3DBlob> error;

    HRESULT hr = D3D12SerializeRootSignature(
        &rootSignatureDesc,
        D3D_ROOT_SIGNATURE_VERSION_1,
        &signature,
        &error
    );

    if (FAILED(hr)) {
        if (error) {
            NEXT_LOG_ERROR("PBR root signature serialization failed: %s", (const char*)error->GetBufferPointer());
        } else {
            NEXT_LOG_ERROR("PBR root signature serialization failed: 0x%X", hr);
        }
        return false;
    }

    Microsoft::WRL::ComPtr<ID3D12RootSignature> tempRootSig;
    hr = device_.GetDevice()->CreateRootSignature(
        0,
        signature->GetBufferPointer(),
        signature->GetBufferSize(),
        IID_PPV_ARGS(&tempRootSig)
    );

    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create PBR root signature: 0x%X", hr);
        return false;
    }

    pbrRootSignature_.SetRootSignature(tempRootSig.Get());

    // Input layout for PBR
    std::vector<InputElementDesc> inputLayout = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 36, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
    };

    // Debug: Log PSO creation parameters
    NEXT_LOG_DEBUG("Creating PBR PSO with:");
    NEXT_LOG_DEBUG("  - Input Layout elements: %zu", inputLayout.size());
    NEXT_LOG_DEBUG("  - Render Target Format: DXGI_FORMAT_R8G8B8A8_UNORM");
    NEXT_LOG_DEBUG("  - Root Signature: 0x%p", pbrRootSignature_.GetRootSignature());

    if (!pbrPipelineState_.Initialize(
            &device_,
            &pbrRootSignature_,
            &pbrVertexShader_,
            &pbrPixelShader_,
            inputLayout,
            DXGI_FORMAT_R8G8B8A8_UNORM,
            D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
            D3D12_FILL_MODE_SOLID,
            true)) {
        NEXT_LOG_ERROR("Failed to create PBR PSO: 0x%X (E_INVALIDARG - Invalid parameter)");

        // Try to validate Root Signature
        ID3D12RootSignature* rs = pbrRootSignature_.GetRootSignature();
        if (!rs) {
            NEXT_LOG_ERROR("  Root Signature is NULL!");
        } else {
            NEXT_LOG_DEBUG("  Root Signature pointer is valid");
        }

        return false;
    }

    if (!pbrPipelineStateWireframe_.Initialize(
            &device_,
            &pbrRootSignature_,
            &pbrVertexShader_,
            &pbrPixelShader_,
            inputLayout,
            DXGI_FORMAT_R8G8B8A8_UNORM,
            D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
            D3D12_FILL_MODE_WIREFRAME,
            true)) {
        NEXT_LOG_ERROR("Failed to create PBR wireframe PSO");
        return false;
    }

    // PBR vertex structure
    struct PBRVertex {
        float position[3];
        float normal[3];
        float tangent[3];
        float texcoord[2];
    };

    // Per-face vertices keep normals and tangents correct at cube edges.
    PBRVertex vertices[] = {
        {{-1.0f, -1.0f, -1.0f}, { 0.0f,  0.0f, -1.0f}, { 1.0f, 0.0f,  0.0f}, {0.0f, 1.0f}},
        {{-1.0f,  1.0f, -1.0f}, { 0.0f,  0.0f, -1.0f}, { 1.0f, 0.0f,  0.0f}, {0.0f, 0.0f}},
        {{ 1.0f,  1.0f, -1.0f}, { 0.0f,  0.0f, -1.0f}, { 1.0f, 0.0f,  0.0f}, {1.0f, 0.0f}},
        {{ 1.0f, -1.0f, -1.0f}, { 0.0f,  0.0f, -1.0f}, { 1.0f, 0.0f,  0.0f}, {1.0f, 1.0f}},

        {{-1.0f, -1.0f,  1.0f}, { 0.0f,  0.0f,  1.0f}, { 1.0f, 0.0f,  0.0f}, {0.0f, 1.0f}},
        {{-1.0f,  1.0f,  1.0f}, { 0.0f,  0.0f,  1.0f}, { 1.0f, 0.0f,  0.0f}, {0.0f, 0.0f}},
        {{ 1.0f,  1.0f,  1.0f}, { 0.0f,  0.0f,  1.0f}, { 1.0f, 0.0f,  0.0f}, {1.0f, 0.0f}},
        {{ 1.0f, -1.0f,  1.0f}, { 0.0f,  0.0f,  1.0f}, { 1.0f, 0.0f,  0.0f}, {1.0f, 1.0f}},

        {{-1.0f, -1.0f,  1.0f}, {-1.0f,  0.0f,  0.0f}, { 0.0f, 0.0f, -1.0f}, {0.0f, 1.0f}},
        {{-1.0f,  1.0f,  1.0f}, {-1.0f,  0.0f,  0.0f}, { 0.0f, 0.0f, -1.0f}, {0.0f, 0.0f}},
        {{-1.0f,  1.0f, -1.0f}, {-1.0f,  0.0f,  0.0f}, { 0.0f, 0.0f, -1.0f}, {1.0f, 0.0f}},
        {{-1.0f, -1.0f, -1.0f}, {-1.0f,  0.0f,  0.0f}, { 0.0f, 0.0f, -1.0f}, {1.0f, 1.0f}},

        {{ 1.0f, -1.0f, -1.0f}, { 1.0f,  0.0f,  0.0f}, { 0.0f, 0.0f,  1.0f}, {0.0f, 1.0f}},
        {{ 1.0f,  1.0f, -1.0f}, { 1.0f,  0.0f,  0.0f}, { 0.0f, 0.0f,  1.0f}, {0.0f, 0.0f}},
        {{ 1.0f,  1.0f,  1.0f}, { 1.0f,  0.0f,  0.0f}, { 0.0f, 0.0f,  1.0f}, {1.0f, 0.0f}},
        {{ 1.0f, -1.0f,  1.0f}, { 1.0f,  0.0f,  0.0f}, { 0.0f, 0.0f,  1.0f}, {1.0f, 1.0f}},

        {{-1.0f,  1.0f, -1.0f}, { 0.0f,  1.0f,  0.0f}, { 1.0f, 0.0f,  0.0f}, {0.0f, 1.0f}},
        {{-1.0f,  1.0f,  1.0f}, { 0.0f,  1.0f,  0.0f}, { 1.0f, 0.0f,  0.0f}, {0.0f, 0.0f}},
        {{ 1.0f,  1.0f,  1.0f}, { 0.0f,  1.0f,  0.0f}, { 1.0f, 0.0f,  0.0f}, {1.0f, 0.0f}},
        {{ 1.0f,  1.0f, -1.0f}, { 0.0f,  1.0f,  0.0f}, { 1.0f, 0.0f,  0.0f}, {1.0f, 1.0f}},

        {{-1.0f, -1.0f,  1.0f}, { 0.0f, -1.0f,  0.0f}, { 1.0f, 0.0f,  0.0f}, {0.0f, 1.0f}},
        {{-1.0f, -1.0f, -1.0f}, { 0.0f, -1.0f,  0.0f}, { 1.0f, 0.0f,  0.0f}, {0.0f, 0.0f}},
        {{ 1.0f, -1.0f, -1.0f}, { 0.0f, -1.0f,  0.0f}, { 1.0f, 0.0f,  0.0f}, {1.0f, 0.0f}},
        {{ 1.0f, -1.0f,  1.0f}, { 0.0f, -1.0f,  0.0f}, { 1.0f, 0.0f,  0.0f}, {1.0f, 1.0f}}
    };

    if (!pbrVertexBuffer_.Initialize(&device_, sizeof(vertices), D3D12_HEAP_TYPE_UPLOAD)) {
        NEXT_LOG_ERROR("Failed to create PBR vertex buffer");
        return false;
    }

    if (!pbrVertexBuffer_.UploadData(vertices, sizeof(vertices))) {
        NEXT_LOG_ERROR("Failed to upload PBR vertex data");
        return false;
    }

    UINT16 indices[NumCubeIndices] = {
        0, 1, 2, 0, 2, 3,
        5, 4, 6, 5, 6, 7,
        8, 10, 11, 8, 9, 10,
        12, 13, 14, 12, 14, 15,
        16, 17, 19, 17, 18, 19,
        20, 21, 22, 20, 22, 23
    };

    if (!pbrIndexBuffer_.Initialize(&device_, sizeof(indices), D3D12_HEAP_TYPE_UPLOAD)) {
        NEXT_LOG_ERROR("Failed to create PBR index buffer");
        return false;
    }

    if (!pbrIndexBuffer_.UploadData(indices, sizeof(indices))) {
        NEXT_LOG_ERROR("Failed to upload PBR index data");
        return false;
    }

    // Create MVP constant buffer for PBR
    if (!pbrMVPBuffer_.Initialize(&device_, sizeof(Mat4) * 2)) {  // MVP + Model matrix
        NEXT_LOG_ERROR("Failed to create PBR MVP buffer");
        return false;
    }

    // Create material constant buffer
    if (!pbrMaterialBuffer_.Initialize(&device_, sizeof(PBRMaterial))) {
        NEXT_LOG_ERROR("Failed to create PBR material buffer");
        return false;
    }

    // Create lighting constant buffer (GPU-aligned layout)
    if (!pbrLightingBuffer_.Initialize(&device_, sizeof(LightingBufferGPU))) {
        NEXT_LOG_ERROR("Failed to create PBR lighting buffer");
        return false;
    }

    // Initialize PBR sampler
    if (!pbrSampler_.Initialize(&device_, samplerHeap_)) {
        NEXT_LOG_ERROR("Failed to initialize PBR sampler");
        return false;
    }

    DescriptorAllocation pbrSamplerAllocation = descriptorHeapManager_.Allocate(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 1);
    if (pbrSamplerAllocation.count == 0) {
        NEXT_LOG_ERROR("Failed to allocate PBR sampler descriptor");
        return false;
    }

    pbrSampler_.SetGPUDescriptorHandle(pbrSamplerAllocation.gpuHandle);
    if (!pbrSampler_.Create(pbrSamplerAllocation.cpuHandle)) {
        NEXT_LOG_ERROR("Failed to create PBR sampler");
        return false;
    }

    // Enable albedo map sampling if the default texture loaded
    if (texture_.GetResource()) {
        pbrMaterial_.GetMaterialData().setUseAlbedoMap(true);
    }

    // Create indirect command signature for GPU-driven path
    D3D12_INDIRECT_ARGUMENT_DESC argDesc = {};
    argDesc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;

    D3D12_COMMAND_SIGNATURE_DESC sigDesc = {};
    sigDesc.ByteStride = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
    sigDesc.NumArgumentDescs = 1;
    sigDesc.pArgumentDescs = &argDesc;

    hr = device_.GetDevice()->CreateCommandSignature(&sigDesc, nullptr, IID_PPV_ARGS(&pbrIndirectSignature_));
    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create indirect command signature: 0x%X", hr);
        return false;
    }

    if (!pbrIndirectArgs_.Initialize(&device_, sizeof(D3D12_DRAW_INDEXED_ARGUMENTS), D3D12_HEAP_TYPE_UPLOAD)) {
        NEXT_LOG_ERROR("Failed to create indirect args buffer");
        return false;
    }

    NEXT_LOG_INFO("PBR rendering resources created successfully");
    return true;
}

bool DX12Renderer::UpdateLightingBuffers() {
    // Update material buffer
    if (!pbrMaterialBuffer_.UpdateData(&pbrMaterial_.GetMaterialData(), sizeof(PBRMaterial))) {
        return false;
    }

    LightingBufferGPU lighting = {};
    lighting.camera.position[0] = lightingScene_.camera.position.x;
    lighting.camera.position[1] = lightingScene_.camera.position.y;
    lighting.camera.position[2] = lightingScene_.camera.position.z;

    lighting.settings.ambientColor[0] = lightingScene_.settings.ambientColor.x;
    lighting.settings.ambientColor[1] = lightingScene_.settings.ambientColor.y;
    lighting.settings.ambientColor[2] = lightingScene_.settings.ambientColor.z;
    lighting.settings.ambientIntensity = lightingScene_.settings.ambientIntensity;
    lighting.settings.exposure = lightingScene_.settings.exposure;
    lighting.settings.gamma = lightingScene_.settings.gamma;
    lighting.settings.toneMapMode = lightingScene_.settings.toneMapMode;
    lighting.settings.debugViewMode = ToPbrShaderDebugMode(debugViews_.GetDebugMode());

    lighting.directionalLight.direction[0] = lightingScene_.directionalLight.direction.x;
    lighting.directionalLight.direction[1] = lightingScene_.directionalLight.direction.y;
    lighting.directionalLight.direction[2] = lightingScene_.directionalLight.direction.z;
    lighting.directionalLight.intensity = lightingScene_.directionalLight.intensity;
    lighting.directionalLight.color[0] = lightingScene_.directionalLight.color.x;
    lighting.directionalLight.color[1] = lightingScene_.directionalLight.color.y;
    lighting.directionalLight.color[2] = lightingScene_.directionalLight.color.z;
    lighting.directionalLight.enabled = lightingScene_.directionalLight.enabled;

    for (size_t i = 0; i < LightingScene::MAX_POINT_LIGHTS; i++) {
        PointLightGPU& dst = lighting.pointLights[i];
        if (i < lightingScene_.pointLights.size()) {
            const PointLight& src = lightingScene_.pointLights[i];
            dst.position[0] = src.position.x;
            dst.position[1] = src.position.y;
            dst.position[2] = src.position.z;
            dst.intensity = src.intensity;
            dst.color[0] = src.color.x;
            dst.color[1] = src.color.y;
            dst.color[2] = src.color.z;
            dst.radius = src.radius;
            dst.constant = src.constant;
            dst.linearAttenuation = src.linear;
            dst.quadratic = src.quadratic;
            dst.enabled = src.enabled;
        } else {
            dst.enabled = 0;
        }
    }

    int pointCount = static_cast<int>(lightingScene_.pointLights.size());
    if (pointCount > static_cast<int>(LightingScene::MAX_POINT_LIGHTS)) {
        pointCount = static_cast<int>(LightingScene::MAX_POINT_LIGHTS);
    }
    lighting.numPointLights = pointCount;
    lighting.numSpotLights = static_cast<int>(lightingScene_.spotLights.size());

    return pbrLightingBuffer_.UpdateData(&lighting, sizeof(lighting));
}

void DX12Renderer::RenderPBRCube() {
    // Calculate MVP matrix
    Mat4 model = Mat4::RotateX(time_ * 0.5f) * Mat4::RotateY(time_ * 0.7f);

    Vec3 eye(frameDesc_.cameraPosition[0], frameDesc_.cameraPosition[1], frameDesc_.cameraPosition[2]);
    Vec3 target(frameDesc_.cameraTarget[0], frameDesc_.cameraTarget[1], frameDesc_.cameraTarget[2]);
    Vec3 up(frameDesc_.cameraUp[0], frameDesc_.cameraUp[1], frameDesc_.cameraUp[2]);
    Mat4 view = Mat4::LookAt(eye, target, up);

    float aspect = static_cast<float>(width_) / static_cast<float>(height_);
    Mat4 projection = Mat4::Perspective(3.14159f / 4.0f, aspect, 0.1f, 2000.0f);

    Mat4 mvp = projection * view * model;
    Mat4 mvpTransposed = mvp.Transpose();
    Mat4 modelTransposed = model.Transpose();

    // Upload MVP and Model matrices
    Mat4 matrices[2] = {mvpTransposed, modelTransposed};
    if (!pbrMVPBuffer_.UpdateData(matrices, sizeof(matrices))) {
        NEXT_LOG_ERROR("Skipping PBR draw: failed to update MVP buffer");
        return;
    }

    // Set root signature
    commandList_.SetGraphicsRootSignature(pbrRootSignature_.GetRootSignature());

    // Set constant buffers
    commandList_.GetCommandList()->SetGraphicsRootConstantBufferView(0, pbrMVPBuffer_.GetGPUVirtualAddress());
    commandList_.GetCommandList()->SetGraphicsRootConstantBufferView(1, pbrMaterialBuffer_.GetGPUVirtualAddress());
    commandList_.GetCommandList()->SetGraphicsRootConstantBufferView(2, pbrLightingBuffer_.GetGPUVirtualAddress());
    commandList_.GetCommandList()->SetGraphicsRootDescriptorTable(3, pbrTextureAllocation_.gpuHandle);
    commandList_.GetCommandList()->SetGraphicsRootDescriptorTable(4, pbrSampler_.GetGPUDescriptorHandle());

    // Set pipeline state
    const bool wireframe = debugViews_.GetDebugMode() == DebugViewMode::Wireframe;
    commandList_.SetPipelineState(wireframe ? pbrPipelineStateWireframe_.GetPSO() : pbrPipelineState_.GetPSO());

    // Set viewport and scissor rect
    commandList_.RSSetViewports(1, &viewport_);
    commandList_.RSSetScissorRects(1, &scissorRect_);

    // Set primitive topology
    commandList_.IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Set vertex and index buffers (44 bytes per vertex: 3+3+3+2 floats * 4 bytes)
    D3D12_VERTEX_BUFFER_VIEW vbv = pbrVertexBuffer_.GetVertexBufferView(sizeof(float) * 11);
    commandList_.IASetVertexBuffers(0, 1, &vbv);

    D3D12_INDEX_BUFFER_VIEW ibv = pbrIndexBuffer_.GetIndexBufferView(DXGI_FORMAT_R16_UINT);
    commandList_.IASetIndexBuffer(&ibv, DXGI_FORMAT_R16_UINT);

    if (useGpuDriven_ && pbrIndirectSignature_ && pbrIndirectArgs_.GetResource()) {
        D3D12_DRAW_INDEXED_ARGUMENTS args = {};
        args.IndexCountPerInstance = NumCubeIndices;
        args.InstanceCount = 1;
        args.StartIndexLocation = 0;
        args.BaseVertexLocation = 0;
        args.StartInstanceLocation = 0;
        pbrIndirectArgs_.UploadData(&args, sizeof(args));

        commandList_.ExecuteIndirect(
            pbrIndirectSignature_.Get(),
            1,
            pbrIndirectArgs_.GetResource(),
            0);
    } else {
        // Draw indexed cube
        commandList_.DrawIndexedInstanced(NumCubeIndices, 1, 0, 0, 0);
    }
}

void DX12Renderer::RenderMeshShaderDebug() {
    if (!meshShaderDebugEnabled_) {
        return;
    }

    meshShaderDebugPass_.Render(commandList_.GetCommandList(), 1);
}

void DX12Renderer::RenderSamplerFeedbackDebug() {
    if (!samplerFeedbackDebugEnabled_ ||
        !samplerFeedbackPSO_ ||
        !samplerFeedbackMap_ ||
        samplerFeedbackUavAllocation_.count == 0 ||
        !srvHeap_ ||
        !samplerHeap_ ||
        !texture_.GetResource()) {
        return;
    }

    D3D12_RESOURCE_BARRIER textureBarrier = {};
    textureBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    textureBarrier.Transition.pResource = texture_.GetResource();
    textureBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    textureBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    textureBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList_.ResourceBarrier(1, &textureBarrier);

    ID3D12DescriptorHeap* heaps[] = {
        srvHeap_->GetHeap(),
        samplerHeap_->GetHeap()
    };
    commandList_.GetCommandList()->SetDescriptorHeaps(2, heaps);
    commandList_.GetCommandList()->SetComputeRootSignature(samplerFeedbackRootSignature_.GetRootSignature());
    commandList_.GetCommandList()->SetPipelineState(samplerFeedbackPSO_.Get());
    commandList_.GetCommandList()->SetComputeRootDescriptorTable(0, texture_.GetGPUDescriptorHandle());
    commandList_.GetCommandList()->SetComputeRootDescriptorTable(1, samplerFeedbackUavAllocation_.gpuHandle);

    const UINT groupsX = std::max<UINT>(1, (texture_.GetWidth() + 7) / 8);
    const UINT groupsY = std::max<UINT>(1, (texture_.GetHeight() + 7) / 8);
    commandList_.GetCommandList()->Dispatch(groupsX, groupsY, 1);

    if (!samplerFeedbackDispatchLogged_) {
        NEXT_LOG_INFO("Sampler feedback pass dispatched");
        samplerFeedbackDispatchLogged_ = true;
    }

    D3D12_RESOURCE_BARRIER uavBarrier = {};
    uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarrier.UAV.pResource = samplerFeedbackMap_.Get();
    commandList_.ResourceBarrier(1, &uavBarrier);

    textureBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    textureBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    commandList_.ResourceBarrier(1, &textureBarrier);
}

void DX12Renderer::RenderDebugCells() {
    const size_t cellCount = frameDesc_.RenderedDebugCellCount();
    if (cellCount == 0) {
        debugCellIndexCount_ = 0;
        giManager_.SetRayTracingSceneGeometry(
            pbrVertexBuffer_.GetResource(),
            24,
            sizeof(float) * 11,
            pbrIndexBuffer_.GetResource(),
            NumCubeIndices,
            DXGI_FORMAT_R16_UINT,
            false);
        return;
    }

    std::vector<DebugCellVertex> vertices;
    std::vector<uint16_t> indices;
    vertices.reserve(cellCount * 4);
    indices.reserve(cellCount * 12);

    for (size_t i = 0; i < cellCount; ++i) {
        const RendererDebugCell& cell = frameDesc_.debugCells[i];
        const float halfSize = std::max(1.0f, cell.size) * 0.47f;
        const float y = cell.center[1] - 1.0f;
        const bool placeholder = cell.IsPlaceholder();
        const float color[4] = {
            placeholder ? 1.0f : 0.15f,
            placeholder ? 0.64f : 0.82f,
            placeholder ? 0.18f : 0.58f,
            1.0f
        };

        const uint16_t base = static_cast<uint16_t>(vertices.size());
        const float x0 = cell.center[0] - halfSize;
        const float x1 = cell.center[0] + halfSize;
        const float z0 = cell.center[2] - halfSize;
        const float z1 = cell.center[2] + halfSize;

        vertices.push_back({{x0, y, z0}, {color[0], color[1], color[2], color[3]}, {0.0f, 0.0f}});
        vertices.push_back({{x0, y, z1}, {color[0], color[1], color[2], color[3]}, {0.0f, 1.0f}});
        vertices.push_back({{x1, y, z1}, {color[0], color[1], color[2], color[3]}, {1.0f, 1.0f}});
        vertices.push_back({{x1, y, z0}, {color[0], color[1], color[2], color[3]}, {1.0f, 0.0f}});

        indices.push_back(base + 0);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
        indices.push_back(base + 0);
        indices.push_back(base + 2);
        indices.push_back(base + 3);
        indices.push_back(base + 0);
        indices.push_back(base + 2);
        indices.push_back(base + 1);
        indices.push_back(base + 0);
        indices.push_back(base + 3);
        indices.push_back(base + 2);
    }

    debugCellIndexCount_ = static_cast<UINT>(indices.size());
    if (!debugCellVertexBuffer_.UploadData(vertices.data(), vertices.size() * sizeof(DebugCellVertex))) {
        debugCellIndexCount_ = 0;
        return;
    }
    if (!debugCellIndexBuffer_.UploadData(indices.data(), indices.size() * sizeof(uint16_t))) {
        debugCellIndexCount_ = 0;
        return;
    }
    giManager_.SetRayTracingSceneGeometry(
        debugCellVertexBuffer_.GetResource(),
        static_cast<uint32_t>(vertices.size()),
        sizeof(DebugCellVertex),
        debugCellIndexBuffer_.GetResource(),
        static_cast<uint32_t>(indices.size()),
        DXGI_FORMAT_R16_UINT);

    Vec3 eye(frameDesc_.cameraPosition[0], frameDesc_.cameraPosition[1], frameDesc_.cameraPosition[2]);
    Vec3 target(frameDesc_.cameraTarget[0], frameDesc_.cameraTarget[1], frameDesc_.cameraTarget[2]);
    Vec3 up(frameDesc_.cameraUp[0], frameDesc_.cameraUp[1], frameDesc_.cameraUp[2]);

    const float aspect = height_ > 0 ? static_cast<float>(width_) / static_cast<float>(height_) : 1.0f;
    CubeConstants constants = {};
    constants.model = Mat4::Identity().Transpose();
    constants.view = Mat4::LookAt(eye, target, up).Transpose();
    constants.projection = Mat4::Perspective(3.14159f / 4.0f, aspect, 0.1f, 2000.0f).Transpose();
    constants.time = time_;
    if (!debugCellFrameBuffer_.UpdateData(&constants, sizeof(constants))) {
        debugCellIndexCount_ = 0;
        return;
    }

    commandList_.SetGraphicsRootSignature(rootSignature_.GetRootSignature());
    commandList_.GetCommandList()->SetGraphicsRootConstantBufferView(0, debugCellFrameBuffer_.GetGPUVirtualAddress());
    commandList_.GetCommandList()->SetGraphicsRootDescriptorTable(1, texture_.GetGPUDescriptorHandle());
    commandList_.GetCommandList()->SetGraphicsRootDescriptorTable(2, sampler_.GetGPUDescriptorHandle());
    commandList_.SetPipelineState(pipelineState_.GetPSO());
    commandList_.RSSetViewports(1, &viewport_);
    commandList_.RSSetScissorRects(1, &scissorRect_);
    commandList_.IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    D3D12_VERTEX_BUFFER_VIEW vbv = debugCellVertexBuffer_.GetVertexBufferView(sizeof(DebugCellVertex));
    commandList_.IASetVertexBuffers(0, 1, &vbv);

    D3D12_INDEX_BUFFER_VIEW ibv = debugCellIndexBuffer_.GetIndexBufferView(DXGI_FORMAT_R16_UINT);
    commandList_.IASetIndexBuffer(&ibv, DXGI_FORMAT_R16_UINT);
    commandList_.DrawIndexedInstanced(debugCellIndexCount_, 1, 0, 0, 0);
}

} // namespace Next
