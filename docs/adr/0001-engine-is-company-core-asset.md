# 0001. 引擎是公司核心资产(多游戏通用底座)

- **Status**: Accepted
- **Date**: 2026-05-29

## Context
早期文档把引擎绑定到单一游戏(两宋历史开放世界)。产品已切换为"真实代码黑客"的开放世界游戏(看门狗式)。但更重要的事实是:这套自研引擎是**公司的核心技术资产,所有游戏都将构建其上**。若继续把引擎和某一款游戏的题材/玩法耦合,会让引擎无法复用、文档身份分裂。

## Decision
- 引擎(`engine/*`、`tools/*`)是**与游戏无关**的核心资产。`engine/*` **永不依赖 `game/*`**。
- 每款游戏是引擎的消费者,通过**公共 API + 版本化依赖 + 数据(资产/脚本)** 使用引擎(见 [ENGINEERING_HANDBOOK §5](../ENGINEERING_HANDBOOK.md))。
- **游戏特有技术留在游戏侧**:首作的"真实代码黑客"玩法(HackOps:沙箱、Ops Runtime、World API、游戏内终端 UI)属于 `game/<title>/`,不进引擎。仅当某能力被证明多游戏通用时,才走 ADR 下沉进引擎。
- "开放世界/影视级运镜/分层画质"等是**引擎能力目标**(游戏无关),不是某款游戏的目标。

## Consequences
- 正面:引擎可复用、文档身份清晰、边界可治理。
- 负面/约束:需要纪律——不能把游戏逻辑抄近路塞进引擎;游戏要主动管理引擎版本升级。
- 文档据此重构:`docs/00-vision-principles.md`、`docs/README.md`、`ARCHITECTURE.md`、`ENGINEERING_HANDBOOK.md`。

## Alternatives considered
- 继续单游戏耦合:被否,违背"核心资产、多游戏复用"的事实。
