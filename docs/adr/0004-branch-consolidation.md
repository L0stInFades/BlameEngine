# 0004. 收敛 `hackops/*` 长期分叉分支

- **Status**: Proposed (需 owner 拍板)
- **Date**: 2026-05-29

## Context
远端有 4 条长期分叉分支:
- `hackops/dev-foundation` — 含本轮引擎硬化(archetype ECS、压缩、依赖 manifest、流送预算、质量工具链),最新、质量最高。
- `hackops/ops-workspace`、`hackops/python-worker` — 含较早的 Ops Runtime / python-worker 探索(沙箱化玩家代码执行的雏形),但**不含**上面的引擎硬化。
- `hackops/policy-sim` — 早期策略仿真探索。

长期分叉是债务,且最有价值的 HackOps 工作(Ops Runtime)与最新引擎基线分散在不同分支,互不包含。

## Decision (proposed)
1. 以 `hackops/dev-foundation` 为集成线,合入 `master`,使 `master` = 可发布引擎基线。
2. 把 `ops-workspace`/`python-worker` 中仍有价值的 Ops Runtime 工作**rebase 到集成线之上**(方向不可反,否则丢引擎硬化)。
3. 删除已并入/废弃分支,回到短分支模型(见 [ENGINEERING_HANDBOOK §2](../ENGINEERING_HANDBOOK.md))。

## Consequences
- 正面:单一可信主线;Ops Runtime 与引擎硬化合一。
- 负面/风险:rebase/合并分叉工作有冲突成本;需要人工核对哪些 Ops Runtime 代码仍适用于新 archetype ECS / 资产格式。

## Alternatives considered
- 维持现状:被否,债务持续累积、主线不清。
- 以旧的 ops 分支为主线:被否,会丢失最新引擎硬化与质量工具链。
