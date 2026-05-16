#include "obj_loader.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>
#include <string_view>
#include <unordered_map>

namespace Next {

namespace {

static inline void LTrim(std::string_view& s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n')) {
        s.remove_prefix(1);
    }
}

static inline void RTrim(std::string_view& s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) {
        s.remove_suffix(1);
    }
}

static inline std::string_view Trim(std::string_view s) {
    LTrim(s);
    RTrim(s);
    return s;
}

struct Vec2 {
    float x, y;
};
struct Vec3 {
    float x, y, z;
};

static inline Vec3 Sub(const Vec3& a, const Vec3& b) {
    return Vec3{a.x - b.x, a.y - b.y, a.z - b.z};
}
static inline Vec3 Cross(const Vec3& a, const Vec3& b) {
    return Vec3{
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}
static inline float Dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline Vec3 Add(const Vec3& a, const Vec3& b) { return Vec3{a.x + b.x, a.y + b.y, a.z + b.z}; }
static inline Vec3 Mul(const Vec3& a, float s) { return Vec3{a.x * s, a.y * s, a.z * s}; }
static inline float Len(const Vec3& a) { return std::sqrt(std::max(0.0f, Dot(a, a))); }
static inline Vec3 Normalize(const Vec3& a) {
    const float l = Len(a);
    if (l <= 1e-20f) return Vec3{0.0f, 1.0f, 0.0f};
    return Mul(a, 1.0f / l);
}

static inline bool IsFinite(const Vec2& v) {
    return std::isfinite(v.x) && std::isfinite(v.y);
}

static inline bool IsFinite(const Vec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

static bool ParseVec2(std::string_view text, Vec2& out) {
    std::istringstream iss{std::string(text)};
    return static_cast<bool>(iss >> out.x >> out.y) && IsFinite(out);
}

static bool ParseVec3(std::string_view text, Vec3& out) {
    std::istringstream iss{std::string(text)};
    return static_cast<bool>(iss >> out.x >> out.y >> out.z) && IsFinite(out);
}

static inline bool StartsWith(std::string_view s, std::string_view prefix) {
    return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix;
}

static inline int FixObjIndex(int idx, int count) {
    // OBJ indices are 1-based. Negative indices are relative to the end (-1 = last).
    if (idx > 0) return idx - 1;
    if (idx < 0) return count + idx;
    return -1;
}

struct VertexKey {
    int pos = -1;
    int uv = -1;
    int nrm = -1;
    bool operator==(const VertexKey& o) const { return pos == o.pos && uv == o.uv && nrm == o.nrm; }
};

struct VertexKeyHash {
    size_t operator()(const VertexKey& k) const noexcept {
        // Cheap hash combine; fine for tool usage.
        size_t h = 1469598103934665603ull;
        auto mix = [&](int v) {
            h ^= static_cast<size_t>(v + 0x9e3779b9);
            h *= 1099511628211ull;
        };
        mix(k.pos);
        mix(k.uv);
        mix(k.nrm);
        return h;
    }
};

static bool ParseFaceVertex(std::string_view token, int posCount, int uvCount, int nrmCount, VertexKey& out) {
    // token formats: v | v/vt | v//vn | v/vt/vn
    out = VertexKey{};
    int a = 0, b = 0, c = 0;
    bool hasB = false, hasC = false;

    auto parseInt = [](std::string_view s, int& outInt) -> bool {
        if (s.empty()) return false;
        int sign = 1;
        size_t i = 0;
        if (s[0] == '-') {
            sign = -1;
            i = 1;
        }
        if (i == s.size()) return false;

        int v = 0;
        for (; i < s.size(); ++i) {
            char ch = s[i];
            if (ch < '0' || ch > '9') return false;
            const int digit = ch - '0';
            if (v > (std::numeric_limits<int>::max() - digit) / 10) {
                return false;
            }
            v = v * 10 + digit;
        }
        outInt = v * sign;
        return true;
    };

    const size_t s1 = token.find('/');
    if (s1 == std::string_view::npos) {
        if (!parseInt(token, a)) return false;
    } else {
        if (!parseInt(token.substr(0, s1), a)) return false;
        const size_t s2 = token.find('/', s1 + 1);
        if (s2 == std::string_view::npos) {
            // v/vt
            hasB = true;
            if (!parseInt(token.substr(s1 + 1), b)) return false;
        } else {
            // v/vt/vn or v//vn
            if (s2 > s1 + 1) {
                hasB = true;
                if (!parseInt(token.substr(s1 + 1, s2 - (s1 + 1)), b)) return false;
            } else {
                hasB = false;
            }
            hasC = true;
            if (!parseInt(token.substr(s2 + 1), c)) return false;
        }
    }

    out.pos = FixObjIndex(a, posCount);
    out.uv = hasB ? FixObjIndex(b, uvCount) : -1;
    out.nrm = hasC ? FixObjIndex(c, nrmCount) : -1;
    if (out.pos < 0 || out.pos >= posCount) return false;
    if (hasB && (out.uv < 0 || out.uv >= uvCount)) return false;
    if (hasC && (out.nrm < 0 || out.nrm >= nrmCount)) return false;
    return true;
}

} // namespace

bool LoadObjMesh(const std::string& path, ObjMesh& out, std::string& outError) {
    out = ObjMesh{};
    outError.clear();

    std::ifstream in(path);
    if (!in.is_open()) {
        outError = "Failed to open OBJ: " + path;
        return false;
    }

    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<Vec2> uvs;

    // For normal generation (per position).
    std::vector<Vec3> accumNormal;
    std::vector<uint32_t> accumCount;

    std::string line;
    line.reserve(4096);

    // First pass: parse and build triangles.
    std::unordered_map<VertexKey, uint32_t, VertexKeyHash> remap;
    remap.reserve(1 << 16);

    std::vector<VertexKey> face;
    face.reserve(8);

    auto emitVertex = [&](const VertexKey& key) -> uint32_t {
        auto it = remap.find(key);
        if (it != remap.end()) {
            return it->second;
        }

        ObjMeshVertex v{};
        const Vec3 p = positions[static_cast<size_t>(key.pos)];
        v.position[0] = p.x;
        v.position[1] = p.y;
        v.position[2] = p.z;

        if (key.uv >= 0 && key.uv < static_cast<int>(uvs.size())) {
            const Vec2 t = uvs[static_cast<size_t>(key.uv)];
            v.uv[0] = t.x;
            v.uv[1] = t.y;
            out.hasUVs = true;
        } else {
            v.uv[0] = 0.0f;
            v.uv[1] = 0.0f;
        }

        if (key.nrm >= 0 && key.nrm < static_cast<int>(normals.size())) {
            const Vec3 n = normals[static_cast<size_t>(key.nrm)];
            v.normal[0] = n.x;
            v.normal[1] = n.y;
            v.normal[2] = n.z;
            out.hasNormals = true;
        } else {
            v.normal[0] = 0.0f;
            v.normal[1] = 1.0f;
            v.normal[2] = 0.0f;
        }

        const uint32_t idx = static_cast<uint32_t>(out.vertices.size());
        out.vertices.push_back(v);
        remap.emplace(key, idx);
        return idx;
    };

    auto expandBounds = [&](const Vec3& p) {
        if (out.vertices.size() == 1) {
            out.boundsMin[0] = out.boundsMax[0] = p.x;
            out.boundsMin[1] = out.boundsMax[1] = p.y;
            out.boundsMin[2] = out.boundsMax[2] = p.z;
        } else {
            out.boundsMin[0] = std::min(out.boundsMin[0], p.x);
            out.boundsMin[1] = std::min(out.boundsMin[1], p.y);
            out.boundsMin[2] = std::min(out.boundsMin[2], p.z);
            out.boundsMax[0] = std::max(out.boundsMax[0], p.x);
            out.boundsMax[1] = std::max(out.boundsMax[1], p.y);
            out.boundsMax[2] = std::max(out.boundsMax[2], p.z);
        }
    };

    // Parse.
    size_t lineNumber = 0;
    while (std::getline(in, line)) {
        ++lineNumber;
        std::string_view s = Trim(std::string_view(line));
        if (s.empty() || s.front() == '#') continue;

        if (StartsWith(s, "v ")) {
            Vec3 p{};
            if (!ParseVec3(s.substr(2), p)) {
                outError = "OBJ invalid vertex position: " + path + ":" + std::to_string(lineNumber);
                return false;
            }
            positions.push_back(p);
            continue;
        }

        if (StartsWith(s, "vt ")) {
            Vec2 t{};
            if (!ParseVec2(s.substr(3), t)) {
                outError = "OBJ invalid texture coordinate: " + path + ":" + std::to_string(lineNumber);
                return false;
            }
            // OBJ UV origin is typically bottom-left; keep as-is.
            uvs.push_back(t);
            continue;
        }

        if (StartsWith(s, "vn ")) {
            Vec3 n{};
            if (!ParseVec3(s.substr(3), n)) {
                outError = "OBJ invalid normal: " + path + ":" + std::to_string(lineNumber);
                return false;
            }
            normals.push_back(n);
            continue;
        }

        if (StartsWith(s, "f ")) {
            face.clear();
            std::istringstream iss(std::string(s.substr(2)));
            std::string tok;
            while (iss >> tok) {
                VertexKey key;
                if (!ParseFaceVertex(tok, static_cast<int>(positions.size()), static_cast<int>(uvs.size()),
                                     static_cast<int>(normals.size()), key)) {
                    outError = "OBJ invalid face vertex '" + tok + "': " + path + ":" + std::to_string(lineNumber);
                    return false;
                }
                face.push_back(key);
            }

            if (face.size() < 3) {
                outError = "OBJ face has fewer than three vertices: " + path + ":" + std::to_string(lineNumber);
                return false;
            }

            // Fan triangulation.
            for (size_t i = 1; i + 1 < face.size(); ++i) {
                const VertexKey k0 = face[0];
                const VertexKey k1 = face[i];
                const VertexKey k2 = face[i + 1];

                const uint32_t i0 = emitVertex(k0);
                const uint32_t i1 = emitVertex(k1);
                const uint32_t i2 = emitVertex(k2);

                out.indices.push_back(i0);
                out.indices.push_back(i1);
                out.indices.push_back(i2);

                // Expand bounds from positions (accurate even if vertices are merged).
                expandBounds(positions[static_cast<size_t>(k0.pos)]);
                expandBounds(positions[static_cast<size_t>(k1.pos)]);
                expandBounds(positions[static_cast<size_t>(k2.pos)]);
            }
            continue;
        }

        // Ignore: g/o/s/usemtl/mtllib/etc.
    }

    if (out.vertices.empty() || out.indices.empty()) {
        outError = "OBJ contained no triangles: " + path;
        return false;
    }

    // Generate normals if missing.
    if (!out.hasNormals) {
        accumNormal.assign(positions.size(), Vec3{0.0f, 0.0f, 0.0f});
        accumCount.assign(positions.size(), 0u);

        // Walk triangles; accumulate face normals per position index.
        // We need the original position index per vertex, but remap key is not stored.
        // Re-parse faces cheaply from the already-built vertex buffer by using closest match is not reliable.
        // Instead, compute smooth normals per unique vertex from its triangle adjacency.
        // This is acceptable for an importer tool and avoids carrying extra tables.
        std::vector<Vec3> vertAccum(out.vertices.size(), Vec3{0.0f, 0.0f, 0.0f});
        std::vector<uint32_t> vertCount(out.vertices.size(), 0u);

        for (size_t i = 0; i + 2 < out.indices.size(); i += 3) {
            const uint32_t ia = out.indices[i + 0];
            const uint32_t ib = out.indices[i + 1];
            const uint32_t ic = out.indices[i + 2];

            const ObjMeshVertex& va = out.vertices[ia];
            const ObjMeshVertex& vb = out.vertices[ib];
            const ObjMeshVertex& vc = out.vertices[ic];

            Vec3 pa{va.position[0], va.position[1], va.position[2]};
            Vec3 pb{vb.position[0], vb.position[1], vb.position[2]};
            Vec3 pc{vc.position[0], vc.position[1], vc.position[2]};

            Vec3 n = Cross(Sub(pb, pa), Sub(pc, pa));
            const float l = Len(n);
            if (l <= 1e-20f) continue;
            n = Mul(n, 1.0f / l);

            vertAccum[ia] = Add(vertAccum[ia], n);
            vertAccum[ib] = Add(vertAccum[ib], n);
            vertAccum[ic] = Add(vertAccum[ic], n);
            vertCount[ia]++;
            vertCount[ib]++;
            vertCount[ic]++;
        }

        for (size_t i = 0; i < out.vertices.size(); ++i) {
            Vec3 n = vertAccum[i];
            if (vertCount[i] != 0) {
                n = Normalize(n);
            } else {
                n = Vec3{0.0f, 1.0f, 0.0f};
            }
            out.vertices[i].normal[0] = n.x;
            out.vertices[i].normal[1] = n.y;
            out.vertices[i].normal[2] = n.z;
        }
        out.hasNormals = true;
    }

    return true;
}

} // namespace Next
