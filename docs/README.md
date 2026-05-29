# Blame Engine — 文档索引

> **Blame Engine**（代码符号沿用 `NEXT`/`Next`)是公司的**核心技术资产**:一套**与具体游戏无关**的自研引擎,公司所有游戏都在它之上开发。
> 引擎核心(`engine/*`)不依赖任何一款游戏;每款游戏(`game/*`)作为引擎的**消费者**存在。

本索引只描述当前源码的真实状态,不复述历史阶段的宣传口径。点对点的阶段报告已归档到 [`history/`](history/)。

---

## 1. 这是什么 / 给谁看

- **引擎团队**:维护 `engine/*`、`tools/*` 与构建/质量基线。读 [架构](ARCHITECTURE.md) + [工程手册](ENGINEERING_HANDBOOK.md) + [贡献指南](../CONTRIBUTING.md)。
- **游戏团队**:在引擎之上做游戏,把引擎当成被版本化依赖的库/平台。读 [入门](../GETTING_STARTED.md) + [工程手册 §游戏如何消费引擎](ENGINEERING_HANDBOOK.md)。
- **新人**:从 [GETTING_STARTED](../GETTING_STARTED.md) 开始,30 分钟内 clone → 构建 → 跑通测试。

**当前首个项目**:一款"真实代码黑客"的**开放世界游戏**(看门狗式,但黑客是真实可运行的代码)。其中的 **HackOps** 是这款游戏内的"真实代码黑客层",**不是**独立的引擎模块,也不是另一个引擎目标。引擎本身保持游戏无关。详见 [00-vision-principles](00-vision-principles.md)。

## 2. 现状与战略(一句话)

**技术选型([ADR-0005](adr/0005-ue5-renderer-jolt-headless-world.md))**:渲染交给 **UE5**(只当无逻辑渲染客户端)、物理交给 **Jolt**;公司自研、且拥有的核心 = **headless 权威世界 + 安全玩家代码运行时(WASM 沙箱)+ 一套 Game API**(玩家代码 / AI agent / UE5 视图共用)。

地基(构建/质量工具链、Job System、Archetype ECS、资产管线、世界流送调度器)**真实且有测试**;护城河核心(Game API、WASM 沙箱、Jolt 绑定、sim↔UE5 复制层、AI-agent 工具面)**待建**。逐模块成熟度见 **[ARCHITECTURE.md](ARCHITECTURE.md)**,缺口见 **[TECH_DEBT.md](TECH_DEBT.md)**。

## 3. 文档地图

### 入门与治理(先读这些)
| 文档 | 作用 |
|---|---|
| [`../GETTING_STARTED.md`](../GETTING_STARTED.md) | 新人上手:clone → preset → 构建 → 测试 → 运行 |
| [`ARCHITECTURE.md`](ARCHITECTURE.md) | 分层架构、模块依赖规则、**逐模块成熟度** |
| [`../CONTRIBUTING.md`](../CONTRIBUTING.md) | 如何贡献:分支模型、评审、质量闸门、提交规范 |
| [`ENGINEERING_HANDBOOK.md`](ENGINEERING_HANDBOOK.md) | **代码管理**:版本/发布、API 稳定性、依赖、游戏如何消费引擎、所有权 |
| [`adr/`](adr/) | 架构决策记录(ADR):为什么这样设计 |
| [`design/`](design/) | 详细设计文档(如 [sim↔UE5 边界](design/sim-ue5-boundary.md)) |

### 工程规范
| 文档 | 作用 |
|---|---|
| [`CODE_STYLE.md`](CODE_STYLE.md) | 命名/格式/语言特性约定 |
| [`CODE_QUALITY.md`](CODE_QUALITY.md) | clang-format / clang-tidy / sanitizers / CI 闸门 |

### 子系统设计参考(`00-08`,设计意图 + 路线,非"已完成"声明)
| 文档 | 子系统 |
|---|---|
| [`00-vision-principles.md`](00-vision-principles.md) | 愿景、边界、架构原则、引擎 vs 游戏 |
| [`01-engine-structure.md`](01-engine-structure.md) | 分层、模块边界、运行时模型(四大件) |
| 渲染 | 交给 UE5,见 [ADR-0005](adr/0005-ue5-renderer-jolt-headless-world.md) + [sim↔UE5 边界](design/sim-ue5-boundary.md)(旧自研渲染稿已归档至 [`history/02-rendering-roadmap.md`](history/02-rendering-roadmap.md)) |
| [`03-world-streaming-time.md`](03-world-streaming-time.md) | 世界分区与流送 |
| [`04-ai-systems.md`](04-ai-systems.md) | AI 系统设计(尚未实现) |
| [`05-scripting-quests.md`](05-scripting-quests.md) | 脚本与任务系统 |
| [`06-tools-modding-experiments.md`](06-tools-modding-experiments.md) | 工具、Mod、实验体系 |
| [`07-milestones.md`](07-milestones.md) | 里程碑路线 |
| [`08-development-workflow.md`](08-development-workflow.md) | 开发流程 |

### 参考手册
| 文档 | 作用 |
|---|---|
| [`asset_format_specification.md`](asset_format_specification.md) | `.npkg`/资产二进制格式(v2) |
| [`ASSET_PIPELINE_QUICKSTART.md`](ASSET_PIPELINE_QUICKSTART.md) | `next_assetc` 资产导入/打包 |
| [`HACKOPS_DEV_QUICKSTART.md`](HACKOPS_DEV_QUICKSTART.md) | 终端/HackOps 技术线开发 |
| [`SCRIPTING_LUA_GUIDE.md`](SCRIPTING_LUA_GUIDE.md) | Lua 集成指南 |
| [`TECH_DEBT.md`](TECH_DEBT.md) | 已知技术债与缺口(活文档) |

### 历史归档
- [`history/`](history/):checkpoint 完成报告、handover、修复报告等**点对点历史文档**(非当前指导)。

## 4. 30 秒快速开始

```bash
# 终端/HackOps 技术线(无需渲染器,跨平台最快)
cmake --preset terminal-dev && cmake --build --preset terminal-dev

# 测试(带 ASan/UBSan)
cmake --preset asan && cmake --build out/build/asan --target test_runtime \
  && ctest --test-dir out/build/asan -R RuntimeTest --output-on-failure
```

完整命令与各预设见 [`../GETTING_STARTED.md`](../GETTING_STARTED.md)。
