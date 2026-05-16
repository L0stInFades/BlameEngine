#include "next/streaming/eviction_policy.h"
#include "next/foundation/logger.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace Next {
namespace Streaming {

// ===== Eviction Policy Implementation =====

EvictionPolicy::EvictionPolicy()
    : memoryBudget_(0)
    , memoryUsage_(0)
    , maxCellCount_(0)
    , currentCellCount_(0)
    , initialized_(false)
{
}

EvictionPolicy::~EvictionPolicy() {
    Shutdown();
}

bool EvictionPolicy::Initialize(const EvictionPolicyConfig& config) {
    if (initialized_) {
        NEXT_LOG_WARNING("EvictionPolicy already initialized");
        return true;
    }

    config_ = config;

    // Reset statistics
    stats_ = Statistics();
    evictionsThisFrame_ = 0;

    NEXT_LOG_INFO("EvictionPolicy initialized (CP7: World Streaming)");
    NEXT_LOG_INFO("  Strategy: %u", static_cast<uint32_t>(config_.strategy));
    NEXT_LOG_INFO("  Memory threshold: %.2f", config_.memoryThreshold);
    NEXT_LOG_INFO("  Protected radius: %.1f meters", config_.protectedRadius);

    initialized_ = true;
    return true;
}

void EvictionPolicy::Update(float deltaTime, const Vec3& cameraPosition, uint64_t currentFrame) {
    if (!initialized_) {
        return;
    }

    // Reset per-frame statistics
    evictionsThisFrame_ = 0;
    stats_.evictionsThisFrame = 0;

    // Cache frame context for candidate selection.
    lastCameraPosition_ = cameraPosition;
    lastFrame_ = currentFrame;
}

std::vector<EvictionCandidate> EvictionPolicy::SelectEvictionCandidates(
    const std::unordered_map<CellCoord, const CellData*, CellCoord::Hash>& loadedCells,
    size_t targetMemoryFree,
    uint32_t maxCandidates
) const {
    std::vector<EvictionCandidate> candidates;
    candidates.reserve(loadedCells.size());

    // Generate candidates
    for (const auto& [coord, cell] : loadedCells) {
        if (!cell) {
            continue;
        }

        // Skip protected cells entirely (they are not valid eviction candidates).
        if (IsCellProtected(coord, cell, lastCameraPosition_, lastFrame_)) {
            continue;
        }

        EvictionCandidate candidate;
        candidate.coord = coord;
        candidate.score = CalculateEvictionScore(coord, cell, lastCameraPosition_, lastFrame_);

        // Access info (if present).
        candidate.lastAccessFrame = cell->lastAccessFrame;
        candidate.accessCount = 0;
        auto accessIt = accessInfo_.find(coord);
        if (accessIt != accessInfo_.end()) {
            candidate.lastAccessFrame = accessIt->second.lastAccessFrame;
            candidate.accessCount = accessIt->second.accessCount;
        }

        // Distance using cell metadata world position when available.
        Vec3 toCell = cell->metadata.worldPosition - lastCameraPosition_;
        candidate.distance = toCell.Length();

        candidate.priority = GetCellPriority(coord);
        candidate.memorySize = cell->metadata.memorySize;

        candidates.push_back(candidate);
    }

    // Sort by score (lower score = evict first)
    std::sort(candidates.begin(), candidates.end(),
        [](const EvictionCandidate& a, const EvictionCandidate& b) {
            return a.score < b.score;
        }
    );

    // Select top candidates up to max
    if (candidates.size() > maxCandidates) {
        candidates.resize(maxCandidates);
    }

    return candidates;
}

bool EvictionPolicy::ShouldEvictCell(const CellCoord& coord, const CellData* cell, const Vec3& cameraPosition, uint64_t currentFrame) const {
    if (!cell) {
        return false;
    }

    // Check if cell is protected
    if (IsCellProtected(coord, cell, cameraPosition, currentFrame)) {
        // stats_.protectedCells++;  // Removed: Can't modify in const function
        return false;
    }

    // Check minimum load time
    uint64_t framesSinceLoad = currentFrame - cell->lastAccessFrame;
    if (framesSinceLoad < config_.minLoadTime) {
        return false;
    }

    return true;
}

float EvictionPolicy::CalculateEvictionScore(const CellCoord& coord, const CellData* cell, const Vec3& cameraPosition, uint64_t currentFrame) const {
    if (!cell) {
        return 0.0f;
    }

    // Check if protected
    if (IsCellProtected(coord, cell, cameraPosition, currentFrame)) {
        return std::numeric_limits<float>::max();  // Never evict
    }

    // Calculate score based on strategy
    switch (config_.strategy) {
        case EvictionStrategy::LRU:
            return CalculateLRUScore(coord, cell, currentFrame);

        case EvictionStrategy::LFU:
            return CalculateLFUScore(coord, cell);

        case EvictionStrategy::Distance:
            return CalculateDistanceScore(coord, cameraPosition);

        case EvictionStrategy::Priority:
            return CalculatePriorityScore(coord);

        default:
            return CalculateCompositeScore(coord, cell, cameraPosition, currentFrame);
    }
}

void EvictionPolicy::RecordAccess(const CellCoord& coord, uint64_t frame) {
    AccessInfo info;
    info.lastAccessFrame = frame;
    info.accessCount = 1;
    info.lastAccessTime = static_cast<float>(frame);

    accessInfo_[coord] = info;
}

void EvictionPolicy::RecordAccessCount(const CellCoord& coord, uint32_t count) {
    auto it = accessInfo_.find(coord);
    if (it != accessInfo_.end()) {
        it->second.accessCount = count;
    }
}

void EvictionPolicy::SetCellPriority(const CellCoord& coord, float priority) {
    priorities_[coord] = priority;
}

float EvictionPolicy::GetCellPriority(const CellCoord& coord) const {
    auto it = priorities_.find(coord);
    if (it != priorities_.end()) {
        return it->second;
    }
    return 1.0f;
}

void EvictionPolicy::SetMemoryBudget(size_t budgetBytes) {
    memoryBudget_ = budgetBytes;
}

void EvictionPolicy::SetMemoryUsage(size_t usedBytes) {
    memoryUsage_ = usedBytes;
}

float EvictionPolicy::GetMemoryUtilization() const {
    if (memoryBudget_ == 0) {
        return 0.0f;
    }
    return static_cast<float>(memoryUsage_) / static_cast<float>(memoryBudget_);
}

bool EvictionPolicy::ShouldEvict() const {
    return GetMemoryUtilization() > config_.memoryThreshold;
}

void EvictionPolicy::SetMaxCellCount(uint32_t maxCount) {
    maxCellCount_ = maxCount;
}

void EvictionPolicy::SetCurrentCellCount(uint32_t currentCount) {
    currentCellCount_ = currentCount;
}

bool EvictionPolicy::ShouldEvictByCount() const {
    if (maxCellCount_ == 0) {
        return false;
    }
    return static_cast<float>(currentCellCount_) / static_cast<float>(maxCellCount_) > config_.cellCountThreshold;
}

EvictionPolicy::Statistics EvictionPolicy::GetStatistics() const {
    return stats_;
}

void EvictionPolicy::RecordEvictionBatch(uint32_t evictionCount, uint64_t memoryFreed, float averageScore) {
    if (evictionCount == 0) {
        return;
    }

    const uint32_t previousEvictions = stats_.totalEvictions;
    stats_.totalEvictions += evictionCount;
    evictionsThisFrame_ += evictionCount;
    stats_.evictionsThisFrame = evictionsThisFrame_;
    stats_.memoryFreed += memoryFreed;

    const uint32_t totalEvictions = stats_.totalEvictions;
    if (totalEvictions != 0) {
        const float previousWeightedScore =
            stats_.averageEvictionScore * static_cast<float>(previousEvictions);
        const float batchWeightedScore = averageScore * static_cast<float>(evictionCount);
        stats_.averageEvictionScore =
            (previousWeightedScore + batchWeightedScore) / static_cast<float>(totalEvictions);
    }
}

void EvictionPolicy::ResetStatistics() {
    stats_ = Statistics();
    evictionsThisFrame_ = 0;
}

void EvictionPolicy::Shutdown() {
    if (!initialized_) {
        return;
    }

    accessInfo_.clear();
    priorities_.clear();

    initialized_ = false;
    NEXT_LOG_INFO("EvictionPolicy shutdown complete");
}

// ===== Private Methods =====

float EvictionPolicy::CalculateLRUScore(const CellCoord& coord, const CellData* cell, uint64_t currentFrame) const {
    auto it = accessInfo_.find(coord);
    if (it == accessInfo_.end()) {
        return 0.0f;  // Unknown recency, treat as highly evictable
    }

    // Smaller = more evictable. Older access => larger framesSinceAccess => smaller score.
    const uint64_t framesSinceAccess = currentFrame - it->second.lastAccessFrame;
    return 1.0f / (static_cast<float>(framesSinceAccess) + 1.0f);
}

float EvictionPolicy::CalculateLFUScore(const CellCoord& coord, const CellData* cell) const {
    auto it = accessInfo_.find(coord);
    if (it == accessInfo_.end()) {
        return 0.0f;
    }

    // Smaller = more evictable. Fewer accesses => smaller score.
    return static_cast<float>(it->second.accessCount + 1);
}

float EvictionPolicy::CalculateDistanceScore(const CellCoord& coord, const Vec3& cameraPosition) const {
    // Smaller = more evictable. Further away => smaller score.
    constexpr float kCellSize = 64.0f;
    const float cellCenterX = (static_cast<float>(coord.x) + 0.5f) * kCellSize;
    const float cellCenterZ = (static_cast<float>(coord.z) + 0.5f) * kCellSize;
    const float dx = cellCenterX - cameraPosition.x;
    const float dz = cellCenterZ - cameraPosition.z;
    const float dist = std::sqrt(dx * dx + dz * dz);
    return 1.0f / (dist + 1.0f);
}

float EvictionPolicy::CalculatePriorityScore(const CellCoord& coord) const {
    auto it = priorities_.find(coord);
    if (it == priorities_.end()) {
        return 1.0f;
    }

    // Smaller = more evictable. Lower priority => lower score.
    return it->second;
}

float EvictionPolicy::CalculateCompositeScore(const CellCoord& coord, const CellData* cell, const Vec3& cameraPosition, uint64_t currentFrame) const {
    float lruScore = CalculateLRUScore(coord, cell, currentFrame);
    float lfuScore = CalculateLFUScore(coord, cell);
    float distanceScore = CalculateDistanceScore(coord, cameraPosition);
    float priorityScore = CalculatePriorityScore(coord);

    // Combine scores with weights
    return config_.lruWeight * lruScore +
           config_.lfuWeight * lfuScore +
           config_.distanceWeight * distanceScore +
           config_.priorityWeight * priorityScore;
}

bool EvictionPolicy::IsCellProtected(const CellCoord& coord, const CellData* cell, const Vec3& cameraPosition, uint64_t currentFrame) const {
    if (!cell) {
        return false;
    }

    // Protect cells close to the camera (a cheap proxy for "visible").
    if (config_.protectVisibleCells) {
        Vec3 toCell = cell->metadata.worldPosition - cameraPosition;
        if (toCell.Length() <= config_.protectedRadius) {
            return true;
        }
    }

    // Check if high priority
    if (config_.protectHighPriority) {
        float priority = GetCellPriority(coord);
        if (priority > 5.0f) {  // Threshold for "high priority"
            return true;
        }
    }

    // Check protected radius
    // (Already covered above via protectVisibleCells + protectedRadius.)

    return false;
}

// ===== Memory Eviction Helper =====

MemoryEvictionHelper::MemoryEvictionHelper() {
}

size_t MemoryEvictionHelper::CalculateMemoryToFree(size_t currentUsage, size_t budget, float targetUtilization) {
    size_t targetUsage = static_cast<size_t>(budget * targetUtilization);
    if (currentUsage > targetUsage) {
        return currentUsage - targetUsage;
    }
    return 0;
}

size_t MemoryEvictionHelper::EstimateCellMemory(const CellData* cell) {
    if (!cell) {
        return 0;
    }
    return cell->metadata.memorySize;
}

std::vector<CellCoord> MemoryEvictionHelper::SelectCellsForEviction(
    const std::unordered_map<CellCoord, const CellData*, CellCoord::Hash>& loadedCells,
    const EvictionPolicy& policy,
    size_t targetMemoryToFree,
    const Vec3& cameraPosition,
    uint64_t currentFrame
) {
    std::vector<CellCoord> cellsToEvict;
    size_t memoryFreed = 0;

    // Generate candidates
    std::vector<EvictionCandidate> candidates = policy.SelectEvictionCandidates(
        loadedCells, targetMemoryToFree, 128  // Max candidates
    );

    // Select cells until we free enough memory
    for (const auto& candidate : candidates) {
        if (memoryFreed >= targetMemoryToFree) {
            break;
        }

        cellsToEvict.push_back(candidate.coord);
        memoryFreed += candidate.memorySize;
    }

    return cellsToEvict;
}

bool MemoryEvictionHelper::ValidateEvictionCandidates(
    const std::vector<CellCoord>& candidates,
    const std::unordered_map<CellCoord, const CellData*, CellCoord::Hash>& loadedCells,
    size_t expectedMemoryFree
) {
    size_t totalMemory = 0;

    for (const CellCoord& coord : candidates) {
        auto it = loadedCells.find(coord);
        if (it != loadedCells.end()) {
            const CellData* cell = it->second;
            if (cell) {
                totalMemory += cell->metadata.memorySize;
            }
        }
    }

    return totalMemory >= expectedMemoryFree;
}

} // namespace Streaming
} // namespace Next
