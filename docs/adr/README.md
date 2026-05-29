# 架构决策记录 (ADR)

记录重大架构决策的**原因**(不只是结果)。一条决策一个文件,编号递增,**append-only**:决策变了就写新 ADR 标记 supersede,不改旧的。新决策用 [`0000-template.md`](0000-template.md)。

何时写 ADR(见 [`../../CONTRIBUTING.md`](../../CONTRIBUTING.md) §7):架构性 / 跨模块 / 破坏公共 API / 引入第三方依赖 / 改变引擎↔游戏边界。

## 索引

| # | 标题 | 状态 |
|---|---|---|
| [0001](0001-engine-is-company-core-asset.md) | 引擎是公司核心资产(多游戏通用底座) | Accepted |
| [0002](0002-archetype-ecs.md) | Runtime ECS 采用 archetype 数据导向存储 | Accepted |
| [0003](0003-quality-gates.md) | 工业级 C++ 质量闸门 | Accepted |
| [0004](0004-branch-consolidation.md) | 收敛 `hackops/*` 长期分叉分支 | Proposed |
| [0005](0005-ue5-renderer-jolt-headless-world.md) | 渲染交 UE5 / 物理用 Jolt / 自研 headless 权威世界 + 安全玩家代码运行时 + Game API | Accepted |
| [0006](0006-sim-ue5-boundary.md) | sim↔UE5 边界:无锁三重缓冲快照流 + 单向命令/事件队列(细节见 [`design/sim-ue5-boundary.md`](../design/sim-ue5-boundary.md)) | Accepted |
