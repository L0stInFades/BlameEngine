# Docs Index

这份索引只描述当前源码真实状态，不复述历史阶段宣传口径。

## 当前可运行目标

- `song_demo`
- `next_editor`
- `ctest -C Debug --output-on-failure`

## 当前最小可用能力

- Asset Pipeline：`next_assetc`、`.npkg` 扫描、运行时加载、编辑器导入与加载
- Runtime：Entity / Component / Query / System 基础闭环
- World：世界分区与流送主路径、placeholder cell 可控兜底
- Task：任务定义、实例推进、save/load、事件驱动 auto-accept
- Script：stub mode 始终可用；检测到 system Lua 时启用真实 VM 生命周期调用
- Editor：Content / Import / live backbuffer Viewport 面板

## 当前明确未完成

- 高阶 GI / RT / VXGI 等渲染模块的生产可用性
- 编辑器独立 render-to-texture 场景视口
- 复杂脚本绑定、热重载依赖图、完整脚本沙箱
- 完整场景编辑工作流（gizmo、scene hierarchy、拖拽入场景）

## 推荐阅读

1. `docs/EDITOR_QUICKSTART.md`
2. `docs/ASSET_PIPELINE_QUICKSTART.md`
3. `docs/01-engine-structure.md`
4. `docs/03-world-streaming-time.md`
5. `docs/05-scripting-quests.md`
6. `docs/07-milestones.md`
7. `docs/08-development-workflow.md`
8. `docs/O3DE_METAL_ADAPTATION.md`
9. `docs/RHI_METAL_SPLIT.md`

## Smoke 命令

```powershell
.\build\bin\Debug\song_demo.exe --smoke-frames 5
.\build\bin\Debug\song_demo.exe --run-self-tests --smoke-frames 1
.\build\bin\Debug\next_editor.exe --smoke-frames 5 --no-imgui
.\build\bin\Debug\next_editor.exe --load-package assets\test_package.npkg --smoke-frames 5 --no-imgui
ctest --test-dir build -C Debug --output-on-failure
```
