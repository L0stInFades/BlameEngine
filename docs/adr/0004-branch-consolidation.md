# 0004. 收敛 `hackops/*` 长期分叉分支

- **Status**: Accepted（owner 已拍板，2026-05-29 执行完毕）
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

## 执行记录 (2026-05-29)
执行时发现一个 Decision 起草时未预见的事实:**`master`(旧两宋线,root `e90957a`)与 `hackops/dev-foundation`(root `92113ad`)历史不相交(无共同祖先)**,无法常规合并。据此调整了步骤 1 的机制(目标不变):

1. **合入方式**:在 dev-foundation 上 `git merge -s ours --allow-unrelated-histories <旧master>`,生成合并提交 `05b975c`——其 tree 即 Blame Engine 内容(与 dev-foundation 完全一致),第二父指向旧 master。再把 `master` **快进**到该提交(无 force-push)。两条历史均可达、**零丢失**。
2. **旧线保留**:tag `archive/pre-blame` → 旧 master `96df70f`(含 `engine/ops` Ops Runtime ~1275 LOC、旧自研渲染器、`game/song`)。`git cherry` 已确认 `ops-workspace`/`policy-sim`/`python-worker` 三条小分支的提交均已并入旧 master,无独有内容。
3. **删除**:`hackops/{dev-foundation,ops-workspace,policy-sim,python-worker}` 四条远端分支已删,远端仅余 `master`。本地修正了 single-branch fetch refspec → 标准通配。
4. **Ops Runtime 移植**(原步骤 2)**未在本次执行**——它需人工核对是否适配新 archetype ECS/资产格式,改记为 [`TECH_DEBT.md`](../TECH_DEBT.md) P1 任务「复活 `engine/ops`」,源在 tag `archive/pre-blame`。

结果:单一可信主线 `master` = 可发布的 Blame Engine 基线;旧资产经 tag + 合并父链可随时取回。
