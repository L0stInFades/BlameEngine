#pragma once

#include "next/math/math.h"
#include "next/streaming/world_partition.h"
#include "next/streaming/streaming_manager.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <memory>
#include <functional>

namespace Next {
namespace Streaming {

// ===== Visualization Mode =====

enum class VisualizationMode : uint32_t {
    None = 0,
    LoadState = 1,           // Color cells by load state
    Priority = 2,            // Color cells by priority
    MemoryUsage = 3,         // Color cells by memory usage
    LOD = 4,                 // Show LOD levels
    HLOD = 5,                // Show HLOD clusters
    Interest = 6,            // Show interest regions
    Prediction = 7,          // Show predicted paths
    IO = 8,                  // Show IO operations
    Heatmap = 9,             // Activity heatmap
    Performance = 10,        // Performance metrics
    Custom = 11              // Custom visualization
};

// ===== Cell Visualization Data =====

struct CellVisualizationData {
    CellCoord coord;
    Vec3 worldPosition;
    float cellSize;

    // State
    CellLoadState loadState;
    float priority;
    uint64_t memoryUsage;

    // Timing
    float loadTime;
    uint64_t loadFrame;
    uint64_t lastAccessFrame;

    // LOD
    uint32_t currentLOD;
    bool isHLOD;
    bool isImpostor;

    // Colors (for visualization)
    Vec3 color;
    float alpha;

    CellVisualizationData()
        : worldPosition(0.0f, 0.0f, 0.0f)
        , cellSize(64.0f)
        , loadState(CellLoadState::Unloaded)
        , priority(0.0f)
        , memoryUsage(0)
        , loadTime(0.0f)
        , loadFrame(0)
        , lastAccessFrame(0)
        , currentLOD(0)
        , isHLOD(false)
        , isImpostor(false)
        , color(1.0f, 1.0f, 1.0f)
        , alpha(1.0f)
    {}
};

// ===== Debug Line =====

struct DebugLine {
    Vec3 start;
    Vec3 end;
    Vec3 color;
    float lifetime;  // 0 = persistent, >0 = seconds

    DebugLine()
        : start(0.0f, 0.0f, 0.0f)
        , end(0.0f, 0.0f, 0.0f)
        , color(1.0f, 1.0f, 1.0f)
        , lifetime(0.0f)
    {}
};

// ===== Debug Box =====

struct DebugBox {
    Vec3 boundsMin;  // Renamed from 'min' to avoid Windows macro conflict
    Vec3 boundsMax;  // Renamed from 'max' to avoid Windows macro conflict
    Vec3 color;
    float alpha;
    float lifetime;

    DebugBox()
        : boundsMin(0.0f, 0.0f, 0.0f)
        , boundsMax(0.0f, 0.0f, 0.0f)
        , color(1.0f, 1.0f, 1.0f)
        , alpha(1.0f)
        , lifetime(0.0f)
    {}
};

// ===== Debug Text =====

struct DebugText {
    Vec3 position;
    std::string text;
    Vec3 color;
    float size;
    float lifetime;

    DebugText()
        : position(0.0f, 0.0f, 0.0f)
        , text("")
        , color(1.0f, 1.0f, 1.0f)
        , size(12.0f)
        , lifetime(0.0f)
    {}
};

// ===== Debug Visualization Configuration =====

struct DebugVisualizationConfig {
    // Enable/disable visualization
    bool enabled = false;
    VisualizationMode mode = VisualizationMode::LoadState;

    // Display settings
    bool showCellBorders = true;
    bool showCellText = true;
    bool showStats = true;
    bool showIOOperations = true;
    bool showPredictions = true;

    // Color schemes
    Vec3 unloadedColor = Vec3(0.2f, 0.2f, 0.2f);        // Gray
    Vec3 queuedColor = Vec3(0.5f, 0.5f, 0.0f);          // Yellow
    Vec3 loadingColor = Vec3(0.0f, 0.5f, 1.0f);         // Blue
    Vec3 loadedColor = Vec3(0.0f, 1.0f, 0.0f);          // Green
    Vec3 unloadingColor = Vec3(1.0f, 0.5f, 0.0f);       // Orange
    Vec3 errorColor = Vec3(1.0f, 0.0f, 0.0f);           // Red

    // Priority colors (gradient from low to high)
    Vec3 lowPriorityColor = Vec3(0.0f, 0.0f, 1.0f);     // Blue
    Vec3 highPriorityColor = Vec3(1.0f, 0.0f, 0.0f);    // Red

    // Heatmap settings
    bool enableHeatmap = true;
    float heatmapDecay = 0.98f;  // Heatmap decay per frame
    float heatmapIntensity = 1.0f;

    // Overlay settings
    bool showOverlay = true;
    Vec2 overlayPosition = Vec2(10.0f, 10.0f);
    float overlaySize = 16.0f;

    // Camera control
    bool allowCameraControl = true;  // Allow debug camera to fly around

    // Cell sizing — must match WorldPartitionConfig::cellSize so cell borders,
    // text anchors, and heatmap quantization match the streaming partition.
    float cellSize = 64.0f;

    DebugVisualizationConfig() = default;
};

// ===== Debug Visualization System =====

class DebugVisualizationSystem {
public:
    DebugVisualizationSystem();
    ~DebugVisualizationSystem();

    // Initialize with configuration
    bool Initialize(const DebugVisualizationConfig& config);

    // Update (called every frame)
    void Update(float deltaTime, const StreamingManager* streamingManager);

    // Render
    void Render(const Mat4& viewMatrix, const Mat4& projectionMatrix);

    // Visualization mode
    void SetVisualizationMode(VisualizationMode mode);
    VisualizationMode GetVisualizationMode() const { return config_.mode; }

    // Toggle visualization
    void Toggle() { config_.enabled = !config_.enabled; }
    bool IsEnabled() const { return config_.enabled; }

    // Debug elements
    void DrawLine(const Vec3& start, const Vec3& end, const Vec3& color, float lifetime = 0.0f);
    void DrawBox(const Vec3& boundsMin, const Vec3& boundsMax, const Vec3& color, float alpha = 1.0f, float lifetime = 0.0f);
    void DrawText(const Vec3& position, const std::string& text, const Vec3& color = Vec3(1.0f, 1.0f, 1.0f), float size = 12.0f, float lifetime = 0.0f);

    // Clear debug elements
    void ClearDebugElements();
    void ClearTemporaryElements();  // Clear only elements with lifetime > 0

    // Cell visualization
    void UpdateCellVisualization(const CellCoord& coord, const CellVisualizationData& data);
    CellVisualizationData* GetCellVisualization(const CellCoord& coord);

    // Statistics overlay
    void DrawStatisticsOverlay(const StreamingStatistics& stats);
    void DrawPerformanceMetrics(const IOStatistics& ioStats);

    // Heatmap
    void UpdateHeatmap(const Vec3& position, float intensity);
    Vec3 GetHeatmapColor(const Vec3& position) const;

    // Configuration
    void SetConfig(const DebugVisualizationConfig& config) { config_ = config; }
    const DebugVisualizationConfig& GetConfig() const { return config_; }

    // Screenshot/capture
    void CaptureVisualization(const std::string& filename);

    // Renderer integration: install sinks the GPU layer can use to rasterize
    // accumulated debug primitives. The streaming module is renderer-agnostic
    // and only buffers DebugLine/DebugBox/DebugText into vectors; the renderer
    // (or an editor/tool) installs sinks here to drain them every Render() pass.
    // Leaving them unset is fine — Render() then queues primitives for later
    // pickup and the caller can pull them via the public Get*() accessors.
    using LineSinkFn = std::function<void(const std::vector<DebugLine>&,
                                          const Mat4& view, const Mat4& projection)>;
    using BoxSinkFn  = std::function<void(const std::vector<DebugBox>&,
                                          const Mat4& view, const Mat4& projection)>;
    using TextSinkFn = std::function<void(const std::vector<DebugText>&,
                                          const Mat4& view, const Mat4& projection)>;
    void SetLineSink(LineSinkFn fn) { lineSink_ = std::move(fn); }
    void SetBoxSink(BoxSinkFn fn)   { boxSink_  = std::move(fn); }
    void SetTextSink(TextSinkFn fn) { textSink_ = std::move(fn); }

    // Read-only buffer accessors for renderer integrations that prefer pull
    // over push (e.g. an ImGui debug panel rendering on its own pass).
    const std::vector<DebugLine>& GetLines() const { return lines_; }
    const std::vector<DebugBox>&  GetBoxes() const { return boxes_; }
    const std::vector<DebugText>& GetTexts() const { return texts_; }

    // Cleanup
    void Shutdown();

    // State check
    bool IsInitialized() const { return initialized_; }

private:
    // Visualization modes
    void RenderLoadStateMode(const Mat4& viewMatrix, const Mat4& projectionMatrix);
    void RenderPriorityMode(const Mat4& viewMatrix, const Mat4& projectionMatrix);
    void RenderMemoryUsageMode(const Mat4& viewMatrix, const Mat4& projectionMatrix);
    void RenderLODMode(const Mat4& viewMatrix, const Mat4& projectionMatrix);
    void RenderInterestMode(const Mat4& viewMatrix, const Mat4& projectionMatrix);
    void RenderPredictionMode(const Mat4& viewMatrix, const Mat4& projectionMatrix);
    void RenderIOMode(const Mat4& viewMatrix, const Mat4& projectionMatrix);
    void RenderHeatmapMode(const Mat4& viewMatrix, const Mat4& projectionMatrix);

    // Helper methods
    Vec3 GetColorForLoadState(CellLoadState state) const;
    Vec3 GetColorForPriority(float priority, float minPriority, float maxPriority) const;
    Vec3 GetColorForMemoryUsage(uint64_t usage, uint64_t maxUsage) const;
    Vec3 GetColorForLOD(uint32_t lod) const;

    // Draw cell borders
    void DrawCellBorder(const CellCoord& coord, const Vec3& color, float alpha = 1.0f);

    // Draw cell text
    void DrawCellText(const CellCoord& coord, const std::string& text);

    // Heatmap grid
    struct HeatmapCell {
        float intensity;
        uint64_t lastUpdateFrame;
    };

    std::unordered_map<CellCoord, HeatmapCell, CellCoord::Hash> heatmap_;
    void UpdateHeatmapDecay();

    // Debug element management
    void UpdateDebugElements(float deltaTime);
    void RemoveExpiredElements();

    // Rendering
    void RenderDebugLines(const Mat4& viewMatrix, const Mat4& projectionMatrix);
    void RenderDebugBoxes(const Mat4& viewMatrix, const Mat4& projectionMatrix);
    void RenderDebugText(const Mat4& viewMatrix, const Mat4& projectionMatrix);

    // Configuration
    DebugVisualizationConfig config_;

    // Cell visualization data
    std::unordered_map<CellCoord, CellVisualizationData, CellCoord::Hash> cellData_;

    // Debug elements
    std::vector<DebugLine> lines_;
    std::vector<DebugBox> boxes_;
    std::vector<DebugText> texts_;

    // Renderer sinks (set by integration layer; nullptr → no GPU pass).
    LineSinkFn lineSink_;
    BoxSinkFn  boxSink_;
    TextSinkFn textSink_;

    // Current frame
    uint64_t currentFrame_;

    // State
    bool initialized_;
};

// ===== Streaming Profiler =====

class StreamingProfiler {
public:
    StreamingProfiler();
    ~StreamingProfiler();

    // Initialize
    bool Initialize(uint32_t maxSamples = 1024);

    // Profile events
    void BeginEvent(const std::string& name);
    void EndEvent(const std::string& name);
    void RecordMetric(const std::string& name, float value);

    // Statistics
    float GetAverageEventTime(const std::string& name) const;
    float GetMaxEventTime(const std::string& name) const;
    float GetMinEventTime(const std::string& name) const;
    uint32_t GetEventCount(const std::string& name) const;

    // Export
    void ExportToCSV(const std::string& filename);
    std::string GenerateReport() const;

    // Reset
    void Reset();

    // Cleanup
    void Shutdown();

private:
    struct EventData {
        std::string name;
        float startTime;
        float endTime;
        float duration;
    };

    struct MetricData {
        std::string name;
        std::vector<float> values;
        float average;
        float minValue;  // Renamed from 'min' to avoid Windows macro conflict
        float maxValue;  // Renamed from 'max' to avoid Windows macro conflict
    };

    std::unordered_map<std::string, std::vector<EventData>> events_;
    std::unordered_map<std::string, MetricData> metrics_;
    uint32_t maxSamples_;
    bool initialized_;
};

// ===== Streaming HUD =====

class StreamingHUD {
public:
    StreamingHUD();
    ~StreamingHUD();

    // Initialize
    bool Initialize(const DebugVisualizationConfig& config);

    // Update and render
    void Update(float deltaTime);
    void Render(const StreamingStatistics& stats, const IOStatistics& ioStats);

    // HUD panels
    void ShowGeneralPanel(const StreamingStatistics& stats);
    void ShowPerformancePanel(const IOStatistics& ioStats);
    void ShowMemoryPanel(const StreamingStatistics& stats);
    void ShowLODPanel(const StreamingStatistics& stats);

    // Toggle panels
    void TogglePanel(const std::string& panelName);
    bool IsPanelVisible(const std::string& panelName) const;

    // Configuration
    void SetConfig(const DebugVisualizationConfig& config);

    // Cleanup
    void Shutdown();

private:
    struct Panel {
        std::string name;
        bool visible;
        Vec2 position;
        Vec2 size;
    };

    std::unordered_map<std::string, Panel> panels_;
    DebugVisualizationConfig config_;
    bool initialized_;
};

} // namespace Streaming
} // namespace Next
