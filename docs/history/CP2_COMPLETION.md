# CP2 完成报告：Job System

**完成时间**：2025-12-28  
**验收状态**：✅ 通过

## 1. 目标
- 并行化与预算化：把可并行工作移出主线程，提供每帧可控的 CPU 开销。
- 依赖与栅栏：支持任务间依赖，完成后自动入队。
- 可观测性：可查询队列长度、执行耗时、活跃线程数。

## 2. 交付内容

### 2.1 模块结构
```
engine/jobsystem/
  include/next/jobsystem/job.h
  include/next/jobsystem/job_system.h
  src/job_system.cpp
```

### 2.2 核心功能
- **线程池 + 优先级队列**：High/Normal/Low 三优先级，全局任务队列，自动调度。
- **依赖调度**：提交时注册依赖，前置任务完成后自动入队，无需手工等待。
- **等待与栅栏**：`Wait(handle)` 等单个任务，`WaitForAll()` 等待已提交任务全部完成。
- **帧预算处理**：`Pump(budgetMs)` 允许主线程在给定预算内抢占执行任务。
- **取消与超时**：支持 `Cancel(handle)`，支持 `WaitFor(handle, timeoutMs)`。
- **任务命名/统计**：提交时可传 name；统计包含取消数、队列长度、平均/最大耗时。
- **统计接口**：`GetStats()` 返回队列长度、提交/完成数、活跃线程数、平均/最大耗时。
- **自测用例**：Game 启动时提交 32 个任务做自检，验证线程池与依赖链可用。

### 2.3 API 摘要
```cpp
auto& jobs = Next::JobSystem::Instance();
jobs.Initialize(); // 可选指定 worker 数，默认硬件线程-1

Next::JobHandle a = jobs.Submit([] { /* work */ }, Next::JobPriority::High);
Next::JobHandle b = jobs.Submit([] { /* depends */ }, Next::JobPriority::Normal, {a});

jobs.Wait(b);         // 等单个
jobs.WaitForAll();    // 等全部

jobs.Pump(0.25);   // 主线程在 0.25ms 预算内帮忙执行
jobs.Cancel(a);    // 取消任务（如仍在队列，会直接完成并通知依赖）
bool done = jobs.WaitFor(b, 100); // 最长等待 100ms

auto stats = jobs.GetStats(); // 队列长度、耗时、活跃线程数

jobs.Shutdown();
```

## 3. 集成点
- `game/song/src/game.cpp`
  - 初始化时启动 Job System，关闭时清理。
  - 主循环内调用 `Pump(0.25)` 保证帧预算内协助任务执行。
  - `RunJobSystemSelfTest()` 在启动时提交 32 个任务验证线程池可用。

## 4. 快速验证
```cmd
build.bat
build\bin\Debug\song_demo.exe
```
- 观察启动日志：JobSystem 初始化、32 个任务自测完成。
- 游戏窗口正常，WASD/ESC 控制可用。

## 5. 已知限制 / 技术债
- **取消/超时**：接口未暴露取消与超时策略，需按需求补充。
- **无锁队列**：当前使用互斥 + 条件变量，性能可接受但未来可替换为无锁队列。
- **Profiler 线程安全**：CP2 未对 Profiler 做全锁保护，高频 scope 建议谨慎使用。
- **任务命名/分组**：暂未支持标签/命名，后续可扩展用于可视化与分组统计。
- **内存分配**：任务依赖链使用 `std::vector`/`shared_ptr`，未做专用分配器优化。
