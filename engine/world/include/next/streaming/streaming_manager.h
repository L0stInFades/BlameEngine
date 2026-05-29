#pragma once

#include "next/streaming/world_partition.h"
#include "next/streaming/async_io.h"
#include "next/streaming/interest_manager.h"
#include "next/streaming/lod_system.h"
#include "next/streaming/eviction_policy.h"
#include "next/jobsystem/job.h"
#include <cstddef>
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <functional>
#include <string>
#include <mutex>

namespace Next {
namespace Streaming {

// ===== Streaming Handle =====

struct StreamingHandle {
    uint64_t id = 0;

    bool IsValid() const { return id != 0; }
    void Reset() { id = 0; }
    explicit operator bool() const { return IsValid(); }
    bool operator==(const StreamingHandle& other) const { return id == other.id; }
    bool operator!=(const StreamingHandle& other) const { return !(*this == other); }

    static StreamingHandle Invalid() { return {}; }
};

// ===== Asset Bundle =====

struct AssetBundle {
    uint64_t bundleId = 0;
    std::wstring bundlePath;
    std::vector<CellCoord> cells;  // Cells contained in this bundle
    uint64_t totalSize = 0;
    uint64_t compressedSize = 0;

    // Dependencies
    std::vector<uint64_t> dependencyBundles;

    bool IsValid() const { return bundleId != 0; }
    bool HasPath() const { return !bundlePath.empty(); }
    bool HasCells() const { return !cells.empty(); }
    bool HasDependencies() const { return !dependencyBundles.empty(); }
    bool HasSize() const { return totalSize != 0 || compressedSize != 0; }
    size_t CellCount() const { return cells.size(); }
    float CompressionRatio() const {
        return totalSize == 0 ? 0.0f : static_cast<float>(compressedSize) / static_cast<float>(totalSize);
    }
};

// ===== Streaming Statistics =====

struct StreamingStatistics {
    // Cell counts
    uint32_t loadedCells = 0;
    uint32_t loadingCells = 0;
    uint32_t queuedCells = 0;
    uint32_t unloadedCells = 0;

    // Memory usage
    uint64_t memoryUsed = 0;
    uint64_t memoryBudget = 0;
    float memoryUtilization = 0.0f;  // 0.0 - 1.0

    // Performance
    float averageLoadTime = 0.0f;
    float averageUnloadTime = 0.0f;
    uint32_t cellsLoadedPerSecond = 0;
    uint32_t cellsUnloadedPerSecond = 0;

    // Quality
    uint32_t visibleCells = 0;
    uint32_t highDetailCells = 0;
    uint32_t lowDetailCells = 0;
    uint32_t hlodCells = 0;

    // Errors
    uint32_t failedLoads = 0;
    uint32_t timeoutErrors = 0;
    uint32_t placeholderCells = 0;

    // Per-frame scheduler-budget accounting (see StreamingManagerConfig budgets). These
    // describe the most recent Update() frame.
    uint64_t uploadBytesThisFrame = 0;   // cell bytes committed/uploaded during the last Update
    uint32_t loadStartsThisFrame = 0;    // load pipelines started during the last Update
    uint32_t budgetDeferredCells = 0;    // completions deferred by the upload budget last Update

    uint64_t PendingCellCount() const {
        return static_cast<uint64_t>(loadingCells) + static_cast<uint64_t>(queuedCells);
    }
    uint64_t ActiveCellCount() const {
        return static_cast<uint64_t>(loadedCells) + PendingCellCount();
    }
    bool HasLoadedCells() const { return loadedCells != 0; }
    bool HasPendingCells() const { return PendingCellCount() != 0; }
    bool HasActiveCells() const { return ActiveCellCount() != 0; }
    bool HasVisibleCells() const { return visibleCells != 0; }
    bool HasMemoryBudget() const { return memoryBudget != 0; }
    bool HasMemoryUsage() const { return memoryUsed != 0 || memoryUtilization != 0.0f; }
    bool IsOverMemoryBudget() const { return memoryBudget != 0 && memoryUsed > memoryBudget; }
    bool HasFailures() const { return failedLoads != 0 || timeoutErrors != 0; }
    bool HasPlaceholderCells() const { return placeholderCells != 0; }
};

// ===== Streaming Cell Info =====

struct StreamingCellInfo {
    CellCoord coord;
    Vec3 worldPosition;
    float cellSize = 0.0f;
    CellLoadState state = CellLoadState::Unloaded;
    uint32_t layerMask = 0;
    size_t layerCount = 0;
    size_t loadedLayerCount = 0;
    uint64_t dataSize = 0;
    uint64_t memorySize = 0;
    float priority = 0.0f;
    uint64_t lastAccessFrame = 0;
    uint64_t asyncOperationHandle = 0;
    bool placeholder = false;

    bool IsLoaded() const { return state == CellLoadState::Loaded; }
    bool IsPending() const {
        return state == CellLoadState::Queued ||
               state == CellLoadState::Loading ||
               state == CellLoadState::Decompressing ||
               state == CellLoadState::Uploading;
    }
    bool IsUnloading() const { return state == CellLoadState::Unloading; }
    bool IsError() const { return state == CellLoadState::Error; }
    bool IsPlaceholder() const { return placeholder; }
    bool HasLayer(CellLayer layer) const { return (layerMask & CellMetadata::LayerBit(layer)) != 0; }
    bool HasLayers() const { return layerMask != 0 || layerCount != 0; }
    bool HasLoadedLayers() const { return loadedLayerCount != 0; }
    bool HasDiskData() const { return dataSize != 0; }
    bool HasMemoryData() const { return memorySize != 0; }
    bool HasPriority() const { return priority != 0.0f; }
    bool HasAsyncOperation() const { return asyncOperationHandle != 0; }
    float MemoryToDiskRatio() const {
        return dataSize == 0 ? 0.0f : static_cast<float>(memorySize) / static_cast<float>(dataSize);
    }
    float ClampedCellSize(float minimumSize = 1.0f) const {
        return cellSize < minimumSize ? minimumSize : cellSize;
    }
};

// ===== Streaming Manager Configuration =====

struct StreamingManagerConfig {
    // Memory budget
    size_t memoryBudgetMB = 2048;        // Total memory budget for streaming
    size_t vertexDataBudgetMB = 512;     // Budget for vertex/index data
    size_t textureBudgetMB = 1024;       // Budget for textures

    // Performance targets
    float targetLoadTime = 0.016f;       // Target load time per cell (60fps)
    float maxStallTime = 0.033f;         // Maximum time to block on streaming (30fps)

    // Streaming parameters
    float loadRadius = 256.0f;
    float unloadRadius = 384.0f;
    float prefetchRadius = 128.0f;

    // Quality settings
    bool enableHLOD = true;
    bool enableImpostors = true;
    uint32_t maxLODLevel = 4;
    float lodTransitionDistance = 64.0f;

    // Prediction settings
    bool enablePrediction = true;
    float predictionTime = 2.0f;         // Predict N seconds ahead
    uint32_t predictionSamples = 8;      // Number of prediction samples

    // Eviction policy
    EvictionStrategy evictionStrategy = EvictionStrategy::LRU;
    float evictionThreshold = 0.9f;      // Evict when 90% of budget used

    // IO settings
    uint32_t maxConcurrentLoads = 16;
    uint32_t maxConcurrentUnloads = 8;
    bool prioritizeVisibleCells = true;

    // Per-frame scheduler budgets (0 = unlimited). These bound the main-thread work a single
    // Update() commits, so a burst of async completions can't stall the terminal/UI thread.
    // maxConcurrentLoads already bounds in-flight decompression; these bound per-frame commit
    // throughput (bytes uploaded to memory/GPU) and how many new pipelines start per frame.
    uint64_t maxUploadBytesPerFrame = 0; // cap on cell bytes committed/uploaded per frame
    uint32_t maxLoadStartsPerFrame = 0;  // cap on new load (read+decompress) pipelines started per frame

    // Debug settings
    bool enableProfiling = true;
    bool enableVisualization = false;
    bool logStreamingEvents = false;

    // Camera projection (used to drive LOD screen-size selection in Update()).
    // Sensible streaming-scope defaults; override per-game if you know better.
    float cameraFovRadians = 60.0f * 3.14159265358979323846f / 180.0f;
    float cameraAspectRatio = 16.0f / 9.0f;
    float cameraNearPlane = 0.1f;
    float cameraFarPlane = 10000.0f;

    // Cell data loading (framework -> real IO integration).
    // If a cell file is missing and allowPlaceholderCellLoad==true, the cell will be marked loaded
    // with placeholder memory usage (keeps demos running without authoring data).
    std::wstring cellDataDirectory = L"data/world/cells";
    std::wstring cellFileExtension = L".ncell"; // also supports ".npkg"
    bool allowPlaceholderCellLoad = true;
    uint64_t placeholderCellSizeBytes = 256 * 1024;
};

// ===== Streaming Manager =====

class StreamingManager {
public:
    StreamingManager();
    ~StreamingManager();

    // Initialize with configuration
    bool Initialize(const StreamingManagerConfig& config);

    // Update (called every frame)
    void Update(float deltaTime, const Vec3& cameraPosition, const Vec3& cameraDirection, const Vec3& cameraVelocity);

    // Manual control
    void LoadCell(const CellCoord& coord, float priority = 0.0f);
    void UnloadCell(const CellCoord& coord);
    void ReloadCell(const CellCoord& coord);

    // Cell queries
    bool IsCellLoaded(const CellCoord& coord) const;
    CellData* GetCell(const CellCoord& coord);
    const CellData* GetCell(const CellCoord& coord) const;
    bool GetCellInfo(const CellCoord& coord, StreamingCellInfo& outInfo) const;
    std::vector<CellCoord> GetLoadedCells() const;
    std::vector<StreamingCellInfo> GetLoadedCellInfos() const;
    // Returns cell coordinates whose centers are within `radius` of `position`.
    // Note: this includes cells that are not currently loaded.
    std::vector<CellCoord> GetCellsInRange(const Vec3& position, float radius) const;

    // Asset bundles
    StreamingHandle LoadAssetBundle(const std::wstring& bundlePath);
    void UnloadAssetBundle(StreamingHandle handle);
    bool IsAssetBundleLoaded(StreamingHandle handle) const;
    bool GetAssetBundleInfo(StreamingHandle handle, AssetBundle& outBundle) const;

    // Layer management
    void LoadCellLayer(const CellCoord& coord, CellLayer layer, float priority = 0.0f);
    void UnloadCellLayer(const CellCoord& coord, CellLayer layer);
    bool IsCellLayerLoaded(const CellCoord& coord, CellLayer layer) const;

    // Priority control
    void SetCellPriority(const CellCoord& coord, float priority);
    void BoostCellPriority(const CellCoord& coord, float boost);
    void SetGlobalPriorityOverride(std::function<float(const CellCoord&, float)> priorityFunc);

    // Configuration
    void SetConfig(const StreamingManagerConfig& config);
    const StreamingManagerConfig& GetConfig() const { return config_; }

    // Statistics
    StreamingStatistics GetStatistics() const;
    WorldPartition::Statistics GetWorldPartitionStatistics() const;
    InterestManager::Statistics GetInterestStatistics() const;
    IOStatistics GetIOStatistics() const;
    LODSystem::Statistics GetLODStatistics() const;
    EvictionPolicy::Statistics GetEvictionStatistics() const;
    void ResetStatistics();

    // Sub-systems access
    WorldPartition* GetWorldPartition() { return worldPartition_.get(); }
    const WorldPartition* GetWorldPartition() const { return worldPartition_.get(); }
    AsyncIOSystem* GetAsyncIO() { return asyncIO_.get(); }
    const AsyncIOSystem* GetAsyncIO() const { return asyncIO_.get(); }
    InterestManager* GetInterestManager() { return interestManager_.get(); }
    const InterestManager* GetInterestManager() const { return interestManager_.get(); }
    LODSystem* GetLODSystem() { return lodSystem_.get(); }
    const LODSystem* GetLODSystem() const { return lodSystem_.get(); }
    EvictionPolicy* GetEvictionPolicy() { return evictionPolicy_.get(); }
    const EvictionPolicy* GetEvictionPolicy() const { return evictionPolicy_.get(); }

    // Memory management
    size_t GetMemoryUsage() const;
    size_t GetMemoryBudget() const;
    float GetMemoryUtilization() const;

    // Force unload all (for level changes)
    void UnloadAll();

    // Cleanup
    void Shutdown();

    // State check
    bool IsInitialized() const { return initialized_; }

private:
    // World partition index (available cells on disk). If empty, the system can fall back to "infinite grid"
    // behavior (useful for prototype/placeholder loads).
    void ScanAvailableCells();

    // Core update logic
    void UpdateStreaming(float deltaTime, const Vec3& cameraPosition, const Vec3& cameraDirection);
    void UpdatePredictiveStreaming(const Vec3& cameraPosition, const Vec3& cameraDirection, const Vec3& cameraVelocity);
    void UpdatePriority(const Vec3& cameraPosition, const Vec3& cameraDirection);
    void ProcessLoadQueue();
    void ProcessUnloadQueue();
    void ProcessCellOpCompletions();

    // Cell lifecycle
    struct CellLoadRequest {
        CellCoord coord;
        float priority;
        uint32_t frameIndex;
        std::vector<CellLayer> layers;  // Layers to load (empty = all layers)
    };

    struct CellUnloadRequest {
        CellCoord coord;
        uint32_t frameIndex;
    };

    void QueueCellLoad(const CellLoadRequest& request);
    void QueueCellUnload(const CellUnloadRequest& request);
    void ProcessCellLoad(const CellLoadRequest& request);
    void ProcessCellUnload(const CellUnloadRequest& request);
    std::wstring GetCellFilePath(const CellCoord& coord) const;

    // IO callbacks
    void OnCellLoadComplete(const CellCoord& coord, bool success, uint64_t bytesProcessed);
    void OnCellLoadFailed(const CellCoord& coord, const std::string& error);

    // Layer loading
    void LoadCellLayers(CellData* cell, const std::vector<CellLayer>& layers);
    void UnloadCellLayers(CellData* cell, const std::vector<CellLayer>& layers);

    // Memory management
    bool CheckMemoryBudget() const;
    void EnforceMemoryBudget();
    void UpdateMemoryStatistics();

    // Priority calculation
    float CalculateCellPriority(const CellCoord& coord, const Vec3& cameraPosition, const Vec3& cameraDirection) const;
    float CalculateLayerPriority(CellLayer layer) const;

    // Statistics tracking
    void UpdateStatistics(float deltaTime);
    void ReleaseCellLayers(CellData* cell);

    // Configuration
    StreamingManagerConfig config_;

    // Sub-systems
    std::unique_ptr<WorldPartition> worldPartition_;
    std::unique_ptr<AsyncIOSystem> asyncIO_;
    std::unique_ptr<InterestManager> interestManager_;
    std::unique_ptr<LODSystem> lodSystem_;
    std::unique_ptr<EvictionPolicy> evictionPolicy_;
    std::unique_ptr<StreamingMemoryPool> memoryPool_;

    // Load/unload queues
    std::vector<CellLoadRequest> loadQueue_;
    std::vector<CellUnloadRequest> unloadQueue_;

    // Active operations
    struct ActiveCellOp {
        Next::JobHandle job;
        uint64_t asyncRequestId = 0;
        std::wstring filePath;     // cell package path
        std::string packageName;   // derived from file stem
        uint64_t fileBytes = 0;
        bool packageBacked = false;
        std::vector<uint8_t> rawReadBuffer;
        std::vector<uint8_t> rawDecompressedBuffer;
        uint64_t decompressedBytes = 0;
        CompressionType compressionType = CompressionType::None;
    };
    std::unordered_map<CellCoord, ActiveCellOp, CellCoord::Hash> activeLoadOperations_;
    std::unordered_map<CellCoord, Next::JobHandle, CellCoord::Hash> activeUnloadOperations_;

    struct CellOpCompletion {
        CellCoord coord;
        bool isLoad = true;
        bool success = false;
        bool packageBacked = false;
        std::string packageName;
        uint64_t bytes = 0;
        uint64_t diskBytes = 0;
        std::vector<uint8_t> rawCellData;
        std::string error;
    };
    mutable std::mutex completionMutex_;
    std::vector<CellOpCompletion> completions_;

    // Asset bundles
    std::unordered_map<uint64_t, AssetBundle> assetBundles_;
    std::unordered_map<CellCoord, uint64_t, CellCoord::Hash> cellToBundle_;
    std::unordered_map<CellCoord, std::string, CellCoord::Hash> cellToPackageName_;
    std::unordered_set<CellCoord, CellCoord::Hash> availableCells_;
    std::unordered_map<CellCoord, std::wstring, CellCoord::Hash> availableCellPaths_;
    std::unordered_map<uint64_t, std::unordered_map<CellCoord, std::wstring, CellCoord::Hash>> bundleCellPathIndex_;
    std::unordered_set<CellCoord, CellCoord::Hash> bundleAvailableCells_;
    std::unordered_map<CellCoord, std::wstring, CellCoord::Hash> bundleCellPaths_;
    std::unordered_map<CellCoord, uint64_t, CellCoord::Hash> bundleCellPathOwners_;

    // Statistics
    StreamingStatistics stats_;
    uint64_t currentFrame_;
    float elapsedTime_;

    // Cached camera state (for eviction/policy decisions that happen outside Update())
    Vec3 lastCameraPosition_{0.0f, 0.0f, 0.0f};
    Vec3 lastCameraDirection_{0.0f, 0.0f, -1.0f};
    Vec3 lastCameraVelocity_{0.0f, 0.0f, 0.0f};

    // Priority override
    std::function<float(const CellCoord&, float)> priorityOverride_;

    // State
    bool initialized_;
};

} // namespace Streaming
} // namespace Next
