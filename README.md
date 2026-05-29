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

地基是真的、有测试;护城河大部分**还没建**。这不是免责声明,是路线图——明确区分 production / prototype / absent 本身就是这份 README 的功能。

| 已建成 | 级别 | 说明 |
|---|---|---|
| 构建 / CI / 质量工具链 | **production** | CMake presets + clang-format/tidy + ASan/UBSan + code-quality CI |
| Foundation + Job System | **production** | 含 `next/math`(从已删的 renderer 迁入);工作窃取 Job |
| Archetype ECS(`engine/runtime`) | **production** | 数据导向、迁移、查询、CommandBuffer;~127 ECS 测试 |
| 资产运行时(`engine/runtime/asset`) | **usable** | 句柄 + 引用计数 + 内容哈希 ID + 依赖 manifest |
| 序列化 / 压缩 | **usable** | 往返可用 |
| Neovim 嵌入(`engine/terminal`) | **usable** | 真实 msgpack-rpc,CI 冒烟——玩家写代码的编辑器 UX |
| 世界流送(`engine/world`) | sim 侧保留 | WorldPartition / InterestManager / Prediction / Streaming / AsyncIO / LOD / Eviction |
| 任务系统(`engine/task`) | **prototype** | 定义/实例/存档存在;条件/动作**未接世界** |

| 护城河 · 待建(P0) | 现状 |
|---|---|
| **Game API**(能力域化契约) | **absent** — 三消费者共用的核心契约 |
| **WASM 玩家代码沙箱** | **absent** — 今天玩家代码走 `popen(python3)`,**零隔离**;这是安全核心 |
| **Jolt 物理绑定** | **absent** — 权威、确定性、服务器侧 |
| **sim↔UE5 复制层** | **absent** — 设计已定([边界文档](docs/design/sim-ue5-boundary.md)),实现待写 |
| AI-agent 工具面(P1) | **absent** — 把 Game API 以工具协议暴露给 agent |

逐模块成熟度见 [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md),已知缺口见 [`docs/TECH_DEBT.md`](docs/TECH_DEBT.md)。

## 快速开始

C++17 + CMake presets。**没有一个 preset 构建渲染器**(渲染在独立的 UE5 视图客户端里)。

```bash
# 终端 / HackOps 技术线:最快,跨平台,构建 demo + 工具,无渲染器
cmake --preset terminal-dev
cmake --build --preset terminal-dev

# 测试(主要正确性闸门:ASan + UBSan)—— 应见 10 套件 100% 通过
cmake --preset asan
cmake --build out/build/asan -j
ctest --test-dir out/build/asan --output-on-failure

# 跑 headless demo(无需画面)
out/build/terminal-dev/bin/hackops_demo \
  --policy tools/nvim_surface_probe/sample_policy.py \
  --snapshot /tmp/hackops-policy-snapshot.txt
```

预设:`terminal-dev`(终端线) · `headless`(核心库 + 工具 + 测试) · `asan`(测试闸门)。
测试套件(10,全绿):Foundation · Serialization · JobSystem · Runtime(ECS) · AssetCompiler · Platform · Math · WorldStreaming · Task · Script。
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
