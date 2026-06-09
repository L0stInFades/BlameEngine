# 技术债与缺口登记 (Tech Debt & Gaps)

**活文档** —— 按 [ADR-0005](adr/0005-ue5-renderer-jolt-headless-world.md) 的战略(渲染=UE5、物理=Jolt、自研核心=headless 权威世界 + 安全玩家代码运行时 + Game API)登记要建/要补的东西。
逐模块成熟度见 [`ARCHITECTURE.md`](ARCHITECTURE.md)。

## 最关键的一句话
先把 **headless 世界**做成可玩纵切面(无渲染器),护城河是 **Game API + WASM 玩家代码沙箱**。渲染/物理/内容工具不再是我们的负担(交给 UE5/Jolt)。

## 🔴 审计(2026-06-03,92-agent workflow):usable ≠ artist-deliverable + 4 降级 + 3 BLOCKER

对全部 9 个 `usable` 模块按「能像 UE5/Unity 交给美术/设计师用」+ 无性能问题 + 无重大 bug 核验(构建绿、30/30 ASan 为前提)。结论:**0/9 artist-deliverable**(架构使然:渲染器/编辑器/imgui 已删,创作委托给仓内不存在的 UE5 工程);**4 个连 usable 都撑不起,降级 prototype**;**3 个 BLOCKER**。
**2026-06-10 状态:3 个 BLOCKER 已全部修复(B1/W13、B2、B3),F-1 已缓解**;详见下表与「本轮已清偿(2026-06-10)」。

**🔴 BLOCKER(先修):**
| # | 模块 | Bug | 证据 |
|---|---|---|---|
| ~~B1~~ ✅ **已修(W13)** | boundary | ~~`SnapshotPublisher` 发相对「上一已发布帧」的 delta、无条件推进基线,却发进故意丢帧的 TripleBuffer → delta 永久丢失、UE5 镜像永久错位~~ → **修复**:delta 改为相对**最后已 ack 的基线**(`DeltaMode::Reliable` 默认)+ 有界 inflight 历史 + **keyframe 回退**(无 ack→每帧整帧);新增 `SnapshotReceiver`(镜像 + baseline 不匹配则 skip、stale 拒绝)+ transport 加 ack 反向通道。`test_boundary_reliability` 证明重丢帧下镜像**收敛**、无永久错位 | 已修复并测试 |
| ~~B2~~ ✅ **已修(2026-06-10)** | sandbox_wasm | ~~64KiB 内存红线(`policy.memoryBytes`)在 wasm 后端**完全未实施** → guest 线性内存只受模块自报 + wasm3 的 2GiB 顶 = 内存炸弹 host-OOM;`callDepth` 也未强制~~ → **修复**:`rt->memoryLimit` 把 wasm3 为 guest 做的**每一次**线性内存分配(初始 + `memory.grow`)按字节钳在 policy 红线内;声明初始内存超 policy 的模块在 Run 入口**确定性 `OutOfMemory` 拒绝**(与 RefVm arena 检查对等);`callDepth` 经解释器栈预算强制(每帧 2KiB 额度,递归炸弹 → `StackOverflow` 而非耗尽宿主)。新增 `test_sandbox_wasm`(手工编码 guest:256MiB 内存炸弹 / grow 炸弹 / 递归炸弹 / 零内存 policy,**无需 wasm 工具链**)入 ctest + CI sanitizer 矩阵 | `wasm_sandbox.cpp` + `tests/sandbox/test_wasm_sandbox.cpp`,ASan 绿 |
| ~~B3~~ ✅ **已修(2026-06-10)** | asset | ~~渲染面读视图 `GetMeshAssetView`/`GetTextureAssetView`/`GetMaterialAssetView` 返回裸 `const void*`,保活 `shared_ptr` 函数返回即析构 → 并发 `UnloadAsset` 即 **use-after-free**~~ → **修复**:三个视图结构体携带 `shared_ptr<const void> keepAlive` 共享所有权——视图(及其拷贝)存活期间 payload 永不悬垂,卸载后读取安全。回归测试 `MeshAssetViewSurvivesAssetUnload`(Release + UnloadPackage 后 memcmp 原始字节,ASan 下无修复即炸) | `asset_manager.{h,cpp}` + `test_asset_integration.cpp`,ASan 绿 |

**完整性闸门有水分**:招牌「30/30 ASan 绿」**排除了最安全敏感/并发最复杂的代码**——`BUILD_WITH_WASM=OFF` + `BUILD_TERMINAL=OFF`,即整个 WASM 前端 + 537 行手写 wasm 改写器 + 整个 Neovim 层都不在门内。**2026-06-10 部分收口**:WASM 后端现在有专门 ctest 套件(`WasmSandboxTest`,手工编码 guest 无需工具链)并以 `wasm-sanitizers` CI job 跑在 ASan/UBSan 下;Windows 也补了 `windows-headless` CI job(MSVC + Ninja 全量 ctest)。**仍在门外**:terminal(Neovim 层,零单测)。

**降级 usable → prototype** 的 4 个及其要害:
- **serialization**:World 存档序列化器是未实现孤儿头(无 `.cpp`/不在 CMake);Binary `GetObjectKeys()` 恒 false → map 静默丢数据;JSON 无深度上限 → 不可信文件栈溢出 DoS;`ReadInt32/64` 越界 cast → UBSan abort;`Compact` 枚举 → 空指针崩溃。
- **terminal**:不在 ASan 门内、零单测;nvim 缺失即 SIGPIPE 杀宿主;`SendInput` 每键 ≥500ms 阻塞;仅单色文本快照。
- **sandbox_wasm**:见 B2(✅ 内存/调用深度已修,2026-06-10);另每 `Run()` 重建整个 VM(无实例缓存),meter 对无 global 段的合法 wasm 误拒。
- **boundary**:~~见 B1;无锁并发从未跑 TSan;跨进程/网络 transport 不存在(仅 InProcess)~~ → **全部已补**:B1 已修(W13);无锁 `SpscRing`/`TripleBuffer` 上 **TSan CI job + 双线程压测**(W16,`test_boundary_concurrency`);**跨进程/UDP transport 已落地**(W15,`DatagramTransport` over `IDatagram` + 真 POSIX `UdpDatagram` + 序列化 `snapshot_codec`,丢包下镜像收敛);**服务器权威时钟**(W14,`SnapshotReceiver` 单调跟随 + `RenderClock` 不越权外推)。

**仍 programmer-usable(干净/无崩溃),但非 artist-deliverable** 的 5 个仍有真实债:
- **gameapi**:无正确性 bug;~~但 `QueryByTag`/`SenseRadius`/`SenseNearest` 是 O(N) 全世界扫无空间索引,每 host-call 平价 50 燃料、仅按次数限流 → **非对称 DoS**(F-1 仍 OPEN;实测 N=4 与 N=4000 同为 2754 燃料)~~ → **F-1 已缓解(2026-06-10)**:三个 O(N) 扫描共享独立紧配额 `maxWorldScansPerTick`(默认 256/tick,校验通过后、扫描前计费;`GameApiRateLimit.WorldScanQuotaBoundsONScans` 测试钉死)。按工作量计费/空间索引仍是后续优化。`SpawnEntities`/Comms 信号为声明未接的死面。
- **asset**(除 B3):**假热重载**(注释称持柄透明拾取热重载,实际 cache-hit 只 AddRef 不重读、无 mtime/Reload → 返回陈旧数据);「content-hash ID」实为**名字串的 CRC64**;「依赖 manifest」运行时**无读取器**;材质编译是**硬编码 TestPBR stub**;位串行 CRC64 全量跑加载热路径(多 GB 包阻塞数秒)+ 每载双拷贝。
- **sandbox RefVm**:每 `Run()` 堆分配并清零整个 arena(64KiB→16MiB 实测 0.67ms→181ms/run,100% 是 setup);真实游戏 `game/hackops/src/main.cpp:111` **仍用 `popen(python3)`**、不链接 `next_sandbox`(ADR-0008 要消灭的反模式);F-2 浮点确定性未用 `-ffp-contract=off` 编译期强制。
- **level / vegetation**:库本身稳健、fail-closed、ASan 绿;但 C++-only 编写、无序列化/存盘(level)、UE5 端 mock、broadphase 全局半径永不收缩(vegetation)。

**系统性性能债**:全引擎**零基准 / 零 profiling / 零大 N 测试**,性能结论建立在「正确性」上,最大实测规模玩具级(6–10 实体)对着声称的 10k–1M。最贵几处:gameapi F-1;RefVm/wasm3 每 Run 重建;asset 位串行 CRC64 + 双拷贝;boundary `BuildDelta` 每 tick 重建全量 `std::map`(10k 实体 @60Hz ≈ 60 万 alloc/free/秒,无脏集,打脸自己「稳态零分配」);默认 JSON 序列化 ~10× 内存膨胀、写慢 21×。

## P0 — 护城河核心(必须自研,要砸好钢)
| 缺口 | 说明 | 状态 |
|---|---|---|
| **Game API**(能力域化 / 版本化 / 确定性) | 玩家代码 / AI agent / UE5 视图三者唯一契约;整个架构的中心 | ✅ **v1 落地**(`next_gameapi`,[ADR-0007](adr/0007-game-api-contract.md));随玩法持续演进 |
| **玩家代码沙箱**(安全第一) | 宿主只暴露 Game API,燃料/内存限额、无逃逸;替换 `popen(python3)` 探针 | ✅ **落地**(`next_sandbox` + 参考 VM,[ADR-0008](adr/0008-player-code-sandbox.md));边界安全审计见 [sandbox-audit](security/sandbox-audit-2026-05-30.md)(39 项边界守住、ASan/UBSan 全清,1 中危发现 F-1);**玩家语言前端已落地**:现代 C++/Rust 编到 wasm32,经 `next_sandbox_wasm`(wasm3)运行,A*/二分查找在 headless 世界跑通([ADR-0011](adr/0011-wasm-language-frontend.md));**CPU 燃料也已落地**:加载期 gas 插桩让 WASM 模块自计费,无限循环 → `FuelExhausted`([ADR-0012](adr/0012-wasm-fuel-gas-metering.md)) |
| **sim↔UE5 状态复制层** | headless 权威状态 → UE5 视图镜像;进程内起步、网络/服务器权威设计 | ✅ **进程内落地**(`next_boundary`,[ADR-0006](adr/0006-sim-ue5-boundary.md));**待补**:跨进程 / 网络 transport |
| **物理**(权威/确定性/服务器侧) | `IPhysicsWorld` 抽象 + 确定性参考后端 + 射线 + ECS `PhysicsSystem`;Jolt 经 `BUILD_WITH_JOLT` 可选接入;意图→物理经 gameplay `ActuationSystem` 统一为单一 Transform 写者 | ✅ **落地**(`next_physics` + 可选 `next_physics_jolt` + `next_gameplay`,[ADR-0009](adr/0009-physics-jolt-backend.md)/[ADR-0010](adr/0010-actuation-single-transform-writer.md));**已补**(ADR-0015,两后端):`AddForce`/`AddImpulse` 施力底座 + `AddTorque`/`AddForceAtPosition`/`GetAngularVelocity` 角动力学(reference 对角惯量 + 四元数角积分)——水体多点浮力 / **船只自扶正**在用;**待补**:Jolt 跨平台确定性、精确世界惯量张量、把受力控制接到玩家角色操控 |
| **headless 世界规则/状态接线** | 任务条件/动作接真实世界状态;世界状态可查询/可回放 | 部分(Game API 的 `Tasks`/意图 + `DefaultIntentResolver` 已起步);任务系统接线待续 |

## P1 — 重大
| 缺口 | 说明 | 粗估 |
|---|---|---|
| ~~**沙箱 host-call 成本不对称**(F-1,[审计](security/sandbox-audit-2026-05-30.md))~~ ✅ **已缓解(2026-06-10)** | ~~`Sense*`/`QueryByTag` 在门面是 O(N) 全世界扫描,但每个 `HostCall` 只收**固定 50 燃料**、只按**次数**(`maxHostCallsPerTick`)限流~~ → O(N) 世界扫描类(QueryByTag/SenseRadius/SenseNearest)现共享**独立紧配额** `maxWorldScansPerTick`(默认 256/tick),guest 无法再把平价调用放大成无界 host 工作。**剩余**(降级 P2):按工作量计费、空间加速(网格/BVH) | ~~周~~ 已落地 |
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
| ~~Windows/macOS CI 跑 ctest(补平台测试矩阵)~~ ✅ **已补(2026-06-10)**:`windows-headless` CI job(MSVC + Ninja,全量 ctest);macOS 已由 sanitizers job 覆盖 | ~~周~~ 已落地 |
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

## 本轮已清偿(2026-06-10,审计 BLOCKER 收口 + 平台/沙箱 CI 补门)
交付前清障:92-agent 审计遗留的 2 个 OPEN BLOCKER + 1 个中危全部落地,并把两个最大的 CI 盲区补上。全部 build+test 绿(headless 49/49 + WASM 套件)。
- **B2 修复(sandbox_wasm)**:`policy.memoryBytes` 现于 wasm3 后端**强制**——`rt->memoryLimit` 字节级钳制 guest 的每次线性内存分配(初始 + `memory.grow`),声明初始内存超 policy 的模块在 Run 入口确定性 `OutOfMemory` 拒绝(与 RefVm 对等),`policy.callDepth` 经解释器栈预算强制(递归炸弹 → `StackOverflow`)。新增 `tests/sandbox/test_wasm_sandbox.cpp`(6 项对抗:256MiB 内存炸弹 / grow 炸弹 / 递归炸弹 / 零内存 policy / 红线 arena / 良性 guest),guest 为**手工编码字节**,无需 wasm 工具链即可入 CI。`wasm_demo` 12/12 仍绿(policy 提到 4MiB——上限第一次真的生效)。
- **B3 修复(asset)**:三个 `Get*AssetView` 视图结构体携带 `shared_ptr<const void> keepAlive` 共享底层资产所有权——视图存活期间 payload 不悬垂,Release/UnloadPackage 后读取安全(`MeshAssetViewSurvivesAssetUnload` 回归,ASan 下无修复即炸)。
- **F-1 缓解(gameapi)**:O(N) 世界扫描(QueryByTag/SenseRadius/SenseNearest)共享独立紧配额 `maxWorldScansPerTick`(默认 256/tick;校验通过后、扫描前计费;无效调用不耗配额;`BeginTick` 重置)。按工作量计费/空间索引降级为 P2 优化。
- **CI 补门**:`wasm-sanitizers` job(macOS,`BUILD_WITH_WASM=ON`,SandboxTest + WasmSandboxTest 跑 ASan/UBSan——WASM 后端首次进 sanitizer 门)+ `windows-headless` job(windows-latest,MSVC + Ninja,全量 ctest——Windows 构建修复不再裸奔)。
- **仓库卫生**:清除根目录杂物(`08aed65.patch` 冗余补丁导出、`Testing/` ctest 残留入 .gitignore、`web_*_backup/` 移出仓库)。

## 本轮已清偿(2026-06-05,水体工业化收口 + 边界网络化 + B1 修复)
按 30-item 审计清单逐项落地,全部 build+test 绿、ASan/UBSan 清、clang-format 合规、入 CI(headless **49/49** ctest;Jolt 构建绿;boundary 跑 TSan;真 UDP 回环绿)。
- **Phase 0 尾**:**W28** NWTR 线格 v1→v2 版本化 + 迁移(冻结布局 + static_assert 触发器 + `PackCellLegacyV1` 真迁移测试,未知版本 fail-closed);**W29** `WaterForceSystem`×`ActuationSystem` 组合测试(玩家驾船过池保持漂浮、单写者成立、系统序无关、确定性);**W4** 重放分歧诊断台(physics/water/actuation 三通道 FNV 校验和 + 定位最早分歧 tick+子系统,注入式验证);**W16** TSan 预设 + `test_boundary_concurrency` 双线程压测 SpscRing/TripleBuffer。
- **Phase 1**:**W6** 把头条浮力场景跑在 **Jolt 后端**(阿基米德平衡、钳制阻力稳定、水流冲带、**箱体自扶正**——证明产品后端 Jolt 上船只物理正确);**W9** 波热路径 `CompiledWaves`(预提 k/ω,与内联**逐位一致**,船 4 角共用)+ 渲染用 `SampleHeightLOD`(非权威)+ 微基准。
- **Phase 2 玩法**:**W10** 水查询入沙箱 ABI(`CallId::GetWaterState` Sense 域 + POD + `IWaterQuery`,门面/调度/**沙箱 guest** 三层测试,守 Sense 门 + fail-closed);**W11** 电子设备+导电水**短路**(`ElectronicComponent` + `WaterHazardSystem`,latched/边沿触发,含上涨洪水淹没定点设备);**W12** **游泳/溺水/氧气**(`SwimmerComponent` + `SwimSystem`,头没水耗氧→溺水→死亡 latched,涉水正常呼吸,确定性)。
- **Phase 3 网络**:**W13** 修 **B1 BLOCKER**(见上);**W14** 服务器权威时钟(`SnapshotReceiver` 单调跟随 + `RenderClock` 不越权);**W15** 跨进程/UDP transport(`snapshot_codec` 扁平版本化 fail-closed + `DatagramTransport`/`IDatagram` + 真 POSIX `UdpDatagram`,丢包下镜像**收敛**——消费者每帧重发累计 ack 自愈)。
- **Phase 4 UE5**:**W17–W21** 渲染契约(`MockWaterConsumer::EvaluateSurface` 仅凭流出字节重建权威波面 height+法线、亚毫米吻合、覆盖全类型+时间+跨 cell;作者管线 WaterBuilder/`.water`/`assetc` 本就完备)+ `docs/design/ue5-water-contract.md`(数据通路 + UE5 端职责 + 诚实的「仓内已证 vs UE5 仓外」表)。渲染/材质/编辑器 UI 仍属 UE5(ADR-0005),`MockWaterConsumer` 是 headless 参考+完备性证明,非渲染器。
- **诚实残留**:UE5 实际 GPU 出图/材质/编辑器 UI 仍仓外;箱-任意倾斜面体积仍近似;弯河=多 AABB 段;Flood=时间水位非体积解。

## 本轮已清偿(2026-06-04,水体系统 engine/water + engine/water_world)
- **水体系统**([ADR-0015](adr/0015-water-system.md),`next_water` + `next_water_world`):**超越植被的真实每帧力仿真**(用户诉求"把水彻底做到可交付")。纯核心:`det_trig` 确定性 sin/cos(跨构建逐位重放,波是热路径上的权威量)+ Gerstner 波面(buoyancy 用快速垂直和、点查询用定点反演精确高度、解析法线)+ 球缺/箱体**解析淹没体积**(水面/容器底双向裁剪)+ 阿基米德浮力 + **速度钳制阻力**(Jolt `ApplyBuoyancyImpulse` 配方:`k=clamp(rate·dt,0,1)`,显式固定步无条件稳定,无"软木塞从水里弹射")+ 风驱波频谱(深水色散 + 陡度预算)+ **总陡度≤1** 的 fail-closed 校验 + `NWTR` cell。**物理施力扩展**:给 `IPhysicsWorld` 加 `AddForce`/`AddImpulse`(reference 半隐式累加器 + Jolt `BodyInterface`,两后端,trunk 仍 Jolt 无关)。world 集成:`WaterStore`(按 bodyId 跨 cell 去重 + broadphase + **无限海洋全局特例**避免网格爆炸)、`WaterCook` + `assetc water` 子命令、`WaterWorldQuery : IWorldQuery`(水面复合进既有 Sense raycast,零 ABI 改动)+ 潜水隐蔽(stealth)/导电电击(hacking)游戏钩子、`WaterForceSystem`(注册在 PhysicsSystem 前、只施力不写 Transform → 单写者不变量成立)、`WaterStreamingSystem.Sync`(generation 感知)、`MockWaterConsumer` + splash/exit 事件。**端到端纵切面 `WaterSliceTest`**(cook→真实 IO 流送→Sync→World 物理浮起→splash→复合射线→卸载)+ **600 变密度浮体 × 3000 帧全部沉降到 `V_sub/V_tot=ρ_body/ρ`(<2%)且无弹射** + 规模重放状态哈希 + 对抗 fuzz,**57 测试 + ASan/UBSan 全绿、已入 CI**。**诚实残留**:UE5 端 mock(只证字节契约,非渲染一致);无力矩/自扶正与箱-任意倾斜面体积(Scardovelli-Zaleski,推迟到 P2/Jolt);河流为 AABB 走廊(弯曲河=多段);Flood 为时间驱动水位(非体积求解);游泳/溺水/氧气、水+电短路结算、AI 水体分类等下游 gameplay 消费者待接(本系统已备好其权威状态/查询/事件)。

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
