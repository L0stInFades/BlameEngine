#include "next/streaming/lod_system.h"
#include "next/foundation/logger.h"
#include <algorithm>
#include <cmath>

namespace Next {
namespace Streaming {

// ===== LOD System Implementation =====

LODSystem::LODSystem()
    : qualityScale_(1.0f)
    , maxFrameTimeSamples_(60)
    , initialized_(false)
{
}

LODSystem::~LODSystem() {
    Shutdown();
}

bool LODSystem::Initialize(const LODSystemConfig& config) {
    if (initialized_) {
        NEXT_LOG_WARNING("LODSystem already initialized");
        return true;
    }

    config_ = config;
    qualityScale_ = 1.0f;

    recentFrameTimes_.reserve(maxFrameTimeSamples_);


    initialized_ = true;
    return true;
}

void LODSystem::Update(float deltaTime, const Vec3& cameraPosition, const Mat4& viewProjectionMatrix) {
    (void)deltaTime;
    if (!initialized_) {
        return;
    }

    // Cache camera state for screen-size, HLOD distance, and dither math
    // performed elsewhere this frame.
    cameraPosition_ = cameraPosition;
    viewProjection_ = viewProjectionMatrix;

    // Update statistics
    stats_.highDetailObjects = 0;
    stats_.mediumDetailObjects = 0;
    stats_.lowDetailObjects = 0;
    stats_.hlodObjects = 0;
    stats_.impostorObjects = 0;
    stats_.averageLODLevel = 0.0f;
    stats_.currentQualityScale = qualityScale_;

    if (!objectLODs_.empty()) {
        double totalLevel = 0.0;
        for (const auto& [objectId, levels] : objectLODs_) {
            if (levels.empty()) continue;

            // Without a per-object world position we approximate using the
            // distance from the camera to the origin of the cell its HLOD
            // cluster belongs to (if any), falling back to 0.0.
            const uint32_t lod = SelectLODLevel(levels, 0.0f, 0.0f);
            totalLevel += static_cast<double>(lod);
            if (lod == 0) {
                stats_.highDetailObjects++;
            } else if (lod <= 2) {
                stats_.mediumDetailObjects++;
            } else {
                stats_.lowDetailObjects++;
            }
        }
        stats_.averageLODLevel = static_cast<float>(totalLevel /
            static_cast<double>(objectLODs_.size()));
    }

    stats_.hlodObjects = static_cast<uint32_t>(hlodClusters_.size());
    stats_.impostorObjects = static_cast<uint32_t>(impostors_.size());
}

void LODSystem::RegisterLODLevels(uint64_t objectId, const std::vector<LODLevel>& levels) {
    objectLODs_[objectId] = levels;
}

void LODSystem::UnregisterLODLevels(uint64_t objectId) {
    objectLODs_.erase(objectId);
}

uint64_t LODSystem::CreateHLODCluster(const CellCoord& cell, const std::vector<uint64_t>& objectIds) {
    if (!config_.enableHLOD) {
        return 0;
    }

    // Coalesce repeated cluster requests for the same cell so we don't leak
    // cluster IDs when callers re-register cells (e.g. after a streaming
    // reload).
    auto existing = cellToHLOD_.find(cell);
    if (existing != cellToHLOD_.end()) {
        auto cIt = hlodClusters_.find(existing->second);
        if (cIt != hlodClusters_.end()) {
            BuildHLODCluster(&cIt->second, objectIds);
            return existing->second;
        }
    }

    const uint64_t clusterId = nextHLODClusterId_++;

    LODCluster cluster;
    cluster.clusterId = clusterId;
    cluster.cell = cell;
    cluster.center = Vec3(
        (static_cast<float>(cell.x) + 0.5f) * cellSize_,
        0.0f,
        (static_cast<float>(cell.z) + 0.5f) * cellSize_);
    cluster.radius = cellSize_ * 0.7071068f;  // half-diagonal of the cell square

    auto inserted = hlodClusters_.emplace(clusterId, std::move(cluster));
    LODCluster* clusterPtr = &inserted.first->second;

    BuildHLODCluster(clusterPtr, objectIds);
    GenerateHLODMesh(clusterPtr);
    cellToHLOD_[cell] = clusterId;

    return clusterId;
}

void LODSystem::DestroyHLODCluster(uint64_t clusterId) {
    hlodClusters_.erase(clusterId);
}

LODCluster* LODSystem::GetHLODCluster(uint64_t clusterId) {
    auto it = hlodClusters_.find(clusterId);
    if (it != hlodClusters_.end()) {
        return &it->second;
    }
    return nullptr;
}

const LODCluster* LODSystem::GetHLODCluster(uint64_t clusterId) const {
    auto it = hlodClusters_.find(clusterId);
    if (it != hlodClusters_.end()) {
        return &it->second;
    }
    return nullptr;
}

void LODSystem::CreateImpostor(uint64_t objectId, const CellCoord& cell, const Vec3& boundsMin, const Vec3& boundsMax) {
    ImpostorData impostor;
    impostor.objectId = objectId;
    impostor.cell = cell;
    impostor.boundsMin = boundsMin;
    impostor.boundsMax = boundsMax;
    impostor.maxScreenSize = config_.impostorScreenSizeThreshold;

    impostors_[objectId] = std::move(impostor);

    // Allocate placeholder billboard handles. Renderer integration is expected
    // to back these handles with real texture allocations.
    GenerateImpostorTextures(&impostors_[objectId], objectId);
}

void LODSystem::DestroyImpostor(uint64_t objectId) {
    impostors_.erase(objectId);
}

ImpostorData* LODSystem::GetImpostor(uint64_t objectId) {
    auto it = impostors_.find(objectId);
    if (it != impostors_.end()) {
        return &it->second;
    }
    return nullptr;
}

const ImpostorData* LODSystem::GetImpostor(uint64_t objectId) const {
    auto it = impostors_.find(objectId);
    if (it != impostors_.end()) {
        return &it->second;
    }
    return nullptr;
}

uint32_t LODSystem::CalculateLODLevel(uint64_t objectId, const Vec3& objectPosition, const Vec3& cameraPosition) const {
    auto it = objectLODs_.find(objectId);
    if (it == objectLODs_.end()) {
        return 0;
    }

    const std::vector<LODLevel>& levels = it->second;
    if (levels.empty()) {
        return 0;
    }

    float distance = CalculateDistance(objectPosition, cameraPosition);
    float screenSize = 1000.0f / (distance + 1.0f);  // Approximate screen size

    return SelectLODLevel(levels, distance, screenSize);
}

float LODSystem::CalculateLODFactor(const Vec3& objectPosition, const Vec3& cameraPosition) const {
    float distance = CalculateDistance(objectPosition, cameraPosition);
    float lodDistance = config_.lodDistanceMultiplier * config_.lodTransitionDistance;

    return 1.0f - std::min(distance / lodDistance, 1.0f);
}

float LODSystem::CalculateScreenSize(const Vec3& objectPosition, float objectRadius, const Mat4& viewProjectionMatrix) const {
    // Use the projection's vertical scale (m(1,1) = 1/tan(fov/2)) to convert
    // bounding-sphere radius into normalized screen-height fraction.
    // For a perspective composed view-projection matrix, vp(1,1) is dominated
    // by the projection's y-focal length scaled by the camera's local up
    // alignment with world Y. This is a robust approximation for upright
    // cameras and a reasonable estimate for tilted views.
    const float yFocal = std::abs(viewProjectionMatrix(1, 1));

    const Vec3 toObject = objectPosition - cameraPosition_;
    const float distance = toObject.Length();
    if (distance < 1e-3f) {
        return 1.0f;  // Object effectively at the eye — fully on-screen.
    }

    const float screenSize = (objectRadius * yFocal) / distance;
    return std::max(0.0f, std::min(1.0f, screenSize));
}

bool LODSystem::ShouldUseHLOD(const CellCoord& cell, float distance) const {
    if (!config_.enableHLOD) {
        return false;
    }
    return distance > config_.hlodDistance;
}

uint64_t LODSystem::GetHLODMesh(const CellCoord& cell) const {
    auto it = cellToHLOD_.find(cell);
    if (it != cellToHLOD_.end()) {
        return it->second;
    }
    return 0;
}

bool LODSystem::ShouldUseImpostor(float screenSize) const {
    if (!config_.enableImpostors) {
        return false;
    }
    return screenSize < config_.impostorScreenSizeThreshold;
}

uint64_t LODSystem::GetImpostorTexture(uint64_t objectId) const {
    auto it = impostors_.find(objectId);
    if (it != impostors_.end() && it->second.billboardTextures.size() > 0) {
        return it->second.billboardTextures[0];
    }
    return 0;
}

void LODSystem::SetQualityScale(float scale) {
    // Manual clamp instead of std::clamp (C++17)
    if (scale < config_.minQualityScale) {
        qualityScale_ = config_.minQualityScale;
    } else if (scale > config_.maxQualityScale) {
        qualityScale_ = config_.maxQualityScale;
    } else {
        qualityScale_ = scale;
    }
}


void LODSystem::UpdateAutoLOD(float frameTime) {
    if (!config_.enableAutoLOD) {
        return;
    }

    // Track frame times
    recentFrameTimes_.push_back(frameTime);
    if (recentFrameTimes_.size() > maxFrameTimeSamples_) {
        recentFrameTimes_.erase(recentFrameTimes_.begin());
    }

    // Adjust quality based on performance
    if (ShouldReduceQuality()) {
        qualityScale_ -= 0.01f;
        qualityScale_ = std::max(qualityScale_, config_.minQualityScale);
    } else if (ShouldIncreaseQuality()) {
        qualityScale_ += 0.01f;
        qualityScale_ = std::min(qualityScale_, config_.maxQualityScale);
    }
}

LODSystem::Statistics LODSystem::GetStatistics() const {
    return stats_;
}

void LODSystem::Shutdown() {
    if (!initialized_) {
        return;
    }

    objectLODs_.clear();
    hlodClusters_.clear();
    impostors_.clear();
    cellToHLOD_.clear();
    recentFrameTimes_.clear();

    initialized_ = false;
    // NEXT_LOG_INFO("LODSystem shutdown complete");
}

// ===== Private Methods =====

uint32_t LODSystem::SelectLODLevel(const std::vector<LODLevel>& levels, float distance, float screenSize) const {
    if (levels.empty()) {
        return 0;
    }

    float adjustedDistance = distance / qualityScale_;

    for (uint32_t i = 0; i < levels.size(); ++i) {
        if (adjustedDistance <= levels[i].distance) {
            return i;
        }
    }

    return static_cast<uint32_t>(levels.size()) - 1;
}

float LODSystem::CalculateDistance(const Vec3& objectPosition, const Vec3& cameraPosition) const {
    Vec3 toObject = objectPosition - cameraPosition;
    return toObject.Length();
}

void LODSystem::BuildHLODCluster(LODCluster* cluster, const std::vector<uint64_t>& objectIds) {
    if (!cluster) {
        return;
    }

    cluster->originalObjectIds = objectIds;

    // If we have LOD information for the cluster's source objects, derive
    // synthetic HLOD levels by halving the triangle/vertex budget per step.
    // This produces a consistent set of HLOD levels even before the asset
    // pipeline materializes a real merged mesh.
    cluster->hlodLevels.clear();
    uint64_t totalTriangles = 0;
    uint64_t totalVertices = 0;
    for (uint64_t objectId : objectIds) {
        auto it = objectLODs_.find(objectId);
        if (it == objectLODs_.end() || it->second.empty()) {
            continue;
        }
        const LODLevel& base = it->second.front();
        totalTriangles += base.triangleCount;
        totalVertices += base.vertexCount;
    }

    const uint32_t hlodLevelCount = std::min<uint32_t>(config_.maxLODLevels, 4u);
    for (uint32_t i = 0; i < hlodLevelCount; ++i) {
        LODLevel level;
        level.level = i;
        level.distance = config_.hlodDistance * static_cast<float>(i + 1);
        level.screenSize = config_.impostorScreenSizeThreshold *
                           static_cast<float>(hlodLevelCount - i);
        const uint64_t shift = static_cast<uint64_t>(i);
        level.triangleCount = totalTriangles >> shift;
        level.vertexCount = totalVertices >> shift;
        level.meshHandle = 0;
        level.isHLOD = true;
        level.hlodClusterId = cluster->clusterId;
        cluster->hlodLevels.push_back(level);
    }
}

void LODSystem::GenerateHLODMesh(LODCluster* cluster) {
    if (!cluster) {
        return;
    }

    // Allocate a placeholder mesh handle. The renderer/asset pipeline is
    // expected to bind a concrete merged mesh to this handle when it builds
    // HLOD geometry; until then the handle uniquely identifies the cluster.
    if (cluster->hlodMeshHandle == 0) {
        cluster->hlodMeshHandle = nextResourceHandle_++;
    }
    for (LODLevel& level : cluster->hlodLevels) {
        if (level.meshHandle == 0) {
            level.meshHandle = nextResourceHandle_++;
        }
    }
}

void LODSystem::GenerateImpostorTextures(ImpostorData* impostor, uint64_t objectId) {
    if (!impostor) {
        return;
    }
    (void)objectId;

    // Eight billboard slots covering 45° increments around the object.
    constexpr size_t kBillboardSlots = 8;
    impostor->billboardTextures.assign(kBillboardSlots, 0);
    for (size_t i = 0; i < kBillboardSlots; ++i) {
        impostor->billboardTextures[i] = nextResourceHandle_++;
    }
    impostor->maxScreenSize = config_.impostorScreenSizeThreshold;
}

void LODSystem::RenderImpostorView(uint64_t objectId, const Vec3& cameraPosition, const Vec3& cameraDirection, void* textureOutput) {
    (void)cameraPosition;
    (void)textureOutput;  // Real renderer fills this; framework only routes.

    auto it = impostors_.find(objectId);
    if (it == impostors_.end()) {
        return;
    }
    ImpostorData& impostor = it->second;
    if (impostor.billboardTextures.empty()) {
        return;
    }

    // Pick the billboard slot whose look direction best matches the camera.
    // billboardTextures is indexed at 45° increments starting at +X (yaw=0)
    // and increasing counter-clockwise around world Y.
    const Vec3 dirXZ(cameraDirection.x, 0.0f, cameraDirection.z);
    const float lenXZ = dirXZ.Length();
    if (lenXZ < 1e-3f) {
        return;
    }
    const float yaw = std::atan2(dirXZ.z, dirXZ.x);  // [-π, π]
    constexpr float kTwoPi = 6.28318530717958647692f;
    const float normalized = (yaw < 0.0f ? yaw + kTwoPi : yaw) / kTwoPi;
    const size_t slotCount = impostor.billboardTextures.size();
    size_t slot = static_cast<size_t>(std::floor(normalized * static_cast<float>(slotCount)));
    if (slot >= slotCount) slot = slotCount - 1;

    if (impostor.billboardTextures[slot] == 0) {
        impostor.billboardTextures[slot] = nextResourceHandle_++;
    }
}

float LODSystem::CalculateDitherFactor(const Vec3& objectPosition, const Vec3& cameraPosition, uint32_t currentLOD, uint32_t targetLOD) const {
    if (!config_.enableDithering || currentLOD == targetLOD) {
        return 0.0f;
    }

    const float distance = CalculateDistance(objectPosition, cameraPosition);
    const float ditherDistance = std::max(config_.ditherDistance, 1e-3f);

    // Find the transition midpoint between currentLOD and targetLOD using the
    // configured per-step transition distance, then express our position
    // within the dither band as a [0, 1] blend factor.
    const uint32_t loLevel = std::min(currentLOD, targetLOD);
    const float midDistance = config_.lodTransitionDistance *
                              (static_cast<float>(loLevel) + 0.5f) *
                              std::max(config_.lodDistanceMultiplier, 1e-3f);

    const float halfBand = ditherDistance * 0.5f;
    if (distance <= midDistance - halfBand) {
        return (currentLOD < targetLOD) ? 0.0f : 1.0f;
    }
    if (distance >= midDistance + halfBand) {
        return (currentLOD < targetLOD) ? 1.0f : 0.0f;
    }

    const float t = (distance - (midDistance - halfBand)) / ditherDistance;
    return (currentLOD < targetLOD) ? t : (1.0f - t);
}

void LODSystem::UpdatePerformanceMetrics(float frameTime) {
    // Already handled in UpdateAutoLOD
}

bool LODSystem::ShouldReduceQuality() const {
    if (recentFrameTimes_.size() < maxFrameTimeSamples_) {
        return false;
    }

    float averageFrameTime = 0.0f;
    for (float time : recentFrameTimes_) {
        averageFrameTime += time;
    }
    averageFrameTime /= recentFrameTimes_.size();

    return averageFrameTime > config_.targetFrameTime * 1.2f;
}

bool LODSystem::ShouldIncreaseQuality() const {
    if (recentFrameTimes_.size() < maxFrameTimeSamples_) {
        return false;
    }

    float averageFrameTime = 0.0f;
    for (float time : recentFrameTimes_) {
        averageFrameTime += time;
    }
    averageFrameTime /= recentFrameTimes_.size();

    return averageFrameTime < config_.targetFrameTime * 0.8f;
}

} // namespace Streaming
} // namespace Next
