# 0006. sim↔UE5 边界:无锁三重缓冲快照流 + 单向命令/事件队列

- **Status**: Accepted
- **Date**: 2026-05-29
- **Refines**: [ADR-0005](0005-ue5-renderer-jolt-headless-world.md)(它定了"headless 权威 + UE5 视图",本 ADR 定**机制**)。细节见 [`../design/sim-ue5-boundary.md`](../design/sim-ue5-boundary.md)。

## Context
ADR-0005 把渲染交给 UE5、自研核心做成 headless 权威世界,但没定 sim 与 UE5 之间**怎么通信**。要求:**最高性能 + 井水不犯河水**(两端独立、互不阻塞、无共享可变状态、可换渲染器、sim 可脱离 UE5 运行)。

## Decision
采用 **3 条单向通道**,载荷为**扁平 POD(C ABI)**,跑在**共享内存环**上:
1. **状态快照(sim→UE5)**:**无锁三重缓冲**;每 sim tick 发布"渲染视图增量"(spawn/despawn + 本 tick 变化的 transform(SoA)+ 视觉状态 id)。UE5 原子取最新、在最近两帧间**插值**。
2. **事件(sim→UE5)**:append-only 队列,一次性表现触发(VFX/音效),不带权威。
3. **输入/命令(UE5→sim)**:append-only 队列,tick 边界消费(玩家输入、相机兴趣、提交的代码)。
- **两端时钟解耦**,唯一同步点是一次原子的发布/获取;热路径无锁、无跨边界回调。
- 实体用**稳定 id** 引用;UE5 自维护 `id→Actor` 映射;sim 对 UE5 **零依赖**(可 headless 运行)。
- **同一份数据模型**:进程内(环在进程内)→ 同机跨进程(/dev/shm)→ 联网(同样的字节即复制包)。**进程内起步,升级只换 transport,不重构**。
- UE5 侧 = 薄 `MirrorSubsystem`:对象池、关闭逐 Actor tick、批量应用快照、海量用 ISM/Niagara;**绕开 UE 的复制/GAS/GameMode/Blueprint 玩法**。

## Consequences
- 正面:无锁三重缓冲 → 零停顿;贴着 archetype 列存储 → 近零拷贝快照;时钟解耦 + 插值 → 任意帧率平滑、互不阻塞;**快照即 netcode 模型** → 升级专用服务器/多人零重构;故障隔离(跨进程时一端崩不污染另一端)。
- 负面/成本:要维护边界数据模型 + sim 侧脏集追踪;进程内阶段共享地址空间(故障隔离要到跨进程那步才有)。

## Alternatives considered
- 用 UE 的 Actor 复制 / GAS:否——耦合、慢、把权威交给 UE。
- 同步调用 / 跨边界共享可变状态 / 回调:否——锁、停顿、线程灾难、耦合。
- 一开始就联网:推迟——进程内先拿性能,数据模型保证可无痛升级。

## 2026-06-05 实现:可靠增量协议 + 服务器权威时钟 + 跨进程/UDP transport(W13/W14/W15)
把"快照即 netcode 模型"从设计落成可丢包健壮的实现,全 build+test 绿、ASan/UBSan 清、TSan 绿、真 UDP 回环绿,入 CI。
- **W13 修 B1 BLOCKER(可靠增量)**:三重缓冲**故意丢中间帧**,旧 `SnapshotPublisher` 发"相对上一已发布帧"的 delta 并无条件推进基线 → 丢帧的 spawn/despawn/move **永久丢失**、UE5 镜像永久错位。改为:delta 相对**最后已 ACK 的基线**(`DeltaMode::Reliable` 默认;`PerFrame` 为无损信道的旧语义)+ 有界 inflight 历史 + `Acknowledge(seq)` + **keyframe 回退**(无 ack/消费者落后超阈值 → 发整帧,消费者必能应用,自愈)。新增 `SnapshotReceiver`(镜像 + 应用规则:keyframe 重置、delta 仅在 baseline==所持序时应用否则 skip、stale 序拒绝)。`ISnapshotTransport` 加 `PushAck/PopAck` 反向通道。证明:`test_boundary_reliability` 重丢帧/丢全部 ack 下镜像**收敛**。
- **W14 服务器权威时钟**:快照携带权威 tick/seconds;`SnapshotReceiver::ServerTick/ServerSeconds` **单调**跟随(stale 帧不能回退时间或重置镜像);`RenderClock` 跟随权威、**永不越权外推**、卡顿后前跳。
- **W15 跨进程/UDP transport**:`snapshot_codec`(扁平版本化 `NSNP` blob,fail-closed 解码 + 记录数上限)+ `DatagramTransport : ISnapshotTransport` over 抽象 `IDatagram`(1 字节 tag 复用 snapshot/event/command/ack;单调 high-water 选最新;`pending_`/`held_` 分离保证"持有指针到下次 Acquire 前有效")+ `InMemoryDatagramLink`(逐向丢包注入)+ 真 POSIX `UdpDatagram`(127.0.0.1 非阻塞;Windows 桩返回 null)。丢包下镜像收敛(消费者每帧重发累计 ack 自愈)。**升级只换 transport,数据模型/调用点不变**——兑现了本 ADR 的承诺。
- 设计经 agent-workflow 对抗审计(2 轮:review→verify→synthesize,再逐项 re-verify):首轮抓出真实缺陷(延迟 ack livelock、high-water 在 Acquire 被重置、PopEvent 覆盖持有快照、丢包测试只丢 ack 等),全部修复并补测。
