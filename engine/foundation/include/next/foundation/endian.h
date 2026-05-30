#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <vector>

// Little-endian fixed-width integer codecs — the single source of truth for on-the-wire / bytecode
// integer encoding. Routing every read and write through these makes producers (e.g. the sandbox
// BytecodeBuilder) and consumers (the VM decoder) provably symmetric, so the two sides can never
// silently drift. Works for signed and unsigned 8/16/32/64-bit integers (the value is reinterpreted
// through its same-width unsigned type, so two's-complement bytes round-trip exactly).

namespace Next {

namespace detail {
template<typename T>
using LeUnsigned = std::conditional_t<
    sizeof(T) == 8, uint64_t,
    std::conditional_t<sizeof(T) == 4, uint32_t, std::conditional_t<sizeof(T) == 2, uint16_t, uint8_t>>>;
}  // namespace detail

// Read sizeof(T) little-endian bytes from `src` (which must hold at least sizeof(T) bytes).
template<typename T>
inline T ReadLE(const uint8_t* src) {
    static_assert(std::is_integral_v<T>, "ReadLE requires an integral type");
    using U = detail::LeUnsigned<T>;
    U u = 0;
    for (size_t i = 0; i < sizeof(T); ++i) {
        u |= static_cast<U>(src[i]) << (8 * i);
    }
    T out;
    std::memcpy(&out, &u, sizeof(T));
    return out;
}

// Write `value` as sizeof(T) little-endian bytes into `dst` (which must hold at least sizeof(T)).
template<typename T>
inline void WriteLE(uint8_t* dst, T value) {
    static_assert(std::is_integral_v<T>, "WriteLE requires an integral type");
    using U = detail::LeUnsigned<T>;
    U u;
    std::memcpy(&u, &value, sizeof(T));
    for (size_t i = 0; i < sizeof(T); ++i) {
        dst[i] = static_cast<uint8_t>((u >> (8 * i)) & 0xFF);
    }
}

// Append `value` as little-endian bytes to a growable byte buffer.
template<typename T>
inline void AppendLE(std::vector<uint8_t>& out, T value) {
    const size_t offset = out.size();
    out.resize(offset + sizeof(T));
    WriteLE<T>(out.data() + offset, value);
}

}  // namespace Next
