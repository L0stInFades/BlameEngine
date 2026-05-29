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

**待建的新核心模块**(尚不存在):`engine/sandbox`(WASM 玩家代码运行时)、`engine/gameapi`(能力域化 Game API)、`engine/physics`(Jolt 绑定)、sim↔UE5 复制层、AI-agent 工具面。

**已从仓库删除(2026-05,ADR-0005)**:`engine/renderer`、`engine/rhi`(自研 DX12/Metal 渲染)、`tools/editor`、`third_party/imgui`、`game/song`(渲染 demo)。渲染交 UE5、物理交 Jolt,核心不再背负这部分代码。删除前先把通用数学库 `next/math` 抽到 `engine/foundation`(headless 世界依赖它)。

## 4. 模块成熟度表(诚实评级)

评级:**production** · **usable** · **prototype** · **placeholder** · **absent** · **🗑 已删除**(代码已从仓库移除,改由外部方案承担)。

| 模块 | 成熟度 | 状态 |
|---|---|---|
| 构建 / CI / 质量工具链 | **production** | presets + clang-format/tidy/ASan + CI;~250 测试 ASan 通过 |
| Foundation / Job System | **production** | 日志/断言/数学;工作窃取 Job(28 测试) |
| Archetype ECS(headless 世界核心) | **production** | 数据导向、迁移、查询、CommandBuffer;127 测试 ASan 通过 |
| 资产运行时 | **usable** | 句柄+引用计数+内容哈希 ID+依赖 manifest |
| 任务系统 | **prototype** | 定义/实例/存档存在;条件/动作未接世界 |
| 序列化 / 压缩 | **usable** | 往返可用;无 schema 迁移 |
| Neovim 嵌入(玩家代码 UX) | **usable** | 真实 msgpack-rpc 嵌入,CI 冒烟 |
| **Game API(契约核心)** | **absent → P0 新建** | 玩家代码 / AI agent / UE5 三者共用的能力域化 API |
| **WASM 玩家代码沙箱(安全核心)** | **absent → P0 新建** | 当前是 `popen(python3)` 探针,零隔离;护城河所在 |
| **Jolt 物理绑定** | **absent → P0 新建** | 权威、确定性、服务器侧 |
| **sim↔UE5 状态复制层** | **absent → P0 新建** | headless 权威 → UE5 视图镜像 |
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
