#pragma once

#include "next/math/math.h"
#include "next/streaming/world_partition.h"
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>

namespace Next {
namespace Streaming {

// ===== Interest Point Type =====

enum class InterestPointType : uint32_t {
    Camera = 0,           // Camera position
    Player = 1,           // Player position (may differ from camera)
    Waypoint = 2,         // AI navigation waypoint
    Quest = 3,            // Quest objective
    Cinematic = 4,        // Cinematic camera path
    Audio = 5,            // Audio listener/source
    Script = 6,           // Script-defined interest point
    Physics = 7,          // Physics simulation region
    AI = 8,               // AI activity region
    Custom = 9
};

// ===== Interest Point =====

struct InterestPoint {
    uint64_t id;
    InterestPointType type;
    Vec3 position;
    Vec3 velocity;        // For prediction
    Vec3 direction;       // Looking direction (for camera)

    // Shape (can be sphere, box, or frustum)
    enum class Shape : uint32_t {
        Point = 0,
        Sphere = 1,
        Box = 2,
        Frustum = 3
    } shape;

    // Shape parameters
    float radius;         // For sphere
    Vec3 extents;         // For box (half-extents)
    struct {              // For frustum
        float fov;
        float aspect;
        float nearZ;
        float farZ;
    } frustumParams;

    // Priority (higher = more important)
    float priority;

    // Duration (0 = permanent, >0 = temporary in seconds)
    float duration;
    float remainingTime;

    // Layer mask (which layers this interest point affects)
    uint32_t layerMask;

    // User data
    void* userData;

    InterestPoint()
        : id(0)
        , type(InterestPointType::Custom)
        , position(0.0f, 0.0f, 0.0f)
        , velocity(0.0f, 0.0f, 0.0f)
        , direction(0.0f, 0.0f, -1.0f)
        , shape(Shape::Point)
        , radius(0.0f)
        , extents(0.0f, 0.0f, 0.0f)
        , frustumParams{60.0f, 16.0f / 9.0f, 0.1f, 1000.0f}
        , priority(1.0f)
        , duration(0.0f)
        , remainingTime(0.0f)
        , layerMask(0xFFFFFFFF)
        , userData(nullptr)
    {}
};

// ===== Interest Region =====

struct InterestRegion {
    Vec3 center;
    float radius;
    float weight;         // Weight for priority calculation
    std::vector<CellCoord> cells;  // Cells in this region
};

// ===== Interest Manager Configuration =====

struct InterestManagerConfig {
    // Maximum number of interest points
    uint32_t maxInterestPoints = 64;

    // Camera settings
    float cameraBaseWeight = 1.0f;
    float cameraForwardBoost = 2.0f;
    float cameraFalloff = 1.0f;

    // Prediction settings
    bool enablePrediction = true;
    float predictionTime = 2.0f;         // Predict N seconds ahead
    uint32_t predictionSamples = 8;      // Number of samples along predicted path

    // Cinematic priority
    float cinematicPriorityMultiplier = 10.0f;
    float questPriorityMultiplier = 5.0f;

    // Layer priority multipliers
    std::vector<float> layerWeights;

    InterestManagerConfig() {
        layerWeights.resize(static_cast<size_t>(CellLayer::Max), 1.0f);
    }
};

// ===== Interest Manager =====

class InterestManager {
public:
    InterestManager();
    ~InterestManager();

    // Initialize with configuration
    bool Initialize(const InterestManagerConfig& config);

    // Optional: provide world partition for accurate coord<->world conversions.
    void SetWorldPartition(const WorldPartition* partition) { worldPartition_ = partition; }

    // Update (called every frame)
    void Update(float deltaTime);

    // Interest point management
    uint64_t AddInterestPoint(const InterestPoint& point);
    void RemoveInterestPoint(uint64_t id);
    void UpdateInterestPoint(uint64_t id, const InterestPoint& point);
    InterestPoint* GetInterestPoint(uint64_t id);
    const InterestPoint* GetInterestPoint(uint64_t id) const;

    // Convenience methods for common interest points
    uint64_t SetCameraPosition(const Vec3& position, const Vec3& direction, const Vec3& velocity = Vec3(0.0f, 0.0f, 0.0f));
    uint64_t SetPlayerPosition(const Vec3& position, const Vec3& velocity = Vec3(0.0f, 0.0f, 0.0f));
    uint64_t AddCinematicPath(const std::vector<Vec3>& path, float duration);
    uint64_t AddQuestObjective(const Vec3& position, float radius);

    // Interest calculation
    float CalculateCellInterest(const CellCoord& coord) const;
    std::vector<InterestRegion> GetInterestRegions() const;

    // Priority calculation
    float CalculatePriority(const CellCoord& coord, const Vec3& cameraPosition, const Vec3& cameraDirection) const;

    // Configuration
    void SetConfig(const InterestManagerConfig& config) { config_ = config; }
    const InterestManagerConfig& GetConfig() const { return config_; }

    // Statistics
    struct Statistics {
        uint32_t activeInterestPoints = 0;
        uint32_t highInterestCells = 0;
        uint32_t mediumInterestCells = 0;
        uint32_t lowInterestCells = 0;

        uint64_t InterestCellCount() const {
            return static_cast<uint64_t>(highInterestCells) +
                   static_cast<uint64_t>(mediumInterestCells) +
                   static_cast<uint64_t>(lowInterestCells);
        }

        bool HasActiveInterestPoints() const { return activeInterestPoints != 0; }
        bool HasHighInterestCells() const { return highInterestCells != 0; }
        bool HasMediumInterestCells() const { return mediumInterestCells != 0; }
        bool HasLowInterestCells() const { return lowInterestCells != 0; }
        bool HasInterestCells() const { return InterestCellCount() != 0; }
    };

    Statistics GetStatistics() const;

    // Cleanup
    void Shutdown();

    // State check
    bool IsInitialized() const { return initialized_; }

private:
    // Interest calculation
    float CalculateInterestAtPoint(const Vec3& point) const;
    float CalculatePredictedInterest(const CellCoord& coord, const InterestPoint& point) const;
    std::vector<Vec3> PredictPath(const Vec3& position, const Vec3& velocity, float time, uint32_t samples) const;

    // Frustum culling
    bool IsCellInFrustum(const CellCoord& coord, const InterestPoint& point) const;

    // Distance calculation
    float CalculateDistanceToCell(const CellCoord& coord, const Vec3& point) const;
    float CalculateWeightedDistance(float distance, const InterestPoint& point) const;

    // Interest region generation
    void UpdateInterestRegions();
    void UpdateStatistics();
    std::vector<CellCoord> GetCellsInRegion(const Vec3& center, float radius) const;

    // Interest point management
    void UpdateTemporaryPoints(float deltaTime);
    void RemoveExpiredPoints();

    // Configuration
    InterestManagerConfig config_;

    // Interest points
    std::unordered_map<uint64_t, InterestPoint> interestPoints_;
    std::unordered_map<uint64_t, std::vector<Vec3>> cinematicPaths_;  // Stored paths for cinematic interest
    uint64_t nextInterestPointId_;

    // Cached interest regions
    std::vector<InterestRegion> interestRegions_;
    uint64_t lastRegionUpdateFrame_;

    // Statistics
    Statistics stats_;

    // State
    const WorldPartition* worldPartition_ = nullptr;
    uint64_t currentFrame_ = 0;
    uint64_t cameraPointId_ = 0;
    uint64_t playerPointId_ = 0;
    bool initialized_;
};

// ===== Camera Path Prediction =====

class CameraPathPredictor {
public:
    CameraPathPredictor();
    ~CameraPathPredictor();

    // Initialize
    bool Initialize(float predictionTime, uint32_t sampleCount);

    // Update with current camera state
    void Update(const Vec3& position, const Vec3& direction, const Vec3& velocity, float deltaTime);

    // Predict future positions
    std::vector<Vec3> PredictFuturePositions() const;

    // Predict interest regions
    std::vector<InterestRegion> PredictInterestRegions() const;

    // Configuration
    void SetPredictionTime(float time) { predictionTime_ = time; }
    void SetSampleCount(uint32_t count) { sampleCount_ = count; }

private:
    // Path prediction algorithms
    enum class PredictionMethod {
        Linear = 0,         // Simple linear extrapolation
        VelocityBased = 1,  // Based on current velocity
        AccelerationBased = 2,  // Based on acceleration
        CurveBased = 3      // Curve fitting to recent positions
    };

    // History for curve-based prediction
    struct PositionSample {
        Vec3 position;
        Vec3 velocity;
        float time;
    };

    std::vector<PositionSample> positionHistory_;
    size_t maxHistorySize_;

    // Prediction settings
    float predictionTime_;
    uint32_t sampleCount_;
    PredictionMethod predictionMethod_;

    // Current state
    Vec3 currentPosition_;
    Vec3 currentDirection_;
    Vec3 currentVelocity_;
    Vec3 currentAcceleration_;
};

} // namespace Streaming
} // namespace Next
