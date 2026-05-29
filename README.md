# Blame Engine

**Blame Engine**(代码符号沿用 `NEXT`/`Next`)是公司的**核心技术资产**:一套**与具体游戏无关**的自研引擎,公司所有游戏都在它之上开发。引擎核心(`engine/*`)不依赖任何一款游戏;每款游戏(`game/*`)是引擎的消费者。

> **技术选型([ADR-0005](docs/adr/0005-ue5-renderer-jolt-headless-world.md))**:渲染交给 **UE5**(只当无逻辑渲染客户端)、物理交给 **Jolt**;公司自研、且拥有的核心 = **headless 权威世界 + 安全玩家代码运行时(WASM 沙箱)+ 一套 Game API**(玩家代码 / AI agent / UE5 视图共用)。
>
> **现状(诚实)**:地基扎实且有测试(构建/质量工具链、Job System、Archetype ECS、资产管线、世界流送调度器);护城河核心(Game API、WASM 沙箱、Jolt 绑定、sim↔UE5 复制层、AI-agent 工具面)待建。详见 [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) 与 [`docs/TECH_DEBT.md`](docs/TECH_DEBT.md)。

**当前首个项目**:一款"真实代码黑客"的开放世界游戏(看门狗式,黑客是玩家真实编写、真实执行、真实影响世界状态的代码)。游戏内的 **HackOps** 是该游戏的黑客层(**游戏特性**,非引擎模块)。

## 文档入口

| 我是… | 先读 |
|---|---|
| 新人 | [`GETTING_STARTED.md`](GETTING_STARTED.md) — 30 分钟 clone→构建→测试→运行 |
| 想懂架构 | [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) — 分层 + 模块成熟度 |
| 要贡献代码 | [`CONTRIBUTING.md`](CONTRIBUTING.md) — 分支/评审/质量闸门 |
| 做游戏 / 管代码 | [`docs/ENGINEERING_HANDBOOK.md`](docs/ENGINEERING_HANDBOOK.md) — 版本/API 稳定性/游戏如何消费引擎 |
| 全部文档 | [`docs/README.md`](docs/README.md) — 文档索引 |

## 30 秒快速开始

```bash
# 终端/HackOps 技术线(无需渲染器,跨平台最快)
cmake --preset terminal-dev && cmake --build --preset terminal-dev

# 测试(ASan/UBSan)
cmake --preset asan && cmake --build out/build/asan --target test_runtime \
  && ctest --test-dir out/build/asan -R RuntimeTest --output-on-failure
```

预设:`terminal-dev`(终端线)· `headless`(核心库+工具+测试)· `asan`(测试闸门)。渲染交 UE5、物理交 Jolt,本仓库不构建渲染器。详见 [`GETTING_STARTED.md`](GETTING_STARTED.md)。
