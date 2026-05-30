#include "next/sandbox/bytecode.h"

#include <cstring>
#include <stdexcept>

#include "next/foundation/endian.h"

namespace Next::sandbox {

BytecodeBuilder::Label BytecodeBuilder::NewLabel() {
    labels_.push_back(kUnbound);
    return static_cast<Label>(labels_.size() - 1);
}

void BytecodeBuilder::Bind(Label label) {
    labels_.at(label) = static_cast<uint32_t>(code_.size());
}

uint32_t BytecodeBuilder::Here() const {
    return static_cast<uint32_t>(code_.size());
}

uint32_t BytecodeBuilder::AddressOf(Label label) const {
    const uint32_t off = labels_.at(label);
    if (off == kUnbound) {
        throw std::logic_error("BytecodeBuilder: AddressOf on an unbound label");
    }
    return off;
}

void BytecodeBuilder::EmitU8(uint8_t v) {
    code_.push_back(v);
}

void BytecodeBuilder::EmitU16(uint16_t v) {
    AppendLE<uint16_t>(code_, v);
}

void BytecodeBuilder::EmitU32(uint32_t v) {
    AppendLE<uint32_t>(code_, v);
}

void BytecodeBuilder::EmitI64(int64_t v) {
    AppendLE<int64_t>(code_, v);
}

BytecodeBuilder& BytecodeBuilder::Emit(Op op) {
    EmitU8(static_cast<uint8_t>(op));
    return *this;
}

BytecodeBuilder& BytecodeBuilder::PushI(int64_t value) {
    EmitU8(static_cast<uint8_t>(Op::Push));
    EmitI64(value);
    return *this;
}

BytecodeBuilder& BytecodeBuilder::PushF(double value) {
    int64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return PushI(bits);
}

BytecodeBuilder& BytecodeBuilder::LdLoc(uint16_t index) {
    EmitU8(static_cast<uint8_t>(Op::LdLoc));
    EmitU16(index);
    return *this;
}

BytecodeBuilder& BytecodeBuilder::StLoc(uint16_t index) {
    EmitU8(static_cast<uint8_t>(Op::StLoc));
    EmitU16(index);
    return *this;
}

void BytecodeBuilder::EmitBranch(Op op, Label target, FixupKind kind) {
    EmitU8(static_cast<uint8_t>(op));
    fixups_.push_back(Fixup{static_cast<uint32_t>(code_.size()), target, kind});
    EmitU32(0);  // placeholder, patched in Build()
}

BytecodeBuilder& BytecodeBuilder::Jmp(Label target) {
    EmitBranch(Op::Jmp, target, FixupKind::RelI32);
    return *this;
}
BytecodeBuilder& BytecodeBuilder::Jz(Label target) {
    EmitBranch(Op::Jz, target, FixupKind::RelI32);
    return *this;
}
BytecodeBuilder& BytecodeBuilder::Jnz(Label target) {
    EmitBranch(Op::Jnz, target, FixupKind::RelI32);
    return *this;
}
BytecodeBuilder& BytecodeBuilder::Call(Label target) {
    EmitBranch(Op::Call, target, FixupKind::AbsU32);
    return *this;
}

BytecodeBuilder& BytecodeBuilder::HostCall(gameapi::CallId id) {
    EmitU8(static_cast<uint8_t>(Op::HostCall));
    EmitU32(static_cast<uint32_t>(id));
    return *this;
}

std::vector<uint8_t> BytecodeBuilder::Build() const {
    std::vector<uint8_t> code = code_;
    for (const Fixup& fx : fixups_) {
        const uint32_t target = labels_.at(fx.label);
        if (target == kUnbound) {
            throw std::logic_error("BytecodeBuilder: branch to an unbound label");
        }
        uint32_t encoded = 0;
        if (fx.kind == FixupKind::RelI32) {
            const int32_t rel = static_cast<int32_t>(target) - static_cast<int32_t>(fx.operandPos + 4);
            std::memcpy(&encoded, &rel, sizeof(encoded));
        } else {
            encoded = target;
        }
        WriteLE<uint32_t>(code.data() + fx.operandPos, encoded);
    }

    std::vector<uint8_t> image;
    image.reserve(kModuleHeaderSize + code.size());
    image.insert(image.end(), kModuleMagic, kModuleMagic + 4);
    AppendLE<uint32_t>(image, kModuleVersion);
    AppendLE<uint32_t>(image, static_cast<uint32_t>(code.size()));
    image.insert(image.end(), code.begin(), code.end());
    return image;
}

}  // namespace Next::sandbox
