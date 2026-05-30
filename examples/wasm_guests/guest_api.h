// Player-side Game API SDK for C++ guests compiled to wasm32 (ADR-0011).
//
// This is the C++ half of the player-facing toolchain: a freestanding (no libc / no STL) header
// that mirrors the frozen flat ABI from engine/gameapi/include/next/gameapi/abi.h and wraps the
// single imported host_call into typed helpers. A guest places POD args in its own linear memory
// and passes their byte offsets — exactly the NBVM HostCall window contract, just expressed as a
// function call. The host (Wasm3Sandbox) routes it through the same capability-gated GameApi.

#pragma once
#include <stdint.h>

extern "C" __attribute__((import_module("env"), import_name("host_call"))) int host_call(int callId, int argsOff,
                                                                                         int argsLen, int retOff,
                                                                                         int retLen);

// CallId values are frozen and append-only (see abi.h). Only the ones the demo guests use.
enum : int {
    CALL_SELF = 3,
    CALL_GET_POSITION = 5,
    CALL_QUERY_BY_TAG = 6,
    CALL_MOVE_TO = 9,
};

// Status values (abi.h Status). Ok == 0.
enum : int { ST_OK = 0 };

struct Vec3 {
    float x, y, z;
};
struct EntityArg {
    uint64_t entity;
};
struct EntityResult {
    uint64_t entity;
};
struct PositionResult {
    Vec3 position;
};
struct QueryByTagArgs {
    uint32_t tag;
};
struct EntityListHeader {
    uint32_t count;
    uint32_t capacity;
};
struct MoveToArgs {
    Vec3 target;
    float maxSpeed;
};

// In wasm32 a data address IS its linear-memory byte offset; that is what host_call windows expect.
inline int Off(const void* p) {
    return static_cast<int>(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)));
}

// --- typed helpers (each owns a static arg/ret buffer; guests are single-threaded) ---

inline uint64_t api_self() {
    static EntityResult r;
    if (host_call(CALL_SELF, 0, 0, Off(&r), sizeof(r)) != ST_OK)
        return 0;
    return r.entity;
}

inline bool api_get_position(uint64_t entity, Vec3& out) {
    static EntityArg a;
    static PositionResult r;
    a.entity = entity;
    if (host_call(CALL_GET_POSITION, Off(&a), sizeof(a), Off(&r), sizeof(r)) != ST_OK)
        return false;
    out = r.position;
    return true;
}

// Query entities carrying `tag`; writes up to `cap` ids (ascending) into out[], returns total count.
inline int api_query_by_tag(uint32_t tag, uint64_t* out, int cap) {
    static QueryByTagArgs a;
    static uint8_t buf[8 + 8 * 128];  // header + up to 128 ids
    a.tag = tag;
    host_call(CALL_QUERY_BY_TAG, Off(&a), sizeof(a), Off(buf), sizeof(buf));
    const EntityListHeader* h = reinterpret_cast<const EntityListHeader*>(buf);
    const uint64_t* ids = reinterpret_cast<const uint64_t*>(buf + 8);
    int n = static_cast<int>(h->count) < cap ? static_cast<int>(h->count) : cap;
    for (int i = 0; i < n; ++i)
        out[i] = ids[i];
    return static_cast<int>(h->count);
}

inline int api_move_to(float x, float y, float z, float maxSpeed) {
    static MoveToArgs a;
    a.target = {x, y, z};
    a.maxSpeed = maxSpeed;
    return host_call(CALL_MOVE_TO, Off(&a), sizeof(a), 0, 0);
}
