#include "next/sandbox/wasm_meter.h"

#include <cstring>

// Append-only WASM gas-metering transform (see wasm_meter.h). Pure byte manipulation — no wasm3,
// no STL beyond vector/string. The opcode immediate-skip table below was validated against the
// real clang (A*) and rustc (binary-search) guest modules.

namespace Next::sandbox {
namespace {

// ---- LEB128 ----------------------------------------------------------------------------------
bool ReadU(const uint8_t* d, size_t n, size_t& p, uint64_t& out) {
    out = 0;
    int shift = 0;
    while (p < n) {
        const uint8_t b = d[p++];
        out |= static_cast<uint64_t>(b & 0x7F) << shift;
        if ((b & 0x80) == 0)
            return true;
        shift += 7;
        if (shift >= 64)
            return false;
    }
    return false;
}

// Skip one LEB128 (works for both signed and unsigned: consume bytes until the high bit clears).
bool SkipLEB(const uint8_t* d, size_t n, size_t& p) {
    while (p < n) {
        if ((d[p++] & 0x80) == 0)
            return true;
    }
    return false;
}

void WriteU(std::vector<uint8_t>& o, uint64_t v) {
    do {
        uint8_t b = v & 0x7F;
        v >>= 7;
        if (v != 0)
            b |= 0x80;
        o.push_back(b);
    } while (v != 0);
}

void WriteS(std::vector<uint8_t>& o, int64_t v) {
    bool more = true;
    while (more) {
        uint8_t b = static_cast<uint8_t>(v & 0x7F);
        v >>= 7;  // arithmetic shift
        if ((v == 0 && (b & 0x40) == 0) || (v == -1 && (b & 0x40) != 0))
            more = false;
        else
            b |= 0x80;
        o.push_back(b);
    }
}

// ---- opcode immediate skip (advances p past the opcode's immediates) -------------------------
// Returns false on an opcode/encoding the meter cannot handle (fail-closed).
bool SkipBlockType(const uint8_t* d, size_t n, size_t& p) {
    if (p >= n)
        return false;
    const uint8_t b = d[p];
    // 0x40 empty, or a single value type byte.
    if (b == 0x40 || b == 0x7F || b == 0x7E || b == 0x7D || b == 0x7C || b == 0x7B || b == 0x70 || b == 0x6F) {
        ++p;
        return true;
    }
    return SkipLEB(d, n, p);  // s33 type index
}

// Skip the immediates of the instruction whose opcode is `op`, starting at p (just past the opcode).
bool SkipImmediates(uint8_t op, const uint8_t* d, size_t n, size_t& p) {
    switch (op) {
        // no immediate
        case 0x00:  // unreachable
        case 0x01:  // nop
        case 0x05:  // else
        case 0x0B:  // end
        case 0x0F:  // return
        case 0x1A:  // drop
        case 0x1B:  // select
        case 0xD1:  // ref.is_null
            return true;
        // block / loop / if -> blocktype
        case 0x02:
        case 0x03:
        case 0x04:
            return SkipBlockType(d, n, p);
        // single uleb index
        case 0x0C:  // br
        case 0x0D:  // br_if
        case 0x10:  // call
        case 0x20:  // local.get
        case 0x21:  // local.set
        case 0x22:  // local.tee
        case 0x23:  // global.get
        case 0x24:  // global.set
        case 0x25:  // table.get
        case 0x26:  // table.set
        case 0xD2:  // ref.func
            return SkipLEB(d, n, p);
        // call_indirect -> typeidx + tableidx
        case 0x11:
            return SkipLEB(d, n, p) && SkipLEB(d, n, p);
        // br_table -> vec(labelidx) + default
        case 0x0E: {
            uint64_t count = 0;
            if (!ReadU(d, n, p, count))
                return false;
            for (uint64_t i = 0; i < count + 1; ++i)
                if (!SkipLEB(d, n, p))
                    return false;
            return true;
        }
        // select t -> vec(valtype)
        case 0x1C: {
            uint64_t count = 0;
            if (!ReadU(d, n, p, count))
                return false;
            if (p + count > n)
                return false;
            p += count;
            return true;
        }
        // ref.null -> reftype byte
        case 0xD0:
            if (p >= n)
                return false;
            ++p;
            return true;
        // memory.size / memory.grow -> 1 memidx byte
        case 0x3F:
        case 0x40:
            if (p >= n)
                return false;
            ++p;
            return true;
        // const
        case 0x41:  // i32.const (sleb)
        case 0x42:  // i64.const (sleb)
            return SkipLEB(d, n, p);
        case 0x43:  // f32.const
            if (p + 4 > n)
                return false;
            p += 4;
            return true;
        case 0x44:  // f64.const
            if (p + 8 > n)
                return false;
            p += 8;
            return true;
        // 0xFC prefix (bulk-memory / trunc_sat / table)
        case 0xFC: {
            uint64_t sub = 0;
            if (!ReadU(d, n, p, sub))
                return false;
            switch (sub) {
                case 0:
                case 1:
                case 2:
                case 3:
                case 4:
                case 5:
                case 6:
                case 7:  // trunc_sat: no immediate
                    return true;
                case 8:  // memory.init: dataidx + memidx byte
                    return SkipLEB(d, n, p) && (p < n ? (++p, true) : false);
                case 9:  // data.drop
                    return SkipLEB(d, n, p);
                case 10:  // memory.copy: 2 memidx bytes
                    if (p + 2 > n)
                        return false;
                    p += 2;
                    return true;
                case 11:  // memory.fill: 1 memidx byte
                    if (p >= n)
                        return false;
                    ++p;
                    return true;
                case 12:  // table.init
                case 14:  // table.copy
                    return SkipLEB(d, n, p) && SkipLEB(d, n, p);
                case 13:  // elem.drop
                case 15:  // table.grow
                case 16:  // table.size
                case 17:  // table.fill
                    return SkipLEB(d, n, p);
                default:
                    return false;
            }
        }
        default:
            // Loads/stores 0x28..0x3E carry a memarg (align uleb + offset uleb).
            if (op >= 0x28 && op <= 0x3E)
                return SkipLEB(d, n, p) && SkipLEB(d, n, p);
            // Comparisons / numeric / conversions / sign-extension: no immediate.
            if (op >= 0x45 && op <= 0xC4)
                return true;
            // 0xFD (SIMD) / 0xFE (atomics) / anything else: not understood -> fail-closed.
            return false;
    }
}

bool IsBoundary(uint8_t op) {
    // Opcodes that end a straight-line run for cost accounting / open a new metered block.
    return op == 0x02 || op == 0x03 || op == 0x04 || op == 0x05 || op == 0x0B || op == 0x0C || op == 0x0D ||
           op == 0x0E || op == 0x0F;
}

struct Injection {
    size_t offset;  // byte offset within the function body where the charge is spliced
    uint32_t cost;  // floored at 1
};

// Instrument one function body (the bytes after the body-size prefix: localdecls + instrs + end).
// Appends `i32.const cost; call gasFuncIdx` at function entry and every loop/if/else header.
bool InstrumentBody(const uint8_t* body, size_t len, uint64_t gasFuncIdx, std::vector<uint8_t>& out, std::string* err) {
    size_t p = 0;
    // local declarations: vec(count, valtype)
    uint64_t localGroups = 0;
    if (!ReadU(body, len, p, localGroups)) {
        if (err)
            *err = "bad local decl count";
        return false;
    }
    for (uint64_t i = 0; i < localGroups; ++i) {
        if (!SkipLEB(body, len, p) || p >= len) {  // count + 1 type byte
            if (err)
                *err = "bad local decl";
            return false;
        }
        ++p;  // valtype byte
    }
    const size_t localsEnd = p;

    // Walk instructions, collecting injection points (function entry + each block/loop/if/else head).
    std::vector<Injection> inj;
    bool open = true;
    size_t openStart = localsEnd;
    uint32_t count = 0;
    while (p < len) {
        const uint8_t op = body[p++];
        if (!SkipImmediates(op, body, len, p)) {
            if (err)
                *err = "unsupported/blocked opcode in guest body";
            return false;
        }
        if (IsBoundary(op)) {
            if (open)
                inj.push_back({openStart, count < 1 ? 1u : count});
            open = false;
            if (op == 0x02 || op == 0x03 || op == 0x04 || op == 0x05) {
                openStart = p;  // body of block/loop/if, or else-arm: charge its head
                count = 0;
                open = true;
            }
        } else if (open) {
            ++count;
        }
    }
    if (open)
        inj.push_back({openStart, count < 1 ? 1u : count});

    // Rebuild the body, splicing the charge sequence at each injection offset (offsets ascending).
    out.clear();
    out.reserve(len + inj.size() * 4);
    size_t cursor = 0;
    for (const Injection& in : inj) {
        out.insert(out.end(), body + cursor, body + in.offset);
        out.push_back(0x41);  // i32.const
        WriteS(out, static_cast<int64_t>(in.cost));
        out.push_back(0x10);  // call
        WriteU(out, gasFuncIdx);
        cursor = in.offset;
    }
    out.insert(out.end(), body + cursor, body + len);
    return true;
}

// A located section in the original module.
struct Section {
    uint8_t id = 0;
    size_t headerStart = 0;  // the id byte
    size_t payloadStart = 0;
    size_t payloadEnd = 0;
};

// Emit a rebuilt section: id + uleb(payload size) + payload.
void EmitSection(std::vector<uint8_t>& out, uint8_t id, const std::vector<uint8_t>& payload) {
    out.push_back(id);
    WriteU(out, payload.size());
    out.insert(out.end(), payload.begin(), payload.end());
}

}  // namespace

bool InstrumentWasmForFuel(const std::vector<uint8_t>& in, std::vector<uint8_t>& out, std::string* error) {
    auto fail = [&](const char* m) {
        if (error)
            *error = m;
        return false;
    };
    const uint8_t* d = in.data();
    const size_t n = in.size();
    if (n < 8 || d[0] != 0x00 || d[1] != 0x61 || d[2] != 0x73 || d[3] != 0x6D)
        return fail("not a wasm module");

    // 1. Scan sections.
    std::vector<Section> sections;
    size_t p = 8;
    while (p < n) {
        Section s;
        s.headerStart = p;
        s.id = d[p++];
        uint64_t size = 0;
        if (!ReadU(d, n, p, size))
            return fail("bad section size");
        s.payloadStart = p;
        s.payloadEnd = p + size;
        if (s.payloadEnd > n)
            return fail("section overruns module");
        sections.push_back(s);
        p = s.payloadEnd;
    }

    const Section* typeSec = nullptr;
    const Section* importSec = nullptr;
    const Section* funcSec = nullptr;
    const Section* globalSec = nullptr;
    const Section* exportSec = nullptr;
    const Section* codeSec = nullptr;
    for (const Section& s : sections) {
        switch (s.id) {
            case 1:
                typeSec = &s;
                break;
            case 2:
                importSec = &s;
                break;
            case 3:
                funcSec = &s;
                break;
            case 6:
                globalSec = &s;
                break;
            case 7:
                exportSec = &s;
                break;
            case 10:
                codeSec = &s;
                break;
            default:
                break;
        }
    }
    if (!typeSec || !funcSec || !globalSec || !exportSec || !codeSec)
        return fail("module missing a section required for fuel metering (type/function/global/export/code)");

    // Helper: read the leading vec count of a section payload, return count + offset of entries.
    auto sectionCount = [&](const Section& s, uint64_t& count, size_t& entriesStart) -> bool {
        size_t q = s.payloadStart;
        if (!ReadU(d, n, q, count))
            return false;
        entriesStart = q;
        return true;
    };

    uint64_t numTypes = 0, numDefinedFuncs = 0, numDefinedGlobals = 0, numExports = 0, numCodeBodies = 0;
    size_t typeEntries = 0, funcEntries = 0, globalEntries = 0, exportEntries = 0, codeEntries = 0;
    if (!sectionCount(*typeSec, numTypes, typeEntries) || !sectionCount(*funcSec, numDefinedFuncs, funcEntries) ||
        !sectionCount(*globalSec, numDefinedGlobals, globalEntries) ||
        !sectionCount(*exportSec, numExports, exportEntries) || !sectionCount(*codeSec, numCodeBodies, codeEntries))
        return fail("bad section vector count");
    if (numCodeBodies != numDefinedFuncs)
        return fail("function/code count mismatch in input");

    // 2. Count imported funcs/globals (they precede defined ones in the index space).
    uint64_t numImportedFuncs = 0, numImportedGlobals = 0;
    if (importSec) {
        size_t q = importSec->payloadStart;
        uint64_t importCount = 0;
        if (!ReadU(d, n, q, importCount))
            return fail("bad import count");
        for (uint64_t i = 0; i < importCount; ++i) {
            uint64_t nameLen = 0;
            if (!ReadU(d, n, q, nameLen) || q + nameLen > n)
                return fail("bad import module name");
            q += nameLen;
            if (!ReadU(d, n, q, nameLen) || q + nameLen > n)
                return fail("bad import field name");
            q += nameLen;
            if (q >= n)
                return fail("bad import kind");
            const uint8_t kind = d[q++];
            if (kind == 0x00) {  // func: typeidx
                ++numImportedFuncs;
                if (!SkipLEB(d, n, q))
                    return fail("bad import func");
            } else if (kind == 0x01) {  // table: reftype + limits
                q++;                    // reftype
                if (q >= n)
                    return fail("bad import table");
                const uint8_t flag = d[q++];
                if (!SkipLEB(d, n, q))
                    return fail("bad table limits min");
                if ((flag & 1) && !SkipLEB(d, n, q))
                    return fail("bad table limits max");
            } else if (kind == 0x02) {  // mem: limits
                if (q >= n)
                    return fail("bad import mem");
                const uint8_t flag = d[q++];
                if (!SkipLEB(d, n, q))
                    return fail("bad mem limits min");
                if ((flag & 1) && !SkipLEB(d, n, q))
                    return fail("bad mem limits max");
            } else if (kind == 0x03) {  // global: valtype + mut
                ++numImportedGlobals;
                if (q + 2 > n)
                    return fail("bad import global");
                q += 2;
            } else {
                return fail("unknown import kind");
            }
        }
    }

    const uint64_t gasTypeIndex = numTypes;
    const uint64_t gasFuncIdx = numImportedFuncs + numDefinedFuncs;
    const uint64_t fuelGlobalIdx = numImportedGlobals + numDefinedGlobals;

    // 3. Build the new payloads for the edited sections.
    // type: append (i32)->()  ==  0x60 0x01 0x7f 0x00
    std::vector<uint8_t> typePayload;
    WriteU(typePayload, numTypes + 1);
    typePayload.insert(typePayload.end(), d + typeEntries, d + typeSec->payloadEnd);
    typePayload.insert(typePayload.end(), {0x60, 0x01, 0x7F, 0x00});

    // function: append the gas function's type index
    std::vector<uint8_t> funcPayload;
    WriteU(funcPayload, numDefinedFuncs + 1);
    funcPayload.insert(funcPayload.end(), d + funcEntries, d + funcSec->payloadEnd);
    WriteU(funcPayload, gasTypeIndex);

    // global: append a mutable i64 fuel global, init i64.const 0  ==  0x7e 0x01 0x42 0x00 0x0b
    std::vector<uint8_t> globalPayload;
    WriteU(globalPayload, numDefinedGlobals + 1);
    globalPayload.insert(globalPayload.end(), d + globalEntries, d + globalSec->payloadEnd);
    globalPayload.insert(globalPayload.end(), {0x7E, 0x01, 0x42, 0x00, 0x0B});

    // export: append  "__fuel" (kind 0x03 global) -> fuelGlobalIdx
    std::vector<uint8_t> exportPayload;
    WriteU(exportPayload, numExports + 1);
    exportPayload.insert(exportPayload.end(), d + exportEntries, d + exportSec->payloadEnd);
    const char* fname = kFuelGlobalName;
    const size_t fnameLen = std::strlen(fname);
    WriteU(exportPayload, fnameLen);
    exportPayload.insert(exportPayload.end(), fname, fname + fnameLen);
    exportPayload.push_back(0x03);  // global
    WriteU(exportPayload, fuelGlobalIdx);

    // code: instrument each existing body, then append the __gas body.
    std::vector<uint8_t> codePayload;
    WriteU(codePayload, numCodeBodies + 1);
    {
        size_t q = codeEntries;
        for (uint64_t i = 0; i < numCodeBodies; ++i) {
            uint64_t bodySize = 0;
            if (!ReadU(d, n, q, bodySize))
                return fail("bad code body size");
            if (q + bodySize > codeSec->payloadEnd)
                return fail("code body overruns section");
            std::vector<uint8_t> newBody;
            if (!InstrumentBody(d + q, bodySize, gasFuncIdx, newBody, error))
                return false;
            WriteU(codePayload, newBody.size());
            codePayload.insert(codePayload.end(), newBody.begin(), newBody.end());
            q += bodySize;
        }
    }
    // __gas(i32 cost): fuel -= (i64)cost; if (fuel < 0) unreachable
    {
        std::vector<uint8_t> body;
        body.push_back(0x00);  // 0 local groups
        body.push_back(0x23);
        WriteU(body, fuelGlobalIdx);            // global.get fuel
        body.insert(body.end(), {0x20, 0x00});  // local.get 0 (cost)
        body.push_back(0xAC);                   // i64.extend_i32_s
        body.push_back(0x7D);                   // i64.sub
        body.push_back(0x24);
        WriteU(body, fuelGlobalIdx);  // global.set fuel
        body.push_back(0x23);
        WriteU(body, fuelGlobalIdx);            // global.get fuel
        body.insert(body.end(), {0x42, 0x00});  // i64.const 0
        body.push_back(0x53);                   // i64.lt_s
        body.insert(body.end(), {0x04, 0x40});  // if (void)
        body.push_back(0x00);                   // unreachable
        body.push_back(0x0B);                   // end if
        body.push_back(0x0B);                   // end func
        WriteU(codePayload, body.size());
        codePayload.insert(codePayload.end(), body.begin(), body.end());
    }

    // 4. Re-emit the module: edited sections rebuilt, everything else copied verbatim, order kept.
    out.clear();
    out.reserve(in.size() + codePayload.size() / 8 + 64);
    out.insert(out.end(), d, d + 8);  // magic + version
    for (const Section& s : sections) {
        switch (s.id) {
            case 1:
                EmitSection(out, 1, typePayload);
                break;
            case 3:
                EmitSection(out, 3, funcPayload);
                break;
            case 6:
                EmitSection(out, 6, globalPayload);
                break;
            case 7:
                EmitSection(out, 7, exportPayload);
                break;
            case 10:
                EmitSection(out, 10, codePayload);
                break;
            default:
                out.insert(out.end(), d + s.headerStart, d + s.payloadEnd);  // verbatim
                break;
        }
    }
    return true;
}

}  // namespace Next::sandbox
