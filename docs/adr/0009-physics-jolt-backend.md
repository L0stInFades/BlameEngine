# 0009. 物理 = `IPhysicsWorld` 抽象 + 确定性参考后端;Jolt 为可选后端(FetchContent,默认关)

- **Status**: Accepted
- **Date**: 2026-05-30
- **Refines**: [ADR-0005](0005-ue5-renderer-jolt-headless-world.md)(它定了"物理 = Jolt,权威/确定性/服务器侧";本 ADR 定**怎么接、边界在哪**)。同构于 [ADR-0008](0008-player-code-sandbox.md) 的"安全契约 + 后端可换"思路。

## Context
ADR-0005 选 Jolt 是为了拿到**独立于 UE5 的确定性服务器物理**。但若 headless 核心**直接**依赖 Jolt,就把护城河绑死在某个第三方库上,且 Jolt 是个大依赖——overnight 全量编译 + 跨平台 + 与 sanitizer 预设交互都有风险,会动摇主干绿色。需要:物理能用、确定性、可 headless 测试、可换后端、且**不引入 Jolt 也能构建/测试**。

## Decision
1. **物理是一个由接口定义的能力,不是某个引擎**:新增 `engine/physics`(库 `next_physics`),核心只认抽象 `IPhysicsWorld`(创建/销毁刚体、设/取速度与 transform、固定步 `Step(dt)`)。`engine/*` 与 game 永不直接 include Jolt。
2. **自带确定性参考后端** `ReferencePhysicsWorld`(零第三方依赖):重力 + 半隐式欧拉积分 + 可配置地平面;static 不动、kinematic 按设定速度走、dynamic 受重力。它让 headless 世界**现在就有可用、可测、确定性的物理**(同 `RefVm` 之于沙箱),保证主干恒绿。
3. **Jolt 为可选后端**:`next_physics_jolt`(仅在 `BUILD_WITH_JOLT=ON` 时构建,默认 **OFF**)通过 **FetchContent** 拉 JoltPhysics,提供 `JoltPhysicsWorld : IPhysicsWorld` 与 `MakeJoltPhysicsWorld()`。**核心 `next_physics` 从不引用 Jolt 符号**——Jolt 纯增量、可换、可缺席。默认预设(`headless`/`asan`)不拉、不建 Jolt,主干构建/CI 不依赖网络与 Jolt 工具链。
4. **ECS 接线、固定步、写回 Transform**:`RigidBodyComponent` 把实体绑到一个 body;`PhysicsSystem`(`System` 子类)在 `OnComponentAdded`/`OnEntityDestroyed` 建/销 body,在 `Update(dt)` 以**固定步**驱动 `IPhysicsWorld::Step(dt)`,再把 body 的 transform **写回 `TransformComponent`**。于是物理结果自动顺着既有管线流动:`sim tick → physics step → TransformComponent → SnapshotPublisher 增量 → 边界 → UE5 视图`([ADR-0006](0006-sim-ue5-boundary.md))。
5. **确定性与服务器权威**:物理在 headless 权威 sim 里跑,固定步、不取墙钟。参考后端天然确定;Jolt 后端按其确定性配置(固定步、确定性设置)接入。权威 100% 在 sim;玩家代码/AI 经 Game API 的**意图**间接影响物理(后续:`MoveTo` 等意图 → kinematic/dynamic body 的速度/力;当前 `DefaultIntentResolver` 直接写 Transform,二者可共存并逐步收敛到"意图→物理→Transform"单一路径)。

## Consequences
- 正面:护城河不被 Jolt 绑死(可换/可缺席);主干恒绿、CI 不依赖 Jolt;物理无缝接入既有 Transform→快照管线;确定性从设计起就在;`尝试集成` 的风险被 `BUILD_WITH_JOLT` 隔离。
- 负面/成本:维护一层物理抽象 + 参考后端(换解耦,值);参考后端不是完整碰撞(只够驱动管线 + 测试,完整碰撞是 Jolt 的活);Jolt 的确定性/许可/版本治理随启用而来;意图→物理的统一路径仍待接。

## Jolt 后端与仓库其它部分的配合(已落地)
Jolt 后端不是 `IPhysicsWorld` 的"另一个实现"就完事 — 必须与 level loader / gameapi Raycast / ActuationSystem 全部对得上,否则同一条 `LevelDef` 在参考后端和 Jolt 后端会跑出两种物理。已处理的两处具体配合点:
- **质量(`BodyDesc::mass` / `BodyDefData::mass`)**:Jolt 默认按形状密度推质量,会让同一条 `BodyDefData` 在两后端下质量不同。`JoltPhysicsWorld::CreateBody` 对所有非 static 体设 `EOverrideMassProperties::CalculateInertia` + `mMassPropertiesOverride.mMass = max(desc.mass, 1)`,让惯性张量按指定质量缩放。`mass <= 0` 与参考后端一致地归一化为 1。验证:`JoltPhysics.MassOverrideIsRespected`。
- **Raycast 表面法线**:之前是占位符 `-direction`(斜射到墙面上会得到错法线,反弹/滑步算出来全偏)。Jolt v5.2.0 `RayCastResult.mSubShapeID2` 给到命中叶子 shape,`BodyInterface::GetShape` 拿到 shape 后用 `Shape::GetSurfaceNormal` 算真法线,再由 `BodyInterface::GetWorldTransform` 旋到世界系,并按契约反向(面朝射线起点)。验证:`JoltPhysics.RaycastSurfaceNormalPointsAlongBoxAxis` 与 `JoltPhysics.RaycastObliqueHitGivesAxisAlignedNormalOnBox`。
- **全栈垂直切片**:`test_jolt_physics_slice.cpp` 与 `test_physics_slice.cpp` 同形,跑 `sandbox(RefVm) → GameApi → DefaultIntentResolver → ActuationSystem(kinematic) → PhysicsSystem(Jolt) → SnapshotPublisher`,证明 Jolt 在同一条权威链路上不掉链子,不止是"接口对"。

## Alternatives considered

## Alternatives considered
- **核心直接依赖 Jolt**:否——绑死第三方、大依赖污染主干/CI、与 ADR-0008 的"后端可换"哲学相悖。
- **Jolt 设为强制依赖(默认 ON)**:否——overnight 拉 + 编译大库会动摇主干绿色;`尝试` 阶段应隔离在 flag 后。
- **直接用 UE5 的 Chaos 物理**:否(ADR-0005 已否)——要独立于 UE5 的确定性服务器物理。
- **只做抽象、不做参考后端**:弱——那样不启用 Jolt 时物理是空壳、无法 headless 测试管线;参考后端让契约可证、管线可跑。

## 验证矩阵
- `BUILD_WITH_JOLT=OFF`(主干):`test_physics` 9/9、`test_actuation` 5/5、`test_physics_slice` 1/1、`test_level` 全部通过;**Jolt 不构建、不拉取、不链接**。
- `BUILD_WITH_JOLT=ON`(`out/build/jolt`):`JoltPhysicsTest` 11/11(backend identity / drop + rest / raycast 命中 + 最近体 + 真法线 + 斜射轴对齐法线 / 质量覆盖 / PhysicsSystem 驱动 Transform / 实体销毁→body 销毁 / ActuationSystem 到达 / 跨运行确定性);`JoltPhysicsSliceTest` 1/1(同 `PhysicsSlice` 形状的完整垂直切片,Guest → GameApi → Jolt → Snapshot)。两套都过。
