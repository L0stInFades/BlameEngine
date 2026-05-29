# 上手指南 (Getting Started)

面向第一次接触 **Blame Engine** 的工程师。目标:30 分钟内 clone → 构建 → 跑通测试 → 运行一个 demo。
引擎定位与文档全景见 [`docs/README.md`](docs/README.md);架构见 [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)。

## 1. 前置依赖

| 平台 | 编译器 | 备注 |
|---|---|---|
| 通用 | CMake ≥ 3.20 | `cmake --version` |
| Windows | Visual Studio 2022 (x64) | DX12U 路径需 Windows SDK ≥ 10.0.20348.0 |
| macOS | Xcode / Command Line Tools | 提供 AppleClang + Metal |
| Linux | gcc/clang (C++17) | 目前**无渲染后端**(只跑终端线/测试,见 §5) |

**可选依赖**(缺失会自动降级,不阻断构建):
```bash
# macOS
brew install lz4 zstd        # 压缩(缺失则资产不压缩)
brew install neovim          # 终端/HackOps 技术线需要
brew install llvm cppcheck clang-format   # 代码质量工具(见 docs/CODE_QUALITY.md)
# system Lua 存在时脚本系统启用真实 VM,否则走 stub 模式
```

## 2. 获取代码

```bash
git clone https://github.com/L0stInFades/NEXT.git
cd NEXT
```

分支模型见 [`docs/ENGINEERING_HANDBOOK.md`](docs/ENGINEERING_HANDBOOK.md);默认从 `master`(可发布的引擎基线)拉取。

## 3. 构建预设 (CMake presets)

> 渲染交给 UE5、物理交给 Jolt(见 [ADR-0005](docs/adr/0005-ue5-renderer-jolt-headless-world.md));本仓库是 **headless 权威世界核心**,**不构建渲染器**。所有预设都无渲染器。

| 预设 | 用途 | 平台 |
|---|---|---|
| `terminal-dev` | 终端/HackOps 技术线;最快、构建 demo+工具,不含测试 | macOS / Linux / Windows |
| `headless` | headless 核心库 + 工具 + 测试(RelWithDebInfo) | macOS / Linux / Windows |
| `asan` | ASan+UBSan 测试构建(**主要正确性闸门**) | macOS / Linux |

```bash
cmake --preset terminal-dev
cmake --build --preset terminal-dev
```

## 4. 跑通测试(推荐先做这个)

```bash
cmake --preset asan
cmake --build out/build/asan -j
ctest --test-dir out/build/asan --output-on-failure
```

应看到 10 个套件 100% 通过(Foundation / 序列化 / Job / ECS / 资产 / 平台 / 数学 / 流送 / 任务 / 脚本),且无 ASan/UBSan 报错。

## 5. 运行 demo

```bash
# 终端/HackOps 技术线(headless,无需渲染器)
out/build/terminal-dev/bin/hackops_demo \
  --policy tools/nvim_surface_probe/sample_policy.py \
  --snapshot /tmp/hackops-policy-snapshot.txt
```

> 带画面的运行在 **UE5 视图客户端**里(独立仓库/项目),通过 sim↔UE5 边界消费本核心的快照流;见 [`docs/design/sim-ue5-boundary.md`](docs/design/sim-ue5-boundary.md)。本仓库自身只跑 headless。

## 6. 代码质量工具(提交前)

```bash
scripts/clang_format.sh            # 按 .clang-format 格式化
scripts/clang_format_diff.sh       # 只检查你改动的行(CI 用同一脚本)
scripts/clang_tidy.sh path/to.cpp  # 静态分析(macOS 会自动带 SDK sysroot)
```

完整说明见 [`docs/CODE_QUALITY.md`](docs/CODE_QUALITY.md)。

## 7. 下一步

- 想理解系统全貌 / 各模块成熟度 → [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)
- 想提交代码 → [`CONTRIBUTING.md`](CONTRIBUTING.md)
- 在引擎上做游戏 → [`docs/ENGINEERING_HANDBOOK.md`](docs/ENGINEERING_HANDBOOK.md)(§游戏如何消费引擎)
- 已知缺口 → [`docs/TECH_DEBT.md`](docs/TECH_DEBT.md)

## 8. 常见问题

- **找不到 lz4/zstd**:`brew install lz4 zstd`(或系统包管理器);缺失时资产以未压缩方式打包,不影响构建。
- **Linux 无法渲染**:目前只有 DX12(Windows)和 Metal(macOS)后端,Linux 仅支持 `terminal-dev` 与测试;Vulkan 后端见 [`docs/TECH_DEBT.md`](docs/TECH_DEBT.md)。
- **clang-tidy 在 macOS 报 `cstdint not found`**:用 `scripts/clang_tidy.sh`,它会自动注入 `-isysroot $(xcrun --show-sdk-path)`。
