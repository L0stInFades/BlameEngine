#pragma once

#include <cstdint>
#include <vector>

#include "next/vegetation/vegetation_def.h"

namespace Next::vegetation {

// Deterministic per-cell scatter (ADR-0014). Given a VegetationDef + a terrain sampler, places
// instances inside the world-space square cell [cellX*cellSize, (cellX+1)*cellSize) x (same in z).
// The seed derives ONLY from def.masterSeed + the world cell coordinate + species + grid node, so a
// cell scatters identically no matter when it streams in (order-independent). Same build + same
// (def, cell, cellSize) => byte-identical output.
//
// Algorithm, per species: a jittered grid sized from density (step = max(minSpacing, 1/sqrt(density)),
// retiled to divide the cell evenly so neighbouring cells share no seam). Each node, with its own
// deterministic stream, jitters one candidate within its tile, which is then rejected if it fails the
// altitude / slope / mask filters or falls within minSpacing of an already-accepted same-species
// instance (a 3x3 hash-grid neighbour check). Density is governed by the tile size; the min-spacing
// reject is what breaks up the grid into blue-noise. Output is capped at def.maxInstancesPerCell.
//
// PRECONDITION: callers should pass a def that VegetationValidator accepted. ScatterCell is itself
// defensive (skips non-positive-density species, guards cellSize), but does not re-validate.
std::vector<VegetationInstance> ScatterCell(const VegetationDef& def, const ITerrainSampler& terrain, int32_t cellX,
                                            int32_t cellZ, float cellSize);

}  // namespace Next::vegetation
