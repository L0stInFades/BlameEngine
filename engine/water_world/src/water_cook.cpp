#include "next/water_world/water_cook.h"

#include <algorithm>
#include <fstream>
#include <sstream>

#include "next/streaming/layered_cell_file.h"
#include "next/water/water_cell.h"
#include "next/water/water_spectrum.h"

namespace Next::water {

WaterBodyInstance BakeBody(const WaterBodyDef& def, uint32_t bodyId) {
    WaterBodyInstance b;
    for (int i = 0; i < 3; ++i) {
        b.boundsMin[i] = def.boundsMin[i];
        b.boundsMax[i] = def.boundsMax[i];
        b.flowVelocity[i] = def.flowVelocity[i];
    }
    b.surfaceHeight = def.surfaceHeight;
    b.density = def.density;
    b.linearDrag = def.linearDrag;
    b.quadraticDrag = def.quadraticDrag;
    b.floodRate = def.floodRate;
    b.floodMaxHeight = def.floodMaxHeight;
    const uint8_t n = static_cast<uint8_t>(std::min<size_t>(def.waves.size(), kMaxWavesPerBody));
    for (uint8_t i = 0; i < n; ++i) {
        b.waves[i] = def.waves[i];
    }
    b.waveCount = n;
    b.bodyId = bodyId;
    b.visual = def.visual;
    b.flags = def.flags;
    b.type = static_cast<uint8_t>(def.type);
    return b;
}

WaterCookResult CookWaterCell(const WaterSceneDef& scene, int32_t cellX, int32_t cellZ, float cellSize,
                              Next::Streaming::CellFileCompression codec) {
    WaterCookResult result;

    result.report = WaterValidator::Validate(scene);
    if (!result.report.Ok()) {
        result.ok = false;
        return result;  // fail-closed: never cook an invalid scene
    }

    const float cellMinX = static_cast<float>(cellX) * cellSize;
    const float cellMaxX = cellMinX + cellSize;
    const float cellMinZ = static_cast<float>(cellZ) * cellSize;
    const float cellMaxZ = cellMinZ + cellSize;

    std::vector<WaterBodyInstance> bodies;
    for (size_t i = 0; i < scene.bodies.size(); ++i) {
        const WaterBodyDef& def = scene.bodies[i];
        const bool overlaps = def.boundsMin[0] <= cellMaxX && def.boundsMax[0] >= cellMinX &&
                              def.boundsMin[2] <= cellMaxZ && def.boundsMax[2] >= cellMinZ;
        if (overlaps) {
            bodies.push_back(BakeBody(def, static_cast<uint32_t>(i + 1)));  // 1-based stable id
        }
    }
    result.bodyCount = bodies.size();

    // Symmetric with UnpackCell's ceiling: never WRITE a per-cell blob the reader would refuse to load.
    // A dense (but scene-valid) layout can pile > kMaxWaterBodiesPerCell bodies into one cell; fail the
    // cook fail-closed here rather than emit an unreadable asset.
    if (bodies.size() > kMaxWaterBodiesPerCell) {
        result.ok = false;
        result.report.errors.push_back(
            WaterValidationError{WaterValidationCode::TooManyBodies, -1, -1,
                                 "per-cell body count exceeds kMaxWaterBodiesPerCell (reader would reject)"});
        result.bytes.clear();
        return result;
    }

    const std::vector<uint8_t> waterBlob = PackCell(cellX, cellZ, cellSize, bodies);

    Next::Streaming::LayeredCellChunkInput chunk;
    chunk.layer = Next::Streaming::CellLayer::Water;
    chunk.codec = codec;
    chunk.data = waterBlob;

    std::vector<Next::Streaming::LayeredCellChunkInput> chunks;
    chunks.push_back(std::move(chunk));
    result.bytes = Next::Streaming::PackLayeredCell(chunks);
    result.ok = true;
    return result;
}

bool WriteWaterCellFile(const std::string& path, const std::vector<uint8_t>& bytes) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }
    if (!bytes.empty()) {
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    return static_cast<bool>(file);
}

bool WriteWaterCellFileMerged(const std::string& path, const std::vector<uint8_t>& newLayeredCellBytes) {
    std::vector<uint8_t> existing;
    {
        std::ifstream in(path, std::ios::binary | std::ios::ate);
        if (in) {
            const std::streamoff n = in.tellg();
            if (n > 0) {
                existing.resize(static_cast<size_t>(n));
                in.seekg(0);
                in.read(reinterpret_cast<char*>(existing.data()), static_cast<std::streamsize>(n));
            }
        }
    }

    std::vector<Next::Streaming::LayeredCellChunk> existingChunks;
    if (existing.empty() || !Next::Streaming::ParseLayeredCell(existing.data(), existing.size(), existingChunks)) {
        return WriteWaterCellFile(path, newLayeredCellBytes);  // no prior layered cell -> write as-is
    }

    std::vector<Next::Streaming::LayeredCellChunk> newChunks;
    if (!Next::Streaming::ParseLayeredCell(newLayeredCellBytes.data(), newLayeredCellBytes.size(), newChunks)) {
        return false;  // freshly-cooked bytes should always parse
    }

    // Keep each existing chunk whose layer is NOT in the new set, then append the new chunks — so the
    // freshly-cooked layers REPLACE the old same-layer chunks while every other layer survives.
    std::vector<Next::Streaming::LayeredCellChunkInput> merged;
    for (const Next::Streaming::LayeredCellChunk& ec : existingChunks) {
        bool replaced = false;
        for (const Next::Streaming::LayeredCellChunk& nc : newChunks) {
            if (nc.layer == ec.layer) {
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            merged.push_back({ec.layer, ec.codec, ec.data});
        }
    }
    for (const Next::Streaming::LayeredCellChunk& nc : newChunks) {
        merged.push_back({nc.layer, nc.codec, nc.data});
    }
    return WriteWaterCellFile(path, Next::Streaming::PackLayeredCell(merged));
}

namespace {

bool ParseType(const std::string& s, WaterType& out) {
    if (s == "ocean") {
        out = WaterType::Ocean;
    } else if (s == "pool") {
        out = WaterType::Pool;
    } else if (s == "river") {
        out = WaterType::River;
    } else if (s == "flood") {
        out = WaterType::Flood;
    } else if (s == "lake") {
        out = WaterType::Lake;
    } else {
        return false;
    }
    return true;
}

uint16_t ParseFlagList(const std::string& csv) {
    uint16_t flags = WaterNone;
    std::string token;
    std::istringstream ss(csv);
    while (std::getline(ss, token, ',')) {
        if (token == "buoyant") {
            flags |= WaterBuoyant;
        } else if (token == "conductive") {
            flags |= WaterConductive;
        } else if (token == "lethal") {
            flags |= WaterLethal;
        } else if (token == "current") {
            flags |= WaterCurrent;
        } else if (token == "breaksSight") {
            flags |= WaterBreaksSight;
        } else if (token == "extinguishes") {
            flags |= WaterExtinguishes;
        }
    }
    return flags;
}

}  // namespace

bool ParseWaterDefText(const std::string& text, WaterSceneDef& outScene, std::string& outError) {
    outScene = WaterSceneDef{};
    outError.clear();

    std::istringstream stream(text);
    std::string line;
    int lineNo = 0;

    auto fail = [&outError, &lineNo](const std::string& msg) -> bool {
        outError = "line " + std::to_string(lineNo) + ": " + msg;
        return false;
    };

    while (std::getline(stream, line)) {
        ++lineNo;
        std::istringstream ls(line);
        std::string key;
        ls >> key;
        if (key.empty() || key[0] == '#') {
            continue;
        }

        if (key == "scene") {
            ls >> outScene.id;
            if (outScene.id.empty()) {
                return fail("scene requires an id");
            }
        } else if (key == "name") {
            std::string rest;
            std::getline(ls, rest);
            const size_t start = rest.find_first_not_of(" \t");
            outScene.name = (start == std::string::npos) ? std::string() : rest.substr(start);
        } else if (key == "body") {
            std::string id;
            std::string typeStr;
            if (!(ls >> id >> typeStr)) {
                return fail("body requires <id> <type>");
            }
            WaterType type = WaterType::Pool;
            if (!ParseType(typeStr, type)) {
                return fail("unknown water type '" + typeStr + "'");
            }
            WaterBodyDef body;
            body.id = id;
            body.type = type;
            outScene.bodies.push_back(std::move(body));
        } else {
            if (outScene.bodies.empty()) {
                return fail("'" + key + "' must follow a 'body' line");
            }
            WaterBodyDef& b = outScene.bodies.back();
            if (key == "bounds") {
                if (!(ls >> b.boundsMin[0] >> b.boundsMin[1] >> b.boundsMin[2] >> b.boundsMax[0] >> b.boundsMax[1] >>
                      b.boundsMax[2])) {
                    return fail("bounds requires minX minY minZ maxX maxY maxZ");
                }
            } else if (key == "surface") {
                if (!(ls >> b.surfaceHeight)) {
                    return fail("surface requires a float");
                }
            } else if (key == "density") {
                if (!(ls >> b.density)) {
                    return fail("density requires a float");
                }
            } else if (key == "flow") {
                if (!(ls >> b.flowVelocity[0] >> b.flowVelocity[1] >> b.flowVelocity[2])) {
                    return fail("flow requires vx vy vz");
                }
                b.flags |= WaterCurrent;
            } else if (key == "drag") {
                if (!(ls >> b.linearDrag >> b.quadraticDrag)) {
                    return fail("drag requires <linear> <quadratic>");
                }
            } else if (key == "flood") {
                if (!(ls >> b.floodRate >> b.floodMaxHeight)) {
                    return fail("flood requires <rate> <maxHeight>");
                }
            } else if (key == "visual") {
                unsigned long v = 0;
                if (!(ls >> v)) {
                    return fail("visual requires a uint32");
                }
                b.visual = static_cast<uint32_t>(v);
            } else if (key == "flags") {
                std::string csv;
                ls >> csv;
                b.flags = ParseFlagList(csv);
            } else if (key == "wave") {
                float amp = 0.0f;
                float wavelength = 0.0f;
                float dirX = 0.0f;
                float dirZ = 0.0f;
                float speed = 0.0f;
                float steep = 0.0f;
                if (!(ls >> amp >> wavelength >> dirX >> dirZ >> speed >> steep)) {
                    return fail("wave requires amp wavelength dirX dirZ speed steepness");
                }
                WaveComponent w;
                w.amplitude = amp;
                w.wavelength = wavelength;
                const float len = std::sqrt((dirX * dirX) + (dirZ * dirZ));
                w.direction[0] = (len > 1e-8f) ? dirX / len : 1.0f;
                w.direction[1] = (len > 1e-8f) ? dirZ / len : 0.0f;
                w.speed = speed;
                w.steepness = steep;
                b.waves.push_back(w);
            } else if (key == "ocean") {
                OceanSpectrumParams p;
                int count = 4;
                if (!(ls >> p.windDir[0] >> p.windDir[1] >> p.windSpeed >> p.medianWavelength >> count)) {
                    return fail("ocean requires windDirX windDirZ windSpeed medianWavelength count");
                }
                const std::vector<WaveComponent> waves = GenerateOceanWaves(p, static_cast<uint8_t>(count));
                for (const WaveComponent& w : waves) {
                    b.waves.push_back(w);
                }
            } else {
                return fail("unknown key '" + key + "'");
            }
        }
    }
    return true;
}

}  // namespace Next::water
