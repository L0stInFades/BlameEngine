#pragma once

#include <cstdint>
#include <vector>

#include "next/gameapi/abi.h"

// Reference VM bytecode (ADR-0008). A deliberately small stack machine: there is NO instruction
// that can touch anything outside the operand stack, the current frame's locals, and the linear
// memory arena. The ONLY way out is HostCall, fully mediated by a HostGateway. That absence of
// escape primitives — not a cage around native code — is what makes the reference backend safe.
//
// Encoding: 1-byte opcode + fixed little-endian operands. Stack ops pop the right operand first:
//   binary `a b OP` -> push (a OP b);  store `addr val ST` -> pop val, pop addr;  load `addr LD`.

namespace Next::sandbox {

enum class Op : uint8_t {
    Halt = 0,
    Nop = 1,
    Push = 2,  // + i64 imm: push imm
    Pop = 3,
    Dup = 4,
    Swap = 5,
    LdLoc = 6,  // + u16 idx: push locals[idx]
    StLoc = 7,  // + u16 idx: locals[idx] = pop
    Ld8 = 8,    // pop addr -> push zero-extended byte
    Ld16 = 9,
    Ld32 = 10,
    Ld64 = 11,
    St8 = 12,  // pop val, pop addr -> store low bits
    St16 = 13,
    St32 = 14,
    St64 = 15,
    Add = 16,
    Sub = 17,
    Mul = 18,
    Div = 19,  // signed; /0 traps; INT64_MIN/-1 defined as INT64_MIN (no UB)
    Mod = 20,
    Neg = 21,
    And = 22,
    Or = 23,
    Xor = 24,
    Not = 25,
    Shl = 26,   // shift amount masked to 0..63
    Shr = 27,   // arithmetic (signed) shift right, amount masked to 0..63
    FAdd = 28,  // operands reinterpreted as IEEE-754 double; result reinterpreted back
    FSub = 29,
    FMul = 30,
    FDiv = 31,
    I2F = 32,  // int64 -> double bits
    F2I = 33,  // double bits -> int64 (NaN/overflow defined, no UB)
    Eq = 34,   // push 1/0
    Ne = 35,
    Lts = 36,  // signed <
    Les = 37,
    Gts = 38,
    Ges = 39,
    Jmp = 40,       // + i32 rel: pc = next + rel
    Jz = 41,        // + i32 rel: pop cond; if cond==0 branch
    Jnz = 42,       // + i32 rel: pop cond; if cond!=0 branch
    Call = 43,      // + u32 abs: push frame, pc = abs
    Ret = 44,       // pop frame; empty call stack -> clean halt
    HostCall = 45,  // + u32 CallId: pop retLen,retOff,argsLen,argsOff; push Status
};

// Fixed locals per call frame. Local index out of range traps IllegalInstruction.
constexpr uint16_t kLocalsPerFrame = 16;

// Module image header (little-endian), prepended by BytecodeBuilder::Build, parsed by RefVm.
constexpr uint8_t kModuleMagic[4] = {'N', 'B', 'V', 'M'};
constexpr uint32_t kModuleVersion = 1;
constexpr uint32_t kModuleHeaderSize = 12;  // magic[4] + version(u32) + codeLen(u32)

// Fluent, label-aware assembler. Emits code-relative addresses; Build() resolves branch fixups
// and prepends the header. Avoids a text parser while keeping guest programs readable in tests
// and usable as a future toolchain target.
class BytecodeBuilder {
public:
    using Label = uint32_t;

    Label NewLabel();
    void Bind(Label label);                 // bind to the current code offset
    uint32_t Here() const;                  // current code offset (code-relative)
    uint32_t AddressOf(Label label) const;  // bound label's offset

    BytecodeBuilder& Emit(Op op);  // opcode with no operand
    BytecodeBuilder& PushI(int64_t value);
    BytecodeBuilder& PushF(double value);
    BytecodeBuilder& LdLoc(uint16_t index);
    BytecodeBuilder& StLoc(uint16_t index);
    BytecodeBuilder& Jmp(Label target);
    BytecodeBuilder& Jz(Label target);
    BytecodeBuilder& Jnz(Label target);
    BytecodeBuilder& Call(Label target);
    BytecodeBuilder& HostCall(gameapi::CallId id);

    // Resolve fixups and return the full module image (header + code). Throws std::logic_error
    // if a referenced label was never bound (a builder bug, surfaced loudly at construction time).
    std::vector<uint8_t> Build() const;

    uint32_t CodeSize() const { return static_cast<uint32_t>(code_.size()); }

private:
    enum class FixupKind { RelI32, AbsU32 };
    struct Fixup {
        uint32_t operandPos;  // where the 4-byte operand begins (code-relative)
        Label label;
        FixupKind kind;
    };

    void EmitU8(uint8_t v);
    void EmitU16(uint16_t v);
    void EmitU32(uint32_t v);
    void EmitI64(int64_t v);
    void EmitBranch(Op op, Label target, FixupKind kind);

    std::vector<uint8_t> code_;
    std::vector<uint32_t> labels_;  // label -> offset, or kUnbound
    std::vector<Fixup> fixups_;
    static constexpr uint32_t kUnbound = 0xFFFF'FFFFu;
};

}  // namespace Next::sandbox
