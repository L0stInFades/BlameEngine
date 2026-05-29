# Editor Quickstart

`next_editor` 现在的定位是“可运行的内容工作台”，不是完整场景编辑器。当前已经打通：

- ImGui Docking UI
- Content Browser：扫描 `assets/*.npkg`、Load / Unload、查看资产列表与基础元信息
- Import 面板：调用 `next_assetc import`
- Viewport：显示 live backbuffer 输出

## 构建

```powershell
cmake --build build --config Debug --target next_editor
```

## 运行

编辑器已经不再依赖仓库根工作目录。下面两种方式都可以：

```powershell
.\build\bin\Debug\next_editor.exe
```

```powershell
cd build
.\bin\Debug\next_editor.exe
```

常用参数：

```powershell
.\build\bin\Debug\next_editor.exe --smoke-frames 300
.\build\bin\Debug\next_editor.exe --smoke-seconds 3
.\build\bin\Debug\next_editor.exe --no-imgui
.\build\bin\Debug\next_editor.exe --load-package assets\test_package.npkg
```

菜单栏：

- `Window`：重新打开 `Content / Import / Viewport / ImGui Demo`
- `Layout -> Reset Layout`：恢复默认布局

## Content Browser

当前行为：

- 扫描 `assets/*.npkg`
- 显示 `Loaded / Not loaded` 和包引用计数
- `Inspect` 后列出包内资产
- 选中资产后显示类型、大小、header 级元信息
- `Load Asset (Sync)` 会把资产加载到运行时 `AssetManager`

## Import

当前通过外部工具 `next_assetc import` 导入资源，先支持 OBJ mesh-only：

```powershell
.\build\bin\Debug\next_assetc.exe import SourceAssets\tri.obj assets\tri_import.npkg
```

导入成功后：

- Content Browser 会立即重新扫描 `assets`
- 若勾选 `Auto-load after import`，会自动加载刚导入的包
- 失败时保留上一次成功扫描结果，不会清空当前选择

## Viewport

当前 `Viewport` 面板显示的是渲染器 live backbuffer 输出。独立 render-to-texture 场景视口、gizmo、scene hierarchy 和拖拽入场景不在当前阶段交付范围内。
