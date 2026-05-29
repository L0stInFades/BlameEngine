# 渲染模块待办提示文档

以下清单汇总当前渲染模块的**全部待办事项**（按优先级分组），用于修复关键缺陷并完善功能。

## P0（阻塞性问题，必须优先修复）
1. 解决 `DX12Renderer` ODR 冲突：`include/next/renderer/dx12/dx12_renderer.h` 与 `include/next/renderer/dx12/renderer.h` 同名类定义冲突，需删除/重命名其中一套。
2. 实现正确的 RTV/DSV 绑定：
   - 实现 `DX12CommandList::OMSetRenderTargets`（使用 CPU descriptor handle，而非 `ID3D12Resource*`）。
   - 在 `dx12_renderer.cpp` / `renderer.cpp` 中传入正确 RTV/DSV 句柄。
3. 绑定 Descriptor Heaps：在 `BeginFrame` 或每次渲染前调用 `SetDescriptorHeaps`（CBV_SRV_UAV + Sampler）。
4. 修复 Swapchain Resize：
   - `ResizeBuffers` 前等待 GPU 完成（`commandQueue->WaitForGPU()`）。
   - 检查 tearing 支持再使用 `DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING`。
   - Resize 后重建 RTV（调用 `CreateRenderTargetViews()`）。
   - 建议调用 `MakeWindowAssociation` 禁用 Alt+Enter。
5. 修复 `DepthBuffer::Resize`：避免 `Shutdown()` 清空指针后再 `Initialize`，并传入正确 `width/height`。
6. 修复 SRV 覆盖问题：
   - `DX12Texture::CreateShaderResourceView` 不得固定写入 slot 0。
   - 所有纹理必须通过 `DX12DescriptorAllocator/Manager` 分配。
7. 统一材质分配接口：`DX12Material::Initialize` 需要 `DX12DescriptorHeapManager`，需在 `dx12_renderer.cpp` 中引入并替换 `DX12DescriptorHeap*`。
8. 纹理上传同步：替换 `WaitForSingleObject(5000)` 的临时同步，使用共享 Fence + `WaitForGPU` 机制。

## P1（正确性与稳定性）
1. 修复 cube 常量缓冲布局：`cube.vs.hlsl` 需要 model/view/projection/time，但 `DX12Renderer::UpdateConstantBuffer` 仅上传 `Mat4`。
2. 修复 PBR root signature register space：`pbr_step1.ps.hlsl` 使用 `b0/b1 (space0)`，而 C++ 使用 `space1`。
3. 修复 Lighting 常量缓冲对齐：`LightingSettings` 等结构需 16-byte 对齐，或拆分为多个 cbuffers。
4. 完善 `DX12Renderer::Resize`：更新 viewport/scissor 并重建 depth buffer。
5. 修复 `DX12DescriptorAllocator::ReleaseFrameAllocations`：当前 `frameIndex` 参数未使用，需正确推进/释放。
6. 扩大 SRV/Sampler heap 容量：当前只分配 1 个 slot，无法支持多纹理材质。
7. 实现 `DX12Device::QueryFeatures` 并正确填充 `DX12Features`，避免错误判定 DX12U 能力。

## P2（架构与功能完善）
1. 统一渲染器体系：收敛 `DX12Renderer` / `PBRRenderer` / `CubeRenderer`，保留单一主入口。
2. 统一材质系统：弃用 `PBRMaterialAsset` 或重构其对齐 `DX12Material` + allocator。
3. 完善后处理管线：Bloom / Eye Adaptation / Color Grading / Tone Mapping。
4. 完善 Debug Views：Wireframe / Normals / Depth / Heatmap 等视图实现。
5. 补齐 TODO：`command_list.cpp` 中 VRS/Mesh Shader 等占位实现。
6. 添加 descriptor heap 容量监控与日志报警，避免运行时溢出。
