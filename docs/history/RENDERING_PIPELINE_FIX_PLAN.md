# 渲染管线关键问题修复计划

**日期**: 2026-01-15
**类型**: 关键问题修复
**状态**: 🔴 高优先级

## 📊 问题清单

### 问题 1: SRV 覆盖问题 🔴 P0

**症状**: 多个材质共享同一个描述符槽位，导致纹理相互覆盖

**根本原因**:
- 所有材质使用同一个 SRV heap
- 描述符分配后，GPU 句柄计算错误
- 缺少描述符表基准点管理

**影响**:
- 纹理渲染错误
- 多材质场景崩溃
- 内存损坏

### 问题 2: Shader/CB Layout 对齐问题 🔴 P0

**症状**: Root Signature 与 Shader register 不匹配

**具体问题**:

#### PBR Renderer Layout
```cpp
// Root Signature 定义 (pbr_renderer.cpp)
rootParameters[0] = b0, space0, VS  // Transform
rootParameters[1] = b0, space0, PS  // Material ❌ 冲突！
rootParameters[2] = b1, space0, PS  // Lighting
```

#### PBR Shader 定义
```hlsl
// pbr.vs.hlsl
cbuffer ConstantBuffer : register(b0, space0) { ... }  // ✅ 匹配

// pbr.ps.hlsl
cbuffer MaterialBuffer : register(b0) { ... }  // ❌ 应该是 space1
cbuffer LightingBuffer : register(b1) { ... }  // ⚠️ 缺少 space
```

**问题**:
1. VS 和 PS 都使用 `register(b0, space0)` - 在同一空间冲突
2. PS 的 MaterialBuffer 和 LightingBuffer 都使用 `space0` - 缺少空间隔离
3. 缺少明确的 register space 声明

**影响**:
- 常量缓冲区数据错误
- 着色器参数混乱
- 渲染结果不正确

### 问题 3: 描述符管理缺失 ⚠️ P1

**症状**: 缺少统一的描述符表基准点管理

**缺失功能**:
- 描述符表基准点跟踪
- 描述符表布局管理
- 多材质支持

## 🎯 修复方案

### 修复 1: 修复 SRV 覆盖问题

#### 方案: 使用描述符分配器的偏移

**修改文件**: `engine/renderer/src/dx12/texture.cpp`

```cpp
bool DX12Texture::CreateShaderResourceView(DXGI_FORMAT format, D3D12_CPU_DESCRIPTOR_HANDLE cpuDescriptor) {
    // 使用提供的 CPU 描述符句柄
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = cpuDescriptor;

    // 计算 GPU 句柄 - 直接从分配中获取
    // 不再从 heap 基准点推导，而是使用分配器提供的正确句柄

    D3D12_CPU_DESCRIPTOR_HANDLE cpuBase = srvHeap_->GetCPUDescriptorHandle(0);
    D3D12_GPU_DESCRIPTOR_HANDLE gpuBase = srvHeap_->GetGPUDescriptorHandle(0);
    UINT descriptorSize = device_->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // 计算偏移
    UINT64 offset = (cpuHandle.ptr - cpuBase.ptr) / descriptorSize;
    gpuDescriptorHandle_.ptr = gpuBase.ptr + offset * descriptorSize;

    // 创建 SRV
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.PlaneSlice = 0;
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

    device_->GetDevice()->CreateShaderResourceView(texture_.Get(), &srvDesc, cpuHandle);

    return true;
}
```

**关键改进**:
1. 使用描述符分配器提供的正确 GPU 句柄
2. 不再假设描述符来自堆的基准点
3. 每个纹理都有独立的描述符槽位

### 修复 2: 校准 Shader/CB Layout

#### 2.1 修复 PBR Shader Register Spaces

**修改文件**: `engine/renderer/shaders/pbr.vs.hlsl`

```hlsl
// PBR Vertex Shader
// 使用 space0 避免与像素着色器冲突

cbuffer TransformBuffer : register(b0, space0) {
    float4x4 mvp;
    float4x4 model;
};
```

**修改文件**: `engine/renderer/shaders/pbr.ps.hlsl`

```hlsl
// PBR Pixel Shader
// 使用 space1 与顶点着色器隔离

// Material parameters - space1, register 0
cbuffer MaterialBuffer : register(b0, space1) {
    PBRMaterial material;
};

// Lighting parameters - space1, register 1
cbuffer LightingBuffer : register(b1, space1) {
    CameraData camera;
    LightingSettings settings;
    DirectionalLight directionalLight;
    PointLight pointLights[4];
    int numPointLights;
    int numSpotLights;
};
```

#### 2.2 修复 PBR Root Signature

**修改文件**: `engine/renderer/src/dx12/pbr_renderer.cpp`

```cpp
bool PBRRenderer::CreateRootSignature() {
    D3D12_ROOT_PARAMETER rootParameters[3];

    // Parameter 0: Transform buffer (vertex shader, space0, b0)
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[0].Descriptor.ShaderRegister = 0;
    rootParameters[0].Descriptor.RegisterSpace = 0;  // space0
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    // Parameter 1: Material buffer (pixel shader, space1, b0)
    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[1].Descriptor.ShaderRegister = 0;
    rootParameters[1].Descriptor.RegisterSpace = 1;  // space1
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // Parameter 2: Lighting buffer (pixel shader, space1, b1)
    rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[2].Descriptor.ShaderRegister = 1;
    rootParameters[2].Descriptor.RegisterSpace = 1;  // space1
    rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
    rootSignatureDesc.NumParameters = 3;
    rootSignatureDesc.pParameters = rootParameters;
    rootSignatureDesc.NumStaticSamplers = 0;
    rootSignatureDesc.pStaticSamplers = nullptr;
    rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    // 序列化和创建...
}
```

**关键改进**:
1. 明确使用 register space 隔离 VS 和 PS
2. MaterialBuffer 使用 space1, b0
3. LightingBuffer 使用 space1, b1
4. 消除所有 register 冲突

#### 2.3 修复 Cube Shader Layout

**修改文件**: `engine/renderer/shaders/cube.vs.hlsl`

```hlsl
// Cube Vertex Shader
// 简单的 MVP 变换，使用 space0

cbuffer ConstantBuffer : register(b0, space0) {
    float4x4 modelMatrix;
    float4x4 viewMatrix;
    float4x4 projectionMatrix;
    float time;
    float padding[3];
};
```

### 修复 3: 添加描述符表管理

**新增文件**: `engine/renderer/include/next/renderer/dx12/descriptor_table_manager.h`

```cpp
#pragma once

#include "next/renderer/dx12/device.h"
#include "next/renderer/dx12/descriptor_allocator.h"
#include <d3d12.h>
#include <wrl/client.h>
#include <vector>

namespace Next {

/**
 * @brief 描述符表布局管理器
 *
 * 管理渲染管线中的描述符表布局，确保：
 * - 每个材质有独立的描述符槽位
 * - 描述符表基准点正确
 * - GPU 句柄计算正确
 */
class DescriptorTableManager {
public:
    DescriptorTableManager();
    ~DescriptorTableManager();

    bool Initialize(DX12Device* device, DX12DescriptorHeapManager* heapManager);
    void Shutdown();

    /**
     * @brief 为材质分配描述符表
     * @param materialId 材质ID
     * @param numDescriptors 需要的描述符数量（纹理数量）
     * @return 描述符表 GPU 基准点
     */
    D3D12_GPU_DESCRIPTOR_HANDLE AllocateMaterialTable(
        const std::string& materialId,
        UINT numDescriptors);

    /**
     * @brief 获取材质的描述符表基准点
     */
    D3D12_GPU_DESCRIPTOR_HANDLE GetMaterialTable(const std::string& materialId) const;

    /**
     * @brief 释放材质的描述符表
     */
    void ReleaseMaterialTable(const std::string& materialId);

    /**
     * @brief 获取 SRV heap 的基准点（用于设置描述符表）
     */
    D3D12_GPU_DESCRIPTOR_HANDLE GetSRVHeapBase() const;

private:
    struct MaterialTableInfo {
        std::string materialId;
        D3D12_GPU_DESCRIPTOR_HANDLE gpuBaseHandle;
        DescriptorAllocation allocation;
        UINT numDescriptors;
    };

    DX12Device* device_;
    DX12DescriptorHeapManager* heapManager_;

    std::vector<MaterialTableInfo> materialTables_;

    D3D12_GPU_DESCRIPTOR_HANDLE srvHeapBase_;

    bool initialized_;
};

} // namespace Next
```

## 📅 实施计划

### Phase 1: 修复 SRV 覆盖（1 天）

**任务**:
1. ✅ 分析问题根源
2. [ ] 修复 texture.cpp 的 GPU 句柄计算
3. [ ] 更新材质系统使用正确的描述符
4. [ ] 测试多材质场景

### Phase 2: 校准 CB Layout（1 天）

**任务**:
1. [ ] 修复 pbr.vs.hlsl register space
2. [ ] 修复 pbr.ps.hlsl register space
3. [ ] 修复 pbr_renderer.cpp Root Signature
4. [ ] 测试 PBR 渲染

### Phase 3: 添加描述符管理（1 天）

**任务**:
1. [ ] 创建 DescriptorTableManager
2. [ ] 集成到渲染管线
3. [ ] 测试多材质支持

## 🎯 验收标准

### SRV 覆盖修复
- ✅ 多材质场景无覆盖
- ✅ 每个材质有独立槽位
- ✅ GPU 句柄计算正确

### CB Layout 校准
- ✅ 所有 register space 明确声明
- ✅ VS 和 PS 无冲突
- ✅ Root Signature 匹配 Shader

### 稳定性
- ✅ 无渲染错误
- ✅ 无内存泄漏
- ✅ 性能无明显下降

## 📊 测试计划

### 测试场景 1: 单材质立方体
```cpp
// 测试单个材质渲染
PBRMaterial material;
material.albedo = Vec3(1.0f, 0.0f, 0.0f);
material.metallic = 0.5f;
material.roughness = 0.5f;

// 预期: 红色金属立方体
```

### 测试场景 2: 多材质场景
```cpp
// 测试多个材质
PBRMaterial mat1, mat2, mat3;
mat1.albedo = Vec3(1.0f, 0.0f, 0.0f);  // Red
mat2.albedo = Vec3(0.0f, 1.0f, 0.0f);  // Green
mat3.albedo = Vec3(0.0f, 0.0f, 1.0f);  // Blue

// 预期: 三个不同颜色的立方体，无覆盖
```

### 测试场景 3: 带纹理的材质
```cpp
// 测试纹理加载
material.LoadAlbedoMap(L"texture.png", queue);

// 预期: 纹理正确显示，无崩溃
```

## 📝 注意事项

### DX12 Register Space 规则
1. **不同着色器阶段**可以使用相同的 register（如果使用不同的 space）
2. **同一着色器阶段**必须使用不同的 register 或 space
3. **推荐**: VS 使用 space0, PS 使用 space1
4. **常量缓冲区**: 通常使用 CBV（b 寄存器）
5. **纹理**: 使用 SRV（t 寄存器）
6. **采样器**: 使用 Sampler（s 寄存器）

### 描述符堆规则
1. **CBV_SRV_UAV heap**: 可以混合 CBV、SRV、UAV
2. **Sampler heap**: 只包含采样器
3. **RTV heap**: 只包含 RTV
4. **DSV heap**: 只包含 DSV
5. **描述符表**: 描述符堆的连续视图

### 对齐规则
1. **16 字节对齐**: 所有 CBV 成员必须 16 字节对齐
2. **结构体打包**: 使用 float4 代替 float3 避免对齐问题
3. **数组对齐**: 数组元素也需要 16 字节对齐

## 🚀 后续优化

### 短期（本周）
1. 修复所有已知问题
2. 添加回归测试
3. 性能基准测试

### 中期（2 周）
1. 添加纹理支持
2. 实现多材质管理
3. 优化描述符分配

### 长期（1 月）
1. 实现绑定less纹理
2. 优化描述符堆管理
3. 添加性能分析工具

---

**文档版本**: 1.0
**创建时间**: 2026-01-15
**状态**: 计划制定完成
**下一步**: 开始 Phase 1 - 修复 SRV 覆盖问题
