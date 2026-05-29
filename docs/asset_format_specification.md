# Asset Format Specification (CP3)

## Overview

This document defines the intermediate asset formats for CP3 implementation. All runtime assets are compiled from source formats (FBX, PNG, etc.) into these binary formats for efficient streaming and loading.

## Common Header Structure

All asset files share a common header:

```
struct AssetHeader {
    uint32_t magic;           // 'NEXT' (0x4E455854)
    uint32_t version;         // Format version (starting at 1)
    uint32_t assetType;       // Asset type enum
    uint32_t dataSize;        // Size of data block in bytes
    uint64_t checksum;        // CRC64 checksum of data block
    char name[64];            // Null-terminated asset name
};
```

## Asset Types

### 1. Mesh Asset (Type = 1)

```
struct MeshHeader {
    AssetHeader common;
    
    uint32_t vertexCount;
    uint32_t indexCount;
    uint32_t vertexStride;    // Size of one vertex in bytes
    uint32_t indexType;       // 0 = uint16, 1 = uint32
    uint32_t vertexFormat;    // Vertex layout format ID
    uint32_t boundingBox[6];  // minX, minY, minZ, maxX, maxY, maxZ (float)
    uint32_t materialCount;   // Number of submeshes/materials
    uint32_t flags;           // Mesh flags (hasNormals, hasUVs, etc.)
};

// Data block layout:
// 1. Vertex data (vertexCount * vertexStride bytes)
// 2. Index data (indexCount * (2 or 4) bytes)
// 3. Submesh ranges (materialCount * 2 * uint32: start index, index count)
```

### 2. Texture Asset (Type = 2)

```
struct TextureHeader {
    AssetHeader common;
    
    uint32_t width;
    uint32_t height;
    uint32_t depth;           // For 3D textures
    uint32_t mipLevels;
    uint32_t arraySize;       // For texture arrays
    uint32_t format;          // DXGI_FORMAT enum
    uint32_t flags;           // Texture flags (sRGB, cubemap, etc.)
};

// Data block layout:
// Mip levels stored from largest to smallest
// For each mip level: dataSize = width * height * depth * bytesPerPixel
// Array slices stored sequentially
```

### 3. Material Asset (Type = 3)

```
struct MaterialHeader {
    AssetHeader common;
    
    uint32_t textureCount;    // Number of texture references
    uint32_t parameterCount;  // Number of material parameters
    uint32_t shaderID;        // Shader identifier
    uint32_t flags;           // Material flags (blend mode, etc.)
};

// Data block layout:
// 1. Texture references (textureCount * TextureRef)
// 2. Material parameters (parameterCount * MaterialParam)
// 3. Shader constants (variable size)

struct TextureRef {
    char name[64];           // Texture asset name
    uint32_t slot;           // Texture slot index
    uint32_t type;           // Texture type (albedo, normal, etc.)
};

struct MaterialParam {
    char name[32];           // Parameter name
    uint32_t type;           // Parameter type (float, vec3, etc.)
    float value[4];          // Parameter value
};
```

## Package Container Format

Packages are used to bundle multiple assets for efficient streaming:

```
struct PackageHeader {
    uint32_t magic;           // 'NPKG' (0x4E504B47)
    uint32_t version;         // Package format version (currently 2)
    uint32_t assetCount;      // Number of assets in package
    uint32_t indexOffset;     // Offset to asset index table
    uint32_t dataOffset;      // Offset to asset data section
    uint64_t checksum;        // CRC64 of everything after the PackageHeader
    char name[64];            // Package name
};

struct AssetEntry {
    uint32_t assetType;
    uint32_t assetSize;            // bytes physically stored at dataOffset (post-compression)
    uint32_t dataOffset;           // relative to data section start
    uint32_t compressionAlgorithm; // 0 = None, 1 = Zstd, 2 = LZ4 (Next::Compression::Algorithm)
    uint32_t compressedSize;       // == assetSize when compressed, 0 when uncompressed
    uint32_t decompressedSize;     // logical blob size; == assetSize when uncompressed
    char name[64];                 // Asset name
};

// Package layout:
// 1. PackageHeader
// 2. Asset index table (starting at indexOffset)
// 3. Asset data section (starting at dataOffset)
//
// Size-field invariants:
//  - Entries chain: entry[i+1].dataOffset == entry[i].dataOffset + entry[i].assetSize.
//  - Uncompressed: compressionAlgorithm == 0, compressedSize == 0, decompressedSize == assetSize.
//  - Compressed:   compressionAlgorithm != 0, compressedSize == assetSize, decompressedSize is
//                  the size after decompression. The loader decompresses transparently on read.
```

### Per-Entry Compression

The asset compiler (`next_assetc`) compresses each blob independently. `CreatePackage` takes a
mode: `auto` (default — try LZ4, keep the compressed bytes only if strictly smaller), `none`,
`lz4`, or `zstd`. The runtime `PackageContainer::ReadAssetData` decompresses transparently using
`compressionAlgorithm`; callers always receive the full logical blob. If a package declares a codec
that is unavailable in the current build, the loader rejects it.

### Package Manifest Sidecar

Alongside each `<name>.npkg`, the compiler emits a queryable `<name>.npkg.manifest.json` describing
the package: per-asset `id`, `type`, `compression`, `dataOffset`, `storedBytes`, `decompressedBytes`,
and forward `dependencies` (e.g. a material's referenced textures), plus package totals and
`warnings` (currently: a referenced asset not present in the package — a soft warning, since it may
be provided by another package loaded at runtime). The per-asset `id` is `CRC64("<package-stem>::<name>")`,
which is exactly the stable id the runtime `AssetManager` assigns — so the manifest bridges offline
cook identity and runtime handle identity.

## Versioning and Migration

All formats include a version number. Migration scripts should be provided for each version bump. Format changes should be backward compatible when possible.

**Package format v1 → v2:** `AssetEntry` gained `compressionAlgorithm` (changing the index stride) and entries may now be LZ4/Zstd compressed. The loader rejects v1 packages; the migration path is to re-cook from source (`next_assetc test <dir>` or `next_assetc package …`). This follows the "binary cache is regenerable from human-readable source" principle — the `.npkg` is a build artifact, not a source of truth.

## Validation

Each asset should pass validation:
1. Magic number matches
2. Checksum validates
3. Data size matches header
4. For meshes: vertex/index counts are reasonable
5. For textures: dimensions are power-of-two (not required but recommended)
6. For materials: texture references exist in the same package

## Example Assets for CP3

For CP3 testing, we need:
1. A simple cube mesh (8 vertices, 36 indices)
2. A 256x256 checkerboard texture
3. A simple PBR material referencing the texture
4. A package containing all three assets
