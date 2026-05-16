#include "next/streaming/interest_manager.h"
#include "next/foundation/logger.h"
#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace Next {
namespace Streaming {

// ===== Interest Manager Implementation =====

InterestManager::InterestManager()
    : nextInterestPointId_(1)
    , lastRegionUpdateFrame_(0)
    , initialized_(false)
{
}

InterestManager::~InterestManager() {
    Shutdown();
}

bool InterestManager::Initialize(const InterestManagerConfig& config) {
    if (initialized_) {
        NEXT_LOG_WARNING("InterestManager already initialized");
        return true;
    }

    config_ = config;

    // Reset statistics
    stats_ = Statistics();

    NEXT_LOG_INFO("InterestManager initialized (CP7: World Streaming)");
    initialized_ = true;
    return true;
}

void InterestManager::Update(float deltaTime) {
    if (!initialized_) {
        return;
    }

    currentFrame_++;

    // Update temporary interest points
    UpdateTemporaryPoints(deltaTime);

    // Remove expired points
    RemoveExpiredPoints();

    // Update interest regions periodically
    // Keep this cheap: refresh at a low cadence (or when empty).
    if (interestRegions_.empty() || (currentFrame_ - lastRegionUpdateFrame_) >= 30) {
        UpdateInterestRegions();
        lastRegionUpdateFrame_ = currentFrame_;
    }

    UpdateStatistics();
}

uint64_t InterestManager::AddInterestPoint(const InterestPoint& point) {
    if (!initialized_) {
        return 0;
    }

    if (config_.maxInterestPoints > 0 && interestPoints_.size() >= config_.maxInterestPoints) {
        NEXT_LOG_WARNING("InterestManager: max interest points reached (%u)", config_.maxInterestPoints);
        return 0;
    }

    uint64_t id = nextInterestPointId_++;
    InterestPoint newPoint = point;
    newPoint.id = id;
    if (newPoint.duration > 0.0f && newPoint.remainingTime <= 0.0f) {
        newPoint.remainingTime = newPoint.duration;
    }

    interestPoints_[id] = newPoint;

    return id;
}

void InterestManager::RemoveInterestPoint(uint64_t id) {
    interestPoints_.erase(id);
}

void InterestManager::UpdateInterestPoint(uint64_t id, const InterestPoint& point) {
    auto it = interestPoints_.find(id);
    if (it != interestPoints_.end()) {
        it->second = point;
        it->second.id = id;
    }
}

InterestPoint* InterestManager::GetInterestPoint(uint64_t id) {
    auto it = interestPoints_.find(id);
    if (it != interestPoints_.end()) {
        return &it->second;
    }
    return nullptr;
}

const InterestPoint* InterestManager::GetInterestPoint(uint64_t id) const {
    auto it = interestPoints_.find(id);
    if (it != interestPoints_.end()) {
        return &it->second;
    }
    return nullptr;
}

uint64_t InterestManager::SetCameraPosition(const Vec3& position, const Vec3& direction, const Vec3& velocity) {
    InterestPoint p;
    p.type = InterestPointType::Camera;
    p.position = position;
    p.direction = direction;
    p.velocity = velocity;
    p.priority = config_.cameraBaseWeight;
    p.shape = InterestPoint::Shape::Frustum;
    p.frustumParams = {60.0f, 16.0f / 9.0f, 0.1f, 1000.0f};

    if (cameraPointId_ != 0) {
        auto it = interestPoints_.find(cameraPointId_);
        if (it != interestPoints_.end()) {
            p.id = cameraPointId_;
            it->second = p;
            return cameraPointId_;
        }
        cameraPointId_ = 0;
    }

    cameraPointId_ = AddInterestPoint(p);
    return cameraPointId_;
}

uint64_t InterestManager::SetPlayerPosition(const Vec3& position, const Vec3& velocity) {
    InterestPoint p;
    p.type = InterestPointType::Player;
    p.position = position;
    p.velocity = velocity;
    p.priority = 1.0f;
    p.shape = InterestPoint::Shape::Sphere;
    p.radius = 10.0f;

    if (playerPointId_ != 0) {
        auto it = interestPoints_.find(playerPointId_);
        if (it != interestPoints_.end()) {
            p.id = playerPointId_;
            it->second = p;
            return playerPointId_;
        }
        playerPointId_ = 0;
    }

    playerPointId_ = AddInterestPoint(p);
    return playerPointId_;
}

uint64_t InterestManager::AddCinematicPath(const std::vector<Vec3>& path, float duration) {
    if (!initialized_ || path.empty()) {
        return 0;
    }

    // Approximate the whole path as a bounding sphere interest point.
    Vec3 minV = path[0];
    Vec3 maxV = path[0];
    for (const Vec3& v : path) {
        minV.x = std::min(minV.x, v.x);
        minV.y = std::min(minV.y, v.y);
        minV.z = std::min(minV.z, v.z);
        maxV.x = std::max(maxV.x, v.x);
        maxV.y = std::max(maxV.y, v.y);
        maxV.z = std::max(maxV.z, v.z);
    }
    const Vec3 center = (minV + maxV) * 0.5f;
    float radius = 0.0f;
    for (const Vec3& v : path) {
        radius = std::max(radius, (v - center).Length());
    }

    InterestPoint p;
    p.type = InterestPointType::Cinematic;
    p.position = center;
    p.priority = config_.cinematicPriorityMultiplier;
    p.shape = InterestPoint::Shape::Sphere;
    p.radius = std::max(radius, 1.0f);
    p.duration = duration;
    p.remainingTime = duration;

    const uint64_t id = AddInterestPoint(p);
    if (id != 0) {
        cinematicPaths_[id] = path;
    }
    return id;
}

uint64_t InterestManager::AddQuestObjective(const Vec3& position, float radius) {
    InterestPoint point;
    point.type = InterestPointType::Quest;
    point.position = position;
    point.priority = config_.questPriorityMultiplier;
    point.shape = InterestPoint::Shape::Sphere;
    point.radius = radius;

    return AddInterestPoint(point);
}

float InterestManager::CalculateCellInterest(const CellCoord& coord) const {
    float totalInterest = 0.0f;

    for (const auto& [id, point] : interestPoints_) {
        const float distance = CalculateDistanceToCell(coord, point.position);
        float contrib = 0.0f;

        switch (point.shape) {
            case InterestPoint::Shape::Point: {
                contrib = 1.0f / (1.0f + distance * config_.cameraFalloff * 0.02f);
                break;
            }
            case InterestPoint::Shape::Sphere: {
                const float outside = std::max(0.0f, distance - point.radius);
                contrib = 1.0f / (1.0f + outside * 0.05f);
                break;
            }
            case InterestPoint::Shape::Box: {
                Vec3 cellPos = worldPartition_ ? worldPartition_->CellToWorld(coord)
                                               : Vec3(static_cast<float>(coord.x) * 64.0f, 0.0f, static_cast<float>(coord.z) * 64.0f);
                Vec3 d = cellPos - point.position;
                const float ox = std::max(0.0f, std::abs(d.x) - point.extents.x);
                const float oy = std::max(0.0f, std::abs(d.y) - point.extents.y);
                const float oz = std::max(0.0f, std::abs(d.z) - point.extents.z);
                const float outside = std::sqrt(ox * ox + oy * oy + oz * oz);
                contrib = 1.0f / (1.0f + outside * 0.05f);
                break;
            }
            case InterestPoint::Shape::Frustum: {
                if (IsCellInFrustum(coord, point)) {
                    contrib = 1.0f / (1.0f + distance * 0.01f);
                } else {
                    contrib = 0.0f;
                }
                break;
            }
            default:
                contrib = 0.0f;
                break;
        }

        if (config_.enablePrediction) {
            contrib = std::max(contrib, CalculatePredictedInterest(coord, point));
        }

        totalInterest += contrib * point.priority;
    }

    return totalInterest;
}

std::vector<InterestRegion> InterestManager::GetInterestRegions() const {
    return interestRegions_;
}

float InterestManager::CalculatePriority(const CellCoord& coord, const Vec3& cameraPosition, const Vec3& cameraDirection) const {
    (void)cameraPosition;
    (void)cameraDirection;
    return CalculateCellInterest(coord);
}

InterestManager::Statistics InterestManager::GetStatistics() const {
    return stats_;
}

void InterestManager::Shutdown() {
    if (!initialized_) {
        return;
    }

    interestPoints_.clear();
    cinematicPaths_.clear();
    interestRegions_.clear();

    initialized_ = false;
    NEXT_LOG_INFO("InterestManager shutdown complete");
}

// ===== Private Methods =====

float InterestManager::CalculateInterestAtPoint(const Vec3& point) const {
    (void)point;
    // Legacy helper retained for API stability; cell-based interest is implemented in CalculateCellInterest().
    return 1.0f;
}

float InterestManager::CalculatePredictedInterest(const CellCoord& coord, const InterestPoint& point) const {
    if (!config_.enablePrediction) {
        return 0.0f;
    }

    // Only consider prediction when we have meaningful velocity.
    if (point.velocity.Dot(point.velocity) < 1e-6f) {
        return 0.0f;
    }

    const std::vector<Vec3> path = PredictPath(point.position, point.velocity, config_.predictionTime, config_.predictionSamples);
    float best = 0.0f;
    for (const Vec3& p : path) {
        const float dist = CalculateDistanceToCell(coord, p);
        const float v = 1.0f / (1.0f + dist * 0.02f);
        best = std::max(best, v);
    }

    // Predicted interest should be weaker than direct interest.
    return best * 0.5f;
}

std::vector<Vec3> InterestManager::PredictPath(const Vec3& position, const Vec3& velocity, float time, uint32_t samples) const {
    std::vector<Vec3> path;
    if (samples == 0) {
        return path;
    }
    path.reserve(samples);

    const float denom = (samples > 1) ? static_cast<float>(samples - 1) : 1.0f;
    for (uint32_t i = 0; i < samples; ++i) {
        float t = time * (static_cast<float>(i) / denom);
        Vec3 predictedPos = position + velocity * t;
        path.push_back(predictedPos);
    }

    return path;
}

bool InterestManager::IsCellInFrustum(const CellCoord& coord, const InterestPoint& point) const {
    if (point.shape != InterestPoint::Shape::Frustum) {
        return true;
    }

    Vec3 cellPos = worldPartition_ ? worldPartition_->CellToWorld(coord)
                                   : Vec3(static_cast<float>(coord.x) * 64.0f, 0.0f, static_cast<float>(coord.z) * 64.0f);
    Vec3 toCell = cellPos - point.position;
    const float dist = toCell.Length();
    if (dist < point.frustumParams.nearZ || dist > point.frustumParams.farZ) {
        return false;
    }

    Vec3 forward = point.direction;
    const float fwdLen = forward.Length();
    if (fwdLen < 1e-6f) {
        return true;
    }
    forward = forward * (1.0f / fwdLen);

    Vec3 dir = toCell * (1.0f / std::max(dist, 1e-6f));
    const float dot = std::max(-1.0f, std::min(1.0f, dir.Dot(forward)));

    const float halfFovRad = (point.frustumParams.fov * 0.5f) * (3.1415926535f / 180.0f);
    const float cosHalfFov = std::cos(halfFovRad);
    return dot >= cosHalfFov;
}

float InterestManager::CalculateDistanceToCell(const CellCoord& coord, const Vec3& point) const {
    Vec3 cellPos;
    if (worldPartition_) {
        cellPos = worldPartition_->CellToWorld(coord);
    } else {
        constexpr float kCellSize = 64.0f;
        cellPos = Vec3((static_cast<float>(coord.x) + 0.5f) * kCellSize, 0.0f, (static_cast<float>(coord.z) + 0.5f) * kCellSize);
    }
    return (cellPos - point).Length();
}

float InterestManager::CalculateWeightedDistance(float distance, const InterestPoint& point) const {
    float w = 1.0f / (1.0f + distance * config_.cameraFalloff * 0.02f);
    switch (point.type) {
        case InterestPointType::Cinematic:
            w *= config_.cinematicPriorityMultiplier;
            break;
        case InterestPointType::Quest:
            w *= config_.questPriorityMultiplier;
            break;
        default:
            break;
    }
    return w;
}

void InterestManager::UpdateInterestRegions() {
    interestRegions_.clear();
    interestRegions_.reserve(interestPoints_.size());

    for (const auto& [id, point] : interestPoints_) {
        InterestRegion r;
        r.center = point.position;
        r.weight = point.priority;

        switch (point.shape) {
            case InterestPoint::Shape::Sphere:
                r.radius = std::max(point.radius, 1.0f);
                break;
            case InterestPoint::Shape::Box:
                r.radius = std::max(std::max(point.extents.x, point.extents.z), 1.0f);
                break;
            case InterestPoint::Shape::Frustum:
                r.radius = std::max(point.frustumParams.farZ, 1.0f);
                break;
            default:
                r.radius = 128.0f;
                break;
        }

        r.cells = GetCellsInRegion(r.center, r.radius);
        interestRegions_.push_back(std::move(r));
    }
}

void InterestManager::UpdateStatistics() {
    stats_.activeInterestPoints = static_cast<uint32_t>(interestPoints_.size());
    stats_.highInterestCells = 0;
    stats_.mediumInterestCells = 0;
    stats_.lowInterestCells = 0;

    std::unordered_set<CellCoord, CellCoord::Hash> countedCells;
    for (const InterestRegion& region : interestRegions_) {
        for (const CellCoord& coord : region.cells) {
            if (!countedCells.insert(coord).second) {
                continue;
            }

            const float interest = CalculateCellInterest(coord);
            if (interest >= 2.0f) {
                stats_.highInterestCells++;
            } else if (interest >= 1.0f) {
                stats_.mediumInterestCells++;
            } else if (interest > 0.0f) {
                stats_.lowInterestCells++;
            }
        }
    }
}

std::vector<CellCoord> InterestManager::GetCellsInRegion(const Vec3& center, float radius) const {
    std::vector<CellCoord> out;

    if (radius <= 0.0f) {
        return out;
    }

    const float cellSize = worldPartition_ ? std::max(1.0f, worldPartition_->GetConfig().cellSize) : 64.0f;
    const float invCellSize = 1.0f / cellSize;

    const int32_t minX = static_cast<int32_t>(std::floor((center.x - radius) * invCellSize));
    const int32_t maxX = static_cast<int32_t>(std::floor((center.x + radius) * invCellSize));
    const int32_t minZ = static_cast<int32_t>(std::floor((center.z - radius) * invCellSize));
    const int32_t maxZ = static_cast<int32_t>(std::floor((center.z + radius) * invCellSize));

    const float radiusSq = radius * radius;
    for (int32_t x = minX; x <= maxX; ++x) {
        for (int32_t z = minZ; z <= maxZ; ++z) {
            CellCoord c{x, z};
            Vec3 cellCenter = worldPartition_ ? worldPartition_->CellToWorld(c)
                                             : Vec3((static_cast<float>(x) + 0.5f) * cellSize, 0.0f, (static_cast<float>(z) + 0.5f) * cellSize);
            Vec3 d = cellCenter - center;
            if (d.Dot(d) <= radiusSq) {
                out.push_back(c);
            }
        }
    }

    return out;
}

void InterestManager::UpdateTemporaryPoints(float deltaTime) {
    for (auto& [id, point] : interestPoints_) {
        if (point.duration > 0.0f) {
            point.remainingTime -= deltaTime;
        }
    }
}

void InterestManager::RemoveExpiredPoints() {
    auto it = interestPoints_.begin();
    while (it != interestPoints_.end()) {
        if (it->second.duration > 0.0f && it->second.remainingTime <= 0.0f) {
            if (it->first == cameraPointId_) {
                cameraPointId_ = 0;
            }
            if (it->first == playerPointId_) {
                playerPointId_ = 0;
            }
            cinematicPaths_.erase(it->first);
            it = interestPoints_.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace Streaming
} // namespace Next
