# 0014. 植被系统:核心拥有放置与逻辑状态,UE5 只负责渲染

- **Status**: Accepted
- **Date**: 2026-06-03
- **Refines**: [ADR-0005](0005-ue5-renderer-jolt-headless-world.md)(headless 权威 + UE5 视图)、[ADR-0006](0006-sim-ue5-boundary.md)(sim↔UE5 边界机制)。引用 [ADR-0013](0013-level-design-system.md) 的"数据驱动 Def + fail-closed 校验 + 确定性加载"形态。

## Context

引擎此前没有植被系统:`engine/world` 的 `CellLayer::Vegetation` 只是一个**流送层枚举占位**(`LoadCellLayer` 里只有 `StaticMesh` 有真实 blob,其余层是占位),`docs/04-ai-systems.md` 的"植物/农田"是已废弃的两宋开放世界设计文档,没有对应代码。

现在要做一个真正的植被系统,约束有三:
1. **不要 SpeedTree**;要能**深度定制**;倾向开源。
2. 渲染前端是 **UE5(只当无逻辑视图,ADR-0005 红线:UE5 内零游戏逻辑、零权威状态;sim 必须能脱离 UE5 headless 运行)**。
3. 由此引出关键问题:**植被系统用 UE5 自带的(PCG/Foliage)是否"越界"?**

## Decision

### 1. 越界判定:植被分两半,红线从中间切开

- **位置 + 每株逻辑状态**(在哪、是否被砍/烧、挡不挡视线、是不是掩体)= **权威世界状态**,**必须留在 headless 核心**。把它交给 UE5 PCG/Foliage 拥有 = **越界**:会让专用服务器 / CI / AI-agent 不知道植被在哪,破坏 headless 可运行性、确定性、服务器权威/反作弊。
- **渲染**(画网格、风动、LOD、impostor、Nanite、HISM、UE5.7 skeletal vegetation)= **纯表现**,**交给 UE5** = **不越界**,正是 UE5 作为"无逻辑渲染客户端"的本职;不用反而是在重造 2026-05 故意删掉的渲染器。

一句话:**谁拥有植被的位置与状态是红线;用谁来画不是。**

### 2. 新建 `engine/vegetation`(`next_vegetation`,`Next::vegetation`)

镜像 `engine/level` 的形态,纯下行依赖(仅 `next_foundation` 的 `next/math`),无 UE5 / 无 `game/` / 不耦合 world/runtime,确定性、headless、ASan 可测:

- **`VegetationDef`**:物种表(`SpeciesId`、`VisualStateId`、密度、最小间距、坡度/海拔范围、缩放范围、`logicalRadius`、`requiredMask`、`blocksLOS`/`destructible`/`alignToSlope` 标志);`VegetationBuilder` 流式构造。
- **确定性 per-cell scatter**(`ScatterCell`):抖动网格 + 最小间距拒绝(blue-noise),按坡度(cos 阈值,免 per-point acos)/海拔/mask 过滤。**种子只来自 `masterSeed + 世界 cell 坐标 + 物种 + 网格节点**,所以一个 cell **无论何时流入都同构生成、顺序无关**(与 UE5 PCG 由世界坐标派生种子的做法一致)。`TerrainSampler` 抽象解耦地形来源,保证可 headless 测试。
- **`VegetationValidator`**:total / fail-closed,累积全部缺陷(物种 id 唯一非零、密度有界、坡度/海拔/缩放范围合法、`maxInstancesPerCell` 安全上限防失控)。
- **`CellLayer::Vegetation` blob 打包**(`PackCell`/`UnpackCell`):版本化、`static_assert` 布局的扁平 POD(`VegetationInstance` 固定 44 字节),fail-closed 解析。这就是递给 UE5 的字节。
- **空间查询**(`QueryRadiusXZ`):供 AI / 视线 / 掩体用,确定性顺序。

### 3. 网格生成的开源库:cook-time 内容工具,不进 `engine/*`

核心只需要 `VisualStateId`(不透明,转发给 UE5)+ 逻辑半径,**不需要视觉网格**。所以"替代 SpeedTree 的程序化树木生成"是**内容/cook 管线**关注点,产出 UE5 侧网格资产,**不是引擎运行时依赖**:

- 要**可嵌入**就用 **MIT** 的 **ez-tree** 或 **tree-gen**(C++,空间殖民);
- **GPL** 的 **Kraut / MTree**(MTree 带 Pivot Painter 2.0 → UE5 风动导出)只能当**外部工具**(传染许可,不可进引擎);
- 算法层值得掌握的是 **space colonization** 或 **self-organizing trees**。

### 4. 数据流

`核心 ScatterCell → CellLayer::Vegetation blob → 流送 → UE5 读入 HISM/Nanite/foliage 渲染`。UE5 侧可叠加**纯装饰性**微草(无权威,等同 VFX);任何**玩法相关**(挡视线 / 可破坏 / 掩体)的植株一律核心权威。少量可交互植株也可当普通 `Renderable` 实体走快照流(ADR-0006),海量静态用 cell blob 更省。

## Consequences

- **正面**:护城河边界清晰(植被权威留核心);headless 可测 + 确定性(19/19 测试 ASan 通过,新增 `VegetationTest`);渲染器可替换(只递 `VisualStateId` + 实例数据);散布算法自研、可深度定制(密度/间距/坡度/海拔/mask/缩放/标志全可调)。
- **负面 / 新增工作(诚实)**:
  - 视觉网格仍需 UE5 侧 cook 管线 + 一个 `VisualStateId` 注册表(本 ADR 只定契约,未集成具体生成库——留后续 ADR)。
  - scatter 当前是**同构建可复现**;**跨平台逐位一致**未保证(`cos`/`sqrt` 末位可能差)。**baked 的 cell blob 才是跨平台产物**(与 UE5 bake PCG 同理);若未来要求 scatter 跨平台逐位,需替换为定点/查表的坡度阈值。
  - 尚未接到 `engine/world` 的真实 cell IO:`next_vegetation` 只产/解 blob,**把 blob 写入 `.ncell` 层、由 `StreamingManager` 加载**是下一步(需 world ↔ vegetation 接线)。
  - 物种网格 LOD / impostor / 风动参数等属 UE5 侧,核心不持有。

## Alternatives considered

- **直接用 UE5 PCG/Foliage 当权威放置** — 否决:越界,破坏 headless 可运行 / 确定性 / 服务器权威。
- **把 SpeedTree 集成进核心** — 否决:用户明确不要;闭源、不可深度定制;与"自研且拥有核心"相悖。
- **核心内做完整程序化树木网格生成** — 暂缓:网格是渲染/cook 关注点,核心只需 placement + 逻辑;在 `engine/*` 里背一个重网格生成库不划算。
- **全部植被都走快照流当普通实体** — 部分采用:适合少量可交互植株;海量静态植被用 per-cell blob(流送友好、UE5 直灌 HISM)更省。
- **Poisson-disk(Bridson)散布** — 暂未采用:抖动网格 + 间距拒绝更易做到**按 cell 切块、顺序无关、确定性**(Bridson 的 active-list 依赖全局顺序);后续若需更优 blue-noise 可在保持 per-cell 确定性的前提下替换。

## 交付状态(2026-06-03,经只读审计校正)

本 ADR 的**决策**(越界判定 + 新建 `engine/vegetation` 核心)成立、已实现、单测 + ASan 通过。**但这只是 headless 核心切片,不构成可交付的"引擎植被系统"**——交付链路在 world streaming / cook / UE5 消费端 / 玩法集成处仍断。因此 README/ARCHITECTURE 的成熟度从 ~~usable~~ 改为 **prototype(core-only)**;**在下列验收门全过前,对外不得称"植被系统可交付",只能称"核心切片"**。

**真正"交付"的验收门(全部需在干净 checkout 上通过):**
1. **layered `.ncell` v2**:`engine/world/include/next/streaming/cell_file_format.h` 增加 layer chunk table(layer id / offset / 压缩前后大小 / codec / version),v1 单 StaticMesh payload 兼容。验收:world 单测能在同一 cell 同时加载 StaticMesh + Vegetation 两层。
2. **vegetation cook**:`tools/assetc` 增命令(如 `next_assetc vegetation <def.json> <terrain> <cellX> <cellZ> <out.nveg>`):parse → `VegetationValidator::Validate`(坏 def fail-closed)→ terrain sample → `ScatterCell` → `PackCell` → 写 vegetation chunk。验收:样例 cell 产稳定 golden hash。
3. **`StreamingManager::LoadCellLayer(Vegetation)` 读真实 chunk**:当前固定 placeholder(`engine/world/src/streaming_manager.cpp:1069`,且 `.ncell` 加载只填 StaticMesh:`:633`)。改为分配 layer 内存、填 `cell->layers[CellLayer::Vegetation]`,`allowPlaceholderCellLoad=false` 时缺层即失败。验收:加载后 `UnpackCell` 成功。
4. **runtime vegetation store**:加载后索引 + `QueryRadiusXZ`/按 flags 查询 + destructible overlay;**`instanceId` 改为确定性无碰撞键**((cellX,cellZ,species,ordinal) 或 64-bit;当前仅 32-bit hash);**`logicalRadius` 写入实例或随 cell 加载 species table**(当前 `logicalRadius` 只在 `VegetationSpecies`、未进 `VegetationInstance` blob)。
5. **Game API / gameplay 接入**:`IWorldQuery` 或新植被 facade 把 `VegBlocksLineOfSight` + `logicalRadius` 计入 LOS/raycast/sense;destructible 植株有状态覆盖并产生 sim→UE5 事件。
6. **UE5 只读消费合同**:`engine/boundary` 增植被 cell load/unload 消息或共享 payload 视图(当前 `snapshot.h` 只发逐实体 `RenderableComponent`,无批量植被 payload)。UE5 侧仅:按 `visual` 分组 → 查 `VisualStateId`→mesh/material registry → 批量写 HISM/Nanite/foliage;**不持有放置、不跑 PCG 权威**。
7. **最小内容样例 + 端到端验收**:2 species + 1 VisualStateId registry + ~9 baked cells;端到端测试 cook → world 加载 Vegetation 层 → unpack → query → boundary/mock UE consumer 收到实例。CI 门:`test_vegetation test_world_streaming test_asset_compiler test_boundary` 全绿 + 该端到端测试,且从**干净 checkout** 重新配置/编译证明整条链路。
