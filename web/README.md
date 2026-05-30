# BLAME / OPS — 介绍页 / landing page

一款**实时黑客游戏**的前端介绍页:玩家写的是真代码,在真正内嵌的 Neovim 里,
跑在真实运行时上,输出真实驱动世界。面向投资人与玩家,硬核但不自娱自乐。
底层是同一套自研 **Blame Engine**(C++17)。所有技术细节都对照仓库真实源码。

## 核心卖点(全部对照源码)

| 区块 | 来源 |
|---|---|
| §00 赌注 | 真编辑器 / 真执行 / 真后果 / 真引擎 |
| §01 核心循环 | `game/hackops/src/main.cpp`(`RunPolicy` / `RouteStateForScore`)+ `tools/nvim_surface_probe/sample_policy.py` |
| §02 技术 | `engine/terminal/src/nvim_surface.cpp`(手写 msgpack-rpc · `nvim --embed` · `ext_linegrid`)· jobsystem · runtime |
| §03 渲染栈 | `engine/renderer`(`RenderGraph` · `MeshShaderPass` · `global_illumination.cpp` 2,737 LOC · DDGI/VXGI/RTGI shader · `taa.cpp`)+ RHI |
| §04 大世界 | `engine/world`(`WorldPartition` · `InterestManager` · `PredictionSystem` · `StreamingManager` 2,065 LOC · `AsyncIOSystem` · `CellLayer`/`CellLoadState`) |
| §05 架构 | `docs/01-engine-structure.md` |
| §06 体量 | `find engine -name '*.cpp'…` 实测 LOC |
| §07 运行 | `hackops_demo` / `next_nvim_surface_probe` 真实输出 |
| §08 路线图 | `docs/projects/hackops-tech-prep.md`(engine/sandbox · World API · Ops Workspace …) |

## 设计

- **美学**:终端 / ops / 监控的 brutalist 数据手册。单色骨白 on 近黑 + 单一青色信号色。
- **Hero 画布**:网络拓扑图。一次「入侵脉冲」按图距(BFS)从入口节点扩散,边点亮、
  数据包沿边流动 —— 把「玩家代码在系统里传播」可视化。节点分 host(○)与 camera(□)。
- **性能**:画布离屏 / 切后台即暂停 rAF,限到 ~30fps,边批量两条 path,辉光只给波前节点;
  滚动 rAF 节流 + 缓存偏移。支持 `prefers-reduced-motion`。

## 本地预览

```bash
cd web
python3 -m http.server 8723
# 打开 http://localhost:8723
```

纯静态:`index.html` + `styles.css` + `main.js`,无构建步骤、无运行时依赖。
唯一外部资源是 Google Fonts(有系统字体回退)。
