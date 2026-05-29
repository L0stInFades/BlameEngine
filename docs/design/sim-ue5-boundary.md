# 设计:sim ↔ UE5 边界

权威 headless 世界(sim,我们拥有)与 UE5 视图(纯渲染)之间的通信机制。决策见 [ADR-0006](../adr/0006-sim-ue5-boundary.md)(机制)/ [ADR-0005](../adr/0005-ue5-renderer-jolt-headless-world.md)(战略)。
目标:**最高性能 + 井水不犯河水**。

## 1. 不变量(红线)

1. **数据单向**:sim→UE5 = 状态快照 + 事件(UE5 只读);UE5→sim = 输入/命令(sim 只读)。无双向共享可变对象,无跨边界回调。
2. **UE5 无权威、无逻辑**:UE5 只持有只读快照 + 命令出口;不做任何游戏决策(不用 UE 复制/GAS/GameMode/Blueprint 玩法)。
3. **sim 对 UE5 零依赖**:边界是扁平 C-ABI 头,两端各自 include;sim 能脱离 UE5 headless 运行(专用服务器 / CI / AI-agent)。
4. **稳定 id 引用**:实体用 64-bit 稳定 id;UE5 自维护 `id→Actor`。
5. **热路径无锁、无停顿**:唯一同步点是一次原子发布/获取。

## 2. 三通道总览

```
   sim(固定步 tick,权威)                         UE5(显示率渲染,纯视图)
   ──────────────────────                          ──────────────────────
   每 tick:                                         每帧:
     收集脏集 → 写快照后台缓冲 ──[三重缓冲·原子发布]──▶ 取最新快照 → 两帧间插值 → 批量刷 proxy
     push 事件 ───────────────[append-only 队列]────▶ drain → 触发 VFX/音效
     drain 命令 ◀──────────────[append-only 队列]──── push 输入/相机兴趣/玩家代码
```

## 3. 数据模型(扁平 POD / C ABI)

只发"渲染视图"所需,且只发增量。**SoA**,贴着 sim 的 ECS 列存储,便于近零拷贝 + UE5 批量/SIMD 应用。

```cpp
// boundary/snapshot.h — 两端共享的扁平定义(无 STL、无虚函数、无指针,可直接放进共享内存)
using EntityId = uint64_t;

enum class VisualStateId : uint32_t {};   // 指向 UE5 侧已注册的 mesh/skeleton/material 变体

struct TransformPacked {                   // 量化以省带宽(联网时尤为重要)
    float    pos[3];                       // 也可改为相对 cell 原点的定点数
    int16_t  rotQuat[4];                   // 16-bit 量化四元数
    uint16_t scale;                        // 统一缩放(需要非统一时再扩展)
};

struct SpawnRecord   { EntityId id; VisualStateId visual; TransformPacked xform; };
struct UpdateRecord  { EntityId id; TransformPacked xform; uint32_t animState; };

struct Snapshot {                          // 一个 tick 的渲染视图增量
    uint64_t      tick;                    // sim tick 序号(用于插值/排序)
    double        simTimeSeconds;          // 该 tick 的权威时间(插值用)
    uint32_t      spawnCount, updateCount, despawnCount;
    // 紧随其后在同一块内存里依次排布:
    //   SpawnRecord[spawnCount] · UpdateRecord[updateCount] · EntityId[despawnCount]
    // (变长;放进环形缓冲的连续块。静态实体不进 update,零成本。)
};

struct GameEvent  { uint32_t type; EntityId subject; float params[4]; };   // sim→UE5 一次性表现触发
struct InputCmd   { uint32_t type; float a[4]; };                          // UE5→sim 输入/命令
```

## 4. 状态发布:无锁三重缓冲(SPSC,wait-free)

producer(sim)与 consumer(UE5)各自私有一个槽,第三个槽经 `mid_` 原子交换;带 fresh 位表示"有新帧"。

```cpp
// 参考实现 —— 落地后用 TSan 验证。
class SnapshotTripleBuffer {
    SnapshotBlock slot_[3];
    std::atomic<uint32_t> mid_{0};               // 低 2 位=槽索引,bit2=fresh
    uint32_t write_ = 1, read_ = 2;
    static constexpr uint32_t kFresh = 0x4, kIdx = 0x3;
public:
    SnapshotBlock& Write() { return slot_[write_]; }            // sim 写这里
    void Publish() {                                            // sim 发布(一次原子交换)
        uint32_t old = mid_.exchange(write_ | kFresh, std::memory_order_acq_rel);
        write_ = old & kIdx;                                    // 回收消费者没在用的槽
    }
    const SnapshotBlock* Acquire() {                            // UE5 取最新;无新帧返回 nullptr
        if (!(mid_.load(std::memory_order_acquire) & kFresh)) return nullptr;
        uint32_t old = mid_.exchange(read_, std::memory_order_acq_rel);  // 交回 read 槽(清 fresh)
        read_ = old & kIdx;
        return &slot_[read_];
    }
};
```
sim 永不等 UE5(慢消费者只是丢中间帧),UE5 永不等 sim(无新帧就复用上一帧插值)——**温度上的井水不犯河水**。

## 5. 时钟解耦 + UE5 端插值

- sim 固定步(如 30/60 Hz,确定性,喂 Jolt + 玩家代码 + 回放);UE5 按显示率渲染。
- UE5 保留最近**两个**快照,按 `renderTime = now - interpDelay` 在两帧之间插值(快进对象用 sim 给的速度外推)。这平滑了 sim tick 抖动、给任意帧率丝滑画面。
- 这正是 **snapshot interpolation**(经典 netcode);因此联网时这套逻辑**白捡**。

## 6. 传输抽象:进程内 → 跨进程 → 联网(同一数据模型)

```cpp
struct ISnapshotTransport {                       // 边界只认这个接口
    virtual SnapshotBlock& BeginWrite() = 0;      // producer
    virtual void Publish() = 0;
    virtual const SnapshotBlock* Acquire() = 0;   // consumer
    virtual void PushEvent(const GameEvent&) = 0;
    virtual bool PopCommand(InputCmd&) = 0;
};
```
- **进程内**(首个切片):sim 作为 native lib 链进 UE5 进程,环形缓冲就在进程内;可指针发布、零拷贝、最易调试。
- **同机跨进程**:`/dev/shm` / mmap 共享内存环;多一次 memcpy(近免费),换**真正的故障隔离**。
- **联网**(专用权威服务器):同样的字节 = 复制包(+量化已就位)。
- **切换只换 `ISnapshotTransport` 实现,边界数据模型不变**。

## 7. UE5 侧:`MirrorSubsystem`(薄、纯视图、C++)

```cpp
// UMirrorSubsystem : public UWorldSubsystem  (UE5 C++;无 Blueprint 玩法、无 UPROPERTY 复制)
void UMirrorSubsystem::Tick(float dt) {
    if (const SnapshotBlock* snap = Transport->Acquire()) {       // 取最新(或保留上一帧插值)
        for (auto& s : Spawns(snap))   SpawnProxy(s.id, s.visual, s.xform);   // 对象池取
        for (auto& d : Despawns(snap)) RecycleProxy(d);                       // 还池
        SwapInterpBuffers(snap);                                             // 保留两帧
    }
    ApplyInterpolatedTransforms(GetWorldTime());   // 批量刷 proxy 的 transform(SoA→批处理)
    while (PopEvent(ev)) FireCosmeticCue(ev);       // VFX/音效,纯表现
    while (HasInput())   Transport->PushCommand(CollectInput());
}
```
要点:**对象池**复用 view Actor(别按实体增删 spawn/destroy);**关闭这些 proxy 的 Actor Tick**,统一在 subsystem 里批量刷;海量人群/载具走 **ISM/HISM 或 Niagara**,不是逐 Actor。`VisualStateId → mesh/material` 是 UE5 侧一张注册表。

## 8. 与我们 ECS 的对接(近零拷贝)

- 给可渲染实体加一个 `Renderable` tag component → 它们的 `TransformComponent` 在 archetype 里**本就是连续一列**,该 archetype 的列即"渲染集",打包快照 ≈ 直接遍历该列(数据导向 ECS 红利)。
- sim 侧维护**脏集**(本 tick 变更的 transform/state)→ 只这些进 `UpdateRecord`;spawn/despawn 来自实体创建/销毁。
- 打包用 Job System 并行。

## 9. 确定性
玩家代码(WASM)+ Jolt 驱动权威状态。为支持回放/反作弊/AI-agent 推理,sim tick 要确定性(固定步、确定性物理配置、WASM 偏确定性)。快照/插值天然容忍非确定性的**渲染**端。

## 10. 演进路线
1. 进程内:sim lib + 进程内环 + `MirrorSubsystem` → 最快出可玩切片。
2. 跨进程:同接口换共享内存环 → 故障隔离 + sim 可独立 headless 跑。
3. 联网:同字节走可靠/不可靠通道 → 专用权威服务器 + 多 UE5 客户端(服务器权威是玩家代码安全/反作弊的前提)。

每步都不改边界数据模型,只换 `ISnapshotTransport`。
