#include "song/game.h"
#include "next/platform/platform.h"
#include "next/platform/window.h"
#include "next/platform/input.h"
#include "next/foundation/logger.h"
#include "next/runtime/world.h"
#include "next/renderer/renderer.h"
#include "next/profiler/profiler.h"
#include "next/profiler/cpu_scope.h"
#include "next/jobsystem/job_system.h"
#include "next/runtime/asset/asset_manager.h"
#include "next/runtime/asset/package_container.h"
#include "next/streaming/streaming_manager.h"
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#ifdef CreateWindow
#undef CreateWindow
#endif
#endif

namespace {

std::filesystem::path GetExecutableDirectory() {
#ifdef _WIN32
    char path[MAX_PATH] = {};
    DWORD len = GetModuleFileNameA(nullptr, path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        return {};
    }
    return std::filesystem::path(path).parent_path();
#else
    return {};
#endif
}

void PushUniquePath(std::vector<std::filesystem::path>& roots, const std::filesystem::path& path) {
    if (path.empty()) {
        return;
    }

    for (const auto& existing : roots) {
        if (existing == path) {
            return;
        }
    }

    roots.push_back(path);
}

std::filesystem::path ResolveRuntimePath(const std::filesystem::path& path) {
    if (path.empty() || path.is_absolute()) {
        return path;
    }

    std::error_code ec;
    std::vector<std::filesystem::path> roots;
    PushUniquePath(roots, std::filesystem::current_path(ec));

    std::filesystem::path probe = GetExecutableDirectory();
    for (int i = 0; i < 6 && !probe.empty(); ++i) {
        PushUniquePath(roots, probe);
        const std::filesystem::path parent = probe.parent_path();
        if (parent == probe) {
            break;
        }
        probe = parent;
    }

    std::filesystem::path fallback;
    for (const auto& root : roots) {
        const std::filesystem::path candidate = root / path;
        if (fallback.empty()) {
            const std::filesystem::path absoluteCandidate = std::filesystem::absolute(candidate, ec);
            fallback = ec ? candidate : absoluteCandidate;
        }

        if (std::filesystem::exists(candidate, ec)) {
            const std::filesystem::path absoluteCandidate = std::filesystem::absolute(candidate, ec);
            return ec ? candidate : absoluteCandidate;
        }
    }

    return fallback.empty() ? path : fallback;
}

std::string FixedAssetName(const char* name, size_t capacity) {
    size_t length = 0;
    while (length < capacity && name[length] != '\0') {
        ++length;
    }
    return std::string(name, length);
}

uint64_t MegabytesToBytesSaturated(uint64_t megabytes) {
    constexpr uint64_t kBytesPerMegabyte = 1024ull * 1024ull;
    const uint64_t maxBytes = std::numeric_limits<uint64_t>::max();
    return megabytes > maxBytes / kBytesPerMegabyte ? maxBytes : megabytes * kBytesPerMegabyte;
}

void LogRendererDeviceInfo(Next::Renderer& renderer) {
    const Next::RendererDeviceInfo info = renderer.GetDeviceInfo();
    NEXT_LOG_INFO("Renderer device info: available=%s backend=%s device=%s unifiedMemory=%s "
                  "argumentBuffers=%s argumentBufferTier=%s bindlessResources=%s "
                  "asyncUploadQueue=%s maxFramesInFlight=%u "
                  "queues(graphics=%s compute=%s copy=%s dedicatedCopy=%s) "
                  "hasBackend=%s hasDeviceName=%s hasRenderable=%s hasQueueSupport=%s "
                  "hasDedicatedQueues=%s hasArgumentTier=%s hasCapabilities=%s",
                  info.available ? "true" : "false",
                  Next::Renderer::BackendToString(info.backend),
                  info.GetDeviceName(),
                  info.HasUnifiedMemory() ? "true" : "false",
                  info.HasArgumentBuffers() ? "true" : "false",
                  Next::RHI::ArgumentBufferTierName(info.features.argumentBufferTier),
                  info.HasBindlessResources() ? "true" : "false",
                  info.HasAsyncUploadQueue() ? "true" : "false",
                  info.features.maxFramesInFlight,
                  info.SupportsGraphicsQueue() ? "true" : "false",
                  info.SupportsComputeQueue() ? "true" : "false",
                  info.SupportsCopyQueue() ? "true" : "false",
                  info.HasDedicatedCopyQueue() ? "true" : "false",
                  info.HasBackend() ? "true" : "false",
                  info.HasDeviceName() ? "true" : "false",
                  info.HasRenderableDevice() ? "true" : "false",
                  info.HasQueueSupport() ? "true" : "false",
                  info.HasDedicatedQueueSupport() ? "true" : "false",
                  info.HasArgumentBufferTier() ? "true" : "false",
                  info.HasDeviceCapabilities() ? "true" : "false");
}

void LogRendererLifetimeStats(Next::Renderer& renderer) {
    const Next::RendererLifetimeStats stats = renderer.GetLifetimeStats();
    NEXT_LOG_INFO("Renderer lifetime stats: submittedFrames=%llu pendingReleases=%llu "
                  "peakPendingReleases=%llu queuedReleases=%llu collectedReleases=%llu "
                  "collectPasses=%llu forcedCollectPasses=%llu releaseLatency=%u "
                  "hasSubmitted=%s hasPending=%s hasPeakPending=%s hasQueued=%s "
                  "hasCollected=%s hasCollectPasses=%s hasForcedCollects=%s hasReleaseActivity=%s",
                  static_cast<unsigned long long>(stats.submittedFrameIndex),
                  static_cast<unsigned long long>(stats.pendingReleaseObjectCount),
                  static_cast<unsigned long long>(stats.peakPendingReleaseObjectCount),
                  static_cast<unsigned long long>(stats.queuedReleaseObjectCount),
                  static_cast<unsigned long long>(stats.collectedReleaseObjectCount),
                  static_cast<unsigned long long>(stats.releaseCollectPassCount),
                  static_cast<unsigned long long>(stats.forcedReleaseCollectPassCount),
                  stats.releaseCollectLatency,
                  stats.HasSubmittedFrames() ? "true" : "false",
                  stats.HasPendingReleases() ? "true" : "false",
                  stats.HasPeakPendingReleases() ? "true" : "false",
                  stats.HasQueuedReleases() ? "true" : "false",
                  stats.HasCollectedReleases() ? "true" : "false",
                  stats.HasReleaseCollectPasses() ? "true" : "false",
                  stats.HasForcedReleaseCollectPasses() ? "true" : "false",
                  stats.HasReleaseQueueActivity() ? "true" : "false");
}

void LogRendererFrameDebugStats(size_t submittedCells,
                                size_t renderedCells,
                                size_t overflowCells,
                                size_t placeholderCells,
                                size_t renderedPlaceholderCells) {
    Next::RendererFrameDebugStats stats;
    stats.submittedDebugCellCount = submittedCells;
    stats.renderedDebugCellCount = renderedCells;
    stats.overflowDebugCellCount = overflowCells;
    stats.placeholderDebugCellCount = placeholderCells;
    stats.renderedPlaceholderDebugCellCount = renderedPlaceholderCells;
    NEXT_LOG_INFO("Renderer frame debug stats: submittedCells=%zu renderedCells=%zu overflowCells=%zu "
                  "placeholderCells=%zu renderedPlaceholderCells=%zu hasCells=%s hasOverflow=%s "
                  "hasRendered=%s hasPlaceholders=%s hasRenderedPlaceholders=%s hasActivity=%s",
                  submittedCells,
                  renderedCells,
                  overflowCells,
                  placeholderCells,
                  renderedPlaceholderCells,
                  stats.HasDebugCells() ? "true" : "false",
                  stats.HasDebugCellOverflow() ? "true" : "false",
                  stats.HasRenderedDebugCells() ? "true" : "false",
                  stats.HasPlaceholderDebugCells() ? "true" : "false",
                  stats.HasRenderedPlaceholderDebugCells() ? "true" : "false",
                  stats.HasDebugCellActivity() ? "true" : "false");
}

void LogStreamingStats(Next::Streaming::StreamingManager& streaming) {
    const Next::Streaming::StreamingStatistics stats = streaming.GetStatistics();
    NEXT_LOG_INFO("World streaming stats: loaded=%u loading=%u queued=%u pending=%llu active=%llu "
                  "visible=%u high=%u low=%u hlod=%u memory=%llu/%llu utilization=%.2f "
                  "failed=%u timeouts=%u placeholders=%u hasLoaded=%s hasPending=%s hasActive=%s "
                  "hasVisible=%s hasMemoryBudget=%s hasMemory=%s overBudget=%s hasFailures=%s "
                  "hasPlaceholders=%s",
                  stats.loadedCells,
                  stats.loadingCells,
                  stats.queuedCells,
                  static_cast<unsigned long long>(stats.PendingCellCount()),
                  static_cast<unsigned long long>(stats.ActiveCellCount()),
                  stats.visibleCells,
                  stats.highDetailCells,
                  stats.lowDetailCells,
                  stats.hlodCells,
                  static_cast<unsigned long long>(stats.memoryUsed),
                  static_cast<unsigned long long>(stats.memoryBudget),
                  stats.memoryUtilization,
                  stats.failedLoads,
                  stats.timeoutErrors,
                  stats.placeholderCells,
                  stats.HasLoadedCells() ? "true" : "false",
                  stats.HasPendingCells() ? "true" : "false",
                  stats.HasActiveCells() ? "true" : "false",
                  stats.HasVisibleCells() ? "true" : "false",
                  stats.HasMemoryBudget() ? "true" : "false",
                  stats.HasMemoryUsage() ? "true" : "false",
                  stats.IsOverMemoryBudget() ? "true" : "false",
                  stats.HasFailures() ? "true" : "false",
                  stats.HasPlaceholderCells() ? "true" : "false");
}

void LogStreamingIOStats(Next::Streaming::StreamingManager& streaming) {
    const Next::Streaming::IOStatistics stats = streaming.GetIOStatistics();
    NEXT_LOG_INFO("World streaming IO stats: bytes(read=%llu written=%llu decompressed=%llu total=%llu) "
                  "pending(read=%u write=%u decompress=%u total=%llu) "
                  "throughput(read=%.2f write=%.2f decompress=%.2f hasThroughput=%s) "
                  "timing(read=%.2f write=%.2f decompress=%.2f hasTiming=%s) "
                  "failures=%u hasBytes=%s hasPending=%s hasFailures=%s",
                  static_cast<unsigned long long>(stats.totalBytesRead),
                  static_cast<unsigned long long>(stats.totalBytesWritten),
                  static_cast<unsigned long long>(stats.totalBytesDecompressed),
                  static_cast<unsigned long long>(stats.TotalBytesProcessed()),
                  stats.pendingReads,
                  stats.pendingWrites,
                  stats.pendingDecompressions,
                  static_cast<unsigned long long>(stats.PendingOperationCount()),
                  stats.averageReadSpeedMBps,
                  stats.averageWriteSpeedMBps,
                  stats.averageDecompressSpeedMBps,
                  stats.HasThroughput() ? "true" : "false",
                  stats.averageReadTime,
                  stats.averageWriteTime,
                  stats.averageDecompressTime,
                  stats.HasTiming() ? "true" : "false",
                  stats.failedOperations,
                  stats.HasProcessedBytes() ? "true" : "false",
                  stats.HasPendingOperations() ? "true" : "false",
                  stats.HasFailures() ? "true" : "false");
}

void LogWorldPartitionStats(Next::Streaming::StreamingManager& streaming) {
    const Next::Streaming::WorldPartition::Statistics stats = streaming.GetWorldPartitionStatistics();
    NEXT_LOG_INFO("World partition stats: loaded=%zu queued=%zu loading=%zu pending=%zu active=%zu "
                  "tracked=%zu memoryMB=%zu avgLoad=%.2f avgUnload=%.2f hasLoaded=%s hasQueued=%s "
                  "hasLoading=%s hasPending=%s hasActive=%s hasTracked=%s hasMemory=%s hasTiming=%s",
                  stats.loadedCells,
                  stats.queuedCells,
                  stats.loadingCells,
                  stats.PendingCellCount(),
                  stats.ActiveCellCount(),
                  stats.totalCells,
                  stats.memoryUsageMB,
                  stats.averageLoadTime,
                  stats.averageUnloadTime,
                  stats.HasLoadedCells() ? "true" : "false",
                  stats.HasQueuedCells() ? "true" : "false",
                  stats.HasLoadingCells() ? "true" : "false",
                  stats.HasPendingCells() ? "true" : "false",
                  stats.HasActiveCells() ? "true" : "false",
                  stats.HasTrackedCells() ? "true" : "false",
                  stats.HasMemoryUsage() ? "true" : "false",
                  stats.HasTiming() ? "true" : "false");
}

void LogStreamingInterestStats(Next::Streaming::StreamingManager& streaming) {
    const Next::Streaming::InterestManager::Statistics stats = streaming.GetInterestStatistics();
    NEXT_LOG_INFO("World streaming interest stats: points=%u cells(high=%u medium=%u low=%u total=%llu) "
                  "hasPoints=%s hasHigh=%s hasMedium=%s hasLow=%s hasCells=%s",
                  stats.activeInterestPoints,
                  stats.highInterestCells,
                  stats.mediumInterestCells,
                  stats.lowInterestCells,
                  static_cast<unsigned long long>(stats.InterestCellCount()),
                  stats.HasActiveInterestPoints() ? "true" : "false",
                  stats.HasHighInterestCells() ? "true" : "false",
                  stats.HasMediumInterestCells() ? "true" : "false",
                  stats.HasLowInterestCells() ? "true" : "false",
                  stats.HasInterestCells() ? "true" : "false");
}

void LogStreamingLODStats(Next::Streaming::StreamingManager& streaming) {
    const Next::Streaming::LODSystem::Statistics stats = streaming.GetLODStatistics();
    NEXT_LOG_INFO("World streaming LOD stats: detailed(high=%u medium=%u low=%u total=%llu) "
                  "representations(hlod=%u impostor=%u total=%llu) total=%llu average=%.2f "
                  "quality=%.2f hasDetailed=%s hasRepresentations=%s hasObjects=%s "
                  "hasAverage=%s hasQuality=%s",
                  stats.highDetailObjects,
                  stats.mediumDetailObjects,
                  stats.lowDetailObjects,
                  static_cast<unsigned long long>(stats.DetailedObjectCount()),
                  stats.hlodObjects,
                  stats.impostorObjects,
                  static_cast<unsigned long long>(stats.RepresentationObjectCount()),
                  static_cast<unsigned long long>(stats.TotalObjectCount()),
                  stats.averageLODLevel,
                  stats.currentQualityScale,
                  stats.HasDetailedObjects() ? "true" : "false",
                  stats.HasRepresentationObjects() ? "true" : "false",
                  stats.HasObjects() ? "true" : "false",
                  stats.HasAverageLODLevel() ? "true" : "false",
                  stats.HasQualityScale() ? "true" : "false");
}

void LogStreamingEvictionStats(Next::Streaming::StreamingManager& streaming) {
    const Next::Streaming::EvictionPolicy::Statistics stats = streaming.GetEvictionStatistics();
    NEXT_LOG_INFO("World streaming eviction stats: total=%u frame=%u protected=%u averageScore=%.4f "
                  "memoryFreed=%llu hasEvictions=%s hasFrameEvictions=%s hasProtected=%s "
                  "hasAverageScore=%s hasMemoryFreed=%s hasActivity=%s",
                  stats.totalEvictions,
                  stats.evictionsThisFrame,
                  stats.protectedCells,
                  stats.averageEvictionScore,
                  static_cast<unsigned long long>(stats.memoryFreed),
                  stats.HasEvictions() ? "true" : "false",
                  stats.HasFrameEvictions() ? "true" : "false",
                  stats.HasProtectedCells() ? "true" : "false",
                  stats.HasAverageEvictionScore() ? "true" : "false",
                  stats.HasMemoryFreed() ? "true" : "false",
                  stats.HasActivity() ? "true" : "false");
}

void LogAssetStats(Next::AssetManager& assetManager) {
    const Next::AssetStats stats = assetManager.GetStats();
    NEXT_LOG_INFO("Asset stats: loaded=%zu memory=%zu pendingLoads=%zu pendingCallbacks=%zu "
                  "outstandingAsync=%zu failed=%zu hasLoaded=%s hasMemory=%s hasPendingLoads=%s "
                  "hasPendingCallbacks=%s hasOutstandingAsync=%s hasFailures=%s",
                  stats.loadedAssets,
                  stats.totalMemory,
                  stats.pendingLoads,
                  stats.pendingCallbacks,
                  stats.OutstandingAsyncOperationCount(),
                  stats.failedLoads,
                  stats.HasLoadedAssets() ? "true" : "false",
                  stats.HasMemoryUsage() ? "true" : "false",
                  stats.HasPendingLoads() ? "true" : "false",
                  stats.HasPendingCallbacks() ? "true" : "false",
                  stats.HasOutstandingAsyncOperations() ? "true" : "false",
                  stats.HasFailures() ? "true" : "false");
}

void LogRendererCommandStats(Next::Renderer& renderer) {
    const Next::RendererCommandStats stats = renderer.GetCommandStats();
    NEXT_LOG_INFO("Renderer command stats: recording=%s queue=%s submittedFrames=%llu "
                  "beginAttempts=%llu begun=%llu beginFailures=%llu renderPassAttempts=%llu "
                  "renderPassBegun=%llu renderPassFailures=%llu renderPassEnded=%llu "
                  "commitAttempts=%llu committed=%llu commitFailures=%llu presentAttempts=%llu "
                  "presented=%llu presentFailures=%llu frameGraphPassAttempts=%llu "
                  "frameGraphPasses=%llu frameGraphPassFailures=%llu frameGraphDependencies=%llu frameGraphAccesses=%llu "
                  "frameGraphTransitions=%llu "
                  "frameGraphTransitionState(attachment=%llu buffer=%llu shader=%llu copy=%llu present=%llu other=%llu) "
                  "frameGraphAccess(attachment=%llu buffer=%llu shader=%llu copy=%llu present=%llu other=%llu) "
                  "frameGraphStageHints(accesses=%llu vertex=%llu fragment=%llu compute=%llu) "
                  "frameGraphResourceUses(attempts=%llu declared=%llu skipped=%llu failures=%llu buffer=%llu texture=%llu "
                  "vertexStage=%llu fragmentStage=%llu) "
                  "lastFrameGraphPass=%u lastFrameGraphDependencyRange=%u+%u lastFrameGraphTransitionRange=%u+%u "
                  "lastFrameGraphAccessRange=%u+%u lastFrameGraphQueue=%s "
                  "lastFrameGraphResourceUse(pass=%u range=%u+%u declared=%llu skipped=%llu) "
                  "hasSubmitted=%s hasBeginAttempts=%s hasBegun=%s "
                  "hasRenderPassAttempts=%s hasRenderPasses=%s hasCommitAttempts=%s hasCommitted=%s "
                  "hasPresentAttempts=%s hasPresented=%s hasFrameGraphPassAttempts=%s "
                  "hasFrameGraphPasses=%s hasFrameGraphDependencies=%s hasFrameGraphAccesses=%s hasFrameGraphTransitions=%s "
                  "hasFrameGraphTransitionState=%s hasFrameGraphAccess=%s hasFrameGraphShaderStageHints=%s "
                  "hasFrameGraphResourceUses=%s hasFrameGraphStageResourceUses=%s "
                  "hasCommandActivity=%s hasFailures=%s",
                  stats.recording ? "true" : "false",
                  Next::RHI::QueueClassName(stats.queueClass),
                  static_cast<unsigned long long>(stats.submittedFrameIndex),
                  static_cast<unsigned long long>(stats.beginAttemptCount),
                  static_cast<unsigned long long>(stats.begunCommandBufferCount),
                  static_cast<unsigned long long>(stats.beginFailureCount),
                  static_cast<unsigned long long>(stats.renderPassAttemptCount),
                  static_cast<unsigned long long>(stats.renderPassBeginCount),
                  static_cast<unsigned long long>(stats.renderPassFailureCount),
                  static_cast<unsigned long long>(stats.renderPassEndCount),
                  static_cast<unsigned long long>(stats.commitAttemptCount),
                  static_cast<unsigned long long>(stats.committedCommandBufferCount),
                  static_cast<unsigned long long>(stats.commitFailureCount),
                  static_cast<unsigned long long>(stats.presentAttemptCount),
                  static_cast<unsigned long long>(stats.presentedCommandBufferCount),
                  static_cast<unsigned long long>(stats.presentFailureCount),
                  static_cast<unsigned long long>(stats.frameGraphPassAttemptCount),
                  static_cast<unsigned long long>(stats.frameGraphPassEncodedCount),
                  static_cast<unsigned long long>(stats.frameGraphPassFailureCount),
                  static_cast<unsigned long long>(stats.frameGraphDependencyEncodedCount),
                  static_cast<unsigned long long>(stats.frameGraphAccessEncodedCount),
                  static_cast<unsigned long long>(stats.frameGraphTransitionEncodedCount),
                  static_cast<unsigned long long>(stats.frameGraphAttachmentTransitionCount),
                  static_cast<unsigned long long>(stats.frameGraphBufferTransitionCount),
                  static_cast<unsigned long long>(stats.frameGraphShaderTransitionCount),
                  static_cast<unsigned long long>(stats.frameGraphCopyTransitionCount),
                  static_cast<unsigned long long>(stats.frameGraphPresentTransitionCount),
                  static_cast<unsigned long long>(stats.frameGraphOtherTransitionCount),
                  static_cast<unsigned long long>(stats.frameGraphAttachmentAccessCount),
                  static_cast<unsigned long long>(stats.frameGraphBufferAccessCount),
                  static_cast<unsigned long long>(stats.frameGraphShaderAccessCount),
                  static_cast<unsigned long long>(stats.frameGraphCopyAccessCount),
                  static_cast<unsigned long long>(stats.frameGraphPresentAccessCount),
                  static_cast<unsigned long long>(stats.frameGraphOtherAccessCount),
                  static_cast<unsigned long long>(stats.frameGraphShaderStageHintAccessCount),
                  static_cast<unsigned long long>(stats.frameGraphVertexStageHintAccessCount),
                  static_cast<unsigned long long>(stats.frameGraphFragmentStageHintAccessCount),
                  static_cast<unsigned long long>(stats.frameGraphComputeStageHintAccessCount),
                  static_cast<unsigned long long>(stats.frameGraphResourceUseAttemptCount),
                  static_cast<unsigned long long>(stats.frameGraphResourceUseDeclaredCount),
                  static_cast<unsigned long long>(stats.frameGraphResourceUseSkippedCount),
                  static_cast<unsigned long long>(stats.frameGraphResourceUseFailureCount),
                  static_cast<unsigned long long>(stats.frameGraphBufferUseDeclaredCount),
                  static_cast<unsigned long long>(stats.frameGraphTextureUseDeclaredCount),
                  static_cast<unsigned long long>(stats.frameGraphVertexStageUseDeclaredCount),
                  static_cast<unsigned long long>(stats.frameGraphFragmentStageUseDeclaredCount),
                  stats.lastFrameGraphPassIndex,
                  stats.lastFrameGraphDependencyOffset,
                  stats.lastFrameGraphDependencyCount,
                  stats.lastFrameGraphTransitionOffset,
                  stats.lastFrameGraphTransitionCount,
                  stats.lastFrameGraphAccessOffset,
                  stats.lastFrameGraphAccessCount,
                  Next::RHI::QueueClassName(stats.lastFrameGraphPassQueueClass),
                  stats.lastFrameGraphResourceUsePassIndex,
                  stats.lastFrameGraphResourceUseAccessOffset,
                  stats.lastFrameGraphResourceUseAccessCount,
                  static_cast<unsigned long long>(stats.lastFrameGraphResourceUseDeclaredCount),
                  static_cast<unsigned long long>(stats.lastFrameGraphResourceUseSkippedCount),
                  stats.HasSubmittedFrames() ? "true" : "false",
                  stats.HasBeginAttempts() ? "true" : "false",
                  stats.HasBegunCommandBuffers() ? "true" : "false",
                  stats.HasRenderPassAttempts() ? "true" : "false",
                  stats.HasRenderPasses() ? "true" : "false",
                  stats.HasCommitAttempts() ? "true" : "false",
                  stats.HasCommittedCommandBuffers() ? "true" : "false",
                  stats.HasPresentAttempts() ? "true" : "false",
                  stats.HasPresentedCommandBuffers() ? "true" : "false",
                  stats.HasFrameGraphPassAttempts() ? "true" : "false",
                  stats.HasFrameGraphPasses() ? "true" : "false",
                  stats.HasFrameGraphDependencies() ? "true" : "false",
                  stats.HasFrameGraphAccesses() ? "true" : "false",
                  stats.HasFrameGraphTransitions() ? "true" : "false",
                  stats.HasFrameGraphTransitionSummary() ? "true" : "false",
                  stats.HasFrameGraphAccessSummary() ? "true" : "false",
                  stats.HasFrameGraphShaderStageHints() ? "true" : "false",
                  stats.HasFrameGraphResourceUses() ? "true" : "false",
                  stats.HasFrameGraphStageResourceUses() ? "true" : "false",
                  stats.HasCommandActivity() ? "true" : "false",
                  stats.HasFailures() ? "true" : "false");
}

void LogRendererRenderPassStats(Next::Renderer& renderer) {
    const Next::RendererRenderPassStats stats = renderer.GetRenderPassStats();
    const std::string summary = stats.BuildLogSummary();
    NEXT_LOG_INFO("%s", summary.c_str());
}

void LogRendererUploadQueueStats(Next::Renderer& renderer) {
    const Next::RendererUploadQueueStats stats = renderer.GetUploadQueueStats();
    NEXT_LOG_INFO("Renderer upload queue stats: ready=%s queue=%s dedicated=%s pendingUploads=%llu "
                  "pendingBytes=%llu stagingCapacity=%llu submitted=%llu completed=%llu failed=%llu "
                  "finished=%llu lastSubmitted=%llu retainedStatuses=%llu retainedCapacity=%llu "
                  "hasStatuses=%s hasStatusCapacity=%s hasPending=%s hasPendingBytes=%s hasStaging=%s "
                  "hasSubmitted=%s hasCompleted=%s hasFinished=%s hasLastSubmitted=%s "
                  "hasUploadActivity=%s hasFailures=%s",
                  stats.ready ? "true" : "false",
                  Next::RHI::QueueClassName(stats.queueClass),
                  stats.dedicatedQueue ? "true" : "false",
                  static_cast<unsigned long long>(stats.pendingUploadCount),
                  static_cast<unsigned long long>(stats.pendingUploadBytes),
                  static_cast<unsigned long long>(stats.stagingCapacityBytes),
                  static_cast<unsigned long long>(stats.submittedUploadCount),
                  static_cast<unsigned long long>(stats.completedUploadCount),
                  static_cast<unsigned long long>(stats.failedUploadCount),
                  static_cast<unsigned long long>(stats.FinishedUploadCount()),
                  static_cast<unsigned long long>(stats.lastSubmittedUpload),
                  static_cast<unsigned long long>(stats.retainedStatusCount),
                  static_cast<unsigned long long>(stats.retainedStatusCapacity),
                  stats.HasRetainedStatuses() ? "true" : "false",
                  stats.HasRetainedStatusCapacity() ? "true" : "false",
                  stats.HasPendingUploads() ? "true" : "false",
                  stats.HasPendingUploadBytes() ? "true" : "false",
                  stats.HasStagingCapacity() ? "true" : "false",
                  stats.HasSubmittedUploads() ? "true" : "false",
                  stats.HasCompletedUploads() ? "true" : "false",
                  stats.HasFinishedUploads() ? "true" : "false",
                  stats.HasLastSubmittedUpload() ? "true" : "false",
                  stats.HasUploadActivity() ? "true" : "false",
                  stats.HasFailures() ? "true" : "false");
}

void LogRendererResourcePoolStats(Next::Renderer& renderer) {
    const Next::RendererResourcePoolStats stats = renderer.GetResourcePoolStats();
    uint64_t bufferDeviceLocalRemaining = 0;
    uint64_t textureDeviceLocalRemaining = 0;
    stats.BudgetRemaining(Next::RHI::ResourceType::Buffer,
                          Next::RHI::ResourceMemory::DeviceLocal,
                          bufferDeviceLocalRemaining);
    stats.BudgetRemaining(Next::RHI::ResourceType::Texture,
                          Next::RHI::ResourceMemory::DeviceLocal,
                          textureDeviceLocalRemaining);
    NEXT_LOG_INFO("Renderer resource pool stats: liveResources=%llu liveBytes=%llu peakResources=%llu "
                  "peakBytes=%llu failedAllocs=%llu failedBytes=%llu hasLive=%s hasPeak=%s "
                  "hasBudgets=%s overBudget=%s hasFailedAllocs=%s hasPoolActivity=%s "
                  "bufferDeviceBudget=%llu bufferDeviceRemaining=%llu "
                  "textureDeviceBudget=%llu textureDeviceRemaining=%llu "
                  "buffers(live=%llu liveBytes=%llu peak=%llu peakBytes=%llu failed=%llu failedBytes=%llu) "
                  "textures(live=%llu liveBytes=%llu peak=%llu peakBytes=%llu failed=%llu failedBytes=%llu)",
                  static_cast<unsigned long long>(stats.TotalLiveResourceCount()),
                  static_cast<unsigned long long>(stats.TotalLiveBytes()),
                  static_cast<unsigned long long>(stats.TotalPeakResourceCount()),
                  static_cast<unsigned long long>(stats.TotalPeakBytes()),
                  static_cast<unsigned long long>(stats.TotalFailedAllocationCount()),
                  static_cast<unsigned long long>(stats.TotalFailedAllocationBytes()),
                  stats.HasLiveResources() ? "true" : "false",
                  stats.HasPeakResources() ? "true" : "false",
                  stats.HasMemoryBudgets() ? "true" : "false",
                  stats.IsAnyOverBudget() ? "true" : "false",
                  stats.HasFailedAllocations() ? "true" : "false",
                  stats.HasPoolActivity() ? "true" : "false",
                  static_cast<unsigned long long>(stats.buffers.deviceLocal.budgetBytes),
                  static_cast<unsigned long long>(bufferDeviceLocalRemaining),
                  static_cast<unsigned long long>(stats.textures.deviceLocal.budgetBytes),
                  static_cast<unsigned long long>(textureDeviceLocalRemaining),
                  static_cast<unsigned long long>(stats.buffers.liveResourceCount),
                  static_cast<unsigned long long>(stats.buffers.liveBytes),
                  static_cast<unsigned long long>(stats.buffers.peakResourceCount),
                  static_cast<unsigned long long>(stats.buffers.peakBytes),
                  static_cast<unsigned long long>(stats.buffers.failedAllocationCount),
                  static_cast<unsigned long long>(stats.buffers.failedAllocationBytes),
                  static_cast<unsigned long long>(stats.textures.liveResourceCount),
                  static_cast<unsigned long long>(stats.textures.liveBytes),
                  static_cast<unsigned long long>(stats.textures.peakResourceCount),
                  static_cast<unsigned long long>(stats.textures.peakBytes),
                  static_cast<unsigned long long>(stats.textures.failedAllocationCount),
                  static_cast<unsigned long long>(stats.textures.failedAllocationBytes));
}

void LogRendererPipelineStats(Next::Renderer& renderer) {
    const Next::RendererPipelineStats stats = renderer.GetPipelineStats();
    const Next::RendererSwapchainStats swapchainStats = renderer.GetSwapchainStats();
    const bool formatsMatchSwapchain = Next::RendererPipelineFormatsMatchSwapchain(stats, swapchainStats);
    const Next::RendererPipelineColorAttachmentInfo* firstColorAttachment = stats.GetColorAttachment(0);
    const Next::RendererPipelineVertexBufferInfo* firstVertexBuffer = stats.GetVertexBuffer(0);
    const Next::RendererPipelineVertexAttributeInfo* firstVertexAttribute = stats.GetVertexAttribute(0);
    NEXT_LOG_INFO("Renderer pipeline stats: ready=%s shaderReady=%s shaderSource=%s shaderManifestVersion=%u "
                  "shaderRequiredArgumentBufferTier=%s shaderMaterialSrgArgumentBuffer=%u "
                  "shaderMaterialSrgArgs=%u/%u/%u shaderMaterialLayout=%s shaderPipelineLayout=%s "
                  "pipelineDebugName=\"%s\" color=%s depth=%s samples=%u formatsMatchSwapchain=%s "
                  "colorAttachments=%u colorAttachmentCapacity=%u hasColorAttachments=%s "
                  "hasMultipleColorAttachments=%s completeColorAttachments=%s "
                  "firstAttachment=%s/%s/%s/0x%02x "
                  "topology=%s fill=%s cull=%s frontFace=%s depthBias=%d depthBiasClamp=%.2f "
                  "depthBiasSlope=%.2f depthClip=%s alphaToCoverage=%s "
                  "depthTest=%s depthWrite=%s depthCompare=%s stencilTest=%s "
                  "stencilReadMask=0x%02x stencilWriteMask=0x%02x "
                  "frontStencil=%s/%s/%s/%s backStencil=%s/%s/%s/%s "
                  "blend=%s srcColor=%s dstColor=%s colorOp=%s srcAlpha=%s dstAlpha=%s "
                  "alphaOp=%s writeMask=0x%02x "
                  "hasRaster=%s hasTriangleTopology=%s hasDepthBias=%s hasDepthClip=%s "
                  "hasAlphaToCoverage=%s hasDepthStencil=%s hasDepthTest=%s hasDepthWrite=%s "
                  "hasStencil=%s hasStencilMasks=%s hasStencilFaceState=%s "
                  "hasActiveStencilFace=%s hasNonDefaultStencilFace=%s "
                  "hasColorBlendState=%s hasColorBlend=%s "
                  "hasColorWriteMask=%s writesAllColors=%s "
                  "vertexBuffers=%u vertexAttributes=%u primaryVertex=%u vertexStride=%u "
                  "vertexBufferCapacity=%u vertexAttributeCapacity=%u firstVertexBuffer=%s/%u/%s/%u "
                  "firstVertexAttribute=%s/location%u/buffer%u/%s/offset%u "
                  "hasVertexInput=%s hasDetailedVertexInput=%s hasCompleteVertexBuffers=%s "
                  "hasCompleteVertexAttributes=%s hasMultipleVertexBuffers=%s hasMultipleVertexAttributes=%s "
                  "shaderDebugName=\"%s\" shaderManifest=%s shaderPath=%s "
                  "vertexEntry=%s fragmentEntry=%s "
                  "cached=%llu requests=%llu hits=%llu misses=%llu failedCreates=%llu "
                  "hasShader=%s hasPipelineName=%s hasPipelineColor=%s hasPipelineDepth=%s hasPipelineSamples=%s "
                  "hasDebugName=%s hasManifestVersion=%s hasRequiredArgumentBufferTier=%s hasMaterialSrgIndex=%s "
                  "hasMaterialSrgArgs=%s hasMaterialLayout=%s hasPipelineLayout=%s "
                  "hasManifest=%s hasShaderPath=%s "
                  "hasEntries=%s hasCompleteShaderDesc=%s hasCached=%s hasRequests=%s hasHits=%s "
                  "hasMisses=%s hasCacheActivity=%s hasCreateFailures=%s",
                  stats.ready ? "true" : "false",
                  stats.shaderLibraryReady ? "true" : "false",
                  Next::RendererShaderLibrarySourceName(stats.shaderLibrarySource),
                  stats.shaderManifestVersion,
                  Next::RHI::ArgumentBufferTierName(stats.shaderRequiredArgumentBufferTier),
                  stats.shaderMaterialShaderResourceGroupArgumentBufferIndex,
                  stats.shaderMaterialShaderResourceGroupUniformArgumentIndex,
                  stats.shaderMaterialShaderResourceGroupTextureArgumentBaseIndex,
                  stats.shaderMaterialShaderResourceGroupSamplerArgumentBaseIndex,
                  stats.GetShaderMaterialLayout(),
                  stats.GetShaderPipelineLayout(),
                  stats.GetPipelineDebugName(),
                  Next::RHI::FormatName(stats.pipelineColorFormat),
                  Next::RHI::FormatName(stats.pipelineDepthStencilFormat),
                  stats.pipelineSampleCount,
                  formatsMatchSwapchain ? "true" : "false",
                  stats.colorAttachmentCount,
                  static_cast<unsigned>(Next::kRendererPipelineColorAttachmentMaxCount),
                  stats.HasColorAttachments() ? "true" : "false",
                  stats.HasMultipleColorAttachments() ? "true" : "false",
                  stats.HasCompleteColorAttachmentTable() ? "true" : "false",
                  firstColorAttachment ? "true" : "false",
                  Next::RHI::FormatName(firstColorAttachment ? firstColorAttachment->format
                                                             : Next::RHI::Format::Unknown),
                  firstColorAttachment && firstColorAttachment->blendEnabled ? "true" : "false",
                  firstColorAttachment ? static_cast<unsigned>(firstColorAttachment->writeMask) : 0u,
                  Next::RHI::PrimitiveTopologyName(stats.primitiveTopology),
                  Next::RHI::FillModeName(stats.fillMode),
                  Next::RHI::CullModeName(stats.cullMode),
                  Next::RHI::FrontFaceWindingName(stats.frontFace),
                  stats.depthBias,
                  stats.depthBiasClamp,
                  stats.depthBiasSlopeScale,
                  stats.depthClipEnabled ? "true" : "false",
                  stats.alphaToCoverageEnabled ? "true" : "false",
                  stats.depthTestEnabled ? "true" : "false",
                  stats.depthWriteEnabled ? "true" : "false",
                  Next::RHI::CompareFunctionName(stats.depthCompare),
                  stats.stencilTestEnabled ? "true" : "false",
                  static_cast<unsigned>(stats.stencilReadMask),
                  static_cast<unsigned>(stats.stencilWriteMask),
                  Next::RHI::CompareFunctionName(stats.frontStencilCompare),
                  Next::RHI::StencilOperationName(stats.frontStencilFailOperation),
                  Next::RHI::StencilOperationName(stats.frontStencilDepthFailOperation),
                  Next::RHI::StencilOperationName(stats.frontStencilPassOperation),
                  Next::RHI::CompareFunctionName(stats.backStencilCompare),
                  Next::RHI::StencilOperationName(stats.backStencilFailOperation),
                  Next::RHI::StencilOperationName(stats.backStencilDepthFailOperation),
                  Next::RHI::StencilOperationName(stats.backStencilPassOperation),
                  stats.colorBlendEnabled ? "true" : "false",
                  Next::RHI::BlendFactorName(stats.sourceColorBlendFactor),
                  Next::RHI::BlendFactorName(stats.destinationColorBlendFactor),
                  Next::RHI::BlendOperationName(stats.colorBlendOperation),
                  Next::RHI::BlendFactorName(stats.sourceAlphaBlendFactor),
                  Next::RHI::BlendFactorName(stats.destinationAlphaBlendFactor),
                  Next::RHI::BlendOperationName(stats.alphaBlendOperation),
                  static_cast<unsigned>(stats.colorWriteMask),
                  stats.HasRasterState() ? "true" : "false",
                  stats.HasTriangleTopology() ? "true" : "false",
                  stats.HasDepthBias() ? "true" : "false",
                  stats.HasDepthClipState() ? "true" : "false",
                  stats.HasAlphaToCoverage() ? "true" : "false",
                  stats.HasDepthStencilState() ? "true" : "false",
                  stats.HasDepthTesting() ? "true" : "false",
                  stats.HasDepthWriting() ? "true" : "false",
                  stats.HasStencilTesting() ? "true" : "false",
                  stats.HasStencilMasks() ? "true" : "false",
                  stats.HasStencilFaceState() ? "true" : "false",
                  stats.HasActiveStencilFaceState() ? "true" : "false",
                  stats.HasNonDefaultStencilFaceState() ? "true" : "false",
                  stats.HasColorBlendState() ? "true" : "false",
                  stats.HasColorBlend() ? "true" : "false",
                  stats.HasColorWriteMask() ? "true" : "false",
                  stats.WritesAllColorChannels() ? "true" : "false",
                  stats.vertexBufferCount,
                  stats.vertexAttributeCount,
                  stats.primaryVertexBufferIndex,
                  stats.primaryVertexStride,
                  static_cast<unsigned>(Next::kRendererPipelineVertexBufferMaxCount),
                  static_cast<unsigned>(Next::kRendererPipelineVertexAttributeMaxCount),
                  firstVertexBuffer ? "true" : "false",
                  firstVertexBuffer ? firstVertexBuffer->stride : 0u,
                  Next::RHI::VertexStepFunctionName(firstVertexBuffer ? firstVertexBuffer->stepFunction
                                                                      : Next::RHI::VertexStepFunction::PerVertex),
                  firstVertexBuffer ? firstVertexBuffer->stepRate : 0u,
                  firstVertexAttribute ? "true" : "false",
                  firstVertexAttribute ? firstVertexAttribute->location : 0u,
                  firstVertexAttribute ? firstVertexAttribute->bufferIndex : 0u,
                  Next::RHI::VertexFormatName(firstVertexAttribute ? firstVertexAttribute->format
                                                                   : Next::RHI::VertexFormat::Unknown),
                  firstVertexAttribute ? firstVertexAttribute->offset : 0u,
                  stats.HasVertexInputLayout() ? "true" : "false",
                  stats.HasDetailedVertexInputLayout() ? "true" : "false",
                  stats.HasCompleteVertexBufferTable() ? "true" : "false",
                  stats.HasCompleteVertexAttributeTable() ? "true" : "false",
                  stats.HasMultipleVertexBuffers() ? "true" : "false",
                  stats.HasMultipleVertexAttributes() ? "true" : "false",
                  stats.GetShaderDebugName(),
                  stats.GetShaderManifestPath(),
                  stats.GetShaderLibraryPath(),
                  stats.GetShaderVertexEntryPoint(),
                  stats.GetShaderFragmentEntryPoint(),
                  static_cast<unsigned long long>(stats.cachedPipelineCount),
                  static_cast<unsigned long long>(stats.requestCount),
                  static_cast<unsigned long long>(stats.hitCount),
                  static_cast<unsigned long long>(stats.missCount),
                  static_cast<unsigned long long>(stats.failedCreateCount),
                  stats.HasShaderLibrary() ? "true" : "false",
                  stats.HasPipelineDebugName() ? "true" : "false",
                  stats.HasPipelineColorFormat() ? "true" : "false",
                  stats.HasPipelineDepthStencilFormat() ? "true" : "false",
                  stats.HasPipelineSampleCount() ? "true" : "false",
                  stats.HasShaderDebugName() ? "true" : "false",
                  stats.HasShaderManifestVersion() ? "true" : "false",
                  stats.HasShaderRequiredArgumentBufferTier() ? "true" : "false",
                  stats.HasShaderMaterialShaderResourceGroupArgumentBufferIndex() ? "true" : "false",
                  stats.HasShaderMaterialShaderResourceGroupArgumentLayout() ? "true" : "false",
                  stats.HasShaderMaterialLayout() ? "true" : "false",
                  stats.HasShaderPipelineLayout() ? "true" : "false",
                  stats.HasShaderManifest() ? "true" : "false",
                  stats.HasShaderLibraryPath() ? "true" : "false",
                  stats.HasShaderEntryPoints() ? "true" : "false",
                  stats.HasCompleteShaderDescriptor() ? "true" : "false",
                  stats.HasCachedPipelines() ? "true" : "false",
                  stats.HasPipelineRequests() ? "true" : "false",
                  stats.HasCacheHits() ? "true" : "false",
                  stats.HasCacheMisses() ? "true" : "false",
                  stats.HasPipelineCacheActivity() ? "true" : "false",
                  stats.HasCreateFailures() ? "true" : "false");
}

void LogRendererGeometryStats(Next::Renderer& renderer) {
    const Next::RendererGeometryStats stats = renderer.GetGeometryStats();
    NEXT_LOG_INFO("Renderer geometry stats: ready=%s vertexReady=%s indexReady=%s "
                  "vertexBuffer=%u vertexStride=%u vertexBytes=%llu indexBytes=%llu "
                  "indexFormat=%s indexOffset=%llu resolvedIndexOffset=%llu "
                  "indexCount=%u instances=%u indexBase=%u vertexBase=%d instanceBase=%u "
                  "stencil=%u blend=%.2f,%.2f,%.2f,%.2f "
                  "hasVertex=%s hasIndex=%s hasStride=%s hasIndexFormat=%s "
                  "hasIndexedDraw=%s hasReadyGeometry=%s hasBlend=%s",
                  stats.ready ? "true" : "false",
                  stats.vertexBufferReady ? "true" : "false",
                  stats.indexBufferReady ? "true" : "false",
                  stats.vertexBufferIndex,
                  stats.vertexStride,
                  static_cast<unsigned long long>(stats.vertexBufferBytes),
                  static_cast<unsigned long long>(stats.indexBufferBytes),
                  Next::RHI::IndexFormatName(stats.indexFormat),
                  static_cast<unsigned long long>(stats.indexBufferByteOffset),
                  static_cast<unsigned long long>(stats.resolvedIndexBufferByteOffset),
                  stats.indexCount,
                  stats.instanceCount,
                  stats.indexOffset,
                  stats.vertexOffset,
                  stats.instanceOffset,
                  stats.stencilReference,
                  stats.blendConstant[0],
                  stats.blendConstant[1],
                  stats.blendConstant[2],
                  stats.blendConstant[3],
                  stats.HasVertexBuffer() ? "true" : "false",
                  stats.HasIndexBuffer() ? "true" : "false",
                  stats.HasVertexStride() ? "true" : "false",
                  stats.HasIndexFormat() ? "true" : "false",
                  stats.HasIndexedDraw() ? "true" : "false",
                  stats.HasReadyGeometry() ? "true" : "false",
                  stats.HasBlendConstant() ? "true" : "false");
}

void LogRendererDrawStateStats(Next::Renderer& renderer) {
    const Next::RendererDrawStateStats stats = renderer.GetDrawStateStats();
    NEXT_LOG_INFO("Renderer draw state stats: ready=%s drawable=%ux%u "
                  "viewport=%.0f,%.0f,%.0f,%.0f depth=%.2f..%.2f "
                  "scissor=%u,%u,%u,%u viewportError=%s scissorError=%s "
                  "hasDrawable=%s hasViewport=%s hasScissor=%s hasDepthRange=%s "
                  "validViewport=%s validScissor=%s validDrawState=%s "
                  "readyDrawState=%s viewportMatchesDrawable=%s scissorMatchesDrawable=%s "
                  "fullDrawableDrawState=%s",
                  stats.ready ? "true" : "false",
                  stats.drawableSize.width,
                  stats.drawableSize.height,
                  stats.viewport.minX,
                  stats.viewport.minY,
                  stats.viewport.maxX,
                  stats.viewport.maxY,
                  stats.viewport.minZ,
                  stats.viewport.maxZ,
                  stats.scissor.minX,
                  stats.scissor.minY,
                  stats.scissor.maxX,
                  stats.scissor.maxY,
                  Next::RHI::ViewportDescriptorErrorName(stats.viewportError),
                  Next::RHI::ScissorDescriptorErrorName(stats.scissorError),
                  stats.HasDrawableSize() ? "true" : "false",
                  stats.HasViewport() ? "true" : "false",
                  stats.HasScissor() ? "true" : "false",
                  stats.HasViewportDepthRange() ? "true" : "false",
                  stats.HasValidViewport() ? "true" : "false",
                  stats.HasValidScissor() ? "true" : "false",
                  stats.HasValidDrawState() ? "true" : "false",
                  stats.HasReadyDrawState() ? "true" : "false",
                  stats.ViewportMatchesDrawable() ? "true" : "false",
                  stats.ScissorMatchesDrawable() ? "true" : "false",
                  stats.HasFullDrawableDrawState() ? "true" : "false");
}

void LogRendererDrawSubmissionStats(Next::Renderer& renderer) {
    const Next::RendererDrawSubmissionStats stats = renderer.GetDrawSubmissionStats();
    NEXT_LOG_INFO("Renderer draw submission stats: ready=%s pipelineReady=%s geometryReady=%s drawStateReady=%s "
                  "lastFrame(draws=%u indexed=%u base=%u debug=%u indices=%llu instances=%llu) "
                  "submitted(frames=%llu draws=%llu indexed=%llu indices=%llu instances=%llu) "
                  "materialSrg(argBuffer=%u lastBinds=%u submittedBinds=%llu completeBinding=%s) "
                  "hasLastDraws=%s hasLastIndexed=%s hasBase=%s hasDebug=%s "
                  "hasLastIndices=%s hasLastInstances=%s hasSubmittedFrames=%s "
                  "hasSubmittedDraws=%s hasSubmittedIndexed=%s hasSubmittedIndices=%s "
                  "hasSubmittedInstances=%s hasMaterialSrgBindIndex=%s hasLastMaterialSrgBinds=%s "
                  "hasSubmittedMaterialSrgBinds=%s hasRequiredState=%s hasReadySubmission=%s hasActivity=%s",
                  stats.ready ? "true" : "false",
                  stats.pipelineReady ? "true" : "false",
                  stats.geometryReady ? "true" : "false",
                  stats.drawStateReady ? "true" : "false",
                  stats.lastFrameDrawCount,
                  stats.lastFrameIndexedDrawCount,
                  stats.lastFrameBaseDrawCount,
                  stats.lastFrameDebugDrawCount,
                  static_cast<unsigned long long>(stats.lastFrameIndexCount),
                  static_cast<unsigned long long>(stats.lastFrameInstanceCount),
                  static_cast<unsigned long long>(stats.submittedFrameCount),
                  static_cast<unsigned long long>(stats.submittedDrawCount),
                  static_cast<unsigned long long>(stats.submittedIndexedDrawCount),
                  static_cast<unsigned long long>(stats.submittedIndexCount),
                  static_cast<unsigned long long>(stats.submittedInstanceCount),
                  stats.materialShaderResourceGroupArgumentBufferBindingIndex,
                  stats.lastFrameMaterialShaderResourceGroupArgumentBufferBindCount,
                  static_cast<unsigned long long>(
                      stats.submittedMaterialShaderResourceGroupArgumentBufferBindCount),
                  stats.HasCompleteMaterialShaderResourceGroupArgumentBufferBinding() ? "true" : "false",
                  stats.HasLastFrameDraws() ? "true" : "false",
                  stats.HasLastFrameIndexedDraws() ? "true" : "false",
                  stats.HasLastFrameBaseDraws() ? "true" : "false",
                  stats.HasLastFrameDebugDraws() ? "true" : "false",
                  stats.HasLastFrameIndices() ? "true" : "false",
                  stats.HasLastFrameInstances() ? "true" : "false",
                  stats.HasSubmittedFrames() ? "true" : "false",
                  stats.HasSubmittedDraws() ? "true" : "false",
                  stats.HasSubmittedIndexedDraws() ? "true" : "false",
                  stats.HasSubmittedIndices() ? "true" : "false",
                  stats.HasSubmittedInstances() ? "true" : "false",
                  stats.HasMaterialShaderResourceGroupArgumentBufferBindingIndex() ? "true" : "false",
                  stats.HasLastFrameMaterialShaderResourceGroupArgumentBufferBindings() ? "true" : "false",
                  stats.HasSubmittedMaterialShaderResourceGroupArgumentBufferBindings() ? "true" : "false",
                  stats.HasRequiredDrawState() ? "true" : "false",
                  stats.HasReadySubmission() ? "true" : "false",
                  stats.HasSubmissionActivity() ? "true" : "false");
}

void LogRendererDrawItemStats(Next::Renderer& renderer) {
    const Next::RendererDrawItemStats stats = renderer.GetDrawItemStats();
    const Next::RendererDrawItemInfo* firstItem = stats.GetFirstItem();
    const Next::RendererDrawItemInfo* lastItem = stats.GetLastItem();
    const Next::RendererDrawItemInfo emptyItem;
    if (!firstItem) {
        firstItem = &emptyItem;
    }
    if (!lastItem) {
        lastItem = &emptyItem;
    }

    NEXT_LOG_INFO("Renderer draw item stats: ready=%s items=%u capacity=%u base=%u debug=%u placeholders=%u "
                  "culledDebug=%u hasItems=%s hasBase=%s hasDebug=%s hasPlaceholders=%s "
                  "hasCulled=%s completeTable=%s readyItems=%s "
                  "first(kind=%s draw=%u debugCell=%u hasDebugCell=%s placeholder=%s uniform=%u+%llu "
                  "vertex=%u stride=%u indexFormat=%s indexOffset=%llu resolvedIndexOffset=%llu "
                  "indexCount=%u instances=%u ready=%s) "
                  "last(kind=%s draw=%u debugCell=%u hasDebugCell=%s placeholder=%s uniform=%u+%llu "
                  "vertex=%u stride=%u indexFormat=%s indexOffset=%llu resolvedIndexOffset=%llu "
                  "indexCount=%u instances=%u ready=%s)",
                  stats.ready ? "true" : "false",
                  stats.itemCount,
                  stats.capacity,
                  stats.baseItemCount,
                  stats.debugItemCount,
                  stats.placeholderDebugItemCount,
                  stats.culledDebugItemCount,
                  stats.HasItems() ? "true" : "false",
                  stats.HasBaseItem() ? "true" : "false",
                  stats.HasDebugItems() ? "true" : "false",
                  stats.HasPlaceholderDebugItems() ? "true" : "false",
                  stats.HasCulledDebugItems() ? "true" : "false",
                  stats.HasCompleteDrawItemTable() ? "true" : "false",
                  stats.HasReadyItems() ? "true" : "false",
                  Next::RendererDrawItemKindName(firstItem->kind),
                  firstItem->drawIndex,
                  firstItem->debugCellIndex,
                  firstItem->HasDebugCellIndex() ? "true" : "false",
                  firstItem->debugCellPlaceholder ? "true" : "false",
                  firstItem->uniformBufferIndex,
                  static_cast<unsigned long long>(firstItem->uniformBufferOffset),
                  firstItem->vertexBufferIndex,
                  firstItem->vertexStride,
                  Next::RHI::IndexFormatName(firstItem->indexFormat),
                  static_cast<unsigned long long>(firstItem->indexBufferByteOffset),
                  static_cast<unsigned long long>(firstItem->resolvedIndexBufferByteOffset),
                  firstItem->indexCount,
                  firstItem->instanceCount,
                  firstItem->HasReadyItem() ? "true" : "false",
                  Next::RendererDrawItemKindName(lastItem->kind),
                  lastItem->drawIndex,
                  lastItem->debugCellIndex,
                  lastItem->HasDebugCellIndex() ? "true" : "false",
                  lastItem->debugCellPlaceholder ? "true" : "false",
                  lastItem->uniformBufferIndex,
                  static_cast<unsigned long long>(lastItem->uniformBufferOffset),
                  lastItem->vertexBufferIndex,
                  lastItem->vertexStride,
                  Next::RHI::IndexFormatName(lastItem->indexFormat),
                  static_cast<unsigned long long>(lastItem->indexBufferByteOffset),
                  static_cast<unsigned long long>(lastItem->resolvedIndexBufferByteOffset),
                  lastItem->indexCount,
                  lastItem->instanceCount,
                  lastItem->HasReadyItem() ? "true" : "false");
}

void LogRendererSamplerStats(Next::Renderer& renderer) {
    const Next::RendererSamplerStats stats = renderer.GetSamplerStats();
    NEXT_LOG_INFO("Renderer sampler stats: ready=%s cached=%llu materialSlots=%u "
                  "boundMaterialSamplers=%u hasCached=%s hasSlots=%s hasBound=%s "
                  "completeMaterialTable=%s hasSamplerActivity=%s",
                  stats.ready ? "true" : "false",
                  static_cast<unsigned long long>(stats.cachedSamplerCount),
                  stats.materialSamplerSlotCount,
                  stats.boundMaterialSamplerCount,
                  stats.HasCachedSamplers() ? "true" : "false",
                  stats.HasMaterialSamplerSlots() ? "true" : "false",
                  stats.HasBoundMaterialSamplers() ? "true" : "false",
                  stats.HasCompleteMaterialSamplerTable() ? "true" : "false",
                  stats.HasSamplerActivity() ? "true" : "false");
}

void LogRendererMaterialStats(Next::Renderer& renderer) {
    const Next::RendererMaterialStats stats = renderer.GetMaterialStats();
    const Next::RendererMaterialBindingInfo baseColor =
        stats.GetActiveMaterialBinding(Next::RendererTextureSlot::BaseColor);
    const Next::RendererMaterialBindingInfo normal =
        stats.GetActiveMaterialBinding(Next::RendererTextureSlot::Normal);
    const Next::RendererMaterialBindingInfo metallicRoughness =
        stats.GetActiveMaterialBinding(Next::RendererTextureSlot::MetallicRoughness);
    const Next::RendererMaterialBindingInfo emissive =
        stats.GetActiveMaterialBinding(Next::RendererTextureSlot::Emissive);
    const Next::RendererMaterialBindingInfo occlusion =
        stats.GetActiveMaterialBinding(Next::RendererTextureSlot::Occlusion);
    const Next::RendererShaderResourceBindingInfo* uniformSrgBinding =
        stats.GetShaderResourceGroupBinding(0);
    const Next::RendererShaderResourceBindingInfo* textureSrgBinding =
        stats.GetShaderResourceGroupBinding(1);
    const Next::RendererShaderResourceBindingInfo* samplerSrgBinding =
        stats.GetShaderResourceGroupBinding(2);
    NEXT_LOG_INFO("Renderer material stats: ready=%s capacity=%u count=%u active=%llu "
                  "activeIndex=%u activeBoundTextures=%u activeComplete=%s activeValid=%s "
                  "shaderTextures=%u shaderSamplers=%u fallbackTextures=%u completeShaderBindings=%s "
                  "hasCapacity=%s hasMaterials=%s hasActive=%s hasActiveSlot=%s "
                  "hasFreeSlots=%s tableFull=%s hasActiveBound=%s hasShaderTextures=%s "
                  "hasShaderSamplers=%s hasFallbacks=%s hasMaterialActivity=%s "
                  "layout(vertex=%u uniform=%u textures=%u+%u samplers=%u+%u complete=%s) "
                  "srg(bindings=%u table=%s valid=%s resources=%s encoder=%s args=%u length=%llu "
                  "stride=%llu argBuffer=%s bytes=%llu drawCapacity=%u encodedDraws=%u "
                  "bind=%u bound=%s binds=%llu "
                  "encoded=%u completeEncoding=%s completeBinding=%s "
                  "error=%s register=%u conflict=%u "
                  "uniform=%s@%u+%u/%u texture=%s@%u+%u/%u sampler=%s@%u+%u/%u) "
                  "bindingSources(base=%s normal=%s metallicRoughness=%s emissive=%s occlusion=%s) "
                  "bindingIndices(uniform=%u base=%u/%u normal=%u/%u metallicRoughness=%u/%u "
                  "emissive=%u/%u occlusion=%u/%u)",
                  stats.ready ? "true" : "false",
                  stats.materialCapacity,
                  stats.materialCount,
                  static_cast<unsigned long long>(stats.activeMaterial.id),
                  stats.activeMaterialIndex,
                  stats.activeMaterialBoundTextureCount,
                  stats.HasCompleteActiveTextureSet() ? "true" : "false",
                  stats.HasValidActiveParameters() ? "true" : "false",
                  stats.shaderVisibleTextureCount,
                  stats.shaderVisibleSamplerCount,
                  stats.fallbackTextureCount,
                  stats.HasCompleteShaderBindings() ? "true" : "false",
                  stats.HasMaterialCapacity() ? "true" : "false",
                  stats.HasMaterials() ? "true" : "false",
                  stats.HasActiveMaterial() ? "true" : "false",
                  stats.HasActiveMaterialSlot() ? "true" : "false",
                  stats.HasFreeMaterialSlots() ? "true" : "false",
                  stats.IsMaterialTableFull() ? "true" : "false",
                  stats.HasActiveBoundTextures() ? "true" : "false",
                  stats.HasShaderVisibleTextures() ? "true" : "false",
                  stats.HasShaderVisibleSamplers() ? "true" : "false",
                  stats.HasFallbackTextures() ? "true" : "false",
                  stats.HasMaterialActivity() ? "true" : "false",
                  stats.bindingLayout.vertexBufferIndex,
                  stats.bindingLayout.uniformBufferIndex,
                  stats.bindingLayout.textureBindingBaseIndex,
                  stats.bindingLayout.textureBindingCount,
                  stats.bindingLayout.samplerBindingBaseIndex,
                  stats.bindingLayout.samplerBindingCount,
                  stats.HasMaterialBindingLayout() ? "true" : "false",
                  stats.shaderResourceGroupBindingCount,
                  stats.HasCompleteShaderResourceGroupBindingTable() ? "true" : "false",
                  stats.HasValidShaderResourceGroupLayout() ? "true" : "false",
                  stats.HasCompleteShaderResourceGroupResources() ? "true" : "false",
                  stats.HasReadyShaderResourceGroupArgumentEncoder() ? "true" : "false",
                  stats.shaderResourceGroupArgumentCount,
                  static_cast<unsigned long long>(stats.shaderResourceGroupEncodedLength),
                  static_cast<unsigned long long>(stats.shaderResourceGroupEncodedStride),
                  stats.HasShaderResourceGroupArgumentBuffer() ? "true" : "false",
                  static_cast<unsigned long long>(stats.shaderResourceGroupArgumentBufferBytes),
                  stats.shaderResourceGroupArgumentBufferDrawCapacity,
                  stats.shaderResourceGroupEncodedDrawCount,
                  stats.shaderResourceGroupArgumentBufferBindingIndex,
                  stats.HasBoundShaderResourceGroupArgumentBuffer() ? "true" : "false",
                  static_cast<unsigned long long>(stats.shaderResourceGroupArgumentBufferBindCount),
                  stats.shaderResourceGroupEncodedResourceCount,
                  stats.HasCompleteShaderResourceGroupEncoding() ? "true" : "false",
                  stats.HasCompleteShaderResourceGroupArgumentBufferBinding() ? "true" : "false",
                  Next::RHI::ShaderResourceGroupLayoutErrorName(stats.shaderResourceGroupLayoutValidation.error),
                  stats.shaderResourceGroupLayoutValidation.registerIndex,
                  stats.shaderResourceGroupLayoutValidation.conflictBindingIndex,
                  uniformSrgBinding ? Next::RHI::ShaderResourceBindingTypeName(uniformSrgBinding->type) : "none",
                  uniformSrgBinding ? uniformSrgBinding->bindingIndex
                                    : Next::RHI::kInvalidShaderResourceBindingIndex,
                  uniformSrgBinding ? uniformSrgBinding->bindingCount : 0u,
                  uniformSrgBinding ? uniformSrgBinding->boundResourceCount : 0u,
                  textureSrgBinding ? Next::RHI::ShaderResourceBindingTypeName(textureSrgBinding->type) : "none",
                  textureSrgBinding ? textureSrgBinding->bindingIndex
                                    : Next::RHI::kInvalidShaderResourceBindingIndex,
                  textureSrgBinding ? textureSrgBinding->bindingCount : 0u,
                  textureSrgBinding ? textureSrgBinding->boundResourceCount : 0u,
                  samplerSrgBinding ? Next::RHI::ShaderResourceBindingTypeName(samplerSrgBinding->type) : "none",
                  samplerSrgBinding ? samplerSrgBinding->bindingIndex
                                    : Next::RHI::kInvalidShaderResourceBindingIndex,
                  samplerSrgBinding ? samplerSrgBinding->bindingCount : 0u,
                  samplerSrgBinding ? samplerSrgBinding->boundResourceCount : 0u,
                  Next::RendererMaterialBindingSourceName(baseColor.source),
                  Next::RendererMaterialBindingSourceName(normal.source),
                  Next::RendererMaterialBindingSourceName(metallicRoughness.source),
                  Next::RendererMaterialBindingSourceName(emissive.source),
                  Next::RendererMaterialBindingSourceName(occlusion.source),
                  baseColor.uniformBufferIndex,
                  baseColor.textureBindingIndex,
                  baseColor.samplerBindingIndex,
                  normal.textureBindingIndex,
                  normal.samplerBindingIndex,
                  metallicRoughness.textureBindingIndex,
                  metallicRoughness.samplerBindingIndex,
                  emissive.textureBindingIndex,
                  emissive.samplerBindingIndex,
                  occlusion.textureBindingIndex,
                  occlusion.samplerBindingIndex);
}

void LogRendererResourceStateStats(Next::Renderer& renderer) {
    const Next::RendererResourceStateStats stats = renderer.GetResourceStateStats();
    const Next::RendererResourceStateInfo* firstResource = stats.GetResource(0);
    const Next::RendererResourceStateInfo* baseColorTexture =
        stats.GetMaterialTexture(Next::RendererTextureSlot::BaseColor);
    const Next::RendererResourceStateInfo* normalTexture =
        stats.GetMaterialTexture(Next::RendererTextureSlot::Normal);
    NEXT_LOG_INFO("Renderer resource state stats: ready=%s resources=%u capacity=%u buffers=%u textures=%u "
                  "expectedMatches=%u frameGraph=%s transitions=%u passes=%u readyPass=%u "
                  "readyPassTransitions=%u hasFrameGraph=%s hasFrameGraphTransitions=%s "
                  "hasFrameGraphPasses=%s hasReadyPass=%s "
                  "hasResources=%s hasBuffers=%s hasTextures=%s hasMatches=%s "
                  "hasMismatches=%s completeTable=%s allExpected=%s readyStates=%s "
                  "first=%s/%s/%s->%s/binding=%u usage=0x%08x name=\"%s\" "
                  "baseColor=%s/%s->%s/binding=%u normal=%s/%s->%s/binding=%u",
                  stats.ready ? "true" : "false",
                  stats.resourceCount,
                  static_cast<unsigned int>(stats.resources.size()),
                  stats.bufferResourceCount,
                  stats.textureResourceCount,
                  stats.expectedStateMatchCount,
                  Next::RHI::FrameGraphDescriptorErrorName(stats.frameGraphValidation.error),
                  stats.frameGraphTransitionCount,
                  stats.frameGraphPassCount,
                  stats.frameGraphReadyPassIndex,
                  stats.frameGraphReadyPassTransitionCount,
                  stats.HasValidFrameGraphResourcePlan() ? "true" : "false",
                  stats.HasFrameGraphTransitions() ? "true" : "false",
                  stats.HasFrameGraphPasses() ? "true" : "false",
                  stats.HasFrameGraphReadyPass() ? "true" : "false",
                  stats.HasResources() ? "true" : "false",
                  stats.HasBuffers() ? "true" : "false",
                  stats.HasTextures() ? "true" : "false",
                  stats.HasExpectedStateMatches() ? "true" : "false",
                  stats.HasStateMismatches() ? "true" : "false",
                  stats.HasCompleteResourceStateTable() ? "true" : "false",
                  stats.HasAllExpectedStates() ? "true" : "false",
                  stats.HasReadyResourceStates() ? "true" : "false",
                  firstResource ? Next::RendererResourceStateKindName(firstResource->kind) : "none",
                  firstResource ? Next::RHI::ResourceStateName(firstResource->currentState) : "unknown",
                  firstResource ? Next::RHI::ResourceStateName(firstResource->expectedState) : "unknown",
                  firstResource && firstResource->MatchesExpectedState() ? "match" : "mismatch",
                  firstResource ? firstResource->bindingIndex : Next::kRendererInvalidBindingIndex,
                  firstResource ? firstResource->usage : 0u,
                  firstResource ? firstResource->GetDebugName() : "",
                  baseColorTexture ? "true" : "false",
                  baseColorTexture ? Next::RHI::ResourceStateName(baseColorTexture->currentState) : "unknown",
                  baseColorTexture ? Next::RHI::ResourceStateName(baseColorTexture->expectedState) : "unknown",
                  baseColorTexture ? baseColorTexture->bindingIndex : Next::kRendererInvalidBindingIndex,
                  normalTexture ? "true" : "false",
                  normalTexture ? Next::RHI::ResourceStateName(normalTexture->currentState) : "unknown",
                  normalTexture ? Next::RHI::ResourceStateName(normalTexture->expectedState) : "unknown",
                  normalTexture ? normalTexture->bindingIndex : Next::kRendererInvalidBindingIndex);
}

void LogRendererSwapchainStats(Next::Renderer& renderer) {
    const Next::RendererSwapchainStats stats = renderer.GetSwapchainStats();
    NEXT_LOG_INFO("Renderer swapchain stats: ready=%s drawable=%ux%u color=%s depth=%s framebufferOnly=%s "
                  "resizes=%llu depthCreateFailures=%llu acquireAttempts=%llu acquired=%llu "
                  "acquireFailures=%llu presented=%llu released=%llu frameAcquired=%s "
                  "hasDrawable=%s hasColor=%s hasDepth=%s hasDepthCreateFailures=%s "
                  "hasAcquireFailures=%s hasResizeActivity=%s hasAcquireAttempts=%s hasAcquired=%s "
                  "hasPresented=%s hasReleased=%s hasPresentationActivity=%s hasSwapchainActivity=%s "
                  "hasFailures=%s",
                  stats.ready ? "true" : "false",
                  stats.drawableSize.width,
                  stats.drawableSize.height,
                  Next::RHI::FormatName(stats.colorFormat),
                  Next::RHI::FormatName(stats.depthFormat),
                  stats.framebufferOnly ? "true" : "false",
                  static_cast<unsigned long long>(stats.resizeCount),
                  static_cast<unsigned long long>(stats.depthCreateFailureCount),
                  static_cast<unsigned long long>(stats.acquireAttemptCount),
                  static_cast<unsigned long long>(stats.acquiredFrameCount),
                  static_cast<unsigned long long>(stats.acquireFailureCount),
                  static_cast<unsigned long long>(stats.presentedFrameCount),
                  static_cast<unsigned long long>(stats.releasedFrameCount),
                  stats.frameAcquired ? "true" : "false",
                  stats.HasDrawableSize() ? "true" : "false",
                  stats.HasColorFormat() ? "true" : "false",
                  stats.HasDepthFormat() ? "true" : "false",
                  stats.HasDepthCreateFailures() ? "true" : "false",
                  stats.HasAcquireFailures() ? "true" : "false",
                  stats.HasResizeActivity() ? "true" : "false",
                  stats.HasAcquireAttempts() ? "true" : "false",
                  stats.HasAcquiredFrames() ? "true" : "false",
                  stats.HasPresentedFrames() ? "true" : "false",
                  stats.HasReleasedFrames() ? "true" : "false",
                  stats.HasPresentationActivity() ? "true" : "false",
                  stats.HasSwapchainActivity() ? "true" : "false",
                  stats.HasFailures() ? "true" : "false");
}

bool ApplyRendererResourcePoolBudget(Next::Renderer& renderer, uint64_t budgetMegabytes) {
    if (budgetMegabytes == 0) {
        return true;
    }

    const uint64_t budgetBytes = MegabytesToBytesSaturated(budgetMegabytes);
    const Next::RHI::ResourceType resourceTypes[] = {
        Next::RHI::ResourceType::Buffer,
        Next::RHI::ResourceType::Texture,
    };
    const Next::RHI::ResourceMemory memories[] = {
        Next::RHI::ResourceMemory::DeviceLocal,
        Next::RHI::ResourceMemory::Shared,
        Next::RHI::ResourceMemory::Upload,
        Next::RHI::ResourceMemory::Readback,
    };

    bool accepted = true;
    for (const Next::RHI::ResourceType resourceType : resourceTypes) {
        for (const Next::RHI::ResourceMemory memory : memories) {
            Next::RendererResourcePoolBudgetDesc budget;
            budget.resourceType = resourceType;
            budget.memory = memory;
            budget.budgetBytes = budgetBytes;
            accepted = renderer.SetResourcePoolBudget(budget) && accepted;
        }
    }

    const Next::RendererResourcePoolStats stats = renderer.GetResourcePoolStats();
    NEXT_LOG_INFO("Renderer resource pool budget configured: requestedMB=%llu accepted=%s "
                  "bufferDeviceLocal=%llu textureDeviceLocal=%llu hasBudgets=%s overBudget=%s",
                  static_cast<unsigned long long>(budgetMegabytes),
                  accepted ? "true" : "false",
                  static_cast<unsigned long long>(stats.buffers.deviceLocal.budgetBytes),
                  static_cast<unsigned long long>(stats.textures.deviceLocal.budgetBytes),
                  stats.HasMemoryBudgets() ? "true" : "false",
                  stats.IsAnyOverBudget() ? "true" : "false");
    return accepted;
}

void ApplyMaterialParameters(const Next::MaterialAssetView& materialView,
                             Next::RendererMaterialDesc& materialDesc) {
    const Next::MaterialParam* params = materialView.GetParameters();
    if (!params) {
        return;
    }

    for (uint32_t i = 0; i < materialView.header.parameterCount; ++i) {
        const Next::MaterialParam& param = params[i];
        const std::string name = FixedAssetName(param.name, sizeof(param.name));

        if (name == "metallic" && param.type == Next::MaterialParam::FLOAT) {
            materialDesc.metallic = std::clamp(param.value[0], 0.0f, 1.0f);
        } else if (name == "roughness" && param.type == Next::MaterialParam::FLOAT) {
            materialDesc.roughness = std::clamp(param.value[0], 0.0f, 1.0f);
        } else if (name == "baseColor" &&
                   (param.type == Next::MaterialParam::COLOR || param.type == Next::MaterialParam::VEC4 ||
                    param.type == Next::MaterialParam::VEC3)) {
            materialDesc.baseColorFactor[0] = std::clamp(param.value[0], 0.0f, 1.0f);
            materialDesc.baseColorFactor[1] = std::clamp(param.value[1], 0.0f, 1.0f);
            materialDesc.baseColorFactor[2] = std::clamp(param.value[2], 0.0f, 1.0f);
            if (param.type != Next::MaterialParam::VEC3) {
                materialDesc.baseColorFactor[3] = std::clamp(param.value[3], 0.0f, 1.0f);
            }
        }
    }
}

} // namespace

namespace Song {

Game::Game(const GameOptions& options)
    : options_(options)
    , running_(false)
    , initialized_(false) {
}

Game::~Game() {
    Shutdown();
}

bool Game::Initialize() {
    NEXT_LOG_INFO("Initializing Song Dynasty Game...");
    NEXT_LOG_INFO("Platform: %s", Next::GetPlatformName());

    if (initialized_) {
        NEXT_LOG_WARNING("Game already initialized");
        return true;
    }

    if (!InitializeEngine()) {
        NEXT_LOG_ERROR("Failed to initialize engine");
        return false;
    }

    initialized_ = true;
    NEXT_LOG_INFO("Game initialized successfully");
    return true;
}

bool Game::InitializeEngine() {
    // Initialize platform
    if (!Next::PlatformInitialize()) {
        NEXT_LOG_ERROR("Failed to initialize platform");
        return false;
    }

    // Initialize logger
    Next::Logger::Initialize();

    // Create window
    window_ = Next::CreateWindow();
    Next::WindowDesc windowDesc;
    windowDesc.title = "NEXT Engine - Song Dynasty (CP0 Demo)";
    windowDesc.width = 1280;
    windowDesc.height = 720;

    if (!window_ || !window_->Initialize(windowDesc)) {
        NEXT_LOG_ERROR("Failed to create window");
        delete window_;
        window_ = nullptr;
        return false;
    }

    // Input (singleton owned by platform)
    input_ = Next::GetInput();

    // Create renderer
    renderer_ = Next::Renderer::Create();
    if (!renderer_ || !renderer_->Initialize(window_)) {
        NEXT_LOG_ERROR("Failed to initialize renderer");
        delete renderer_;
        renderer_ = nullptr;
        window_->Shutdown();
        delete window_;
        window_ = nullptr;
        return false;
    }
    LogRendererDeviceInfo(*renderer_);
    if (options_.rendererResourcePoolBudgetMB > 0) {
        ApplyRendererResourcePoolBudget(*renderer_, options_.rendererResourcePoolBudgetMB);
    }

    // Initialize job system (leave one core for main thread)
    auto& jobSystem = Next::JobSystem::Instance();
    jobSystem.Initialize(0);

    // Initialize asset manager (CP3)
    auto& assetManager = Next::AssetManager::Instance();
    if (!assetManager.Initialize()) {
        NEXT_LOG_ERROR("Failed to initialize asset manager");
        return false;
    }
    QueueDemoTextureUpload();

    world_ = std::make_unique<Next::World>();

    // Initialize World Streaming (CP7) - demo integration.
    streaming_ = std::make_unique<Next::Streaming::StreamingManager>();
    Next::Streaming::StreamingManagerConfig streamingCfg;
    streamingCfg.memoryBudgetMB = 256;
    streamingCfg.loadRadius = 256.0f;
    streamingCfg.unloadRadius = 384.0f;
    streamingCfg.maxConcurrentLoads = 32;
    streamingCfg.maxConcurrentUnloads = 16;
    streamingCfg.enablePrediction = false;
    streamingCfg.allowPlaceholderCellLoad = options_.allowPlaceholderCells;
    streamingCfg.cellDataDirectory = L"data/world/cells";
    streamingCfg.cellFileExtension = L".ncell";
    streamingCfg.logStreamingEvents = true;
    if (!streaming_->Initialize(streamingCfg)) {
        NEXT_LOG_WARNING("World streaming failed to initialize (continuing without streaming)");
        streaming_.reset();
    }

    running_ = true;
    NEXT_LOG_INFO("Engine initialized");

    return true;
}

void Game::Shutdown() {
    if (!initialized_) {
        return;
    }

    running_ = false;
    ShutdownEngine();
    initialized_ = false;
    NEXT_LOG_INFO("Game shutdown complete");
}

void Game::ShutdownEngine() {
    // Cleanup in reverse order
    NEXT_LOG_INFO("Shutting down engine...");

    if (renderer_) {
        LogRendererSwapchainStats(*renderer_);
        LogRendererCommandStats(*renderer_);
        LogRendererRenderPassStats(*renderer_);
        LogRendererPipelineStats(*renderer_);
        LogRendererGeometryStats(*renderer_);
        LogRendererDrawStateStats(*renderer_);
        LogRendererDrawSubmissionStats(*renderer_);
        LogRendererDrawItemStats(*renderer_);
        LogRendererSamplerStats(*renderer_);
        LogRendererMaterialStats(*renderer_);
        LogRendererResourceStateStats(*renderer_);
        LogRendererUploadQueueStats(*renderer_);
        LogRendererResourcePoolStats(*renderer_);
        LogRendererFrameDebugStats(lastFrameDebugCellCount_,
                                   lastFrameRenderedDebugCellCount_,
                                   lastFrameDebugCellOverflowCount_,
                                   lastFramePlaceholderDebugCellCount_,
                                   lastFrameRenderedPlaceholderDebugCellCount_);
        LogRendererLifetimeStats(*renderer_);
        renderer_->Shutdown();
        delete renderer_;
        renderer_ = nullptr;
    }

    if (streaming_) {
        LogStreamingStats(*streaming_);
        LogWorldPartitionStats(*streaming_);
        LogStreamingInterestStats(*streaming_);
        LogStreamingIOStats(*streaming_);
        LogStreamingLODStats(*streaming_);
        LogStreamingEvictionStats(*streaming_);
        streaming_->Shutdown();
        streaming_.reset();
    }

    world_.reset();

    LogAssetStats(Next::AssetManager::Instance());

    Next::JobSystem::Instance().Shutdown();

    // Shutdown asset manager (CP3)
    Next::AssetManager::Instance().Shutdown();

    if (window_) {
        window_->Shutdown();
        delete window_;
        window_ = nullptr;
    }

    input_ = nullptr;

    Next::Logger::Shutdown();
    Next::PlatformShutdown();
}

void Game::Run() {
    NEXT_LOG_INFO("Starting game loop");

    if (!running_) {
        NEXT_LOG_ERROR("Game not initialized; call Initialize() first");
        return;
    }

    auto* window = window_;
    auto* input = input_;
    auto& profiler = Next::Profiler::Instance();
    auto& jobSystem = Next::JobSystem::Instance();

    if (options_.runSelfTests) {
        RunJobSystemSelfTest();
        RunAssetSystemTest();
        RunECSSelfTest();
    }

    double previousTime = Next::GetTimeInSeconds();
    const double smokeStart = previousTime;
    uint32_t frameCount = 0;
    uint32_t totalFrames = 0;
    const uint32_t LOG_INTERVAL_FRAMES = 60; // Log stats every 60 frames (approx 1 second at 60fps)

    while (!window->ShouldClose() && running_) {
        profiler.BeginFrame();

        double currentTime = Next::GetTimeInSeconds();
        float deltaTime = static_cast<float>(currentTime - previousTime);
        previousTime = currentTime;

        // Poll window events
        window->PollEvents();

        // Update input
        input->Update();

        // Handle exit
        if (input->IsKeyJustPressed(Next::KeyCode::Escape)) {
            running_ = false;
        }

        // Tick game
        Tick(deltaTime);

        // Main thread helps consume budgeted jobs (up to 0.25ms)
        jobSystem.Pump(0.25);

        // Cap framerate at 120 FPS for development
        if (deltaTime < 0.00833f) {
            Next::SleepMs(static_cast<uint32_t>((0.00833f - deltaTime) * 1000.0f));
        }

        profiler.EndFrame();

        // Log stats periodically
        frameCount++;
        totalFrames++;
        if (frameCount >= LOG_INTERVAL_FRAMES) {
            profiler.LogStats();
            frameCount = 0;
        }

        if (options_.smokeFrames > 0 && totalFrames >= options_.smokeFrames) {
            running_ = false;
        }
        if (options_.smokeSeconds > 0.0 && (currentTime - smokeStart) >= options_.smokeSeconds) {
            running_ = false;
        }
    }

    NEXT_LOG_INFO("Game loop finished");
}

void Game::Tick(float deltaTime) {
    HandleInput(deltaTime);
    UpdateGame(deltaTime);
    Render(deltaTime);
}

void Game::HandleInput(float deltaTime) {
    auto* input = Next::GetInput();

    // WASD movement (minimal camera proxy for driving streaming)
    const float speed = 240.0f;  // units/sec
    if (input->IsKeyPressed(Next::KeyCode::W)) {
        camZ_ += speed * deltaTime;
    }
    if (input->IsKeyPressed(Next::KeyCode::S)) {
        camZ_ -= speed * deltaTime;
    }
    if (input->IsKeyPressed(Next::KeyCode::A)) {
        camX_ -= speed * deltaTime;
    }
    if (input->IsKeyPressed(Next::KeyCode::D)) {
        camX_ += speed * deltaTime;
    }
}

void Game::UpdateGame(float deltaTime) {
    Next::AssetManager::Instance().PumpAsyncCallbacks();
    PollRendererTextureUpload();

    if (world_) {
        world_->Update(deltaTime);
    }

    if (streaming_) {
        const Next::Vec3 pos(camX_, camY_, camZ_);
        const Next::Vec3 last(lastCamX_, lastCamY_, lastCamZ_);
        const Next::Vec3 vel = (pos - last) * (deltaTime > 1e-6f ? (1.0f / deltaTime) : 0.0f);
        const Next::Vec3 dir(0.0f, 0.0f, 1.0f);

        streaming_->Update(deltaTime, pos, dir, vel);

        lastCamX_ = camX_;
        lastCamY_ = camY_;
        lastCamZ_ = camZ_;
    }
}

void Game::Render(float deltaTime) {
    if (!renderer_ || !window_) {
        return;
    }

    Next::RendererFrameDesc frame = {};
    frame.cameraPosition[0] = camX_;
    frame.cameraPosition[1] = camY_ + 48.0f;
    frame.cameraPosition[2] = camZ_ - 96.0f;
    frame.cameraTarget[0] = camX_;
    frame.cameraTarget[1] = camY_;
    frame.cameraTarget[2] = camZ_ + 64.0f;
    frame.cameraUp[0] = 0.0f;
    frame.cameraUp[1] = 1.0f;
    frame.cameraUp[2] = 0.0f;
    frame.deltaSeconds = deltaTime;

    if (streaming_) {
        const auto loadedCells = streaming_->GetLoadedCellInfos();
        frame.debugCells.reserve(loadedCells.size());

        for (const Next::Streaming::StreamingCellInfo& cell : loadedCells) {
            Next::RendererDebugCell debugCell = {};
            debugCell.center[0] = cell.worldPosition.x;
            debugCell.center[1] = cell.worldPosition.y;
            debugCell.center[2] = cell.worldPosition.z;
            debugCell.size = cell.ClampedCellSize();
            debugCell.flags = cell.IsPlaceholder() ? Next::kRendererDebugCellPlaceholder : 0u;
            frame.debugCells.push_back(debugCell);
        }
    }

    const Next::RendererFrameDebugStats frameDebugStats = frame.GetDebugStats();
    lastFrameDebugCellCount_ = frameDebugStats.submittedDebugCellCount;
    lastFrameRenderedDebugCellCount_ = frameDebugStats.renderedDebugCellCount;
    lastFrameDebugCellOverflowCount_ = frameDebugStats.overflowDebugCellCount;
    lastFramePlaceholderDebugCellCount_ = frameDebugStats.placeholderDebugCellCount;
    lastFrameRenderedPlaceholderDebugCellCount_ = frameDebugStats.renderedPlaceholderDebugCellCount;
    if (frameDebugStats.HasDebugCellOverflow() && !frameDebugOverflowLogged_) {
        NEXT_LOG_WARNING("Renderer frame debug cells overflow: submitted=%zu rendered=%zu overflow=%zu",
                         frameDebugStats.submittedDebugCellCount,
                         frameDebugStats.renderedDebugCellCount,
                         frameDebugStats.overflowDebugCellCount);
        frameDebugOverflowLogged_ = true;
    }

    renderer_->SetFrameDesc(frame);

    renderer_->BeginFrame();
    renderer_->Render();
    renderer_->EndFrame();
    window_->SwapBuffers();
}

void Game::QueueDemoTextureUpload() {
    if (!renderer_) {
        return;
    }

    auto& assetManager = Next::AssetManager::Instance();
    namespace fs = std::filesystem;
    fs::path packagePath = ResolveRuntimePath(fs::path("assets") / "test_package.npkg");
    if (!fs::exists(packagePath)) {
        return;
    }

    if (!assetManager.LoadPackage(packagePath.string())) {
        NEXT_LOG_WARNING("Renderer texture asset package failed to load: %s", packagePath.string().c_str());
        return;
    }

    const std::string packageName = packagePath.stem().string();
    std::shared_ptr<Next::PackageContainer> package = assetManager.GetPackage(packageName);
    if (!package) {
        return;
    }

    const std::vector<std::string> textureAssets = package->GetAssetsByType(Next::AssetType::Texture);
    if (textureAssets.empty()) {
        NEXT_LOG_DEBUG("Renderer texture upload skipped: package %s has no texture assets", packageName.c_str());
        return;
    }

    std::string selectedTextureName;
    std::string selectedNormalTextureName;
    std::string selectedMetallicRoughnessTextureName;
    std::string selectedEmissiveTextureName;
    std::string selectedOcclusionTextureName;
    Next::RendererMaterialDesc materialDesc;
    bool materialDescFromPackage = false;
    const std::vector<std::string> materialAssets = package->GetAssetsByType(Next::AssetType::Material);
    if (!materialAssets.empty()) {
        const std::string materialAssetName = packageName + "::" + materialAssets.front();
        Next::AssetHandle materialHandle = assetManager.LoadAssetSync(materialAssetName);
        if (materialHandle.IsValid()) {
            Next::MaterialAssetView materialView;
            Next::TextureRef albedoRef = {};
            if (assetManager.GetMaterialAssetView(materialHandle, materialView)) {
                materialDesc.sourceAssetId = materialHandle.GetID();
                ApplyMaterialParameters(materialView, materialDesc);
                materialDescFromPackage = true;

                if (materialView.FindTextureRef(Next::TextureRef::ALBEDO, albedoRef)) {
                    const std::string referencedTexture = FixedAssetName(albedoRef.name, sizeof(albedoRef.name));
                    if (!referencedTexture.empty() && package->HasAsset(referencedTexture)) {
                        selectedTextureName = referencedTexture;
                        NEXT_LOG_INFO("Renderer material %s selected albedo texture: %s",
                                      materialAssets.front().c_str(),
                                      selectedTextureName.c_str());
                    }
                }

                Next::TextureRef normalRef = {};
                if (materialView.FindTextureRef(Next::TextureRef::NORMAL, normalRef)) {
                    const std::string referencedTexture = FixedAssetName(normalRef.name, sizeof(normalRef.name));
                    if (!referencedTexture.empty() && package->HasAsset(referencedTexture)) {
                        selectedNormalTextureName = referencedTexture;
                        NEXT_LOG_INFO("Renderer material %s selected normal texture: %s",
                                      materialAssets.front().c_str(),
                                      selectedNormalTextureName.c_str());
                    }
                }

                Next::TextureRef metallicRoughnessRef = {};
                if (materialView.FindTextureRef(Next::TextureRef::METALLIC_ROUGHNESS, metallicRoughnessRef)) {
                    const std::string referencedTexture = FixedAssetName(metallicRoughnessRef.name,
                                                                         sizeof(metallicRoughnessRef.name));
                    if (!referencedTexture.empty() && package->HasAsset(referencedTexture)) {
                        selectedMetallicRoughnessTextureName = referencedTexture;
                        NEXT_LOG_INFO("Renderer material %s selected metallic roughness texture: %s",
                                      materialAssets.front().c_str(),
                                      selectedMetallicRoughnessTextureName.c_str());
                    }
                }

                Next::TextureRef emissiveRef = {};
                if (materialView.FindTextureRef(Next::TextureRef::EMISSIVE, emissiveRef)) {
                    const std::string referencedTexture = FixedAssetName(emissiveRef.name, sizeof(emissiveRef.name));
                    if (!referencedTexture.empty() && package->HasAsset(referencedTexture)) {
                        selectedEmissiveTextureName = referencedTexture;
                        NEXT_LOG_INFO("Renderer material %s selected emissive texture: %s",
                                      materialAssets.front().c_str(),
                                      selectedEmissiveTextureName.c_str());
                    }
                }

                Next::TextureRef occlusionRef = {};
                if (materialView.FindTextureRef(Next::TextureRef::OCCLUSION, occlusionRef)) {
                    const std::string referencedTexture = FixedAssetName(occlusionRef.name, sizeof(occlusionRef.name));
                    if (!referencedTexture.empty() && package->HasAsset(referencedTexture)) {
                        selectedOcclusionTextureName = referencedTexture;
                        NEXT_LOG_INFO("Renderer material %s selected occlusion texture: %s",
                                      materialAssets.front().c_str(),
                                      selectedOcclusionTextureName.c_str());
                    }
                }
            }
            assetManager.Release(materialHandle);
        }
    }

    if (selectedTextureName.empty()) {
        selectedTextureName = textureAssets.front();
        NEXT_LOG_INFO("Renderer material texture selection fell back to first texture: %s",
                      selectedTextureName.c_str());
    }

    auto queueMaterialTextureUpload =
        [this, packageName, materialDesc, materialDescFromPackage](const std::string& textureName,
                                                                   Next::RendererTextureSlot slot) {
    const std::string assetName = packageName + "::" + textureName;
    Next::AssetManager::Instance().LoadAssetAsync(assetName, [this, materialDesc, materialDescFromPackage, slot](const Next::AssetLoadResult& result) {
        auto& manager = Next::AssetManager::Instance();
        if (!result.success || !result.handle.IsValid()) {
            NEXT_LOG_WARNING("Renderer %s texture asset load failed: %s",
                             Next::RendererTextureSlotName(slot),
                             result.errorMessage.c_str());
            return;
        }

        Next::TextureAssetView textureView;
        if (!manager.GetTextureAssetView(result.handle, textureView)) {
            NEXT_LOG_WARNING("Renderer %s asset was not a texture payload",
                             Next::RendererTextureSlotName(slot));
            manager.Release(result.handle);
            return;
        }

        Next::RendererTextureUploadDesc upload;
        upload.slot = slot;
        upload.format = textureView.header.format == 28 ? Next::RendererTextureFormat::RGBA8Unorm
                                                        : Next::RendererTextureFormat::Unknown;
        upload.sourceAssetId = result.handle.GetID();
        upload.width = textureView.header.width;
        upload.height = textureView.header.height;
        upload.pixels = textureView.pixels;
        upload.pixelBytes = textureView.pixelBytes;

        const Next::RendererTextureUploadHandle uploadHandle =
            renderer_ ? renderer_->UploadTexture2D(upload) : Next::RendererTextureUploadHandle{};
        if (uploadHandle) {
            if (slot == Next::RendererTextureSlot::Occlusion) {
                demoOcclusionTextureUploadId_ = uploadHandle.id;
                demoOcclusionRendererTextureId_ = uploadHandle.texture.id;
                demoOcclusionTextureUploadFinalLogged_ = false;
            } else if (slot == Next::RendererTextureSlot::Emissive) {
                demoEmissiveTextureUploadId_ = uploadHandle.id;
                demoEmissiveRendererTextureId_ = uploadHandle.texture.id;
                demoEmissiveTextureUploadFinalLogged_ = false;
            } else if (slot == Next::RendererTextureSlot::MetallicRoughness) {
                demoMetallicRoughnessTextureUploadId_ = uploadHandle.id;
                demoMetallicRoughnessRendererTextureId_ = uploadHandle.texture.id;
                demoMetallicRoughnessTextureUploadFinalLogged_ = false;
            } else if (slot == Next::RendererTextureSlot::Normal) {
                demoNormalTextureUploadId_ = uploadHandle.id;
                demoNormalRendererTextureId_ = uploadHandle.texture.id;
                demoNormalTextureUploadFinalLogged_ = false;
            } else if (slot == Next::RendererTextureSlot::BaseColor) {
                demoTextureUploadId_ = uploadHandle.id;
                demoRendererTextureId_ = uploadHandle.texture.id;
                demoTextureUploadFinalLogged_ = false;
            }
            NEXT_LOG_INFO("Renderer texture asset upload queued: slot=%s %ux%u (upload=%llu texture=%llu asset=%llu)",
                          Next::RendererTextureSlotName(slot),
                          upload.width,
                          upload.height,
                          static_cast<unsigned long long>(uploadHandle.id),
                          static_cast<unsigned long long>(uploadHandle.texture.id),
                          static_cast<unsigned long long>(upload.sourceAssetId));
            Next::RendererMaterialDesc rendererMaterial = materialDesc;
            Next::RendererMaterialHandle rendererMaterialHandle{demoRendererMaterialId_};
            Next::RendererMaterialInfo existingMaterialInfo;
            if (rendererMaterialHandle && renderer_->GetMaterialInfo(rendererMaterialHandle, existingMaterialInfo)) {
                rendererMaterial = existingMaterialInfo.desc;
            }
            rendererMaterial.SetTexture(slot, uploadHandle.texture);
            bool materialActive = false;
            if (rendererMaterialHandle) {
                materialActive = renderer_->UpdateMaterial(rendererMaterialHandle, rendererMaterial) &&
                    renderer_->SetActiveMaterial(rendererMaterialHandle);
            } else {
                rendererMaterialHandle = renderer_->CreateMaterial(rendererMaterial);
                materialActive = rendererMaterialHandle && renderer_->SetActiveMaterial(rendererMaterialHandle);
                if (materialActive) {
                    demoRendererMaterialId_ = rendererMaterialHandle.id;
                }
            }
            if (materialActive) {
                Next::RendererMaterialInfo materialInfo;
                const bool hasMaterialInfo = renderer_->GetMaterialInfo(rendererMaterialHandle, materialInfo);
                NEXT_LOG_INFO("Renderer material binding active: material=%llu slot=%s texture=%llu "
                              "materialAsset=%llu descHasSource=%s boundTextures=%llu descHasAny=%s "
                              "descComplete=%s descValid=%s slotBound=%s roughness=%.2f metallic=%.2f "
                              "baseColor=%.2f,%.2f,%.2f source=%s info=%s active=%s "
                              "infoReady=%s infoActivity=%s infoHasSource=%s infoBoundTextures=%llu "
                              "infoComplete=%s infoValid=%s infoSlotBound=%s",
                              static_cast<unsigned long long>(rendererMaterialHandle.id),
                              Next::RendererTextureSlotName(slot),
                              static_cast<unsigned long long>(rendererMaterial.GetTexture(slot).id),
                              static_cast<unsigned long long>(rendererMaterial.sourceAssetId),
                              rendererMaterial.HasSourceAsset() ? "true" : "false",
                              static_cast<unsigned long long>(rendererMaterial.BoundTextureCount()),
                              rendererMaterial.HasAnyTexture() ? "true" : "false",
                              rendererMaterial.HasCompleteTextureSet() ? "true" : "false",
                              rendererMaterial.HasValidParameters() ? "true" : "false",
                              rendererMaterial.HasTexture(slot) ? "true" : "false",
                              rendererMaterial.roughness,
                              rendererMaterial.metallic,
                              rendererMaterial.baseColorFactor[0],
                              rendererMaterial.baseColorFactor[1],
                              rendererMaterial.baseColorFactor[2],
                              materialDescFromPackage ? "package" : "default",
                              hasMaterialInfo ? "true" : "false",
                              materialInfo.active ? "true" : "false",
                              materialInfo.HasActiveMaterial() ? "true" : "false",
                              materialInfo.HasMaterialActivity() ? "true" : "false",
                              materialInfo.HasSourceAsset() ? "true" : "false",
                              static_cast<unsigned long long>(materialInfo.BoundTextureCount()),
                              materialInfo.HasCompleteTextureSet() ? "true" : "false",
                              materialInfo.HasValidParameters() ? "true" : "false",
                              materialInfo.HasTexture(slot) ? "true" : "false");
            } else {
                NEXT_LOG_WARNING("Renderer material binding for %s was not accepted by backend",
                                 Next::RendererTextureSlotName(slot));
            }
            const Next::RendererTextureUploadStats stats = renderer_->GetTextureUploadStats();
            const Next::RendererTextureUploadStatus status = renderer_->GetTextureUploadStatus(uploadHandle);
            Next::RendererTextureInfo textureInfo;
            const bool hasTextureInfo = renderer_->GetTextureInfo(uploadHandle.texture, textureInfo);
            const Next::RendererActiveTextureSlotInfo activeSlotInfo = stats.GetActiveTexture(slot);
            NEXT_LOG_INFO("Renderer texture upload stats: slot=%s queued=%llu completed=%llu failed=%llu "
                          "basePending=%s materialPending=%s finished=%llu hasQueued=%s hasCompleted=%s "
                          "hasFailed=%s hasFinished=%s hasPending=%s hasBasePending=%s hasMaterialPending=%s "
                          "hasActive=%s hasBaseActive=%s hasBaseSource=%s hasUploadActivity=%s "
                          "activeSlots=%u lastQueued=%llu lastCompleted=%llu lastFailed=%llu lastTexture=%llu "
                          "baseTexture=%llu baseAsset=%llu baseSize=%ux%u "
                          "slotActiveTexture=%llu slotAsset=%llu slotActive=%s slotReady=%s "
                          "slotHasSource=%s slotSize=%ux%u status=%s info=%s infoHasTexture=%s "
                          "infoFormat=%s infoHasFormat=%s infoPending=%s infoReady=%s "
                          "infoActive=%s infoHasSource=%s infoActivity=%s",
                          Next::RendererTextureSlotName(slot),
                          static_cast<unsigned long long>(stats.queuedUploads),
                          static_cast<unsigned long long>(stats.completedUploads),
                          static_cast<unsigned long long>(stats.failedUploads),
                          stats.baseColorUploadPending ? "true" : "false",
                          stats.materialTextureUploadPending ? "true" : "false",
                          static_cast<unsigned long long>(stats.FinishedUploadCount()),
                          stats.HasQueuedUploads() ? "true" : "false",
                          stats.HasCompletedUploads() ? "true" : "false",
                          stats.HasFailedUploads() ? "true" : "false",
                          stats.HasFinishedUploads() ? "true" : "false",
                          stats.HasPendingUploads() ? "true" : "false",
                          stats.HasPendingBaseColorUpload() ? "true" : "false",
                          stats.HasPendingMaterialTextureUploads() ? "true" : "false",
                          stats.HasActiveMaterialTextures() ? "true" : "false",
                          stats.HasActiveBaseColorTexture() ? "true" : "false",
                          stats.HasActiveBaseColorSourceAsset() ? "true" : "false",
                          stats.HasTextureUploadActivity() ? "true" : "false",
                          stats.activeMaterialTextureCount,
                          static_cast<unsigned long long>(stats.lastQueuedUpload),
                          static_cast<unsigned long long>(stats.lastCompletedUpload),
                          static_cast<unsigned long long>(stats.lastFailedUpload),
                          static_cast<unsigned long long>(stats.lastQueuedTexture.id),
                          static_cast<unsigned long long>(stats.activeBaseColorTexture.id),
                          static_cast<unsigned long long>(stats.activeBaseColorSourceAssetId),
                          stats.activeBaseColorWidth,
                          stats.activeBaseColorHeight,
                          static_cast<unsigned long long>(activeSlotInfo.texture.id),
                          static_cast<unsigned long long>(activeSlotInfo.sourceAssetId),
                          activeSlotInfo.active ? "true" : "false",
                          activeSlotInfo.HasActiveTexture() ? "true" : "false",
                          activeSlotInfo.HasSourceAsset() ? "true" : "false",
                          activeSlotInfo.width,
                          activeSlotInfo.height,
                          Next::RendererTextureUploadStatusName(status),
                          hasTextureInfo ? "true" : "false",
                          textureInfo.HasTexture() ? "true" : "false",
                          Next::RendererTextureFormatName(textureInfo.format),
                          textureInfo.HasFormat() ? "true" : "false",
                          textureInfo.HasPendingUpload() ? "true" : "false",
                          textureInfo.HasReadyTexture() ? "true" : "false",
                          textureInfo.HasActiveTexture() ? "true" : "false",
                          textureInfo.HasSourceAsset() ? "true" : "false",
                          textureInfo.HasTextureActivity() ? "true" : "false");
        } else {
            const Next::RendererResourcePoolStats resourceStats =
                renderer_ ? renderer_->GetResourcePoolStats() : Next::RendererResourcePoolStats{};
            NEXT_LOG_WARNING("Renderer %s texture asset upload was not accepted by backend "
                             "(poolFailedAllocs=%llu poolFailedBytes=%llu)",
                             Next::RendererTextureSlotName(slot),
                             static_cast<unsigned long long>(resourceStats.TotalFailedAllocationCount()),
                             static_cast<unsigned long long>(resourceStats.TotalFailedAllocationBytes()));
        }

        manager.Release(result.handle);
    });
    };

    queueMaterialTextureUpload(selectedTextureName, Next::RendererTextureSlot::BaseColor);
    if (!selectedNormalTextureName.empty()) {
        queueMaterialTextureUpload(selectedNormalTextureName, Next::RendererTextureSlot::Normal);
    }
    if (!selectedMetallicRoughnessTextureName.empty()) {
        queueMaterialTextureUpload(selectedMetallicRoughnessTextureName,
                                   Next::RendererTextureSlot::MetallicRoughness);
    }
    if (!selectedEmissiveTextureName.empty()) {
        queueMaterialTextureUpload(selectedEmissiveTextureName, Next::RendererTextureSlot::Emissive);
    }
    if (!selectedOcclusionTextureName.empty()) {
        queueMaterialTextureUpload(selectedOcclusionTextureName, Next::RendererTextureSlot::Occlusion);
    }
}

void Game::PollRendererTextureUpload() {
    if (!renderer_) {
        return;
    }

    auto pollUpload = [this](Next::RendererTextureSlot slot,
                             uint64_t& uploadId,
                             uint64_t& textureId,
                             bool& finalLogged) {
        if (uploadId == 0 || finalLogged) {
            return;
        }

        const Next::RendererTextureUploadHandle handle{uploadId, Next::RendererTextureHandle{textureId}};
        const Next::RendererTextureUploadStatus status = renderer_->GetTextureUploadStatus(handle);
        if (status != Next::RendererTextureUploadStatus::Completed &&
            status != Next::RendererTextureUploadStatus::Failed) {
            return;
        }

        const Next::RendererTextureUploadStats stats = renderer_->GetTextureUploadStats();
        const Next::RendererResourcePoolStats resourceStats = renderer_->GetResourcePoolStats();
        const Next::RendererActiveTextureSlotInfo activeSlotInfo = stats.GetActiveTexture(slot);
        Next::RendererTextureInfo textureInfo;
        const bool hasTextureInfo = renderer_->GetTextureInfo(handle.texture, textureInfo);
        Next::RendererMaterialInfo materialInfo;
        const bool hasMaterialInfo = renderer_->GetMaterialInfo(Next::RendererMaterialHandle{demoRendererMaterialId_},
                                                                materialInfo);
        NEXT_LOG_INFO("Renderer texture upload final: slot=%s upload=%llu texture=%llu status=%s queued=%llu "
                      "completed=%llu failed=%llu finished=%llu hasCompleted=%s hasFailed=%s hasPending=%s "
                      "hasFinished=%s hasLastCompleted=%s hasActive=%s hasBaseActive=%s "
                      "hasUploadActivity=%s slotActiveTexture=%llu slotAsset=%llu slotActive=%s slotReady=%s "
                      "slotHasSource=%s slotSize=%ux%u "
                      "activeSlots=%u poolLiveBytes=%llu poolFailedAllocs=%llu info=%s infoActive=%s "
                      "infoReady=%s infoPending=%s infoHasFormat=%s infoHasSource=%s infoActivity=%s "
                      "infoSlot=%s infoFormat=%s infoAsset=%llu infoSize=%ux%u "
                      "material=%llu materialInfo=%s "
                      "materialActive=%s materialReady=%s materialHasSource=%s materialHasBound=%s "
                      "materialComplete=%s materialValid=%s materialActivity=%s "
                      "materialBoundTextures=%llu materialSlotBound=%s materialRoughness=%.2f "
                      "materialMetallic=%.2f",
                      Next::RendererTextureSlotName(slot),
                      static_cast<unsigned long long>(handle.id),
                      static_cast<unsigned long long>(handle.texture.id),
                      Next::RendererTextureUploadStatusName(status),
                      static_cast<unsigned long long>(stats.queuedUploads),
                      static_cast<unsigned long long>(stats.completedUploads),
                      static_cast<unsigned long long>(stats.failedUploads),
                      static_cast<unsigned long long>(stats.FinishedUploadCount()),
                      stats.HasCompletedUploads() ? "true" : "false",
                      stats.HasFailedUploads() ? "true" : "false",
                      stats.HasPendingUploads() ? "true" : "false",
                      stats.HasFinishedUploads() ? "true" : "false",
                      stats.HasLastCompletedUpload() ? "true" : "false",
                      stats.HasActiveMaterialTextures() ? "true" : "false",
                      stats.HasActiveBaseColorTexture() ? "true" : "false",
                      stats.HasTextureUploadActivity() ? "true" : "false",
                      static_cast<unsigned long long>(activeSlotInfo.texture.id),
                      static_cast<unsigned long long>(activeSlotInfo.sourceAssetId),
                      activeSlotInfo.active ? "true" : "false",
                      activeSlotInfo.HasActiveTexture() ? "true" : "false",
                      activeSlotInfo.HasSourceAsset() ? "true" : "false",
                      activeSlotInfo.width,
                      activeSlotInfo.height,
                      stats.activeMaterialTextureCount,
                      static_cast<unsigned long long>(resourceStats.TotalLiveBytes()),
                      static_cast<unsigned long long>(resourceStats.TotalFailedAllocationCount()),
                      hasTextureInfo ? "true" : "false",
                      textureInfo.active ? "true" : "false",
                      textureInfo.HasReadyTexture() ? "true" : "false",
                      textureInfo.HasPendingUpload() ? "true" : "false",
                      textureInfo.HasFormat() ? "true" : "false",
                      textureInfo.HasSourceAsset() ? "true" : "false",
                      textureInfo.HasTextureActivity() ? "true" : "false",
                      Next::RendererTextureSlotName(textureInfo.slot),
                      Next::RendererTextureFormatName(textureInfo.format),
                      static_cast<unsigned long long>(textureInfo.sourceAssetId),
                      textureInfo.width,
                      textureInfo.height,
                      static_cast<unsigned long long>(demoRendererMaterialId_),
                      hasMaterialInfo ? "true" : "false",
                      materialInfo.active ? "true" : "false",
                      materialInfo.HasActiveMaterial() ? "true" : "false",
                      materialInfo.HasSourceAsset() ? "true" : "false",
                      materialInfo.HasBoundTextures() ? "true" : "false",
                      materialInfo.HasCompleteTextureSet() ? "true" : "false",
                      materialInfo.HasValidParameters() ? "true" : "false",
                      materialInfo.HasMaterialActivity() ? "true" : "false",
                      static_cast<unsigned long long>(materialInfo.BoundTextureCount()),
                      materialInfo.HasTexture(slot) ? "true" : "false",
                      materialInfo.desc.roughness,
                      materialInfo.desc.metallic);
        finalLogged = true;
        uploadId = 0;
        textureId = 0;
    };

    pollUpload(Next::RendererTextureSlot::BaseColor,
               demoTextureUploadId_,
               demoRendererTextureId_,
               demoTextureUploadFinalLogged_);
    pollUpload(Next::RendererTextureSlot::Normal,
               demoNormalTextureUploadId_,
               demoNormalRendererTextureId_,
               demoNormalTextureUploadFinalLogged_);
    pollUpload(Next::RendererTextureSlot::MetallicRoughness,
               demoMetallicRoughnessTextureUploadId_,
               demoMetallicRoughnessRendererTextureId_,
               demoMetallicRoughnessTextureUploadFinalLogged_);
    pollUpload(Next::RendererTextureSlot::Emissive,
               demoEmissiveTextureUploadId_,
               demoEmissiveRendererTextureId_,
               demoEmissiveTextureUploadFinalLogged_);
    pollUpload(Next::RendererTextureSlot::Occlusion,
               demoOcclusionTextureUploadId_,
               demoOcclusionRendererTextureId_,
               demoOcclusionTextureUploadFinalLogged_);
}

void Game::RunJobSystemSelfTest() {
    auto& jobSystem = Next::JobSystem::Instance();

    NEXT_LOG_INFO("Running JobSystem self-test (CP2 sanity)");
    const int taskCount = 32;
    std::atomic<int> counter{0};

    std::vector<Next::JobHandle> handles;
    handles.reserve(taskCount);

    for (int i = 0; i < taskCount; ++i) {
        auto handle = jobSystem.Submit([&counter]() {
            counter.fetch_add(1, std::memory_order_relaxed);
        }, (i % 3 == 0) ? Next::JobPriority::High : Next::JobPriority::Normal, {}, "SelfTest");
        handles.push_back(handle);
    }

    // Wait for completion
    for (auto& h : handles) {
        jobSystem.Wait(h);
    }

    NEXT_LOG_INFO("JobSystem self-test completed: %d/%d tasks finished",
                  counter.load(std::memory_order_relaxed), taskCount);
}

void Game::RunAssetSystemTest() {
    auto& assetManager = Next::AssetManager::Instance();
    auto& jobSystem = Next::JobSystem::Instance();

    NEXT_LOG_INFO("Running Asset System test (CP3)");
    NEXT_CPU_SCOPE("AssetSystemTest");

    namespace fs = std::filesystem;
    fs::path packagePath = ResolveRuntimePath(fs::path("assets") / "test_package.npkg");
    std::string packageName = packagePath.stem().string();

    if (!fs::exists(packagePath)) {
        NEXT_LOG_WARNING("Package %s not found. Please run: build\\bin\\Debug\\next_assetc.exe test assets", packagePath.string().c_str());
        NEXT_LOG_WARNING("Or import a mesh package: build\\bin\\Debug\\next_assetc.exe import SourceAssets\\your.obj assets\\your.npkg");
        return;
    }

    if (!assetManager.LoadPackage(packagePath.string())) {
        NEXT_LOG_ERROR("Failed to load package for asset system test");
        return;
    }

    auto loadAndLog = [&](const char* asset) {
        NEXT_CPU_SCOPE("LoadAssetSync");
        auto handle = assetManager.LoadAssetSync(asset);
        if (!handle.IsValid()) {
            NEXT_LOG_ERROR("Failed to load asset: %s", asset);
            return false;
        }
        NEXT_LOG_INFO("Loaded asset: %s (id=%llu)", asset, handle.GetID());
        assetManager.Release(handle);
        return true;
    };

    NEXT_LOG_INFO("Loading assets from %s", packagePath.string().c_str());
    loadAndLog("TestCube");
    loadAndLog("TestChecker");
    loadAndLog("TestPBR");

    // Optional: if an imported package exists, load it too.
    {
        fs::path imported = ResolveRuntimePath(fs::path("assets") / "tri_import.npkg");
        if (fs::exists(imported)) {
            NEXT_LOG_INFO("Loading imported package: %s", imported.string().c_str());
            if (assetManager.LoadPackage(imported.string())) {
                loadAndLog("tri_import");
            }
        }
    }

    // Test 1: Synchronous loading simulation
    {
        NEXT_CPU_SCOPE("Test1_SyncLoad");
        NEXT_LOG_INFO("Test 1: Synchronous loading simulation");

        // Simulate loading two assets
        auto startTime = Next::GetTimeInSeconds();

        // In real implementation, we would load actual assets
        // For CP3 demo, we'll simulate with a small job
        auto loadJob = jobSystem.Submit([]() {
            NEXT_LOG_DEBUG("Simulating asset loading...");
            Next::SleepMs(10); // Simulate IO delay
        }, Next::JobPriority::High, {}, "AssetLoadSim");

        jobSystem.Wait(loadJob);

        auto endTime = Next::GetTimeInSeconds();
        NEXT_LOG_INFO("  Simulated sync load completed in %.2f ms", (endTime - startTime) * 1000.0);
    }

    // Test 2: Reference counting simulation
    {
        NEXT_CPU_SCOPE("Test2_RefCounting");
        NEXT_LOG_INFO("Test 2: Reference counting simulation");

        // Simulate loading an asset multiple times
        NEXT_LOG_INFO("  Simulating asset 'TestCube' loaded 3 times...");

        // In real implementation, we would track actual ref counts
        NEXT_LOG_INFO("  Reference count simulation: loaded=3, refcount=3");

        // Simulate releasing references
        NEXT_LOG_INFO("  Releasing 2 references...");
        NEXT_LOG_INFO("  Reference count simulation: loaded=3, refcount=1");

        // Simulate unloading when refcount reaches 0
        NEXT_LOG_INFO("  Releasing final reference...");
        NEXT_LOG_INFO("  Asset marked for cleanup (refcount=0)");
    }

    // Test 3: Async loading with JobSystem integration
    {
        NEXT_CPU_SCOPE("Test3_AsyncLoad");
        NEXT_LOG_INFO("Test 3: Async loading with JobSystem integration");

        std::atomic<int> asyncLoadCount{0};
        const int asyncLoads = 4;

        NEXT_LOG_INFO("  Submitting %d async asset load simulations...", asyncLoads);

        std::vector<Next::JobHandle> asyncHandles;
        for (int i = 0; i < asyncLoads; ++i) {
            auto handle = jobSystem.Submit([&asyncLoadCount, i]() {
                NEXT_LOG_DEBUG("Async asset load simulation %d", i);
                Next::SleepMs(5 + (i * 2)); // Varying delays
                asyncLoadCount.fetch_add(1, std::memory_order_relaxed);
            }, Next::JobPriority::Normal, {}, "AsyncAssetLoad");

            asyncHandles.push_back(handle);
        }

        // Wait for all async loads
        for (auto& handle : asyncHandles) {
            jobSystem.Wait(handle);
        }

        NEXT_LOG_INFO("  Async loads completed: %d/%d",
                     asyncLoadCount.load(std::memory_order_relaxed), asyncLoads);
    }

    // Test 4: Asset statistics
    {
        NEXT_CPU_SCOPE("Test4_Stats");
        NEXT_LOG_INFO("Test 4: Asset statistics reporting");

        // Get simulated stats
        auto stats = assetManager.GetStats();

        NEXT_LOG_INFO("  Asset Manager Statistics:");
        NEXT_LOG_INFO("    Loaded assets: %zu", stats.loadedAssets);
        NEXT_LOG_INFO("    Total memory: %zu bytes", stats.totalMemory);
        NEXT_LOG_INFO("    Pending loads: %zu", stats.pendingLoads);
        NEXT_LOG_INFO("    Failed loads: %zu", stats.failedLoads);
    }

    assetManager.UnloadPackage(packageName);

    NEXT_LOG_INFO("Asset System test completed (CP3 demo)");
}

void Game::RunECSSelfTest() {
    NEXT_LOG_INFO("Running ECS self-test (CP4)");
    NEXT_CPU_SCOPE("ECSSelfTest");

    using namespace Next;

    // Create world
    World world;
    NEXT_LOG_INFO("World created");

    // Test 1: Basic entity creation
    {
        NEXT_CPU_SCOPE("Test1_EntityCreation");
        NEXT_LOG_INFO("Test 1: Basic entity creation");

        auto startTime = Next::GetTimeInSeconds();

        std::vector<Entity> entities;
        for (int i = 0; i < 100; ++i) {
            Entity e = world.CreateEntity();
            entities.push_back(e);
        }

        auto endTime = Next::GetTimeInSeconds();
        NEXT_LOG_INFO("  Created 100 entities in %.2f ms", (endTime - startTime) * 1000.0);
        NEXT_LOG_INFO("  World entity count: %zu", world.GetEntityCount());
    }

    // Test 2: Component operations
    {
        NEXT_CPU_SCOPE("Test2_ComponentOperations");
        NEXT_LOG_INFO("Test 2: Component operations");

        auto startTime = Next::GetTimeInSeconds();

        // Create entity with components
        Entity e1 = world.CreateEntity();
        world.AddComponent<NameComponent>(e1, "TestEntity1");
        world.AddComponent<TransformComponent>(e1);

        Entity e2 = world.CreateEntity();
        world.AddComponent<NameComponent>(e2, "TestEntity2");
        world.AddComponent<TransformComponent>(e2);
        world.AddComponent<HierarchyComponent>(e2);

        // Check components
        bool hasTransform = world.HasComponent<TransformComponent>(e1);
        bool hasHierarchy = world.HasComponent<HierarchyComponent>(e1);
        bool e2HasHierarchy = world.HasComponent<HierarchyComponent>(e2);

        auto endTime = Next::GetTimeInSeconds();
        NEXT_LOG_INFO("  Component operations in %.2f ms", (endTime - startTime) * 1000.0);

        const char* hasTransformStr = hasTransform ? "yes" : "no";
        const char* hasHierarchyStr = hasHierarchy ? "yes" : "no";
        NEXT_LOG_INFO("  e1 has Transform: %s, has Hierarchy: %s", hasTransformStr, hasHierarchyStr);

        const char* e2HasHierarchyStr = e2HasHierarchy ? "yes" : "no";
        NEXT_LOG_INFO("  e2 has Hierarchy: %s", e2HasHierarchyStr);
    }

    // Test 3: Query entities
    {
        NEXT_CPU_SCOPE("Test3_QueryEntities");
        NEXT_LOG_INFO("Test 3: Query entities with components");

        auto startTime = Next::GetTimeInSeconds();

        // Create entities with various components
        for (int i = 0; i < 50; ++i) {
            Entity e = world.CreateEntity();
            world.AddComponent<TransformComponent>(e);

            if (i % 2 == 0) {
                world.AddComponent<NameComponent>(e, "EvenEntity");
            }
            if (i % 3 == 0) {
                world.AddComponent<HierarchyComponent>(e);
            }
        }

        // Query entities with TransformComponent
        auto withTransform = world.QueryEntitiesWith<TransformComponent>();
        NEXT_LOG_INFO("  Entities with Transform: %zu", withTransform.size());

        // Query entities with both Transform and Name
        auto withTransformAndName = world.QueryEntitiesWith<TransformComponent, NameComponent>();
        NEXT_LOG_INFO("  Entities with Transform + Name: %zu", withTransformAndName.size());

        auto endTime = Next::GetTimeInSeconds();
        NEXT_LOG_INFO("  Query completed in %.2f ms", (endTime - startTime) * 1000.0);
    }

    // Test 4: Entity destruction
    {
        NEXT_CPU_SCOPE("Test4_EntityDestruction");
        NEXT_LOG_INFO("Test 4: Entity destruction");

        size_t beforeCount = world.GetEntityCount();

        // Destroy some entities
        auto entities = world.GetAllEntities();
        if (entities.size() > 10) {
            for (size_t i = 0; i < 10; ++i) {
                world.DestroyEntity(entities[i]);
            }
        }

        size_t afterCount = world.GetEntityCount();
        NEXT_LOG_INFO("  Before: %zu entities, After: %zu entities", beforeCount, afterCount);
    }

    // Test 5: Stress test with 10,000 entities
    {
        NEXT_CPU_SCOPE("Test5_StressTest");
        NEXT_LOG_INFO("Test 5: Stress test (10,000 entities)");

        auto startTime = Next::GetTimeInSeconds();

        World stressWorld;
        std::vector<Entity> stressEntities;

        // Create entities
        for (int i = 0; i < 10000; ++i) {
            Entity e = stressWorld.CreateEntity();
            stressEntities.push_back(e);

            // Add components
            stressWorld.AddComponent<TransformComponent>(e);
            auto* transform = stressWorld.GetComponent<TransformComponent>(e);
            if (transform) {
                transform->position[0] = (float)i;
            }

            if (i % 2 == 0) {
                stressWorld.AddComponent<NameComponent>(e);
            }
        }

        auto createEndTime = Next::GetTimeInSeconds();
        NEXT_LOG_INFO("  Created 10,000 entities in %.2f ms", (createEndTime - startTime) * 1000.0);

        // Query test
        auto queryStartTime = Next::GetTimeInSeconds();
        auto results = stressWorld.QueryEntitiesWith<TransformComponent>();
        auto queryEndTime = Next::GetTimeInSeconds();

        NEXT_LOG_INFO("  Query returned %zu entities in %.2f ms",
                     results.size(), (queryEndTime - queryStartTime) * 1000.0);

        // Update test (simulate 60 frames)
        auto updateStartTime = Next::GetTimeInSeconds();
        for (int frame = 0; frame < 60; ++frame) {
            for (auto& e : results) {
                auto* transform = stressWorld.GetComponent<TransformComponent>(e);
                if (transform) {
                    transform->position[1] += 0.01f; // Simple animation
                }
            }
        }
        auto updateEndTime = Next::GetTimeInSeconds();

        NEXT_LOG_INFO("  60 frames update in %.2f ms (avg: %.2f ms/frame)",
                     (updateEndTime - updateStartTime) * 1000.0,
                     (updateEndTime - updateStartTime) * 1000.0 / 60.0);

        // World stats
        auto stats = stressWorld.GetStats();
        NEXT_LOG_INFO("  World Stats:");
        NEXT_LOG_INFO("    Entities: %zu", stats.entityCount);
        NEXT_LOG_INFO("    Components: %zu", stats.totalComponents);
        NEXT_LOG_INFO("    Systems: %zu", stats.systemCount);

        auto totalTime = Next::GetTimeInSeconds();
        NEXT_LOG_INFO("  Total stress test time: %.2f ms", (totalTime - startTime) * 1000.0);
    }

    NEXT_LOG_INFO("ECS self-test completed (CP4)");
}

} // namespace Song
