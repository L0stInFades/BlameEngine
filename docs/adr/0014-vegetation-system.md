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

## 交付状态(2026-06-03:交付门已全过)

只读审计(2026-06-03)曾正确指出此前只是 headless 核心切片、交付链路在 streaming/cook/UE5/玩法处断,故一度降级为 `prototype(core-only)`。**随后把整条链路打通**,下列验收门**全部实现并测试**(全套 26 测试 + ASan 从完整构建通过),成熟度回升为 **usable**:

1. ✅ **layered `.ncell`**:`engine/world/.../layered_cell_file.h`(`NCL2` chunk 表:layer id/codec/offset/压缩前后大小 + 每层 codec),v1 单载荷 `.ncell` 不动。`LayeredCellTest`:同一 cell 同时携带 StaticMesh + Vegetation,Zstd/LZ4 往返,fail-closed。
2. ✅ **vegetation cook**:`next_vegetation_world` 的 `CookVegetationCell`(validate→scatter→`PackCell`→`PackLayeredCell`)+ `assetc vegetation <def.txt> <cellX> <cellZ> <cellSize> <out> [codec]` CLI + 文本 def 解析。`VegetationCookTest`:坏 def fail-closed、golden 字节稳定、与直接 scatter 逐字节一致。
3. ✅ **`LoadCellLayer(Vegetation)` 真实 IO**:读 layered cell 文件、`ExtractLayer`、`memoryPool_` 分配填 `cell->layers[Vegetation]`,`allowPlaceholderCellLoad=false` 缺层 fail-closed(占位层 data==null 可区分)。`VegetationStreamingTest`。
4. ✅ **runtime `VegetationStore`**:`(cell,ordinal)` 无碰撞键、`QueryRadius`/`AllLive`/按 flags、destructible overlay;**`instanceId` 改为每 cell ordinal**、**`logicalRadius` 入 `VegetationInstance`**(48 字节)。`VegetationStoreTest`。
5. ✅ **Game API / gameplay**:`VegetationWorldQuery : gameapi::IWorldQuery`(植被=竖直圆柱,**复合进既有 Sense raycast**,与物理 fallback 取近)+ `SegmentBlockedByVegetation` + `DestroyVegetation`→`VegetationDestroyedEvent`。`VegetationQueryTest`。
6. ✅ **UE5 只读消费合同**:破坏走 `boundary::GameEvent`(`ToBoundaryEvent`,纯表现);批量 per-cell payload + `MockVegetationConsumer`(按 `visual` 分 HISM 桶、load/unload/destroy),**不持有放置、不跑 PCG**。`VegetationViewTest`。
7. ✅ **端到端纵切面**:`VegetationSliceTest` = cook→write `.nlc`→`LoadCellLayer`→store+mock consumer(同字节同实例)→LOS→破坏(sim 与 view 同步)→unload。全部 vegetation 测试已进 `code-quality.yml`。

**诚实残留(非"植被系统"完整性缺口,而是边界/后续):**
- **UE5 端是忠实 mock**:仓内无 UE 工程,且 `engine/*` 红线不依赖 UE5——真正的 Unreal `MirrorSubsystem` C++ 属于(尚不存在的)UE5 客户端工程。本 ADR 交付的是**契约 + 可被消费的字节 + 消费者模拟**。
- **cook CLI 用平地形**:cook **库** terrain-agnostic(任意 `ITerrainSampler`);真实 heightmap/Jolt 地形源是单独的事。
- **StaticMesh async 单载荷管线保持 v1**(未迁 layered):刻意非目标,避免动复杂的 async/memorypool 管线;layered 格式已能携带 StaticMesh(已测)。
- **scatter 同构建可复现、非跨平台逐位**(`cos`/`sqrt` 末位);baked blob 才是跨平台产物。
- **网格生成 OSS**(ez-tree/tree-gen)接入真实内容管线仍属后续;核心只转发 `VisualStateId`,registry 在 UE5 侧。

## 硬化轮(2026-06-03):"能编译/测试绿 ≠ 收工"

在"端到端打通"之后,针对"绿测掩盖的真实缺口"又做了一轮硬化(均有测试,30 套 ASan 全绿,RelWithDebInfo 亦绿):

- **性能/规模**:空间查询/LOS raycast 原本走 `AllLive()` = O(全部已加载实例),开放世界会爆。改为**均匀网格 broadphase**(`QueryAABB`/`QueryRadius`/raycast 走它),`GatherCandidates` 取 dense-range 与 populated-bucket 的较小者(避免大半径扫空桶——一版草稿曾把整套测试从 ~10s 拖到 506s)。`VegetationScaleTest` 用 ~1 万实例对**暴力扫描**逐一核对正确性。
- **运行系统**:`VegetationStreamingSystem::Sync()` 每帧把 `StreamingManager` 的 load/unload 自动喂给 store(自动 ingest/evict),不再是测试里手工接线。
- **内存计入**:植被层字节经 `CellData::MemorySize()` 计入流送预算/eviction(此前不可见)。
- **真实地形**:`HeightmapTerrainSampler`(双线性高度 + 梯度法线 + mask)证明 scatter 在真实坡地上正确(on-surface、海拔带、坡度排除)。
- **对抗 fuzz**:三个 parser 在 ASan/UBSan 下吃 1 万+ 随机/截断/位翻转输入 → 无 OOB/UB、全 fail-closed。
- **对抗 review(1 轮,4 角度 + 验证 + 补扫)** 在我自己的代码里查出 **8 个真实 bug** 并全部修复 + 加测:per-cell cap 跨物种溢出、退化法线毒化坡度过滤、`memorySize += ` 与 StaticMesh `=` 路径冲突、alloc 失败 fail-open、Sync 只认 coord 导致 reload 后陈旧、heightmap 数组越界、`BucketOf` float→int UB、`UnpackCell` 无 instanceCount 上限。

**仍诚实保留的边界/后续**(非"绿测掩盖",而是已知范围):UE5 端仍是 mock(仓内无 UE 工程);植被层走**同步**读路径而非 async 流送管线(刻意,避免重写复杂 async/内存池);Sync 的 (ptr,size) token 不区分"同指针同大小但内容不同"的 reload(需 per-layer 版本号);store 用 `std::map`(确定性)而非 `unordered_map`(性能)是有意取舍;若干测试 fixture(`MakeForest`)可抽公共头。
