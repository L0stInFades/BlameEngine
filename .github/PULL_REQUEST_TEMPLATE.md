<!-- Blame Engine PR. See CONTRIBUTING.md. Keep PRs small and focused. -->

## 动机 / Why
<!-- 为什么需要这个改动 -->

## 改动 / What
<!-- 关键改动点 -->

## 测试 / Testing
- [ ] 新逻辑有单测
- [ ] 在 ASan/UBSan 下通过:`ctest --test-dir out/build/asan --output-on-failure`
- 已构建平台: - [ ] macOS  - [ ] Linux  - [ ] Windows(headless 核心)

## 质量闸门 / Quality
- [ ] `scripts/clang_format_diff.sh` 干净(改动行已格式化)
- [ ] `scripts/clang_tidy.sh <改动文件>` 无 bug/性能类告警

## API / 边界 / Stability
- [ ] 未破坏 **Stable** 公共 API(若破坏:标题加 `BREAKING:` + 附 ADR + 更新 CHANGELOG)
- [ ] 未让 `engine/*` 依赖 `game/*`
- [ ] 架构性/跨模块/新依赖决策已附 ADR 链接(否则留空):

## 其他 / Notes
<!-- 截图 / 关联 issue / 风险 / 对下游游戏的影响 -->
