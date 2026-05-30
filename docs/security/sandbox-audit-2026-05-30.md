# 玩家代码沙箱 — 边界条件安全审计报告

| | |
|---|---|
| **审计对象** | `engine/sandbox`(`RefVm` 参考后端)+ `engine/gameapi`(Game API 契约 / 网关) |
| **依据** | [ADR-0007](../adr/0007-game-api-contract.md)、[ADR-0008](../adr/0008-player-code-sandbox.md) |
| **代码版本** | `7b352cc`(`master`) |
| **方法** | **白盒审计 + 在真实 headless 世界上跑对抗性玩家代码**。审计工具 `tools/sandbox_audit` 启动一个真实的权威世界(ECS World + GameApi + ObjectiveStore + 参考物理),经生产路径 `GameApiGateway` 接上沙箱,然后用手写的 guest 字节码逐项打边界。没有 mock。 |
| **复现** | `cmake --preset headless && cmake --build out/build/headless --target sandbox_audit && ./out/build/headless/bin/sandbox_audit` |
| **结果摘要** | 39 项安全边界**全部守住(0 失败)**;1 项**中危**设计发现(host-call 成本不对称);4 项低危/加固建议。整体安全姿态:**强**。 |

> 术语:`[SAFE]` = 该安全边界按要求守住;`[FIND]` = 发现/加固项。下文每条断言后括注的 `C#/A#/B#` 对应审计工具实际跑过的实验,可在 `tools/sandbox_audit/src/main.cpp` 复核。

---

## 0. 执行摘要

这套沙箱的安全模型是**自研的确定性栈式字节码 VM**,而**不是**把原生代码关进笼子。它的安全性来自一个朴素但有力的事实:**这套指令集里根本没有可以触碰 operand 栈 / 当前帧 locals / 线性内存以外任何东西的指令**;唯一的出口是 `HostCall`,而 `HostCall` 被 `HostGateway` 完全中介。这种"没有逃逸原语"的设计,比"事后拦截"更可靠。

在此之上有两道纵深防御能力环(网关外环 + GameApi 门面内环,取**交集**),以及一套硬上限预算(燃料 / 内存 / 栈深 / 调用深 / host-call 次数)。审计跑下来,**所有内存安全、能力门控、确定性、故障隔离、控制流完整性边界都守住了**。

值得正视的不是"逃逸",而是一类**成本不对称**:某些 host-call(`SenseRadius` / `QueryByTag` / `SenseNearest`)在 host 侧是 O(N) 全世界扫描,但对 guest 只收**固定 50 燃料**,且只按"次数"限流、不按"工作量"计价。审计实测:对 N=4 与 N=4000 的世界,同一段 guest 消耗的燃料**完全相同(2754)**,而 host 工作量是 O(K·N)。在多 agent 权威服务器场景下,这是一个真实的算法复杂度 / 非对称 DoS 面(见 [F-1](#f-1-host-call-成本不对称中危))。

---

## 1. 暴露的 API 边界(Part A)

玩家代码能触达 host 的**唯一**方式是 `Op::HostCall`,它携带一个 `CallId` 和两段(已被 VM 边界检查过的)guest 内存窗口 `[argsOff,argsLen)` / `[retOff,retLen)`。网关把窗口翻译成 host 指针,做能力复检,转给 `AbiDispatch`,后者按下表解码 POD、调用 `GameApi` 门面。

**共 16 个 host-call,按能力域(`Capability`)分组。** 全部以 `PlayerDefault` 能力集在真实世界上实跑验证:

| CallId | 能力域 | 参数结构 | 结果结构 | 实测行为(证据) |
|---|---|---|---|---|
| `GetTick` | Time | — | `TickResult{u64}` | 返回权威时钟 tick=7 ✓(A) |
| `GetTimeSeconds` | Time | — | `SecondsResult{f64}` | 返回 0.7s ✓(A) |
| `Self` | Observe | — | `EntityResult{EntityId}` | 返回受控实体 id ✓(A) |
| `IsValid` | Observe | `EntityArg` | `BoolResult` | self→1,伪造 id→0 ✓(A) |
| `GetPosition` | Observe | `EntityArg` | `PositionResult{Vec3}` | self 在原点,x=0 ✓(A) |
| `QueryByTag` | Observe | `QueryByTagArgs{tag}` | `EntityListHeader`+`EntityId[]` | tag=5 命中 2 个 prop ✓(A) |
| `SenseRadius` | Sense | `SenseRadiusArgs{radius}` | `EntityListHeader`+`EntityId[]` | r=4 含边界命中 2 个(t1@3、t3@4)✓(A) |
| `SenseNearest` | Sense | `SenseNearestArgs{radius,tag}` | `SenseNearestResult{entity,dist}` | tag5 最近 dist=3 ✓(A) |
| `MoveTo` | Actuate | `MoveToArgs{Vec3,maxSpeed}` | — | 记录 1 条 Actuate 意图(非直接写世界)✓(A) |
| `Stop` | Actuate | — | — | 记录意图 ✓(A) |
| `SetActionFlag` | Actuate | `SetActionFlagArgs{action,on}` | — | action≥32 → `OutOfRange` ✓(A) |
| `SendSignal` | Comms | `SendSignalArgs{channel,code,payload}` | — | 记录 Comms 意图 ✓(A) |
| `GetObjective` | Tasks | `ObjectiveArg{id}` | `ObjectiveResult{state}` | id=42→100;未知 id→`NotFound` ✓(A) |
| `ReportProgress` | Tasks | `ReportProgressArgs{id,delta}` | — | 记录 Tasks 意图 ✓(A) |
| `Log` | Log | `LogArgs{level,len}`+bytes | — | 捕获 guest 字节;%元字符当数据 ✓(A/C12) |
| `Raycast` | Sense | `RaycastArgs{origin,dir,maxDist}` | `RaycastResult{...}` | +x 命中静态墙 hit=1;无物理时 `Unsupported` ✓(A) |

### 边界设计要点(均经验证)

1. **写即意图,不直接改世界。** 所有 Actuate/Comms/Tasks 调用只往本上下文的意图队列追加一条**已校验**的 `Intent`;世界的修改由 resolver 在 tick 边界统一施加。`GameApi` 持 `const World*`,门面**从不**改世界。→ 玩家代码无法绕过仲裁直接写权威状态。
2. **两道能力环取交集。** 网关用 policy 的 `granted` 集做外环复检(未知 id 映射到 `Capability::Count_`,恒拒);`GameApi` 门面用自己的 `caps_` 做内环。**更严的一层赢**(C7/C8 实证:policy 给了 Actuate 但门面是 Observer,仍被拒)。
3. **逐参数定义域校验。** 每个浮点入口走 `IsFinite`(拒 NaN/Inf),半径拒负,tag 限 `0..63`,action 限 `<32`。→ 杜绝 NaN 投毒、越界 tag。
4. **per-tick 限流。** `maxHostCallsPerTick`(默认 4096)、`maxCommsPerTick`(64)、`maxLogsPerTick`(64);`BeginTick()` 每 tick 复位。能力检查在计费**之前**,被拒的调用不耗预算。
5. **确定性时钟。** 时间只来自 `SimClock`,无墙钟;为回放 / 反作弊 / 服务器权威打底。

---

## 2. 语言 / ISA 支持情况(Part B)—— 诚实结论

**直接回答"支持到哪个标准、哪些用法":目前没有任何高级语言标准。** 玩家代码 = **自研 `NBVM v1` 字节码**,经 `BytecodeBuilder` 构造。**还没有** C / Rust / AssemblyScript / WASM 前端 —— 这正是路线图上"玩家语言前端"这个已知缺口(见 [TECH_DEBT](../TECH_DEBT.md))。所以本节描述的是**指令集能力**,不是某个语言标准的覆盖度。

### ISA 形态(`engine/sandbox/include/next/sandbox/bytecode.h`)

- **栈式机**,46 个 opcode(0..45),1 字节 opcode + 定长小端操作数。
- **数据类型**:单一 64 位 cell。整数按 2's-complement;浮点是把 cell 当 **IEEE-754 double 位型**(`I2F`/`F2I` 显式转换)。无独立类型系统、无类型检查。
- **算术**(实测):`Add/Sub/Mul` 用无符号运算实现 → 溢出**回绕有定义,无 UB**(B:INT64_MAX+1 → INT64_MIN);`Div/Mod` 除零陷入,`INT64_MIN/-1` 有定义(=INT64_MIN / 0);`Neg(INT64_MIN)=INT64_MIN`。
- **位运算/移位**:`And/Or/Xor/Not/Shl/Shr`;移位量掩码到 `0..63`(B:`1<<64==1`);`Shr` 是**可移植算术右移**(用无符号运算重写,绕开 C++ 实现定义行为 → 跨平台位级一致)。
- **浮点**:`FAdd/FSub/FMul/FDiv` + `I2F/F2I`。`FDiv` 除零按 IEEE-754 出 ±inf,**不陷入**(B 实证);`F2I(NaN)=0`、`F2I(±1e300)` 饱和到 `INT64_MIN/MAX`(**无 UB**)。
- **内存**:`Ld8/16/32/64`、`St8/16/32/64`,对**固定大小的线性内存 arena**寻址。
- **控制流**:`Jmp/Jz/Jnz`(相对)、`Call`(绝对)/`Ret`,帧含**固定 16 个 locals**(越界 → `IllegalInstruction`,B 实证)。
- **唯一外联**:`HostCall`。

### 刻意不存在的原语(=安全面的一部分)

无堆 / 动态分配、无间接 / 计算跳转(函数指针)、无系统调用、无 FMA(避免编译器收缩破坏确定性)、**无任何指向 host 内存的指针**。guest 能表达任意整数 / 定点 / 浮点计算与有界控制流,但表达不出"逃逸"。

---

## 3. 安全审计 —— 边界条件(Part C/D)

### 3.1 确认守住的边界(证据见括注)

| 边界 | 攻击向量 | 结果 | 证据 |
|---|---|---|---|
| **内存读写越界** | 负地址 / 超界 / 跨界 / 微小 arena 宽加载 | `BadMemoryAccess` 陷入 | 既有 test + C16 |
| **host-call args 窗口越界** | `argsLen=-1`(巨大无符号) | 调网关**前** `BadMemoryAccess` | C3 |
| **host-call ret 窗口跨界** | `retOff` 贴 arena 末尾、`retLen` 装不下结构体 | `BadMemoryAccess` 陷入 | C4 |
| **`argsLen=0` + 野 offset** | VM 在 len=0 时跳过 offset 检查 | 网关传 `nullptr`,dispatch → `InvalidArgument`,无越界 | C1 |
| **`retLen=0` + 野 offset** | 同上 | `WriteRet` 拒写 → `BufferTooSmall`,无越界 | C2 |
| **欠尺寸 ret 缓冲溢写** | `retLen` 小于结构体 | `BufferTooSmall` 且**不写**;窗口外 canary 完好 | C5 |
| **args/ret 窗口重叠** | 同区作 args 与 ret | dispatch 先 memcpy 出 args 再写 ret → 结果正确,无自毁 | C11 |
| **未知 / 非法 CallId** | id=9999 | `CapabilityFor→Count_` → `PermissionDenied`(值,非陷入),不达门面 | C6 |
| **能力越权** | Observer guest 调 `MoveTo` | `PermissionDenied`(值),无意图,世界不动 | C7 |
| **纵深防御绕过** | policy 放行但门面无该能力 | 交集仍拒 | C8 |
| **环境权限(ambient authority)** | 不做 host-call,狂写自己 arena | `hostCalls=0`,世界完全不受影响 | C9 |
| **跨 run 状态泄漏** | 同一 `RefVm` 连续两次 `Run` | 第二次 arena 全 0,无残留 | C10 |
| **格式化字符串注入** | `Log` 喂 `%n%n%s%x%p` | 当**数据**打印(`%.*s`),无注入无崩溃 | C12 |
| **限流绕过** | 超 `maxComms` / `maxHostCallsPerTick` | `RateLimited` | C13/C14 |
| **NaN/Inf 投毒** | `MoveTo` 目标含 NaN | `InvalidArgument`,无意图 | C15 |
| **原生栈打爆** | 无限递归 | `callDepth` 上限 → `StackOverflow`,不触原生栈 | C17 |
| **控制流劫持** | 越界跳转 | 跳转目标对 codeLen 边界检查 → `IllegalInstruction` | 既有 test + C18 |
| **燃料 / 故障隔离** | 死循环 / host-call 抛异常 | `FuelExhausted` / 异常转 `HostCallError` 陷入,**绝不**穿回 host | 既有 test |

**结论:在内存安全、能力门控、确定性、故障隔离、控制流完整性这些"硬"安全属性上,未发现任何逃逸或破坏路径。** 这套设计是扎实的。

### 3.2 发现与加固项

#### F-1. host-call 成本不对称(**中危**;权威服务器场景下偏高)

- **现象**:`SenseRadius` / `QueryByTag` / `SenseNearest` 在门面里是 `world_->Each<...>` **O(N) 全世界扫描**;但 VM 对每个 `HostCall` 只收**固定 50 燃料**,且 per-tick 只按**次数**(`maxHostCallsPerTick` 默认 4096)限流,**不按工作量计价**。
- **实测证据(Part D)**:同一段做 50 次 `SenseRadius` 的 guest,在 N=4 与 **N=4000** 的世界里消耗燃料**完全相同 = 2754**;host 工作量却是 O(K·N)。燃料模型完全没有为这次扫描定价。
- **影响**:单 agent 每 tick 可迫使 `4096 × O(N)` 次实体访问;M 个 agent 则 `M × 4096 × N`。N=1e4、M=100 时即约 `4×10⁹` 次访问/tick —— 对多 agent 权威服务器是真实的算法复杂度 / 非对称拒绝服务面。对单机单 agent 的 HackOps 当前形态影响小,但这是护城河面向"服务器权威"演进时**必须**在上线前解决的。
- **建议(任选其一或组合)**:
  1. **按工作量计费**:`HostCall` 燃料 ∝ 实际扫描 / 写出的实体数(把 host 成本反映到 guest 预算里)——最对症。
  2. 给感知类调用单独的、更紧的 per-tick 配额(独立于通用 host-call 预算)。
  3. 引入空间加速结构(网格 / BVH),把 O(N) 降到 O(log N + hits)——同时也呼应 [TECH_DEBT](../TECH_DEBT.md) 里 `SenseRadius` 的既有条目。

#### F-2. 浮点确定性依赖编译开关(**低危**;影响回放/反作弊)

- `FAdd/FSub/FMul/FDiv` 直接用宿主 `double`。这 4 个基本运算在符合 IEEE-754 的平台上是**正确舍入**的,本身跨平台位级一致;ISA 也刻意没有超越函数 / FMA。但**没有任何构建层面的保证**禁止编译器做浮点收缩(`-ffp-contract`)或 `-ffast-math` —— 一旦启用,guest 浮点会跨平台分叉,破坏回放 / 反作弊的位级一致前提。
- **建议**:对 `next_sandbox`(及未来任何后端)显式加 `-ffp-contract=off`,并在工程规范里把"禁止 `-ffast-math`"写成确定性硬约束。

#### F-3. 无加载期(静态)字节码校验(**低危 / 信息**)

- `LoadModule` 只校验 magic / version / codeLen;分支目标、栈效应、可达性都是**运行期惰性校验**(每步都检查,所以对 `RefVm` 是安全的)。代价:畸形模块只在**运行时**才陷入,而非加载时被拒。
- **建议**:未来上生产级 WASM 后端时,采用**加载期验证**(WASM 标准本就要求);`RefVm` 可选做一遍轻量静态检查以更早报错。当前不构成安全风险。

#### F-4. 两个 host-call 上限语义易混(**低危 / 文档**)

- `SandboxPolicy::maxHostCalls`(VM,限**单次 `Run()`**)与 `GameApiConfig::maxHostCallsPerTick`(门面,限**一个 tick**)默认值相同(4096)但作用域不同。容易误配。
- **建议**:在两处注释里交叉说明其作用域关系;若设计上一 tick 恒为一次 `Run`,可考虑合并或显式文档化。

#### F-5. 每次 `Run()` 重新分配 arena/栈/帧(**低危 / 性能**)

- arena(`std::vector<uint8_t>(memoryBytes)`,可达 256 MiB 上限)、operand 栈、帧向量都在每次 `Run()` 堆分配。超大 arena 请求已被正确转成 `OutOfMemory` 陷入(非崩溃);但高频(多 guest × 多 tick)下堆分配有抖动。
- **建议**:跨 `Run()` 复用 / 池化这些缓冲(`RefVm` 实例已只持有 code,天然适合持有可复用的执行缓冲)。纯性能,无安全含义。

---

## 4. 复现与产物

- **审计工具**:`tools/sandbox_audit`(`sandbox_audit` 目标)。其 stdout 即本报告 3.1/3.2 的证据来源;退出码在任一 `[SAFE]` 边界失守时为非 0(可被 CI / 人工 gate)。
- **永久回归**:本轮新发现的边界断言已固化进 `tests/sandbox/test_sandbox_boundary.cpp`,纳入 ctest + CI sanitizers 矩阵(在 ASan/UBSan 下持续验证),不再是一次性结论。
- **本次运行**:39 `[SAFE]` / 15 `[INFO]` / 1 `[FIND]` / **0 失败**。

## 5. 总评

**沙箱的核心安全契约(零环境权限、能力门控、内存安全、确定性、故障隔离、控制流完整性)经真实 headless 世界上的对抗性玩家代码验证,全部成立 —— 这是一套精品级的安全设计。** 唯一需要在"服务器权威"上线前正面解决的是 **F-1 的成本不对称**;F-2~F-5 为低危加固。需要补齐的能力不是安全,而是**玩家语言前端**(把高级语言编译到本 ISA / WASM)—— 那是功能路线图,不是安全缺口。
