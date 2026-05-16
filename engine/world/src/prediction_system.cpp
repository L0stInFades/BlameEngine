#include "next/streaming/prediction_system.h"
#include "next/foundation/logger.h"
#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <tuple>

namespace Next {
namespace Streaming {

// ===== Prediction System Implementation =====

PredictionSystem::PredictionSystem()
    : currentVelocity_(0.0f, 0.0f, 0.0f)
    , smoothedVelocity_(0.0f, 0.0f, 0.0f)
    , currentAcceleration_(0.0f, 0.0f, 0.0f)
    , elapsedTime_(0.0f)
    , totalPredictions_(0)
    , correctPredictions_(0)
    , initialized_(false)
{
}

PredictionSystem::~PredictionSystem() {
    Shutdown();
}

bool PredictionSystem::Initialize(const PredictionSystemConfig& config) {
    if (initialized_) {
        NEXT_LOG_WARNING("PredictionSystem already initialized");
        return true;
    }

    config_ = config;
    elapsedTime_ = 0.0f;

    // Note: std::deque doesn't have reserve(), it's already efficient
    // cameraHistory_.reserve(config_.maxHistorySamples);

    // Reset statistics
    stats_ = Statistics();
    totalPredictions_ = 0;
    correctPredictions_ = 0;

    NEXT_LOG_INFO("PredictionSystem initialized (CP7: World Streaming)");
    NEXT_LOG_INFO("  Method: %u", static_cast<uint32_t>(config_.method));
    NEXT_LOG_INFO("  Prediction time horizon: %.2f seconds", config_.predictionTimeHorizon);
    NEXT_LOG_INFO("  Prediction samples: %u", config_.predictionSamples);

    initialized_ = true;
    return true;
}

void PredictionSystem::Update(const Vec3& cameraPosition, const Vec3& cameraDirection, float deltaTime, uint64_t frameIndex) {
    if (!initialized_) {
        return;
    }

    // Add camera sample to history
    CameraSample sample;
    sample.position = cameraPosition;
    sample.direction = cameraDirection;
    sample.velocity = currentVelocity_;
    sample.timestamp = elapsedTime_;
    sample.frameIndex = frameIndex;

    AddCameraSample(sample);
    latestSample_ = sample;

    // Update position and velocity
    currentPosition_ = cameraPosition;
    currentDirection_ = cameraDirection;

    UpdateVelocity(cameraPosition, deltaTime);

    // Update rolling average confidence (EMA over per-frame velocity stability).
    const float frameStability = ComputeVelocityStability();
    constexpr float kConfidenceEmaAlpha = 0.1f;
    stats_.averageConfidence = stats_.averageConfidence * (1.0f - kConfidenceEmaAlpha) +
                               frameStability * kConfidenceEmaAlpha;

    elapsedTime_ += deltaTime;
}

std::vector<PredictionResult> PredictionSystem::PredictFuturePositions(uint32_t sampleCount) const {
    std::vector<PredictionResult> results;
    results.reserve(sampleCount);

    uint32_t samples = sampleCount > 0 ? sampleCount : config_.predictionSamples;
    float timeStep = config_.predictionTimeHorizon / samples;

    for (uint32_t i = 0; i < samples; ++i) {
        float time = timeStep * i;
        PredictionResult result;
        result.predictedPosition = PredictHybrid(time);
        result.predictedDirection = currentDirection_;
        result.confidence = CalculateConfidence(result);
        result.timeHorizon = time;
        result.cells = PredictCellsAtPath(result.predictedPosition,
                                          result.predictedDirection,
                                          time);

        results.push_back(result);
    }

    return results;
}

PredictionResult PredictionSystem::PredictAtTime(float timeInSeconds) const {
    PredictionResult result;
    result.predictedPosition = PredictHybrid(timeInSeconds);  // Use PredictHybrid instead of missing method
    result.predictedDirection = currentDirection_;
    result.confidence = CalculateConfidence(result);
    result.timeHorizon = timeInSeconds;

    return result;
}

std::vector<PrefetchRequest> PredictionSystem::GeneratePrefetchRequests() const {
    std::vector<PrefetchRequest> requests;

    // Predict future positions
    std::vector<PredictionResult> predictions = PredictFuturePositions(config_.predictionSamples);

    // Deduplicate prefetch requests across all sample points along the
    // predicted path: the same cell can be touched by multiple time slices.
    std::unordered_map<CellCoord, PrefetchRequest, CellCoord::Hash> uniqueRequests;
    const uint32_t currentFrame = static_cast<uint32_t>(latestSample_.frameIndex);
    const uint32_t maxRequests = config_.maxPrefetchRequests;

    for (const auto& prediction : predictions) {
        if (prediction.confidence < config_.minConfidence) {
            continue;
        }

        const std::vector<CellCoord>& cells = !prediction.cells.empty()
            ? prediction.cells
            : PredictCellsAtPath(prediction.predictedPosition,
                                 prediction.predictedDirection,
                                 prediction.timeHorizon);

        for (const CellCoord& coord : cells) {
            const float timeToLoad = CalculateTimeToCell(coord,
                                                         currentPosition_,
                                                         smoothedVelocity_);

            auto existing = uniqueRequests.find(coord);
            if (existing == uniqueRequests.end()) {
                PrefetchRequest request;
                request.coord = coord;
                request.priority = prediction.confidence;
                request.timeToLoad = timeToLoad;
                request.frameIndex = currentFrame;
                uniqueRequests.emplace(coord, request);
            } else {
                // Keep highest priority and earliest needed time.
                if (prediction.confidence > existing->second.priority) {
                    existing->second.priority = prediction.confidence;
                }
                if (timeToLoad < existing->second.timeToLoad) {
                    existing->second.timeToLoad = timeToLoad;
                }
            }
        }
    }

    requests.reserve(uniqueRequests.size());
    for (auto& [coord, request] : uniqueRequests) {
        requests.push_back(request);
    }

    // Highest priority first; tie-break on earliest time-to-load.
    std::sort(requests.begin(), requests.end(),
              [](const PrefetchRequest& a, const PrefetchRequest& b) {
                  if (a.priority != b.priority) return a.priority > b.priority;
                  return a.timeToLoad < b.timeToLoad;
              });

    if (maxRequests > 0 && requests.size() > maxRequests) {
        requests.resize(maxRequests);
    }

    pendingRequests_ = requests;
    return requests;
}

void PredictionSystem::ClearPrefetchRequests() {
    pendingRequests_.clear();
    stats_.prefetchRequestsGenerated = 0;
    stats_.prefetchRequestsHit = 0;
    stats_.prefetchRequestsMiss = 0;
}

void PredictionSystem::AddCameraSample(const CameraSample& sample) {
    cameraHistory_.push_back(sample);

    if (cameraHistory_.size() > config_.maxHistorySamples) {
        cameraHistory_.pop_front();
    }

    // Prune old samples
    PruneHistory();
}

const CameraSample* PredictionSystem::GetLatestSample() const {
    if (cameraHistory_.empty()) {
        return nullptr;
    }
    return &cameraHistory_.back();
}

std::vector<CameraSample> PredictionSystem::GetRecentSamples(uint32_t count) const {
    std::vector<CameraSample> samples;

    uint32_t numSamples = std::min(static_cast<uint32_t>(cameraHistory_.size()), count);
    samples.reserve(numSamples);

    auto it = cameraHistory_.end();
    for (uint32_t i = 0; i < numSamples; ++i) {
        --it;
        samples.push_back(*it);
    }

    return samples;
}

float PredictionSystem::CalculateConfidence(const PredictionResult& result) const {
    return CalculatePredictionConfidence(result.predictedPosition);
}

float PredictionSystem::GetAverageConfidence() const {
    return stats_.averageConfidence;
}

PredictionSystem::Statistics PredictionSystem::GetStatistics() const {
    return stats_;
}

void PredictionSystem::ResetStatistics() {
    stats_ = Statistics();
    totalPredictions_ = 0;
    correctPredictions_ = 0;
}

void PredictionSystem::Shutdown() {
    if (!initialized_) {
        return;
    }

    cameraHistory_.clear();

    initialized_ = false;
    NEXT_LOG_INFO("PredictionSystem shutdown complete");
}

// ===== Private Methods =====

Vec3 PredictionSystem::PredictLinear(float timeInSeconds) const {
    return currentPosition_ + currentVelocity_ * timeInSeconds;
}

Vec3 PredictionSystem::PredictVelocityBased(float timeInSeconds) const {
    return currentPosition_ + smoothedVelocity_ * timeInSeconds;
}

Vec3 PredictionSystem::PredictAccelerationBased(float timeInSeconds) const {
    // p = p0 + v0*t + 0.5*a*t^2
    return currentPosition_ + currentVelocity_ * timeInSeconds + currentAcceleration_ * 0.5f * timeInSeconds * timeInSeconds;
}

Vec3 PredictionSystem::PredictCurveBased(float timeInSeconds) const {
    CurveCoefficients coeffs = FitCurveToHistory();
    float t = timeInSeconds;

    // Quadratic: p(t) = a*t^2 + b*t + c
    return coeffs.a * t * t + coeffs.b * t + coeffs.c;
}

Vec3 PredictionSystem::PredictHybrid(float timeInSeconds) const {
    // Use different methods based on time horizon
    if (timeInSeconds < 0.5f) {
        return PredictVelocityBased(timeInSeconds);
    } else if (timeInSeconds < 1.5f) {
        return PredictAccelerationBased(timeInSeconds);
    } else {
        return PredictCurveBased(timeInSeconds);
    }
}

PredictionSystem::CurveCoefficients PredictionSystem::FitCurveToHistory() const {
    CurveCoefficients coeffs;
    coeffs.c = currentPosition_;

    if (cameraHistory_.size() < 3) {
        coeffs.a = Vec3(0.0f, 0.0f, 0.0f);
        coeffs.b = currentVelocity_;
        return coeffs;
    }

    // Quadratic least squares fit p(t) = a*t^2 + b*t + c per axis.
    // Time origin is the latest sample, so all sample times are <= 0 and
    // forward prediction uses t > 0 in PredictCurveBased.
    const size_t maxSamples = config_.curveFittingSamples > 0
        ? static_cast<size_t>(config_.curveFittingSamples)
        : static_cast<size_t>(10);
    const size_t n = std::min(cameraHistory_.size(), maxSamples);
    const float t0 = cameraHistory_.back().timestamp;

    double St0 = 0.0, St1 = 0.0, St2 = 0.0, St3 = 0.0, St4 = 0.0;
    double Spx0 = 0.0, Spx1 = 0.0, Spx2 = 0.0;
    double Spy0 = 0.0, Spy1 = 0.0, Spy2 = 0.0;
    double Spz0 = 0.0, Spz1 = 0.0, Spz2 = 0.0;

    auto it = cameraHistory_.end();
    for (size_t i = 0; i < n; ++i) {
        --it;
        const double t = static_cast<double>(it->timestamp - t0);
        const double t2 = t * t;
        const double t3 = t2 * t;
        const double t4 = t2 * t2;

        St0 += 1.0;
        St1 += t;
        St2 += t2;
        St3 += t3;
        St4 += t4;

        Spx0 += static_cast<double>(it->position.x);
        Spx1 += static_cast<double>(it->position.x) * t;
        Spx2 += static_cast<double>(it->position.x) * t2;

        Spy0 += static_cast<double>(it->position.y);
        Spy1 += static_cast<double>(it->position.y) * t;
        Spy2 += static_cast<double>(it->position.y) * t2;

        Spz0 += static_cast<double>(it->position.z);
        Spz1 += static_cast<double>(it->position.z) * t;
        Spz2 += static_cast<double>(it->position.z) * t2;
    }

    auto det3 = [](double m00, double m01, double m02,
                   double m10, double m11, double m12,
                   double m20, double m21, double m22) {
        return m00 * (m11 * m22 - m12 * m21)
             - m01 * (m10 * m22 - m12 * m20)
             + m02 * (m10 * m21 - m11 * m20);
    };

    const double D = det3(St4, St3, St2,
                          St3, St2, St1,
                          St2, St1, St0);

    if (std::abs(D) < 1e-12) {
        // Degenerate Vandermonde-like system (e.g. dt~0 across history);
        // fall back to physics-based extrapolation.
        coeffs.a = currentAcceleration_ * 0.5f;
        coeffs.b = currentVelocity_;
        return coeffs;
    }

    const double invD = 1.0 / D;

    auto solveAxis = [&](double Sp0, double Sp1, double Sp2,
                         float& a, float& b, float& c) {
        const double Da = det3(Sp2, St3, St2,
                               Sp1, St2, St1,
                               Sp0, St1, St0);
        const double Db = det3(St4, Sp2, St2,
                               St3, Sp1, St1,
                               St2, Sp0, St0);
        const double Dc = det3(St4, St3, Sp2,
                               St3, St2, Sp1,
                               St2, St1, Sp0);
        a = static_cast<float>(Da * invD);
        b = static_cast<float>(Db * invD);
        c = static_cast<float>(Dc * invD);
    };

    float ax, bx, cx, ay, by, cy, az, bz, cz;
    solveAxis(Spx0, Spx1, Spx2, ax, bx, cx);
    solveAxis(Spy0, Spy1, Spy2, ay, by, cy);
    solveAxis(Spz0, Spz1, Spz2, az, bz, cz);

    coeffs.a = Vec3(ax, ay, az);
    coeffs.b = Vec3(bx, by, bz);
    coeffs.c = Vec3(cx, cy, cz);

    return coeffs;
}

void PredictionSystem::UpdateVelocity(const Vec3& newPosition, float deltaTime) {
    if (deltaTime <= 0.0f) {
        return;
    }

    Vec3 diff = newPosition - currentPosition_;
    Vec3 newVelocity = diff * (1.0f / deltaTime);  // Manual division to avoid operator/

    // Calculate acceleration
    UpdateAcceleration(newVelocity, deltaTime);

    currentVelocity_ = newVelocity;

    // Apply smoothing
    if (config_.useVelocitySmoothing) {
        smoothedVelocity_ = smoothedVelocity_ * (1.0f - config_.velocitySmoothingFactor) +
                           newVelocity * config_.velocitySmoothingFactor;
    } else {
        smoothedVelocity_ = newVelocity;
    }
}
void PredictionSystem::UpdateAcceleration(const Vec3& newVelocity, float deltaTime) {
    if (deltaTime <= 0.0f) {
        return;
    }

    Vec3 velocityDiff = newVelocity - currentVelocity_;
    currentAcceleration_ = velocityDiff * (1.0f / deltaTime);  // Manual division
}

void PredictionSystem::PruneHistory() {
    if (cameraHistory_.empty()) {
        return;
    }

    float currentTime = cameraHistory_.back().timestamp;
    float maxAge = config_.historyDuration;

    while (!cameraHistory_.empty() && (currentTime - cameraHistory_.front().timestamp) > maxAge) {
        cameraHistory_.pop_front();
    }
}

Vec3 PredictionSystem::CalculateAverageVelocity() const {
    if (cameraHistory_.empty()) {
        return currentVelocity_;
    }

    Vec3 sum(0.0f, 0.0f, 0.0f);
    for (const auto& sample : cameraHistory_) {
        sum = sum + sample.velocity;
    }

    return sum * (1.0f / cameraHistory_.size());
}

std::vector<CellCoord> PredictionSystem::PredictCellsAtPath(const Vec3& position, const Vec3& direction, float timeHorizon) const {
    std::vector<CellCoord> cells;

    const float invCellSize = 1.0f / cellSize_;
    auto worldToCell = [invCellSize](const Vec3& p) {
        return CellCoord(
            static_cast<int32_t>(std::floor(p.x * invCellSize)),
            static_cast<int32_t>(std::floor(p.z * invCellSize))
        );
    };

    std::unordered_set<CellCoord, CellCoord::Hash> seen;
    auto pushIfNew = [&](const CellCoord& c) {
        if (seen.insert(c).second) {
            cells.push_back(c);
        }
    };

    // Start cell.
    pushIfNew(worldToCell(position));

    // Walk along the predicted path: integrate either the smoothed velocity
    // (preferred when available) or the supplied direction at the current
    // smoothed speed, sampling at predictionSamples intervals.
    const Vec3 walkDir = smoothedVelocity_.Length() > 1e-3f
        ? smoothedVelocity_
        : direction * std::max(currentVelocity_.Length(), 1.0f);

    const uint32_t steps = std::max<uint32_t>(config_.predictionSamples, 4u);
    const float stepDt = timeHorizon > 0.0f ? timeHorizon / static_cast<float>(steps) : 0.0f;

    for (uint32_t i = 1; i <= steps; ++i) {
        const float t = stepDt * static_cast<float>(i);
        const Vec3 sample = position + walkDir * t;
        pushIfNew(worldToCell(sample));
    }

    // Add forward neighbour cells along the velocity vector to cover diagonals
    // missed by the discrete step sampling.
    if (walkDir.Length() > 1e-3f) {
        const Vec3 dir = walkDir.Normalize();
        const Vec3 endPos = position + walkDir * timeHorizon;
        const CellCoord endCell = worldToCell(endPos);
        const int32_t dx = dir.x >= 0.0f ? 1 : -1;
        const int32_t dz = dir.z >= 0.0f ? 1 : -1;
        pushIfNew(CellCoord(endCell.x + dx, endCell.z));
        pushIfNew(CellCoord(endCell.x, endCell.z + dz));
    }

    return cells;
}

float PredictionSystem::CalculateTimeToCell(const CellCoord& coord, const Vec3& position, const Vec3& velocity) const {
    // World position of the cell center.
    const Vec3 cellCenter(
        (static_cast<float>(coord.x) + 0.5f) * cellSize_,
        position.y,
        (static_cast<float>(coord.z) + 0.5f) * cellSize_
    );

    const Vec3 toCell = cellCenter - position;
    const float distance = toCell.Length();

    const float speed = velocity.Length();
    if (speed < 1e-3f) {
        // No meaningful velocity — fall back to a large but finite estimate
        // proportional to distance so callers can still rank by urgency.
        return distance * 0.1f + config_.predictionTimeHorizon;
    }

    // Project displacement onto velocity direction; if behind us, treat as the
    // raw distance/speed (cell will still be reached if the camera turns back).
    const float along = toCell.Dot(velocity) / speed;
    const float effectiveDistance = along > 0.0f ? along : distance;
    return effectiveDistance / speed;
}

float PredictionSystem::CalculatePredictionConfidence(const Vec3& predictedPosition) const {
    (void)predictedPosition;
    // Combine rolling velocity stability with horizon-based decay.
    const float stability = stats_.averageConfidence > 0.0f
        ? stats_.averageConfidence
        : ComputeVelocityStability();
    const float horizonDecay = 1.0f - (config_.confidenceDecay * config_.predictionTimeHorizon);
    const float confidence = stability * horizonDecay;
    return std::max(0.0f, std::min(1.0f, confidence));
}

bool PredictionSystem::IsPredictionReliable(float confidence) const {
    return confidence >= config_.minConfidence;
}

float PredictionSystem::ComputeVelocityStability() const {
    // With no/limited history we are uncertain — return mid-range confidence
    // so prefetching still occurs but with conservative weighting.
    if (cameraHistory_.size() < 3) {
        return 0.5f;
    }

    double meanSpeed = 0.0;
    size_t count = 0;
    for (const auto& sample : cameraHistory_) {
        meanSpeed += static_cast<double>(sample.velocity.Length());
        ++count;
    }
    meanSpeed /= static_cast<double>(count);

    double variance = 0.0;
    for (const auto& sample : cameraHistory_) {
        const double d = static_cast<double>(sample.velocity.Length()) - meanSpeed;
        variance += d * d;
    }
    variance /= static_cast<double>(count);
    const double stddev = std::sqrt(variance);

    // Coefficient of variation: stable motion -> small ratio -> high confidence.
    const double denom = std::max(meanSpeed, 1e-3);
    const double cv = stddev / denom;
    double confidence = 1.0 - cv;
    if (confidence < 0.0) confidence = 0.0;
    if (confidence > 1.0) confidence = 1.0;
    return static_cast<float>(confidence);
}

} // namespace Streaming
} // namespace Next
