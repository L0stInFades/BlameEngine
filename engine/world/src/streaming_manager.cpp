#include "next/streaming/streaming_manager.h"
#include "next/streaming/cell_file_format.h"
#include "next/streaming/layered_cell_file.h"
#include "next/foundation/logger.h"
#include "next/runtime/asset/asset_manager.h"
#include "next/jobsystem/job_system.h"
#include <algorithm>
#include <cstddef>
#include <cmath>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <unordered_set>

#ifdef _WIN32
#include <windows.h>
#endif

namespace Next {
namespace Streaming {

namespace {

constexpr size_t kStreamingBytesPerMB = 1024u * 1024u;

std::string PathLogString(const std::filesystem::path& path) {
    return path.u8string();
}

std::wstring CellIndexPathString(const std::filesystem::path& path) {
    std::error_code ec;
    const std::filesystem::path absolutePath = std::filesystem::absolute(path, ec);
    return (ec ? path : absolutePath).wstring();
}

struct CellPayloadInfo {
    bool valid = false;
    bool hasHeader = false;
    CompressionType compression = CompressionType::None;
    uint64_t payloadOffset = 0;
    uint64_t payloadSize = 0;
    uint64_t decompressedSize = 0;
    std::string error;
};

CellPayloadInfo InspectCellPayload(const std::vector<uint8_t>& fileData) {
    CellPayloadInfo info;

    if (fileData.size() < sizeof(uint32_t)) {
        info.valid = !fileData.empty();
        info.payloadSize = fileData.size();
        info.decompressedSize = fileData.size();
        return info;
    }

    uint32_t magic = 0;
    std::memcpy(&magic, fileData.data(), sizeof(magic));
    if (magic != kCellFileMagic) {
        info.valid = true;
        info.payloadSize = fileData.size();
        info.decompressedSize = fileData.size();
        return info;
    }

    info.hasHeader = true;
    if (fileData.size() < sizeof(CellFileHeader)) {
        info.error = "cell header is truncated";
        return info;
    }

    CellFileHeader header;
    std::memcpy(&header, fileData.data(), sizeof(header));

    if (header.version != kCellFileVersion) {
        info.error = "unsupported cell header version";
        return info;
    }
    if (header.headerSize < sizeof(CellFileHeader) || header.headerSize > fileData.size()) {
        info.error = "invalid cell header size";
        return info;
    }
    if (header.compressedSize == 0 || header.decompressedSize == 0) {
        info.error = "cell payload size is empty";
        return info;
    }
    if (header.compressedSize > fileData.size() ||
        static_cast<uint64_t>(header.headerSize) > static_cast<uint64_t>(fileData.size()) - header.compressedSize) {
        info.error = "cell payload size exceeds file size";
        return info;
    }
    if (static_cast<uint64_t>(header.headerSize) + header.compressedSize != fileData.size()) {
        info.error = "cell payload size does not match file size";
        return info;
    }
    if (header.decompressedSize > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
        info.error = "cell decompressed payload is too large";
        return info;
    }

    if (!IsSupportedCellFileCompression(header.compressionType)) {
        info.error = "unsupported cell payload compression";
        return info;
    }
    const CompressionType compression = static_cast<CompressionType>(header.compressionType);
    if (compression == CompressionType::None && header.compressedSize != header.decompressedSize) {
        info.error = "uncompressed cell payload size mismatch";
        return info;
    }

    info.valid = true;
    info.compression = compression;
    info.payloadOffset = header.headerSize;
    info.payloadSize = header.compressedSize;
    info.decompressedSize = header.decompressedSize;
    return info;
}

std::filesystem::path GetExecutableDirectory() {
#ifdef _WIN32
    wchar_t path[MAX_PATH] = {};
    DWORD len = GetModuleFileNameW(nullptr, path, MAX_PATH);
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

std::filesystem::path NormalizeRuntimePathSeparators(const std::filesystem::path& path) {
#ifdef _WIN32
    return path;
#else
    std::wstring text = path.wstring();
    std::replace(text.begin(), text.end(), L'\\', L'/');
    return std::filesystem::path(text);
#endif
}

std::filesystem::path ResolveRuntimePath(const std::filesystem::path& inputPath) {
    const std::filesystem::path path = NormalizeRuntimePathSeparators(inputPath);
    if (path.empty()) {
        return {};
    }

    std::error_code ec;
    if (path.is_absolute()) {
        const std::filesystem::path absolutePath = std::filesystem::absolute(path, ec);
        return ec ? path : absolutePath;
    }

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

bool ComputeStreamingMemoryBudgetBytes(const StreamingManagerConfig& config, size_t& outBytes) {
    outBytes = 0;
    if (config.memoryBudgetMB == 0 ||
        config.memoryBudgetMB > std::numeric_limits<size_t>::max() / kStreamingBytesPerMB) {
        return false;
    }

    outBytes = config.memoryBudgetMB * kStreamingBytesPerMB;
    return true;
}

bool ValidateStreamingManagerConfig(const StreamingManagerConfig& config, size_t& memoryBudgetBytes) {
    if (!ComputeStreamingMemoryBudgetBytes(config, memoryBudgetBytes)) {
        NEXT_LOG_ERROR("Invalid streaming memory budget: %zu MB", config.memoryBudgetMB);
        return false;
    }
    if (config.maxConcurrentLoads == 0 || config.maxConcurrentUnloads == 0) {
        NEXT_LOG_ERROR("Invalid streaming concurrency limits: loads=%u unloads=%u", config.maxConcurrentLoads,
                       config.maxConcurrentUnloads);
        return false;
    }

    return true;
}

StreamingManagerConfig ResolveStreamingManagerConfigPaths(const StreamingManagerConfig& config) {
    StreamingManagerConfig resolved = config;
    resolved.cellDataDirectory = ResolveRuntimePath(std::filesystem::path(config.cellDataDirectory)).wstring();
    return resolved;
}

WorldPartitionConfig MakeWorldPartitionConfig(const StreamingManagerConfig& config) {
    WorldPartitionConfig partitionConfig;
    partitionConfig.cellSize = 64.0f;
    partitionConfig.loadRadius = config.loadRadius;
    partitionConfig.unloadRadius = config.unloadRadius;
    partitionConfig.maxLoadedCells = 256;
    partitionConfig.enableHLOD = config.enableHLOD;
    return partitionConfig;
}

std::filesystem::path BuildCellFileName(const CellCoord& coord, const std::wstring& extension) {
    return std::filesystem::path(L"cell_" + std::to_wstring(coord.x) + L"_" + std::to_wstring(coord.z) + extension);
}

uint32_t CellFileExtensionRank(const std::filesystem::path& path, const std::wstring& preferredExtension) {
    const std::wstring preferred = preferredExtension.empty() ? L".ncell" : preferredExtension;
    const std::wstring ext = path.extension().wstring();
    if (ext == preferred) {
        return 0;
    }
    if (ext == L".ncell") {
        return 1;
    }
    if (ext == L".npkg") {
        return 2;
    }
    return std::numeric_limits<uint32_t>::max();
}

std::filesystem::path ResolveCellFilePath(const std::filesystem::path& root, const CellCoord& coord,
                                          const std::wstring& preferredExtension) {
    std::vector<std::wstring> extensions;
    auto pushUniqueExt = [&](const std::wstring& ext) {
        if (ext.empty()) {
            return;
        }
        for (const auto& existing : extensions) {
            if (existing == ext) {
                return;
            }
        }
        extensions.push_back(ext);
    };

    pushUniqueExt(preferredExtension.empty() ? L".ncell" : preferredExtension);
    pushUniqueExt(L".ncell");
    pushUniqueExt(L".npkg");

    std::error_code ec;
    std::filesystem::path fallback;
    for (const auto& extension : extensions) {
        const std::filesystem::path candidate = root / BuildCellFileName(coord, extension);
        if (fallback.empty()) {
            fallback = candidate;
        }
        if (std::filesystem::exists(candidate, ec)) {
            return candidate;
        }
    }

    return fallback;
}

bool IsPackageCellPath(const std::filesystem::path& path) {
    return path.extension() == ".npkg";
}

StreamingCellInfo MakeStreamingCellInfo(const CellData& cell) {
    StreamingCellInfo info;
    info.coord = cell.coord;
    info.worldPosition = cell.metadata.worldPosition;
    info.cellSize = cell.metadata.cellSize;
    info.state = cell.state;
    info.layerMask = cell.metadata.layerMask;
    info.layerCount = cell.LayerCount();
    info.loadedLayerCount = cell.LoadedLayerCount();
    info.dataSize = cell.DiskDataSize();
    info.memorySize = cell.MemorySize();
    info.priority = cell.priority;
    info.lastAccessFrame = cell.lastAccessFrame;
    info.asyncOperationHandle = cell.asyncOperationHandle;
    info.placeholder = cell.IsPlaceholder();
    return info;
}

}  // namespace

// ===== Streaming Manager Implementation =====

StreamingManager::StreamingManager() : currentFrame_(0), elapsedTime_(0.0f), initialized_(false) {}

StreamingManager::~StreamingManager() {
    Shutdown();
}

bool StreamingManager::Initialize(const StreamingManagerConfig& config) {
    if (initialized_) {
        NEXT_LOG_WARNING("StreamingManager already initialized");
        return true;
    }

    size_t memoryBudgetBytes = 0;
    if (!ValidateStreamingManagerConfig(config, memoryBudgetBytes)) {
        return false;
    }

    config_ = ResolveStreamingManagerConfigPaths(config);

    // Initialize sub-systems
    worldPartition_ = std::make_unique<WorldPartition>();
    const WorldPartitionConfig partitionConfig = MakeWorldPartitionConfig(config_);
    if (!worldPartition_->Initialize(partitionConfig)) {
        NEXT_LOG_ERROR("Failed to initialize WorldPartition");
        return false;
    }

    asyncIO_ = std::make_unique<AsyncIOSystem>();
    if (!asyncIO_->Initialize(AsyncIOConfig())) {
        NEXT_LOG_ERROR("Failed to initialize AsyncIOSystem");
        return false;
    }

    interestManager_ = std::make_unique<InterestManager>();
    if (!interestManager_->Initialize(InterestManagerConfig())) {
        NEXT_LOG_ERROR("Failed to initialize InterestManager");
        return false;
    }
    interestManager_->SetWorldPartition(worldPartition_.get());

    lodSystem_ = std::make_unique<LODSystem>();
    if (!lodSystem_->Initialize(LODSystemConfig())) {
        NEXT_LOG_ERROR("Failed to initialize LODSystem");
        return false;
    }
    // Keep cell sizing consistent across LOD/cluster math and the partition.
    lodSystem_->SetCellSize(partitionConfig.cellSize);

    evictionPolicy_ = std::make_unique<EvictionPolicy>();
    if (!evictionPolicy_->Initialize(EvictionPolicyConfig())) {
        NEXT_LOG_ERROR("Failed to initialize EvictionPolicy");
        return false;
    }
    evictionPolicy_->SetMemoryBudget(memoryBudgetBytes);
    evictionPolicy_->SetMaxCellCount(static_cast<uint32_t>(partitionConfig.maxLoadedCells));

    memoryPool_ = std::make_unique<StreamingMemoryPool>();
    if (!memoryPool_->Initialize(memoryBudgetBytes)) {
        NEXT_LOG_ERROR("Failed to initialize StreamingMemoryPool");
        return false;
    }

    // Reserve space for queues
    loadQueue_.reserve(config.maxConcurrentLoads);
    unloadQueue_.reserve(config.maxConcurrentUnloads);

    // Index existing authored cells from disk (prevents trying to stream infinite empty grid).
    ScanAvailableCells();

    // Reset statistics
    stats_ = StreamingStatistics();
    currentFrame_ = 0;
    elapsedTime_ = 0.0f;

    NEXT_LOG_INFO("StreamingManager initialized (CP7: World Streaming)");
    NEXT_LOG_INFO("  Memory budget: %zu MB", config.memoryBudgetMB);
    NEXT_LOG_INFO("  Load radius: %.1f meters", config.loadRadius);
    NEXT_LOG_INFO("  Max LOD level: %u", config.maxLODLevel);
    NEXT_LOG_INFO("  Available cells: %zu", availableCells_.size());

    initialized_ = true;
    return true;
}

void StreamingManager::ScanAvailableCells() {
    availableCells_.clear();
    availableCellPaths_.clear();

    const std::filesystem::path root(config_.cellDataDirectory);
    std::error_code ec;
    if (root.empty() || !std::filesystem::exists(root, ec) || !std::filesystem::is_directory(root, ec)) {
        return;
    }

    const std::wstring want = config_.cellFileExtension.empty() ? L".ncell" : config_.cellFileExtension;

    auto tryParseCell = [](const std::filesystem::path& file, CellCoord& out) -> bool {
        const std::wstring stem = file.stem().wstring();  // without extension
        if (stem.rfind(L"cell_", 0) != 0) {
            return false;
        }
        const std::wstring rest = stem.substr(5);
        const size_t us1 = rest.find(L'_');
        if (us1 == std::wstring::npos) {
            return false;
        }
        const size_t us2 = rest.find(L'_', us1 + 1);
        const std::wstring xStr = rest.substr(0, us1);
        const std::wstring zStr =
            (us2 == std::wstring::npos) ? rest.substr(us1 + 1) : rest.substr(us1 + 1, us2 - (us1 + 1));
        try {
            out.x = std::stoi(xStr);
            out.z = std::stoi(zStr);
        } catch (...) {
            return false;
        }
        return true;
    };

    std::filesystem::recursive_directory_iterator it(root, std::filesystem::directory_options::skip_permission_denied,
                                                     ec);
    const std::filesystem::recursive_directory_iterator end;
    for (; !ec && it != end; it.increment(ec)) {
        const auto& entry = *it;
        std::error_code entryEc;
        if (!entry.is_regular_file(entryEc)) {
            continue;
        }
        const std::filesystem::path p = entry.path();
        if (CellFileExtensionRank(p, want) == std::numeric_limits<uint32_t>::max()) {
            continue;
        }

        CellCoord c;
        if (tryParseCell(p, c)) {
            availableCells_.insert(c);
            auto existing = availableCellPaths_.find(c);
            if (existing == availableCellPaths_.end() ||
                CellFileExtensionRank(p, want) < CellFileExtensionRank(std::filesystem::path(existing->second), want)) {
                availableCellPaths_[c] = CellIndexPathString(p);
            }
        }
    }
}

void StreamingManager::Update(float deltaTime, const Vec3& cameraPosition, const Vec3& cameraDirection,
                              const Vec3& cameraVelocity) {
    if (!initialized_) {
        return;
    }

    currentFrame_++;
    elapsedTime_ += deltaTime;

    // Reset per-frame scheduler-budget counters. These accumulate across the queue/commit
    // passes below (including the headless-mode second completion drain), so they must be
    // cleared once at the frame boundary rather than inside those passes.
    stats_.uploadBytesThisFrame = 0;
    stats_.loadStartsThisFrame = 0;
    stats_.budgetDeferredCells = 0;

    lastCameraPosition_ = cameraPosition;
    lastCameraDirection_ = cameraDirection;
    lastCameraVelocity_ = cameraVelocity;

    // Update sub-systems
    worldPartition_->Update(deltaTime, cameraPosition, cameraDirection);
    // Keep InterestManager in sync so priority/interest calculations can work off camera state.
    interestManager_->SetCameraPosition(cameraPosition, cameraDirection, cameraVelocity);
    interestManager_->Update(deltaTime);
    {
        // Build a view-projection from camera state and config-supplied lens
        // parameters so LOD screen-size selection runs against a real matrix.
        const Vec3 target = cameraPosition + cameraDirection;
        const Vec3 up(0.0f, 1.0f, 0.0f);
        const Mat4 view = Mat4::LookAt(cameraPosition, target, up);
        const Mat4 projection = Mat4::Perspective(config_.cameraFovRadians, config_.cameraAspectRatio,
                                                  config_.cameraNearPlane, config_.cameraFarPlane);
        const Mat4 viewProjection = projection * view;
        lodSystem_->Update(deltaTime, cameraPosition, viewProjection);
    }
    evictionPolicy_->Update(deltaTime, cameraPosition, currentFrame_);

    asyncIO_->Update();

    // Apply async load/unload completions from worker threads.
    ProcessCellOpCompletions();

    // Update streaming
    UpdateStreaming(deltaTime, cameraPosition, cameraDirection);

    // Update predictive streaming
    if (config_.enablePrediction) {
        UpdatePredictiveStreaming(cameraPosition, cameraDirection, cameraVelocity);
    }

    // Update priorities
    UpdatePriority(cameraPosition, cameraDirection);

    // Process queues
    ProcessLoadQueue();
    ProcessUnloadQueue();

    // Tests and headless tools may use StreamingManager without booting worker threads.
    // In that mode, drain queued jobs on the main thread so cell IO still makes progress.
    if (!Next::JobSystem::Instance().IsInitialized()) {
        Next::JobSystem::Instance().Pump(-1.0);
        ProcessCellOpCompletions();
    }

    // Enforce memory budget
    EnforceMemoryBudget();

    // Update statistics
    UpdateStatistics(deltaTime);
}

void StreamingManager::ProcessCellOpCompletions() {
    std::vector<CellOpCompletion> local;
    {
        std::lock_guard<std::mutex> lock(completionMutex_);
        if (completions_.empty()) {
            return;
        }
        local.swap(completions_);
    }

    const uint64_t uploadBudget = config_.maxUploadBytesPerFrame;
    std::vector<CellOpCompletion> deferred;

    for (size_t completionIndex = 0; completionIndex < local.size(); ++completionIndex) {
        CellOpCompletion& c = local[completionIndex];

        // Per-frame commit/upload byte budget: once this frame's budget is spent, defer the
        // remaining successful loads to a later frame so committing a burst of completions
        // can't stall the main thread. Always allow at least one commit per frame (when
        // uploadBytesThisFrame == 0) so forward progress is guaranteed even for a large cell.
        if (c.isLoad && c.success && uploadBudget != 0 && stats_.uploadBytesThisFrame != 0 &&
            stats_.uploadBytesThisFrame + c.bytes > uploadBudget) {
            for (size_t j = completionIndex; j < local.size(); ++j) {
                deferred.push_back(std::move(local[j]));
            }
            break;
        }

        CellData* cell = worldPartition_ ? worldPartition_->GetCell(c.coord) : nullptr;

        if (c.isLoad) {
            // Clear active op tracking.
            activeLoadOperations_.erase(c.coord);

            if (!cell) {
                // Cell no longer exists; drop the package ref if we loaded it.
                if (c.success && c.packageBacked && !c.packageName.empty()) {
                    Next::AssetManager::Instance().UnloadPackage(c.packageName);
                }
                continue;
            }

            if (!c.success) {
                worldPartition_->UpdateCellState(c.coord, CellLoadState::Error);
                stats_.failedLoads++;
                continue;
            }

            // If the cell was requested to unload while loading, immediately release the package.
            if (cell->state == CellLoadState::Unloading || cell->state == CellLoadState::Unloaded) {
                if (c.packageBacked && !c.packageName.empty()) {
                    Next::AssetManager::Instance().UnloadPackage(c.packageName);
                }
                ReleaseCellLayers(cell);
                cell->metadata.memorySize = 0;
                cell->metadata.dataSize = 0;
                worldPartition_->UpdateCellState(c.coord, CellLoadState::Unloaded);
                continue;
            }

            // Track which package backs this cell (used for unload).
            if (c.packageBacked && !c.packageName.empty()) {
                cellToPackageName_[c.coord] = c.packageName;
            } else {
                cellToPackageName_.erase(c.coord);
            }

            void* layerData = nullptr;
            if (!c.packageBacked) {
                if (c.rawCellData.empty()) {
                    worldPartition_->UpdateCellState(c.coord, CellLoadState::Error);
                    stats_.failedLoads++;
                    continue;
                }

                layerData =
                    memoryPool_ ? memoryPool_->Allocate(c.rawCellData.size(), alignof(std::max_align_t)) : nullptr;
                if (!layerData) {
                    NEXT_LOG_ERROR("Failed to allocate %zu bytes for cell (%d,%d)", c.rawCellData.size(), c.coord.x,
                                   c.coord.z);
                    worldPartition_->UpdateCellState(c.coord, CellLoadState::Error);
                    stats_.failedLoads++;
                    continue;
                }

                std::memcpy(layerData, c.rawCellData.data(), c.rawCellData.size());
            }

            CellData::LayerData ld;
            ld.layer = CellLayer::StaticMesh;
            ld.data = layerData;
            ld.size = c.bytes;
            ld.state = CellLoadState::Loaded;
            cell->layers[CellLayer::StaticMesh] = ld;

            const uint64_t diskBytes = c.diskBytes != 0 ? c.diskBytes : c.bytes;
            cell->isPlaceholderData = false;
            cell->metadata.dataSize = diskBytes;
            cell->metadata.memorySize = c.bytes;  // approximate for package-backed cells
            cell->metadata.SetLayerPresent(CellLayer::StaticMesh);
            worldPartition_->UpdateCellState(c.coord, CellLoadState::Loaded);
            evictionPolicy_->RecordAccess(c.coord, currentFrame_);
            stats_.uploadBytesThisFrame += c.bytes;
        } else {
            activeUnloadOperations_.erase(c.coord);
            cellToPackageName_.erase(c.coord);

            if (!cell) {
                continue;
            }

            // Unload completion always transitions to Unloaded in this framework implementation.
            ReleaseCellLayers(cell);
            cell->metadata.memorySize = 0;
            cell->metadata.dataSize = 0;
            cell->isPlaceholderData = false;
            worldPartition_->UpdateCellState(c.coord, CellLoadState::Unloaded);
        }
    }

    // Re-queue any completions deferred by the upload budget so they commit on a later frame.
    if (!deferred.empty()) {
        stats_.budgetDeferredCells += static_cast<uint32_t>(deferred.size());
        std::lock_guard<std::mutex> lock(completionMutex_);
        for (CellOpCompletion& existing : completions_) {
            deferred.push_back(std::move(existing));
        }
        completions_.swap(deferred);
    }
}

void StreamingManager::LoadCell(const CellCoord& coord, float priority) {
    if (!initialized_) {
        return;
    }

    evictionPolicy_->SetCellPriority(coord, priority);

    CellLoadRequest request;
    request.coord = coord;
    request.priority = priority;
    request.frameIndex = static_cast<uint32_t>(currentFrame_);

    QueueCellLoad(request);
}

void StreamingManager::UnloadCell(const CellCoord& coord) {
    if (!initialized_) {
        return;
    }

    CellUnloadRequest request;
    request.coord = coord;
    request.frameIndex = static_cast<uint32_t>(currentFrame_);

    QueueCellUnload(request);
}

void StreamingManager::ReloadCell(const CellCoord& coord) {
    if (!initialized_) {
        return;
    }

    // Unload first
    UnloadCell(coord);

    // Then reload
    LoadCell(coord, 1.0f);  // High priority for reload
}

bool StreamingManager::IsCellLoaded(const CellCoord& coord) const {
    return worldPartition_ ? worldPartition_->IsCellLoaded(coord) : false;
}

CellData* StreamingManager::GetCell(const CellCoord& coord) {
    return worldPartition_ ? worldPartition_->GetCell(coord) : nullptr;
}

const CellData* StreamingManager::GetCell(const CellCoord& coord) const {
    return worldPartition_ ? worldPartition_->GetCell(coord) : nullptr;
}

bool StreamingManager::GetCellInfo(const CellCoord& coord, StreamingCellInfo& outInfo) const {
    outInfo = {};
    if (!worldPartition_) {
        return false;
    }

    const CellData* cell = worldPartition_->GetCell(coord);
    if (!cell) {
        return false;
    }

    outInfo = MakeStreamingCellInfo(*cell);
    return true;
}

std::vector<CellCoord> StreamingManager::GetLoadedCells() const {
    return worldPartition_ ? worldPartition_->GetLoadedCells() : std::vector<CellCoord>();
}

std::vector<StreamingCellInfo> StreamingManager::GetLoadedCellInfos() const {
    std::vector<StreamingCellInfo> infos;
    if (!worldPartition_) {
        return infos;
    }

    const std::vector<CellCoord> loadedCells = worldPartition_->GetLoadedCells();
    infos.reserve(loadedCells.size());
    for (const CellCoord& coord : loadedCells) {
        const CellData* cell = worldPartition_->GetCell(coord);
        if (cell) {
            infos.push_back(MakeStreamingCellInfo(*cell));
        }
    }
    return infos;
}

std::vector<CellCoord> StreamingManager::GetCellsInRange(const Vec3& position, float radius) const {
    std::vector<CellCoord> cells;

    if (!worldPartition_) {
        return cells;
    }

    const float cellSize = std::max(1.0f, worldPartition_->GetConfig().cellSize);
    const float invCellSize = 1.0f / cellSize;

    const int32_t minX = static_cast<int32_t>(std::floor((position.x - radius) * invCellSize));
    const int32_t maxX = static_cast<int32_t>(std::floor((position.x + radius) * invCellSize));
    const int32_t minZ = static_cast<int32_t>(std::floor((position.z - radius) * invCellSize));
    const int32_t maxZ = static_cast<int32_t>(std::floor((position.z + radius) * invCellSize));

    // Conservative reserve; we'll filter by circle check below.
    const int64_t spanX = static_cast<int64_t>(maxX) - static_cast<int64_t>(minX) + 1;
    const int64_t spanZ = static_cast<int64_t>(maxZ) - static_cast<int64_t>(minZ) + 1;
    if (spanX > 0 && spanZ > 0 && spanX * spanZ < 1024 * 1024) {
        cells.reserve(static_cast<size_t>(spanX * spanZ));
    }

    const float radiusSq = radius * radius;
    const bool hasAvailableCellIndex = !availableCells_.empty() || !bundleAvailableCells_.empty();
    for (int32_t x = minX; x <= maxX; ++x) {
        for (int32_t z = minZ; z <= maxZ; ++z) {
            CellCoord coord{x, z};
            Vec3 cellCenter = worldPartition_->CellToWorld(coord);
            Vec3 toCell = cellCenter - position;
            if (toCell.Dot(toCell) <= radiusSq) {
                if (!config_.allowPlaceholderCellLoad && hasAvailableCellIndex &&
                    availableCells_.find(coord) == availableCells_.end() &&
                    bundleAvailableCells_.find(coord) == bundleAvailableCells_.end()) {
                    continue;
                }
                cells.push_back(coord);
            }
        }
    }

    return cells;
}

StreamingHandle StreamingManager::LoadAssetBundle(const std::wstring& bundlePath) {
    if (!initialized_) {
        return StreamingHandle{0};
    }

    std::filesystem::path p(bundlePath);
    const std::string bundlePathForLog = PathLogString(p);
    if (!std::filesystem::exists(p)) {
        NEXT_LOG_ERROR("LoadAssetBundle: path does not exist: %s", bundlePathForLog.c_str());
        return StreamingHandle{0};
    }

    auto tryParseCell = [](const std::filesystem::path& file, CellCoord& out) -> bool {
        const std::wstring stem = file.stem().wstring();  // without extension
        if (stem.rfind(L"cell_", 0) != 0) {
            return false;
        }

        // Accept:
        // - cell_X_Z
        // - cell_X_Z_anything...
        const std::wstring rest = stem.substr(5);
        const size_t us1 = rest.find(L'_');
        if (us1 == std::wstring::npos) {
            return false;
        }
        const size_t us2 = rest.find(L'_', us1 + 1);
        const std::wstring xStr = rest.substr(0, us1);
        const std::wstring zStr =
            (us2 == std::wstring::npos) ? rest.substr(us1 + 1) : rest.substr(us1 + 1, us2 - (us1 + 1));
        try {
            out.x = std::stoi(xStr);
            out.z = std::stoi(zStr);
        } catch (...) {
            return false;
        }
        return true;
    };

    AssetBundle bundle;
    static uint64_t sNextBundleId = 1;
    bundle.bundleId = sNextBundleId++;
    bundle.bundlePath = bundlePath;
    bundle.totalSize = 0;
    bundle.compressedSize = 0;

    const std::wstring want = config_.cellFileExtension.empty() ? L".ncell" : config_.cellFileExtension;
    std::unordered_map<CellCoord, std::wstring, CellCoord::Hash> discoveredCellPaths;
    std::unordered_map<CellCoord, uint64_t, CellCoord::Hash> discoveredCellSizes;

    auto recordBundleCell = [&](const std::filesystem::path& file) {
        if (CellFileExtensionRank(file, want) == std::numeric_limits<uint32_t>::max()) {
            return;
        }

        CellCoord c;
        if (!tryParseCell(file, c)) {
            return;
        }

        uint64_t fileSize = 0;
        std::error_code fileSizeEc;
        fileSize = static_cast<uint64_t>(std::filesystem::file_size(file, fileSizeEc));
        if (fileSizeEc) {
            fileSize = 0;
        }

        auto existing = discoveredCellPaths.find(c);
        if (existing != discoveredCellPaths.end()) {
            if (CellFileExtensionRank(file, want) >=
                CellFileExtensionRank(std::filesystem::path(existing->second), want)) {
                return;
            }
            bundle.totalSize -= discoveredCellSizes[c];
        } else {
            bundle.cells.push_back(c);
        }

        discoveredCellPaths[c] = CellIndexPathString(file);
        discoveredCellSizes[c] = fileSize;
        bundle.totalSize += fileSize;
    };

    std::error_code pathEc;
    if (std::filesystem::is_directory(p, pathEc)) {
        std::filesystem::recursive_directory_iterator it(p, std::filesystem::directory_options::skip_permission_denied,
                                                         pathEc);
        const std::filesystem::recursive_directory_iterator end;
        for (; !pathEc && it != end; it.increment(pathEc)) {
            const auto& entry = *it;
            std::error_code entryEc;
            if (!entry.is_regular_file(entryEc)) {
                continue;
            }
            recordBundleCell(entry.path());
        }
    } else {
        recordBundleCell(p);
    }

    if (bundle.cells.empty()) {
        NEXT_LOG_WARNING("LoadAssetBundle: no cells found under %s", bundlePathForLog.c_str());
    }

    const uint64_t id = bundle.bundleId;
    assetBundles_[id] = bundle;
    bundleCellPathIndex_[id] = std::move(discoveredCellPaths);
    const auto& bundleCellPaths = bundleCellPathIndex_[id];
    for (const CellCoord& c : assetBundles_[id].cells) {
        cellToBundle_[c] = id;
        auto pathIt = bundleCellPaths.find(c);
        if (pathIt != bundleCellPaths.end()) {
            bundleAvailableCells_.insert(c);
            bundleCellPaths_[c] = pathIt->second;
            bundleCellPathOwners_[c] = id;
        }
        LoadCell(c, 1.0f);
    }

    return StreamingHandle{id};
}

void StreamingManager::UnloadAssetBundle(StreamingHandle handle) {
    if (!initialized_ || !handle) {
        return;
    }

    auto it = assetBundles_.find(handle.id);
    if (it == assetBundles_.end()) {
        return;
    }

    auto findReplacementBundle = [this, bundleId = handle.id](const CellCoord& coord, uint64_t& replacementId,
                                                              std::wstring& replacementPath) {
        replacementId = 0;
        replacementPath.clear();

        for (const auto& [otherId, otherBundle] : assetBundles_) {
            if (otherId == bundleId) {
                continue;
            }
            if (std::find(otherBundle.cells.begin(), otherBundle.cells.end(), coord) == otherBundle.cells.end()) {
                continue;
            }
            if (otherId < replacementId) {
                continue;
            }
            replacementId = otherId;
        }

        if (replacementId == 0) {
            return false;
        }

        auto bundlePathsIt = bundleCellPathIndex_.find(replacementId);
        if (bundlePathsIt != bundleCellPathIndex_.end()) {
            auto pathIt = bundlePathsIt->second.find(coord);
            if (pathIt != bundlePathsIt->second.end()) {
                replacementPath = pathIt->second;
            }
        }
        return true;
    };

    for (const CellCoord& c : it->second.cells) {
        uint64_t replacementId = 0;
        std::wstring replacementPath;
        const bool stillReferenced = findReplacementBundle(c, replacementId, replacementPath);

        auto mapIt = cellToBundle_.find(c);
        if (stillReferenced) {
            if (mapIt == cellToBundle_.end() || mapIt->second == handle.id) {
                cellToBundle_[c] = replacementId;
            }

            auto ownerIt = bundleCellPathOwners_.find(c);
            if (ownerIt == bundleCellPathOwners_.end() || ownerIt->second == handle.id) {
                if (!replacementPath.empty()) {
                    bundleAvailableCells_.insert(c);
                    bundleCellPaths_[c] = replacementPath;
                    bundleCellPathOwners_[c] = replacementId;
                } else {
                    bundleCellPathOwners_.erase(c);
                    bundleCellPaths_.erase(c);
                    bundleAvailableCells_.erase(c);
                }
            }
            continue;
        }

        if (mapIt != cellToBundle_.end() && mapIt->second == handle.id) {
            cellToBundle_.erase(mapIt);
        }
        auto ownerIt = bundleCellPathOwners_.find(c);
        if (ownerIt != bundleCellPathOwners_.end() && ownerIt->second == handle.id) {
            bundleCellPathOwners_.erase(ownerIt);
            bundleCellPaths_.erase(c);
            bundleAvailableCells_.erase(c);
        }
        UnloadCell(c);
    }

    bundleCellPathIndex_.erase(handle.id);
    assetBundles_.erase(it);
}

bool StreamingManager::IsAssetBundleLoaded(StreamingHandle handle) const {
    if (!handle) {
        return false;
    }
    return assetBundles_.find(handle.id) != assetBundles_.end();
}

bool StreamingManager::GetAssetBundleInfo(StreamingHandle handle, AssetBundle& outBundle) const {
    outBundle = {};
    if (!handle) {
        return false;
    }

    auto it = assetBundles_.find(handle.id);
    if (it == assetBundles_.end()) {
        return false;
    }

    outBundle = it->second;
    return true;
}

void StreamingManager::LoadCellLayer(const CellCoord& coord, CellLayer layer, float priority) {
    if (!initialized_) {
        return;
    }

    // StaticMesh stays on the async single-payload .ncell path (unchanged).
    if (layer == CellLayer::StaticMesh) {
        LoadCell(coord, priority);
        return;
    }

    // The cell must exist at the cell level first (same contract as before). If it doesn't yet, kick a
    // cell load; callers drive Update() to create it, then call LoadCellLayer again.
    if (!worldPartition_->GetCell(coord)) {
        LoadCell(coord, priority);
    }
    CellData* cell = worldPartition_->GetCell(coord);
    if (!cell) {
        return;
    }

    // Already loaded with real data?
    auto existing = cell->layers.find(layer);
    if (existing != cell->layers.end() && existing->second.state == CellLoadState::Loaded &&
        existing->second.data != nullptr) {
        return;
    }

    // Real IO (ADR-0014): read this layer's decompressed bytes from the layered cell file.
    std::vector<uint8_t> layerBytes;
    if (TryReadLayeredCellLayer(coord, layer, layerBytes)) {
        void* mem = nullptr;
        if (!layerBytes.empty()) {
            mem = memoryPool_ ? memoryPool_->Allocate(layerBytes.size(), alignof(std::max_align_t)) : nullptr;
            if (!mem) {
                NEXT_LOG_ERROR("LoadCellLayer: failed to allocate %zu bytes for layer=%u cell(%d,%d)",
                               layerBytes.size(), static_cast<uint32_t>(layer), coord.x, coord.z);
                return;  // had real cooked data but couldn't allocate -> fail-closed, never fake a placeholder
            }
            std::memcpy(mem, layerBytes.data(), layerBytes.size());
        }
        if (mem != nullptr || layerBytes.empty()) {
            CellData::LayerData ld;
            ld.layer = layer;
            ld.data = mem;
            ld.size = layerBytes.size();
            ld.state = CellLoadState::Loaded;
            ld.generation = nextLayerGeneration_++;
            cell->layers[layer] = ld;
            cell->metadata.SetLayerPresent(layer);  // memory is counted via CellData::MemorySize() (sums layers)
            return;
        }
    }

    // No cooked data for this layer.
    if (!config_.allowPlaceholderCellLoad) {
        NEXT_LOG_WARNING("LoadCellLayer: no cooked data for layer=%u cell(%d,%d); fail-closed (no placeholder)",
                         static_cast<uint32_t>(layer), coord.x, coord.z);
        return;
    }

    // Placeholder (keeps demos running without authored layer data).
    CellData::LayerData ld;
    ld.layer = layer;
    ld.data = nullptr;
    ld.size = 0;
    ld.state = CellLoadState::Loaded;
    ld.generation = nextLayerGeneration_++;
    cell->layers[layer] = ld;
    cell->metadata.SetLayerPresent(layer);
    cell->isPlaceholderData = true;
}

bool StreamingManager::TryReadLayeredCellLayer(const CellCoord& coord, CellLayer layer,
                                               std::vector<uint8_t>& out) const {
    out.clear();
    const std::wstring ext = config_.layeredCellExtension.empty() ? L".nlc" : config_.layeredCellExtension;
    const std::filesystem::path path = std::filesystem::path(config_.cellDataDirectory) /
                                       (L"cell_" + std::to_wstring(coord.x) + L"_" + std::to_wstring(coord.z) + ext);

    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) {
        return false;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    file.seekg(0, std::ios::end);
    const std::streamoff fileSize = file.tellg();
    if (fileSize <= 0) {
        return false;
    }
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> blob(static_cast<size_t>(fileSize));
    if (!file.read(reinterpret_cast<char*>(blob.data()), fileSize)) {
        return false;
    }
    return ExtractLayer(blob.data(), blob.size(), layer, out);
}

void StreamingManager::UnloadCellLayer(const CellCoord& coord, CellLayer layer) {
    if (!initialized_) {
        return;
    }

    CellData* cell = worldPartition_->GetCell(coord);
    if (!cell) {
        return;
    }

    auto it = cell->layers.find(layer);
    if (it == cell->layers.end()) {
        return;
    }

    if (it->second.data && memoryPool_) {
        memoryPool_->Free(it->second.data);
    }
    cell->layers.erase(it);  // memory recount is automatic: CellData::MemorySize() sums the remaining layers
    cell->metadata.SetLayerPresent(layer, false);
}

void StreamingManager::ReleaseCellLayers(CellData* cell) {
    if (!cell) {
        return;
    }

    for (auto& [layer, layerData] : cell->layers) {
        (void)layer;
        if (layerData.data && memoryPool_) {
            memoryPool_->Free(layerData.data);
        }
        layerData.data = nullptr;
        layerData.size = 0;
        layerData.state = CellLoadState::Unloaded;
    }

    cell->layers.clear();
    cell->metadata.layerMask = 0;
}

bool StreamingManager::IsCellLayerLoaded(const CellCoord& coord, CellLayer layer) const {
    const CellData* cell = worldPartition_ ? worldPartition_->GetCell(coord) : nullptr;
    if (cell) {
        return cell->IsLayerLoaded(layer);
    }
    return false;
}

void StreamingManager::SetCellPriority(const CellCoord& coord, float priority) {
    if (evictionPolicy_) {
        evictionPolicy_->SetCellPriority(coord, priority);
    }
}

void StreamingManager::BoostCellPriority(const CellCoord& coord, float boost) {
    if (!evictionPolicy_) {
        return;
    }

    float currentPriority = evictionPolicy_->GetCellPriority(coord);
    evictionPolicy_->SetCellPriority(coord, currentPriority + boost);
}

void StreamingManager::SetGlobalPriorityOverride(std::function<float(const CellCoord&, float)> priorityFunc) {
    priorityOverride_ = priorityFunc;
}

void StreamingManager::SetConfig(const StreamingManagerConfig& config) {
    size_t memoryBudgetBytes = 0;
    if (!ValidateStreamingManagerConfig(config, memoryBudgetBytes)) {
        return;
    }

    config_ = ResolveStreamingManagerConfigPaths(config);
    if (!initialized_) {
        return;
    }

    // Update sub-system configs
    const WorldPartitionConfig partitionConfig = MakeWorldPartitionConfig(config_);
    if (worldPartition_) {
        worldPartition_->SetConfig(partitionConfig);
    }

    if (lodSystem_) {
        lodSystem_->SetConfig(LODSystemConfig());
    }
    if (evictionPolicy_) {
        evictionPolicy_->SetConfig(EvictionPolicyConfig());
        evictionPolicy_->SetMemoryBudget(memoryBudgetBytes);
        evictionPolicy_->SetMaxCellCount(static_cast<uint32_t>(partitionConfig.maxLoadedCells));
    }
    ScanAvailableCells();
}

StreamingStatistics StreamingManager::GetStatistics() const {
    return stats_;
}

WorldPartition::Statistics StreamingManager::GetWorldPartitionStatistics() const {
    return worldPartition_ ? worldPartition_->GetStatistics() : WorldPartition::Statistics();
}

InterestManager::Statistics StreamingManager::GetInterestStatistics() const {
    return interestManager_ ? interestManager_->GetStatistics() : InterestManager::Statistics();
}

IOStatistics StreamingManager::GetIOStatistics() const {
    return asyncIO_ ? asyncIO_->GetStatistics() : IOStatistics();
}

LODSystem::Statistics StreamingManager::GetLODStatistics() const {
    return lodSystem_ ? lodSystem_->GetStatistics() : LODSystem::Statistics();
}

EvictionPolicy::Statistics StreamingManager::GetEvictionStatistics() const {
    return evictionPolicy_ ? evictionPolicy_->GetStatistics() : EvictionPolicy::Statistics();
}

void StreamingManager::ResetStatistics() {
    stats_ = StreamingStatistics();
}

size_t StreamingManager::GetMemoryUsage() const {
    if (!worldPartition_) {
        return 0;
    }

    size_t usage = 0;

    auto loadedCells = worldPartition_->GetLoadedCells();
    for (const CellCoord& coord : loadedCells) {
        const CellData* cell = worldPartition_->GetCell(coord);
        if (cell) {
            usage += cell->MemorySize();
        }
    }

    return usage;
}

size_t StreamingManager::GetMemoryBudget() const {
    size_t memoryBudgetBytes = 0;
    if (!ComputeStreamingMemoryBudgetBytes(config_, memoryBudgetBytes)) {
        return 0;
    }
    return memoryBudgetBytes;
}

float StreamingManager::GetMemoryUtilization() const {
    size_t budget = GetMemoryBudget();
    if (budget == 0) {
        return 0.0f;
    }
    return static_cast<float>(GetMemoryUsage()) / static_cast<float>(budget);
}

void StreamingManager::UnloadAll() {
    if (!initialized_) {
        return;
    }

    // Unload loaded cells + cancel any in-flight loads.
    std::vector<CellCoord> loadedCells = worldPartition_->GetLoadedCells();
    for (const CellCoord& coord : loadedCells) {
        UnloadCell(coord);
    }
    std::vector<CellCoord> activeLoads;
    activeLoads.reserve(activeLoadOperations_.size());
    for (const auto& [coord, op] : activeLoadOperations_) {
        (void)op;
        activeLoads.push_back(coord);
    }
    for (const CellCoord& coord : activeLoads) {
        UnloadCell(coord);
    }

    // Drain unload jobs so shutdown/level transitions don't leak package refs.
    constexpr uint32_t kMaxIters = 500;
    for (uint32_t i = 0; i < kMaxIters; ++i) {
        ProcessUnloadQueue();
        Next::JobSystem::Instance().Pump(0.25);
        ProcessCellOpCompletions();

        if (unloadQueue_.empty() && activeUnloadOperations_.empty() && activeLoadOperations_.empty()) {
            break;
        }
    }

    NEXT_LOG_INFO("UnloadAll requested for %zu cells (inflightLoads=%zu inflightUnloads=%zu)", loadedCells.size(),
                  activeLoadOperations_.size(), activeUnloadOperations_.size());
}

void StreamingManager::Shutdown() {
    if (!initialized_) {
        return;
    }

    // Join the async IO worker threads up front. UnloadAll() and the buffer cleanup below
    // free the read buffers that IO workers may still be reading into, and request
    // cancellation is only best-effort (it does not guarantee a worker has stopped touching
    // a buffer). Stopping the threads first prevents a shutdown-time heap-use-after-free
    // when a load is still in flight. asyncIO_->Shutdown() is idempotent; the asyncIO_.reset()
    // further below destroys the now-quiesced system.
    if (asyncIO_) {
        asyncIO_->Shutdown();
    }

    UnloadAll();

    // Shutdown sub-systems
    if (memoryPool_) {
        memoryPool_->Shutdown();
        memoryPool_.reset();
    }

    if (evictionPolicy_) {
        evictionPolicy_->Shutdown();
        evictionPolicy_.reset();
    }

    if (lodSystem_) {
        lodSystem_->Shutdown();
        lodSystem_.reset();
    }

    if (interestManager_) {
        interestManager_->Shutdown();
        interestManager_.reset();
    }

    if (asyncIO_) {
        asyncIO_->Shutdown();
        asyncIO_.reset();
    }

    if (worldPartition_) {
        worldPartition_->Shutdown();
        worldPartition_.reset();
    }

    loadQueue_.clear();
    unloadQueue_.clear();
    activeLoadOperations_.clear();
    activeUnloadOperations_.clear();
    cellToPackageName_.clear();
    cellToBundle_.clear();
    assetBundles_.clear();
    availableCellPaths_.clear();
    availableCells_.clear();
    bundleCellPathIndex_.clear();
    bundleCellPathOwners_.clear();
    bundleCellPaths_.clear();
    bundleAvailableCells_.clear();
    {
        std::lock_guard<std::mutex> lock(completionMutex_);
        completions_.clear();
    }

    initialized_ = false;
    NEXT_LOG_INFO("StreamingManager shutdown complete");
}

// ===== Private Methods =====

void StreamingManager::UpdateStreaming(float deltaTime, const Vec3& cameraPosition, const Vec3& cameraDirection) {
    (void)deltaTime;
    // If we're over budget, do not queue additional loads this frame. Let eviction catch up.
    if (GetMemoryUtilization() >= config_.evictionThreshold) {
        // Still allow distance-based unloads.
        std::vector<CellCoord> loadedCells = worldPartition_->GetLoadedCells();
        const float unloadRadiusSq = config_.unloadRadius * config_.unloadRadius;
        for (const CellCoord& coord : loadedCells) {
            const CellData* cell = worldPartition_->GetCell(coord);
            if (!cell) {
                continue;
            }

            Vec3 toCell = cell->metadata.worldPosition - cameraPosition;
            if (toCell.Dot(toCell) > unloadRadiusSq) {
                UnloadCell(coord);
            }
        }
        return;
    }

    // Find cells that should be loaded (includes unloaded cells).
    std::vector<CellCoord> desiredCells = GetCellsInRange(cameraPosition, config_.loadRadius);

    // Load / queue cells.
    for (const CellCoord& coord : desiredCells) {
        const CellData* existing = worldPartition_->GetCell(coord);
        if (existing) {
            if (existing->state == CellLoadState::Loaded || existing->state == CellLoadState::Queued ||
                existing->state == CellLoadState::Loading || existing->state == CellLoadState::Decompressing ||
                existing->state == CellLoadState::Uploading) {
                continue;
            }
        }

        float priority = CalculateCellPriority(coord, cameraPosition, cameraDirection);
        LoadCell(coord, priority);
    }

    // Unload cells that are too far away.
    std::vector<CellCoord> loadedCells = worldPartition_->GetLoadedCells();
    const float unloadRadiusSq = config_.unloadRadius * config_.unloadRadius;
    for (const CellCoord& coord : loadedCells) {
        const CellData* cell = worldPartition_->GetCell(coord);
        if (!cell) {
            continue;
        }

        Vec3 toCell = cell->metadata.worldPosition - cameraPosition;
        if (toCell.Dot(toCell) > unloadRadiusSq) {
            UnloadCell(coord);
        }
    }
}

void StreamingManager::UpdatePredictiveStreaming(const Vec3& cameraPosition, const Vec3& cameraDirection,
                                                 const Vec3& cameraVelocity) {
    const float velSq = cameraVelocity.Dot(cameraVelocity);
    if (velSq < 1e-4f) {
        return;
    }

    const float horizon = std::max(0.1f, config_.predictionTime);
    const uint32_t samples = std::max(1u, config_.predictionSamples);
    const float denom = (samples > 1) ? static_cast<float>(samples - 1) : 1.0f;

    std::unordered_set<CellCoord, CellCoord::Hash> predicted;

    for (uint32_t i = 0; i < samples; ++i) {
        const float t = horizon * (static_cast<float>(i) / denom);
        const Vec3 pos = cameraPosition + cameraVelocity * t;
        const float r = std::max(0.0f, config_.prefetchRadius);
        if (r <= 0.0f) {
            continue;
        }

        for (const CellCoord& coord : GetCellsInRange(pos, r)) {
            predicted.insert(coord);
        }
    }

    for (const CellCoord& coord : predicted) {
        const CellData* existing = worldPartition_->GetCell(coord);
        if (existing) {
            if (existing->state == CellLoadState::Loaded || existing->state == CellLoadState::Queued ||
                existing->state == CellLoadState::Loading || existing->state == CellLoadState::Decompressing ||
                existing->state == CellLoadState::Uploading) {
                continue;
            }
        }

        float p = CalculateCellPriority(coord, cameraPosition, cameraDirection);
        p += 0.25f;  // predictive bias
        LoadCell(coord, p);
    }

    if (config_.logStreamingEvents && !predicted.empty()) {
        NEXT_LOG_INFO("Predictive streaming queued %zu cells", predicted.size());
    }
}

void StreamingManager::UpdatePriority(const Vec3& cameraPosition, const Vec3& cameraDirection) {
    // Update priorities for all queued cells
    for (auto& request : loadQueue_) {
        request.priority = CalculateCellPriority(request.coord, cameraPosition, cameraDirection);
    }
}

void StreamingManager::ProcessLoadQueue() {
    // If we're already over budget, don't start new loads this frame.
    if (GetMemoryUtilization() >= config_.evictionThreshold) {
        return;
    }

    // Sort by priority (highest first)
    std::sort(loadQueue_.begin(), loadQueue_.end(),
              [](const CellLoadRequest& a, const CellLoadRequest& b) { return a.priority > b.priority; });

    // Process loads while under the in-flight cap.
    std::vector<CellCoord> started;
    size_t inflight = activeLoadOperations_.size();
    for (const auto& request : loadQueue_) {
        if (inflight >= config_.maxConcurrentLoads) {
            break;
        }
        // Per-frame admission budget: bound how many new read+decompress pipelines we kick
        // off in a single frame so a large queue can't submit a thundering herd at once.
        if (config_.maxLoadStartsPerFrame != 0 && stats_.loadStartsThisFrame >= config_.maxLoadStartsPerFrame) {
            break;
        }
        if (activeLoadOperations_.count(request.coord) != 0) {
            continue;
        }

        ProcessCellLoad(request);
        started.push_back(request.coord);
        stats_.loadStartsThisFrame++;
        inflight = activeLoadOperations_.size();
    }

    // Remove started requests (queue is small; O(n^2) is fine).
    if (!started.empty()) {
        loadQueue_.erase(std::remove_if(loadQueue_.begin(), loadQueue_.end(),
                                        [&](const CellLoadRequest& r) {
                                            for (const CellCoord& c : started) {
                                                if (r.coord == c)
                                                    return true;
                                            }
                                            return false;
                                        }),
                         loadQueue_.end());
    }
}

void StreamingManager::ProcessUnloadQueue() {
    std::vector<CellCoord> started;
    size_t inflight = activeUnloadOperations_.size();
    for (const auto& request : unloadQueue_) {
        if (inflight >= config_.maxConcurrentUnloads) {
            break;
        }
        if (activeUnloadOperations_.count(request.coord) != 0) {
            continue;
        }
        ProcessCellUnload(request);
        started.push_back(request.coord);
        inflight = activeUnloadOperations_.size();
    }

    if (!started.empty()) {
        unloadQueue_.erase(std::remove_if(unloadQueue_.begin(), unloadQueue_.end(),
                                          [&](const CellUnloadRequest& r) {
                                              for (const CellCoord& c : started) {
                                                  if (r.coord == c)
                                                      return true;
                                              }
                                              return false;
                                          }),
                           unloadQueue_.end());
    }
}

void StreamingManager::QueueCellLoad(const CellLoadRequest& request) {
    // Deduplicate (queue is small).
    for (const auto& r : loadQueue_) {
        if (r.coord == request.coord) {
            return;
        }
    }
    loadQueue_.push_back(request);
}

void StreamingManager::QueueCellUnload(const CellUnloadRequest& request) {
    for (const auto& r : unloadQueue_) {
        if (r.coord == request.coord) {
            return;
        }
    }
    unloadQueue_.push_back(request);
}

void StreamingManager::ProcessCellLoad(const CellLoadRequest& request) {
    worldPartition_->RequestCellLoad(request.coord, request.priority);

    CellData* cell = worldPartition_->GetCell(request.coord);
    if (!cell) {
        OnCellLoadComplete(request.coord, false, 0);
        return;
    }

    // If already in-flight, don't double-submit.
    if (activeLoadOperations_.count(request.coord) != 0) {
        return;
    }

    // Determine file path.
    const std::wstring filePath = GetCellFilePath(request.coord);
    const std::filesystem::path fsPath(filePath);
    const std::string filePathForLog = PathLogString(fsPath);

    // Missing data: allow placeholder to keep demo running.
    if (!std::filesystem::exists(fsPath)) {
        if (!config_.allowPlaceholderCellLoad) {
            NEXT_LOG_ERROR("Missing cell file: %s", filePathForLog.c_str());
            worldPartition_->UpdateCellState(request.coord, CellLoadState::Error);
            stats_.failedLoads++;
            return;
        }

        // Placeholder load: no actual IO.
        worldPartition_->UpdateCellState(request.coord, CellLoadState::Loading);
        cell->isPlaceholderData = true;
        cell->metadata.memorySize = config_.placeholderCellSizeBytes;
        cell->metadata.dataSize = config_.placeholderCellSizeBytes;
        worldPartition_->UpdateCellState(request.coord, CellLoadState::Loaded);
        evictionPolicy_->RecordAccess(request.coord, currentFrame_);
        NEXT_LOG_WARNING("Loaded placeholder cell for (%d,%d) because '%s' was missing", request.coord.x,
                         request.coord.z, filePathForLog.c_str());
        return;
    }

    uint64_t fileSize = 0;
    try {
        fileSize = static_cast<uint64_t>(std::filesystem::file_size(fsPath));
    } catch (...) {
        fileSize = 0;
    }

    if (fileSize == 0) {
        NEXT_LOG_ERROR("Cell file is empty/unreadable: %s", filePathForLog.c_str());
        worldPartition_->UpdateCellState(request.coord, CellLoadState::Error);
        stats_.failedLoads++;
        return;
    }

    const std::string pkgName = fsPath.stem().string();
    const std::string pkgPath = fsPath.u8string();
    const bool packageBacked = IsPackageCellPath(fsPath);

    worldPartition_->UpdateCellState(request.coord, CellLoadState::Loading);

    ActiveCellOp op;
    op.filePath = filePath;
    op.packageName = pkgName;
    op.fileBytes = fileSize;
    op.packageBacked = packageBacked;

    if (!packageBacked) {
        if (fileSize > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
            NEXT_LOG_ERROR("Cell file is too large to load on this platform: %s", filePathForLog.c_str());
            worldPartition_->UpdateCellState(request.coord, CellLoadState::Error);
            stats_.failedLoads++;
            return;
        }

        size_t memoryBudgetBytes = 0;
        if (!ComputeStreamingMemoryBudgetBytes(config_, memoryBudgetBytes)) {
            NEXT_LOG_ERROR("Invalid streaming memory budget while loading cell: %zu MB", config_.memoryBudgetMB);
            worldPartition_->UpdateCellState(request.coord, CellLoadState::Error);
            stats_.failedLoads++;
            return;
        }
        const uint64_t rawCellPayloadBudgetBytes = static_cast<uint64_t>(memoryBudgetBytes);
        if (fileSize > rawCellPayloadBudgetBytes) {
            NEXT_LOG_ERROR("Cell file exceeds streaming memory budget: %s (%llu bytes > %llu bytes)",
                           filePathForLog.c_str(), static_cast<unsigned long long>(fileSize),
                           static_cast<unsigned long long>(rawCellPayloadBudgetBytes));
            worldPartition_->UpdateCellState(request.coord, CellLoadState::Error);
            stats_.failedLoads++;
            return;
        }

        try {
            op.rawReadBuffer.resize(static_cast<size_t>(fileSize));
        } catch (const std::exception& e) {
            NEXT_LOG_ERROR("Failed to allocate cell read buffer for %s: %s", filePathForLog.c_str(), e.what());
            worldPartition_->UpdateCellState(request.coord, CellLoadState::Error);
            stats_.failedLoads++;
            return;
        }
        void* readBuffer = op.rawReadBuffer.data();
        const CellCoord coord = request.coord;
        const uint32_t ioPriority =
            request.priority >= 1.0f ? 0u : static_cast<uint32_t>((1.0f - std::max(0.0f, request.priority)) * 1000.0f);
        const uint64_t asyncRequestId =
            asyncIO_
                ? asyncIO_->SubmitReadRequest(
                      filePath, 0, fileSize, readBuffer, CompressionType::None,
                      [this, coord, fileSize, rawCellPayloadBudgetBytes, ioPriority](bool success,
                                                                                     uint64_t bytesProcessed) {
                          auto pushCompletion = [this](CellOpCompletion completion) {
                              std::lock_guard<std::mutex> lock(completionMutex_);
                              completions_.push_back(std::move(completion));
                          };

                          CellOpCompletion c;
                          c.coord = coord;
                          c.isLoad = true;
                          c.packageBacked = false;
                          c.diskBytes = fileSize;
                          c.bytes = bytesProcessed;
                          c.success = success && bytesProcessed == fileSize;

                          auto it = activeLoadOperations_.find(coord);
                          if (!c.success) {
                              c.error = "AsyncIO read failed";
                              pushCompletion(std::move(c));
                              return;
                          }
                          if (it == activeLoadOperations_.end()) {
                              c.success = false;
                              c.error = "AsyncIO read completed after active load was removed";
                              pushCompletion(std::move(c));
                              return;
                          }

                          ActiveCellOp& active = it->second;
                          const CellPayloadInfo payload = InspectCellPayload(active.rawReadBuffer);
                          if (!payload.valid) {
                              c.success = false;
                              c.error = payload.error.empty() ? "Invalid cell payload" : payload.error;
                              pushCompletion(std::move(c));
                              return;
                          }

                          if (payload.decompressedSize > rawCellPayloadBudgetBytes) {
                              c.success = false;
                              c.bytes = 0;
                              c.error = "Cell payload exceeds streaming memory budget";
                              pushCompletion(std::move(c));
                              return;
                          }

                          if (payload.compression == CompressionType::None) {
                              c.bytes = payload.decompressedSize;
                              const size_t payloadOffset = static_cast<size_t>(payload.payloadOffset);
                              const size_t payloadSize = static_cast<size_t>(payload.payloadSize);
                              try {
                                  if (payloadOffset == 0 && payloadSize == active.rawReadBuffer.size()) {
                                      c.rawCellData = std::move(active.rawReadBuffer);
                                  } else {
                                      const uint8_t* payloadBegin = active.rawReadBuffer.data() + payloadOffset;
                                      c.rawCellData.assign(payloadBegin, payloadBegin + payloadSize);
                                  }
                              } catch (const std::exception& e) {
                                  c.success = false;
                                  c.bytes = 0;
                                  c.rawCellData.clear();
                                  c.error = std::string("Failed to allocate cell payload buffer: ") + e.what();
                              }
                              pushCompletion(std::move(c));
                              return;
                          }

                          try {
                              active.rawDecompressedBuffer.resize(static_cast<size_t>(payload.decompressedSize));
                          } catch (const std::exception& e) {
                              c.success = false;
                              c.bytes = 0;
                              c.error = std::string("Failed to allocate cell decompression buffer: ") + e.what();
                              pushCompletion(std::move(c));
                              return;
                          }
                          active.decompressedBytes = payload.decompressedSize;
                          active.compressionType = payload.compression;

                          if (worldPartition_ && worldPartition_->GetCell(coord)) {
                              worldPartition_->UpdateCellState(coord, CellLoadState::Decompressing);
                          }

                          const size_t payloadOffset = static_cast<size_t>(payload.payloadOffset);
                          const void* compressedInput = active.rawReadBuffer.data() + payloadOffset;
                          void* decompressedOutput = active.rawDecompressedBuffer.data();
                          const uint64_t expectedBytes = payload.decompressedSize;
                          const uint64_t decompressRequestId =
                              asyncIO_
                                  ? asyncIO_->SubmitDecompressRequest(
                                        compressedInput, payload.payloadSize, decompressedOutput,
                                        payload.decompressedSize, payload.compression,
                                        [this, coord, fileSize, expectedBytes](bool decompressSuccess,
                                                                               uint64_t decompressedBytes) {
                                            CellOpCompletion dc;
                                            dc.coord = coord;
                                            dc.isLoad = true;
                                            dc.packageBacked = false;
                                            dc.diskBytes = fileSize;
                                            dc.bytes = decompressedBytes;
                                            dc.success = decompressSuccess && decompressedBytes == expectedBytes;

                                            auto activeIt = activeLoadOperations_.find(coord);
                                            if (dc.success && activeIt != activeLoadOperations_.end()) {
                                                dc.rawCellData = std::move(activeIt->second.rawDecompressedBuffer);
                                            } else if (!dc.success) {
                                                dc.error = "AsyncIO decompression failed";
                                            } else {
                                                dc.success = false;
                                                dc.error =
                                                    "AsyncIO decompression completed after active load was removed";
                                            }

                                            std::lock_guard<std::mutex> lock(completionMutex_);
                                            completions_.push_back(std::move(dc));
                                        },
                                        ioPriority)
                                  : 0;

                          if (decompressRequestId == 0) {
                              c.success = false;
                              c.bytes = 0;
                              c.error = "Failed to submit async cell decompression";
                              pushCompletion(std::move(c));
                              return;
                          }

                          active.asyncRequestId = decompressRequestId;
                      },
                      ioPriority)
                : 0;

        if (asyncRequestId == 0) {
            NEXT_LOG_ERROR("Failed to submit async cell read: %s", filePathForLog.c_str());
            worldPartition_->UpdateCellState(request.coord, CellLoadState::Error);
            stats_.failedLoads++;
            return;
        }

        op.asyncRequestId = asyncRequestId;
        activeLoadOperations_[request.coord] = std::move(op);
        return;
    }

    auto& js = Next::JobSystem::Instance();
    op.job = js.Submit(
        [this, coord = request.coord, pkgName, pkgPath, filePath, fileSize, packageBacked]() {
            CellOpCompletion c;
            c.coord = coord;
            c.isLoad = true;
            c.packageBacked = packageBacked;
            c.packageName = pkgName;
            c.bytes = fileSize;
            c.diskBytes = fileSize;

            if (packageBacked) {
                c.success = Next::AssetManager::Instance().LoadPackage(pkgPath);
                if (!c.success) {
                    c.error = "LoadPackage failed";
                }
            } else {
                std::ifstream in(std::filesystem::path(filePath), std::ios::binary);
                if (!in.good()) {
                    c.success = false;
                    c.error = "Failed to open raw cell file";
                } else {
                    c.rawCellData.resize(static_cast<size_t>(fileSize));
                    in.read(reinterpret_cast<char*>(c.rawCellData.data()),
                            static_cast<std::streamsize>(c.rawCellData.size()));
                    c.success = in.good() || static_cast<uint64_t>(in.gcount()) == fileSize;
                    if (!c.success) {
                        c.rawCellData.clear();
                        c.error = "Failed to read raw cell file";
                    }
                }
            }

            std::lock_guard<std::mutex> lock(completionMutex_);
            completions_.push_back(std::move(c));
        },
        Next::JobPriority::High, {}, "CellLoadPackage");

    activeLoadOperations_[request.coord] = std::move(op);
}

void StreamingManager::ProcessCellUnload(const CellUnloadRequest& request) {
    CellData* cell = worldPartition_->GetCell(request.coord);
    if (!cell) {
        return;
    }

    // Cancel in-flight load (best-effort).
    auto itLoad = activeLoadOperations_.find(request.coord);
    if (itLoad != activeLoadOperations_.end()) {
        if (itLoad->second.asyncRequestId != 0 && asyncIO_) {
            asyncIO_->CancelRequest(itLoad->second.asyncRequestId);
        }
        if (itLoad->second.job.IsValid()) {
            Next::JobSystem::Instance().Cancel(itLoad->second.job);
        }
        activeLoadOperations_.erase(itLoad);
    }

    worldPartition_->RequestCellUnload(request.coord);

    // Only package-backed cells need AssetManager unload.
    std::string pkgName;
    auto itPkg = cellToPackageName_.find(request.coord);
    if (itPkg != cellToPackageName_.end()) {
        pkgName = itPkg->second;
    }

    auto& js = Next::JobSystem::Instance();
    Next::JobHandle job = js.Submit(
        [this, coord = request.coord, pkgName]() {
            if (!pkgName.empty()) {
                Next::AssetManager::Instance().UnloadPackage(pkgName);
            }

            CellOpCompletion c;
            c.coord = coord;
            c.isLoad = false;
            c.success = true;
            c.packageName = pkgName;

            std::lock_guard<std::mutex> lock(completionMutex_);
            completions_.push_back(std::move(c));
        },
        Next::JobPriority::Normal, {}, "CellUnloadPackage");

    activeUnloadOperations_[request.coord] = job;
}

std::wstring StreamingManager::GetCellFilePath(const CellCoord& coord) const {
    auto bundlePath = bundleCellPaths_.find(coord);
    if (bundlePath != bundleCellPaths_.end()) {
        return bundlePath->second;
    }

    auto indexedPath = availableCellPaths_.find(coord);
    if (indexedPath != availableCellPaths_.end()) {
        return indexedPath->second;
    }

    const std::filesystem::path root = ResolveRuntimePath(std::filesystem::path(config_.cellDataDirectory));
    return ResolveCellFilePath(root, coord, config_.cellFileExtension).wstring();
}

void StreamingManager::OnCellLoadComplete(const CellCoord& coord, bool success, uint64_t bytesProcessed) {
    (void)bytesProcessed;
    // Update cell state
    if (success) {
        worldPartition_->UpdateCellState(coord, CellLoadState::Loaded);
    } else {
        worldPartition_->UpdateCellState(coord, CellLoadState::Error);
        stats_.failedLoads++;
    }

    activeLoadOperations_.erase(coord);
}

void StreamingManager::OnCellLoadFailed(const CellCoord& coord, const std::string& error) {
    NEXT_LOG_ERROR("Cell load failed: [%d, %d] - %s", coord.x, coord.z, error.c_str());
    worldPartition_->UpdateCellState(coord, CellLoadState::Error);
    stats_.failedLoads++;
    activeLoadOperations_.erase(coord);
}

void StreamingManager::LoadCellLayers(CellData* cell, const std::vector<CellLayer>& layers) {
    if (!cell) {
        return;
    }
    if (layers.empty()) {
        return;
    }

    for (CellLayer layer : layers) {
        LoadCellLayer(cell->coord, layer, cell->priority);
    }
}

void StreamingManager::UnloadCellLayers(CellData* cell, const std::vector<CellLayer>& layers) {
    if (!cell) {
        return;
    }
    if (layers.empty()) {
        return;
    }

    for (CellLayer layer : layers) {
        UnloadCellLayer(cell->coord, layer);
    }
}

bool StreamingManager::CheckMemoryBudget() const {
    return GetMemoryUtilization() < config_.evictionThreshold;
}

void StreamingManager::EnforceMemoryBudget() {
    if (CheckMemoryBudget()) {
        return;
    }

    const size_t budget = GetMemoryBudget();
    const size_t usage = GetMemoryUsage();
    if (budget == 0 || usage <= budget) {
        return;
    }

    // Free enough memory to get under a target utilization (hysteresis prevents thrashing).
    const float targetUtil = std::max(0.5f, config_.evictionThreshold - 0.1f);
    const size_t targetUsage = static_cast<size_t>(static_cast<double>(budget) * static_cast<double>(targetUtil));
    const size_t targetFree = (usage > targetUsage) ? (usage - targetUsage) : 0;
    if (targetFree == 0) {
        return;
    }

    // Use eviction policy to select cells to unload (non-owning pointers).
    std::unordered_map<CellCoord, const CellData*, CellCoord::Hash> loaded;
    for (const CellCoord& coord : worldPartition_->GetLoadedCells()) {
        if (const CellData* cell = worldPartition_->GetCell(coord)) {
            loaded.emplace(coord, cell);
        }
    }

    std::vector<EvictionCandidate> candidates =
        evictionPolicy_->SelectEvictionCandidates(loaded, targetFree, config_.maxConcurrentUnloads);

    if (config_.logStreamingEvents) {
        NEXT_LOG_INFO("EnforceMemoryBudget: usage=%.2fMB budget=%.2fMB targetFree=%.2fMB loaded=%zu candidates=%zu",
                      static_cast<double>(usage) / (1024.0 * 1024.0), static_cast<double>(budget) / (1024.0 * 1024.0),
                      static_cast<double>(targetFree) / (1024.0 * 1024.0), loaded.size(), candidates.size());
    }

    // Evict selected cells
    uint64_t memoryFreed = 0;
    float scoreTotal = 0.0f;
    for (const auto& candidate : candidates) {
        memoryFreed += candidate.memorySize;
        scoreTotal += candidate.score;
        UnloadCell(candidate.coord);
    }
    if (!candidates.empty()) {
        evictionPolicy_->RecordEvictionBatch(static_cast<uint32_t>(candidates.size()), memoryFreed,
                                             scoreTotal / static_cast<float>(candidates.size()));
    }

    // Apply unloads immediately so utilization reflects the new state within the same frame.
    ProcessUnloadQueue();
}

void StreamingManager::UpdateMemoryStatistics() {
    stats_.memoryUsed = GetMemoryUsage();
    stats_.memoryBudget = GetMemoryBudget();
    stats_.memoryUtilization = GetMemoryUtilization();

    evictionPolicy_->SetMemoryBudget(stats_.memoryBudget);
    evictionPolicy_->SetMemoryUsage(stats_.memoryUsed);
    evictionPolicy_->SetCurrentCellCount(stats_.loadedCells);
}

float StreamingManager::CalculateCellPriority(const CellCoord& coord, const Vec3& cameraPosition,
                                              const Vec3& cameraDirection) const {
    // Higher = more important to load.
    Vec3 cellPos = worldPartition_->CellToWorld(coord);
    Vec3 toCell = cellPos - cameraPosition;
    const float distance = toCell.Length();

    float distancePriority = 1.0f;
    if (config_.loadRadius > 1e-3f) {
        distancePriority = 1.0f - (distance / config_.loadRadius);
    }
    distancePriority = std::max(0.0f, distancePriority);

    float directionPriority = 0.0f;
    if (distance > 1e-3f) {
        Vec3 dirNorm = toCell * (1.0f / distance);
        const float dot = std::max(-1.0f, std::min(1.0f, dirNorm.Dot(cameraDirection)));
        directionPriority = (dot + 1.0f) * 0.5f;  // [0..1]
    }

    float basePriority = distancePriority + (directionPriority * 0.5f);

    // Apply priority override if set
    if (priorityOverride_) {
        basePriority = priorityOverride_(coord, basePriority);
    }

    return basePriority;
}

float StreamingManager::CalculateLayerPriority(CellLayer layer) const {
    // Pull the per-layer priority multiplier from the WorldPartition config so
    // gameplay-critical layers (terrain, navmesh, collision) outrank cosmetic
    // layers (vegetation, audio) when budget gets tight.
    const auto layerIndex = static_cast<size_t>(layer);
    if (worldPartition_) {
        const WorldPartitionConfig& wpConfig = worldPartition_->GetConfig();
        if (layerIndex < wpConfig.layerPriority.size()) {
            return wpConfig.layerPriority[layerIndex];
        }
    }

    // Fallback table mirrors the WorldPartitionConfig defaults so callers get
    // sensible behavior even if the world partition isn't initialized yet.
    switch (layer) {
        case CellLayer::Terrain:
            return 1.0f;
        case CellLayer::StaticMesh:
            return 0.9f;
        case CellLayer::Collision:
            return 0.9f;
        case CellLayer::HLOD:
            return 0.8f;
        case CellLayer::NavMesh:
            return 0.7f;
        case CellLayer::Dynamic:
            return 0.6f;
        case CellLayer::Vegetation:
            return 0.5f;
        case CellLayer::Props:
            return 0.4f;
        case CellLayer::Audio:
            return 0.3f;
        case CellLayer::Quest:
            return 0.2f;
        default:
            return 0.5f;
    }
}

void StreamingManager::UpdateStatistics(float deltaTime) {
    (void)deltaTime;
    // Update basic statistics
    stats_.loadedCells = 0;
    stats_.loadingCells = 0;
    stats_.queuedCells = static_cast<uint32_t>(loadQueue_.size());
    stats_.placeholderCells = 0;

    auto loadedCells = worldPartition_->GetLoadedCells();
    stats_.loadedCells = static_cast<uint32_t>(loadedCells.size());
    stats_.loadingCells = static_cast<uint32_t>(activeLoadOperations_.size());
    for (const CellCoord& coord : loadedCells) {
        const CellData* cell = worldPartition_->GetCell(coord);
        if (cell && cell->IsPlaceholder()) {
            stats_.placeholderCells++;
        }
    }

    // Update memory statistics
    UpdateMemoryStatistics();

    // Pull LOD bucket counts from the LODSystem so streaming consumers can see
    // detail/HLOD/impostor distribution alongside cell residency stats.
    if (lodSystem_) {
        const auto lodStats = lodSystem_->GetStatistics();
        stats_.highDetailCells = lodStats.highDetailObjects;
        stats_.lowDetailCells = lodStats.mediumDetailObjects + lodStats.lowDetailObjects;
        stats_.hlodCells = lodStats.hlodObjects;
        // Anything currently tracked by the LOD system is by definition visible
        // (it gets registered when a cell is loaded into view).
        stats_.visibleCells = lodStats.highDetailObjects + lodStats.mediumDetailObjects + lodStats.lowDetailObjects +
                              lodStats.hlodObjects + lodStats.impostorObjects;
    }
}

}  // namespace Streaming
}  // namespace Next
