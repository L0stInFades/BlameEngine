#include "next/streaming/world_partition.h"
#include "next/foundation/logger.h"
#include <algorithm>
#include <cmath>

namespace Next {
namespace Streaming {

// ===== World Partition Implementation =====

WorldPartition::WorldPartition()
    : currentFrame_(0)
    , initialized_(false)
{
}

WorldPartition::~WorldPartition() {
    Shutdown();
}

bool WorldPartition::Initialize(const WorldPartitionConfig& config) {
    if (initialized_) {
        NEXT_LOG_WARNING("WorldPartition already initialized");
        return true;
    }

    config_ = config;

    // Initialize statistics
    stats_.loadedCells = 0;
    stats_.queuedCells = 0;
    stats_.loadingCells = 0;
    stats_.totalCells = 0;
    stats_.memoryUsageMB = 0;
    stats_.averageLoadTime = 0.0f;
    stats_.averageUnloadTime = 0.0f;

    // Reserve space for load queue
    loadQueue_.reserve(config_.maxPendingLoads);

    NEXT_LOG_INFO("WorldPartition initialized (CP7: World Streaming)");
    NEXT_LOG_INFO("  Cell size: %.1f meters", config_.cellSize);
    NEXT_LOG_INFO("  Load radius: %.1f meters", config_.loadRadius);
    NEXT_LOG_INFO("  Max loaded cells: %zu", config_.maxLoadedCells);

    initialized_ = true;
    return true;
}

void WorldPartition::Update(float deltaTime, const Vec3& cameraPosition, const Vec3& cameraDirection) {
    if (!initialized_) {
        return;
    }

    currentFrame_++;

    // Update load/unload queues
    UpdateLoadQueue(cameraPosition, cameraDirection);
    UpdateUnloadQueue(cameraPosition);

    // Update statistics
    stats_.loadedCells = 0;
    stats_.queuedCells = 0;
    stats_.loadingCells = 0;
    stats_.totalCells = static_cast<uint32_t>(cells_.size());

    uint64_t totalMemoryBytes = 0;
    for (const auto& [coord, cell] : cells_) {
        (void)coord;
        if (cell->state == CellLoadState::Loaded) {
            stats_.loadedCells++;
            totalMemoryBytes += cell->metadata.memorySize;
        } else if (cell->state == CellLoadState::Queued) {
            stats_.queuedCells++;
        } else if (cell->state == CellLoadState::Loading ||
                   cell->state == CellLoadState::Decompressing ||
                   cell->state == CellLoadState::Uploading) {
            stats_.loadingCells++;
        }
    }

    stats_.memoryUsageMB = totalMemoryBytes / (1024 * 1024);
}

void WorldPartition::UpdateLoadQueue(const Vec3& cameraPosition, const Vec3& cameraDirection) {
    if (loadQueue_.empty()) {
        return;
    }

    // Calculate priorities for all queued cells
    CalculatePriorities(cameraPosition, cameraDirection);

    // Sort queue by priority (higher priority first)
    std::sort(loadQueue_.begin(), loadQueue_.end(),
        [](const LoadRequest& a, const LoadRequest& b) {
            return a.priority > b.priority;  // Descending order
        }
    );

    // Limit queue size
    if (loadQueue_.size() > config_.maxPendingLoads) {
        loadQueue_.resize(config_.maxPendingLoads);
    }
}

void WorldPartition::UpdateUnloadQueue(const Vec3& cameraPosition) {
    if (unloadQueue_.empty()) {
        return;
    }

    // Process unload queue (actual unloading is done by StreamingManager)
    unloadQueue_.clear();
}

void WorldPartition::CalculatePriorities(const Vec3& cameraPosition, const Vec3& cameraDirection) {
    for (auto& request : loadQueue_) {
        CellCoord coord = request.coord;

        // Get cell position
        Vec3 cellPosition = CellToWorld(coord);
        Vec3 toCell = cellPosition - cameraPosition;
        float distance = toCell.Length();

        // Base priority: closer = higher priority
        float distancePriority = 1.0f - (distance / config_.loadRadius);
        distancePriority = std::max(0.0f, distancePriority);

        // Direction priority: cells in front of camera get boost
        float directionPriority = 0.0f;
        if (config_.prioritizeCameraDirection && distance > 0.0f) {
            Vec3 toCellNormalized = toCell.Normalize();
            float dot = toCellNormalized.Dot(cameraDirection);
            directionPriority = (dot + 1.0f) * 0.5f;  // Map [-1, 1] to [0, 1]
            directionPriority *= config_.cameraDirectionWeight;
        }

        // Combine priorities
        request.priority = distancePriority + directionPriority;
        request.requestFrame = currentFrame_;
    }
}

CellData* WorldPartition::GetCell(const CellCoord& coord) {
    auto it = cells_.find(coord);
    if (it != cells_.end()) {
        return it->second.get();
    }
    return nullptr;
}

const CellData* WorldPartition::GetCell(const CellCoord& coord) const {
    auto it = cells_.find(coord);
    if (it != cells_.end()) {
        return it->second.get();
    }
    return nullptr;
}

bool WorldPartition::IsCellLoaded(const CellCoord& coord) const {
    const CellData* cell = GetCell(coord);
    return cell && cell->state == CellLoadState::Loaded;
}

std::vector<CellCoord> WorldPartition::GetLoadedCells() const {
    std::vector<CellCoord> loaded;
    loaded.reserve(cells_.size());

    for (const auto& [coord, cell] : cells_) {
        if (cell->state == CellLoadState::Loaded) {
            loaded.push_back(coord);
        }
    }

    return loaded;
}

RegionMetadata* WorldPartition::GetRegion(const RegionCoord& coord) {
    auto it = regions_.find(coord);
    if (it != regions_.end()) {
        return it->second.get();
    }
    return nullptr;
}

const RegionMetadata* WorldPartition::GetRegion(const RegionCoord& coord) const {
    auto it = regions_.find(coord);
    if (it != regions_.end()) {
        return it->second.get();
    }
    return nullptr;
}

CellCoord WorldPartition::WorldToCell(const Vec3& worldPosition) const {
    float cellSize = config_.cellSize;
    int32_t cellX = static_cast<int32_t>(std::floor(worldPosition.x / cellSize));
    int32_t cellZ = static_cast<int32_t>(std::floor(worldPosition.z / cellSize));
    return CellCoord(cellX, cellZ);
}

Vec3 WorldPartition::CellToWorld(const CellCoord& coord) const {
    float cellSize = config_.cellSize;
    float halfSize = cellSize * 0.5f;
    float x = static_cast<float>(coord.x) * cellSize + halfSize;
    float z = static_cast<float>(coord.z) * cellSize + halfSize;
    return Vec3(x, 0.0f, z);
}

void WorldPartition::RequestCellLoad(const CellCoord& coord, float priority) {
    // Check if cell already exists
    CellData* cell = GetCell(coord);
    if (cell) {
        // If the cell exists but isn't resident, allow it to be re-queued.
        if (cell->state == CellLoadState::Unloaded || cell->state == CellLoadState::Error) {
            cell->state = CellLoadState::Queued;
            cell->priority = priority;
            cell->lastAccessFrame = currentFrame_;

            LoadRequest request;
            request.coord = coord;
            request.priority = priority;
            request.requestFrame = currentFrame_;
            loadQueue_.push_back(request);
            return;
        }

        // Update priority if already queued/loading.
        if (cell->state == CellLoadState::Queued || cell->state == CellLoadState::Loading) {
            cell->priority = priority;
            cell->lastAccessFrame = currentFrame_;
        }

        return;
    }

    // Create new cell and add to load queue
    cell = CreateCell(coord);
    if (cell) {
        cell->state = CellLoadState::Queued;
        cell->priority = priority;
        cell->lastAccessFrame = currentFrame_;

        LoadRequest request;
        request.coord = coord;
        request.priority = priority;
        request.requestFrame = currentFrame_;
        loadQueue_.push_back(request);
    }
}

void WorldPartition::RequestCellUnload(const CellCoord& coord) {
    CellData* cell = GetCell(coord);
    if (cell && cell->state == CellLoadState::Loaded) {
        cell->state = CellLoadState::Unloading;
        unloadQueue_.push_back(coord);
    }
}

void WorldPartition::UpdateCellState(const CellCoord& coord, CellLoadState newState) {
    CellData* cell = GetCell(coord);
    if (cell) {
        cell->state = newState;
        cell->lastAccessFrame = currentFrame_;
    }
}

WorldPartition::Statistics WorldPartition::GetStatistics() const {
    Statistics stats = stats_;
    stats.loadedCells = 0;
    stats.loadingCells = 0;
    stats.queuedCells = 0;
    stats.totalCells = cells_.size();

    uint64_t totalMemoryBytes = 0;
    for (const auto& [coord, cell] : cells_) {
        (void)coord;
        if (!cell) {
            continue;
        }

        if (cell->IsLoaded()) {
            stats.loadedCells++;
            totalMemoryBytes += cell->metadata.memorySize;
        } else if (cell->state == CellLoadState::Queued) {
            stats.queuedCells++;
        } else if (cell->state == CellLoadState::Loading ||
                   cell->state == CellLoadState::Decompressing ||
                   cell->state == CellLoadState::Uploading) {
            stats.loadingCells++;
        }
    }

    stats.memoryUsageMB = totalMemoryBytes / (1024 * 1024);
    return stats;
}

void WorldPartition::Shutdown() {
    if (!initialized_) {
        return;
    }

    // Clear all data
    cells_.clear();
    regions_.clear();
    loadQueue_.clear();
    unloadQueue_.clear();

    initialized_ = false;
    NEXT_LOG_INFO("WorldPartition shutdown complete");
}

// ===== Private Methods =====

CellData* WorldPartition::CreateCell(const CellCoord& coord) {
    // Check if cell already exists
    auto it = cells_.find(coord);
    if (it != cells_.end()) {
        return it->second.get();
    }

    // Create new cell
    auto cell = std::make_unique<CellData>();
    cell->coord = coord;
    cell->metadata.coord = coord;
    cell->metadata.cellSize = config_.cellSize;
    cell->metadata.worldPosition = CellToWorld(coord);

    CellData* cellPtr = cell.get();
    cells_[coord] = std::move(cell);

    // Create region if needed
    RegionCoord regionCoord = RegionCoord::FromCellCoord(coord);
    if (!regions_.count(regionCoord)) {
        CreateRegion(regionCoord);
    }

    return cellPtr;
}

void WorldPartition::DestroyCell(const CellCoord& coord) {
    auto it = cells_.find(coord);
    if (it != cells_.end()) {
        cells_.erase(it);
    }
}

RegionMetadata* WorldPartition::CreateRegion(const RegionCoord& coord) {
    // Check if region already exists
    auto it = regions_.find(coord);
    if (it != regions_.end()) {
        return it->second.get();
    }

    // Create new region
    auto region = std::make_unique<RegionMetadata>();
    region->coord = coord;

    RegionMetadata* regionPtr = region.get();
    regions_[coord] = std::move(region);

    return regionPtr;
}

bool WorldPartition::ShouldLoadCell(const CellCoord& coord, const Vec3& cameraPosition, const Vec3& cameraDirection) const {
    Vec3 cellPosition = CellToWorld(coord);
    Vec3 toCell = cellPosition - cameraPosition;
    float distance = toCell.Length();

    // Check distance
    if (distance > config_.loadRadius) {
        return false;
    }

    // Check if cell is already loaded
    if (IsCellLoaded(coord)) {
        return false;
    }

    return true;
}

bool WorldPartition::ShouldUnloadCell(const CellCoord& coord, const Vec3& cameraPosition) const {
    Vec3 cellPosition = CellToWorld(coord);
    Vec3 toCell = cellPosition - cameraPosition;
    float distance = toCell.Length();

    // Check distance
    if (distance > config_.unloadRadius) {
        return true;
    }

    return false;
}

} // namespace Streaming
} // namespace Next
