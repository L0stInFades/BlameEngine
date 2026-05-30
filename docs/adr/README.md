# 架构决策记录 (ADR)

记录重大架构决策的**原因**(不只是结果)。一条决策一个文件,编号递增,**append-only**:决策变了就写新 ADR 标记 supersede,不改旧的。新决策用 [`0000-template.md`](0000-template.md)。

何时写 ADR(见 [`../../CONTRIBUTING.md`](../../CONTRIBUTING.md) §7):架构性 / 跨模块 / 破坏公共 API / 引入第三方依赖 / 改变引擎↔游戏边界。

## 索引

| # | 标题 | 状态 |
|---|---|---|
| [0001](0001-engine-is-company-core-asset.md) | 引擎是公司核心资产(多游戏通用底座) | Accepted |
| [0002](0002-archetype-ecs.md) | Runtime ECS 采用 archetype 数据导向存储 | Accepted |
| [0003](0003-quality-gates.md) | 工业级 C++ 质量闸门 | Accepted |
| [0004](0004-branch-consolidation.md) | 收敛 `hackops/*` 长期分叉分支 | Accepted |
| [0005](0005-ue5-renderer-jolt-headless-world.md) | 渲染交 UE5 / 物理用 Jolt / 自研 headless 权威世界 + 安全玩家代码运行时 + Game API | Accepted |
| [0006](0006-sim-ue5-boundary.md) | sim↔UE5 边界:无锁三重缓冲快照流 + 单向命令/事件队列(细节见 [`design/sim-ue5-boundary.md`](../design/sim-ue5-boundary.md)) | Accepted |
| [0007](0007-game-api-contract.md) | Game API:能力域化 / 版本化 / 确定性的唯一契约(细节见 [`design/gameapi-and-sandbox.md`](../design/gameapi-and-sandbox.md)) | Accepted |
| [0008](0008-player-code-sandbox.md) | 玩家代码沙箱:由安全边界定义、后端可换(非绑定 WASM) | Accepted |
| [0009](0009-physics-jolt-backend.md) | 物理:`IPhysicsWorld` 抽象 + 确定性参考后端;Jolt 为可选后端(FetchContent,默认关) | Accepted |
| [0010](0010-actuation-single-transform-writer.md) | 操控统一:意图驱动物理,Transform 由单一写者拥有(gameplay 层 `ActuationSystem`) | Accepted |
| [0011](0011-wasm-language-frontend.md) | 玩家语言前端:C++/Rust 编到 wasm32,经 WASM 沙箱后端(wasm3)运行;一套 ABI 两个后端 | Accepted |
| [0012](0012-wasm-fuel-gas-metering.md) | WASM CPU 燃料:加载期 gas 插桩(自改写模块计费 + 越界 trap),而非引入 wasmtime | Accepted |
