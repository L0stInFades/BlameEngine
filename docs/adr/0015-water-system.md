# ADR-0015:水体系统(确定性 Gerstner 表面 + 阿基米德浮力,核心拥有仿真,UE5 只渲染)

状态:**已采纳(usable / 端到端打通)**,2026-06-04。
相关:[ADR-0014](0014-vegetation-system.md)(植被,镜像模板)、[ADR-0009](0009-physics-jolt-backend.md)(物理)、[ADR-0006](0006-sim-ue5-boundary.md)(sim↔UE5 边界)、[ADR-0007](0007-game-api.md)/[ADR-0010](0010-actuation-single-transform-writer.md)(Game API / 单一 Transform 写者)。

## 背景

引擎需要一个**可交付**的水体能力——不是"能编译跑通单测",而是能真正用于开发游戏所需的水的一切:可浮起/冲走/淹没的水、可查询的水面、可流送的大规模水体。植被(ADR-0014)确立了模板,但**水必须超越它**:植被是静态放置 + 只读查询;水是**每帧真实力仿真**。

**红线**(同植被):**核心拥有水的权威状态——水面几何、浮力、流场、淹没;UE5 只渲染同一套参数算出的同一个水面**。水的 shader/泡沫/焦散/反射是 UE5 的事,不在本系统范围。

## 决策

1. **纯核心 `engine/water`(库 `next_water`,仅依赖 foundation)**——确定性、headless、可单测:
   - `water_def.h`:权威数据模型。`WaterType{Ocean,Pool,River,Flood,Lake}`、`WaterFlags{Buoyant,Conductive,Lethal,Current,BreaksSight,Extinguishes}`、`WaveComponent`(Gerstner 波)、固定大小 POD wire 记录 `WaterBodyInstance`(264 字节,`static_assert` 锁布局 + trivially_copyable)、作者态 `WaterBodyDef`/`WaterSceneDef`。
   - `det_trig.h`:**引擎自有的确定性 sin/cos**(范围归约 + 折叠 + 9 阶泰勒,误差 ~1e-6,只用 +−×÷ 与精确截断)。每帧波浪求值用它,保证**跨构建/跨平台逐位一致的重放**(比引擎其余部分"仅同构建确定"更严,因为波是热路径上的权威量)。
   - `water_surface.h/.cpp`:解析 Gerstner 表面。`SampleHeightFast`(无水平反演的垂直和,buoyancy 每帧用)+ `SurfaceHeightAt`(定点迭代反演水平挤压,精确点查询用)+ `SurfaceSampleAt`(高度 + 解析法线)+ Flood 随时间抬升并封顶。
   - `water_volume.h/.cpp`:**解析淹没体积**——球用球缺公式 `V=πh²(3r−h)/3`,箱用水面/容器底双向裁剪的板块法,皆精确,只用 +−×÷√。(箱-任意倾斜面的 Scardovelli-Zaleski 闭式解推迟到 P2/Jolt 力矩自扶正时再加——线性 COM 浮力下水平面近似已足。)
   - `buoyancy.h/.cpp`:**纯物理无关**的力模型。浮力 = `ρ·g·V_submerged`(向上,经 AddForce);阻力 + 水流平流 = 对**相对流速**的阻尼,以**速度钳制冲量** `dv=−k·u, k=clamp(rate·dt,0,1)` 施加——`k≤1` 使冲量至多抵消相对速度、绝不反向,这是 Jolt `ApplyBuoyancyImpulse` 的配方,让显式阻力在固定 dt 下**无条件稳定**(无"软木塞从水里弹射"爆炸)。
   - `water_validator.h/.cpp`:总、fail-closed 校验(累计全部错误,字节稳定序),含**总陡度≤1**(否则水面自相交)等真实正确性检查。
   - `water_builder.h/.cpp`:流式作者 API + `AddOceanWaves`(接频谱)。
   - `water_spectrum.h/.cpp`:确定性风驱海浪合成(深水色散 `c=√(g/k)` + 方向扩散 + 陡度预算分配)。
   - `water_cell.h/.cpp`:`NWTR` cell 打包/解包(magic+version+size,fail-closed,分配前 `kMaxWaterBodiesPerCell` 上限防 DoS)。

2. **物理施力扩展(`engine/physics`,两后端)**:给 `IPhysicsWorld` 加 `AddForce`/`AddImpulse`(语义:per-Step 累加施力,Step 中 `a=g+ΣF/m` 半隐式积分后清零;冲量瞬时质量归一)。`ReferencePhysicsWorld` 加 `force[3]` 累加器;`JoltPhysicsWorld` 经 `BodyInterface::AddForce/AddImpulse`。**无需新 getter**——`WaterForceSystem` 从 ECS `RigidBodyComponent.desc` 读质量/形状,从 `GetTransform/GetLinearVelocity` 读 LIVE 位姿/速度(绝不用 `desc.position`,那是创建态)。

3. **world 集成 `engine/water_world`(库 `next_water_world`)**:
   - `water_store.h/.cpp`:权威运行时索引。按 `bodyId` **跨 cell 去重**(refcount)、reload 替换;AABB broadphase 网格 + **全局海洋特例**(超大/Ocean 体不进网格,避免无限海洋撑爆网格);`BodyAt`(topmost)/`SampleWaterAt`/`IsSubmerged`/`BodiesOverlappingAabb`。
   - `water_cook.h/.cpp`:`CookWaterCell`(校验→选出与 cell 重叠的体→赋 1-based 稳定 bodyId→`PackCell`→`PackLayeredCell`)+ `ParseWaterDefText` + `WriteWaterCellFile`。确定性(同输入逐字节一致)、fail-closed。
   - `water_query.h/.cpp`:`WaterWorldQuery:gameapi::IWorldQuery`(水面作为射线目标,**复合进既有 Sense raycast**,与 fallback 取近——零 ABI 改动)+ 游戏钩子查询 `WaterHeightAt`/`SubmersionDepthAt`/`IsSubmergedAt`/`IsHiddenBySubmersion`(潜水隐蔽=stealth)/`IsInConductiveWater`(电击=hacking)。
   - `water_force_system.h/.cpp`:`WaterForceSystem:System`,每固定帧对每个浸入水的 Dynamic 刚体调 `ComputeWaterForce`→`AddForce`+`AddImpulse`;入水/出水边沿发 splash/exit `GameEvent`。**只施力、不写 Transform**——单一 Transform 写者(ActuationSystem)不变;**注册在 PhysicsSystem 之前**,力在该 Step 叠加。
   - `water_view.h/.cpp`:`MockWaterConsumer`(UE5 替身,证明流送字节携带完整水面参数)+ splash/exit `ToBoundaryEvent`。
   - `water_streaming_system.h/.cpp`:`Sync(StreamingManager)`,generation 感知地 ingest/evict(镜像植被)。

4. **管线接入**:`world_partition.h` 加 `CellLayer::Water=10`(`Max=11`,补 layerPriority);`assetc water <scene.txt> <cellX> <cellZ> <cellSize> <out.ncell> [none|lz4|zstd]` 子命令 + 文本场景解析。

## ActuationSystem ↔ WaterForceSystem 组合契约

二者都作用于 Dynamic 体但互不破坏:ActuationSystem 设速度(移动意图),WaterForceSystem 加力(浮力/阻力/水流)。注册序 **Actuation → WaterForce → Physics**,力在 Step 中叠加到已设速度上——对"游泳的智能体"是物理合理的(意图速度 + 浮力托起 + 阻力)。两者都不写 Transform,单写者不变量成立。

## 后果 / 如何超越植被

植被是确定性散布 + 只读覆盖查询(LOS/破坏)。水在五个维度上具体超越:
1. **动力学 vs 静态**:水跑真实每帧力仿真(阿基米德 + 钳制阻力 + 水流平流),物体会浮/沉/漂/被冲走——这是诉求要的"真正的模拟",植被没有。
2. **连续解析场 vs 离散实例**:水回答任意 `(x,z,t)` 的高度/法线/流速/淹没,植被只答"附近有没有"。
3. **跨子系统集成**:水扩展了物理接口(两后端)、复合进 Game API、接物理系统——植被只触 streaming/boundary。
4. **更严的确定性**:`det_trig` 让权威热路径跨构建逐位一致;有规模重放状态哈希测试。
5. **更大规模 + 稳定性证明**:600 变密度浮体 × 3000 帧全部沉降到 `V_sub/V_tot=ρ_body/ρ` 且无弹射(钳制阻力的规模化证明),植被规模测试无力动力学。

## 测试(ASan/UBSan 全绿,在 CI)

`test_water`(核心 22:det_trig 精度、单波闭式对照、球缺/箱体淹没体积、浮力平衡、阻力钳制、水流平流、fail-closed 校验、NWTR 往返、频谱) + `test_water_cook`(6) + `test_water_store`(7) + `test_water_view`(3) + `test_water_buoyancy`(6:**头号**,沉降平衡/无弹射/水流冲走/确定性/splash) + `test_water_query`(4) + `test_water_streaming`(真实 IO) + `test_water_slice`(**端到端纵切面**:cook→流送→Sync→World 物理仿真浮起→splash 事件→WaterWorldQuery 复合→卸载) + `test_water_scale`(3:broadphase 有界且精确、规模沉降无弹射、规模确定性哈希) + `test_water_fuzz`(4:对抗字节/文本全 fail-closed)。物理施力扩展另加 6 个用例(reference + Jolt 各跑)。

## 诚实残留

- **UE5 端为 mock**(仓内无 UE 工程):`MockWaterConsumer` 现不仅证字节契约,还**仅凭流出字节重建权威波面**(`EvaluateSurface` height+法线,`test_water_render` 证亚毫米吻合、覆盖全类型+时间+跨 cell——**渲染契约完备性证明**,W17–W21);但仍不证 UE5 的实际 GPU 出图一致(需 UE5 用同一波形公式,规格见 [docs/design/ue5-water-contract.md](../design/ue5-water-contract.md))。
- ~~**无力矩/自扶正**~~ **已落地(2026-06-05,W5)**:`IPhysicsWorld` 加 `AddTorque`/`AddForceAtPosition` + reference 后端对角惯量/四元数角积分 + 多点浮筒浮力 → 倾斜船体自扶正。**仍近似**:reference 用对角(body≈world)惯量,不旋转惯量张量;箱-任意倾斜面**体积**仍用各角列近似(精确 Scardovelli-Zaleski 与精确世界惯量留待 Jolt/P2)。
- **河流为 AABB 走廊**(弯曲河 = 多段 AABB);凸多边形/样条足迹推迟到 P1。
- **Flood 为时间驱动的水面抬升**(非容器体积求解);需要"按注入体积解水位"时再加固定迭代的体积求解。
- ~~**游泳/溺水/氧气、水+电短路结算**~~ **已落地(2026-06-05,W11/W12)**:`WaterHazardSystem`(`ElectronicComponent`,导电水中淹没的电子设备 latched 短路 + `WSHT` 事件)+ `SwimSystem`(`SwimmerComponent`,头没水耗氧→溺水→死亡 latched + `WDRN`,涉水正常,确定性)。**AI 水体分类**仍是下游消费者(权威查询/事件已备好,玩法逻辑在 sandbox/level 层接)。
- **跨平台逐位**:`det_trig` 让波面跨平台一致;但 reference 与 Jolt 后端之间不逐位一致(求解器不同)——reference 是重放权威,与 ADR-0009 立场一致。

## 2026-06-05 工业化加固(研判后逐项修复:Phase 0 + 船只物理)

来源:10-agent 工业交付研判(裁决 `CoreStrongProductNot`,~30%「强核心、近零产品」)的修复清单。本轮逐项落地,全 build+test 绿、ASan/UBSan 清、已入 CI:
- **W1**:`IsLargeBody`/`BucketCoord` 的 float→int 溢出 UB(double 计算 + 饱和)+ 校验器 `BoundsTooLarge` 坐标量级上限(`kMaxWaterCoord`,开放世界友好、拒绝垃圾/服务器崩溃向量)。
- **W26**:`WriteWaterCellFileMerged` 读-改-写合并 layered cell——给已有植被的 cell cook 水**不再覆盖植被层**;`assetc water` 改用之。
- **W2**:统一到单一 `gameapi::SimClock`——删 `WaterForceSystem::seconds_` 私有累加与 `WaterWorldQuery::SetTime`,力系统/查询/(未来)渲染共享一个权威时间。
- **W27**:`WrapPhase`——波相位在 double 内算并 mod 2π 归约后转 float,根除长会话(大时间)相位精度崩坏。
- **W3**:全局确定性 FP 标志(`-ffp-contract=off -fno-fast-math` / `/fp:precise`)+ ubuntu 确定性 CI job(与 macOS sanitizers 成跨平台矩阵)。
- **W5(船只物理水基础)**:`IPhysicsWorld` 新增 `AddTorque`/`AddForceAtPosition`/`GetAngularVelocity`(**两后端**:reference 对角惯量 + 四元数角积分;Jolt 经 `BodyInterface`)+ 纯核心 `ComputeBoxBuoyancy` 4 底角浮筒浮力 → 净浮力 + 扶正力矩 + 逐角垂直阻力。倾斜筏子自动扶正、平浮(`BoxSelfRightsFromTilt`)。

**~~仍待~~ 已全部落地(2026-06-05;headless 49/49 + 全 ASan/UBSan 清 + Jolt 绿 + boundary 跑 TSan + 真 UDP 回环绿)**:W28 wire 版本化+迁移、W29 Actuation×WaterForce 组合测试、W4 重放分歧诊断台、W16 TSan、W6 buoyancy 跑 Jolt、W7 热路径去分配、**W9** 波热路径 `CompiledWaves`(与内联逐位一致)+ 渲染 LOD;**Phase 2** 玩法 ABI(W10 `GetWaterState` 入沙箱、W11 电子设备水短路、W12 游泳/溺水/氧气);**Phase 3** 网络(W13 修边界 B1、W14 服务器权威时钟、W15 跨进程/UDP transport,见 [ADR-0006](0006-sim-ue5-boundary.md));**Phase 4** W17–W21 渲染契约证明 + 作者管线(见 [docs/design/ue5-water-contract.md](../design/ue5-water-contract.md))。**剩**:UE5 仓外的实际 GPU 出图/材质/编辑器 UI(架构使然,ADR-0005)。
