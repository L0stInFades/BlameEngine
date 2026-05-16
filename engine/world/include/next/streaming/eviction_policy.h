#pragma once

#include "next/streaming/world_partition.h"
#include <vector>
#include <unordered_map>
#include <memory>
#include <queue>

namespace Next {
namespace Streaming {

// ===== Eviction Strategies =====

enum class EvictionStrategy : uint32_t {
    LRU = 0,           // Least Recently Used
    LFU = 1,           // Least Frequently Used
    FIFO = 2,          // First In First Out
    Priority = 3,      // Lowest priority first
    Distance = 4,      // Furthest from camera first
    Custom = 5         // Custom eviction function
};

// ===== Cell Eviction Candidate =====

struct EvictionCandidate {
    CellCoord coord;
    float score;                // Eviction score (lower = evict first)
    uint64_t lastAccessFrame;
    uint32_t accessCount;
    float distance;
    float priority;
    uint64_t memorySize;

    // For priority queue (higher score = higher priority to keep)
    bool operator<(const EvictionCandidate& other) const {
        return score > other.score;
    }
};

// ===== Eviction Policy Configuration =====

struct EvictionPolicyConfig {
    // Strategy
    EvictionStrategy strategy = EvictionStrategy::LRU;

    // Thresholds
    float memoryThreshold = 0.9f;        // Evict when 90% of budget used
    float cellCountThreshold = 0.95f;    // Evict when 95% of max cells loaded

    // Protection
    bool protectVisibleCells = true;     // Never evict visible cells
    bool protectHighPriority = true;     // Never evict high-priority cells
    float protectedRadius = 64.0f;       // Never evict within this radius of camera

    // Cooldown
    uint32_t minLoadTime = 60;           // Minimum frames before cell can be evicted
    float minLoadTimeSeconds = 1.0f;     // Minimum seconds before eviction

    // Batch eviction
    uint32_t maxEvictionsPerFrame = 4;   // Limit evictions per frame
    bool enableBatchEviction = true;

    // Scoring weights (for composite strategies)
    float lruWeight = 1.0f;
    float lfuWeight = 0.5f;
    float distanceWeight = 1.0f;
    float priorityWeight = 2.0f;

    EvictionPolicyConfig() = default;
};

// ===== Eviction Policy =====

class EvictionPolicy {
public:
    EvictionPolicy();
    ~EvictionPolicy();

    // Initialize with configuration
    bool Initialize(const EvictionPolicyConfig& config);

    // Update (called every frame)
    void Update(float deltaTime, const Vec3& cameraPosition, uint64_t currentFrame);

    // Eviction candidate selection
    std::vector<EvictionCandidate> SelectEvictionCandidates(
        const std::unordered_map<CellCoord, const CellData*, CellCoord::Hash>& loadedCells,
        size_t targetMemoryFree,
        uint32_t maxCandidates
    ) const;

    // Single cell eviction check
    bool ShouldEvictCell(const CellCoord& coord, const CellData* cell, const Vec3& cameraPosition, uint64_t currentFrame) const;

    // Eviction scoring
    float CalculateEvictionScore(const CellCoord& coord, const CellData* cell, const Vec3& cameraPosition, uint64_t currentFrame) const;

    // Access tracking (call when cell is accessed)
    void RecordAccess(const CellCoord& coord, uint64_t frame);
    void RecordAccessCount(const CellCoord& coord, uint32_t count);

    // Priority management
    void SetCellPriority(const CellCoord& coord, float priority);
    float GetCellPriority(const CellCoord& coord) const;

    // Memory management
    void SetMemoryBudget(size_t budgetBytes);
    void SetMemoryUsage(size_t usedBytes);
    float GetMemoryUtilization() const;
    bool ShouldEvict() const;

    // Cell count management
    void SetMaxCellCount(uint32_t maxCount);
    void SetCurrentCellCount(uint32_t currentCount);
    bool ShouldEvictByCount() const;

    // Configuration
    void SetConfig(const EvictionPolicyConfig& config) { config_ = config; }
    const EvictionPolicyConfig& GetConfig() const { return config_; }

    // Statistics
    struct Statistics {
        uint32_t totalEvictions = 0;
        uint32_t evictionsThisFrame = 0;
        uint32_t protectedCells = 0;
        float averageEvictionScore = 0.0f;
        uint64_t memoryFreed = 0;

        bool HasEvictions() const { return totalEvictions != 0; }
        bool HasFrameEvictions() const { return evictionsThisFrame != 0; }
        bool HasProtectedCells() const { return protectedCells != 0; }
        bool HasAverageEvictionScore() const { return averageEvictionScore != 0.0f; }
        bool HasMemoryFreed() const { return memoryFreed != 0; }
        bool HasActivity() const {
            return HasEvictions() || HasFrameEvictions() || HasProtectedCells() || HasMemoryFreed();
        }
    };

    Statistics GetStatistics() const;
    void RecordEvictionBatch(uint32_t evictionCount, uint64_t memoryFreed, float averageScore);
    void ResetStatistics();

    // Cleanup
    void Shutdown();

    // State check
    bool IsInitialized() const { return initialized_; }

private:
    // Strategy-specific scoring
    float CalculateLRUScore(const CellCoord& coord, const CellData* cell, uint64_t currentFrame) const;
    float CalculateLFUScore(const CellCoord& coord, const CellData* cell) const;
    float CalculateDistanceScore(const CellCoord& coord, const Vec3& cameraPosition) const;
    float CalculatePriorityScore(const CellCoord& coord) const;
    float CalculateCompositeScore(const CellCoord& coord, const CellData* cell, const Vec3& cameraPosition, uint64_t currentFrame) const;

    // Protection checks
    bool IsCellProtected(const CellCoord& coord, const CellData* cell, const Vec3& cameraPosition, uint64_t currentFrame) const;

    // Access tracking
    struct AccessInfo {
        uint64_t lastAccessFrame;
        uint32_t accessCount;
        float lastAccessTime;
    };

    std::unordered_map<CellCoord, AccessInfo, CellCoord::Hash> accessInfo_;
    std::unordered_map<CellCoord, float, CellCoord::Hash> priorities_;

    // Configuration
    EvictionPolicyConfig config_;

    // Memory tracking
    size_t memoryBudget_;
    size_t memoryUsage_;

    // Cell count tracking
    uint32_t maxCellCount_;
    uint32_t currentCellCount_;

    // Statistics
    Statistics stats_;
    uint32_t evictionsThisFrame_;

    // Cached frame context (set in Update)
    Vec3 lastCameraPosition_{0.0f, 0.0f, 0.0f};
    uint64_t lastFrame_{0};

    // State
    bool initialized_;
};

// ===== Eviction Queue Manager =====

class EvictionQueueManager {
public:
    EvictionQueueManager();
    ~EvictionQueueManager();

    // Initialize
    bool Initialize(uint32_t maxQueueSize = 256);

    // Queue management
    void AddToQueue(const EvictionCandidate& candidate);
    void RemoveFromQueue(const CellCoord& coord);
    void ClearQueue();

    // Priority queue operations
    EvictionCandidate PopBestCandidate();
    const EvictionCandidate* PeekBestCandidate() const;
    std::vector<EvictionCandidate> GetTopCandidates(uint32_t count) const;

    // Queue state
    size_t GetQueueSize() const { return queue_.size(); }
    bool IsEmpty() const { return queue_.empty(); }

    // Statistics
    uint32_t GetTotalProcessed() const { return totalProcessed_; }
    uint32_t GetTotalEvicted() const { return totalEvicted_; }

    // Cleanup
    void Shutdown();

private:
    std::priority_queue<EvictionCandidate> queue_;
    std::unordered_map<CellCoord, float, CellCoord::Hash> coordToScore_;  // For fast removal
    uint32_t maxQueueSize_;
    uint32_t totalProcessed_;
    uint32_t totalEvicted_;
    bool initialized_;
};

// ===== Memory Eviction Helper =====

class MemoryEvictionHelper {
public:
    MemoryEvictionHelper();

    // Calculate memory to free
    static size_t CalculateMemoryToFree(size_t currentUsage, size_t budget, float targetUtilization = 0.8f);

    // Estimate cell memory
    static size_t EstimateCellMemory(const CellData* cell);

    // Select cells to evict to free target memory
    static std::vector<CellCoord> SelectCellsForEviction(
        const std::unordered_map<CellCoord, const CellData*, CellCoord::Hash>& loadedCells,
        const EvictionPolicy& policy,
        size_t targetMemoryToFree,
        const Vec3& cameraPosition,
        uint64_t currentFrame
    );

    // Validate eviction candidates
    static bool ValidateEvictionCandidates(
        const std::vector<CellCoord>& candidates,
        const std::unordered_map<CellCoord, const CellData*, CellCoord::Hash>& loadedCells,
        size_t expectedMemoryFree
    );
};

} // namespace Streaming
} // namespace Next
