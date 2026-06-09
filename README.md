# Blame Engine

> An open world where hacking is **real, player-written code** — really executed, really driving world state.

这是一台游戏无关的自研 C++17 引擎(`engine/*` 永不依赖 `game/*`)。但它存在的理由是一个具体的赌注,先讲赌注,再讲架构。

## 这个赌注

我们要做的第一款游戏:**看门狗式的开放世界,但"黑客"不是 minigame、不是 QTE**。玩家在游戏内一个**真实嵌入的编辑器**(真的 `nvim --embed`,带玩家自己的 LSP / 配置)里**写真实可运行的代码**;这段代码被宿主运行时**真实执行**;它的输出**真实改变世界状态**。

游戏里那一层叫 **HackOps**——它是**游戏特性**,不是引擎模块。

要让"玩家代码 = 真实代码"这件事成立且安全,引擎必须满足三个硬约束,架构正是从它们推导出来的:

1. **权威世界必须能脱离画面运行**——玩家代码、AI co-pilot、反作弊、回放都不该依赖 GPU。→ 一个 **headless 权威世界**(专用服务器 / CI / AI-agent 会话都不碰 UE5)。
2. **玩家代码是不可信输入**——真实执行的前提是真实隔离。→ 一个 **安全的玩家代码运行时(WASM 能力沙箱)**。
3. **玩家代码、AI agent、渲染端必须看同一个世界、走同一套规则**。→ **一套能力域化的 Game API**,同时服务这三个消费者。

这三件(headless 世界 + 玩家代码沙箱 + Game API)= 我们自研且拥有的核心,**护城河**。其余商品化:**渲染交给 UE5**(只当无逻辑的视图客户端),**物理交给 Jolt**(权威、确定性)。详见 [ADR-0005](docs/adr/0005-ue5-renderer-jolt-headless-world.md)。

> 2026-05 的取舍:为聚焦护城河,我们**删除了自研的 DX12/Metal 渲染器 + RHI**。渲染/物理是被集成的商品,不是我们要造的轮子。

## 架构一览

```
        ┌──── 自研核心:Headless 权威世界(无需 GPU 即可运行)────┐
        │  Archetype ECS · 世界状态/回放 · Jolt 物理 · 规则 · 任务  │
        │            ▲ 玩家代码运行时(WASM 能力沙箱) ← 护城河      │
        │  ┌─────────┴──── Game API(能力域化 / 版本化 / 确定性)────┤
        └──┼──────────────┼────────────────┼─────────────────────┘
           │              │                │
      玩家真实代码     AI agent (co-pilot)   UE5 视图客户端
      (WASM 沙箱)    读 API 帮排任务/hack   (渲染/动画/音频/UI;零逻辑)
```

**唯一契约 = Game API**,三个消费者共用。三条**单向** flat C-ABI POD 通道跨边界(详见 [sim↔UE5 边界](docs/design/sim-ue5-boundary.md)):

| 方向 | 内容 | 机制 |
|---|---|---|
| sim → UE5 | 状态快照 | 共享内存环上的 **wait-free 三重缓冲** |
| sim → UE5 | 美术/表现事件(VFX、音效) | append-only 队列 |
| UE5 → sim | 输入 / 命令 | append-only 队列 |

时钟解耦 + UE5 端 snapshot 插值。**同一份数据模型从进程内扩展到跨进程再到联网——这条快照流本身就是 netcode 模型**,联网时白捡。红线:`engine/*` 永不依赖 `game/*`,**任何层都不依赖 UE5**;UE5 内零游戏逻辑、零权威状态。

## 现状(诚实分级)

地基是真的、有测试;**护城河的纵切面已经跑通**(2026-05-30):沙箱里的玩家代码经 Game API 改动 headless 权威世界,再经边界出快照——全程无渲染器、可确定性回放、ASan/UBSan 全绿。物理也已接入(`engine/physics`,确定性参考后端 + 可选 Jolt 后端)。**玩家语言前端也跑通了**:现代 C++(A*)与 Rust(二分查找)编到 wasm32,经 WASM 沙箱后端在 headless 世界里运行,与手写字节码说同一套 Game API,且 CPU 燃料经加载期 gas 插桩强制(无限循环会 `FuelExhausted` 而非挂起,[ADR-0011](docs/adr/0011-wasm-language-frontend.md)/[ADR-0012](docs/adr/0012-wasm-fuel-gas-metering.md))。仍有待补(跨进程/网络、AI-agent 工具面)。这不是免责声明,是路线图——明确区分 production / usable / prototype / absent 本身就是这份 README 的功能。

| 已建成 | 级别 | 说明 |
|---|---|---|
| 构建 / CI / 质量工具链 | **production** | CMake presets + clang-format/tidy + ASan/UBSan + code-quality CI |
| Foundation + Job System | **production** | 含 `next/math`(从已删的 renderer 迁入);工作窃取 Job |
| Archetype ECS(`engine/runtime`) | **production** | 数据导向、迁移、查询、CommandBuffer;~127 ECS 测试 |
| 资产运行时(`engine/runtime/asset`) | **usable** | 句柄 + 引用计数 + 内容哈希 ID + 依赖 manifest;视图 API 携带 `keepAlive` 共享所有权(卸载后读取安全,B3 已修) |
| 序列化 / 压缩 | **usable** | 往返可用 |
| Neovim 嵌入(`engine/terminal`) | **usable** | 真实 msgpack-rpc,CI 冒烟——玩家写代码的编辑器 UX |
| 世界流送(`engine/world`) | sim 侧保留 | WorldPartition / InterestManager / Prediction / Streaming / AsyncIO / LOD / Eviction |
| 任务系统(`engine/task`) | **prototype** | 定义/实例/存档存在;条件/动作**未接世界** |

| 护城河 | 级别 | 说明 |
|---|---|---|
| **Game API**(`engine/gameapi`) | **usable** | 能力域化 / 版本化 / 写即意图;三消费者共用的唯一契约([ADR-0007](docs/adr/0007-game-api-contract.md));O(N) 世界扫描有独立紧配额 `maxWorldScansPerTick`(F-1 缓解) |
| **玩家代码沙箱**(`engine/sandbox`) | **usable** | 安全契约 + `ISandbox` + 自研确定性燃料 VM(零环境权限、能力门控 host-call);对抗性测试全绿([ADR-0008](docs/adr/0008-player-code-sandbox.md));[安全审计](docs/security/sandbox-audit-2026-05-30.md)39 项边界守住 |
| **玩家语言前端**(`next_sandbox_wasm`) | **usable**(opt-in) | C++23 / Rust 2024 → wasm32 → wasm3 后端,与字节码 guest 同一套 Game API ABI;A*/二分查找在 headless 世界跑通(`BUILD_WITH_WASM`,[ADR-0011](docs/adr/0011-wasm-language-frontend.md))。**CPU 燃料经加载期 gas 插桩强制**:无限循环 → `FuelExhausted`,不再挂起([ADR-0012](docs/adr/0012-wasm-fuel-gas-metering.md))。**内存/调用深度红线也已强制**(B2 修复):`memoryBytes` 字节级钳制初始内存与 `memory.grow`(内存炸弹 → `OutOfMemory`,宿主无恙),`callDepth` 经解释器栈预算强制(递归炸弹 → `StackOverflow`);对抗套件 `WasmSandboxTest` 入 CI sanitizer 矩阵 |
| **sim↔UE5 复制层**(`engine/boundary`) | **usable** | wait-free 三重缓冲快照流 + SPSC 命令/事件队列 + ECS 脏集发布器;**可靠增量协议**(delta 对最后已 ack 基线 + keyframe 回退,丢包下镜像收敛)+ 服务器权威时钟 + **跨进程/UDP transport**(`DatagramTransport` + 真 POSIX `UdpDatagram`)——同一数据模型,换 transport 即专用服务器([ADR-0006](docs/adr/0006-sim-ue5-boundary.md) W13–W15);TSan CI 门守锁自由热路径 |
| **物理**(`engine/physics`) | **usable** | `IPhysicsWorld` 抽象 + 确定性参考后端 + ECS `PhysicsSystem`(固定步,写回 Transform);Jolt 为可选后端(`BUILD_WITH_JOLT`,核心 Jolt 无关,[ADR-0009](docs/adr/0009-physics-jolt-backend.md)) |
| **关卡设计系统**(`engine/level`) | **usable** | 数据驱动关卡:`LevelDef` + 流式构造 + **fail-closed 总校验门** + 事务化确定性加载 + 胜负条件;经多轮 agent workflow 严格 review 闭环修复缺陷,加载关卡→跑沙箱 guest→胜利条件触发端到端跑通([ADR-0013](docs/adr/0013-level-design-system.md)) |
| **植被系统**(`engine/vegetation` + `engine/vegetation_world`) | **usable** | **端到端打通**:确定性 per-cell scatter + fail-closed 校验 → layered `.ncell`(`NCL2` chunk 表 + 每层 codec)+ `assetc vegetation` cook → `LoadCellLayer(Vegetation)` 真实 IO → runtime store(半径/flags 查询、可破坏覆盖、(cell,ordinal) 无碰撞键)→ Game API LOS/cover(`VegetationWorldQuery` 复合进 Sense raycast)+ 破坏→`boundary::GameEvent` → UE5 消费合同 + mock consumer(按 visual 分 HISM 桶);**核心拥有放置/状态,UE5 只渲染**;端到端纵切面 `VegetationSliceTest`,26 测试 ASan 通过([ADR-0014](docs/adr/0014-vegetation-system.md))。**诚实残留**:UE5 端为 mock(仓内无 UE 工程);cook CLI 用平地形(库 terrain-agnostic);StaticMesh async 管线未迁 layered |
| **水体系统**(`engine/water` + `engine/water_world`) | **usable** | **端到端打通,且超越植被(真实每帧力仿真)**:确定性 Gerstner 波面(引擎自有 `det_trig` 确定性 sin/cos,跨构建逐位重放)+ 解析阿基米德浮力 + 速度钳制阻力(无"弹射")+ 水流平流 + 淹没/水深查询;`assetc water` cook → layered `.ncell`(`CellLayer::Water`)→ `LoadCellLayer(Water)` 真实 IO → `WaterStore`(按 bodyId 跨 cell 去重 + broadphase + 无限海洋全局特例)→ `WaterWorldQuery` 复合进 Sense raycast + 隐蔽(潜水)/导电(电击)游戏钩子 + `WaterForceSystem`(经**新增 `IPhysicsWorld::AddForce/AddImpulse`,reference+Jolt 两后端**)+ splash→`boundary::GameEvent` + mock consumer;**核心拥有水面/浮力/流场,UE5 只渲染同参数水面**;端到端纵切面 `WaterSliceTest` + 600 体规模沉降/确定性/fuzz,57 测试 ASan 通过([ADR-0015](docs/adr/0015-water-system.md))。**诚实残留**:UE5 端为 mock;无力矩/自扶正(P2/Jolt);河流为 AABB 走廊;Flood 为时间驱动水位 |

| 护城河 · 待建 | 现状 |
|---|---|
| AI-agent 工具面(P1) | **absent** — 把 Game API 以工具协议暴露给 agent |
| 共享内存 transport(同机跨进程) | **absent** — UDP/进程内已落地;`/dev/shm` 环是第三种 `ISnapshotTransport` 实现,数据模型不变 |

逐模块成熟度见 [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md),已知缺口见 [`docs/TECH_DEBT.md`](docs/TECH_DEBT.md)。

## 快速开始

C++17 + CMake presets。**没有一个 preset 构建渲染器**(渲染在独立的 UE5 视图客户端里)。

```bash
# 终端 / HackOps 技术线:最快,跨平台,构建 demo + 工具,无渲染器
cmake --preset terminal-dev
cmake --build --preset terminal-dev

# 测试(主要正确性闸门:ASan + UBSan)—— 应见 49 套件 100% 通过
cmake --preset asan
cmake --build out/build/asan -j
ctest --test-dir out/build/asan --output-on-failure

# Windows headless 测试(MSVC + Ninja)
cmake --preset windows-headless
cmake --build --preset windows-headless
ctest --test-dir out/build/windows-headless --output-on-failure

# 跑 headless demo(无需画面)
out/build/terminal-dev/bin/hackops_demo \
  --policy tools/nvim_surface_probe/sample_policy.py \
  --snapshot /tmp/hackops-policy-snapshot.txt
```

预设:`terminal-dev`(终端线) · `headless`(核心库 + 工具 + 测试) · `windows-headless`(Windows MSVC + Ninja) · `asan`(测试闸门)。
测试套件(49,全绿;`ctest --test-dir out/build/headless -N` 看全名单):Foundation/ECS/资产/平台/流送等地基 + gameapi/sandbox/boundary/physics/level/vegetation/water 全纵切面;CI 另跑 WASM 沙箱对抗套件与 Windows 全量 ctest。
完整步骤见 [`GETTING_STARTED.md`](GETTING_STARTED.md)。

## 文档地图

| 我是… | 先读 |
|---|---|
| 新人 | [`GETTING_STARTED.md`](GETTING_STARTED.md) — 30 分钟 clone → 构建 → 测试 → 运行 |
| 想懂架构 | [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) — 分层 + 逐模块成熟度 |
| 想懂战略取舍 | [`docs/adr/0005-ue5-renderer-jolt-headless-world.md`](docs/adr/0005-ue5-renderer-jolt-headless-world.md) — 为什么 UE5 + Jolt + headless |
| 想懂 sim↔UE5 | [`docs/design/sim-ue5-boundary.md`](docs/design/sim-ue5-boundary.md) — 三通道 / 三重缓冲 / 复制 |
| 要贡献代码 | [`CONTRIBUTING.md`](CONTRIBUTING.md) — 分支 / 评审 / 质量闸门 |
| 在引擎上做游戏 | [`docs/ENGINEERING_HANDBOOK.md`](docs/ENGINEERING_HANDBOOK.md) — 版本 / API 稳定性 / 游戏如何消费引擎 |
| 全部文档 | [`docs/README.md`](docs/README.md) — 文档索引 |

## 仓库结构

```
engine/    游戏无关的核心:foundation(含 next/math) · jobsystem · profiler · platform ·
           serialization · compression · runtime(+asset, Archetype ECS) · task · terminal ·
           world(流送) · script · log(legacy, opt-in)
game/      引擎消费者(game/hackops)
tools/     assetc(仅 sim 侧数据;渲染资产走 UE5 管线) · 各类 probe
tests/     gtest 套件
docs/      ARCHITECTURE · GETTING_STARTED · ENGINEERING_HANDBOOK · CONTRIBUTING ·
           adr/(决策记录) · design/(如 sim↔UE5 边界) · history/(归档)
web/       游戏的市场落地页(独立关注点,与引擎无关)
```
