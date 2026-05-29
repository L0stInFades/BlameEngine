# 开发交接：CP4 → CP5 (DX12U)

## 1. 当前状态（CP4 已完成）
- Runtime World ECS 架构已实现并验证
  - ✅ Entity 系统（带版本号，避免重用问题）
  - ✅ Component 系统（类型安全、高效）
  - ✅ Query 系统（基础实现）
  - ✅ System 框架（可扩展）
  - ✅ 性能测试通过（10,000 实体 @ 118 FPS）
  - ✅ MeshRendererComponent 集成 Asset Pipeline

## 2. CP5 目标（渲染基线 - DX12U + Render Graph）

### 核心目标
基于 **DirectX 12 Ultimate** 实现现代渲染基线和 Render Graph 架构。

### DX12U 优势
- ✅ **最新 API**: DirectX 12 Ultimate (Feature Level 12.2+)
- ✅ **Mesh Shaders**: GPU 驱动几何处理
- ✅ **Variable Rate Shading (VRS)**: 性能优化
- ✅ **Enhanced Barriers**: 更精确的资源同步
- ✅ **Work Graphs**: GPU 计算图（可选，R&D）
- ✅ **Ray Tracing**: DXR 1.1（Tier B/C 稳定后）

### 验收标准
- ✅ DX12U RHI 基础（设备、命令队列、命令列表）
- ✅ Render Graph 框架（Pass、Resource、Barrier）
- ✅ 基础 PBR 材质（Albedo + Normal + Metallic/Roughness）
- ✅ 阴影映射（PCF 或 CSM）
- ✅ 后处理（ACES 色调映射 + FXAA）
- ✅ 调试视图（线框、法线、深度、Albedo）
- ✅ Golden Image 对比（固定场景）
- ✅ 60 FPS @ 1080p

## 3. DX12U 技术栈

### 3.1 环境要求
```cpp
// DX12U 版本要求
D3D12_FEATURE_DATA_FEATURE_LEVELS featureLevels = {};
featureLevels.HighestFeatureLevel = D3D_FEATURE_LEVEL_12_2; // DX12U

// 关键特性检查
D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
options5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_1;  // DXR 1.1
options5.MeshShaderTier >= D3D12_MESH_SHADER_TIER_1;     // Mesh Shaders
options5.VariableShadingTier >= D3D12_VARIABLE_SHADING_TIER_2; // VRS
```

### 3.2 DX12U 核心组件

**1. 增强 Barrier 模型 (Enhanced Barriers)**
```cpp
// DX12U 引入的新 Barrier API
CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
    pResource,
    D3D12_RESOURCE_STATE_COMMON,
    D3D12_RESOURCE_STATE_RENDER_TARGET,
    D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
    D3D12_RESOURCE_BARRIER_FLAG_NONE);

// 全局 Barrier（用于 UAV/Heap 同步）
CD3DX12_RESOURCE_BARRIER globalBarrier = CD3DX12_RESOURCE_BARRIER::UAV(
    pResource  // nullptr 表示全局同步
);
```

**2. Mesh Shaders（可选，性能优化）**
```cpp
// Mesh Shader 管线状态
D3D12_MESH_SHADER_MESH_SHADER meshShader = {};
D3D12_MESH_SHADER_PIXEL_SHADER pixelShader = {};

// 使用 Mesh Shader 替代传统 VS/GS/IA
// 优势：GPU 驱动 culling，减少 CPU 开销
```

**3. Variable Rate Shading (VRS)**
```cpp
// VRS 用于性能优化：降低边缘或低优先级区域的着色率
D3D12_SHADING_RATE_COMBINATION shadingRates[] = {
    D3D12_SHADING_RATE_COMBINATION(D3D12_SHADING_RATE_1X1, D3D12_SHADING_RATE_1X1),
    D3D12_SHADING_RATE_COMBINATION(D3D12_SHADING_RATE_2X2, D3D12_SHADING_RATE_2X2),
};
```

## 4. 建议的实现路线

### Phase 1: DX12U RHI 基础（2-3 天）

**1.1 设备初始化**
```cpp
// engine/renderer/include/next/renderer/dx12/device.h
class DX12Device {
public:
    bool Initialize();

    ID3D12Device* GetDevice() const { return device_.Get(); }
    IDXGIFactory4* GetFactory() const { return factory_.Get(); }

    // 检查 DX12U 特性支持
    bool SupportsMeshShaders() const;
    bool SupportsRayTracing() const;
    bool SupportsVRS() const;

private:
    Microsoft::WRL::ComPtr<ID3D12Device5> device_;  // ID3D12Device5 = DX12U
    Microsoft::WRL::ComPtr<IDXGIFactory4> factory_;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue_;
};
```

**1.2 命令列表和队列**
```cpp
// engine/renderer/include/next/renderer/dx12/command_list.h
class DX12CommandList {
public:
    void Reset();
    void Close();
    void OMSetRenderTargets(...);
    void SetPipelineState(ID3D12PipelineState* pso);
    void SetGraphicsRootSignature(ID3D12RootSignature* rootSig);
    void DrawIndexedInstanced(...);

    // DX12U 特性
    void SetMeshShaders(...);           // Mesh Shader
    void SetShadingRate(...);           // VRS
    void RSSetShadingRate(...);         // VRS

private:
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> commandList_;  // ID3D12GraphicsCommandList4 = DX12U
};
```

**1.3 资源管理**
```cpp
// engine/renderer/include/next/renderer/dx12/resource.h
struct DX12Buffer {
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    D3D12_RESOURCE_STATES currentState;
    size_t size;
    D3D12_HEAP_TYPE heapType;
};

struct DX12Texture {
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    D3D12_RESOURCE_STATES currentState;
    uint32_t width, height, depthOrArraySize;
    DXGI_FORMAT format;
    D3D12_RESOURCE_FLAGS flags;

    // DX12U 特性
    D3D12_SHADING_RATE shadingRate;  // VRS 纹理
};
```

### Phase 2: Render Graph（2 天）

**2.1 Render Pass 基类**
```cpp
// engine/renderer/include/next/renderer/render_pass.h
class RenderPass {
public:
    virtual ~RenderPass() = default;

    virtual void Execute(DX12CommandList* cmdList) = 0;
    virtual const char* GetName() const = 0;

    // 资源依赖
    virtual std::vector<ResourceID> GetReadResources() = 0;
    virtual std::vector<ResourceID> GetWriteResources() = 0;

    // Barrier 描述（使用 DX12U Enhanced Barriers）
    virtual std::vector<CD3DX12_RESOURCE_BARRIER> GetBarriers() = 0;
};
```

**2.2 Render Graph**
```cpp
// engine/renderer/include/next/renderer/render_graph.h
class RenderGraph {
public:
    void AddPass(RenderPass* pass);
    void Compile();  // 解析依赖、插入 Barrier、优化
    void Execute(DX12CommandList* cmdList);

    // 资源管理
    ResourceID CreateBuffer(const BufferDesc& desc, const char* name = nullptr);
    ResourceID CreateTexture(const TextureDesc& desc, const char* name = nullptr);
    ResourceID CreateSwapchain(HWND hwnd, uint32_t width, uint32_t height);

    DX12Texture* GetTexture(ResourceID id);
    DX12Buffer* GetBuffer(ResourceID id);

private:
    std::vector<std::unique_ptr<RenderPass>> passes_;
    std::unordered_map<ResourceID, std::unique_ptr<DX12Resource>> resources_;

    // DX12U Barrier 优化
    void OptimizeBarriers();
};
```

**2.3 基础 Pass 实现**
```cpp
// Shadow Pass - 使用 PCF
class ShadowPass : public RenderPass {
public:
    void Execute(DX12CommandList* cmdList) override;
    const char* GetName() const override { return "ShadowPass"; }
};

// Geometry Pass - GBuffer 生成
class GeometryPass : public RenderPass {
public:
    void Execute(DX12CommandList* cmdList) override;
    const char* GetName() const override { return "GeometryPass"; }
};

// Lighting Pass - 延迟光照
class LightingPass : public RenderPass {
public:
    void Execute(DX12CommandList* cmdList) override;
    const char* GetName() const override { return "LightingPass"; }
};

// Post Process Pass - ACES + FXAA
class PostProcessPass : public RenderPass {
public:
    void Execute(DX12CommandList* cmdList) override;
    const char* GetName() const override { return "PostProcessPass"; }
};
```

### Phase 3: PBR 材质系统（2 天）

**3.1 材质结构**
```cpp
// engine/renderer/include/next/renderer/material.h
struct PBRMaterial {
    // 基础参数
    glm::vec3 albedo = glm::vec3(1.0f);
    float metallic = 0.0f;
    float roughness = 0.5f;
    float ao = 1.0f;

    // 纹理
    DX12Texture* albedoMap = nullptr;
    DX12Texture* normalMap = nullptr;
    DX12Texture* metallicMap = nullptr;
    DX12Texture* roughnessMap = nullptr;
    DX12Texture* aoMap = nullptr;
    DX12Texture* emissiveMap = nullptr;

    // 扩展
    glm::vec3 emissive = glm::vec3(0.0f);
    float normalScale = 1.0f;
    float alphaCutoff = 0.5f;
    bool twoSided = false;
};
```

**3.2 Shader 集成**
```cpp
// HLSL Shaders（使用 DX12U 特性）
// shaders/pbr.hlsl

// Enhanced Barriers 的使用示例
Texture2D tAlbedo : register(t0);
SamplerState sAlbedo : register(s0);

// Root Signature（DX12U 优化）
RootSignature RS = {
    // CBV + Tables
    CBV(b0, space = 0),           // Camera constants
    SRV(t0, space = 0, numDescriptors = 5),  // Textures
    Sampler(s0, space = 0, numDescriptors = 1), // Samplers
};
```

### Phase 4: ECS 集成（1 天）

**4.1 渲染系统**
```cpp
// game/song/src/render_system.cpp
class RenderSystem : public Next::System {
public:
    void Update(float deltaTime) override {
        // 从 ECS 查询可渲染实体
        auto renderables = world_->QueryEntitiesWith<
            Next::TransformComponent,
            Next::MeshRendererComponent
        >();

        for (auto entity : renderables) {
            auto* transform = world_->GetComponent<Next::TransformComponent>(entity);
            auto* renderer = world_->GetComponent<Next::MeshRendererComponent>(entity);

            // 获取 Asset Pipeline 的资源
            auto& assetManager = Next::AssetManager::Instance();
            // 提交渲染命令
        }
    }
};
```

### Phase 5: VRS 优化（可选，R&D）

```cpp
// Variable Rate Shading 实现
class VRSPass : public RenderPass {
public:
    void Execute(DX12CommandList* cmdList) override {
        // 生成 Shading Rate Map
        GenerateShadingRateMap(cmdList);

        // 设置 VRS
        cmdList->RSSetShadingRate(
            D3D12_SHADING_RATE_2X2,  // 边缘 2x2
            nullptr                    // 或使用 Shading Rate Image
        );
    }
};
```

### Phase 6: 调试和验证（1 天）

**6.1 调试视图**
- 线框模式 (`PolygonMode = Wireframe`)
- 法线可视化（Vertex Normal 可视化）
- 深度可视化（Depth Buffer 可视化）
- Albedo 可视化（基础颜色）
- Metallic/Roughness 可视化

**6.2 Golden Image**
```cpp
// 固定场景截图测试
void RunGoldenImageTest() {
    // 设置固定相机位置
    // 渲染一帧
    // 保存截图
    // 与参考图像对比
    // 报告差异
}
```

## 5. DX12U 特性利用

### 5.1 第一阶段（必须实现）
- ✅ **ID3D12Device5**: DX12U 设备接口
- ✅ **Enhanced Barriers**: 精确资源同步
- ✅ **Render Target**: SRGB 格式支持

### 5.2 第二阶段（性能优化）
- ✅ **Variable Rate Shading**: 降低着色开销
- ✅ **Descriptor Indexing**: 动态资源绑定
- ✅ **Shader Model 6.6**: 最新 HLSL 特性

### 5.3 第三阶段（R&D，可选）
- 🔬 **Mesh Shaders**: GPU culling
- 🔬 **Ray Tracing (DXR 1.1)**: 光追反射/阴影
- 🔬 **Work Graphs**: GPU 计算图
- 🔬 **Sampler Feedback**: 纹理流式优化

## 6. 文件结构

```
engine/renderer/
├── include/next/renderer/
│   ├── rhi.h                      # RHI 抽象接口
│   ├── dx12/
│   │   ├── device.h               # DX12U 设备
│   │   ├── command_queue.h        # 命令队列
│   │   ├── command_list.h         # 命令列表（ID3D12GraphicsCommandList4）
│   │   ├── resources.h            # 资源（Buffer/Texture）
│   │   ├── descriptors.h          # 描述符堆/表
│   │   ├── shaders.h              # 着色器管理
│   │   ├── swapchain.h            # 交换链
│   │   └── helpers.h              # DX12U 辅助宏
│   ├── render_graph.h             # Render Graph
│   ├── render_pass.h              # Pass 基类
│   ├── material.h                 # PBR 材质
│   └── mesh.h                     # 网格数据
└── src/
    ├── dx12/
    │   ├── device.cpp
    │   ├── command_queue.cpp
    │   ├── command_list.cpp
    │   ├── resources.cpp
    │   ├── descriptors.cpp
    │   ├── shaders.cpp
    │   └── swapchain.cpp
    ├── render_graph.cpp
    ├── material.cpp
    └── mesh.cpp

shaders/
├── pbr.hlsl                       # PBR 着色器（SM 6.6）
├── shadow.hlsl                    # 阴影着色器
├── postprocess.hlsl               # 后处理着色器
└── debug.hlsl                     # 调试可视化着色器
```

## 7. 技术债和风险

### 7.1 DX12U 学习曲线
- 显式资源管理复杂度高
- Barrier 同步容易出错
- 描述符堆布局需要规划

### 7.2 性能风险
- Draw Call 开销（需要批处理）
- 资源绑定瓶颈（需要 Root Signature 优化）
- GPU 空闲（需要异步执行）

### 7.3 兼容性
- 需要检查硬件支持 DX12U
- Fallback 方案（DX11 或 DX12 Feature Level 11_0）

## 8. 验收口径（CP5）

- ✅ DX12U 初始化成功（ID3D12Device5）
- ✅ 渲染三角形（白色背景）
- ✅ PBR 材质正确显示（从 Asset Pipeline 加载）
- ✅ 阴影映射工作正常
- ✅ 后处理效果可见（ACES 色调映射）
- ✅ 调试视图正常（5 种可视化模式）
- ✅ Golden Image 通过（像素差异 < 1%）
- ✅ 60 FPS @ 1080p (10k 实体)

## 9. 性能目标

| 指标 | 目标 | 说明 |
|------|------|------|
| Draw Call | < 100 | 10k 实体合批后 |
| GPU Time | < 16.6ms | 60 FPS |
| CPU Time | < 5ms | ECS + 提交 |
| Memory | < 500MB | 纹理 + Buffer |
| Triangle | ~100k | 10k 实体 @ 10 三角形/实体 |

## 10. 后续优化（CP5 之后）

- GPU Culling（Mesh Shaders）
- Ray Traced Shadows（DXR 1.1）
- Global Illumination（可选）
- Async Compute（并行计算和渲染）
- Multi-threaded Command Recording（Job System）

---

**文档版本**: 1.0 (DX12U)
**创建时间**: 2026-01-13
**预计工期**: CP5 - 5-7 天（DX12U 渲染基线）
**技术栈**: DirectX 12 Ultimate + Render Graph + PBR + ECS
**下一里程碑**: CP5 完成后 → CP6 手感与运镜
