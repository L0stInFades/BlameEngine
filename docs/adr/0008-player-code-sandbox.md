# 0008. 玩家代码沙箱:由安全边界定义,后端可换(非绑定 WASM)

- **Status**: Accepted
- **Date**: 2026-05-30
- **Refines**: [ADR-0005](0005-ue5-renderer-jolt-headless-world.md)(它把"安全玩家代码运行时"立为护城河,举例 WASM;本 ADR 定**安全模型与可换后端**)。细节见 [`../design/gameapi-and-sandbox.md`](../design/gameapi-and-sandbox.md)。

## Context
产品的赌注是"玩家写真实代码去 hack 一个开放世界"。于是有两个**同级**的一等公民:**玩家写的代码** 和 **沙箱的安全性**。ADR-0005 举例 WASM,但技术不该被钉死——沙箱的护城河是**安全边界本身**,不是某个 VM。同时它必须**确定性**(回放/反作弊/AI-agent/服务器权威)。当前 HackOps 的 `popen(python3)` 是零隔离,必须替换。

## Decision
沙箱 = `engine/sandbox`(库 `next_sandbox`),**由安全契约定义、后端可插拔**。

1. **安全契约(后端无关,五条红线)**:
   - **零环境权限(zero ambient authority)**:guest 除了调用宿主显式暴露的 host-call,**什么都做不了**——无文件、无网络、无 syscall、无墙钟、无 RNG、无线程。
   - **能力门控的 host-call**:唯一的出口是 host-call,且被 `HostGateway` 全程中介——能力校验([[../adr/0007-game-api-contract]] 的 `CapabilitySet`)+ 入参校验;暴露给 guest 的 host 面**就是** Game API 的裁剪子集,别无其它。
   - **燃料计量(fuel)**:每一步 guest 工作扣燃料,耗尽即 trap;以**计数**而非墙钟约束 CPU → 确定性。
   - **内存上限**:guest 内存是固定 arena,越界/超额即 trap。
   - **故障隔离**:guest 的 trap(燃料耗尽/OOM/越界/非法指令/能力拒绝)被**收敛**——只中止 guest,绝不污染宿主。
   - 推论:**确定性**——guest 是 `(字节码, 初始内存, host-call 返回值, 燃料预算)` 的纯函数。

2. **可换后端,统一接口** `ISandbox`:`LoadModule / Instantiate(policy, gateway) / Call(entry, args) -> RunResult{trap, fuelUsed, hostCalls, ret}`。`SandboxPolicy{ fuel, memoryBytes, stackSlots, maxHostCalls, capabilities }`。**安全契约属于接口,不属于某个后端**。

3. **参考后端 = 自研确定性燃料计量字节码 VM**(`RefVm`,"security by construction"):指令集里**根本不存在** I/O / syscall 指令,唯一外联是 `HOSTCALL`(走 gateway);操作数栈/局部/线性内存全部边界检查;DIV/MOD-0、栈溢出、越界、非法 opcode 全部 trap。它**先证明安全契约可落地且可 headless 测试**,并作为 WASM 后端的参照前身。Host-call 约定(线性内存 + 指针 + 长度,边界检查)刻意对齐 WASM,使后端切换零重构边界。

4. **真实代码前端是下一步,接口已就位**:玩家用 C/Rust/AssemblyScript 等编译到沙箱可执行格式(WASM 一类,或我们的字节码)。本轮交付**安全契约 + `ISandbox` + 可证明安全的参考后端**;语言前端(及生产级 WASM 后端,wasm3/wasmtime)落在同一接口之后,不动边界。重隔离需求(原生代码)出现时再叠加 microVM(同一接口)。

## Consequences
- 正面:安全是结构性的(参考 VM 没有逃逸原语,不是"给原生代码套笼子");燃料 + arena → CPU/内存有界且确定;后端可换 → 不被 WASM 锁死、可演进到 wasmtime/microVM;沙箱只能看见 Game API 子集 → 最小权限、可审计;全程 headless 可测(含对抗性逃逸/非确定/溢出测试)。
- 负面/成本:参考 VM 不是玩家语言前端(需后续 WASM 后端或编译器);两段式 host-call(指针 + 边界检查)有少量开销(换安全/可移植,值);要维护字节码格式与 gateway。

## Alternatives considered
- **绑死 WASM(wasmtime/wasmtime-only)**:否——把护城河绑在某个运行时上;wasmtime 是重 Rust 依赖,overnight 落地+测试风险高;接口化后它只是后端之一。
- **`popen`/进程级跑玩家脚本**(现状):否——零隔离、零确定性、零能力门控,是要替换的反面教材。
- **OS 级隔离(容器 / microVM)做默认**:备选——重、慢启动、跨平台差异大;留给"原生代码 / 重隔离"档位,叠在同一 `ISandbox` 之后。
- **直接解释玩家的高级语言(如内嵌 Lua/Python)**:否——解释器自身攻击面大、确定性差、能力难裁剪;旧 `engine/script` 的 Lua 框架不满足安全契约,不作为玩家代码运行时。
