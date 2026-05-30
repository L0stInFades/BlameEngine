# 0007. Game API:能力域化 / 版本化 / 确定性的唯一契约

- **Status**: Accepted
- **Date**: 2026-05-30
- **Refines**: [ADR-0005](0005-ue5-renderer-jolt-headless-world.md)(它定了"Game API 是三个消费者的唯一契约",本 ADR 定**契约本身的形状与红线**)。落地细节见 [`../design/gameapi-and-sandbox.md`](../design/gameapi-and-sandbox.md)。

## Context
ADR-0005 把 Game API 立为护城河的中心:玩家 WASM/沙箱代码、AI agent、UE5 视图三者唯一的接触面。但没定**契约长什么样**。要求:玩家代码是一等公民,安全是一等公民,且整套要**确定性**(回放 / 反作弊 / AI-agent 推理 / 未来服务器权威都依赖它)。一个含糊的 API 会让这三件事全部塌掉。

## Decision
Game API 是一套 **能力域化、版本化、确定性** 的契约,落在 headless 权威世界(Archetype ECS)之上,模块 `engine/gameapi`(库 `next_gameapi`),`engine/*` 与 UE5 零依赖方向不变。

1. **双层、单一实现**:
   - **稳定扁平 C-ABI**(`abi.h`):版本号、错误码 `Status`、不透明 `EntityId`(= ECS 实体的 64-bit 打包形)、每个调用的 POD 入参/出参、以及一张 **host-call id 表**。这是冻结面,沙箱(以及未来跨进程/网络)只认它。
   - **类型化 C++ 门面** `GameApi`:`abi.h` 之上唯一的**实现**;按域提供方法。C-ABI 派发(`AbiDispatch::HostCall`)只是把 POD 解码后转调门面的**薄适配器**,不重复逻辑。

2. **能力域化(capability domains)**:每个调用归属一个能力域;调用方持有一个 `CapabilitySet`,门面在每次调用入口强制校验。v1 能力域:`Observe`(读世界)、`Sense`(感知:近邻/半径,与裸读分离)、`Actuate`(对受控实体下**意图**)、`Comms`(有界信道)、`Tasks`(读任务/推进目标)、`Time`(读 tick/时间)、`Log`(限流诊断)、`SpawnEntities`(特权,默认不授予玩家代码)。授予 ≠ 默认全开:玩家代码拿到的是被裁剪的子集。

3. **写即意图(intent),tick 边界确定性应用**:Game API 的**所有写**都不立即改世界,而是记录为**校验过的意图**进入意图队列;sim 在 tick 边界以**固定顺序**消费、校验(自我归属、`maxSpeed` 钳制、限流)、应用。这把"服务器权威 + 确定性 + 反作弊"做进契约骨架——任何调用方(玩家代码或 AI agent)都无法绕过校验直接改权威状态。

4. **确定性红线**:时间只来自 `Time` 域(tick 计数 + 固定步秒数),无墙钟;查询迭代顺序确定(按 `EntityId` 排序);随机数只能来自 sim 拥有的带种子流(v1 不暴露,保留);浮点只用 IEEE-754 的 +−×÷(不在契约内做超越函数)。

5. **版本化**:`kGameApiAbiVersion`(major.minor)。major 破坏式变更才加;旧玩家模块按其编译版本继续可用,迁移有路径。host-call id 一经分配只增不改。

## Consequences
- 正面:三个消费者共享同一份经校验、确定性的接触面;写即意图 → 反作弊/回放/服务器权威**白捡**;C-ABI 冻结面 → 沙箱与未来跨进程/网络**零重构**;能力域 → 最小权限,安全可审计;门面单一实现 → 无重复逻辑的坏味道。
- 负面/成本:意图的"记录—校验—应用"两段式比直接改状态多一跳(换确定性与安全,值);需维护 POD ABI + id 表;契约要随玩法演进而扩(只增不破)。

## Alternatives considered
- **直接把 `World&` 给玩家代码**:否——零安全、零确定性、零版本化,权威状态裸奔。
- **只做 C++ 门面、不做 C-ABI**:否——沙箱/跨进程/网络都需要扁平冻结面;现在不做,以后重构承重墙。
- **写立即生效**:否——破坏 tick 边界确定性与服务器权威,反作弊无从谈起。
- **能力用运行时字符串权限**(如旧 `LuaAPIPermission`):弱——改用编译期 `CapabilitySet` 位集,零分配、可静态审计。
