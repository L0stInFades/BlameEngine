#pragma once

#include "next/math/math.h"
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include <string>
#include <memory>
#include <functional>

namespace Next {
namespace Streaming {

// ===== Cell Coordinates =====

struct CellCoord {
    int32_t x, z;  // 2D grid coordinates (XZ plane)

    CellCoord() : x(0), z(0) {}
    CellCoord(int32_t x, int32_t z) : x(x), z(z) {}

    bool operator==(const CellCoord& other) const { return x == other.x && z == other.z; }

    bool operator!=(const CellCoord& other) const { return !(*this == other); }

    // Hash function for unordered_map
    struct Hash {
        size_t operator()(const CellCoord& coord) const {
            // Combine x and z into single hash
            return static_cast<size_t>(coord.x) * 73856093 ^ static_cast<size_t>(coord.z) * 19349663;
        }
    };

    // Distance squared (no sqrt for performance)
    int64_t DistanceSquared(const CellCoord& other) const {
        int64_t dx = static_cast<int64_t>(x) - static_cast<int64_t>(other.x);
        int64_t dz = static_cast<int64_t>(z) - static_cast<int64_t>(other.z);
        return dx * dx + dz * dz;
    }
};

// ===== Cell Content Layers =====

enum class CellLayer : uint32_t {
    Terrain = 0,     // 地形数据
    StaticMesh = 1,  // 静态网格
    Vegetation = 2,  // 植被
    Props = 3,       // 道具装饰
    NavMesh = 4,     // 导航网格
    Audio = 5,       // 音频
    HLOD = 6,        // 高层LOD
    Dynamic = 7,     // 动态对象
    Quest = 8,       // 任务点
    Collision = 9,   // 碰撞数据
    Max = 10
};

// ===== Cell Load State =====

enum class CellLoadState : uint8_t {
    Unloaded = 0,       // 未加载
    Queued = 1,         // 已排队等待加载
    Loading = 2,        // 正在加载（IO读取中）
    Decompressing = 3,  // 解压中
    Uploading = 4,      // GPU上传中
    Loaded = 5,         // 已加载
    Unloading = 6,      // 正在卸载
    Error = 7           // 加载失败
};

// ===== Cell Metadata =====

struct CellMetadata {
    CellCoord coord;
    Vec3 worldPosition;  // Center of cell in world space
    float cellSize;      // Size of cell in world units (e.g., 64 meters)
    uint32_t version;    // For incremental updates

    // Statistics
    uint32_t triangleCount;
    uint32_t entityCount;
    uint64_t dataSize;    // Compressed size on disk
    uint64_t memorySize;  // Uncompressed size in memory

    // Layers present in this cell
    uint32_t layerMask;  // Bitmask of CellLayer

    // LOD levels available
    uint32_t lodLevels;  // Number of LOD levels (0 = only HLOD)

    CellMetadata()
        : cellSize(64.0f),
          version(0),
          triangleCount(0),
          entityCount(0),
          dataSize(0),
          memorySize(0),
          layerMask(0),
          lodLevels(0) {}

    static constexpr uint32_t LayerBit(CellLayer layer) {
        const uint32_t layerIndex = static_cast<uint32_t>(layer);
        return layerIndex < static_cast<uint32_t>(CellLayer::Max) ? (1u << layerIndex) : 0u;
    }

    bool HasDiskData() const { return dataSize != 0; }
    bool HasMemoryData() const { return memorySize != 0; }
    bool HasGeometryStats() const { return triangleCount != 0 || entityCount != 0; }
    bool HasLodLevels() const { return lodLevels != 0; }
    bool HasAnyLayer() const { return layerMask != 0; }
    bool HasLayer(CellLayer layer) const { return (layerMask & LayerBit(layer)) != 0; }
    void SetLayerPresent(CellLayer layer, bool present = true) {
        const uint32_t bit = LayerBit(layer);
        if (present) {
            layerMask |= bit;
        } else {
            layerMask &= ~bit;
        }
    }
    size_t LayerCount() const {
        size_t count = 0;
        for (uint32_t bits = layerMask; bits != 0; bits &= bits - 1) {
            ++count;
        }
        return count;
    }
    float MemoryToDiskRatio() const {
        return dataSize == 0 ? 0.0f : static_cast<float>(memorySize) / static_cast<float>(dataSize);
    }
    float ClampedCellSize(float minimumSize = 1.0f) const { return cellSize < minimumSize ? minimumSize : cellSize; }
};

// ===== Cell Data =====

struct CellData {
    CellCoord coord;  // Coordinate of this cell
    CellMetadata metadata;
    CellLoadState state;

    // Layer data (each layer can be independently loaded/unloaded)
    struct LayerData {
        CellLayer layer;
        void* data;     // Raw decompressed data
        uint64_t size;  // Size in bytes
        CellLoadState state;
        uint64_t gpuResourceHandle;  // Handle to GPU resource (if applicable)
        uint64_t generation;         // bumped on every (re)load -> lets a consumer detect an in-place reload

        LayerData() : data(nullptr), size(0), state(CellLoadState::Unloaded), gpuResourceHandle(0), generation(0) {}

        bool IsLoaded() const { return state == CellLoadState::Loaded; }
        bool IsPending() const {
            return state == CellLoadState::Queued || state == CellLoadState::Loading ||
                   state == CellLoadState::Decompressing || state == CellLoadState::Uploading;
        }
        bool HasData() const { return data != nullptr; }
        bool HasSize() const { return size != 0; }
        bool HasGpuResource() const { return gpuResourceHandle != 0; }
    };

    std::unordered_map<CellLayer, LayerData> layers;

    // Dependencies (cells that should be loaded with this one)
    std::vector<CellCoord> dependencies;

    // Priority for loading (higher = more important)
    float priority;

    // Last access time (for eviction)
    uint64_t lastAccessFrame;

    // Async operation handle
    uint64_t asyncOperationHandle;
    bool isPlaceholderData;

    CellData()
        : state(CellLoadState::Unloaded),
          priority(0.0f),
          lastAccessFrame(0),
          asyncOperationHandle(0),
          isPlaceholderData(false) {}

    bool IsLayerLoaded(CellLayer layer) const {
        auto it = layers.find(layer);
        return it != layers.end() && it->second.state == CellLoadState::Loaded;
    }

    bool IsLoaded() const { return state == CellLoadState::Loaded; }
    bool IsPending() const {
        return state == CellLoadState::Queued || state == CellLoadState::Loading ||
               state == CellLoadState::Decompressing || state == CellLoadState::Uploading;
    }
    bool IsUnloading() const { return state == CellLoadState::Unloading; }
    bool IsError() const { return state == CellLoadState::Error; }
    bool IsPlaceholder() const { return isPlaceholderData; }
    bool HasLayers() const { return !layers.empty(); }
    bool HasDependencies() const { return !dependencies.empty(); }
    bool HasPriority() const { return priority != 0.0f; }
    bool HasAsyncOperation() const { return asyncOperationHandle != 0; }
    bool HasMemoryData() const { return metadata.HasMemoryData(); }
    bool HasDiskData() const { return metadata.HasDiskData(); }
    uint64_t MemorySize() const {
        // StaticMesh / placeholder bytes live in metadata.memorySize; other layers (e.g. Vegetation)
        // carry their bytes in their LayerData, so sum them here rather than mutating the shared scalar.
        uint64_t total = metadata.memorySize;
        for (const auto& entry : layers) {
            if (entry.first != CellLayer::StaticMesh) {
                total += entry.second.size;
            }
        }
        return total;
    }
    uint64_t DiskDataSize() const { return metadata.dataSize; }
    size_t LayerCount() const { return layers.size(); }
    size_t LoadedLayerCount() const {
        size_t count = 0;
        for (const auto& [layer, layerData] : layers) {
            (void)layer;
            if (layerData.IsLoaded()) {
                ++count;
            }
        }
        return count;
    }
    bool HasLoadedLayers() const { return LoadedLayerCount() != 0; }

    bool IsFullyLoaded() const {
        for (const auto& [layer, layerData] : layers) {
            if (!layerData.IsLoaded()) {
                return false;
            }
        }
        return !layers.empty();
    }
};

// ===== Region (Higher-level grouping) =====

struct RegionCoord {
    int32_t regionX, regionZ;                  // Region coordinates
    static constexpr int32_t REGION_SIZE = 8;  // Each region is 8x8 cells

    RegionCoord() : regionX(0), regionZ(0) {}
    RegionCoord(int32_t x, int32_t z) : regionX(x), regionZ(z) {}

    static RegionCoord FromCellCoord(const CellCoord& cell) {
        return RegionCoord(cell.x >= 0 ? cell.x / REGION_SIZE : (cell.x - REGION_SIZE + 1) / REGION_SIZE,
                           cell.z >= 0 ? cell.z / REGION_SIZE : (cell.z - REGION_SIZE + 1) / REGION_SIZE);
    }

    bool operator==(const RegionCoord& other) const { return regionX == other.regionX && regionZ == other.regionZ; }

    struct Hash {
        size_t operator()(const RegionCoord& coord) const {
            return static_cast<size_t>(coord.regionX) * 73856093 ^ static_cast<size_t>(coord.regionZ) * 19349663;
        }
    };
};

// ===== Region Metadata =====

struct RegionMetadata {
    RegionCoord coord;
    std::vector<CellCoord> cells;  // Cells in this region
    uint32_t version;

    // HLOD data (merged high-level LOD for entire region)
    uint64_t hlodDataSize;
    uint64_t hlodGPUHandle;

    // Statistics
    uint32_t totalTriangles;
    uint32_t totalEntities;

    RegionMetadata() : version(0), hlodDataSize(0), hlodGPUHandle(0), totalTriangles(0), totalEntities(0) {}
};

// ===== World Partition Configuration =====

struct WorldPartitionConfig {
    // Grid settings
    float cellSize = 64.0f;        // Size of each cell (meters)
    float loadRadius = 256.0f;     // Default load radius around camera
    float unloadRadius = 384.0f;   // Radius beyond which cells are unloaded
    float preloadRadius = 128.0f;  // Prefetch radius ahead of camera

    // Layer priority (for partial loading)
    std::vector<float> layerPriority;  // Priority multiplier per layer

    // Budget limits
    size_t maxLoadedCells = 256;  // Maximum cells loaded simultaneously
    size_t maxMemoryMB = 2048;    // Maximum memory for world data (MB)
    size_t maxPendingLoads = 16;  // Maximum concurrent load operations

    // Quality settings
    bool enableHLOD = true;       // Enable HLOD for distant cells
    uint32_t maxLODLevel = 4;     // Maximum LOD level to load
    bool enableStreaming = true;  // Enable streaming (or load all upfront)

    // Performance tuning
    bool prioritizeCameraDirection = true;  // Give higher priority to cells in front of camera
    float cameraDirectionWeight = 2.0f;     // Weight multiplier for forward direction

    WorldPartitionConfig() {
        // Initialize default layer priorities
        layerPriority.resize(static_cast<size_t>(CellLayer::Max));
        layerPriority[static_cast<size_t>(CellLayer::Terrain)] = 1.0f;
        layerPriority[static_cast<size_t>(CellLayer::StaticMesh)] = 0.9f;
        layerPriority[static_cast<size_t>(CellLayer::HLOD)] = 0.8f;
        layerPriority[static_cast<size_t>(CellLayer::NavMesh)] = 0.7f;
        layerPriority[static_cast<size_t>(CellLayer::Collision)] = 0.9f;
        layerPriority[static_cast<size_t>(CellLayer::Vegetation)] = 0.5f;
        layerPriority[static_cast<size_t>(CellLayer::Props)] = 0.4f;
        layerPriority[static_cast<size_t>(CellLayer::Audio)] = 0.3f;
        layerPriority[static_cast<size_t>(CellLayer::Dynamic)] = 0.6f;
        layerPriority[static_cast<size_t>(CellLayer::Quest)] = 0.2f;
    }
};

// ===== World Partition System =====

class WorldPartition {
public:
    WorldPartition();
    ~WorldPartition();

    // Initialize with configuration
    bool Initialize(const WorldPartitionConfig& config);

    // Update (called every frame)
    void Update(float deltaTime, const Vec3& cameraPosition, const Vec3& cameraDirection);

    // Cell management
    CellData* GetCell(const CellCoord& coord);
    const CellData* GetCell(const CellCoord& coord) const;
    bool IsCellLoaded(const CellCoord& coord) const;
    std::vector<CellCoord> GetLoadedCells() const;

    // Region management
    RegionMetadata* GetRegion(const RegionCoord& coord);
    const RegionMetadata* GetRegion(const RegionCoord& coord) const;

    // Coordinate conversion
    CellCoord WorldToCell(const Vec3& worldPosition) const;
    Vec3 CellToWorld(const CellCoord& coord) const;

    // Configuration
    void SetConfig(const WorldPartitionConfig& config) { config_ = config; }
    const WorldPartitionConfig& GetConfig() const { return config_; }

    // Cell load/unload requests (internal use by StreamingManager)
    void RequestCellLoad(const CellCoord& coord, float priority);
    void RequestCellUnload(const CellCoord& coord);
    void UpdateCellState(const CellCoord& coord, CellLoadState newState);

    // Statistics
    struct Statistics {
        size_t loadedCells = 0;
        size_t queuedCells = 0;
        size_t loadingCells = 0;
        size_t totalCells = 0;
        size_t memoryUsageMB = 0;
        float averageLoadTime = 0.0f;
        float averageUnloadTime = 0.0f;

        size_t PendingCellCount() const { return queuedCells + loadingCells; }
        size_t ActiveCellCount() const { return loadedCells + PendingCellCount(); }
        bool HasLoadedCells() const { return loadedCells != 0; }
        bool HasQueuedCells() const { return queuedCells != 0; }
        bool HasLoadingCells() const { return loadingCells != 0; }
        bool HasPendingCells() const { return PendingCellCount() != 0; }
        bool HasActiveCells() const { return ActiveCellCount() != 0; }
        bool HasTrackedCells() const { return totalCells != 0; }
        bool HasMemoryUsage() const { return memoryUsageMB != 0; }
        bool HasAverageLoadTime() const { return averageLoadTime != 0.0f; }
        bool HasAverageUnloadTime() const { return averageUnloadTime != 0.0f; }
        bool HasTiming() const { return HasAverageLoadTime() || HasAverageUnloadTime(); }
    };

    Statistics GetStatistics() const;

    // Cleanup
    void Shutdown();

private:
    // Core update logic
    void UpdateLoadQueue(const Vec3& cameraPosition, const Vec3& cameraDirection);
    void UpdateUnloadQueue(const Vec3& cameraPosition);
    void CalculatePriorities(const Vec3& cameraPosition, const Vec3& cameraDirection);

    // Cell state management
    CellData* CreateCell(const CellCoord& coord);
    void DestroyCell(const CellCoord& coord);
    bool ShouldLoadCell(const CellCoord& coord, const Vec3& cameraPosition, const Vec3& cameraDirection) const;
    bool ShouldUnloadCell(const CellCoord& coord, const Vec3& cameraPosition) const;

    // Region management
    RegionMetadata* CreateRegion(const RegionCoord& coord);

    // Configuration
    WorldPartitionConfig config_;

    // Storage
    std::unordered_map<CellCoord, std::unique_ptr<CellData>, CellCoord::Hash> cells_;
    std::unordered_map<RegionCoord, std::unique_ptr<RegionMetadata>, RegionCoord::Hash> regions_;

    // Load/unload queues
    struct LoadRequest {
        CellCoord coord;
        float priority;
        uint64_t requestFrame;

        bool operator<(const LoadRequest& other) const {
            return priority < other.priority;  // Lower value = higher priority (for min-heap)
        }
    };
    std::vector<LoadRequest> loadQueue_;
    std::vector<CellCoord> unloadQueue_;

    // Statistics
    Statistics stats_;
    uint64_t currentFrame_;
    bool initialized_;
};

}  // namespace Streaming
}  // namespace Next
