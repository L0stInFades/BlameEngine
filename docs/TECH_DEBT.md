# 技术债与缺口登记 (Tech Debt & Gaps)

**活文档** —— 按 [ADR-0005](adr/0005-ue5-renderer-jolt-headless-world.md) 的战略(渲染=UE5、物理=Jolt、自研核心=headless 权威世界 + 安全玩家代码运行时 + Game API)登记要建/要补的东西。
逐模块成熟度见 [`ARCHITECTURE.md`](ARCHITECTURE.md)。

## 最关键的一句话
先把 **headless 世界**做成可玩纵切面(无渲染器),护城河是 **Game API + WASM 玩家代码沙箱**。渲染/物理/内容工具不再是我们的负担(交给 UE5/Jolt)。

## P0 — 护城河核心(必须自研,要砸好钢)
| 缺口 | 说明 | 状态 |
|---|---|---|
| **Game API**(能力域化 / 版本化 / 确定性) | 玩家代码 / AI agent / UE5 视图三者唯一契约;整个架构的中心 | ✅ **v1 落地**(`next_gameapi`,[ADR-0007](adr/0007-game-api-contract.md));随玩法持续演进 |
| **玩家代码沙箱**(安全第一) | 宿主只暴露 Game API,燃料/内存限额、无逃逸;替换 `popen(python3)` 探针 | ✅ **落地**(`next_sandbox` + 参考 VM,[ADR-0008](adr/0008-player-code-sandbox.md));边界安全审计见 [sandbox-audit](security/sandbox-audit-2026-05-30.md)(39 项边界守住、ASan/UBSan 全清,1 中危发现 F-1);**玩家语言前端已落地**:现代 C++/Rust 编到 wasm32,经 `next_sandbox_wasm`(wasm3)运行,A*/二分查找在 headless 世界跑通([ADR-0011](adr/0011-wasm-language-frontend.md));**CPU 燃料也已落地**:加载期 gas 插桩让 WASM 模块自计费,无限循环 → `FuelExhausted`([ADR-0012](adr/0012-wasm-fuel-gas-metering.md)) |
| **sim↔UE5 状态复制层** | headless 权威状态 → UE5 视图镜像;进程内起步、网络/服务器权威设计 | ✅ **进程内落地**(`next_boundary`,[ADR-0006](adr/0006-sim-ue5-boundary.md));**待补**:跨进程 / 网络 transport |
| **物理**(权威/确定性/服务器侧) | `IPhysicsWorld` 抽象 + 确定性参考后端 + 射线 + ECS `PhysicsSystem`;Jolt 经 `BUILD_WITH_JOLT` 可选接入;意图→物理经 gameplay `ActuationSystem` 统一为单一 Transform 写者 | ✅ **落地**(`next_physics` + 可选 `next_physics_jolt` + `next_gameplay`,[ADR-0009](adr/0009-physics-jolt-backend.md)/[ADR-0010](adr/0010-actuation-single-transform-writer.md));**待补**:Jolt 跨平台确定性、dynamic 受力角色控制 |
| **headless 世界规则/状态接线** | 任务条件/动作接真实世界状态;世界状态可查询/可回放 | 部分(Game API 的 `Tasks`/意图 + `DefaultIntentResolver` 已起步);任务系统接线待续 |

## P1 — 重大
| 缺口 | 说明 | 粗估 |
|---|---|---|
| **沙箱 host-call 成本不对称**(F-1,[审计](security/sandbox-audit-2026-05-30.md)) | `Sense*`/`QueryByTag` 在门面是 O(N) 全世界扫描,但每个 `HostCall` 只收**固定 50 燃料**、只按**次数**(`maxHostCallsPerTick`)限流——guest 成本与 host 工作量脱钩(实测 N=4 与 N=4000 燃料同为 2754)。多 agent 权威服务器下是算法复杂度/非对称 DoS 面。修法:按工作量计费 / 给感知类单独紧配额 / 上空间加速(网格/BVH) | 周 |
| **AI-agent 工具面** | 把 Game API 以工具协议(MCP 式)暴露给 agent,帮玩家排任务/hack | 数周 |
| **复活 `engine/ops`(Ops Runtime 雏形)** | 旧 master 线有 ~1275 LOC 的 `ops_workspace`/`policy_simulation`/`python_worker`(沙箱化玩家代码执行的早期雏形),分支收敛时未移植;评估并把仍适用的逻辑迁到新 archetype ECS,作为 WASM 沙箱的参考/前身。来源:tag `archive/pre-blame`([ADR-0004](adr/0004-branch-consolidation.md)) | 数周 |
| 玩家代码编辑 UX 接入 UE5 | Neovim 表面嵌入 UE5(或伴随窗口) | 数周 |
| 确定性 | 玩家代码 + Jolt 驱动权威状态需可回放/反作弊;从设计起保证 | 持续 |
| 服务器权威 / 网络 | sim 出进程成为专用权威服务器 + UE5 客户端 | 月 |
| 序列化 schema 迁移;实体序列化/反射 | 存档与世界状态持久化 | 周 |

## P2 — 工程卫生
| 缺口 | 粗估 |
|---|---|
| 压缩 CMake 硬编码 lz4/zstd 路径 → `find_package`/包管理 | 天 |
| Windows/macOS CI 跑 ctest(补平台测试矩阵) | 周 |
| API 文档 + 端到端集成测试 | 周 |
| 跨运行稳定的 ComponentTypeID(序列化用) | 天 |
| UE5 许可/版本治理(Epic 抽成、版本升级流程)写入流程 | — |
| 一次性 style pass(枚举命名等) | 天 |
| 品牌 → 代码符号同步(`Next`/`next_*`/`NEXT_*` → Blame 的脚本化重命名) | 天(脚本化 + 验证) |
| `World::Each` 加 const 重载,Game API 的 Observe/Sense 读路径用 `const World*` + `const Components&`(类型层表达"只读",与写即意图一致;现为 runtime 限制) | 天 |
| 边界 lock-free 结构(`TripleBuffer`/`SpscRing`)补 **TSan CI job + 仓内双线程压测**(当前 SPSC 跨线程正确性仅靠 acquire/release 论证,无实跑回归) | 天 |
| ~~玩家语言前端 + WASM CPU 燃料~~ ✅ **已落地**([ADR-0011](adr/0011-wasm-language-frontend.md) + [ADR-0012](adr/0012-wasm-fuel-gas-metering.md)):C++/Rust → wasm32 → `next_sandbox_wasm`(wasm3),加载期 gas 插桩计 `SandboxPolicy::fuel`(无限循环 → `FuelExhausted`)。**剩**:把 HackOps 的 `popen(python3)` 真正切到这条路 | 周 |
| 沙箱浮点确定性(F-2,[审计](security/sandbox-audit-2026-05-30.md)):给 `next_sandbox`(及任何后端)显式 `-ffp-contract=off`,并把"禁用 `-ffast-math`"写入工程规范——guest 的 `FAdd/FSub/FMul/FDiv` 跨平台位级一致是回放/反作弊前提 | 天 |
| `SnapshotPublisher` 每 tick 重建 `std::map`(N 次红黑树分配 + 查找);实体规模变大后改为**有序 vector 归并 diff**(零分配、线性)。当前 map 版清晰正确,纯性能优化,不急 | 天 |
| `SenseRadius`/`SenseNearest` 用 `radius*radius`,半径 > ~1.8e19 时 `r2` 溢出为 +Inf → 退化为无界(游戏坐标不会到此量级;float 平方距离的固有限制,记录备查) | — |

## 🗑 已作废(不再是我们的负担,改由 UE5/Jolt)
- 自研渲染器/RHI 接线、GI/AO/阴影/反射/RT 出图、材质/shader 变体系统、frame graph 执行器 → **UE5**。
- 流送↔自研渲染打通、Transform 世界矩阵供渲染、无 Linux 渲染后端 → **UE5**(流送的 sim 侧兴趣管理仍可复用)。
- 内容创作工具(场景/材质编辑、FBX/PNG 导入、资产视口) → **UE5 编辑器**。
- 自研物理 → **Jolt**。

## 本轮已清偿(2026-05-30,关卡设计系统 engine/level)
- **数据驱动关卡系统**([ADR-0013](adr/0013-level-design-system.md),`next_level`):`LevelDef`(实体/组件/标签/目标/胜负条件,纯数据)+ 流式 `LevelBuilder` + **总校验门** `LevelValidator`(fail-closed,累计全部错误,~32 缺陷码)+ **事务化确定性** `LevelLoader`(校验通过才建,失败 World 不动;向量序 + std::map/set 确定)+ 只读 loss-priority `WinEvaluator`。复用 ECS + gameapi/physics/boundary 组件,**不碰**遗留 Task System。
- **用用户要求的 agent-workflow 闭环造出**:规划(3 架构师 + 质量骨架)→ 实现 → 严格 review → 修复 → 再 review …… 经 **5 轮对抗式 review 收敛(确认缺陷 9→3→3→2→0)**。review 抓出测试没覆盖的真实缺陷:未约束的 `ref.value` 致 ~32GB 分配、NaN MoveTarget 腐蚀 transform、多个静默永不触发/永不渲染、递归式环检测栈溢出、`LoadedLevel` 悬垂 def 指针(use-after-scope)。
- 测试:20 个(validator/loader/conditions/端到端 load→跑沙箱 guest→胜利触发);headless + ASan/UBSan 全绿;入 CI(`LevelTest`)。**待补**:序列化/持久化、复合条件树、运行时 spawn。

## 本轮已清偿(2026-05-30,WASM CPU 燃料计量)
- **加载期 gas 插桩**([ADR-0012](adr/0012-wasm-fuel-gas-metering.md),`engine/sandbox/wasm_meter.{h,cpp}`):wasm3 无原生燃料钩子,于是在 `LoadModule` 里把模块**改写成自计费**——append-only(不重排既有 func/global 索引)地追加一个导出的可变 i64 `__fuel` 全局 + 一个 `__gas(i32)` 函数(`fuel -= cost; if fuel<0 unreachable`),并在每个函数入口与每个 loop/if/else 头部插入 `i32.const cost; call __gas`。因为结构化 WASM 的每条后向边都指向 `loop` 头、每次调用都重入被计费的函数入口,**没有无界计算能绕过计费点**。host 在调用前用 `m3_SetGlobal` 注入 `SandboxPolicy::fuel`、调用后用 `m3_GetGlobal` 读余量(全局在 trap 后仍存活)→ 精确 `fuelUsed`;`__fuel<=0` 的 `unreachable` 映射为 `FuelExhausted`。无法解析的 opcode(SIMD/atomics)**fail-closed 拒绝**。
- **实证**(`tools/wasm_demo`):无限循环 guest 现在 `FuelExhausted`(不再挂起);计数循环在充足预算下完成(精确 fuelUsed 随工作量增长)、紧预算下 trap;真实 A*(C++)/二分查找(Rust)经插桩不变、仍通过。ASan 干净(meter 的字节运算无越界)。默认主干不受影响(wasm opt-in;headless 17/17)。
- 设计经 4-agent workflow 评估:选**插桩**而非 wasmtime(复用既有 `SandboxPolicy::fuel` 货币、零新依赖、保持纯解释器确定性、不在不可信边界内塞 JIT)。

## 本轮已清偿(2026-05-30,玩家语言前端:C++/Rust 进沙箱)
- **WASM 沙箱后端**([ADR-0011](adr/0011-wasm-language-frontend.md)):`next_sandbox_wasm`(`Wasm3Sandbox : ISandbox`,wasm3,经 `BUILD_WITH_WASM` + FetchContent,默认关,像 Jolt 一样可选)。一套 ABI 两个后端——WASM guest import `env.host_call(callId,argsOff,argsLen,retOff,retLen)`,**精确映射**到既有 `HostGateway::Invoke`,与手写 NBVM guest 说同一套 Game API。同样在 host-call 接缝复用 RefVm 的窗口边界检查 + host-call 配额;WASM 自身边界检查每次内存访问。
- **真把 C++/Rust 跑进来了**:`examples/wasm_guests` 在构建期把**现代 C++(C++23)的 A***和 **Rust(edition 2024)的二分查找**编到 wasm32;`tools/wasm_demo` 在真实 headless 世界上经 `Wasm3Sandbox` 运行——C++ guest 经 Game API 感知障碍、规划,**路径长度与 host BFS 真值逐步一致(24,强制绕路 > 曼哈顿 14)**并下 MoveTo;Rust guest 感知 beacon 后用 `core::slice::binary_search` 搜排好序的地图。8/8 校验通过。
- **限制**:wasm3 不计 CPU 燃料(`SandboxPolicy::fuel` 在该后端不生效;内存安全/能力门控/host-call 配额仍生效)。RefVm 仍是燃料计量的确定性参考后端;生产用 wasmtime `consume_fuel` 或加载期插桩。已登记 P2。
- 默认主干不受影响(wasm 全程 opt-in):headless 预设 17/17 ctest 仍绿。

## 本轮已清偿(2026-05-30,操控统一 + 物理感知)
- **操控单一 Transform 写者**([ADR-0010](adr/0010-actuation-single-transform-writer.md)):新增 gameplay 层 `engine/gameplay`(`next_gameplay`)。`ActuationSystem` 把 `MoveTo` 意图按"是否物理实体"二选一移动——物理实体→设 body 速度(物理独占写 Transform),非物理→直接积分 Transform。清除了 resolver 与物理争抢 Transform 的债;gameapi 与 physics 仍互不依赖,由 gameplay 桥接。
- **物理射线 + Game API 感知**:`IPhysicsWorld::Raycast`(参考后端 AABB slab + Jolt `CastRay`);gameapi 加抽象 `IWorldQuery` + `Sense` 域 `Raycast` 调用(physics 无关);gameplay `PhysicsWorldQuery` 实现它并把命中 body 映回 ECS 实体。玩家代码现在能经能力门控的 Game API 物理感知世界(视线/探测)。
- 测试:gameapi 21、gameplay 6、physics 9(含双后端射线);全 16 套 ctest + ASan 绿;`test_actuation` 入 CI sanitizers 矩阵。

## 本轮已清偿(2026-05-30,物理接入)
- **物理 = 可换能力,非绑定 Jolt**([ADR-0009](adr/0009-physics-jolt-backend.md)):`engine/physics`(`next_physics`)= `IPhysicsWorld` 抽象 + 确定性参考后端(重力 + 半隐式欧拉 + AABB 静态碰撞)+ ECS `RigidBodyComponent` + `PhysicsSystem`(固定步、按 ECS hook 建/销 body、把 body transform 写回 `TransformComponent` → 顺 [边界](design/sim-ue5-boundary.md)管线到 UE5)。
- **Jolt 真接进来了**:`next_physics_jolt`(`JoltPhysicsWorld`)经 `BUILD_WITH_JOLT=ON` + FetchContent 拉 JoltPhysics v5.2.0,编译/链接/仿真全通过(单线程确定性 job system;球落到静态地板上正确停住的冒烟测试绿)。核心 `next_physics` **从不引用 Jolt 符号**;默认 `headless`/`asan` 预设不拉、不建 Jolt,主干 15/15 + ASan 全绿不依赖网络。
- 启用:`cmake -S . -B <dir> -DBUILD_WITH_JOLT=ON`(其余照常)。

## 本轮已清偿(2026-05-30,护城河纵切面)
- **Game API** `next_gameapi`([ADR-0007](adr/0007-game-api-contract.md)):能力域化 `CapabilitySet`、冻结扁平 C-ABI(`abi.h` + `AbiDispatch`)、`GameApi` 门面、**写即意图** + `DefaultIntentResolver`、确定性时钟。
- **玩家代码沙箱** `next_sandbox`([ADR-0008](adr/0008-player-code-sandbox.md)):后端无关 `ISandbox` + 安全契约;自研确定性燃料计量 VM(`RefVm`,零环境权限、唯一外联=能力门控 host-call);`GameApiGateway` 纵深防御。对抗性测试(越界/燃料/栈/DIV0/非法指令/未授权 host-call)全绿。
- **sim↔UE5 边界** `next_boundary`([ADR-0006](adr/0006-sim-ue5-boundary.md)):无锁三重缓冲 + SPSC 命令/事件环 + `ISnapshotTransport`/`InProcessTransport` + `SnapshotPublisher`(ECS 脏集→spawn/update/despawn 增量)。
- **垂直切片**(headless,无渲染器):沙箱 guest 经 Game API 感知目标→下 MoveTo→tick 应用→边界出快照,且可确定性回放。
- 全部 4 个新测试套(`GameApiTest`/`SandboxTest`/`BoundaryTest`/`VerticalSliceTest`)纳入 CI sanitizers 矩阵,ASan/UBSan 通过。

## 本轮已清偿(2026-05)
- 资产压缩端到端、`.npkg` v2 + 内容哈希 ID + 依赖 manifest;ECS 重写为 archetype 数据导向([ADR-0002](adr/0002-archetype-ecs.md));流送每帧预算;工业级质量工具链([ADR-0003](adr/0003-quality-gates.md),并修复了一个关机期 UAF);文档重构为"公司核心资产、多游戏"结构([ADR-0001](adr/0001-engine-is-company-core-asset.md));确立 UE5+Jolt+headless 战略([ADR-0005](adr/0005-ue5-renderer-jolt-headless-world.md));收敛远端分支为单一 `master` 主线([ADR-0004](adr/0004-branch-consolidation.md))。
