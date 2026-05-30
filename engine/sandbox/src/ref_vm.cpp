#include "next/sandbox/ref_vm.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>

#include "next/foundation/endian.h"
#include "next/sandbox/bytecode.h"

namespace Next::sandbox {
namespace {

// ---- bit-exact reinterpretation (no unions, no aliasing UB) ----
double BitsToDouble(int64_t bits) {
    double d = 0.0;
    std::memcpy(&d, &bits, sizeof(d));
    return d;
}
int64_t DoubleToBits(double d) {
    int64_t b = 0;
    std::memcpy(&b, &d, sizeof(b));
    return b;
}

// ---- little-endian operand reads; callers MUST bounds-check via NEED() first. These delegate to
// the shared codec so the VM decoder stays byte-symmetric with the BytecodeBuilder encoder. ----
uint16_t ReadU16(const uint8_t* p) {
    return ReadLE<uint16_t>(p);
}
uint32_t ReadU32(const uint8_t* p) {
    return ReadLE<uint32_t>(p);
}
int64_t ReadI64(const uint8_t* p) {
    return ReadLE<int64_t>(p);
}

constexpr int64_t kI64Min = std::numeric_limits<int64_t>::min();
constexpr int64_t kI64Max = std::numeric_limits<int64_t>::max();

}  // namespace

const char* ToString(TrapReason reason) {
    switch (reason) {
        case TrapReason::None:
            return "None";
        case TrapReason::FuelExhausted:
            return "FuelExhausted";
        case TrapReason::OutOfMemory:
            return "OutOfMemory";
        case TrapReason::BadMemoryAccess:
            return "BadMemoryAccess";
        case TrapReason::IllegalInstruction:
            return "IllegalInstruction";
        case TrapReason::StackOverflow:
            return "StackOverflow";
        case TrapReason::StackUnderflow:
            return "StackUnderflow";
        case TrapReason::DivideByZero:
            return "DivideByZero";
        case TrapReason::HostCallDenied:
            return "HostCallDenied";
        case TrapReason::HostCallError:
            return "HostCallError";
    }
    return "Unknown";
}

bool RefVm::LoadModule(const uint8_t* image, size_t size, std::string* error) {
    auto fail = [&](const char* msg) {
        if (error != nullptr)
            *error = msg;
        return false;
    };
    if (image == nullptr || size < kModuleHeaderSize) {
        return fail("module too small for header");
    }
    if (std::memcmp(image, kModuleMagic, 4) != 0) {
        return fail("bad module magic");
    }
    const uint32_t version = ReadU32(image + 4);
    if (version != kModuleVersion) {
        return fail("unsupported module version");
    }
    const uint32_t codeLen = ReadU32(image + 8);
    if (static_cast<size_t>(kModuleHeaderSize) + codeLen != size) {
        return fail("module code length mismatch");
    }
    code_.assign(image + kModuleHeaderSize, image + kModuleHeaderSize + codeLen);
    return true;
}

RunResult RefVm::Run(const SandboxPolicy& policy, HostGateway& gateway, uint32_t entryPc, int64_t arg) noexcept {
    RunResult result;
    // Reject an absurd arena up front: deterministic, and does not depend on allocation actually
    // failing (overcommit can hide that).
    if (policy.memoryBytes > kMaxArenaBytes) {
        result.trap = TrapReason::OutOfMemory;
        return result;
    }
    // Accounting lives OUTSIDE the try so every exit path records it — clean halt, goto-done trap,
    // and exception-converted trap alike (anti-cheat/replay rely on exact fuelUsed/hostCalls).
    uint64_t fuel = policy.fuel;
    uint64_t hostCalls = 0;
    // Fault isolation (ADR-0008 red line #5): a guest run NEVER throws into the host. Allocation
    // failures (gateway-side, or any other) are converted to traps, not propagated.
    try {
        const size_t codeLen = code_.size();
        const uint8_t* code = code_.data();

        std::vector<int64_t> stack;
        stack.reserve(policy.stackSlots < 64 ? policy.stackSlots : 64);

        struct Frame {
            uint32_t returnPc = 0;
            int64_t locals[kLocalsPerFrame] = {};
        };
        std::vector<Frame> frames;
        frames.emplace_back();  // entry frame

        std::vector<uint8_t> mem(policy.memoryBytes, 0);

        uint32_t pc = entryPc;
        TrapReason trap = TrapReason::None;

// These macros share the function's locals and jump to `done` on fault — the standard shape for
// a tight interpreter loop. Undefined at the end of the function.
#define PUSH(v)                                   \
    do {                                          \
        if (stack.size() >= policy.stackSlots) {  \
            trap = TrapReason::StackOverflow;     \
            goto done;                            \
        }                                         \
        stack.push_back(static_cast<int64_t>(v)); \
    } while (0)
#define POP(out)                               \
    do {                                       \
        if (stack.empty()) {                   \
            trap = TrapReason::StackUnderflow; \
            goto done;                         \
        }                                      \
        (out) = stack.back();                  \
        stack.pop_back();                      \
    } while (0)
#define NEED(n)                                        \
    do {                                               \
        if (static_cast<size_t>(pc) + (n) > codeLen) { \
            trap = TrapReason::IllegalInstruction;     \
            goto done;                                 \
        }                                              \
    } while (0)

        // Place the entry argument on the operand stack.
        PUSH(arg);

        while (true) {
            if (pc >= codeLen) {  // fell off the end without Halt/Ret
                trap = TrapReason::IllegalInstruction;
                goto done;
            }
            const Op op = static_cast<Op>(code[pc]);
            const uint64_t cost = (op == Op::HostCall) ? kHostCallFuel : 1;
            if (fuel < cost) {
                trap = TrapReason::FuelExhausted;
                goto done;
            }
            fuel -= cost;
            ++pc;  // past opcode

            switch (op) {
                case Op::Halt: {
                    result.ret = stack.empty() ? 0 : stack.back();
                    goto done;
                }
                case Op::Nop:
                    break;
                case Op::Push: {
                    NEED(8);
                    const int64_t v = ReadI64(code + pc);
                    pc += 8;
                    PUSH(v);
                    break;
                }
                case Op::Pop: {
                    int64_t t;
                    POP(t);
                    break;
                }
                case Op::Dup: {
                    if (stack.empty()) {
                        trap = TrapReason::StackUnderflow;
                        goto done;
                    }
                    const int64_t t = stack.back();
                    PUSH(t);
                    break;
                }
                case Op::Swap: {
                    if (stack.size() < 2) {
                        trap = TrapReason::StackUnderflow;
                        goto done;
                    }
                    std::swap(stack[stack.size() - 1], stack[stack.size() - 2]);
                    break;
                }
                case Op::LdLoc: {
                    NEED(2);
                    const uint16_t idx = ReadU16(code + pc);
                    pc += 2;
                    if (idx >= kLocalsPerFrame) {
                        trap = TrapReason::IllegalInstruction;
                        goto done;
                    }
                    PUSH(frames.back().locals[idx]);
                    break;
                }
                case Op::StLoc: {
                    NEED(2);
                    const uint16_t idx = ReadU16(code + pc);
                    pc += 2;
                    if (idx >= kLocalsPerFrame) {
                        trap = TrapReason::IllegalInstruction;
                        goto done;
                    }
                    int64_t v;
                    POP(v);
                    frames.back().locals[idx] = v;
                    break;
                }
                case Op::Ld8:
                case Op::Ld16:
                case Op::Ld32:
                case Op::Ld64: {
                    const uint64_t width = (op == Op::Ld8) ? 1 : (op == Op::Ld16) ? 2 : (op == Op::Ld32) ? 4 : 8;
                    int64_t addrS;
                    POP(addrS);
                    const uint64_t addr = static_cast<uint64_t>(addrS);
                    if (width > mem.size() || addr > mem.size() - width) {
                        trap = TrapReason::BadMemoryAccess;
                        goto done;
                    }
                    uint64_t v = 0;
                    for (uint64_t i = 0; i < width; ++i)
                        v |= static_cast<uint64_t>(mem[addr + i]) << (8 * i);
                    PUSH(static_cast<int64_t>(v));
                    break;
                }
                case Op::St8:
                case Op::St16:
                case Op::St32:
                case Op::St64: {
                    const uint64_t width = (op == Op::St8) ? 1 : (op == Op::St16) ? 2 : (op == Op::St32) ? 4 : 8;
                    int64_t valS;
                    int64_t addrS;
                    POP(valS);
                    POP(addrS);
                    const uint64_t addr = static_cast<uint64_t>(addrS);
                    if (width > mem.size() || addr > mem.size() - width) {
                        trap = TrapReason::BadMemoryAccess;
                        goto done;
                    }
                    const uint64_t v = static_cast<uint64_t>(valS);
                    for (uint64_t i = 0; i < width; ++i)
                        mem[addr + i] = static_cast<uint8_t>((v >> (8 * i)) & 0xFF);
                    break;
                }
                case Op::Add: {
                    int64_t b, a;
                    POP(b);
                    POP(a);
                    PUSH(static_cast<int64_t>(static_cast<uint64_t>(a) + static_cast<uint64_t>(b)));
                    break;
                }
                case Op::Sub: {
                    int64_t b, a;
                    POP(b);
                    POP(a);
                    PUSH(static_cast<int64_t>(static_cast<uint64_t>(a) - static_cast<uint64_t>(b)));
                    break;
                }
                case Op::Mul: {
                    int64_t b, a;
                    POP(b);
                    POP(a);
                    PUSH(static_cast<int64_t>(static_cast<uint64_t>(a) * static_cast<uint64_t>(b)));
                    break;
                }
                case Op::Div: {
                    int64_t b, a;
                    POP(b);
                    POP(a);
                    if (b == 0) {
                        trap = TrapReason::DivideByZero;
                        goto done;
                    }
                    PUSH((a == kI64Min && b == -1) ? kI64Min : a / b);
                    break;
                }
                case Op::Mod: {
                    int64_t b, a;
                    POP(b);
                    POP(a);
                    if (b == 0) {
                        trap = TrapReason::DivideByZero;
                        goto done;
                    }
                    PUSH((a == kI64Min && b == -1) ? 0 : a % b);
                    break;
                }
                case Op::Neg: {
                    int64_t a;
                    POP(a);
                    PUSH((a == kI64Min) ? kI64Min : -a);
                    break;
                }
                case Op::And: {
                    int64_t b, a;
                    POP(b);
                    POP(a);
                    PUSH(a & b);
                    break;
                }
                case Op::Or: {
                    int64_t b, a;
                    POP(b);
                    POP(a);
                    PUSH(a | b);
                    break;
                }
                case Op::Xor: {
                    int64_t b, a;
                    POP(b);
                    POP(a);
                    PUSH(a ^ b);
                    break;
                }
                case Op::Not: {
                    int64_t a;
                    POP(a);
                    PUSH(~a);
                    break;
                }
                case Op::Shl: {
                    int64_t b, a;
                    POP(b);
                    POP(a);
                    PUSH(static_cast<int64_t>(static_cast<uint64_t>(a) << (static_cast<uint64_t>(b) & 63)));
                    break;
                }
                case Op::Shr: {
                    int64_t b, a;
                    POP(b);
                    POP(a);
                    // Portable arithmetic shift right: signed `>>` is implementation-defined in C++17,
                    // so compute it via defined unsigned ops to keep the result deterministic across
                    // compilers/platforms (a hard requirement for replay / anti-cheat).
                    const uint32_t n = static_cast<uint32_t>(static_cast<uint64_t>(b) & 63);
                    const uint64_t ua = static_cast<uint64_t>(a);
                    const int64_t r = (a >= 0) ? static_cast<int64_t>(ua >> n) : ~static_cast<int64_t>(~ua >> n);
                    PUSH(r);
                    break;
                }
                case Op::FAdd:
                case Op::FSub:
                case Op::FMul:
                case Op::FDiv: {
                    int64_t bb, ab;
                    POP(bb);
                    POP(ab);
                    const double a = BitsToDouble(ab);
                    const double b = BitsToDouble(bb);
                    double r = 0.0;
                    switch (op) {
                        case Op::FAdd:
                            r = a + b;
                            break;
                        case Op::FSub:
                            r = a - b;
                            break;
                        case Op::FMul:
                            r = a * b;
                            break;
                        default:
                            r = a / b;
                            break;  // IEEE-754: /0 -> inf/nan, not a trap
                    }
                    PUSH(DoubleToBits(r));
                    break;
                }
                case Op::I2F: {
                    int64_t a;
                    POP(a);
                    PUSH(DoubleToBits(static_cast<double>(a)));
                    break;
                }
                case Op::F2I: {
                    int64_t a;
                    POP(a);
                    const double d = BitsToDouble(a);
                    int64_t r = 0;
                    if (std::isnan(d)) {
                        r = 0;
                    } else if (d >= 9223372036854775808.0) {
                        r = kI64Max;
                    } else if (d < -9223372036854775808.0) {
                        r = kI64Min;
                    } else {
                        r = static_cast<int64_t>(d);
                    }
                    PUSH(r);
                    break;
                }
                case Op::Eq: {
                    int64_t b, a;
                    POP(b);
                    POP(a);
                    PUSH(a == b ? 1 : 0);
                    break;
                }
                case Op::Ne: {
                    int64_t b, a;
                    POP(b);
                    POP(a);
                    PUSH(a != b ? 1 : 0);
                    break;
                }
                case Op::Lts: {
                    int64_t b, a;
                    POP(b);
                    POP(a);
                    PUSH(a < b ? 1 : 0);
                    break;
                }
                case Op::Les: {
                    int64_t b, a;
                    POP(b);
                    POP(a);
                    PUSH(a <= b ? 1 : 0);
                    break;
                }
                case Op::Gts: {
                    int64_t b, a;
                    POP(b);
                    POP(a);
                    PUSH(a > b ? 1 : 0);
                    break;
                }
                case Op::Ges: {
                    int64_t b, a;
                    POP(b);
                    POP(a);
                    PUSH(a >= b ? 1 : 0);
                    break;
                }
                case Op::Jmp:
                case Op::Jz:
                case Op::Jnz: {
                    NEED(4);
                    int32_t rel = 0;
                    const uint32_t raw = ReadU32(code + pc);
                    std::memcpy(&rel, &raw, sizeof(rel));
                    pc += 4;
                    bool take = true;
                    if (op != Op::Jmp) {
                        int64_t cond;
                        POP(cond);
                        take = (op == Op::Jz) ? (cond == 0) : (cond != 0);
                    }
                    if (take) {
                        const int64_t target = static_cast<int64_t>(pc) + rel;
                        if (target < 0 || static_cast<uint64_t>(target) >= codeLen) {
                            trap = TrapReason::IllegalInstruction;
                            goto done;
                        }
                        pc = static_cast<uint32_t>(target);
                    }
                    break;
                }
                case Op::Call: {
                    NEED(4);
                    const uint32_t target = ReadU32(code + pc);
                    pc += 4;
                    if (target >= codeLen) {
                        trap = TrapReason::IllegalInstruction;
                        goto done;
                    }
                    if (frames.size() >= policy.callDepth) {
                        trap = TrapReason::StackOverflow;
                        goto done;
                    }
                    Frame nf;
                    nf.returnPc = pc;
                    frames.push_back(nf);
                    pc = target;
                    break;
                }
                case Op::Ret: {
                    if (frames.size() <= 1) {  // return from the entry frame -> clean halt
                        result.ret = stack.empty() ? 0 : stack.back();
                        goto done;
                    }
                    pc = frames.back().returnPc;
                    frames.pop_back();
                    break;
                }
                case Op::HostCall: {
                    NEED(4);
                    const uint32_t callId = ReadU32(code + pc);
                    pc += 4;
                    int64_t retLenS, retOffS, argsLenS, argsOffS;
                    POP(retLenS);
                    POP(retOffS);
                    POP(argsLenS);
                    POP(argsOffS);
                    if (hostCalls >= policy.maxHostCalls) {
                        trap = TrapReason::HostCallDenied;
                        goto done;
                    }
                    const uint64_t argsOff = static_cast<uint64_t>(argsOffS);
                    const uint64_t argsLen = static_cast<uint64_t>(argsLenS);
                    const uint64_t retOff = static_cast<uint64_t>(retOffS);
                    const uint64_t retLen = static_cast<uint64_t>(retLenS);
                    const uint64_t memSize = mem.size();
                    if (argsLen > 0 && (argsLen > memSize || argsOff > memSize - argsLen)) {
                        trap = TrapReason::BadMemoryAccess;
                        goto done;
                    }
                    if (retLen > 0 && (retLen > memSize || retOff > memSize - retLen)) {
                        trap = TrapReason::BadMemoryAccess;
                        goto done;
                    }
                    ++hostCalls;
                    const gameapi::Status st = gateway.Invoke(
                        static_cast<gameapi::CallId>(callId), mem.data(), static_cast<uint32_t>(memSize),
                        static_cast<uint32_t>(argsOff), static_cast<uint32_t>(argsLen), static_cast<uint32_t>(retOff),
                        static_cast<uint32_t>(retLen), policy.capabilities);
                    PUSH(static_cast<int64_t>(static_cast<int32_t>(st)));
                    break;
                }
                default:
                    trap = TrapReason::IllegalInstruction;
                    goto done;
            }
        }

    done:
        result.trap = trap;

#undef PUSH
#undef POP
#undef NEED
    } catch (const std::bad_alloc&) {
        result.trap = TrapReason::OutOfMemory;
    } catch (const std::length_error&) {
        result.trap = TrapReason::OutOfMemory;
    } catch (...) {
        result.trap = TrapReason::HostCallError;
    }
    // Recorded on EVERY path (goto-done falls through the skipped catch to here; an exception lands
    // here after the catch). fuel/hostCalls are the values consumed up to the exit point.
    result.fuelUsed = policy.fuel - fuel;
    result.hostCalls = hostCalls;
    return result;
}

}  // namespace Next::sandbox
