# 设计:Game API 契约 + 玩家代码沙箱

护城河的两块承重墙。决策见 [ADR-0007](../adr/0007-game-api-contract.md)(Game API)、[ADR-0008](../adr/0008-player-code-sandbox.md)(沙箱)。
本文是**实现规格**:类型、能力域、host-call 表、意图模型、VM 指令集都按此落地。

---

## A. Game API(`engine/gameapi`,库 `next_gameapi`)

### A.1 分层(单一实现)

```
玩家代码(沙箱)        AI agent(进程内)        UE5 snapshot 生产者
      │                      │                          │
      ▼ host-call(POD)        ▼ 直接调用                  ▼ 只读遍历
  AbiDispatch::HostCall ──► GameApi(类型化门面,唯一实现)──► World(Archetype ECS)
                                  │  能力校验 + 入参校验 + 确定性时钟
                                  ▼
                            IntentQueue(写即意图)──tick 边界──► IntentResolver──► World
```

- `GameApi` 是唯一实现;`AbiDispatch` 只解码 POD → 转调门面。沙箱经 `AbiDispatch` 进入;in-process 消费者直接用门面。

### A.2 ABI 基元(`next/gameapi/abi.h`,冻结面)

```cpp
constexpr uint16_t kGameApiVersionMajor = 1;
constexpr uint16_t kGameApiVersionMinor = 0;
constexpr uint32_t kGameApiAbiVersion = (kGameApiVersionMajor << 16) | kGameApiVersionMinor;

using EntityId = uint64_t;               // = ECS Entity 的 64-bit 打包;0 = 无效
constexpr EntityId kInvalidEntity = 0;

enum class Status : int32_t {            // 错误码(返回值)
    Ok = 0, InvalidArgument, NotFound, PermissionDenied,
    Unsupported, OutOfRange, RateLimited, BufferTooSmall, Internal,
};

// host-call id 表(只增不改)。每个 id 绑定一个能力域 + 一对 POD 入/出参。
enum class CallId : uint32_t {
    GetTick = 1, GetTimeSeconds, Self, IsValid, GetPosition,   // Time/Observe
    QueryByTag, SenseRadius, SenseNearest,                     // Observe/Sense
    MoveTo, Stop, SetActionFlag,                               // Actuate(意图)
    SendSignal,                                                // Comms(意图)
    GetObjective, ReportProgress,                              // Tasks
    Log,                                                       // Log
};
```

POD 入/出参一律 `struct`,无指针、无 STL(可直接放进 guest 线性内存)。例:
`struct Vec3Abi { float x,y,z; };` · `MoveToArgs{ Vec3Abi target; float maxSpeed; }` ·
`QueryByTagArgs{ uint32_t tag; uint32_t cap; }` + guest 提供的输出缓冲(指针+长度,由沙箱边界检查)。

### A.3 能力域(`next/gameapi/capability.h`)

```cpp
enum class Capability : uint32_t {        // 位偏移
    Observe = 0, Sense, Actuate, Comms, Tasks, Time, Log, SpawnEntities,
};
class CapabilitySet {                     // 位集;零分配;可静态审计
    uint32_t bits_ = 0;
  public:
    constexpr CapabilitySet& Grant(Capability c);
    constexpr bool Has(Capability c) const;
    static constexpr CapabilitySet PlayerDefault();   // Observe|Sense|Actuate|Comms|Tasks|Time|Log(无 Spawn)
    static constexpr CapabilitySet None();
};
```

### A.4 写即意图(确定性的关键)

- 每个写调用(`MoveTo/Stop/SetActionFlag/SendSignal/ReportProgress`)**只**追加一条 `Intent` 到 `IntentQueue`,并即时做**廉价校验**(自我归属、`maxSpeed≥0`、限流);**不**碰 World。
- sim 在 tick 边界:`GameApi::DrainIntents()` → `IntentResolver::Apply(world, intents, dt)` **按记录顺序**应用。`DefaultIntentResolver` 提供参考运动学(MoveTo:朝目标按 `min(maxSpeed*dt, dist)` 推进 Transform;Stop:清运动意图),游戏可替换。
- 时钟:`GameApi` 持 `SimClock{ uint64 tick; double seconds; }`,每 tick 由 sim 推进;`Time` 域只读它。无墙钟。

### A.5 确定性与校验不变量
1. `QueryByTag/SenseRadius` 结果按 `EntityId` 升序(确定迭代序)。
2. 所有越界缓冲 → `BufferTooSmall`(返回所需数量,不写溢出)。
3. 能力缺失 → `PermissionDenied`(门面入口 + gateway 双查,纵深防御)。
4. 写意图限流:每 tick 每实例 host-call 数受 `SandboxPolicy.maxHostCalls` 限;`Comms`/`Log` 再加每 tick 配额。

---

## B. 沙箱(`engine/sandbox`,库 `next_sandbox`)

### B.1 接口(后端无关)

```cpp
enum class TrapReason : int32_t {
    None = 0,           // 正常 Halt/Return
    FuelExhausted, OutOfMemory, BadMemoryAccess, IllegalInstruction,
    StackOverflow, StackUnderflow, DivideByZero, HostCallDenied, HostCallError,
};
struct RunResult { TrapReason trap; uint64_t fuelUsed; uint64_t hostCalls; int64_t ret; };

struct SandboxPolicy {                    // 安全预算
    uint64_t fuel = 1'000'000;
    uint32_t memoryBytes = 64 * 1024;
    uint32_t stackSlots = 1024;           // 操作数栈深度上限
    uint32_t callDepth  = 64;             // 调用栈深度上限
    uint64_t maxHostCalls = 4096;
    CapabilitySet capabilities = CapabilitySet::None();
};

struct HostGateway {                      // guest host-call 的唯一出口;能力 + 入参校验在此
    virtual Status Invoke(CallId id, void* guestMemBase, uint32_t guestMemSize,
                          uint32_t argsOff, uint32_t argsLen,
                          uint32_t retOff,  uint32_t retLen,
                          const CapabilitySet& granted) = 0;
    virtual ~HostGateway() = default;
};

struct ISandbox {
    virtual bool LoadModule(const uint8_t* code, size_t len, std::string* err) = 0;
    virtual RunResult Run(const SandboxPolicy&, HostGateway&, uint32_t entryPc, int64_t arg) = 0;
    virtual ~ISandbox() = default;
};
```

- `GameApiGateway`(`next_gameapi` 侧实现 `HostGateway`):把 `(guestMem+off,len)` 边界检查后转给 `AbiDispatch::HostCall(gameApi, id, args, ret)`;能力在此**再查一次**。

### B.2 参考 VM(`RefVm`)——security by construction

- **状态**:操作数栈(i64×stackSlots)、调用栈(帧:返回 pc + 局部 i64×K)、线性内存(`memoryBytes` 字节,零初始化)、pc、燃料计数。
- **内存**:仅 `LOAD/STORE{8,16,32,64}`,地址从栈取,**每次边界检查** `[addr, addr+w) ⊆ [0, memoryBytes)`,否则 `BadMemoryAccess`。
- **指令集**(1 字节 opcode + 定长立即数;每条扣 1 燃料,`HOSTCALL` 扣 `kHostCallFuel`):
  - 栈:`PUSH imm64 · POP · DUP · SWAP`
  - 局部:`LDLOC u16 · STLOC u16`
  - 内存:`LD8/16/32/64 · ST8/16/32/64`(地址、值从栈)
  - 整数:`ADD SUB MUL DIV MOD NEG`(DIV/MOD-0 → `DivideByZero`)
  - 位:`AND OR XOR NOT SHL SHR`
  - 浮点(在 i64 位上按 double 解释):`FADD FSUB FMUL FDIV · I2F F2I`(只此四则,确定性)
  - 比较:`EQ NE LTS LES GTS GES`(→ 0/1)
  - 控制:`JMP i32 · JZ i32 · JNZ i32 · CALL u32 · RET · HALT · NOP`
  - 外联:`HOSTCALL u32(=CallId)`——约定:栈顶依次 `retLen,retOff,argsLen,argsOff`(guest 线性内存偏移),弹出后调用 `gateway.Invoke(...)`,把 `Status`(i64)压回。VM 负责把 `[argsOff,argsLen)`/`[retOff,retLen)` 边界检查后交给 gateway。
- **安全论证**:指令集中**不存在**任何触达 arena 之外的指令;`HOSTCALL` 是唯一外联且全程被 gateway 中介(能力 + 边界)。燃料界 CPU,arena 界内存,trap 收敛故障。guest 是纯函数 → 确定性。
- **工具**:`BytecodeBuilder`(`next/sandbox/bytecode.h`)——带标签的程序化汇编器(`.PushI(x).HostCall(id).Ret()`),供测试与未来工具链构造模块;避免文本解析器。

### B.3 落地顺序与测试
1. `RefVm` + `BytecodeBuilder` + 纯 VM 单测(算术/控制/内存/trap)。
2. `GameApiGateway` + host-call 往返单测(MoveTo 意图、QueryByTag 排序、能力拒绝)。
3. **对抗性安全测试**:越界 LOAD/STORE、燃料耗尽、栈溢出、DIV-0、非法 opcode、未授权 host-call、host-call 指针越界、超 `maxHostCalls`——全部必须 trap 且不污染宿主(ASan 下)。
4. 垂直切片:guest 程序经沙箱调 Game API 下 `MoveTo` → tick 应用 → [sim↔UE5 边界](sim-ue5-boundary.md) 出快照。
