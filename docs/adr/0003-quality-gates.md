# 0003. 工业级 C++ 质量闸门

- **Status**: Accepted
- **Date**: 2026-05-29

## Context
作为被所有游戏依赖的核心资产,引擎需要可靠、可强制的质量基线。此前 CI 只构建、从不跑单测,也无 sanitizer / 格式 / 静态分析闸门。

## Decision
建立质量工具链(见 [`../CODE_QUALITY.md`](../CODE_QUALITY.md)):
- **clang-format**(`.clang-format` 贴合现有风格);CI 闸门**按改动行增量**(`scripts/clang_format_diff.sh`),旧代码 grandfather。
- **clang-tidy**(`.clang-tidy` 精选检查 + 命名规则);macOS 需 SDK sysroot(脚本已处理)。
- **Sanitizers**:`asan` preset = ASan+UBSan;`cmake/NextQuality.cmake` 全局注入。
- **CI**(`code-quality.yml`):format(阻塞)+ 在 sanitizer 下**构建并运行单测**(补上 CI 从不跑测试的缺口)+ cppcheck(建议性)。
- **严格告警**:`next_apply_warnings()` 逐目标 `-Wall -Wextra`,`-Werror` 由 `NEXT_WARNINGS_AS_ERRORS` 控制。

## Consequences
- 正面:新 sanitizer CI **立即抓到一个预存的关机期 heap-use-after-free**(异步 IO 线程未 join 就释放缓冲)并已修复;cppcheck 清零;~250 测试 ASan 下通过。
- 负面/约束:本地需装 clang-format/llvm/cppcheck;CI 时间增加;采用是增量的(旧文件的全量格式化留作一次性 pass)。

## Alternatives considered
- 不设闸门 / 仅人工评审:对"被多游戏依赖的核心资产"不够。
