# 贡献指南 (Contributing)

Blame Engine 是公司核心资产,所有游戏都构建其上。改动引擎会影响**所有**下游游戏,因此我们对质量与稳定性有明确要求。先读 [`GETTING_STARTED.md`](GETTING_STARTED.md) 与 [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md);代码管理与版本策略见 [`docs/ENGINEERING_HANDBOOK.md`](docs/ENGINEERING_HANDBOOK.md)。

## 1. 工作流(一句话)

> 从 `master` 切短分支 → 改动 + 测试 → 本地过质量闸门 → 开 PR → 绿 CI + owner 评审 → 合并 → 删分支。

## 2. 分支

- 从 `master`(可发布的引擎基线)切分支,**短生命周期**,合并后删除。
- 命名:`engine/<topic>`(引擎核心)、`rendering/<topic>`、`game/<title>/<topic>`(游戏侧)、`fix/<topic>`、`docs/<topic>`。
- 不要让分支长期分叉(当前 `hackops/*` 多条长期分叉分支正在按 [ENGINEERING_HANDBOOK §分支治理](docs/ENGINEERING_HANDBOOK.md) 收敛)。

## 3. 提交前必须本地通过的质量闸门

| 闸门 | 命令 | 说明 |
|---|---|---|
| 格式 | `scripts/clang_format_diff.sh` | 只检查你改动的行(CI 用同一脚本) |
| 构建 | `cmake --build --preset <preset>` | 至少在你能构建的平台上 0 error |
| 测试 | `ctest --test-dir out/build/asan --output-on-failure` | 新逻辑必须带测试;**在 ASan/UBSan 下通过** |
| 静态分析 | `scripts/clang_tidy.sh <你改的文件>` | bug/性能类告警应清理 |

详见 [`docs/CODE_QUALITY.md`](docs/CODE_QUALITY.md)。CI(`.github/workflows/code-quality.yml`)会强制执行 format 与 sanitizer 测试。

## 4. 代码风格

- 遵循 [`docs/CODE_STYLE.md`](docs/CODE_STYLE.md)(PascalCase 类型/方法、camelCase 局部、`member_` 私有成员后缀、4 空格、attach 花括号、`T* p` 指针左对齐)。
- **格式化你碰过的文件的改动行**(`.clang-format` 已贴合现有风格;采用是按行增量的,旧代码 grandfather)。
- 不要在头文件里 `using namespace`;不用 C 风格强转;不用裸指针表达所有权;常量用 `constexpr` 不用 `#define`。

## 5. 提交信息

- 祈使句 + 模块 scope 前缀,匹配现有历史:`feat(engine): …`、`fix(renderer): …`、`refactor(runtime): …`、`docs: …`、`test(world): …`、`chore(ci): …`。
- 正文说清**为什么**,不只是**改了什么**。
- 引擎公共 API 的破坏性改动必须在提交信息与 PR 里显式标注 `BREAKING:`,并更新 CHANGELOG + 走 ADR(见 §7)。

## 6. Pull Request

- **小而聚焦**;一个 PR 一个主题。
- 填写 PR 模板(`.github/PULL_REQUEST_TEMPLATE.md`):动机、改动、测试证据、平台、是否破坏 API、是否需要 ADR。
- 至少 1 个评审;触及 `CODEOWNERS` 指定目录时自动请求对应 owner。
- 合并前 CI 必须全绿。

## 7. 何时需要 ADR(架构决策记录)

任何**架构性 / 跨模块 / 破坏公共 API / 引入第三方依赖 / 改变引擎↔游戏边界**的决策,先在 [`docs/adr/`](docs/adr/) 写一条 ADR(用 `0000-template.md`),PR 里链接它。小改动不需要。

## 8. 红线

- **`engine/*` 永不依赖 `game/*`**(见 ARCHITECTURE §1)。游戏特有逻辑放 `game/*`。
- 不破坏标记为 **Stable** 的公共 API(必须先 deprecate 一个 MINOR 版本,见 ENGINEERING_HANDBOOK §API 稳定性)。
- 不提交无法构建/未测试的代码;不把二进制缓存当唯一真相(资产应可从源重新 cook)。
- 不向 `docs/history/` 添加新文件(那是只读归档)。
