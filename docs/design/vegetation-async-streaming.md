# 设计:植被层接入 async 流送管线 + 内存池(替掉同步侧路)

> 来源:2026-06-03 的 33-agent max-effort 调研(31 个并行调研角度 + 设计综合 + critic)。本文档**先记录问题**(目标架构 + 分阶段方案 + critic 挑出的 15 个 gap),实现按本文档的**修正版**进行——任何一个 gap 都不许上线。
> 相关:[ADR-0014](../adr/0014-vegetation-system.md)(植被系统);ADR-0006(sim↔UE5 边界,wait-free 思路可借鉴)。

## 0. 目标与现状

**目标**:让 `CellLayer::Vegetation`(layered `NCL2`/`.nlc`)的加载走 `engine/world` 的 **async 管线**(`AsyncIOSystem` + `StreamingMemoryPool`),替掉当前的**同步阻塞侧路** `StreamingManager::LoadCellLayer` → `TryReadLayeredCellLayer`(在调用线程上 `ifstream` 整文件读 + `ExtractLayer`)。模板 = 现成可用的 StaticMesh async 加载(`ProcessCellLoad` / `ProcessCellOpCompletions`)。

## 1. 目标架构

### 1.1 NCL2 与 NCEL 的分歧 → 三段链
StaticMesh 是 **2-hop**:`SubmitReadRequest` →(read 回调,主线程)→ `SubmitDecompressRequest` →(decompress 回调,主线程)→ `completions_` → `ProcessCellOpCompletions`(commit)。它成立是因为 `.ncell` 是**单个 NCEL 载荷**(`InspectCellPayload` 解析),`SubmitDecompressRequest` 解一条连续 codec 流。

NCL2 不同:`LayeredCellHeader`(16B)+ `LayeredCellChunkEntry[layerCount]`(各 32B)目录 + 拼接的**每 chunk 独立 codec** 的载荷。`InspectCellPayload` 不认它,`SubmitDecompressRequest` 只能解一个 chunk。所以植被是 **3 段**:读目录 → 读那一个 chunk → 解压那个 chunk。

### 1.2 codec 枚举桥接(强制,禁止 reinterpret)
`LayeredCellChunkEntry.codec` 是 `CellFileCompression`(None=0/Zstd=1/LZ4=2);`SubmitDecompressRequest` 收 `Streaming::CompressionType`(None=0/Zstd=1/LZ4=2/Draco=3/Custom=4)。值巧合相同但是**不同类型** → 写一个**显式 total `switch`**,遇未知值 fail-closed。

### 1.3 线程纪律(承重不变量)
- **IO worker** 只 `ifstream`/`ReadFile` 进调用方拥有的 buffer,push 到 `AsyncIOSystem::completionQueue_`,**绝不碰** `StreamingManager` 状态。
- **Decompress worker** 只 `Compression::Decompress` 一个 chunk 进调用方 buffer。
- **主线程**(调 `StreamingManager::Update` → `asyncIO_->Update()` → `ProcessCompletions` 的线程)跑**所有**回调(NCL2 目录解析在这里)、改 `activeLayerLoadOperations_`、在 `ProcessCellOpCompletions` 里做**每一个** `memoryPool_->Allocate`/memcpy 和 `cell->layers` 写。
- 理由:`WorldPartition` 无锁(`GetCell` 返回裸 `CellData*`),所有 cell 改写必须单线程在主线程,和 StaticMesh 一致。**不引入新 mutex。**

### 1.4 commit 纪律
- 层 commit:`memoryPool_->Allocate` + memcpy + `cell->layers[layer] = {data,size,Loaded}` + `SetLayerPresent`;**不写** `metadata.memorySize`/`dataSize`(植被字节由 `CellData::MemorySize()` 求和层得到);**不调** `UpdateCellState`(cell 级状态归 StaticMesh)。
- **层加载失败 = `failedLoads++` + 留空(absent),绝不 `UpdateCellState(Error)`**(否则整 cell 被驱逐 + `RequestCellLoad` 重排 → 重载风暴)。没有代码读"层级 Error",absence 才是真信号。
- 三种终态(测试钉死):缺文件 + placeholders off ⇒ 无 entry;缺文件 + placeholders on ⇒ `{nullptr,0,Loaded}`;有数据但 `Allocate` 失败 ⇒ 不 placeholder(fail-closed)。

### 1.5 op/completion 泛化(加性,默认 StaticMesh,StaticMesh 路径逐字节不变)
- `CellOpCompletion` 加 `CellLayer layer = StaticMesh` + `bool placeholderRequested`。
- **新** `activeLayerLoadOperations_`(键 `{coord, layer}`,**不复用** coord 键的 `activeLoadOperations_` —— 同 coord 上 StaticMesh 与 Vegetation 必须并发在飞)。
- `ProcessCellOpCompletions` commit 按 `c.layer` 分支:StaticMesh 块原样;层块见 §1.4。

## 2. 分阶段迁移(每阶段编译过、StaticMesh + 现有测试全绿、独立可测)

- **Stage 0**:纯重构。抽 `GetLayeredCellFilePath`(用 `layeredCellExtension` 默认 `.nlc`,不是只懂 `.ncell` 的 `GetCellFilePath`);把 `ParseLayeredCell` 拆成 `ParseLayeredCellDirectory`(只验 header+目录)+ `DecodeLayeredCellChunk`(独立 buffer 解码,**不能**把 chunk-only buffer 喂给 `ExtractLayer`/`ParseLayeredCell`,它们的 `offset>=directoryBytes`/`src=data+offset` 假设整文件 buffer)。零行为变化。
- **Stage 1**:async **整文件**读 + 主线程 extract/alloc。**Option A = 同步返回**(submit→**仅 drain 本 op** 的完成,见 gap #1/#13)。公共语义不变 → **零测试改动**。这是达成"走 async IO"的最小正确增量。
- **Stage 2**:op/completion 泛化带 `CellLayer`,走 `ProcessCellOpCompletions` 单一 commit 点。
- **Stage 3**:**部分 chunk 读**(真 per-layer 流送)——真更复杂、风险更高。
- **Stage 4**:per-chunk async 解压 + 优先级。
- **Stage 2b(Option B,真异步)**:`LoadCellLayer` 改非阻塞(`Update` 里 `ProcessLayerLoadQueue` 排空),重写 ~6 个测试点为 pump 模式。

## 3. 必修清单 —— critic 挑出的 15 个 gap(都要修)

> 标 [C]=正确性 bug,[B]=预算/饿死,[T]=线程安全,[X]=自相矛盾/欠规范。

1. **[C][X] Stage 1 的 Option A block-drain 自相矛盾、可能挂死。** 若它既"内联 commit"又复用 `completions_`/`ProcessCellOpCompletions` 就两套 commit、且 drain 不调 `ProcessCellOpCompletions` 时完成项永不落地。**修:Stage 1 必须 commit 到本地 buffer、完全不碰 `completions_`,用裸 `asyncIO_->Update()` + 本地 done 标志 drain,内联 `Allocate`/store。`ProcessCellOpCompletions` 的参与推迟到 Stage 2。**
2. **[B] `ProcessLoadQueue` 顶部 `GetMemoryUtilization() >= evictionThreshold` 直接 return → Option B 下植被被永久饿死**(0.9 利用率的 cell 永远加载不了植被,植被又不能独立驱逐)。"免费继承预算"是假的。**修:植被层加载走一条专用路径(`ProcessLayerLoadQueue`),只检 cap/每帧预算,不检全局 eviction gate(层加载只给已驻留 cell 加有界字节);或显式文档化"内存压力下植被被抑制"并加测试钉死。**
3. **[B] 每帧预算重置在 `StreamingManager::Update`(:477),Option A 的 block-drain 在一次 `LoadCellLayer` 内、跑在帧循环之外** → `uploadBytesThisFrame` 不重置就累积,可能拿上一帧的陈旧字节触发 `maxUploadBytesPerFrame` defer,把自己的完成永远 defer(drain 空转挂死)。**修:Option A 的 drain 不能走 `maxUploadBytesPerFrame` defer 路(再次印证 #1:Stage 1 本地 commit);若 Stage 2 的 drain 复用 `ProcessCellOpCompletions`,drain 前 `uploadBytesThisFrame=0` 或绕过预算检查。**
4. **[C] `ProcessCellOpCompletions` 把空 `rawCellData` 当硬 Error(:606)**,但**合法的空植被 chunk** 必须 commit `{nullptr,0,Loaded}`。**修:层分支必须在 :606 空检查之前进入,或把该检查改成 `if (c.layer == StaticMesh && c.rawCellData.empty())`。**
5. **[C] unload-during-load 命中 :586 分支会 `ReleaseCellLayers(cell)`(释放所有层)+ `memorySize=0` + `UpdateCellState(Unloaded)` —— 一个植被完成在 StaticMesh 驻留时走到这会把 StaticMesh 一起 nuke + 双重 unload。** **修:层分支的 `Unloading/Unloaded` 情况只能丢弃本层在飞 buffer(commit 前还没 alloc,直接 drop),绝不 `ReleaseCellLayers`。**
6. **[C] `ReingestsOnLayerReload` 测试是 `Unload→Write→Load→Sync` 中间无 `Update()`(:136-141)。** Option A(同步返回)没问题;Option B 必须改成 `Unload; pump-until-not-loaded; Write; Load; pump-until-loaded-with-new-size; Sync`,且 `UnloadCellLayer` 也得同步或被 pump。两个异步 op 在同 coord 上的顺序是真正的 hazard。
7. **[C] `UnloadCellLayer`(:1126)与 async 加载不对称。** 它同步、立即 free 池块。植被加载在飞时调它:必须 (a) `CancelRequest` 任何 `{coord,Vegetation}` 在飞 op 并从 `activeLayerLoadOperations_` 擦除**再** free;(b) 完成回调 re-find,没了就 drop。**`UnloadCellLayer` 与 `ProcessCellUnload` 是两个独立入口,都要 cancel。** 否则:unload 擦了层 → 完成又 commit 进 `cell->layers` → 僵尸 Loaded 层 + 泄漏池块。
8. **[T] "所有 `Allocate`/`Free` 在主线程"的不变量被 Stage 1 的 block-drain 破坏**——若 `LoadCellLayer` 被非主线程调用,回调(及 `Allocate`/`cell->layers` 写)就跑在非主线程,与并发的 `Update` 抢无锁 `WorldPartition`。**修:文档化/断言 `LoadCellLayer` 仅主线程**(同步老路因自包含可多线程调,async 路不行——它共享 `activeLayerLoadOperations_`/`asyncIO_` 与 `Update`)。
9. **[T] `VegetationStreamingSystem::Sync` 无同步地读 `GetLoadedCells()`/`cell->layers`** → 不能与 commit 并发。`LayerData` 的发布是非原子结构体赋值。**修:文档化 Sync 与 `LoadCellLayer`/`Update` 必须同线程;commit 时只在最后一刻物化 `Loaded` entry(镜像 StaticMesh,绝不发布半写 buffer)。**
10. **[C] `(ptr,size)` reload token 在内存池 LIFO 同址复用 + 同大小 reload 时失效** → 服务陈旧植被。今天 sync 窗口≈0 latent;async 放大。**修:给 `LayerData` 加 `uint64 generation`,每次 commit bump,`Sync` 改 key 在 generation —— 放进 Stage 2,别 defer。**(若不做则必须显式声明"同大小 reload 不支持"。)
11. **[X] Option A "零测试改动/同步返回"过强**:`LoadCellLayer` 对**不存在的 cell** 会 kick `LoadCell`(异步多帧,:1039-1041),植被读又需要 cell 先在。现有测试能过是因为先 `Update()` 建了 cell。**修:确认每个植被测试调 `LoadCellLayer` 前都已 `Update()` 建 cell;声明 Option A 是"在 cell 已存在前提下同步返回",StaticMesh kick 仍异步。**
12. **[X] 部分读(Stage 3)的"目录前缀不够就重读"与 async_io 静默 seek-past-EOF 互相作用差。** 固定前缀对多层 cell 会崩;显式 header-then-directory(3 读)会把在飞读数 ×3 撞 `maxConcurrentReads=32`/`maxPendingRequests=256`(与 StaticMesh 共享);中途 `submit==0` 不能部分重试。**修:任一子段 `submit==0` 要**重排整个**层 op(回 Queued);量化读压;承认 Stage 3 比"只是分阶段"更险(印证 Stage 1 才是最小正确增量)。**
13. **[X] Stage 1 的 block-drain 调 `asyncIO_->Update()` 会推进**别的 cell 的 StaticMesh 回调(甚至触发刚 cancel 的 StaticMesh 完成),从一次 `LoadCellLayer` 里意外改 StaticMesh 在飞状态。**修:block-drain 只 drain 自己 request 的完成,其余留在队列;或文档化"`LoadCellLayer` 会顺带推进无关 StaticMesh IO"。`asyncIO_->Update()` 不是 side-effect-free。**
14. **[minor] `CellLayerKey::Hash` 的 `CellCoord::Hash{}(coord) << 4` 丢高 4 位熵**,碰撞比号称的多(不影响正确性,map 处理碰撞)。**修:用 `hash_combine` 而非 `<<4`,或注明这是性能非正确性选择。**
15. **[X] Stage 4 "集中 float→uint32 helper" 与"不动 StaticMesh `:1694` 内联公式"矛盾。** **修:二选一——保留 StaticMesh 内联公式不动,给植被同一公式但独立站点,文档"两处、同公式",放弃"集中"说法。**

**净评估**:结构性改动(`{coord,layer}` 键、completion 带 `CellLayer`、不写 `metadata.memorySize`、fail-as-absence、cancel re-find 守卫)是对的。缺陷集中在 **Option-A block-drain 机制**(#1/#3/#8/#11/#13)和 **三个 commit 边界**(#4/#5 + unload-while-loading)。**#2 和 #10 是最可能静默上线的两个真洞。** Stage 1 确是最小正确增量,但必须先按 #1 改成本地 commit 才真正独立。

## 4. 测试方案

- **确定性 pump**(Option B 驱动):`LoadCellLayer; for(i<200 && !IsCellLayerLoaded){ Update(); sleep(1ms); } ASSERT(IsCellLayerLoaded)`。fail-closed 断言则 pump 到 `PendingOperationCount()==0` 再断言 absence。manager 自带真 worker 线程,sleep 是承重的。
- **现有植被测试**:Option A 不改(同步返回,是合规门);Option B 重写 6 个点为 pump,`ReingestsOnLayerReload` 用不同大小 blob。
- **新 async 测试**:`LoadsVegetationLayerViaAsyncIO`(断言 `GetIOStatistics().HasReadBytes()` + `UnpackCell` 对齐 `ScatterCell`)、压缩 chunk(`IsAvailable` 守卫 + `HasDecompressedBytes()`)、缺 chunk fail-as-absence 且 **cell 仍在 `GetLoadedCells()`**、部分读只读 `entry.compressedSize`、upload 预算 defer 不丢、async 内存计账、unload/Shutdown-mid-load 无 UAF(ASan)。
- **StaticMesh 回归门**:每阶段后重跑 `WorldStreamingTest`(`info.memorySize == payload.size`、磁盘加载、load-start/upload 预算),`asan` preset。

## 5. 关键文件/符号(均已核实)
`StreamingManager::{LoadCellLayer,TryReadLayeredCellLayer,ProcessCellLoad,ProcessCellOpCompletions,UnloadCellLayer,ProcessCellUnload}`、新 `GetLayeredCellFilePath`、`ActiveCellOp`/`CellOpCompletion`/`activeLoadOperations_`(`streaming_manager.{h,cpp}`);`AsyncIOSystem::{SubmitReadRequest,SubmitDecompressRequest,CancelRequest,ProcessCompletions,Update}`、`StreamingMemoryPool::{Allocate,Free}`(`async_io.{h,cpp}`);`ParseLayeredCell`/`ExtractLayer`、新 `ParseLayeredCellDirectory`/`DecodeLayeredCellChunk`、`LayeredCellHeader`/`LayeredCellChunkEntry`/`kLayeredCellMaxChunkBytes`(`layered_cell_file.{h,cpp}`);`CellData::{MemorySize,LayerData,IsLayerLoaded}`、`CellMetadata::SetLayerPresent`(`world_partition.h`);`VegetationStreamingSystem::Sync`(`vegetation_streaming_system.cpp`);codec 桥 `CellFileCompression`↔`CompressionType`。
