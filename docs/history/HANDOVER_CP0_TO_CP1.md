# 开发交接: CP0 → CP1

## CP0 完成状态

### ✅ 已交付功能

所有 CP0 验收指标已完成:

- ✅ **清晰目录分层**: Platform/Foundation/Runtime/Renderer/Tools/Game 完整实现
- ✅ **一键构建**: `build.bat` 自动配置并构建项目
- ✅ **窗口系统**: Win32 窗口创建、调整大小、关闭检测
- ✅ **输入系统**: 键盘(WASD/ESC)、鼠标状态检测
- ✅ **日志系统**: 多级日志(Trace/Debug/Info/Warn/Error/Fatal),带时间戳
- ✅ **崩溃捕获**: 异常过滤器、Mini dump 生成、对话框提示
- ✅ **30分钟编译运行**: 使用 VS 2026 + CMake 4.2.1 成功构建
- ✅ **5分钟稳定运行**: 实际测试通过,无崩溃

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
```

---

## 下一步: CP1 - 可观测性优先

### 目标

实现基础的性能监控与调试工具,为后续开发提供可观测性。

### 参考文档

- `docs/08-development-workflow.md` - CP1 部分的详细说明
- `docs/01-engine-structure.md` - 1.7 可观测性章节

### CP1 交付物

#### 1. CPU 采样/计时器
- Frame Profiler 基础框架
- 系统/函数级计时 scope
- 帧时间统计

#### 2. GPU 时间戳框架
- GPU 查询封装
- 基础时间戳接口
- (GPU 具体实现在 CP5 完善)

#### 3. 内存/VRAM/IO 统计接口
- 内存分配统计框架
- 接口定义(可扩展)
- IO 统计接口

#### 4. Frame Profiler 面板
- 控制台输出或简单可视化
- 帧时间、CPU 耗时分布
- 至少能回答三个问题:
  - 这帧 CPU 花在哪?
  - GPU 花在哪?
  - 内存/IO 有没有异常尖峰?

### 验收口径

- 能输出每帧的 CPU 耗时分布
- 能统计并显示帧时间平均值/峰值
- 内存统计接口可用(哪怕是最小实现)
- 新系统必须有调试入口(日志/面板)

---

## 技术债务与已知问题

### 已知问题

1. **输入重复日志**: 当前 WASD 按下时每帧都输出日志,CP6 实现相机系统后会改为实际移动
2. **Renderer 为占位实现**: CP5 会实现 DX12 渲染器
3. **日志级别未过滤**: 所有日志都输出到 stderr,生产环境需添加级别过滤

### 技术债务

| 优先级 | 项目 | 计划在 | 说明 |
|--------|------|----------|------|
| 高 | Renderer 实现 | CP5 | 当前为 DummyRenderer |
| 高 | 相机系统 | CP6 | 当前只输出日志 |
| 中 | Job System | CP2 | 当前为单线程 |
| 中 | 资源加载 | CP3 | 当前无资源系统 |
| 低 | 文件日志 | 后续 | 当前仅控制台输出 |

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

### 目录约定

```
engine/
  platform/      # 平台抽象层
  foundation/    # 基础设施
  runtime/       # 运行时核心(ECS/事件)
  renderer/      # 渲染系统
```

### 构建产物

```
build/
  bin/Debug/     # 可执行文件
  lib/Debug/     # 静态库
```

---

## 代码审查要点

### 需关注的文件

| 文件 | 说明 | 注意事项 |
|------|------|----------|
| `engine/platform/include/next/platform/platform.h` | 平台接口 | 崩溃处理依赖 `dbghelp.lib` |
| `engine/foundation/src/logger.cpp` | 日志实现 | 时间戳使用 `<ctime>` |
| `engine/runtime/include/next/runtime/event_bus.h` | 事件总线 | 单例模式,线程安全未实现 |
| `game/song/src/game.cpp` | 主循环 | 固定帧率上限(120fps) |

### 依赖关系

```
platform  ← foundation
runtime   ← foundation
renderer  ← runtime
game       ← runtime + renderer + platform
```

---

## 快速上手

### 1. 环境准备

无需额外安装,当前环境已就绪。

### 2. 启动 CP1

创建新文件:
```
engine/profiler/
  include/next/profiler/
    profiler.h
    cpu_scope.h
    stats.h
  src/
    profiler.cpp
    cpu_scope.cpp
```

### 3. 修改 CMakeLists.txt

在主 `CMakeLists.txt` 添加:
```cmake
add_subdirectory(engine/profiler)
```

### 4. 集成到 game.cpp

在主循环中添加:
```cpp
profiler.BeginFrame();
// ... 游戏逻辑 ...
profiler.EndFrame();
profiler.LogStats(); // 或显示面板
```

---

## 联系方式与问题反馈

如需了解更多 CP0 实现细节,请查看:
- `docs/CP0_COMPLETION.md` - 完整完成报告
- `docs/00-vision-principles.md` - 项目愿景
- `docs/01-engine-structure.md` - 架构设计

---

**CP0 开发完成时间**: 2025-12-28
**交接给**: 下一位开发人员
**下一里程碑**: CP1 - 可观测性优先
