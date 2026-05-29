# 开发交接: CP1 → CP2

## CP1 完成状态

### ✅ 已交付功能

所有 CP1 验收指标已完成:

- ✅ **Frame Profiler**: 高精度帧时间测量、FPS 统计、历史数据管理（120帧滑动窗口）
- ✅ **CPU Scope 计时器**: RAII 风格代码块级性能分析，自动输出耗时日志
- ✅ **统计面板**: 每 60 帧自动输出性能统计（帧时间、CPU时间、FPS）
- ✅ **GPU 时间戳框架**: 接口预留，待 CP5 实现 DX12 渲染器后完善
- ✅ **内存/IO 统计接口**: 框架预留，待 CP3 实现 Asset Pipeline 后完善
- ✅ **可观测性调试入口**: 所有性能数据通过日志和统计面板可访问

### 构建环境

| 工具 | 版本 | 路径 |
|------|------|------|
| Visual Studio | 2026 (18.1.1) | `E:\VS` |
| CMake | 4.2.1 | `E:\CMake\bin` |
| 生成器 | "Visual Studio 18" | - |

### 快速验证

```cmd
# 进入项目目录
cd E:\NEXT

# 构建项目
build.bat

# 运行验证
build\bin\Debug\song_demo.exe

# 测试控制: WASD(移动日志), ESC(退出)
# 观察输出: 每约 1 秒输出一次性能统计
```

**预期输出示例**:
```
========== Frame Profiler Stats ==========
Frame Time: 15.94 ms (Avg: 6.75 ms, Min: 0.02 ms, Max: 23.03 ms)
CPU Time: 15.94 ms (Avg: 6.75 ms, Min: 0.02 ms, Max: 23.03 ms)
FPS: 62.75 (Avg: 148.15)
==========================================
```

---

## 下一步: CP2 - Job System

### 目标
实现并行化与预算化，提升引擎多核利用率，为后续 Asset Pipeline 和 World Streaming 提供异步任务基础。

### 参考文档
- `docs/08-development-workflow.md` - CP2 部分的详细说明
- `docs/01-engine-structure.md` - 1.4 Job System 章节
- `docs/CP1_COMPLETION.md` - CP1 实现细节与接口说明

### CP2 交付物

#### 1. 任务队列与调度
- 中央 Job 队列（无锁实现）
- Worker 线程池（可配置线程数）
- 任务优先级系统（至少 3 级：High/Medium/Low）

#### 2. 任务依赖与栅栏
- Job 依赖关系图（DAG）
- 栅栏同步机制（等待前置任务完成）
- 任务取消与超时处理

#### 3. 可观测性集成
- Job 执行时间统计（集成 CPU Scope）
- 队列长度监控（实时显示在统计面板）
- 任务耗时分布分析（平均/最大/最小）

#### 4. 预算化机制
- 每帧 CPU 时间预算（防止 Job 拖垮主线程）
- IO 带宽预算（防止加载任务过载）
- GPU 上传预算（防止渲染卡顿）

### 验收口径

- 能把一个"模拟任务"（比如生成一堆假工作）稳定分摊到多核
- 主线程帧率不受 Job 系统影响（预算机制有效）
- 任务依赖正确执行，无死锁或数据竞争
- 所有性能数据可通过 Profiler 查看

---

## 技术债务与已知问题

### 从 CP1 继承的问题

1. **GPU 统计未实现**: 接口预留，CP5 实现 DX12 渲染器后完善
2. **内存统计为最小实现**: 接口定义完成，无实际数据收集（等 CP3 Asset Pipeline）
3. **CPU Scope 精度有限**: 使用 GetTickCount64（毫秒级），可改用 QueryPerformanceCounter 提升精度
4. **线程不安全**: 当前 Profiler 单线程环境，CP2 需添加锁保护

### 新增技术债务

| 优先级 | 项目 | 计划在 | 说明 |
|--------|------|----------|------|
| 高 | Job System 线程安全 | CP2 | 当前所有系统单线程 |
| 高 | 多线程 Profiler | CP2 | 需要原子操作或互斥锁 |
| 中 | 任务依赖优化 | CP2 后期 | DAG 调度性能 |
| 中 | 纤程/协程支持 | CP3 后 | 用于 Asset 加载中的异步 |
| 低 | Job 内存分配器 | CP3 | 减少任务分配开销 |

---

## 开发规范与约定

### CMake 配置

- 生成器使用: `Visual Studio 18` (对应 VS 2026)
- 架构: x64
- 配置: Debug (开发期)

### 代码风格

- 使用 C++17 标准
- 头文件保护: `#pragma once`
- 命名空间: `Next`
- 日志宏: `NEXT_LOG_*`

### 新增模块约定

```
engine/
  profiler/    # CP1 完成
  jobsystem/   # CP2 新增
    include/next/jobsystem/
      job_system.h
      job.h
      worker_pool.h
    src/
      job_system.cpp
      worker_pool.cpp
    CMakeLists.txt
```

### 依赖关系

```
profiler  ← foundation
jobsystem ← foundation + profiler  # CP2 依赖 CP1
```

---

## 代码审查要点

### 需关注的文件

| 文件 | 说明 | 注意事项 |
|------|------|----------|
| `engine/profiler/include/next/profiler/profiler.h` | Profiler 接口 | 多线程访问需加锁 |
| `engine/profiler/include/next/profiler/cpu_scope.h` | CPU Scope | 线程本地存储可能更安全 |
| `game/song/src/game.cpp` | 主循环 | 集成 Job System 后需要调整帧结构 |

### 新增关注点

1. **无锁队列实现**
   - 使用 `std::atomic` 或第三方实现（如 moodycamel::ConcurrentQueue）
   - 注意 ABA 问题

2. **线程本地存储**
   - Profiler 数据可考虑线程本地存储减少锁竞争
   - 统计汇总时再加锁合并

3. **任务调度公平性**
   - 避免高优先级任务饿死低优先级任务
   - 考虑老化机制

---

## 快速上手

### 1. 环境准备

无需额外安装,当前环境已就绪。

### 2. 启动 CP2

创建新模块:
```
engine/jobsystem/
  include/next/jobsystem/
    job_system.h
    job.h
    worker_pool.h
    job_scheduler.h
  src/
    job_system.cpp
    worker_pool.cpp
    job_scheduler.cpp
  CMakeLists.txt
```

### 3. 修改 CMakeLists.txt

在主 `CMakeLists.txt` 添加:
```cmake
# 在 profiler 之后添加
add_subdirectory(engine/jobsystem)
```

### 4. 集成到 game.cpp

在主循环中集成 Job System:
```cpp
#include "next/jobsystem/job_system.h"

void Game::Run() {
    // 初始化 Job System
    auto& jobSystem = Next::JobSystem::Instance();
    jobSystem.Initialize(std::thread::hardware_concurrency() - 1); // 保留一个主线程
    
    while (!window->ShouldClose() && running_) {
        profiler.BeginFrame();
        
        // 提交并行任务
        JobHandle updateJob = jobSystem.Submit([&]() {
            NEXT_CPU_SCOPE("ParallelUpdate");
            UpdateGame(deltaTime);
        });
        
        // 主线程做其他工作...
        
        // 等待任务完成
        jobSystem.Wait(updateJob);
        
        profiler.EndFrame();
    }
    
    jobSystem.Shutdown();
}
```

### 5. 测试验证

```cpp
// 创建测试任务
void TestJobSystem() {
    NEXT_CPU_SCOPE("TestJobSystem");
    
    std::atomic<int> counter{0};
    const int taskCount = 1000;
    
    for (int i = 0; i < taskCount; ++i) {
        jobSystem.Submit([&counter]() {
            NEXT_CPU_SCOPE("WorkerTask");
            counter.fetch_add(1, std::memory_order_relaxed);
            // 模拟工作
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        });
    }
    
    // 等待所有任务完成
    jobSystem.WaitForAll();
    
    NEXT_LOG_INFO("Completed %d tasks", counter.load());
}
```

---

## CP1 遗留接口说明

### Profiler 使用注意事项

**当前限制（CP1 实现）**:
- 单线程环境，无锁保护
- 使用简单 `std::vector` 存储历史数据
- 高精度计时使用 QueryPerformanceCounter

**CP2 需要做的修改**:
```cpp
// 在 profiler.h 中添加线程安全声明
class Profiler {
public:
    // ... 现有接口 ...
    
    // 添加锁保护（CP2 实现）
    void Lock();      // 用于多线程访问
    void Unlock();
    
private:
    std::mutex frameMutex_;  // 保护 frameHistory_
};
```

### CPU Scope 在多线程环境下的使用

**推荐模式**:
```cpp
// 每个线程维护自己的统计
thread_local std::vector<CpuScopeData> threadScopeData_;

// 主线程定期汇总
void GatherThreadStats() {
    profiler.Lock();
    for (auto& threadData : allThreadData) {
        // 合并统计
    }
    profiler.Unlock();
}
```

---

## 联系方式与问题反馈

如需了解更多 CP1 实现细节,请查看:
- `docs/CP1_COMPLETION.md` - 完整完成报告
- `docs/01-engine-structure.md` - 架构设计
- `engine/profiler/include/next/profiler/*.h` - 接口文档

如需了解 CP2 设计思路,请查看:
- `docs/08-development-workflow.md` - CP2 开发流程
- `docs/01-engine-structure.md` - 1.4 Job System 章节

---

**CP1 开发完成时间**: 2025-12-28
**交接给**: 下一位开发人员
**下一里程碑**: CP2 - Job System
