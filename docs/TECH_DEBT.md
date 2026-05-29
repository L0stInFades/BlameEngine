# 技术债与缺口登记 (Tech Debt & Gaps)

**活文档** —— 按 [ADR-0005](adr/0005-ue5-renderer-jolt-headless-world.md) 的战略(渲染=UE5、物理=Jolt、自研核心=headless 权威世界 + 安全玩家代码运行时 + Game API)登记要建/要补的东西。
逐模块成熟度见 [`ARCHITECTURE.md`](ARCHITECTURE.md)。

## 最关键的一句话
先把 **headless 世界**做成可玩纵切面(无渲染器),护城河是 **Game API + WASM 玩家代码沙箱**。渲染/物理/内容工具不再是我们的负担(交给 UE5/Jolt)。

## P0 — 护城河核心(必须自研,要砸好钢)
| 缺口 | 说明 | 粗估 |
|---|---|---|
| **Game API**(能力域化 / 版本化 / 确定性) | 玩家代码 / AI agent / UE5 视图三者唯一契约;整个架构的中心 | 数周–月,持续演进 |
| **WASM 玩家代码沙箱** | 玩家写真实代码 + 安全第一:wasmtime 等,宿主只暴露 Game API,燃料/内存限额、无逃逸;替换当前 `popen(python3)` 探针 | 数周 |
| **Jolt 物理绑定**(权威/确定性/服务器侧) | 接入 headless 世界作为权威物理 | 数周 |
| **sim↔UE5 状态复制层** | headless 权威状态 → UE5 视图镜像(实体 spawn/despawn、transform/状态、事件→VFX);进程内起步、网络/服务器权威设计 | 数周–月 |
| **headless 世界规则/状态接线** | 任务条件/动作接真实世界状态;世界状态可查询/可回放 | 数周 |

## P1 — 重大
| 缺口 | 说明 | 粗估 |
|---|---|---|
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
| Windows/macOS CI 跑 ctest(补平台测试矩阵) | 周 |
| API 文档 + 端到端集成测试 | 周 |
| 跨运行稳定的 ComponentTypeID(序列化用) | 天 |
| UE5 许可/版本治理(Epic 抽成、版本升级流程)写入流程 | — |
| 一次性 style pass(枚举命名等) | 天 |
| 品牌 → 代码符号同步(`Next`/`next_*`/`NEXT_*` → Blame 的脚本化重命名) | 天(脚本化 + 验证) |

## 🗑 已作废(不再是我们的负担,改由 UE5/Jolt)
- 自研渲染器/RHI 接线、GI/AO/阴影/反射/RT 出图、材质/shader 变体系统、frame graph 执行器 → **UE5**。
- 流送↔自研渲染打通、Transform 世界矩阵供渲染、无 Linux 渲染后端 → **UE5**(流送的 sim 侧兴趣管理仍可复用)。
- 内容创作工具(场景/材质编辑、FBX/PNG 导入、资产视口) → **UE5 编辑器**。
- 自研物理 → **Jolt**。

## 本轮已清偿(2026-05)
- 资产压缩端到端、`.npkg` v2 + 内容哈希 ID + 依赖 manifest;ECS 重写为 archetype 数据导向([ADR-0002](adr/0002-archetype-ecs.md));流送每帧预算;工业级质量工具链([ADR-0003](adr/0003-quality-gates.md),并修复了一个关机期 UAF);文档重构为"公司核心资产、多游戏"结构([ADR-0001](adr/0001-engine-is-company-core-asset.md));确立 UE5+Jolt+headless 战略([ADR-0005](adr/0005-ue5-renderer-jolt-headless-world.md));收敛远端分支为单一 `master` 主线([ADR-0004](adr/0004-branch-consolidation.md))。
