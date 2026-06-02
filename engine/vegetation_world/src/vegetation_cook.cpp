#include "next/vegetation_world/vegetation_cook.h"

#include <fstream>
#include <sstream>

#include "next/streaming/layered_cell_file.h"
#include "next/vegetation/vegetation_cell.h"
#include "next/vegetation/vegetation_scatter.h"

namespace Next::vegetation {

CookResult CookVegetationCell(const VegetationDef& def, const ITerrainSampler& terrain, int32_t cellX, int32_t cellZ,
                              float cellSize, Next::Streaming::CellFileCompression codec) {
    CookResult result;

    // Fail-closed: a malformed def is rejected here, never cooked into a cell.
    result.report = VegetationValidator::Validate(def);
    if (!result.report.Ok()) {
        result.ok = false;
        return result;
    }

    const std::vector<VegetationInstance> instances = ScatterCell(def, terrain, cellX, cellZ, cellSize);
    result.instanceCount = instances.size();

    const std::vector<uint8_t> vegBlob = PackCell(cellX, cellZ, cellSize, instances);

    Next::Streaming::LayeredCellChunkInput chunk;
    chunk.layer = Next::Streaming::CellLayer::Vegetation;
    chunk.codec = codec;
    chunk.data = vegBlob;

    std::vector<Next::Streaming::LayeredCellChunkInput> chunks;
    chunks.push_back(std::move(chunk));
    result.bytes = Next::Streaming::PackLayeredCell(chunks);
    result.ok = true;
    return result;
}

bool WriteCellFile(const std::string& path, const std::vector<uint8_t>& bytes) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }
    if (!bytes.empty()) {
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    return static_cast<bool>(file);
}

namespace {

uint16_t ParseFlagList(const std::string& csv) {
    uint16_t flags = VegNone;
    std::string token;
    std::istringstream ss(csv);
    while (std::getline(ss, token, ',')) {
        if (token == "blocksLOS" || token == "blocksLineOfSight") {
            flags = static_cast<uint16_t>(flags | VegBlocksLineOfSight);
        } else if (token == "destructible") {
            flags = static_cast<uint16_t>(flags | VegDestructible);
        } else if (token == "alignToSlope") {
            flags = static_cast<uint16_t>(flags | VegAlignToSlope);
        }
    }
    return flags;
}

}  // namespace

bool ParseVegetationDefText(const std::string& text, VegetationDef& outDef, std::string& outError) {
    outDef = VegetationDef{};
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

        if (key == "id") {
            ls >> outDef.id;
            if (outDef.id.empty()) {
                return fail("id requires a value");
            }
        } else if (key == "name") {
            std::string rest;
            std::getline(ls, rest);
            const size_t start = rest.find_first_not_of(" \t");
            outDef.name = (start == std::string::npos) ? std::string() : rest.substr(start);
        } else if (key == "seed") {
            unsigned long long v = 0;
            if (!(ls >> v)) {
                return fail("seed requires a uint64");
            }
            outDef.masterSeed = static_cast<uint64_t>(v);
        } else if (key == "maxInstancesPerCell") {
            unsigned long v = 0;
            if (!(ls >> v)) {
                return fail("maxInstancesPerCell requires a uint32");
            }
            outDef.maxInstancesPerCell = static_cast<uint32_t>(v);
        } else if (key == "species") {
            unsigned long visual = 0;
            if (!(ls >> visual)) {
                return fail("species requires a visual id");
            }
            VegetationSpecies sp;
            sp.id = static_cast<SpeciesId>(outDef.species.size() + 1);
            sp.visual = static_cast<VisualStateId>(visual);
            outDef.species.push_back(sp);
        } else {
            // Per-species keys target the last species declared.
            if (outDef.species.empty()) {
                return fail("'" + key + "' must follow a 'species' line");
            }
            VegetationSpecies& sp = outDef.species.back();
            if (key == "density") {
                if (!(ls >> sp.densityPerSqMeter)) {
                    return fail("density requires a float");
                }
            } else if (key == "spacing") {
                if (!(ls >> sp.minSpacing)) {
                    return fail("spacing requires a float");
                }
            } else if (key == "slope") {
                if (!(ls >> sp.minSlopeDegrees >> sp.maxSlopeDegrees)) {
                    return fail("slope requires <min> <max>");
                }
            } else if (key == "altitude") {
                if (!(ls >> sp.minAltitude >> sp.maxAltitude)) {
                    return fail("altitude requires <min> <max>");
                }
            } else if (key == "scale") {
                if (!(ls >> sp.minScale >> sp.maxScale)) {
                    return fail("scale requires <min> <max>");
                }
            } else if (key == "radius") {
                if (!(ls >> sp.logicalRadius)) {
                    return fail("radius requires a float");
                }
            } else if (key == "mask") {
                unsigned long m = 0;
                if (!(ls >> m)) {
                    return fail("mask requires a uint32");
                }
                sp.requiredMask = static_cast<uint32_t>(m);
            } else if (key == "flags") {
                std::string csv;
                ls >> csv;
                sp.flags = ParseFlagList(csv);
            } else {
                return fail("unknown key '" + key + "'");
            }
        }
    }
    return true;
}

}  // namespace Next::vegetation
