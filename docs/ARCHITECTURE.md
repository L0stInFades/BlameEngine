# 架构与模块成熟度 (Architecture)

Blame Engine 的架构、模块依赖规则,以及**逐模块真实成熟度**。
本文档是"现在是什么 + 要建成什么";战略决策见 [`adr/0005-ue5-renderer-jolt-headless-world.md`](adr/0005-ue5-renderer-jolt-headless-world.md),已知缺口见 [`TECH_DEBT.md`](TECH_DEBT.md)。

## 0. 技术选型(ADR-0005)

- **渲染 = Unreal Engine 5**(只当**无逻辑的渲染/呈现客户端**;UE5 内不写游戏逻辑、不用 Blueprint 当玩法)。
- **物理 = Jolt**(权威、确定性、服务器侧)。
- **公司自研、且拥有的核心 = headless 权威世界 + 安全玩家代码运行时 + 一套 Game API**。这才是护城河;渲染/物理/内容工具是被许可/被集成的商品。

## 1. 顶层架构

```
        ┌──────── 自研核心:Headless 权威世界(可无渲染器运行)────────────┐
        │  Archetype ECS · 世界状态/回放 · Jolt 物理 · 规则 · 任务系统       │
        │              ▲ 玩家代码运行时(WASM 能力沙箱)  ← 护城河            │
        │   ┌──────────┴────── Game API(能力域化 / 版本化 / 确定性)─────────┤
        └───┼────────────────┼──────────────────┼──────────────────────────┘
            │                │                  │
      玩家真实代码        AI agent(co-pilot)     UE5 视图客户端
      (WASM 沙箱)       读 API 帮排任务/hack    (渲染/动画/音频/相机/UI;零游戏逻辑)
```

**唯一契约 = Game API**,同时服务三个消费者(玩家代码 / AI agent / UE5 视图)。核心可 **headless 运行**(专用服务器、CI 测试、AI-agent 会话都不需要 UE5)。

## 2. headless 核心的分层与依赖规则

依赖单向向下;`engine/*` 永不依赖 `game/*`;**任何层都不依赖 UE5**(UE5 只在视图侧通过 Game API 反向消费 sim)。

```
Game            game/*                 具体游戏(规则、内容、HackOps 玩法)
  │
Game API        (待建)                 能力域化契约:玩家代码 / AI agent / UE5 视图共用
  │
Sim 层          runtime(ECS) · 世界状态 · 规则 · 任务 · 玩家代码沙箱 · Jolt 绑定
  │
Foundation      foundation/jobsystem/profiler/serialization/compression
  │
Platform        platform(headless 子集:文件/线程/时间;窗口/输入交给 UE5 侧)
```

## 3. 模块地图

| 目录 | 库 | 角色 | 说明 |
|---|---|---|---|
| `engine/runtime`(+`/asset`) | `next_runtime`/`next_asset` | **核心** | Archetype ECS + 资产运行时(headless 世界的实体/数据核心) |
| `engine/task` | `next_task` | **核心** | 任务系统(正是 AI-agent"帮排任务"的对象) |
| `engine/terminal` | `next_terminal` | **核心** | Neovim 嵌入(玩家写代码的编辑器 UX) |
| `engine/foundation`(含 `next/math`)/`jobsystem`/`profiler`/`serialization`/`compression` | `next_*` | **核心** | sim 基础设施。数学库 2026-05 从已删除的 renderer 迁入 foundation,headless 世界与 sim↔UE5 边界共用 |
| `engine/platform` | `next_platform` | **核心(子集)** | headless 所需的文件/线程/时间;窗口/输入移交 UE5 侧 |
| `tools/assetc` | `next_assetc` | 工具 | 仅用于 **sim 侧数据**(渲染资产走 UE5 管线) |

**新核心模块**:`engine/gameapi`(`next_gameapi`,能力域化 Game API,[ADR-0007](adr/0007-game-api-contract.md))、`engine/sandbox`(`next_sandbox`,玩家代码沙箱 + 参考 VM 后端,[ADR-0008](adr/0008-player-code-sandbox.md))、`engine/boundary`(`next_boundary`,sim↔UE5 快照流 + 命令/事件队列,[ADR-0006](adr/0006-sim-ue5-boundary.md))、`engine/physics`(`next_physics`:`IPhysicsWorld` 抽象 + 确定性参考后端 + 射线投射 + `PhysicsSystem`;Jolt 为可选后端 `next_physics_jolt`,[ADR-0009](adr/0009-physics-jolt-backend.md))、`engine/gameplay`(`next_gameplay`:gameplay 层 —— `ActuationSystem` 单一 Transform 写者 + `PhysicsWorldQuery` 把物理射线接到 Game API 的 `IWorldQuery`,[ADR-0010](adr/0010-actuation-single-transform-writer.md))**已落地、测试 + ASan 通过**(2026-05-30)。**玩家语言前端**也已落地:`next_sandbox_wasm`(`Wasm3Sandbox`,可选)让现代 C++/Rust 编到 wasm32 后在沙箱里运行,与字节码 guest 同一套 Game API([ADR-0011](adr/0011-wasm-language-frontend.md))。**仍待建**:AI-agent 工具面、dynamic 体的受力角色控制。

**已从仓库删除(2026-05,ADR-0005)**:`engine/renderer`、`engine/rhi`(自研 DX12/Metal 渲染)、`tools/editor`、`third_party/imgui`、`game/song`(渲染 demo)。渲染交 UE5、物理交 Jolt,核心不再背负这部分代码。删除前先把通用数学库 `next/math` 抽到 `engine/foundation`(headless 世界依赖它)。

## 4. 模块成熟度表(诚实评级)

评级:**production** · **usable** · **prototype** · **placeholder** · **absent** · **🗑 已删除**(代码已从仓库移除,改由外部方案承担)。

> ⚠️ **本表的 `usable` = programmer-usable**(干净的 headless C++ 库 + 测试),**不等于**可交付给美术/设计师的 UE5/Unity 级可用:全引擎**无编辑器 / 视口 / 资产导入 GUI / 可视反馈 / 撤销重做**(渲染器/编辑器/imgui 已删,内容创作委托给**仓内并不存在的 UE5 工程**——无 `.uproject`/`.uplugin`,UE5 视图是 mock)。2026-06-03 的 **92-agent 审计**判定 9 个 `usable` 模块中 **0 个 artist-deliverable**,并把其中 4 个降级为 prototype(下表已更);完整证据见 [`TECH_DEBT.md`](TECH_DEBT.md) 的「审计」段。

| 模块 | 成熟度 | 状态 |
|---|---|---|
| 构建 / CI / 质量工具链 | **production** | presets + clang-format/tidy/ASan + CI;~250 测试 ASan 通过 |
| Foundation / Job System | **production** | 日志/断言/数学;工作窃取 Job(28 测试) |
| Archetype ECS(headless 世界核心) | **production** | 数据导向、迁移、查询、CommandBuffer;127 测试 ASan 通过 |
| 资产运行时 | **usable** | 句柄+引用计数+内容哈希 ID+依赖 manifest |
| 任务系统 | **prototype** | 定义/实例/存档存在;条件/动作未接世界 |
| 序列化 / 压缩 | **prototype** | 二进制/JSON 往返;**但**招牌的 ECS-World 存档序列化器(`WorldSerializer`/`SaveWorld`/`LoadWorld`)是**只声明、未实现的孤儿头**(无 `.cpp`、不在 CMake、不被任何文件 include);Binary `GetObjectKeys()` 恒 `return false` → `DeserializeStringMap` **静默丢全部 map 数据**;JSON 解析无递归深度上限 → 不可信文件**栈溢出 DoS**,`ReadInt32/64` 越界 `static_cast` 在工程自带 UBSan 门下 **abort**,`SerializationFormat::Compact` → **空指针解引用崩溃**(均无测试覆盖)。2026-06-03 审计降级 |
| Neovim 嵌入(玩家代码 UX) | **prototype** | 真实 msgpack-rpc 嵌入,**但被排除在构建/ASan 门外**(`asan` 预设 `BUILD_TERMINAL=OFF`、`terminal-dev` 预设 `BUILD_TESTS=OFF`),**零单元测试**(招牌「30/30 ASan」不含本模块);nvim 缺失/崩溃时裸 `write()` 触发 **SIGPIPE 默认处置杀整个宿主进程**(全树无 `SIG_IGN`/`MSG_NOSIGNAL`);`SendInput` 每个按键硬阻塞 **≥500ms**;仅输出单色文本快照。2026-06-03 审计降级 |
| **Game API(契约核心)** `next_gameapi` | **usable** | 能力域化 / 版本化 / 写即意图;`AbiDispatch` + `GameApi` 门面 + `DefaultIntentResolver`;15 测试 + ASan 通过([ADR-0007](adr/0007-game-api-contract.md))。**无正确性 bug**(审计确认);**P1 性能**:`QueryByTag`/`SenseRadius`/`SenseNearest` 是 O(N) 全世界扫无空间索引,每 host-call 只收平价 50 燃料、仅按次数限流 → 非对称 DoS(F-1,仍 OPEN) |
| **玩家代码沙箱(安全核心)** `next_sandbox` | **usable** | 安全契约 + `ISandbox` + 自研确定性燃料 VM 后端(`RefVm`)+ `GameApiGateway`;40 测试含对抗性逃逸/越界/非确定,ASan 通过([ADR-0008](adr/0008-player-code-sandbox.md));[边界安全审计](security/sandbox-audit-2026-05-30.md)39 项守住 |
| **玩家语言前端** `next_sandbox_wasm`(可选) | **prototype** | **完全在构建/测试门之外**(`BUILD_WITH_WASM=OFF`,无 ctest、无 CI,仅 `tools/wasm_demo` 且非 `add_test`)。**🔴 BLOCKER**:`policy.memoryBytes`(红线#4,64KiB 内存上限)在 wasm 后端**完全未实施**(`wasm_sandbox.cpp` 无 `memoryBytes`,`m3_NewRuntime` 不设内存限)→ guest 线性内存只受不可信模块自报上限 + wasm3 的 2GiB 顶,**内存炸弹 host-OOM**;`policy.callDepth` 同样未强制;每 `Run()` **重建整个 VM**(无实例缓存)。其余:`Wasm3Sandbox : ISandbox`(wasm3,FetchContent),C++23/Rust2024→wasm32 经 `tools/wasm_demo` 跑通([ADR-0011](adr/0011-wasm-language-frontend.md) / [ADR-0012](adr/0012-wasm-fuel-gas-metering.md))。2026-06-03 审计降级 |
| **物理** `next_physics` (+可选 `next_physics_jolt`) | **usable** | `IPhysicsWorld` 抽象 + 确定性参考后端 + 射线投射 + **`AddForce`/`AddImpulse` 施力(ADR-0015 浮力底座,半隐式累加-清零,两后端)** + ECS `PhysicsSystem`(固定步,写回 Transform);Jolt v5.2.0 经 `BUILD_WITH_JOLT`/FetchContent 接入(`JoltPhysicsWorld`,单线程确定性),核心 Jolt 无关。15 测试 + ASan 通过([ADR-0009](adr/0009-physics-jolt-backend.md))。**待补**:Jolt 跨平台确定性配置 |
| **gameplay 层** `next_gameplay` | **usable** | `ActuationSystem`(意图→物理速度/直接位移,**单一 Transform 写者**)+ `PhysicsWorldQuery`(物理射线 → Game API `IWorldQuery`,玩家代码经能力门控感知物理世界);6 测试 + ASan 通过([ADR-0010](adr/0010-actuation-single-transform-writer.md))。**待补**:dynamic 体受力角色控制 |
| **sim↔UE5 状态复制层** `next_boundary` | **prototype** | 无锁三重缓冲 + SPSC 命令/事件队列 + `SnapshotPublisher`(ECS 脏集→增量)。**🔴 BLOCKER**:发的是相对「上一**已发布**帧」的 delta 且**无条件**推进基线(`snapshot_publisher.cpp:77`),却发进**故意丢帧**的 TripleBuffer——在 ADR-0006/设计文档自称正常的「时钟解耦/慢 UE5」模式下,丢帧里携带的 spawn/despawn/visual-swap/transform delta **永久丢失**(无 ack / 序号缺口检测 / keyframe / 重同步),UE5 镜像永久错位(已复现 invisible-spawn)。**MAJOR**:无锁 SPSC 跨线程正确性**从未在 TSan/多线程下执行**(全部测试单线程,无 TSan 预设;测试头注释**谎称**已在 TSan/ASan 下验证)。跨进程/网络 transport **不存在**(仅 InProcess)。([ADR-0006](adr/0006-sim-ue5-boundary.md))。2026-06-03 审计降级 |
| **关卡设计系统** `next_level` | **usable** | 数据驱动 `LevelDef`(实体/组件/标签/目标/胜负条件)+ 流式 `LevelBuilder` + **总校验门** `LevelValidator`(fail-closed,30+ 缺陷码)+ **事务化确定性** `LevelLoader`(校验通过才建,失败不碰 World)+ 只读 `WinEvaluator`;经多轮 agent workflow 严格 review 修复全部缺陷,20 测试 + ASan 通过([ADR-0013](adr/0013-level-design-system.md))。**待补**:序列化/持久化、复合条件树 |
| **植被系统** `next_vegetation` + `next_vegetation_world` | **usable** | **端到端打通**(ADR-0014 交付门全过):数据驱动 `VegetationDef`/builder + fail-closed validator + 确定性 per-cell scatter(种子=masterSeed+世界 cell 坐标,顺序无关可复现,instanceId=每 cell ordinal、logicalRadius 入实例)→ layered `.ncell`(`NCL2` chunk 表 + 每层 codec)+ `assetc vegetation` cook(golden 稳定)→ `LoadCellLayer(Vegetation)` 真实 chunk IO(fail-closed)→ runtime `VegetationStore`(半径/flags 查询、destructible 覆盖、(cell,ordinal) 无碰撞键)→ `VegetationWorldQuery : IWorldQuery`(植被作圆柱,复合进既有 Sense raycast)+ `SegmentBlockedByVegetation` + `DestroyVegetation`→`boundary::GameEvent` → UE5 只读消费合同 + `MockVegetationConsumer`(按 visual 分 HISM 桶);**端到端纵切面 `VegetationSliceTest`**,全套 26 测试 + ASan 全绿([ADR-0014](adr/0014-vegetation-system.md))。**诚实残留**:UE5 端是忠实 mock(仓内无 UE 工程,`engine/*` 不依赖 UE5);cook CLI 用平地形(库 terrain-agnostic);StaticMesh async 单载荷管线保持 v1(未迁 layered,刻意非目标);scatter 同构建可复现、非跨平台逐位 |
| **水体系统** `next_water` + `next_water_world` | **usable** | **端到端打通,且超越植被——真实每帧力仿真**(ADR-0015):纯核心 `engine/water`(`det_trig` 确定性 sin/cos + Gerstner 波面 `SampleHeightFast`/`SurfaceHeightAt`/法线 + 球缺/箱体解析淹没体积 + 阿基米德浮力 + 速度钳制阻力[Jolt 配方,显式固定步无条件稳定] + 风驱波频谱 + fail-closed 校验[总陡度≤1] + `NWTR` cell)→ **物理施力扩展** `IPhysicsWorld::AddForce/AddImpulse`(reference 累加器 + Jolt `BodyInterface`,两后端)→ world 集成 `engine/water_world`(`WaterStore` 按 bodyId 跨 cell 去重+broadphase+无限海洋全局特例;`WaterCook`+`assetc water`;`WaterWorldQuery:IWorldQuery` 复合进 Sense raycast + 隐蔽/导电游戏钩子;`WaterForceSystem` 注册在 PhysicsSystem 前、只施力不写 Transform;`WaterStreamingSystem.Sync`;`MockWaterConsumer`+splash 事件)。**端到端纵切面 `WaterSliceTest`**(cook→流送→Sync→物理浮起→splash→复合射线→卸载)+ 600 体规模沉降到 `V_sub/V_tot=ρ_body/ρ` 误差<2%、规模重放哈希、fuzz;57 测试 + ASan/UBSan 全绿([ADR-0015](adr/0015-water-system.md))。**诚实残留**:UE5 端 mock(只证字节契约);无力矩/自扶正与箱-倾斜面体积(P2/Jolt);河流为 AABB 走廊;Flood 为时间驱动水位;reference 与 Jolt 间非逐位(reference 为重放权威) |
| **AI-agent 工具面** | **absent → P1 新建** | 把 Game API 以工具协议暴露给 agent |
| 渲染器 / RHI(自研) | **🗑 已删除** | 2026-05 移除 `engine/renderer`/`engine/rhi`;改由 UE5。math 已迁入 foundation |
| 内容工具 / 场景/材质编辑 | **🗑 已删除** | 移除 `tools/editor`/`imgui`;内容创作走 UE5 编辑器 |
| 物理(自研) | **🗑 superseded** | 从未自研;直接用 Jolt |
| Physics/Animation/Audio/Networking 占位 | **absent** | 动画/音频→UE5;网络→sim↔UE5 复制层 + 服务器权威 |

## 5. 边界与红线

- **`engine/*` 永不依赖 `game/*`,也永不依赖 UE5。** sim 必须能脱离 UE5 headless 运行。
- **UE5 内零游戏逻辑、零权威状态**(Blueprint 仅美术表现/VFX)。这是与 `engine↔game` 同级的 review 红线(见 ADR-0005)。
- 权威逻辑 100% 在 headless 世界;玩家代码与 AI agent 都通过 Game API + 沙箱受限访问;服务器权威保证安全/反作弊。

## 6. 当前最关键的纵切面顺序

> 先把 **headless 世界**做成可玩纵切面(无渲染器),再贴 UE5。建议端到端顺序:
> `headless 世界(ECS+规则+任务)` → `Game API` → `WASM 玩家代码沙箱` → `Jolt 物理` → `AI-agent 工具面` → `最后挂 UE5 视图`。
> 每一步都能 headless 测试。详见 [`TECH_DEBT.md`](TECH_DEBT.md) 的 P0 列表。
