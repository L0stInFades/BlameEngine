# 0010. 操控统一:意图驱动物理,Transform 由单一写者拥有

- **Status**: Accepted
- **Date**: 2026-05-30
- **Refines**: [ADR-0007](0007-game-api-contract.md)(写即意图)+ [ADR-0009](0009-physics-jolt-backend.md)(物理写回 Transform)。补上两者之间被标记的债:**谁写 `TransformComponent`**。

## Context
意图模型([ADR-0007](0007-game-api-contract.md))里 `MoveTo` → `DefaultIntentResolver` 设 `MoveTarget` 组件,`StepKinematics` 直接把 `TransformComponent` 朝目标推进。物理([ADR-0009](0009-physics-jolt-backend.md))的 `PhysicsSystem` 也写 `TransformComponent`。于是一个**既有 `MoveTarget` 又有 `RigidBody`** 的实体会被**两个写者**争抢 Transform——非确定、会抖、是真实的架构债(我自己在 ADR-0009 里标的)。

## Decision
新增 **gameplay 层**(`engine/gameplay`,库 `next_gameplay`,即文档分层里的第 5 层 Gameplay Framework),依赖 `next_gameapi` + `next_physics` + `next_runtime`(这两个核心彼此仍零依赖;由更高层把它们接起来)。其中 `ActuationSystem` 是**唯一的移动器**,按实体是否被物理接管二选一,保证每个实体的 Transform 只有一个写者:

1. **物理实体**(有 `RigidBodyComponent` 且 body 有效):`ActuationSystem` 不直接写 Transform,而是把 `MoveTarget` 翻译成**物理 body 的速度**(朝目标的单位向量 × `maxSpeed`),由 `PhysicsSystem` 积分并**独占写回 Transform**。到达时(`dist ≤ maxSpeed·dt`)精确吸附 + 速度清零 + `MoveTarget.active=0`。
2. **非物理实体**(无 body):`ActuationSystem` 直接把 `TransformComponent` 朝目标积分(等价于 `StepKinematics`),它是这些实体 Transform 的唯一写者。

权威 tick 顺序固定为:`drain 意图 → resolver.Apply(设 MoveTarget) → ActuationSystem(意图→速度/直接位移)→ PhysicsSystem(积分→写 Transform)→ 发布快照`。`ActuationSystem` 在 `PhysicsSystem` 之前注册。`gameapi::DefaultIntentResolver::StepKinematics` 保留为**无物理**子集的参考移动器(纯 gameapi 测试与无物理切片用),整合 sim 用 `ActuationSystem`。

## Consequences
- 正面:每个实体的 Transform **只有一个写者**(物理实体=物理;非物理=ActuationSystem),消除争抢、恢复确定性;`MoveTo` 现在真正"意图→物理→Transform"单路径([ADR-0009](0009-physics-jolt-backend.md) 标的债清偿);gameapi 与 physics 两核心仍互不依赖,由 gameplay 层桥接(分层干净)。
- 负面/成本:多一个 gameplay 层与一个系统;`ActuationSystem` 的非物理分支与 `StepKinematics` 有几行重复的积分数学(换干净分层,值);`MoveTo` 当前只驱动 **kinematic** 体的速度,dynamic 体的"受力移动"(角色控制器)留作后续。

## Alternatives considered
- **让 `StepKinematics` 跳过有 `RigidBody` 的实体**:否——`StepKinematics` 在 gameapi 层,看不到 physics 的 `RigidBody`(会破坏分层)。
- **把物理感知塞进 gameapi 的 resolver**:否——同样破坏 gameapi 对 physics 的零依赖。
- **维持双写者、靠注册顺序让物理"最后写赢"**:否——脆、隐式、仍非确定(两者目标语义不同),是要清的债本身。
