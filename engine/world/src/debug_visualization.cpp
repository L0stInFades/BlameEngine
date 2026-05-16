#include "next/streaming/debug_visualization.h"
#include "next/foundation/logger.h"
#include <algorithm>
#include <sstream>
#include <limits>
#include <cfloat>
#include <chrono>
#include <cstdint>
#include <cmath>
#include <fstream>
#include <iomanip>

namespace Next {
namespace Streaming {

namespace {

constexpr float kFrameTransientLifetime = 0.1f;  // ~6 frames @ 60fps

// Convert a CellCoord to its world-space (x, z) min corner using a runtime
// cellSize so debug overlays follow whatever the streaming partition uses.
Vec3 CellMinCorner(const CellCoord& coord, float cellSize) {
    return Vec3(static_cast<float>(coord.x) * cellSize,
                0.0f,
                static_cast<float>(coord.z) * cellSize);
}

Vec3 CellCenter(const CellCoord& coord, float cellSize) {
    return Vec3((static_cast<float>(coord.x) + 0.5f) * cellSize,
                0.0f,
                (static_cast<float>(coord.z) + 0.5f) * cellSize);
}

float NowSeconds() {
    using clock = std::chrono::steady_clock;
    static const auto epoch = clock::now();
    const auto delta = clock::now() - epoch;
    return std::chrono::duration<float>(delta).count();
}

// Map a heatmap intensity in [0, 1] to a blue->yellow->red gradient.
Vec3 HeatmapGradient(float intensity) {
    if (intensity <= 0.0f) return Vec3(0.0f, 0.0f, 0.0f);
    if (intensity >= 1.0f) return Vec3(1.0f, 0.0f, 0.0f);
    if (intensity < 0.5f) {
        const float t = intensity * 2.0f;  // 0..1 within blue->yellow band
        return Vec3(t, t, 1.0f - t);
    }
    const float t = (intensity - 0.5f) * 2.0f;  // 0..1 within yellow->red band
    return Vec3(1.0f, 1.0f - t, 0.0f);
}

} // namespace

// ===== Debug Visualization System Implementation =====

DebugVisualizationSystem::DebugVisualizationSystem()
    : currentFrame_(0)
    , initialized_(false)
{
}

DebugVisualizationSystem::~DebugVisualizationSystem() {
    Shutdown();
}

bool DebugVisualizationSystem::Initialize(const DebugVisualizationConfig& config) {
    if (initialized_) {
        NEXT_LOG_WARNING("DebugVisualizationSystem already initialized");
        return true;
    }

    config_ = config;

    NEXT_LOG_INFO("DebugVisualizationSystem initialized (CP7: World Streaming)");
    NEXT_LOG_INFO("  Visualization mode: %u", static_cast<uint32_t>(config_.mode));
    NEXT_LOG_INFO("  Enabled: %s", config_.enabled ? "yes" : "no");

    initialized_ = true;
    return true;
}

void DebugVisualizationSystem::Update(float deltaTime, const StreamingManager* streamingManager) {
    if (!initialized_ || !config_.enabled) {
        return;
    }

    currentFrame_++;

    // Update debug elements
    UpdateDebugElements(deltaTime);

    // Update heatmap
    if (config_.enableHeatmap) {
        UpdateHeatmapDecay();
    }
}

void DebugVisualizationSystem::Render(const Mat4& viewMatrix, const Mat4& projectionMatrix) {
    if (!initialized_ || !config_.enabled) {
        return;
    }

    // Render based on visualization mode
    switch (config_.mode) {
        case VisualizationMode::LoadState:
            RenderLoadStateMode(viewMatrix, projectionMatrix);
            break;

        case VisualizationMode::Priority:
            RenderPriorityMode(viewMatrix, projectionMatrix);
            break;

        case VisualizationMode::MemoryUsage:
            RenderMemoryUsageMode(viewMatrix, projectionMatrix);
            break;

        case VisualizationMode::LOD:
            RenderLODMode(viewMatrix, projectionMatrix);
            break;

        case VisualizationMode::Interest:
            RenderInterestMode(viewMatrix, projectionMatrix);
            break;

        case VisualizationMode::Prediction:
            RenderPredictionMode(viewMatrix, projectionMatrix);
            break;

        case VisualizationMode::IO:
            RenderIOMode(viewMatrix, projectionMatrix);
            break;

        case VisualizationMode::Heatmap:
            RenderHeatmapMode(viewMatrix, projectionMatrix);
            break;

        default:
            break;
    }

    // Render debug elements
    RenderDebugLines(viewMatrix, projectionMatrix);
    RenderDebugBoxes(viewMatrix, projectionMatrix);
    RenderDebugText(viewMatrix, projectionMatrix);
}

void DebugVisualizationSystem::SetVisualizationMode(VisualizationMode mode) {
    config_.mode = mode;
}

void DebugVisualizationSystem::DrawLine(const Vec3& start, const Vec3& end, const Vec3& color, float lifetime) {
    DebugLine line;
    line.start = start;
    line.end = end;
    line.color = color;
    line.lifetime = lifetime;

    lines_.push_back(line);
}

void DebugVisualizationSystem::DrawBox(const Vec3& boundsMin, const Vec3& boundsMax, const Vec3& color, float alpha, float lifetime) {
    DebugBox box;
    box.boundsMin = boundsMin;
    box.boundsMax = boundsMax;
    box.color = color;
    box.alpha = alpha;
    box.lifetime = lifetime;

    boxes_.push_back(box);
}

void DebugVisualizationSystem::DrawText(const Vec3& position, const std::string& text, const Vec3& color, float size, float lifetime) {
    DebugText debugText;
    debugText.position = position;
    debugText.text = text;
    debugText.color = color;
    debugText.size = size;
    debugText.lifetime = lifetime;

    texts_.push_back(debugText);
}

void DebugVisualizationSystem::ClearDebugElements() {
    lines_.clear();
    boxes_.clear();
    texts_.clear();
}

void DebugVisualizationSystem::ClearTemporaryElements() {
    // Remove elements with lifetime > 0
    lines_.erase(
        std::remove_if(lines_.begin(), lines_.end(),
            [](const DebugLine& line) { return line.lifetime > 0.0f; }),
        lines_.end()
    );

    boxes_.erase(
        std::remove_if(boxes_.begin(), boxes_.end(),
            [](const DebugBox& box) { return box.lifetime > 0.0f; }),
        boxes_.end()
    );

    texts_.erase(
        std::remove_if(texts_.begin(), texts_.end(),
            [](const DebugText& text) { return text.lifetime > 0.0f; }),
        texts_.end()
    );
}

void DebugVisualizationSystem::UpdateCellVisualization(const CellCoord& coord, const CellVisualizationData& data) {
    cellData_[coord] = data;
}

CellVisualizationData* DebugVisualizationSystem::GetCellVisualization(const CellCoord& coord) {
    auto it = cellData_.find(coord);
    if (it != cellData_.end()) {
        return &it->second;
    }
    return nullptr;
}

void DebugVisualizationSystem::DrawStatisticsOverlay(const StreamingStatistics& stats) {
    if (!config_.showOverlay) {
        return;
    }

    std::ostringstream oss;
    oss << "Streaming"
        << " loaded=" << stats.loadedCells
        << " loading=" << stats.loadingCells
        << " queued=" << stats.queuedCells
        << " visible=" << stats.visibleCells;
    Vec3 textPos(config_.overlayPosition.x, config_.overlayPosition.y, 0.0f);
    DrawText(textPos, oss.str(), Vec3(1.0f, 1.0f, 1.0f),
             config_.overlaySize, kFrameTransientLifetime);

    std::ostringstream oss2;
    oss2 << "Memory " << (stats.memoryUsed / (1024 * 1024)) << "MB / "
         << (stats.memoryBudget / (1024 * 1024)) << "MB ("
         << static_cast<int>(stats.memoryUtilization * 100.0f) << "%)";
    Vec3 memPos(config_.overlayPosition.x,
                config_.overlayPosition.y + config_.overlaySize + 2.0f,
                0.0f);
    DrawText(memPos, oss2.str(), Vec3(1.0f, 1.0f, 0.6f),
             config_.overlaySize, kFrameTransientLifetime);

    std::ostringstream oss3;
    oss3 << "LOD high=" << stats.highDetailCells
         << " low=" << stats.lowDetailCells
         << " hlod=" << stats.hlodCells
         << " errors=" << stats.failedLoads;
    Vec3 lodPos(config_.overlayPosition.x,
                config_.overlayPosition.y + (config_.overlaySize + 2.0f) * 2.0f,
                0.0f);
    DrawText(lodPos, oss3.str(), Vec3(0.7f, 1.0f, 0.7f),
             config_.overlaySize, kFrameTransientLifetime);
}

void DebugVisualizationSystem::DrawPerformanceMetrics(const IOStatistics& ioStats) {
    if (!config_.showOverlay) {
        return;
    }

    std::ostringstream oss;
    oss.setf(std::ios::fixed);
    oss << std::setprecision(2)
        << "IO read=" << ioStats.averageReadSpeedMBps << "MB/s"
        << " write=" << ioStats.averageWriteSpeedMBps << "MB/s"
        << " decomp=" << ioStats.averageDecompressSpeedMBps << "MB/s";
    Vec3 line1(config_.overlayPosition.x,
               config_.overlayPosition.y + (config_.overlaySize + 2.0f) * 4.0f,
               0.0f);
    DrawText(line1, oss.str(), Vec3(0.7f, 0.9f, 1.0f),
             config_.overlaySize, kFrameTransientLifetime);

    std::ostringstream oss2;
    oss2 << "IO pending r=" << ioStats.pendingReads
         << " w=" << ioStats.pendingWrites
         << " d=" << ioStats.pendingDecompressions
         << " errors=" << ioStats.failedOperations;
    Vec3 line2(config_.overlayPosition.x,
               config_.overlayPosition.y + (config_.overlaySize + 2.0f) * 5.0f,
               0.0f);
    DrawText(line2, oss2.str(), Vec3(1.0f, 0.6f, 0.6f),
             config_.overlaySize, kFrameTransientLifetime);
}

void DebugVisualizationSystem::UpdateHeatmap(const Vec3& position, float intensity) {
    // Quantize the world position into a heatmap cell and accumulate intensity.
    const float cellSize = config_.cellSize > 0.0f ? config_.cellSize : 64.0f;
    CellCoord coord(static_cast<int32_t>(std::floor(position.x / cellSize)),
                    static_cast<int32_t>(std::floor(position.z / cellSize)));

    HeatmapCell& cell = heatmap_[coord];
    cell.intensity += intensity * config_.heatmapIntensity;
    if (cell.intensity > 1.0f) cell.intensity = 1.0f;
    cell.lastUpdateFrame = currentFrame_;
}

Vec3 DebugVisualizationSystem::GetHeatmapColor(const Vec3& position) const {
    const float cellSize = config_.cellSize > 0.0f ? config_.cellSize : 64.0f;
    CellCoord coord(static_cast<int32_t>(std::floor(position.x / cellSize)),
                    static_cast<int32_t>(std::floor(position.z / cellSize)));
    auto it = heatmap_.find(coord);
    if (it == heatmap_.end()) {
        return Vec3(0.0f, 0.0f, 0.0f);
    }
    return HeatmapGradient(it->second.intensity);
}

void DebugVisualizationSystem::CaptureVisualization(const std::string& filename) {
    // Snapshot the current debug element buffers and cell visualization state
    // to a text file the renderer-free tooling can ingest. Real screenshot
    // capture would happen on the renderer side; this is the CPU-side dump.
    std::ofstream out(filename);
    if (!out) {
        NEXT_LOG_WARNING("DebugVisualization: cannot open capture file %s", filename.c_str());
        return;
    }

    out << "# Streaming debug capture (frame " << currentFrame_ << ")\n";
    out << "[cells]\n";
    for (const auto& [coord, data] : cellData_) {
        out << coord.x << "," << coord.z
            << " state=" << static_cast<uint32_t>(data.loadState)
            << " priority=" << data.priority
            << " mem=" << data.memoryUsage
            << " lod=" << data.currentLOD
            << " hlod=" << (data.isHLOD ? 1 : 0)
            << " impostor=" << (data.isImpostor ? 1 : 0)
            << "\n";
    }

    out << "[heatmap]\n";
    for (const auto& [coord, cell] : heatmap_) {
        out << coord.x << "," << coord.z
            << " intensity=" << cell.intensity
            << " lastFrame=" << cell.lastUpdateFrame
            << "\n";
    }

    out << "[lines]\n";
    for (const auto& line : lines_) {
        out << line.start.x << "," << line.start.y << "," << line.start.z
            << " -> " << line.end.x << "," << line.end.y << "," << line.end.z
            << " color=" << line.color.x << "," << line.color.y << "," << line.color.z
            << "\n";
    }

    out << "[boxes]\n";
    for (const auto& box : boxes_) {
        out << box.boundsMin.x << "," << box.boundsMin.y << "," << box.boundsMin.z
            << " -> " << box.boundsMax.x << "," << box.boundsMax.y << "," << box.boundsMax.z
            << " alpha=" << box.alpha
            << "\n";
    }

    out << "[texts]\n";
    for (const auto& text : texts_) {
        out << text.position.x << "," << text.position.y << "," << text.position.z
            << " size=" << text.size
            << " text=\"" << text.text << "\"\n";
    }
}

void DebugVisualizationSystem::Shutdown() {
    if (!initialized_) {
        return;
    }

    ClearDebugElements();
    cellData_.clear();
    heatmap_.clear();

    initialized_ = false;
    NEXT_LOG_INFO("DebugVisualizationSystem shutdown complete");
}

// ===== Private Methods =====

void DebugVisualizationSystem::RenderLoadStateMode(const Mat4& viewMatrix, const Mat4& projectionMatrix) {
    for (const auto& [coord, data] : cellData_) {
        Vec3 color = GetColorForLoadState(data.loadState);
        DrawCellBorder(coord, color, 0.5f);

        if (config_.showCellText) {
            std::ostringstream oss;
            oss << "[" << coord.x << "," << coord.z << "]";

            const char* stateName = "";
            switch (data.loadState) {
                case CellLoadState::Unloaded: stateName = "Unloaded"; break;
                case CellLoadState::Queued: stateName = "Queued"; break;
                case CellLoadState::Loading: stateName = "Loading"; break;
                case CellLoadState::Loaded: stateName = "Loaded"; break;
                case CellLoadState::Unloading: stateName = "Unloading"; break;
                case CellLoadState::Error: stateName = "Error"; break;
                default: stateName = "Unknown"; break;
            }

            DrawText(data.worldPosition, oss.str() + " " + stateName);
        }
    }
}

void DebugVisualizationSystem::RenderPriorityMode(const Mat4& viewMatrix, const Mat4& projectionMatrix) {
    (void)viewMatrix;
    (void)projectionMatrix;

    if (cellData_.empty()) return;

    float minPri = std::numeric_limits<float>::max();
    float maxPri = -std::numeric_limits<float>::max();
    for (const auto& [coord, data] : cellData_) {
        if (data.priority < minPri) minPri = data.priority;
        if (data.priority > maxPri) maxPri = data.priority;
    }

    for (const auto& [coord, data] : cellData_) {
        const Vec3 color = GetColorForPriority(data.priority, minPri, maxPri);
        DrawCellBorder(coord, color, 0.6f);

        if (config_.showCellText) {
            std::ostringstream oss;
            oss << "p=" << std::fixed << std::setprecision(2) << data.priority;
            DrawCellText(coord, oss.str());
        }
    }
}

void DebugVisualizationSystem::RenderMemoryUsageMode(const Mat4& viewMatrix, const Mat4& projectionMatrix) {
    (void)viewMatrix;
    (void)projectionMatrix;

    if (cellData_.empty()) return;

    uint64_t maxUsage = 1;
    for (const auto& [coord, data] : cellData_) {
        if (data.memoryUsage > maxUsage) maxUsage = data.memoryUsage;
    }

    for (const auto& [coord, data] : cellData_) {
        const Vec3 color = GetColorForMemoryUsage(data.memoryUsage, maxUsage);
        DrawCellBorder(coord, color, 0.6f);

        if (config_.showCellText) {
            std::ostringstream oss;
            oss << (data.memoryUsage / 1024) << "KB";
            DrawCellText(coord, oss.str());
        }
    }
}

void DebugVisualizationSystem::RenderLODMode(const Mat4& viewMatrix, const Mat4& projectionMatrix) {
    (void)viewMatrix;
    (void)projectionMatrix;

    for (const auto& [coord, data] : cellData_) {
        Vec3 color = GetColorForLOD(data.currentLOD);
        // Darken impostors so the eye can distinguish them from regular LOD steps.
        if (data.isImpostor) {
            color = color * 0.5f;
        }
        DrawCellBorder(coord, color, data.isHLOD ? 0.8f : 0.5f);

        if (config_.showCellText) {
            std::ostringstream oss;
            oss << "L" << data.currentLOD;
            if (data.isHLOD) oss << "H";
            if (data.isImpostor) oss << "I";
            DrawCellText(coord, oss.str());
        }
    }
}

void DebugVisualizationSystem::RenderInterestMode(const Mat4& viewMatrix, const Mat4& projectionMatrix) {
    (void)viewMatrix;
    (void)projectionMatrix;

    // Interest is conveyed indirectly via priority and recent-access frame
    // counters in CellVisualizationData. Highlight cells whose lastAccessFrame
    // is within a recent window relative to the system's current frame.
    constexpr uint64_t kRecentWindowFrames = 60;

    for (const auto& [coord, data] : cellData_) {
        const uint64_t age = (data.lastAccessFrame > currentFrame_)
            ? 0
            : (currentFrame_ - data.lastAccessFrame);

        Vec3 color;
        float alpha;
        if (age <= kRecentWindowFrames) {
            const float t = 1.0f - static_cast<float>(age) /
                                   static_cast<float>(kRecentWindowFrames);
            color = config_.lowPriorityColor * (1.0f - t) +
                    config_.highPriorityColor * t;
            alpha = 0.7f;
        } else {
            color = config_.unloadedColor;
            alpha = 0.2f;
        }
        DrawCellBorder(coord, color, alpha);
    }
}

void DebugVisualizationSystem::RenderPredictionMode(const Mat4& viewMatrix, const Mat4& projectionMatrix) {
    (void)viewMatrix;
    (void)projectionMatrix;

    // Prediction mode highlights cells whose priority comes from forward
    // prefetch (priority > 0.5 by convention) — these are cells the streaming
    // system expects to need soon. Connect them with debug lines so the
    // predicted footprint reads at a glance.
    std::vector<Vec3> predictedCenters;
    predictedCenters.reserve(cellData_.size());

    const float cellSize = config_.cellSize > 0.0f ? config_.cellSize : 64.0f;
    for (const auto& [coord, data] : cellData_) {
        if (data.priority < 0.5f) continue;
        const Vec3 center = CellCenter(coord, cellSize);
        predictedCenters.push_back(center);
        DrawCellBorder(coord, config_.highPriorityColor, 0.7f);
    }

    if (predictedCenters.size() >= 2) {
        std::sort(predictedCenters.begin(), predictedCenters.end(),
                  [](const Vec3& a, const Vec3& b) {
                      if (a.x != b.x) return a.x < b.x;
                      return a.z < b.z;
                  });
        for (size_t i = 1; i < predictedCenters.size(); ++i) {
            DrawLine(predictedCenters[i - 1], predictedCenters[i],
                     config_.highPriorityColor, kFrameTransientLifetime);
        }
    }
}

void DebugVisualizationSystem::RenderIOMode(const Mat4& viewMatrix, const Mat4& projectionMatrix) {
    (void)viewMatrix;
    (void)projectionMatrix;

    // Highlight cells that are currently in IO transitions (Loading, Queued,
    // Unloading, Error) so disk pressure is visible at a glance. Already
    // resident cells fade into the background.
    for (const auto& [coord, data] : cellData_) {
        Vec3 color;
        float alpha = 0.35f;
        switch (data.loadState) {
            case CellLoadState::Loading:
            case CellLoadState::Decompressing:
            case CellLoadState::Uploading:
                color = config_.loadingColor;
                alpha = 0.9f;
                break;
            case CellLoadState::Queued:
                color = config_.queuedColor;
                alpha = 0.7f;
                break;
            case CellLoadState::Unloading:
                color = config_.unloadingColor;
                alpha = 0.7f;
                break;
            case CellLoadState::Error:
                color = config_.errorColor;
                alpha = 1.0f;
                break;
            default:
                color = config_.loadedColor;
                alpha = 0.2f;
                break;
        }
        DrawCellBorder(coord, color, alpha);
    }
}

void DebugVisualizationSystem::RenderHeatmapMode(const Mat4& viewMatrix, const Mat4& projectionMatrix) {
    (void)viewMatrix;
    (void)projectionMatrix;

    for (const auto& [coord, cell] : heatmap_) {
        const Vec3 color = HeatmapGradient(cell.intensity);
        DrawCellBorder(coord, color, std::min(1.0f, cell.intensity + 0.2f));
    }
}

Vec3 DebugVisualizationSystem::GetColorForLoadState(CellLoadState state) const {
    switch (state) {
        case CellLoadState::Unloaded: return config_.unloadedColor;
        case CellLoadState::Queued: return config_.queuedColor;
        case CellLoadState::Loading: return config_.loadingColor;
        case CellLoadState::Loaded: return config_.loadedColor;
        case CellLoadState::Unloading: return config_.unloadingColor;
        case CellLoadState::Error: return config_.errorColor;
        default: return Vec3(1.0f, 1.0f, 1.0f);
    }
}

Vec3 DebugVisualizationSystem::GetColorForPriority(float priority, float minPriority, float maxPriority) const {
    float t = (priority - minPriority) / (maxPriority - minPriority + 0.0001f);
    return config_.lowPriorityColor * (1.0f - t) + config_.highPriorityColor * t;
}

Vec3 DebugVisualizationSystem::GetColorForMemoryUsage(uint64_t usage, uint64_t maxUsage) const {
    float t = static_cast<float>(usage) / static_cast<float>(maxUsage + 1);
    return Vec3(t, 1.0f - t, 0.0f);  // Red to green gradient
}

Vec3 DebugVisualizationSystem::GetColorForLOD(uint32_t lod) const {
    // Color gradient from high detail (green) to low detail (red)
    float t = std::min(lod / 4.0f, 1.0f);
    return Vec3(t, 1.0f - t, 0.0f);
}

void DebugVisualizationSystem::DrawCellBorder(const CellCoord& coord, const Vec3& color, float alpha) {
    if (!config_.showCellBorders) {
        return;
    }
    (void)alpha;  // DebugLine doesn't carry alpha; reserved for renderer composition.

    const float cellSize = config_.cellSize > 0.0f ? config_.cellSize : 64.0f;
    const Vec3 origin = CellMinCorner(coord, cellSize);
    const Vec3 c00(origin.x,            origin.y, origin.z);
    const Vec3 c10(origin.x + cellSize, origin.y, origin.z);
    const Vec3 c11(origin.x + cellSize, origin.y, origin.z + cellSize);
    const Vec3 c01(origin.x,            origin.y, origin.z + cellSize);

    DrawLine(c00, c10, color, kFrameTransientLifetime);
    DrawLine(c10, c11, color, kFrameTransientLifetime);
    DrawLine(c11, c01, color, kFrameTransientLifetime);
    DrawLine(c01, c00, color, kFrameTransientLifetime);
}

void DebugVisualizationSystem::DrawCellText(const CellCoord& coord, const std::string& text) {
    if (!config_.showCellText) {
        return;
    }
    const float cellSize = config_.cellSize > 0.0f ? config_.cellSize : 64.0f;
    DrawText(CellCenter(coord, cellSize), text, Vec3(1.0f, 1.0f, 1.0f),
             config_.overlaySize, kFrameTransientLifetime);
}

void DebugVisualizationSystem::UpdateHeatmapDecay() {
    for (auto& [coord, cell] : heatmap_) {
        cell.intensity *= config_.heatmapDecay;
    }

    // Remove low-intensity cells
    for (auto it = heatmap_.begin(); it != heatmap_.end();) {
        if (it->second.intensity < 0.01f) {
            it = heatmap_.erase(it);
        } else {
            ++it;
        }
    }
}

void DebugVisualizationSystem::UpdateDebugElements(float deltaTime) {
    // Update temporary elements
    for (auto& line : lines_) {
        if (line.lifetime > 0.0f) {
            line.lifetime -= deltaTime;
        }
    }

    for (auto& box : boxes_) {
        if (box.lifetime > 0.0f) {
            box.lifetime -= deltaTime;
        }
    }

    for (auto& text : texts_) {
        if (text.lifetime > 0.0f) {
            text.lifetime -= deltaTime;
        }
    }

    RemoveExpiredElements();
}

void DebugVisualizationSystem::RemoveExpiredElements() {
    lines_.erase(
        std::remove_if(lines_.begin(), lines_.end(),
            [](const DebugLine& line) { return line.lifetime < 0.0f; }),
        lines_.end()
    );

    boxes_.erase(
        std::remove_if(boxes_.begin(), boxes_.end(),
            [](const DebugBox& box) { return box.lifetime < 0.0f; }),
        boxes_.end()
    );

    texts_.erase(
        std::remove_if(texts_.begin(), texts_.end(),
            [](const DebugText& text) { return text.lifetime < 0.0f; }),
        texts_.end()
    );
}

void DebugVisualizationSystem::RenderDebugLines(const Mat4& viewMatrix, const Mat4& projectionMatrix) {
    // If the renderer (or tool) installed a sink, drain the accumulated lines
    // through it. Without a sink the buffer stays put and callers can pull via
    // GetLines() — the streaming module never hard-depends on a GPU backend.
    if (lineSink_ && !lines_.empty()) {
        lineSink_(lines_, viewMatrix, projectionMatrix);
    }
}

void DebugVisualizationSystem::RenderDebugBoxes(const Mat4& viewMatrix, const Mat4& projectionMatrix) {
    if (boxSink_ && !boxes_.empty()) {
        boxSink_(boxes_, viewMatrix, projectionMatrix);
    }
}

void DebugVisualizationSystem::RenderDebugText(const Mat4& viewMatrix, const Mat4& projectionMatrix) {
    if (textSink_ && !texts_.empty()) {
        textSink_(texts_, viewMatrix, projectionMatrix);
    }
}

// ===== Streaming Profiler Implementation =====

StreamingProfiler::StreamingProfiler()
    : maxSamples_(1024)
    , initialized_(false)
{
}

StreamingProfiler::~StreamingProfiler() {
    Shutdown();
}

bool StreamingProfiler::Initialize(uint32_t maxSamples) {
    if (initialized_) {
        return true;
    }

    maxSamples_ = maxSamples;
    initialized_ = true;

    return true;
}

void StreamingProfiler::BeginEvent(const std::string& name) {
    if (!initialized_) {
        return;
    }

    // Use the EventData slots themselves as the in-flight registry: an entry
    // with endTime == 0 (and nonzero startTime) is a still-open event. This
    // avoids growing a separate "open events" map while keeping correct LIFO
    // semantics for nested events with the same name.
    EventData event;
    event.name = name;
    event.startTime = NowSeconds();
    event.endTime = 0.0f;
    event.duration = 0.0f;

    auto& bucket = events_[name];
    bucket.push_back(event);
    if (bucket.size() > maxSamples_) {
        bucket.erase(bucket.begin());
    }
}

void StreamingProfiler::EndEvent(const std::string& name) {
    if (!initialized_) {
        return;
    }

    auto it = events_.find(name);
    if (it == events_.end() || it->second.empty()) {
        return;
    }

    // Close the most recent still-open event (endTime == 0) — LIFO matches
    // the typical Begin/End scoping.
    for (auto rit = it->second.rbegin(); rit != it->second.rend(); ++rit) {
        if (rit->endTime == 0.0f) {
            rit->endTime = NowSeconds();
            rit->duration = rit->endTime - rit->startTime;
            return;
        }
    }
}

void StreamingProfiler::RecordMetric(const std::string& name, float value) {
    auto it = metrics_.find(name);
    if (it == metrics_.end()) {
        MetricData metric;
        metric.name = name;
        metric.average = value;
        metric.minValue = value;
        metric.maxValue = value;
        metric.values.push_back(value);

        metrics_[name] = metric;
    } else {
        it->second.values.push_back(value);
        if (it->second.values.size() > maxSamples_) {
            it->second.values.erase(it->second.values.begin());
        }

        // Update statistics
        float sum = 0.0f;
        float minVal = FLT_MAX;
        float maxVal = -FLT_MAX;

        for (float val : it->second.values) {
            sum += val;
            minVal = std::min(minVal, val);
            maxVal = std::max(maxVal, val);
        }

        it->second.average = sum / it->second.values.size();
        it->second.minValue = minVal;
        it->second.maxValue = maxVal;
    }
}

float StreamingProfiler::GetAverageEventTime(const std::string& name) const {
    auto it = events_.find(name);
    if (it != events_.end() && !it->second.empty()) {
        float sum = 0.0f;
        for (const auto& event : it->second) {
            sum += event.duration;
        }
        return sum / it->second.size();
    }
    return 0.0f;
}

float StreamingProfiler::GetMaxEventTime(const std::string& name) const {
    auto it = events_.find(name);
    if (it != events_.end() && !it->second.empty()) {
        float maxTime = 0.0f;
        for (const auto& event : it->second) {
            maxTime = std::max(maxTime, event.duration);
        }
        return maxTime;
    }
    return 0.0f;
}

float StreamingProfiler::GetMinEventTime(const std::string& name) const {
    auto it = events_.find(name);
    if (it != events_.end() && !it->second.empty()) {
        float minTime = FLT_MAX;
        for (const auto& event : it->second) {
            minTime = std::min(minTime, event.duration);
        }
        return minTime;
    }
    return 0.0f;
}

uint32_t StreamingProfiler::GetEventCount(const std::string& name) const {
    auto it = events_.find(name);
    if (it != events_.end()) {
        return static_cast<uint32_t>(it->second.size());
    }
    return 0;
}

void StreamingProfiler::ExportToCSV(const std::string& filename) {
    std::ofstream out(filename);
    if (!out) {
        NEXT_LOG_WARNING("StreamingProfiler: cannot open CSV file %s", filename.c_str());
        return;
    }

    out.setf(std::ios::fixed);
    out << std::setprecision(6);

    out << "section,name,sample_index,start,end,duration\n";
    for (const auto& [name, samples] : events_) {
        for (size_t i = 0; i < samples.size(); ++i) {
            const EventData& e = samples[i];
            out << "event,"
                << '"' << e.name << '"' << ','
                << i << ','
                << e.startTime << ','
                << e.endTime << ','
                << e.duration << '\n';
        }
    }

    out << "section,name,average,min,max,sample_count\n";
    for (const auto& [name, metric] : metrics_) {
        out << "metric,"
            << '"' << metric.name << '"' << ','
            << metric.average << ','
            << metric.minValue << ','
            << metric.maxValue << ','
            << metric.values.size() << '\n';
    }
}

std::string StreamingProfiler::GenerateReport() const {
    std::ostringstream oss;
    oss << "Streaming Profiler Report\n";
    oss << "========================\n\n";

    for (const auto& [name, metric] : metrics_) {
        oss << metric.name << ":\n";
        oss << "  Average: " << metric.average << "\n";
        oss << "  Min: " << metric.minValue << "\n";
        oss << "  Max: " << metric.maxValue << "\n\n";
    }

    return oss.str();
}

void StreamingProfiler::Reset() {
    events_.clear();
    metrics_.clear();
}

void StreamingProfiler::Shutdown() {
    Reset();
    initialized_ = false;
}

} // namespace Streaming
} // namespace Next
