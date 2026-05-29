# 工程手册 — 代码管理 (Engineering Handbook)

Blame Engine 作为公司核心资产、被**所有游戏**复用,其代码管理目标是:**引擎可被多款游戏长期、安全、可预期地依赖**。本手册定义仓库模型、分支、版本、API 稳定性、依赖、所有权,以及**游戏如何消费引擎**。

贡献流程见 [`../CONTRIBUTING.md`](../CONTRIBUTING.md);架构见 [`ARCHITECTURE.md`](ARCHITECTURE.md)。

> **品牌 vs 代码符号**:产品名是 **Blame Engine**(CMake `project(BlameEngine)`、文档、显示串均已改)。**代码内部符号暂仍用 `NEXT`/`Next` token**(`namespace Next`、`next_*` 库目标、`NEXT_*` 宏)——把它们改名为 Blame 是一次纯机械、需脚本化 + 全量构建/测试验证的重构,作为**刻意的后续任务**(见 [`TECH_DEBT.md`](TECH_DEBT.md) P2),不与功能改动混做。

## 1. 仓库模型

- 当前为**单仓**:`engine/*`(核心)+ `tools/*`(工具)+ `game/*`(游戏 demo)同仓。
- **引擎核心才是产品**;`game/*` 是引擎的消费者样例,不得被 `engine/*` 反向依赖。
- 当游戏规模化后,推荐演进为:**引擎仓**(本仓)+ **每游戏独立仓**,游戏通过版本化依赖(release tag / submodule)消费引擎(见 §5)。在那之前,游戏以 `game/<title>/` 形式在仓内,但**只 include 引擎公共头、链接 `next_*` 目标,绝不触碰引擎内部**。

## 2. 分支模型

| 分支 | 含义 |
|---|---|
| `master` | **始终可发布**的引擎基线:CI 全绿,打 release tag 从这里出 |
| `engine/<topic>` `rendering/<topic>` `fix/<topic>` `docs/<topic>` | 短生命周期特性/修复分支,PR 合并后删除 |
| `game/<title>/<topic>` | 游戏侧改动 |
| `release/x.y`(可选) | 需要并行维护旧版本时的发布维护分支 |

规则:短分支、频繁合并、合并后删除;长期分叉是债务。

### 2.1 分支治理(当前待办)
现有远端有 4 条长期分叉分支:`hackops/dev-foundation`(含本轮引擎硬化:archetype ECS、压缩、质量工具链)、`hackops/ops-workspace`、`hackops/policy-sim`、`hackops/python-worker`(后三者含较早的 Ops Runtime/python-worker 探索,但**不含**最新引擎硬化)。

**收敛方案(需一条 ADR 拍板,见 [`adr/`](adr/)):**
1. 选 `hackops/dev-foundation` 为集成线(它是最新、质量最高的基线),合入或重做 `master`。
2. 把 `ops-workspace`/`python-worker` 里仍有价值的 Ops Runtime 工作,**rebase 到集成线之上**(而不是反向),否则会丢掉引擎硬化。
3. 删除已并入/废弃的分支。此后回到 §2 的短分支模型。

## 3. 版本与发布 (SemVer)

引擎版本遵循 **SemVer `MAJOR.MINOR.PATCH`**,源头是 `CMakeLists.txt` 的 `project(BlameEngine VERSION x.y.z)`。

- **MAJOR**:破坏 **Stable** 公共 API。
- **MINOR**:向后兼容地新增能力;可标记 deprecation。
- **PATCH**:向后兼容的修复。
- 发布即打 tag `vX.Y.Z`,维护 `CHANGELOG.md`(按版本列出 Added/Changed/Deprecated/Removed/Fixed)。
- `0.y.z` 阶段(当前)视为不稳定期,但仍按上述沟通破坏性变更。

## 4. 公共 API 稳定性分级

| 级别 | 标识 | 承诺 |
|---|---|---|
| **Stable** | 公共 `include/` 下、无 `detail`/`internal`/`experimental` 命名 | 受 SemVer 保护;移除前必须先在一个 MINOR 版本用 `[[deprecated]]` 标注并写入 CHANGELOG |
| **Experimental** | `experimental/` 目录或显式注释标注;R&D 双轨 | 可随时变更,游戏自担风险 |
| **Internal** | `detail::` 命名空间、仅 `src/` 可见 | 无任何承诺 |

新公共 API 默认进 Stable;不确定的先放 Experimental。破坏 Stable API → MAJOR + ADR + CHANGELOG。

## 5. 游戏如何消费引擎(核心问题)

1. **依赖边界**:游戏只通过**公共头 + `next_*` 链接目标 + 数据(资产/脚本)** 使用引擎,永不 include 引擎 `src/` 或 `detail::`。
2. **版本钉定**:每款游戏钉定一个引擎版本(release tag 或 submodule commit)。升级引擎是**主动决策**:读 CHANGELOG → 升级 → 跑该游戏的测试 → 合并。
3. **游戏特有技术留在游戏侧**:例如首作的"真实代码黑客"玩法(沙箱、Ops Runtime、World API 桥、游戏内终端 UI)属于 `game/<title>/`,**不进引擎**;只有当某能力被证明是**多游戏通用**时,才提炼下沉进 `engine/*`(走 ADR)。
4. **引擎不得知晓具体游戏**:`engine/*` 里不出现任何游戏名/玩法假设。

## 6. 所有权 (CODEOWNERS)

`.github/CODEOWNERS` 把目录映射到负责人/小组;PR 触及对应目录时自动请求其评审。**请用真实 GitHub handle 替换占位**(`@org/...`)。owner 对其目录的 API 稳定性与质量负责。

## 7. 决策记录 (ADR)

架构性/跨模块/破坏 API/新依赖/改变引擎↔游戏边界的决策,写一条 ADR 到 [`adr/`](adr/)(用 `0000-template.md`)。ADR 是不可变的决策历史(取代了散落的 `*_PLAN`/`*_DECISION` 文档)。

## 8. Definition of Done(合并门槛)

一个改动"完成"= 满足全部:
- 在支持平台上 0 error 构建;
- 新逻辑有测试,且**在 ASan/UBSan 下通过**;
- 改动行 clang-format / clang-tidy 干净;
- 触及的公共 API 变更已更新文档 + CHANGELOG(破坏性则有 ADR);
- 未破坏引擎↔游戏边界;
- CI 全绿。

## 9. 依赖策略

- 优先 **FetchContent / vendored 且钉版本**(如 gtest)。
- **避免硬编码系统路径**:当前 `engine/compression/CMakeLists.txt` 对 lz4/zstd 硬编码 `/opt/homebrew`、`/usr/local`,属技术债——应改为 `find_package` + 可选包管理(vcpkg/conan)回退(见 [`TECH_DEBT.md`](TECH_DEBT.md))。
- **UE5 与 Jolt 是两个被集成的重大依赖**([ADR-0005](adr/0005-ue5-renderer-jolt-headless-world.md)):
  - **UE5(渲染)**按 Epic 许可使用,需治理**引擎版本升级流程与许可合规**(>100 万美元营收 5% 抽成);**红线:UE5 内零游戏逻辑/零权威状态**(Blueprint 仅美术表现),与 `engine↔game` 同级。
  - **Jolt(物理)** 宽松许可,vendored 钉版本,作为 headless 世界的权威物理。
- 引入新第三方依赖需 ADR(评估许可、维护、平台覆盖)。

## 10. 支持平台矩阵

| 层 | 平台 | 状态 |
|---|---|---|
| **headless 核心(自研)** | Windows / macOS / Linux | ✅ 构建;macOS 在 CI 跑 ASan/UBSan 测试(Windows/Linux 跑测试待补)。核心**不需要渲染器**即可运行(专用服务器 / CI / AI-agent)。 |
| **渲染(UE5 视图)** | UE5 支持的平台(Win/macOS/主机等) | 由 UE5 承担(见 ADR-0005);不再自研 DX12/Metal/Vulkan。 |
| **物理(Jolt)** | 同 headless 核心 | 权威物理,服务器侧。 |

补齐项(Win/Linux 跑测试、依赖去硬编码)见 [`TECH_DEBT.md`](TECH_DEBT.md)。
