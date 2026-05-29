#pragma once

#include "next/math/math.h"
#include "next/streaming/world_partition.h"
#include <vector>
#include <unordered_map>
#include <deque>
#include <memory>

namespace Next {
namespace Streaming {

// ===== Prediction Method =====

enum class PredictionMethod : uint32_t {
    Linear = 0,             // Simple linear extrapolation
    VelocityBased = 1,      // Based on current velocity vector
    AccelerationBased = 2,  // Account for acceleration
    CurveBased = 3,         // Curve fitting to recent history
    Hybrid = 4              // Combination of methods
};

// ===== Prefetch Request =====

struct PrefetchRequest {
    CellCoord coord;
    float priority;         // Priority for loading
    float timeToLoad;       // Estimated time until needed (seconds)
    uint32_t frameIndex;    // Frame when request was created

    PrefetchRequest()
        : priority(0.0f)
        , timeToLoad(0.0f)
        , frameIndex(0)
    {}
};

// ===== Prediction Result =====

struct PredictionResult {
    Vec3 predictedPosition;
    Vec3 predictedDirection;
    float confidence;        // 0.0 - 1.0 (how confident we are in this prediction)
    float timeHorizon;       // How far into the future this prediction is
    std::vector<CellCoord> cells;  // Cells that will be needed
};

// ===== Camera History Sample =====

struct CameraSample {
    Vec3 position;
    Vec3 direction;
    Vec3 velocity;
    float timestamp;
    uint64_t frameIndex;

    CameraSample()
        : position(0.0f, 0.0f, 0.0f)
        , direction(0.0f, 0.0f, -1.0f)
        , velocity(0.0f, 0.0f, 0.0f)
        , timestamp(0.0f)
        , frameIndex(0)
    {}
};

// ===== Prediction System Configuration =====

struct PredictionSystemConfig {
    // Prediction settings
    PredictionMethod method = PredictionMethod::Hybrid;
    float predictionTimeHorizon = 2.0f;     // Predict N seconds ahead
    uint32_t predictionSamples = 8;         // Number of samples along prediction path

    // History settings
    uint32_t maxHistorySamples = 60;        // Keep last 60 samples (~1 second at 60fps)
    float historyDuration = 1.0f;           // Keep history for 1 second

    // Confidence settings
    float minConfidence = 0.3f;             // Minimum confidence to prefetch
    float confidenceDecay = 0.1f;           // Confidence decay per second

    // Prefetch settings
    bool enablePrefetch = true;
    float prefetchLeadTime = 1.0f;          // Prefetch N seconds before needed
    uint32_t maxPrefetchRequests = 32;      // Maximum concurrent prefetch requests

    // Velocity prediction
    bool useVelocitySmoothing = true;
    float velocitySmoothingFactor = 0.2f;   // Exponential moving average

    // Curve fitting
    bool useCurveFitting = true;
    uint32_t curveFittingSamples = 10;      // Number of samples for curve fitting

    PredictionSystemConfig() = default;
};

// ===== Prediction System =====

class PredictionSystem {
public:
    PredictionSystem();
    ~PredictionSystem();

    // Initialize with configuration
    bool Initialize(const PredictionSystemConfig& config);

    // Update (called every frame with current camera state)
    void Update(const Vec3& cameraPosition, const Vec3& cameraDirection, float deltaTime, uint64_t frameIndex);

    // Prediction
    std::vector<PredictionResult> PredictFuturePositions(uint32_t sampleCount = 8) const;
    PredictionResult PredictAtTime(float timeInSeconds) const;

    // Prefetch requests
    std::vector<PrefetchRequest> GeneratePrefetchRequests() const;
    void ClearPrefetchRequests();

    // Returns the cached result of the most recent GeneratePrefetchRequests()
    // call. Useful for consumers that want to inspect / dispatch the same
    // request set multiple times per frame without re-running prediction.
    // The cache is invalidated by ClearPrefetchRequests().
    const std::vector<PrefetchRequest>& GetPendingRequests() const { return pendingRequests_; }

    // Camera history
    void AddCameraSample(const CameraSample& sample);
    const CameraSample* GetLatestSample() const;
    std::vector<CameraSample> GetRecentSamples(uint32_t count) const;

    // Velocity calculation
    Vec3 GetCurrentVelocity() const { return currentVelocity_; }
    Vec3 GetSmoothedVelocity() const { return smoothedVelocity_; }
    Vec3 GetAcceleration() const { return currentAcceleration_; }

    // Confidence calculation
    float CalculateConfidence(const PredictionResult& result) const;
    float GetAverageConfidence() const;

    // Configuration
    void SetConfig(const PredictionSystemConfig& config) { config_ = config; }
    const PredictionSystemConfig& GetConfig() const { return config_; }

    // Cell size (in world units). Must be kept in sync with WorldPartitionConfig::cellSize
    // for cell-grid math (PredictCellsAtPath, CalculateTimeToCell) to land in the
    // same partition as the streaming manager.
    void SetCellSize(float cellSize) { if (cellSize > 0.0f) cellSize_ = cellSize; }
    float GetCellSize() const { return cellSize_; }

    // Statistics
    struct Statistics {
        uint32_t prefetchRequestsGenerated;
        uint32_t prefetchRequestsHit;       // Correctly predicted cells
        uint32_t prefetchRequestsMiss;      // Incorrectly predicted cells
        float averageConfidence;
        float predictionAccuracy;            // Percentage of correct predictions
    };

    Statistics GetStatistics() const;
    void ResetStatistics();

    // Cleanup
    void Shutdown();

    // State check
    bool IsInitialized() const { return initialized_; }

private:
    // Prediction algorithms
    Vec3 PredictLinear(float timeInSeconds) const;
    Vec3 PredictVelocityBased(float timeInSeconds) const;
    Vec3 PredictAccelerationBased(float timeInSeconds) const;
    Vec3 PredictCurveBased(float timeInSeconds) const;
    Vec3 PredictHybrid(float timeInSeconds) const;

    // Curve fitting (quadratic Bezier or polynomial)
    struct CurveCoefficients {
        Vec3 a, b, c;  // Quadratic: p(t) = a*t^2 + b*t + c
    };

    CurveCoefficients FitCurveToHistory() const;

    // Velocity calculation
    void UpdateVelocity(const Vec3& newPosition, float deltaTime);

    // Acceleration calculation
    void UpdateAcceleration(const Vec3& newVelocity, float deltaTime);

    // History management
    void PruneHistory();
    Vec3 CalculateAverageVelocity() const;

    // Prefetch generation
    std::vector<CellCoord> PredictCellsAtPath(const Vec3& position, const Vec3& direction, float timeHorizon) const;
    float CalculateTimeToCell(const CellCoord& coord, const Vec3& position, const Vec3& velocity) const;

    // Confidence calculation
    float CalculatePredictionConfidence(const Vec3& predictedPosition) const;
    bool IsPredictionReliable(float confidence) const;

    // Velocity stability for confidence weighting
    float ComputeVelocityStability() const;

    // Configuration
    PredictionSystemConfig config_;

    // Camera history
    std::deque<CameraSample> cameraHistory_;
    CameraSample latestSample_;

    // Current state
    Vec3 currentPosition_;
    Vec3 currentDirection_;
    Vec3 currentVelocity_;
    Vec3 smoothedVelocity_;
    Vec3 currentAcceleration_;
    float elapsedTime_;  // Elapsed time for tracking

    // Cell sizing (must match WorldPartitionConfig::cellSize)
    float cellSize_ = 64.0f;

    // Cached prefetch requests from the most recent generation pass
    mutable std::vector<PrefetchRequest> pendingRequests_;

    // Statistics
    Statistics stats_;
    uint64_t totalPredictions_;
    uint64_t correctPredictions_;

    // State
    bool initialized_;
};

// ===== Cinematic Path Predictor =====

class CinematicPathPredictor {
public:
    CinematicPathPredictor();
    ~CinematicPathPredictor();

    // Initialize
    bool Initialize();

    // Path management
    void SetPath(const std::vector<Vec3>& path, float duration);
    void ClearPath();
    bool HasPath() const { return !path_.empty(); }

    // Update (call with current time along path)
    void Update(float timeAlongPath);

    // Prediction
    Vec3 PredictPositionAtTime(float timeOffset) const;
    std::vector<Vec3> PredictPathSegment(float startTime, float duration) const;
    std::vector<CellCoord> PredictCellsAlongPath(float timeOffset) const;

    // Path state
    float GetPathDuration() const { return pathDuration_; }
    float GetProgress() const { return currentTime_ / pathDuration_; }

private:
    std::vector<Vec3> path_;
    float pathDuration_;
    float currentTime_;
    bool initialized_;
};

// ===== Velocity Extrapolation Predictor =====

class VelocityExtrapolationPredictor {
public:
    VelocityExtrapolationPredictor();
    ~VelocityExtrapolationPredictor();

    // Initialize
    bool Initialize(uint32_t historySize = 30);

    // Update with current state
    void Update(const Vec3& position, const Vec3& velocity, float deltaTime);

    // Predict future position
    Vec3 PredictPosition(float timeInSeconds) const;

    // Predict velocity at future time
    Vec3 PredictVelocity(float timeInSeconds) const;

    // Predict path
    std::vector<Vec3> PredictPath(float duration, uint32_t sampleCount) const;

    // Get extrapolated velocity
    Vec3 GetExtrapolatedVelocity() const { return extrapolatedVelocity_; }

private:
    // History for trend analysis
    struct VelocitySample {
        Vec3 velocity;
        float timestamp;
    };

    std::deque<VelocitySample> velocityHistory_;
    Vec3 extrapolatedVelocity_;
    Vec3 accelerationTrend_;
    uint32_t maxHistorySize_;
    bool initialized_;

    // Calculate acceleration trend from history
    void UpdateAccelerationTrend();
};

// ===== Adaptive Prediction Manager =====

class AdaptivePredictionManager {
public:
    AdaptivePredictionManager();
    ~AdaptivePredictionManager();

    // Initialize
    bool Initialize(const PredictionSystemConfig& config);

    // Update
    void Update(const Vec3& cameraPosition, const Vec3& cameraDirection, float deltaTime, uint64_t frameIndex);

    // Adaptive method selection
    PredictionMethod SelectBestMethod() const;
    void SetMethod(PredictionMethod method);

    // Prediction accuracy feedback
    void RecordPredictionResult(const PredictionResult& result, bool correct);
    float GetMethodAccuracy(PredictionMethod method) const;

    // Prefetch optimization
    std::vector<PrefetchRequest> OptimizePrefetchRequests(const std::vector<PrefetchRequest>& requests) const;

    // Configuration
    void SetConfig(const PredictionSystemConfig& config);

    // Get prediction system
    PredictionSystem* GetPredictionSystem() { return predictionSystem_.get(); }

    // Cleanup
    void Shutdown();

private:
    // Method accuracy tracking
    struct MethodAccuracy {
        uint32_t correctPredictions;
        uint32_t totalPredictions;
        float accuracy;
    };

    std::unordered_map<PredictionMethod, MethodAccuracy> methodAccuracy_;
    PredictionMethod currentMethod_;

    std::unique_ptr<PredictionSystem> predictionSystem_;
    bool initialized_;
};

} // namespace Streaming
} // namespace Next
