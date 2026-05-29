# 02 渲染路线：混合光追/非光追、运镜、材质与资产虚拟化

> **⚠️ 已作废(2026-05)**:按 [ADR-0005](adr/0005-ue5-renderer-jolt-headless-world.md),**渲染交给 UE5**,不再自研。本文是自研渲染时期的设计稿,保留作历史参考与"对 UE5 的能力期望";不代表当前路线。当前架构见 [`ARCHITECTURE.md`](ARCHITECTURE.md)。

## 2.1 目标与约束

渲染系统必须同时满足：
- **画质分层**：同一套内容资产，在不同硬件能力上走不同路径（Tier A/B/C），但保持“同一美术风格与稳定观感”。
- **一镜到底与大场面**：镜头连续、可预测的流式加载；远景规模感不崩；近景细节可靠。
- **研发可持续**：允许新 GI/新压缩/新材质试验，但不污染主干（Production/R&D 双轨）。

## 2.2 渲染架构（Production Track）

### A) RHI（渲染硬件接口）
推荐优先级：
1. Windows 首发：**DX12**（更贴近 PC 生态与 RT 能力）
2. 预留：Vulkan（跨平台/未来主机）

RHI 的职责边界：
- 资源创建（Buffer/Texture/Sampler）
- Pipeline/Shader 管理
- Descriptor/Bindless（建议尽早支持 bindless，为大世界与材质系统服务）
- 命令队列与同步（多队列：graphics/compute/copy）
- GPU 时间戳与统计

### B) Render Graph（渲染图）
核心价值：把“渲染 pass 的依赖与资源生命周期”做成数据结构，便于：
- 自动 barrier/aliasing
- pass 可视化与调试
- 新技术插拔（例如把 GI pass 替换掉）

### C) GPU-Driven 渲染（面向大场面）
建议路线：
- 以 **Instance/Cluster** 为粒度做剔除与绘制（CPU 提交尽量少）
- 支持：Frustum + Occlusion（HiZ）剔除
- 远景依赖：HLOD / Impostor / Instance 批处理

## 2.3 画质分层（同一内容，多条管线）

建议以“能力集（Capabilities）”驱动，而不是按显卡型号写死：

### Tier A（旗舰/电影）
- Path Tracing（照片模式/电影模式/特定室内）
- RTGI/RT Reflections/RT Shadows（可与 PT 共存）
- 高质量体积雾/大气散射/光晕
- 高质量毛发/布料（必要时离线缓存）

### Tier B（主流/推荐默认）
- 混合 RT：优先用 RT 解决“最难伪造”的部分（反射/局部阴影/近景接触）
- 非 RT GI：探针/DDGI + 表面缓存 + 屏幕空间补全
- 高质量 TAAU/TSR + 可选 DLSS/FSR/XeSS

### Tier C（老硬件/无 RT）
- 反射：SSR + Reflection Probes（烘焙/实时更新的环境缓存）
- GI：DDGI（低分辨/低频）或 Light Probes + SSGI + AO 组合
- 阴影：CSM/Contact Shadows + 距离场/屏幕空间补细节

落地要求：
- Tier 切换不允许让内容“换一套资产”；只能在**算法与预算**上切换。
- 每个 Tier 都要定义“预算表”（ms/显存/内存/带宽）与可降级项。

## 2.4 GI/反射/阴影：推荐技术路线

### A) 全局光照（GI）
Production Track 推荐从易到难分三段走：
1. **Probe-based（DDGI / Light Probes）**：先解决“稳定的间接光氛围”，对大世界更友好。
2. **Screen-space 补全**：提升局部细节与接触阴影，注意稳定性（Temporal accumulation）。
3. **混合 RT（可选）**：用 RT 兜底“探针/屏幕空间缺失”的部分，尤其是反射与薄物体遮挡。

可选/研发路线（R&D Track）：
- SDF/体素/卡片化（surface cache）表达几何，用于更强的非 RT GI
- ReSTIR DI/GI（高端方向，先做技术验证再谈生产）

### B) 反射
建议三层叠加（从快到慢）：
1. SSR（便宜，但有屏幕缺失）
2. Reflection Probes / 环境缓存（填补缺失）
3. RT Reflections（旗舰/主流可选，用于关键材质与近景）

### C) 阴影
- 方向光：CSM/Virtual Shadow Map（取决于实现成本）+ Contact Shadows
- 点/聚光：Shadow Atlas + 距离衰减策略
- 旗舰：RT Shadows（用于细小遮挡/软阴影质量）

## 2.5 材质与纹理系统（面向创新与性能）

### A) 材质系统
建议：
- PBR 基础 + Layered Materials（泥土/雨水/磨损/血迹等可叠加）
- 关键着色模型：Skin（SSS）、Cloth、Hair、ClearCoat、Wetness
- 材质参数尽量“实例化 + 纹理驱动”，避免大量 shader permutation 爆炸

### B) 纹理系统（高质量且可扩展）
Production Track 推荐：
- **Virtual Texturing / Sparse Textures**：大世界纹理的长期解；支持 UDIM/大面积地表/建筑细节。
- GPU 压缩格式：BCn（PC），并明确每类贴图的格式策略（BC7/BC5/BC4 等）。
- 贴花/细节法线：以低成本提升近景质感。

R&D Track（可持续实验）：
- 更强的纹理压缩/神经压缩（先做离线对比与部署成本评估，再决定是否进入 Production）。

## 2.6 几何与“突破多边形”的路线拆分

Production Track（可交付优先）：
- Mesh LOD + HLOD + Meshlet/Cluster 剔除
- 位移/视差（谨慎使用，注意稳定与成本）
- 地形：clipmap/virtual heightfield（配合 VT）

R&D Track（研究方向，必须可开关）：
- Signed Distance Field（SDF）作为渲染/碰撞/遮挡的辅助表示
- 基于隐式表面/程序化细分的资产（先用于少量类别：岩石、地形细节、破坏效果）

## 2.7 运镜系统：一/三人称 + 影视镜头的统一设计

镜头系统建议做成“可组合图（Camera Graph）”：
- 输入：角色状态、目标点、碰撞信息、镜头模式、导演指令（cutscene/轨道）
- 输出：相机位姿 + 镜头参数（FOV/焦距/景深/快门/胶片曲线）+ 抖动/呼吸/手持等效果

关键能力：
- **模式一致性**：第一人称、过肩第三人称、自由镜头、锁定镜头在同一套图里切换与混合。
- **镜头碰撞与遮挡处理**：避免穿墙；必要时自动切换到“近景模式”或半透明遮挡策略。
- **镜头制作管线**：支持轨道、关键帧、实时调度（可由剧情/任务触发）。

“一镜到底”对工程的含义：
- Cutscene 与 Gameplay 共用同一套渲染与世界流式；镜头切换不应触发硬加载。
- 需要相机路径预测来驱动预取（详见世界流式文档）。

## 2.8 调试与验收（渲染必须可观测）

必备工具：
- Render Graph 可视化（pass 列表、耗时、资源依赖）
- GPU capture（RenderDoc 等）流程固化
- Shader 热重载（开发期）
- 画质对比工具：同一镜头多 Tier 输出对比（截图/序列帧）
- “金图测试”（Golden Image）：避免重构导致画质悄悄变差

## 2.9 分阶段落地建议（与首作绑定）

Phase 0（立项原型，尽快可跑）：
- Raster + 基础灯光 + 阴影 + 后处理 + 基础材质
- Camera Graph 的骨架（先把手感与镜头统一）

Phase 1（垂直切片）：
- Render Graph、GPU-driven 基础、HLOD 远景策略
- GI：先 Probe/烘焙与运行时混合（不追求全实时）

Phase 2（开放世界生产）：
- VT/Sparse Textures、完善的材质分层
- Tier B/C 的稳定方案（为低端硬件护航）

Phase 3（旗舰与研发落地）：
- 混合 RT、照片/电影模式 Path Tracing
- R&D 技术择优进入 Production（有明确收益与预算）

