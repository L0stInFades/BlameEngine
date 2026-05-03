# NEXT Engine

NEXT 是一个面向“两宋项目”的自研引擎仓库。当前状态不是空壳，也还不是完整生产级引擎；更准确地说，它已经具备可构建、可测试、可启动 demo 和编辑器的工作底座。

## 当前可用能力

- 运行时：基础 ECS、`Query::ForEach`、资源加载、任务系统存档/事件驱动、世界流送主路径
- 工具：`next_assetc`、`next_editor` 的 Content / Import / live backbuffer Viewport
- 程序：`song_demo`、`next_editor`、`next_task`
- 测试：`ctest -C Debug --output-on-failure`

## 当前仍是原型或部分实现

- 高阶渲染特性（GI / RT / VXGI 等）仍有 placeholder
- 编辑器还没有独立 render-to-texture 场景视口
- 脚本系统支持 `stub` 和可选 system Lua 两态，但没有复杂绑定、热重载依赖图或完整沙箱

## 快速开始

### HackOps / 终端技术线

跨平台开发真实 Neovim、Ops Runtime、玩家代码执行等 HackOps 技术时，使用
terminal preset，不需要构建当前 Windows/DX12 渲染路径：

```bash
cmake --preset terminal-dev
cmake --build --preset terminal-dev
out/build/terminal-dev/bin/next_nvim_surface_probe \
  --clean \
  --file tools/nvim_surface_probe/sample_policy.py \
  --snapshot /tmp/nvim-surface-cpp.txt
out/build/terminal-dev/bin/hackops_demo \
  --reset \
  --workspace /tmp/next-hackops-maintenance-window \
  --snapshot smoke \
  --run-policy \
  --run-sim \
  --list
```

详细说明见 `docs/HACKOPS_DEV_QUICKSTART.md`。

### 配置与构建

```powershell
cmake --preset windows-dx12-dev
cmake --build --preset windows-dx12-dev
```

如需启用真实 Lua VM，并且本机已经安装 system Lua：

```powershell
cmake --preset windows-dx12-dev -DUSE_SYSTEM_LUA=ON
cmake --build --preset windows-dx12-dev
```

如果没找到 Lua，脚本系统会自动回退到 stub mode，不会阻断构建。

### 运行

```powershell
.\out\build\windows-dx12-dev\bin\Debug\song_demo.exe
.\out\build\windows-dx12-dev\bin\Debug\next_editor.exe
```

推荐的 smoke 命令：

```powershell
.\out\build\windows-dx12-dev\bin\Debug\song_demo.exe --smoke-frames 5
.\out\build\windows-dx12-dev\bin\Debug\song_demo.exe --run-self-tests --smoke-frames 1
.\out\build\windows-dx12-dev\bin\Debug\next_editor.exe --smoke-frames 5 --no-imgui
.\out\build\windows-dx12-dev\bin\Debug\next_editor.exe --load-package assets\test_package.npkg --smoke-frames 5 --no-imgui
ctest --test-dir out\build\windows-dx12-dev -C Debug --output-on-failure
```

## 入口文档

- `docs/README.md`：当前仓库状态与文档索引
- `docs/EDITOR_QUICKSTART.md`：编辑器用法与当前限制
- `docs/ASSET_PIPELINE_QUICKSTART.md`：资产导入与打包流程
