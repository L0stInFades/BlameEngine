#include <gtest/gtest.h>

#include "next/compression/compression.h"
#include "next/streaming/cell_file_format.h"
#include "next/streaming/streaming_manager.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

namespace {

struct CellCoordHash {
    size_t operator()(const Next::Streaming::CellCoord& c) const noexcept {
        return Next::Streaming::CellCoord::Hash{}(c);
    }
};

static void WriteCellFile(const std::filesystem::path& dir, int32_t x, int32_t z, size_t sizeBytes) {
    std::filesystem::create_directories(dir);
    const std::filesystem::path p = dir / ("cell_" + std::to_string(x) + "_" + std::to_string(z) + ".ncell");
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(out.good());
    std::string blob(sizeBytes, '\0');
    for (size_t i = 0; i < blob.size(); ++i) {
        blob[i] = static_cast<char>((i + static_cast<size_t>(x) * 131u + static_cast<size_t>(z) * 17u) & 0xFF);
    }
    out.write(blob.data(), static_cast<std::streamsize>(blob.size()));
    out.close();
    ASSERT_TRUE(std::filesystem::exists(p));
}

static std::filesystem::path CellFilePath(const std::filesystem::path& dir, int32_t x, int32_t z) {
    return dir / ("cell_" + std::to_string(x) + "_" + std::to_string(z) + ".ncell");
}

static void WriteCompressedCellFile(
    const std::filesystem::path& dir,
    int32_t x,
    int32_t z,
    const std::vector<uint8_t>& payload,
    Next::Compression::Algorithm algorithm,
    Next::Streaming::CompressionType compressionType) {
    ASSERT_FALSE(payload.empty());
    ASSERT_TRUE(Next::Compression::IsAvailable(algorithm));

    const uint64_t bound = Next::Compression::CompressBound(algorithm, payload.size());
    ASSERT_GT(bound, 0u);
    std::vector<uint8_t> compressed(static_cast<size_t>(bound));
    const Next::Compression::Result result = Next::Compression::Compress(
        algorithm,
        payload.data(),
        payload.size(),
        compressed.data(),
        compressed.size());
    ASSERT_TRUE(result.Succeeded()) << result.message;
    compressed.resize(static_cast<size_t>(result.bytesWritten));

    const Next::Streaming::CellFileHeader header = Next::Streaming::MakeCellFileHeader(
        static_cast<Next::Streaming::CellFileCompression>(compressionType),
        compressed.size(),
        payload.size());

    std::filesystem::create_directories(dir);
    const std::filesystem::path p = CellFilePath(dir, x, z);
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(out.good());
    out.write(reinterpret_cast<const char*>(&header), static_cast<std::streamsize>(sizeof(header)));
    out.write(reinterpret_cast<const char*>(compressed.data()), static_cast<std::streamsize>(compressed.size()));
    out.close();
    ASSERT_TRUE(std::filesystem::exists(p));
}

static void WriteDeclaredCompressedCellFile(
    const std::filesystem::path& dir,
    int32_t x,
    int32_t z,
    Next::Streaming::CompressionType compressionType,
    uint64_t compressedSize,
    uint64_t decompressedSize) {
    ASSERT_GT(compressedSize, 0u);
    ASSERT_GT(decompressedSize, 0u);

    const Next::Streaming::CellFileHeader header = Next::Streaming::MakeCellFileHeader(
        static_cast<Next::Streaming::CellFileCompression>(compressionType),
        compressedSize,
        decompressedSize);

    std::filesystem::create_directories(dir);
    const std::filesystem::path p = CellFilePath(dir, x, z);
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(out.good());
    out.write(reinterpret_cast<const char*>(&header), static_cast<std::streamsize>(sizeof(header)));
    std::vector<uint8_t> payload(static_cast<size_t>(compressedSize), 0x7du);
    out.write(reinterpret_cast<const char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
    out.close();
    ASSERT_TRUE(std::filesystem::exists(p));
}

static void ExpectAsyncDecompressionRoundTrip(
    Next::Compression::Algorithm algorithm,
    Next::Streaming::CompressionType compressionType) {
    using namespace Next::Streaming;

    if (!Next::Compression::IsAvailable(algorithm)) {
        GTEST_SKIP() << Next::Compression::AlgorithmName(algorithm) << " backend is not available";
    }

    std::array<uint8_t, 128> input = {};
    for (size_t i = 0; i < input.size(); ++i) {
        input[i] = static_cast<uint8_t>((i * 17u + 23u) & 0xffu);
    }

    const uint64_t bound = Next::Compression::CompressBound(algorithm, input.size());
    ASSERT_GT(bound, 0u);
    std::vector<uint8_t> compressed(static_cast<size_t>(bound));
    const Next::Compression::Result compressResult = Next::Compression::Compress(
        algorithm,
        input.data(),
        input.size(),
        compressed.data(),
        compressed.size());
    ASSERT_TRUE(compressResult.Succeeded()) << compressResult.message;
    compressed.resize(static_cast<size_t>(compressResult.bytesWritten));

    AsyncIOSystem io;
    AsyncIOConfig cfg;
    cfg.ioThreads = 0;
    cfg.decompressionThreads = 1;
    ASSERT_TRUE(io.Initialize(cfg));

    std::array<uint8_t, 128> output = {};
    bool callbackCalled = false;
    bool callbackSuccess = false;
    uint64_t callbackBytes = 0;

    const uint64_t request = io.SubmitDecompressRequest(
        compressed.data(),
        compressed.size(),
        output.data(),
        output.size(),
        compressionType,
        [&](bool success, uint64_t bytesProcessed) {
            callbackCalled = true;
            callbackSuccess = success;
            callbackBytes = bytesProcessed;
        });

    ASSERT_NE(request, 0u);
    for (int i = 0; i < 200 && !callbackCalled; ++i) {
        io.Update();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    EXPECT_TRUE(callbackCalled);
    EXPECT_TRUE(callbackSuccess);
    EXPECT_EQ(callbackBytes, input.size());
    EXPECT_EQ(std::memcmp(output.data(), input.data(), input.size()), 0);

    const IOStatistics stats = io.GetStatistics();
    EXPECT_EQ(stats.totalBytesDecompressed, input.size());
    EXPECT_TRUE(stats.HasDecompressedBytes());
    EXPECT_FALSE(stats.HasFailures());
    EXPECT_EQ(stats.PendingOperationCount(), 0u);

    io.Shutdown();
}

static void ExpectStreamingManagerLoadsCompressedCell(
    Next::Compression::Algorithm algorithm,
    Next::Streaming::CompressionType compressionType) {
    using namespace Next::Streaming;
    using Next::Vec3;

    if (!Next::Compression::IsAvailable(algorithm)) {
        GTEST_SKIP() << Next::Compression::AlgorithmName(algorithm) << " backend is not available";
    }

    const CellCoord coord{0, 0};
    const std::string suffix = Next::Compression::AlgorithmName(algorithm);
    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / ("next_world_streaming_compressed_cell_" + suffix);
    std::filesystem::remove_all(tmp);

    std::vector<uint8_t> payload(16 * 1024);
    for (size_t i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<uint8_t>((i / 64u) & 0x0fu);
    }

    WriteCompressedCellFile(tmp, coord.x, coord.z, payload, algorithm, compressionType);
    const std::filesystem::path path = CellFilePath(tmp, coord.x, coord.z);
    const uint64_t diskBytes = static_cast<uint64_t>(std::filesystem::file_size(path));
    ASSERT_LT(diskBytes, payload.size());

    StreamingManager mgr;
    StreamingManagerConfig cfg;
    cfg.memoryBudgetMB = 64;
    cfg.loadRadius = 0.0f;
    cfg.unloadRadius = 128.0f;
    cfg.maxConcurrentLoads = 4;
    cfg.maxConcurrentUnloads = 4;
    cfg.enablePrediction = false;
    cfg.cellDataDirectory = tmp.wstring();
    cfg.allowPlaceholderCellLoad = false;

    ASSERT_TRUE(mgr.Initialize(cfg));
    mgr.LoadCell(coord, 1.0f);

    for (int i = 0; i < 200 && !mgr.IsCellLoaded(coord); ++i) {
        mgr.Update(0.016f, Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, -1.0f), Vec3(0.0f, 0.0f, 0.0f));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    ASSERT_TRUE(mgr.IsCellLoaded(coord));

    StreamingCellInfo info;
    ASSERT_TRUE(mgr.GetCellInfo(coord, info));
    EXPECT_TRUE(info.IsLoaded());
    EXPECT_TRUE(info.HasDiskData());
    EXPECT_TRUE(info.HasMemoryData());
    EXPECT_EQ(info.dataSize, diskBytes);
    EXPECT_EQ(info.memorySize, payload.size());
    EXPECT_GT(info.MemoryToDiskRatio(), 1.0f);
    EXPECT_TRUE(info.HasLayer(CellLayer::StaticMesh));

    CellData* cell = mgr.GetCell(coord);
    ASSERT_NE(cell, nullptr);
    auto layerIt = cell->layers.find(CellLayer::StaticMesh);
    ASSERT_TRUE(layerIt != cell->layers.end());
    ASSERT_TRUE(layerIt->second.HasData());
    EXPECT_EQ(layerIt->second.size, payload.size());
    EXPECT_EQ(std::memcmp(layerIt->second.data, payload.data(), payload.size()), 0);

    const IOStatistics ioStats = mgr.GetIOStatistics();
    EXPECT_GE(ioStats.totalBytesRead, diskBytes);
    EXPECT_GE(ioStats.totalBytesDecompressed, payload.size());
    EXPECT_TRUE(ioStats.HasReadBytes());
    EXPECT_TRUE(ioStats.HasDecompressedBytes());
    EXPECT_FALSE(ioStats.HasFailures());

    mgr.Shutdown();
    std::filesystem::remove_all(tmp);
}

}  // namespace

TEST(WorldStreaming, StreamingHandleDefaultsInvalidAndResets) {
    using namespace Next::Streaming;

    StreamingHandle handle;
    EXPECT_FALSE(handle.IsValid());
    EXPECT_FALSE(static_cast<bool>(handle));
    EXPECT_EQ(handle, StreamingHandle::Invalid());

    StreamingHandle valid{42};
    EXPECT_TRUE(valid.IsValid());
    EXPECT_TRUE(static_cast<bool>(valid));
    EXPECT_NE(valid, handle);

    valid.Reset();
    EXPECT_FALSE(valid.IsValid());
    EXPECT_EQ(valid, StreamingHandle::Invalid());
}

TEST(WorldStreaming, AssetBundleHelpersExposeMetadata) {
    using namespace Next::Streaming;

    AssetBundle bundle;
    EXPECT_FALSE(bundle.IsValid());
    EXPECT_FALSE(bundle.HasPath());
    EXPECT_FALSE(bundle.HasCells());
    EXPECT_FALSE(bundle.HasDependencies());
    EXPECT_FALSE(bundle.HasSize());
    EXPECT_EQ(bundle.CellCount(), 0u);
    EXPECT_FLOAT_EQ(bundle.CompressionRatio(), 0.0f);

    bundle.bundleId = 7;
    bundle.bundlePath = L"bundle";
    bundle.cells.push_back(CellCoord{1, 2});
    bundle.dependencyBundles.push_back(3);
    bundle.totalSize = 100;
    bundle.compressedSize = 40;

    EXPECT_TRUE(bundle.IsValid());
    EXPECT_TRUE(bundle.HasPath());
    EXPECT_TRUE(bundle.HasCells());
    EXPECT_TRUE(bundle.HasDependencies());
    EXPECT_TRUE(bundle.HasSize());
    EXPECT_EQ(bundle.CellCount(), 1u);
    EXPECT_FLOAT_EQ(bundle.CompressionRatio(), 0.4f);
}

TEST(WorldStreaming, CellMetadataHelpersExposeLayersSizesAndRatios) {
    using namespace Next::Streaming;

    CellMetadata metadata;
    EXPECT_FALSE(metadata.HasDiskData());
    EXPECT_FALSE(metadata.HasMemoryData());
    EXPECT_FALSE(metadata.HasGeometryStats());
    EXPECT_FALSE(metadata.HasLodLevels());
    EXPECT_FALSE(metadata.HasAnyLayer());
    EXPECT_FALSE(metadata.HasLayer(CellLayer::StaticMesh));
    EXPECT_EQ(metadata.LayerCount(), 0u);
    EXPECT_FLOAT_EQ(metadata.MemoryToDiskRatio(), 0.0f);
    EXPECT_FLOAT_EQ(metadata.ClampedCellSize(), 64.0f);

    metadata.cellSize = 0.25f;
    metadata.dataSize = 100;
    metadata.memorySize = 250;
    metadata.triangleCount = 12;
    metadata.lodLevels = 3;
    metadata.SetLayerPresent(CellLayer::StaticMesh);
    metadata.SetLayerPresent(CellLayer::Audio);

    EXPECT_TRUE(metadata.HasDiskData());
    EXPECT_TRUE(metadata.HasMemoryData());
    EXPECT_TRUE(metadata.HasGeometryStats());
    EXPECT_TRUE(metadata.HasLodLevels());
    EXPECT_TRUE(metadata.HasAnyLayer());
    EXPECT_TRUE(metadata.HasLayer(CellLayer::StaticMesh));
    EXPECT_TRUE(metadata.HasLayer(CellLayer::Audio));
    EXPECT_EQ(metadata.LayerCount(), 2u);
    EXPECT_FLOAT_EQ(metadata.MemoryToDiskRatio(), 2.5f);
    EXPECT_FLOAT_EQ(metadata.ClampedCellSize(), 1.0f);
    EXPECT_FLOAT_EQ(metadata.ClampedCellSize(2.0f), 2.0f);

    metadata.SetLayerPresent(CellLayer::Audio, false);
    EXPECT_FALSE(metadata.HasLayer(CellLayer::Audio));
    EXPECT_EQ(metadata.LayerCount(), 1u);
}

TEST(WorldStreaming, CellDataHelpersExposeStateLayersAndMemory) {
    using namespace Next::Streaming;

    CellData cell;
    EXPECT_FALSE(cell.IsLoaded());
    EXPECT_FALSE(cell.IsPending());
    EXPECT_FALSE(cell.IsUnloading());
    EXPECT_FALSE(cell.IsError());
    EXPECT_FALSE(cell.IsPlaceholder());
    EXPECT_FALSE(cell.HasLayers());
    EXPECT_FALSE(cell.HasLoadedLayers());
    EXPECT_FALSE(cell.HasDependencies());
    EXPECT_FALSE(cell.HasPriority());
    EXPECT_FALSE(cell.HasAsyncOperation());
    EXPECT_FALSE(cell.HasMemoryData());
    EXPECT_FALSE(cell.HasDiskData());
    EXPECT_EQ(cell.LayerCount(), 0u);
    EXPECT_EQ(cell.LoadedLayerCount(), 0u);
    EXPECT_EQ(cell.MemorySize(), 0u);
    EXPECT_EQ(cell.DiskDataSize(), 0u);

    cell.state = CellLoadState::Queued;
    EXPECT_TRUE(cell.IsPending());

    uint8_t dummy = 0;
    CellData::LayerData pendingLayer;
    pendingLayer.layer = CellLayer::Terrain;
    pendingLayer.state = CellLoadState::Loading;
    EXPECT_TRUE(pendingLayer.IsPending());
    EXPECT_FALSE(pendingLayer.IsLoaded());
    cell.layers[CellLayer::Terrain] = pendingLayer;

    CellData::LayerData loadedLayer;
    loadedLayer.layer = CellLayer::StaticMesh;
    loadedLayer.data = &dummy;
    loadedLayer.size = 42;
    loadedLayer.state = CellLoadState::Loaded;
    loadedLayer.gpuResourceHandle = 99;
    EXPECT_TRUE(loadedLayer.IsLoaded());
    EXPECT_TRUE(loadedLayer.HasData());
    EXPECT_TRUE(loadedLayer.HasSize());
    EXPECT_TRUE(loadedLayer.HasGpuResource());
    cell.layers[CellLayer::StaticMesh] = loadedLayer;

    cell.state = CellLoadState::Loaded;
    cell.metadata.dataSize = 128;
    cell.metadata.memorySize = 256;
    cell.dependencies.push_back(CellCoord{1, 0});
    cell.priority = 4.0f;
    cell.asyncOperationHandle = 7;
    cell.isPlaceholderData = true;

    EXPECT_TRUE(cell.IsLoaded());
    EXPECT_TRUE(cell.IsPlaceholder());
    EXPECT_TRUE(cell.HasLayers());
    EXPECT_TRUE(cell.HasLoadedLayers());
    EXPECT_TRUE(cell.HasDependencies());
    EXPECT_TRUE(cell.HasPriority());
    EXPECT_TRUE(cell.HasAsyncOperation());
    EXPECT_TRUE(cell.HasMemoryData());
    EXPECT_TRUE(cell.HasDiskData());
    EXPECT_EQ(cell.LayerCount(), 2u);
    EXPECT_EQ(cell.LoadedLayerCount(), 1u);
    EXPECT_FALSE(cell.IsFullyLoaded());
    EXPECT_EQ(cell.MemorySize(), 256u);
    EXPECT_EQ(cell.DiskDataSize(), 128u);

    cell.layers[CellLayer::Terrain].state = CellLoadState::Loaded;
    EXPECT_TRUE(cell.IsFullyLoaded());
}

TEST(WorldStreaming, StreamingCellInfoHelpersExposeSnapshotState) {
    using namespace Next::Streaming;

    StreamingCellInfo info;
    EXPECT_FALSE(info.IsLoaded());
    EXPECT_FALSE(info.IsPending());
    EXPECT_FALSE(info.IsUnloading());
    EXPECT_FALSE(info.IsError());
    EXPECT_FALSE(info.IsPlaceholder());
    EXPECT_FALSE(info.HasLayers());
    EXPECT_FALSE(info.HasLoadedLayers());
    EXPECT_FALSE(info.HasDiskData());
    EXPECT_FALSE(info.HasMemoryData());
    EXPECT_FALSE(info.HasPriority());
    EXPECT_FALSE(info.HasAsyncOperation());
    EXPECT_FALSE(info.HasLayer(CellLayer::StaticMesh));
    EXPECT_FLOAT_EQ(info.MemoryToDiskRatio(), 0.0f);
    EXPECT_FLOAT_EQ(info.ClampedCellSize(), 1.0f);

    info.state = CellLoadState::Loading;
    EXPECT_TRUE(info.IsPending());

    info.state = CellLoadState::Loaded;
    info.cellSize = 0.5f;
    info.layerMask = CellMetadata::LayerBit(CellLayer::StaticMesh);
    info.layerCount = 1;
    info.loadedLayerCount = 1;
    info.dataSize = 64;
    info.memorySize = 128;
    info.priority = 2.0f;
    info.asyncOperationHandle = 3;
    info.placeholder = true;

    EXPECT_TRUE(info.IsLoaded());
    EXPECT_TRUE(info.IsPlaceholder());
    EXPECT_TRUE(info.HasLayers());
    EXPECT_TRUE(info.HasLoadedLayers());
    EXPECT_TRUE(info.HasLayer(CellLayer::StaticMesh));
    EXPECT_TRUE(info.HasDiskData());
    EXPECT_TRUE(info.HasMemoryData());
    EXPECT_TRUE(info.HasPriority());
    EXPECT_TRUE(info.HasAsyncOperation());
    EXPECT_FLOAT_EQ(info.MemoryToDiskRatio(), 2.0f);
    EXPECT_FLOAT_EQ(info.ClampedCellSize(), 1.0f);
    EXPECT_FLOAT_EQ(info.ClampedCellSize(2.0f), 2.0f);
}

TEST(WorldStreaming, StreamingStatisticsHelpersExposeResidencyMemoryAndErrors) {
    using namespace Next::Streaming;

    StreamingStatistics stats;
    EXPECT_EQ(stats.PendingCellCount(), 0u);
    EXPECT_EQ(stats.ActiveCellCount(), 0u);
    EXPECT_FALSE(stats.HasLoadedCells());
    EXPECT_FALSE(stats.HasPendingCells());
    EXPECT_FALSE(stats.HasActiveCells());
    EXPECT_FALSE(stats.HasVisibleCells());
    EXPECT_FALSE(stats.HasMemoryBudget());
    EXPECT_FALSE(stats.HasMemoryUsage());
    EXPECT_FALSE(stats.IsOverMemoryBudget());
    EXPECT_FALSE(stats.HasFailures());
    EXPECT_FALSE(stats.HasPlaceholderCells());

    stats.loadedCells = 3;
    stats.loadingCells = 2;
    stats.queuedCells = 1;
    stats.visibleCells = 2;
    stats.memoryUsed = 120;
    stats.memoryBudget = 100;
    stats.memoryUtilization = 1.2f;
    stats.failedLoads = 1;
    stats.placeholderCells = 1;

    EXPECT_EQ(stats.PendingCellCount(), 3u);
    EXPECT_EQ(stats.ActiveCellCount(), 6u);
    EXPECT_TRUE(stats.HasLoadedCells());
    EXPECT_TRUE(stats.HasPendingCells());
    EXPECT_TRUE(stats.HasActiveCells());
    EXPECT_TRUE(stats.HasVisibleCells());
    EXPECT_TRUE(stats.HasMemoryBudget());
    EXPECT_TRUE(stats.HasMemoryUsage());
    EXPECT_TRUE(stats.IsOverMemoryBudget());
    EXPECT_TRUE(stats.HasFailures());
    EXPECT_TRUE(stats.HasPlaceholderCells());
}

TEST(WorldStreaming, IOStatisticsHelpersExposeBytesQueuesAndFailures) {
    using namespace Next::Streaming;

    IOStatistics stats;
    EXPECT_EQ(stats.TotalBytesProcessed(), 0u);
    EXPECT_EQ(stats.PendingOperationCount(), 0u);
    EXPECT_FALSE(stats.HasReadBytes());
    EXPECT_FALSE(stats.HasWrittenBytes());
    EXPECT_FALSE(stats.HasDecompressedBytes());
    EXPECT_FALSE(stats.HasProcessedBytes());
    EXPECT_FALSE(stats.HasPendingReads());
    EXPECT_FALSE(stats.HasPendingWrites());
    EXPECT_FALSE(stats.HasPendingDecompressions());
    EXPECT_FALSE(stats.HasPendingOperations());
    EXPECT_FALSE(stats.HasThroughput());
    EXPECT_FALSE(stats.HasTiming());
    EXPECT_FALSE(stats.HasFailures());

    stats.totalBytesRead = 100;
    stats.totalBytesWritten = 20;
    stats.totalBytesDecompressed = 30;
    stats.pendingReads = 1;
    stats.pendingWrites = 2;
    stats.pendingDecompressions = 3;
    stats.averageReadSpeedMBps = 10.0f;
    stats.averageDecompressSpeedMBps = 30.0f;
    stats.averageReadTime = 0.5f;
    stats.averageDecompressTime = 0.75f;
    stats.failedOperations = 1;

    EXPECT_EQ(stats.TotalBytesProcessed(), 150u);
    EXPECT_EQ(stats.PendingOperationCount(), 6u);
    EXPECT_TRUE(stats.HasReadBytes());
    EXPECT_TRUE(stats.HasWrittenBytes());
    EXPECT_TRUE(stats.HasDecompressedBytes());
    EXPECT_TRUE(stats.HasProcessedBytes());
    EXPECT_TRUE(stats.HasPendingReads());
    EXPECT_TRUE(stats.HasPendingWrites());
    EXPECT_TRUE(stats.HasPendingDecompressions());
    EXPECT_TRUE(stats.HasPendingOperations());
    EXPECT_TRUE(stats.HasReadThroughput());
    EXPECT_FALSE(stats.HasWriteThroughput());
    EXPECT_TRUE(stats.HasDecompressThroughput());
    EXPECT_TRUE(stats.HasThroughput());
    EXPECT_TRUE(stats.HasReadTiming());
    EXPECT_FALSE(stats.HasWriteTiming());
    EXPECT_TRUE(stats.HasDecompressTiming());
    EXPECT_TRUE(stats.HasTiming());
    EXPECT_TRUE(stats.HasFailures());
}

TEST(WorldStreaming, StreamingMemoryPoolRejectsInvalidInitializationRequests) {
    using namespace Next::Streaming;

    StreamingMemoryPool pool;
    EXPECT_FALSE(pool.Initialize(0, 4));
    EXPECT_FALSE(pool.Initialize(128, 0));
    EXPECT_EQ(pool.GetTotalSize(), 0u);
    EXPECT_EQ(pool.GetAllocationCount(), 0u);

    ASSERT_TRUE(pool.Initialize(128, 4));
    void* allocation = pool.Allocate(16, 16);
    EXPECT_NE(allocation, nullptr);
    pool.Free(allocation);
    pool.Shutdown();
}

TEST(WorldStreaming, StreamingMemoryPoolHonorsAlignmentAndReclaimsTopPadding) {
    using namespace Next::Streaming;

    StreamingMemoryPool pool;
    ASSERT_TRUE(pool.Initialize(256, 4));

    void* first = pool.Allocate(3, 1);
    ASSERT_NE(first, nullptr);
    const size_t usedAfterFirst = pool.GetUsedSize();
    EXPECT_EQ(usedAfterFirst, 3u);
    EXPECT_EQ(pool.GetAllocationCount(), 1u);

    void* second = pool.Allocate(16, 64);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(second) % 64u, 0u);
    EXPECT_GE(reinterpret_cast<std::uintptr_t>(second),
              reinterpret_cast<std::uintptr_t>(first) + 3u);
    EXPECT_EQ(pool.GetAllocationCount(), 2u);

    pool.Free(second);
    EXPECT_EQ(pool.GetUsedSize(), usedAfterFirst);
    EXPECT_EQ(pool.GetAllocationCount(), 1u);

    pool.Free(first);
    EXPECT_EQ(pool.GetUsedSize(), 0u);
    EXPECT_EQ(pool.GetAllocationCount(), 0u);

    pool.Shutdown();
}

TEST(WorldStreaming, StreamingMemoryPoolRejectsInvalidAllocationRequests) {
    using namespace Next::Streaming;

    StreamingMemoryPool pool;
    ASSERT_TRUE(pool.Initialize(128, 4));

    EXPECT_EQ(pool.Allocate(0, 16), nullptr);
    EXPECT_EQ(pool.Allocate(8, 0), nullptr);
    EXPECT_EQ(pool.Allocate(8, 3), nullptr);
    EXPECT_EQ(pool.GetUsedSize(), 0u);
    EXPECT_EQ(pool.GetAllocationCount(), 0u);

    pool.Shutdown();
}

TEST(WorldStreaming, StreamingMemoryPoolRejectsOverflowedAllocationRequests) {
    using namespace Next::Streaming;

    StreamingMemoryPool pool;
    ASSERT_TRUE(pool.Initialize(128, 4));

    EXPECT_EQ(pool.Allocate(std::numeric_limits<size_t>::max(), 16), nullptr);
    EXPECT_EQ(pool.Allocate(std::numeric_limits<size_t>::max() - 8, 16), nullptr);
    EXPECT_EQ(pool.GetUsedSize(), 0u);
    EXPECT_EQ(pool.GetAllocationCount(), 0u);

    pool.Shutdown();
}

TEST(WorldStreaming, WorldPartitionStatisticsHelpersExposeQueuesAndMemory) {
    using namespace Next::Streaming;

    WorldPartition::Statistics stats;
    EXPECT_EQ(stats.PendingCellCount(), 0u);
    EXPECT_EQ(stats.ActiveCellCount(), 0u);
    EXPECT_FALSE(stats.HasLoadedCells());
    EXPECT_FALSE(stats.HasQueuedCells());
    EXPECT_FALSE(stats.HasLoadingCells());
    EXPECT_FALSE(stats.HasPendingCells());
    EXPECT_FALSE(stats.HasActiveCells());
    EXPECT_FALSE(stats.HasTrackedCells());
    EXPECT_FALSE(stats.HasMemoryUsage());
    EXPECT_FALSE(stats.HasTiming());

    stats.loadedCells = 2;
    stats.queuedCells = 3;
    stats.loadingCells = 4;
    stats.totalCells = 10;
    stats.memoryUsageMB = 5;
    stats.averageLoadTime = 0.5f;

    EXPECT_EQ(stats.PendingCellCount(), 7u);
    EXPECT_EQ(stats.ActiveCellCount(), 9u);
    EXPECT_TRUE(stats.HasLoadedCells());
    EXPECT_TRUE(stats.HasQueuedCells());
    EXPECT_TRUE(stats.HasLoadingCells());
    EXPECT_TRUE(stats.HasPendingCells());
    EXPECT_TRUE(stats.HasActiveCells());
    EXPECT_TRUE(stats.HasTrackedCells());
    EXPECT_TRUE(stats.HasMemoryUsage());
    EXPECT_TRUE(stats.HasAverageLoadTime());
    EXPECT_FALSE(stats.HasAverageUnloadTime());
    EXPECT_TRUE(stats.HasTiming());
}

TEST(WorldStreaming, InterestStatisticsHelpersExposePointsAndCells) {
    using namespace Next::Streaming;

    InterestManager::Statistics stats;
    EXPECT_EQ(stats.InterestCellCount(), 0u);
    EXPECT_FALSE(stats.HasActiveInterestPoints());
    EXPECT_FALSE(stats.HasHighInterestCells());
    EXPECT_FALSE(stats.HasMediumInterestCells());
    EXPECT_FALSE(stats.HasLowInterestCells());
    EXPECT_FALSE(stats.HasInterestCells());

    stats.activeInterestPoints = 2;
    stats.highInterestCells = 3;
    stats.mediumInterestCells = 4;
    stats.lowInterestCells = 5;

    EXPECT_EQ(stats.InterestCellCount(), 12u);
    EXPECT_TRUE(stats.HasActiveInterestPoints());
    EXPECT_TRUE(stats.HasHighInterestCells());
    EXPECT_TRUE(stats.HasMediumInterestCells());
    EXPECT_TRUE(stats.HasLowInterestCells());
    EXPECT_TRUE(stats.HasInterestCells());
}

TEST(WorldStreaming, LODStatisticsHelpersExposeDetailRepresentationAndQuality) {
    using namespace Next::Streaming;

    LODSystem::Statistics stats;
    EXPECT_EQ(stats.DetailedObjectCount(), 0u);
    EXPECT_EQ(stats.RepresentationObjectCount(), 0u);
    EXPECT_EQ(stats.TotalObjectCount(), 0u);
    EXPECT_FALSE(stats.HasHighDetailObjects());
    EXPECT_FALSE(stats.HasMediumDetailObjects());
    EXPECT_FALSE(stats.HasLowDetailObjects());
    EXPECT_FALSE(stats.HasDetailedObjects());
    EXPECT_FALSE(stats.HasHLODObjects());
    EXPECT_FALSE(stats.HasImpostorObjects());
    EXPECT_FALSE(stats.HasRepresentationObjects());
    EXPECT_FALSE(stats.HasObjects());
    EXPECT_FALSE(stats.HasAverageLODLevel());
    EXPECT_FALSE(stats.HasQualityScale());

    stats.highDetailObjects = 1;
    stats.mediumDetailObjects = 2;
    stats.lowDetailObjects = 3;
    stats.hlodObjects = 4;
    stats.impostorObjects = 5;
    stats.averageLODLevel = 1.5f;
    stats.currentQualityScale = 0.75f;

    EXPECT_EQ(stats.DetailedObjectCount(), 6u);
    EXPECT_EQ(stats.RepresentationObjectCount(), 9u);
    EXPECT_EQ(stats.TotalObjectCount(), 15u);
    EXPECT_TRUE(stats.HasHighDetailObjects());
    EXPECT_TRUE(stats.HasMediumDetailObjects());
    EXPECT_TRUE(stats.HasLowDetailObjects());
    EXPECT_TRUE(stats.HasDetailedObjects());
    EXPECT_TRUE(stats.HasHLODObjects());
    EXPECT_TRUE(stats.HasImpostorObjects());
    EXPECT_TRUE(stats.HasRepresentationObjects());
    EXPECT_TRUE(stats.HasObjects());
    EXPECT_TRUE(stats.HasAverageLODLevel());
    EXPECT_TRUE(stats.HasQualityScale());
}

TEST(WorldStreaming, EvictionStatisticsHelpersExposeActivityAndMemory) {
    using namespace Next::Streaming;

    EvictionPolicy::Statistics stats;
    EXPECT_FALSE(stats.HasEvictions());
    EXPECT_FALSE(stats.HasFrameEvictions());
    EXPECT_FALSE(stats.HasProtectedCells());
    EXPECT_FALSE(stats.HasAverageEvictionScore());
    EXPECT_FALSE(stats.HasMemoryFreed());
    EXPECT_FALSE(stats.HasActivity());

    stats.totalEvictions = 2;
    stats.evictionsThisFrame = 1;
    stats.protectedCells = 3;
    stats.averageEvictionScore = 0.25f;
    stats.memoryFreed = 4096;

    EXPECT_TRUE(stats.HasEvictions());
    EXPECT_TRUE(stats.HasFrameEvictions());
    EXPECT_TRUE(stats.HasProtectedCells());
    EXPECT_TRUE(stats.HasAverageEvictionScore());
    EXPECT_TRUE(stats.HasMemoryFreed());
    EXPECT_TRUE(stats.HasActivity());
}

TEST(WorldStreaming, StreamingManagerRejectsZeroMemoryBudget) {
    using namespace Next::Streaming;

    StreamingManager mgr;
    StreamingManagerConfig cfg;
    cfg.memoryBudgetMB = 0;
    cfg.loadRadius = 0.0f;
    cfg.unloadRadius = 128.0f;

    EXPECT_FALSE(mgr.Initialize(cfg));
}

TEST(WorldStreaming, StreamingManagerRejectsOverflowedMemoryBudget) {
    using namespace Next::Streaming;

    StreamingManager mgr;
    StreamingManagerConfig cfg;
    cfg.memoryBudgetMB = std::numeric_limits<size_t>::max();
    cfg.loadRadius = 0.0f;
    cfg.unloadRadius = 128.0f;

    EXPECT_FALSE(mgr.Initialize(cfg));
}

TEST(WorldStreaming, StreamingManagerRejectsZeroConcurrentLoadLimit) {
    using namespace Next::Streaming;

    StreamingManager mgr;
    StreamingManagerConfig cfg;
    cfg.memoryBudgetMB = 64;
    cfg.loadRadius = 0.0f;
    cfg.unloadRadius = 128.0f;
    cfg.maxConcurrentLoads = 0;
    cfg.maxConcurrentUnloads = 4;

    EXPECT_FALSE(mgr.Initialize(cfg));
}

TEST(WorldStreaming, StreamingManagerRejectsZeroConcurrentUnloadLimit) {
    using namespace Next::Streaming;

    StreamingManager mgr;
    StreamingManagerConfig cfg;
    cfg.memoryBudgetMB = 64;
    cfg.loadRadius = 0.0f;
    cfg.unloadRadius = 128.0f;
    cfg.maxConcurrentLoads = 4;
    cfg.maxConcurrentUnloads = 0;

    EXPECT_FALSE(mgr.Initialize(cfg));
}

TEST(WorldStreaming, StreamingManagerSetConfigBeforeInitializeStoresConfigOnly) {
    using namespace Next::Streaming;

    StreamingManager mgr;
    StreamingManagerConfig cfg;
    cfg.memoryBudgetMB = 64;
    cfg.loadRadius = 32.0f;
    cfg.unloadRadius = 128.0f;
    cfg.maxConcurrentLoads = 4;
    cfg.maxConcurrentUnloads = 4;

    mgr.SetConfig(cfg);

    EXPECT_EQ(mgr.GetConfig().memoryBudgetMB, 64u);
    EXPECT_FLOAT_EQ(mgr.GetConfig().loadRadius, 32.0f);
    EXPECT_EQ(mgr.GetConfig().maxConcurrentLoads, 4u);
    EXPECT_EQ(mgr.GetConfig().maxConcurrentUnloads, 4u);
}

TEST(WorldStreaming, StreamingManagerSetConfigRejectsInvalidRuntimeConfig) {
    using namespace Next::Streaming;

    StreamingManager mgr;
    StreamingManagerConfig cfg;
    cfg.memoryBudgetMB = 64;
    cfg.loadRadius = 0.0f;
    cfg.unloadRadius = 128.0f;
    cfg.maxConcurrentLoads = 4;
    cfg.maxConcurrentUnloads = 4;
    ASSERT_TRUE(mgr.Initialize(cfg));

    StreamingManagerConfig invalidBudget = cfg;
    invalidBudget.memoryBudgetMB = 0;
    mgr.SetConfig(invalidBudget);
    EXPECT_EQ(mgr.GetConfig().memoryBudgetMB, 64u);
    EXPECT_EQ(mgr.GetMemoryBudget(), 64u * 1024u * 1024u);

    StreamingManagerConfig invalidConcurrency = cfg;
    invalidConcurrency.maxConcurrentLoads = 0;
    mgr.SetConfig(invalidConcurrency);
    EXPECT_EQ(mgr.GetConfig().maxConcurrentLoads, 4u);
    EXPECT_EQ(mgr.GetConfig().maxConcurrentUnloads, 4u);
}

TEST(WorldStreaming, StreamingManagerSetConfigUpdatesValidRuntimeBudget) {
    using namespace Next::Streaming;

    StreamingManager mgr;
    StreamingManagerConfig cfg;
    cfg.memoryBudgetMB = 64;
    cfg.loadRadius = 0.0f;
    cfg.unloadRadius = 128.0f;
    cfg.maxConcurrentLoads = 4;
    cfg.maxConcurrentUnloads = 4;
    ASSERT_TRUE(mgr.Initialize(cfg));

    StreamingManagerConfig updated = cfg;
    updated.memoryBudgetMB = 32;
    updated.loadRadius = 16.0f;
    mgr.SetConfig(updated);

    EXPECT_EQ(mgr.GetConfig().memoryBudgetMB, 32u);
    EXPECT_EQ(mgr.GetMemoryBudget(), 32u * 1024u * 1024u);
    EXPECT_FLOAT_EQ(mgr.GetConfig().loadRadius, 16.0f);
}

TEST(WorldStreaming, StreamingManagerPreInitializeAccessorsReturnEmptyState) {
    using namespace Next::Streaming;

    StreamingManager mgr;
    const StreamingManager& constMgr = mgr;
    const CellCoord coord{0, 0};
    StreamingCellInfo info;

    EXPECT_FALSE(mgr.IsCellLoaded(coord));
    EXPECT_EQ(mgr.GetCell(coord), nullptr);
    EXPECT_EQ(constMgr.GetCell(coord), nullptr);
    EXPECT_FALSE(mgr.GetCellInfo(coord, info));
    EXPECT_TRUE(mgr.GetLoadedCells().empty());
    EXPECT_TRUE(mgr.GetLoadedCellInfos().empty());
    EXPECT_TRUE(mgr.GetCellsInRange(Next::Vec3(0.0f, 0.0f, 0.0f), 128.0f).empty());
    EXPECT_FALSE(mgr.IsCellLayerLoaded(coord, CellLayer::StaticMesh));
    EXPECT_EQ(mgr.GetWorldPartitionStatistics().loadedCells, 0u);
    EXPECT_EQ(mgr.GetMemoryUsage(), 0u);
    EXPECT_FLOAT_EQ(mgr.GetMemoryUtilization(), 0.0f);
}

TEST(WorldStreaming, StreamingManagerPreInitializePriorityControlsAreNoOps) {
    using namespace Next::Streaming;

    StreamingManager mgr;
    const CellCoord coord{0, 0};

    mgr.SetCellPriority(coord, 1.0f);
    mgr.BoostCellPriority(coord, 2.0f);

    SUCCEED();
}

TEST(WorldStreaming, LoadsCellsAroundCamera) {
    using namespace Next::Streaming;
    using Next::Vec3;

    StreamingManager mgr;
    StreamingManagerConfig cfg;
    cfg.memoryBudgetMB = 64;
    cfg.loadRadius = 128.0f;
    cfg.unloadRadius = 160.0f;
    cfg.maxConcurrentLoads = 64;
    cfg.maxConcurrentUnloads = 64;
    cfg.enablePrediction = false;
    const std::filesystem::path placeholderDir = std::filesystem::temp_directory_path() /
        ("next_world_streaming_placeholders_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    cfg.cellDataDirectory = placeholderDir.wstring();

    ASSERT_TRUE(mgr.Initialize(cfg));

    mgr.Update(0.016f, Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, -1.0f), Vec3(0.0f, 0.0f, 0.0f));

    auto loaded = mgr.GetLoadedCells();
    EXPECT_FALSE(loaded.empty());
    EXPECT_GE(loaded.size(), 4u);
    EXPECT_LE(loaded.size(), 64u);
    const StreamingStatistics stats = mgr.GetStatistics();
    EXPECT_EQ(stats.loadedCells, loaded.size());
    EXPECT_TRUE(stats.HasLoadedCells());
    EXPECT_TRUE(stats.HasActiveCells());
    EXPECT_TRUE(stats.HasMemoryBudget());
    EXPECT_TRUE(stats.HasMemoryUsage());
    EXPECT_FALSE(stats.HasFailures());
    const IOStatistics ioStats = mgr.GetIOStatistics();
    EXPECT_EQ(ioStats.PendingOperationCount(), 0u);
    EXPECT_FALSE(ioStats.HasPendingOperations());
    EXPECT_FALSE(ioStats.HasFailures());
    const WorldPartition::Statistics partitionStats = mgr.GetWorldPartitionStatistics();
    EXPECT_EQ(partitionStats.loadedCells, loaded.size());
    EXPECT_TRUE(partitionStats.HasTrackedCells());
    EXPECT_TRUE(partitionStats.HasActiveCells());
    EXPECT_LE(partitionStats.ActiveCellCount(), partitionStats.totalCells);
    const InterestManager::Statistics interestStats = mgr.GetInterestStatistics();
    EXPECT_TRUE(interestStats.HasActiveInterestPoints());
    const LODSystem::Statistics lodStats = mgr.GetLODStatistics();
    EXPECT_TRUE(lodStats.HasQualityScale());
    const EvictionPolicy::Statistics evictionStats = mgr.GetEvictionStatistics();
    EXPECT_FALSE(evictionStats.HasActivity());

    mgr.Shutdown();
}

TEST(WorldStreaming, AsyncIOCallbacksContainThrowingCallbackAndContinue) {
    using namespace Next::Streaming;

    const std::filesystem::path tmp = std::filesystem::temp_directory_path() / "next_async_io_callback_test.bin";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(out.good());
        const std::array<char, 8> bytes = {'n', 'e', 'x', 't', 'o', '3', 'd', 'e'};
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }

    AsyncIOSystem io;
    AsyncIOConfig cfg;
    cfg.ioThreads = 1;
    cfg.decompressionThreads = 0;
    ASSERT_TRUE(io.Initialize(cfg));

    std::array<char, 4> firstRead = {};
    std::array<char, 4> secondRead = {};
    bool secondCallbackCalled = false;

    const uint64_t firstRequest = io.SubmitReadRequest(
        tmp.wstring(),
        0,
        firstRead.size(),
        firstRead.data(),
        CompressionType::None,
        [](bool success, uint64_t bytesProcessed) {
            EXPECT_TRUE(success);
            EXPECT_EQ(bytesProcessed, 4u);
            throw std::runtime_error("io callback failure");
        });
    const uint64_t secondRequest = io.SubmitReadRequest(
        tmp.wstring(),
        4,
        secondRead.size(),
        secondRead.data(),
        CompressionType::None,
        [&](bool success, uint64_t bytesProcessed) {
            secondCallbackCalled = true;
            EXPECT_TRUE(success);
            EXPECT_EQ(bytesProcessed, 4u);
        });

    ASSERT_NE(firstRequest, 0u);
    ASSERT_NE(secondRequest, 0u);

    for (int i = 0; i < 200 && !secondCallbackCalled; ++i) {
        io.Update();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    EXPECT_TRUE(secondCallbackCalled);
    EXPECT_EQ(std::memcmp(firstRead.data(), "next", firstRead.size()), 0);
    EXPECT_EQ(std::memcmp(secondRead.data(), "o3de", secondRead.size()), 0);
    EXPECT_FALSE(io.GetStatistics().HasFailures());
    EXPECT_EQ(io.GetStatistics().PendingOperationCount(), 0u);

    io.Shutdown();
    std::error_code ec;
    std::filesystem::remove(tmp, ec);
}

TEST(WorldStreaming, AsyncIOQueuedReadIsVisibleInStatisticsWithoutWorker) {
    using namespace Next::Streaming;

    const std::filesystem::path tmp = std::filesystem::temp_directory_path() / "next_async_io_pending_test.bin";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(out.good());
        const std::array<char, 4> bytes = {'n', 'e', 'x', 't'};
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }

    AsyncIOSystem io;
    AsyncIOConfig cfg;
    cfg.ioThreads = 0;
    cfg.decompressionThreads = 0;
    ASSERT_TRUE(io.Initialize(cfg));

    std::array<char, 4> readBuffer = {};
    const uint64_t request = io.SubmitReadRequest(
        tmp.wstring(),
        0,
        readBuffer.size(),
        readBuffer.data());

    ASSERT_NE(request, 0u);
    const IOStatistics stats = io.GetStatistics();
    EXPECT_EQ(stats.pendingReads, 1u);
    EXPECT_TRUE(stats.HasPendingReads());
    EXPECT_EQ(stats.PendingOperationCount(), 1u);

    io.Shutdown();
    std::error_code ec;
    std::filesystem::remove(tmp, ec);
}

TEST(WorldStreaming, AsyncIOQueuedWriteIsVisibleInStatisticsWithoutWorker) {
    using namespace Next::Streaming;

    const std::filesystem::path tmp = std::filesystem::temp_directory_path() / "next_async_io_pending_write_test.bin";
    std::error_code ec;
    std::filesystem::remove(tmp, ec);

    AsyncIOSystem io;
    AsyncIOConfig cfg;
    cfg.ioThreads = 0;
    cfg.decompressionThreads = 0;
    ASSERT_TRUE(io.Initialize(cfg));

    const std::array<char, 4> bytes = {'o', '3', 'd', 'e'};
    const uint64_t request = io.SubmitWriteRequest(
        tmp.wstring(),
        0,
        bytes.data(),
        bytes.size());

    ASSERT_NE(request, 0u);
    IOStatistics stats = io.GetStatistics();
    EXPECT_EQ(stats.pendingReads, 0u);
    EXPECT_FALSE(stats.HasPendingReads());
    EXPECT_EQ(stats.pendingWrites, 1u);
    EXPECT_TRUE(stats.HasPendingWrites());
    EXPECT_EQ(stats.PendingOperationCount(), 1u);

    EXPECT_TRUE(io.CancelRequest(request));
    stats = io.GetStatistics();
    EXPECT_EQ(stats.pendingWrites, 0u);
    EXPECT_FALSE(stats.HasPendingOperations());

    io.Shutdown();
    std::filesystem::remove(tmp, ec);
}

TEST(WorldStreaming, AsyncIOCompletedWriteRemainsPendingUntilUpdateDispatchesCallback) {
    using namespace Next::Streaming;

    const std::filesystem::path tmp = std::filesystem::temp_directory_path() / "next_async_io_completion_pending_test.bin";
    std::error_code ec;
    std::filesystem::remove(tmp, ec);

    AsyncIOSystem io;
    AsyncIOConfig cfg;
    cfg.ioThreads = 1;
    cfg.decompressionThreads = 0;
    ASSERT_TRUE(io.Initialize(cfg));

    const std::array<char, 8> bytes = {'n', 'e', 'x', 't', 'o', '3', 'd', 'e'};
    bool callbackCalled = false;
    const uint64_t request = io.SubmitWriteRequest(
        tmp.wstring(),
        0,
        bytes.data(),
        bytes.size(),
        [&](bool success, uint64_t bytesProcessed) {
            callbackCalled = true;
            EXPECT_TRUE(success);
            EXPECT_EQ(bytesProcessed, bytes.size());
        });

    ASSERT_NE(request, 0u);
    IOStatistics stats = io.GetStatistics();
    for (int i = 0; i < 200 && stats.totalBytesWritten != bytes.size(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        stats = io.GetStatistics();
    }

    ASSERT_EQ(stats.totalBytesWritten, bytes.size());
    EXPECT_FALSE(callbackCalled);
    EXPECT_EQ(stats.pendingWrites, 1u);
    EXPECT_TRUE(stats.HasPendingWrites());
    EXPECT_TRUE(stats.HasPendingOperations());

    for (int i = 0; i < 200 && !callbackCalled; ++i) {
        io.Update();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    EXPECT_TRUE(callbackCalled);
    EXPECT_EQ(io.GetStatistics().PendingOperationCount(), 0u);

    io.Shutdown();
    std::filesystem::remove(tmp, ec);
}

TEST(WorldStreaming, AsyncIOCancelCompletedWriteSuppressesCallbackBeforeUpdate) {
    using namespace Next::Streaming;

    const std::filesystem::path tmp = std::filesystem::temp_directory_path() / "next_async_io_cancel_completed_write_test.bin";
    std::error_code ec;
    std::filesystem::remove(tmp, ec);

    AsyncIOSystem io;
    AsyncIOConfig cfg;
    cfg.ioThreads = 1;
    cfg.decompressionThreads = 0;
    ASSERT_TRUE(io.Initialize(cfg));

    const std::array<char, 8> bytes = {'c', 'a', 'n', 'c', 'e', 'l', 'e', 'd'};
    bool callbackCalled = false;
    const uint64_t request = io.SubmitWriteRequest(
        tmp.wstring(),
        0,
        bytes.data(),
        bytes.size(),
        [&](bool, uint64_t) {
            callbackCalled = true;
        });

    ASSERT_NE(request, 0u);
    IOStatistics stats = io.GetStatistics();
    for (int i = 0; i < 200 && stats.totalBytesWritten != bytes.size(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        stats = io.GetStatistics();
    }
    ASSERT_EQ(stats.totalBytesWritten, bytes.size());
    std::this_thread::sleep_for(std::chrono::milliseconds(2));

    stats = io.GetStatistics();
    EXPECT_FALSE(callbackCalled);
    EXPECT_EQ(stats.pendingWrites, 1u);
    EXPECT_TRUE(stats.HasPendingOperations());

    EXPECT_TRUE(io.CancelRequest(request));
    stats = io.GetStatistics();
    EXPECT_EQ(stats.pendingWrites, 0u);
    EXPECT_FALSE(stats.HasPendingOperations());
    EXPECT_FALSE(io.CancelRequest(request));

    for (int i = 0; i < 5; ++i) {
        io.Update();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    EXPECT_FALSE(callbackCalled);
    EXPECT_EQ(io.GetStatistics().PendingOperationCount(), 0u);

    io.Shutdown();
    std::filesystem::remove(tmp, ec);
}

TEST(WorldStreaming, AsyncIOCallbackCanSubmitFollowUpRequest) {
    using namespace Next::Streaming;

    const std::filesystem::path tmp = std::filesystem::temp_directory_path() / "next_async_io_callback_submit_test.bin";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(out.good());
        const std::array<char, 8> bytes = {'n', 'e', 'x', 't', 'o', '3', 'd', 'e'};
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }

    AsyncIOSystem io;
    AsyncIOConfig cfg;
    cfg.ioThreads = 1;
    cfg.decompressionThreads = 0;
    ASSERT_TRUE(io.Initialize(cfg));

    std::array<char, 4> firstRead = {};
    std::array<char, 4> secondRead = {};
    bool firstCallbackCalled = false;
    bool secondCallbackCalled = false;
    uint64_t secondRequest = 0;

    const uint64_t firstRequest = io.SubmitReadRequest(
        tmp.wstring(),
        0,
        firstRead.size(),
        firstRead.data(),
        CompressionType::None,
        [&](bool success, uint64_t bytesProcessed) {
            firstCallbackCalled = true;
            EXPECT_TRUE(success);
            EXPECT_EQ(bytesProcessed, firstRead.size());
            secondRequest = io.SubmitReadRequest(
                tmp.wstring(),
                4,
                secondRead.size(),
                secondRead.data(),
                CompressionType::None,
                [&](bool secondSuccess, uint64_t secondBytesProcessed) {
                    secondCallbackCalled = true;
                    EXPECT_TRUE(secondSuccess);
                    EXPECT_EQ(secondBytesProcessed, secondRead.size());
                });
            EXPECT_NE(secondRequest, 0u);
        });

    ASSERT_NE(firstRequest, 0u);
    for (int i = 0; i < 200 && !secondCallbackCalled; ++i) {
        io.Update();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    EXPECT_TRUE(firstCallbackCalled);
    EXPECT_TRUE(secondCallbackCalled);
    EXPECT_NE(secondRequest, 0u);
    EXPECT_EQ(std::memcmp(firstRead.data(), "next", firstRead.size()), 0);
    EXPECT_EQ(std::memcmp(secondRead.data(), "o3de", secondRead.size()), 0);
    EXPECT_EQ(io.GetStatistics().PendingOperationCount(), 0u);

    io.Shutdown();
    std::error_code ec;
    std::filesystem::remove(tmp, ec);
}

TEST(WorldStreaming, AsyncIOPriorityOrdersQueuedDecompressionRequests) {
    using namespace Next::Streaming;

    AsyncIOSystem io;
    AsyncIOConfig cfg;
    cfg.ioThreads = 0;
    cfg.decompressionThreads = 0;
    ASSERT_TRUE(io.Initialize(cfg));

    std::array<char, 4> lowInput = {'l', 'o', 'w', '!'};
    std::array<char, 4> lowOutput = {};
    std::array<char, 4> mediumInput = {'m', 'i', 'd', '!'};
    std::array<char, 4> mediumOutput = {};
    std::array<char, 4> highInput = {'h', 'i', '!', '!'};
    std::array<char, 4> highOutput = {};
    std::vector<std::string> completionOrder;

    const uint64_t lowRequest = io.SubmitDecompressRequest(
        lowInput.data(),
        lowInput.size(),
        lowOutput.data(),
        lowOutput.size(),
        CompressionType::None,
        [&](bool success, uint64_t bytesProcessed) {
            EXPECT_TRUE(success);
            EXPECT_EQ(bytesProcessed, lowInput.size());
            completionOrder.push_back("low");
        },
        100);
    ASSERT_NE(lowRequest, 0u);

    const uint64_t mediumRequest = io.SubmitDecompressRequest(
        mediumInput.data(),
        mediumInput.size(),
        mediumOutput.data(),
        mediumOutput.size(),
        CompressionType::None,
        [&](bool success, uint64_t bytesProcessed) {
            EXPECT_TRUE(success);
            EXPECT_EQ(bytesProcessed, mediumInput.size());
            completionOrder.push_back("medium");
        },
        50);
    const uint64_t highRequest = io.SubmitDecompressRequest(
        highInput.data(),
        highInput.size(),
        highOutput.data(),
        highOutput.size(),
        CompressionType::None,
        [&](bool success, uint64_t bytesProcessed) {
            EXPECT_TRUE(success);
            EXPECT_EQ(bytesProcessed, highInput.size());
            completionOrder.push_back("high");
        },
        0);

    ASSERT_NE(mediumRequest, 0u);
    ASSERT_NE(highRequest, 0u);

    io.Update();

    ASSERT_EQ(completionOrder.size(), 3u);
    EXPECT_EQ(completionOrder[0], "high");
    EXPECT_EQ(completionOrder[1], "medium");
    EXPECT_EQ(completionOrder[2], "low");
    EXPECT_EQ(std::memcmp(highOutput.data(), highInput.data(), highInput.size()), 0);
    EXPECT_EQ(std::memcmp(mediumOutput.data(), mediumInput.data(), mediumInput.size()), 0);
    EXPECT_EQ(std::memcmp(lowOutput.data(), lowInput.data(), lowInput.size()), 0);
    EXPECT_EQ(io.GetStatistics().PendingOperationCount(), 0u);

    io.Shutdown();
}

TEST(WorldStreaming, AsyncIORejectsUnsupportedUploadGpuRequest) {
    using namespace Next::Streaming;

    AsyncIOSystem io;
    AsyncIOConfig cfg;
    cfg.ioThreads = 0;
    cfg.decompressionThreads = 0;
    ASSERT_TRUE(io.Initialize(cfg));

    IORequest request;
    request.type = IOOperationType::UploadGPU;

    EXPECT_EQ(io.SubmitRequest(request), 0u);
    EXPECT_EQ(io.GetStatistics().PendingOperationCount(), 0u);
    EXPECT_FALSE(io.CancelRequest(0));

    io.Shutdown();
}

TEST(WorldStreaming, AsyncIORejectsRequestsBeyondMaxPendingLimit) {
    using namespace Next::Streaming;

    const std::filesystem::path tmp = std::filesystem::temp_directory_path() / "next_async_io_pending_limit_test.bin";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(out.good());
        const std::array<char, 8> bytes = {'n', 'e', 'x', 't', 'o', '3', 'd', 'e'};
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }

    AsyncIOSystem io;
    AsyncIOConfig cfg;
    cfg.ioThreads = 0;
    cfg.decompressionThreads = 0;
    cfg.maxPendingRequests = 1;
    ASSERT_TRUE(io.Initialize(cfg));

    std::array<char, 4> firstRead = {};
    std::array<char, 4> secondRead = {};
    const uint64_t firstRequest = io.SubmitReadRequest(
        tmp.wstring(),
        0,
        firstRead.size(),
        firstRead.data());
    const uint64_t secondRequest = io.SubmitReadRequest(
        tmp.wstring(),
        4,
        secondRead.size(),
        secondRead.data());

    EXPECT_NE(firstRequest, 0u);
    EXPECT_EQ(secondRequest, 0u);
    EXPECT_EQ(io.GetStatistics().PendingOperationCount(), 1u);

    io.Shutdown();
    std::error_code ec;
    std::filesystem::remove(tmp, ec);
}

TEST(WorldStreaming, AsyncIOMaxConcurrentReadsLimitsReadsOnly) {
    using namespace Next::Streaming;

    const std::filesystem::path tmp = std::filesystem::temp_directory_path() / "next_async_io_read_limit_test.bin";
    std::error_code ec;
    std::filesystem::remove(tmp, ec);

    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(out.good());
        const std::array<char, 8> bytes = {'n', 'e', 'x', 't', 'o', '3', 'd', 'e'};
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }

    AsyncIOSystem io;
    AsyncIOConfig cfg;
    cfg.ioThreads = 0;
    cfg.decompressionThreads = 0;
    cfg.maxPendingRequests = 4;
    cfg.maxConcurrentReads = 1;
    ASSERT_TRUE(io.Initialize(cfg));

    std::array<char, 4> firstRead = {};
    std::array<char, 4> secondRead = {};
    std::array<char, 4> writeBytes = {'o', '3', 'd', 'e'};
    std::array<char, 4> decompressInput = {'n', 'e', 'x', 't'};
    std::array<char, 4> decompressOutput = {};

    const uint64_t firstReadRequest = io.SubmitReadRequest(
        tmp.wstring(),
        0,
        firstRead.size(),
        firstRead.data());
    const uint64_t secondReadRequest = io.SubmitReadRequest(
        tmp.wstring(),
        4,
        secondRead.size(),
        secondRead.data());
    const uint64_t writeRequest = io.SubmitWriteRequest(
        tmp.wstring(),
        0,
        writeBytes.data(),
        writeBytes.size());
    const uint64_t decompressRequest = io.SubmitDecompressRequest(
        decompressInput.data(),
        decompressInput.size(),
        decompressOutput.data(),
        decompressOutput.size(),
        CompressionType::None);

    EXPECT_NE(firstReadRequest, 0u);
    EXPECT_EQ(secondReadRequest, 0u);
    EXPECT_NE(writeRequest, 0u);
    EXPECT_NE(decompressRequest, 0u);

    const IOStatistics stats = io.GetStatistics();
    EXPECT_EQ(stats.pendingReads, 1u);
    EXPECT_EQ(stats.pendingWrites, 1u);
    EXPECT_EQ(stats.pendingDecompressions, 1u);
    EXPECT_EQ(stats.PendingOperationCount(), 3u);

    EXPECT_TRUE(io.CancelAllRequests());
    EXPECT_EQ(io.GetStatistics().PendingOperationCount(), 0u);

    io.Shutdown();
    std::filesystem::remove(tmp, ec);
}

TEST(WorldStreaming, AsyncIOSetConfigBeforeInitializeAppliesPendingLimit) {
    using namespace Next::Streaming;

    const std::filesystem::path tmp = std::filesystem::temp_directory_path() / "next_async_io_set_config_preinit_test.bin";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(out.good());
        const std::array<char, 8> bytes = {'n', 'e', 'x', 't', 'o', '3', 'd', 'e'};
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }

    AsyncIOSystem io;
    AsyncIOConfig cfg;
    cfg.ioThreads = 0;
    cfg.decompressionThreads = 0;
    cfg.maxPendingRequests = 1;
    ASSERT_TRUE(io.SetConfig(cfg));
    EXPECT_EQ(io.GetConfig().maxPendingRequests, 1u);
    const AsyncIOConfig initializedConfig = io.GetConfig();
    ASSERT_TRUE(io.Initialize(initializedConfig));

    std::array<char, 4> firstRead = {};
    std::array<char, 4> secondRead = {};
    const uint64_t firstRequest = io.SubmitReadRequest(
        tmp.wstring(),
        0,
        firstRead.size(),
        firstRead.data());
    const uint64_t secondRequest = io.SubmitReadRequest(
        tmp.wstring(),
        4,
        secondRead.size(),
        secondRead.data());

    EXPECT_NE(firstRequest, 0u);
    EXPECT_EQ(secondRequest, 0u);
    EXPECT_EQ(io.GetStatistics().PendingOperationCount(), 1u);

    io.Shutdown();
    std::error_code ec;
    std::filesystem::remove(tmp, ec);
}

TEST(WorldStreaming, AsyncIOSetConfigAfterInitializeIsRejected) {
    using namespace Next::Streaming;

    const std::filesystem::path tmp = std::filesystem::temp_directory_path() / "next_async_io_set_config_live_test.bin";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(out.good());
        const std::array<char, 8> bytes = {'o', '3', 'd', 'e', 'n', 'e', 'x', 't'};
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }

    AsyncIOSystem io;
    AsyncIOConfig cfg;
    cfg.ioThreads = 0;
    cfg.decompressionThreads = 0;
    cfg.maxPendingRequests = 1;
    ASSERT_TRUE(io.Initialize(cfg));

    AsyncIOConfig liveConfig = cfg;
    liveConfig.ioThreads = 4;
    liveConfig.decompressionThreads = 4;
    liveConfig.maxPendingRequests = 2;
    EXPECT_FALSE(io.SetConfig(liveConfig));
    EXPECT_EQ(io.GetConfig().ioThreads, 0u);
    EXPECT_EQ(io.GetConfig().decompressionThreads, 0u);
    EXPECT_EQ(io.GetConfig().maxPendingRequests, 1u);

    std::array<char, 4> firstRead = {};
    std::array<char, 4> secondRead = {};
    const uint64_t firstRequest = io.SubmitReadRequest(
        tmp.wstring(),
        0,
        firstRead.size(),
        firstRead.data());
    const uint64_t secondRequest = io.SubmitReadRequest(
        tmp.wstring(),
        4,
        secondRead.size(),
        secondRead.data());

    EXPECT_NE(firstRequest, 0u);
    EXPECT_EQ(secondRequest, 0u);
    EXPECT_EQ(io.GetStatistics().PendingOperationCount(), 1u);

    io.Shutdown();
    std::error_code ec;
    std::filesystem::remove(tmp, ec);
}

TEST(WorldStreaming, AsyncIOWriteCreatesFileAndUpdatesStats) {
    using namespace Next::Streaming;

    const std::filesystem::path tmp = std::filesystem::temp_directory_path() / "next_async_io_write_test.bin";
    std::error_code ec;
    std::filesystem::remove(tmp, ec);

    AsyncIOSystem io;
    AsyncIOConfig cfg;
    cfg.ioThreads = 1;
    cfg.decompressionThreads = 0;
    ASSERT_TRUE(io.Initialize(cfg));

    const std::array<char, 8> bytes = {'n', 'e', 'x', 't', 'o', '3', 'd', 'e'};
    bool callbackCalled = false;
    bool callbackSuccess = false;
    uint64_t callbackBytes = 0;

    const uint64_t request = io.SubmitWriteRequest(
        tmp.wstring(),
        0,
        bytes.data(),
        bytes.size(),
        [&](bool success, uint64_t bytesProcessed) {
            callbackCalled = true;
            callbackSuccess = success;
            callbackBytes = bytesProcessed;
        });

    ASSERT_NE(request, 0u);
    for (int i = 0; i < 200 && !callbackCalled; ++i) {
        io.Update();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    EXPECT_TRUE(callbackCalled);
    EXPECT_TRUE(callbackSuccess);
    EXPECT_EQ(callbackBytes, bytes.size());

    const IOStatistics stats = io.GetStatistics();
    EXPECT_EQ(stats.totalBytesWritten, bytes.size());
    EXPECT_TRUE(stats.HasWrittenBytes());
    EXPECT_FALSE(stats.HasFailures());
    EXPECT_EQ(stats.PendingOperationCount(), 0u);

    std::array<char, 8> loaded = {};
    {
        std::ifstream in(tmp, std::ios::binary);
        ASSERT_TRUE(in.good());
        in.read(loaded.data(), static_cast<std::streamsize>(loaded.size()));
        EXPECT_EQ(static_cast<size_t>(in.gcount()), loaded.size());
    }
    EXPECT_EQ(std::memcmp(loaded.data(), bytes.data(), bytes.size()), 0);

    io.Shutdown();
    std::filesystem::remove(tmp, ec);
}

TEST(WorldStreaming, AsyncIODecompressNoneCopiesInputAndUpdatesStats) {
    using namespace Next::Streaming;

    AsyncIOSystem io;
    AsyncIOConfig cfg;
    cfg.ioThreads = 0;
    cfg.decompressionThreads = 1;
    ASSERT_TRUE(io.Initialize(cfg));

    std::array<char, 4> input = {'o', '3', 'd', 'e'};
    std::array<char, 4> output = {};
    bool callbackCalled = false;
    bool callbackSuccess = false;
    uint64_t callbackBytes = 0;

    const uint64_t request = io.SubmitDecompressRequest(
        input.data(),
        input.size(),
        output.data(),
        output.size(),
        CompressionType::None,
        [&](bool success, uint64_t bytesProcessed) {
            callbackCalled = true;
            callbackSuccess = success;
            callbackBytes = bytesProcessed;
        });

    ASSERT_NE(request, 0u);
    for (int i = 0; i < 200 && !callbackCalled; ++i) {
        io.Update();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    EXPECT_TRUE(callbackCalled);
    EXPECT_TRUE(callbackSuccess);
    EXPECT_EQ(callbackBytes, input.size());
    EXPECT_EQ(std::memcmp(output.data(), input.data(), input.size()), 0);

    const IOStatistics stats = io.GetStatistics();
    EXPECT_EQ(stats.totalBytesDecompressed, input.size());
    EXPECT_TRUE(stats.HasDecompressedBytes());
    EXPECT_FALSE(stats.HasFailures());
    EXPECT_EQ(stats.PendingOperationCount(), 0u);

    io.Shutdown();
}

TEST(WorldStreaming, AsyncIODecompressNoneRejectsSmallOutputBuffer) {
    using namespace Next::Streaming;

    AsyncIOSystem io;
    AsyncIOConfig cfg;
    cfg.ioThreads = 0;
    cfg.decompressionThreads = 1;
    ASSERT_TRUE(io.Initialize(cfg));

    std::array<char, 8> input = {'n', 'e', 'x', 't', 'o', '3', 'd', 'e'};
    std::array<char, 4> output = {};
    bool callbackCalled = false;
    bool callbackSuccess = true;
    uint64_t callbackBytes = 99;

    const uint64_t request = io.SubmitDecompressRequest(
        input.data(),
        input.size(),
        output.data(),
        output.size(),
        CompressionType::None,
        [&](bool success, uint64_t bytesProcessed) {
            callbackCalled = true;
            callbackSuccess = success;
            callbackBytes = bytesProcessed;
        });

    ASSERT_NE(request, 0u);
    for (int i = 0; i < 200 && !callbackCalled; ++i) {
        io.Update();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    EXPECT_TRUE(callbackCalled);
    EXPECT_FALSE(callbackSuccess);
    EXPECT_EQ(callbackBytes, 0u);

    const IOStatistics stats = io.GetStatistics();
    EXPECT_FALSE(stats.HasDecompressedBytes());
    EXPECT_TRUE(stats.HasFailures());
    EXPECT_EQ(stats.failedOperations, 1u);
    EXPECT_EQ(stats.PendingOperationCount(), 0u);

    io.Shutdown();
}

TEST(WorldStreaming, AsyncIODecompressLZ4RoundTripsThroughCompressionBackend) {
    ExpectAsyncDecompressionRoundTrip(
        Next::Compression::Algorithm::LZ4,
        Next::Streaming::CompressionType::LZ4);
}

TEST(WorldStreaming, AsyncIODecompressZstdRoundTripsThroughCompressionBackend) {
    ExpectAsyncDecompressionRoundTrip(
        Next::Compression::Algorithm::Zstd,
        Next::Streaming::CompressionType::Zstd);
}

TEST(WorldStreaming, AsyncIOCancelRequestRemovesQueuedRead) {
    using namespace Next::Streaming;

    const std::filesystem::path tmp = std::filesystem::temp_directory_path() / "next_async_io_cancel_one_test.bin";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(out.good());
        const std::array<char, 4> bytes = {'n', 'e', 'x', 't'};
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }

    AsyncIOSystem io;
    AsyncIOConfig cfg;
    cfg.ioThreads = 0;
    cfg.decompressionThreads = 0;
    ASSERT_TRUE(io.Initialize(cfg));

    std::array<char, 4> readBuffer = {};
    const uint64_t request = io.SubmitReadRequest(
        tmp.wstring(),
        0,
        readBuffer.size(),
        readBuffer.data());

    ASSERT_NE(request, 0u);
    EXPECT_EQ(io.GetStatistics().PendingOperationCount(), 1u);
    EXPECT_TRUE(io.CancelRequest(request));
    EXPECT_EQ(io.GetStatistics().PendingOperationCount(), 0u);
    EXPECT_FALSE(io.CancelRequest(request));

    io.Shutdown();
    std::error_code ec;
    std::filesystem::remove(tmp, ec);
}

TEST(WorldStreaming, AsyncIOCancelAllRequestsClearsQueuedReads) {
    using namespace Next::Streaming;

    const std::filesystem::path tmp = std::filesystem::temp_directory_path() / "next_async_io_cancel_all_test.bin";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(out.good());
        const std::array<char, 8> bytes = {'n', 'e', 'x', 't', 'o', '3', 'd', 'e'};
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }

    AsyncIOSystem io;
    AsyncIOConfig cfg;
    cfg.ioThreads = 0;
    cfg.decompressionThreads = 0;
    ASSERT_TRUE(io.Initialize(cfg));

    std::array<char, 4> firstRead = {};
    std::array<char, 4> secondRead = {};
    const uint64_t firstRequest = io.SubmitReadRequest(
        tmp.wstring(),
        0,
        firstRead.size(),
        firstRead.data());
    const uint64_t secondRequest = io.SubmitReadRequest(
        tmp.wstring(),
        4,
        secondRead.size(),
        secondRead.data());

    ASSERT_NE(firstRequest, 0u);
    ASSERT_NE(secondRequest, 0u);
    EXPECT_EQ(io.GetStatistics().PendingOperationCount(), 2u);
    EXPECT_TRUE(io.CancelAllRequests());
    EXPECT_EQ(io.GetStatistics().PendingOperationCount(), 0u);

    io.Shutdown();
    std::error_code ec;
    std::filesystem::remove(tmp, ec);
}

TEST(WorldStreaming, AsyncIOShutdownDropsQueuedRequestsBeforeReinitialize) {
    using namespace Next::Streaming;

    const std::filesystem::path tmp = std::filesystem::temp_directory_path() / "next_async_io_reinit_test.bin";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(out.good());
        const std::array<char, 4> bytes = {'n', 'e', 'x', 't'};
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }

    AsyncIOSystem io;
    AsyncIOConfig cfg;
    cfg.ioThreads = 0;
    cfg.decompressionThreads = 0;
    ASSERT_TRUE(io.Initialize(cfg));

    std::array<char, 4> staleRead = {};
    const uint64_t staleRequest = io.SubmitReadRequest(
        tmp.wstring(),
        0,
        staleRead.size(),
        staleRead.data());
    ASSERT_NE(staleRequest, 0u);
    EXPECT_EQ(io.GetStatistics().PendingOperationCount(), 1u);
    io.Shutdown();

    cfg.ioThreads = 1;
    ASSERT_TRUE(io.Initialize(cfg));

    std::array<char, 4> freshRead = {};
    bool callbackCalled = false;
    const uint64_t freshRequest = io.SubmitReadRequest(
        tmp.wstring(),
        0,
        freshRead.size(),
        freshRead.data(),
        CompressionType::None,
        [&](bool success, uint64_t bytesProcessed) {
            callbackCalled = true;
            EXPECT_TRUE(success);
            EXPECT_EQ(bytesProcessed, 4u);
        });

    ASSERT_NE(freshRequest, 0u);
    for (int i = 0; i < 200 && !callbackCalled; ++i) {
        io.Update();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    EXPECT_TRUE(callbackCalled);
    EXPECT_EQ(std::memcmp(staleRead.data(), "\0\0\0\0", staleRead.size()), 0);
    EXPECT_EQ(std::memcmp(freshRead.data(), "next", freshRead.size()), 0);
    EXPECT_EQ(io.GetStatistics().PendingOperationCount(), 0u);

    io.Shutdown();
    std::error_code ec;
    std::filesystem::remove(tmp, ec);
}

TEST(WorldStreaming, AsyncIOReadFailureReportsFailureAndClearsPendingState) {
    using namespace Next::Streaming;

    const std::filesystem::path missing = std::filesystem::temp_directory_path() / "next_async_io_missing_test.bin";
    std::error_code ec;
    std::filesystem::remove(missing, ec);

    AsyncIOSystem io;
    AsyncIOConfig cfg;
    cfg.ioThreads = 1;
    cfg.decompressionThreads = 0;
    ASSERT_TRUE(io.Initialize(cfg));

    std::array<char, 4> readBuffer = {};
    bool callbackCalled = false;
    bool callbackSuccess = true;
    uint64_t callbackBytes = 99;

    const uint64_t request = io.SubmitReadRequest(
        missing.wstring(),
        0,
        readBuffer.size(),
        readBuffer.data(),
        CompressionType::None,
        [&](bool success, uint64_t bytesProcessed) {
            callbackCalled = true;
            callbackSuccess = success;
            callbackBytes = bytesProcessed;
        });

    ASSERT_NE(request, 0u);
    for (int i = 0; i < 200 && !callbackCalled; ++i) {
        io.Update();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    EXPECT_TRUE(callbackCalled);
    EXPECT_FALSE(callbackSuccess);
    EXPECT_EQ(callbackBytes, 0u);

    const IOStatistics stats = io.GetStatistics();
    EXPECT_TRUE(stats.HasFailures());
    EXPECT_EQ(stats.failedOperations, 1u);
    EXPECT_EQ(stats.PendingOperationCount(), 0u);

    io.Shutdown();
}

TEST(WorldStreaming, AssetBundleInfoTracksLoadedBundleAndInvalidHandles) {
    using namespace Next::Streaming;

    const std::filesystem::path tmp = std::filesystem::temp_directory_path() / "next_world_streaming_bundle_info";
    std::filesystem::remove_all(tmp);
    WriteCellFile(tmp, 2, 3, 4096);
    WriteCellFile(tmp, -1, 0, 2048);

    StreamingManager mgr;
    StreamingManagerConfig cfg;
    cfg.memoryBudgetMB = 64;
    cfg.loadRadius = 0.0f;
    cfg.unloadRadius = 128.0f;
    cfg.maxConcurrentLoads = 64;
    cfg.maxConcurrentUnloads = 64;
    cfg.enablePrediction = false;
    cfg.cellDataDirectory = tmp.wstring();
    cfg.allowPlaceholderCellLoad = false;

    ASSERT_TRUE(mgr.Initialize(cfg));

    AssetBundle info;
    EXPECT_FALSE(mgr.GetAssetBundleInfo(StreamingHandle::Invalid(), info));
    EXPECT_FALSE(info.IsValid());

    const StreamingHandle handle = mgr.LoadAssetBundle(tmp.wstring());
    ASSERT_TRUE(handle.IsValid());
    ASSERT_TRUE(mgr.IsAssetBundleLoaded(handle));
    ASSERT_TRUE(mgr.GetAssetBundleInfo(handle, info));

    EXPECT_EQ(info.bundleId, handle.id);
    EXPECT_TRUE(info.HasPath());
    EXPECT_EQ(info.bundlePath, tmp.wstring());
    EXPECT_TRUE(info.HasCells());
    EXPECT_EQ(info.CellCount(), 2u);
    EXPECT_TRUE(info.HasSize());
    EXPECT_EQ(info.totalSize, 4096u + 2048u);
    EXPECT_FALSE(info.HasDependencies());

    mgr.UnloadAssetBundle(handle);
    EXPECT_FALSE(mgr.IsAssetBundleLoaded(handle));
    EXPECT_FALSE(mgr.GetAssetBundleInfo(handle, info));
    EXPECT_FALSE(info.IsValid());

    mgr.Shutdown();
    std::filesystem::remove_all(tmp);
}

TEST(WorldStreaming, LoadAssetBundleUsesDiscoveredCellPathsOutsideConfiguredRoot) {
    using namespace Next::Streaming;
    using Next::Vec3;

    const CellCoord origin{0, 0};
    const std::filesystem::path tmp = std::filesystem::temp_directory_path() / "next_world_streaming_external_bundle";
    const std::filesystem::path configuredRoot = tmp / "configured_root";
    const std::filesystem::path bundleRoot = tmp / "bundle_root" / "region_0_0";
    std::filesystem::remove_all(tmp);
    std::filesystem::create_directories(configuredRoot);
    WriteCellFile(bundleRoot, origin.x, origin.z, 4096);

    StreamingManager mgr;
    StreamingManagerConfig cfg;
    cfg.memoryBudgetMB = 64;
    cfg.loadRadius = 0.0f;
    cfg.unloadRadius = 64.0f;
    cfg.maxConcurrentLoads = 64;
    cfg.maxConcurrentUnloads = 64;
    cfg.enablePrediction = false;
    cfg.cellDataDirectory = configuredRoot.wstring();
    cfg.allowPlaceholderCellLoad = false;

    ASSERT_TRUE(mgr.Initialize(cfg));

    const StreamingHandle handle = mgr.LoadAssetBundle((tmp / "bundle_root").wstring());
    ASSERT_TRUE(handle.IsValid());

    for (int i = 0; i < 50 && !mgr.IsCellLoaded(origin); ++i) {
        mgr.Update(0.016f, Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, -1.0f), Vec3(0.0f, 0.0f, 0.0f));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    StreamingCellInfo info;
    ASSERT_TRUE(mgr.GetCellInfo(origin, info));
    EXPECT_TRUE(info.IsLoaded());
    EXPECT_TRUE(info.HasDiskData());
    EXPECT_FALSE(info.IsPlaceholder());
    EXPECT_FALSE(mgr.GetStatistics().HasFailures());
    EXPECT_FALSE(mgr.GetIOStatistics().HasFailures());

    mgr.Shutdown();
    std::filesystem::remove_all(tmp);
}

TEST(WorldStreaming, UnloadOverlappingAssetBundlePreservesReferencedCell) {
    using namespace Next::Streaming;
    using Next::Vec3;

    const CellCoord origin{0, 0};
    const std::filesystem::path tmp = std::filesystem::temp_directory_path() / "next_world_streaming_overlap_bundle";
    const std::filesystem::path configuredRoot = tmp / "configured_root";
    const std::filesystem::path firstBundleRoot = tmp / "bundle_a";
    const std::filesystem::path secondBundleRoot = tmp / "bundle_b";
    std::filesystem::remove_all(tmp);
    std::filesystem::create_directories(configuredRoot);
    WriteCellFile(firstBundleRoot, origin.x, origin.z, 4096);
    WriteCellFile(secondBundleRoot, origin.x, origin.z, 2048);

    StreamingManager mgr;
    StreamingManagerConfig cfg;
    cfg.memoryBudgetMB = 64;
    cfg.loadRadius = 0.0f;
    cfg.unloadRadius = 64.0f;
    cfg.maxConcurrentLoads = 64;
    cfg.maxConcurrentUnloads = 64;
    cfg.enablePrediction = false;
    cfg.cellDataDirectory = configuredRoot.wstring();
    cfg.allowPlaceholderCellLoad = false;

    ASSERT_TRUE(mgr.Initialize(cfg));

    const StreamingHandle first = mgr.LoadAssetBundle(firstBundleRoot.wstring());
    const StreamingHandle second = mgr.LoadAssetBundle(secondBundleRoot.wstring());
    ASSERT_TRUE(first.IsValid());
    ASSERT_TRUE(second.IsValid());

    auto update = [&]() {
        mgr.Update(0.016f, Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, -1.0f), Vec3(0.0f, 0.0f, 0.0f));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    };

    for (int i = 0; i < 50 && !mgr.IsCellLoaded(origin); ++i) {
        update();
    }
    ASSERT_TRUE(mgr.IsCellLoaded(origin));

    mgr.UnloadAssetBundle(second);
    EXPECT_FALSE(mgr.IsAssetBundleLoaded(second));
    EXPECT_TRUE(mgr.IsAssetBundleLoaded(first));
    for (int i = 0; i < 10; ++i) {
        update();
    }

    StreamingCellInfo info;
    ASSERT_TRUE(mgr.GetCellInfo(origin, info));
    EXPECT_TRUE(info.IsLoaded());
    EXPECT_TRUE(info.HasDiskData());
    EXPECT_FALSE(info.IsPlaceholder());
    EXPECT_FALSE(mgr.GetStatistics().HasFailures());
    EXPECT_FALSE(mgr.GetIOStatistics().HasFailures());

    mgr.UnloadAssetBundle(first);
    for (int i = 0; i < 50 && mgr.IsCellLoaded(origin); ++i) {
        update();
    }
    EXPECT_FALSE(mgr.IsCellLoaded(origin));

    mgr.Shutdown();
    std::filesystem::remove_all(tmp);
}

TEST(WorldStreaming, LoadsFromDiskViaAsyncIO) {
    using namespace Next::Streaming;
    using Next::Vec3;

    // Create a temp cell directory with at least the origin-adjacent cells.
    const std::filesystem::path tmp = std::filesystem::temp_directory_path() / "next_world_streaming_test_cells";
    std::filesystem::remove_all(tmp);

    for (int x = -4; x <= 4; ++x) {
        for (int z = -4; z <= 4; ++z) {
            WriteCellFile(tmp, x, z, 16 * 1024);
        }
    }

    StreamingManager mgr;
    StreamingManagerConfig cfg;
    cfg.memoryBudgetMB = 64;
    cfg.loadRadius = 128.0f;
    cfg.unloadRadius = 160.0f;
    cfg.maxConcurrentLoads = 64;
    cfg.maxConcurrentUnloads = 64;
    cfg.enablePrediction = false;
    cfg.cellDataDirectory = tmp.wstring();
    cfg.allowPlaceholderCellLoad = false;

    ASSERT_TRUE(mgr.Initialize(cfg));

    // Drive a few frames until at least one cell is loaded from disk.
    for (int i = 0; i < 50; ++i) {
        mgr.Update(0.016f, Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, -1.0f), Vec3(0.0f, 0.0f, 0.0f));
        if (!mgr.GetLoadedCells().empty()) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    auto loaded = mgr.GetLoadedCells();
    ASSERT_FALSE(loaded.empty());

    const auto loadedInfos = mgr.GetLoadedCellInfos();
    EXPECT_EQ(loadedInfos.size(), loaded.size());

    // Spot-check that one loaded cell has a real layer blob.
    StreamingCellInfo info;
    ASSERT_TRUE(mgr.GetCellInfo(loaded[0], info));
    EXPECT_EQ(info.coord, loaded[0]);
    EXPECT_TRUE(info.IsLoaded());
    EXPECT_TRUE(info.HasMemoryData());
    EXPECT_TRUE(info.HasDiskData());
    EXPECT_FALSE(info.IsPlaceholder());
    EXPECT_GT(info.memorySize, 0ull);
    EXPECT_TRUE(info.HasLayer(CellLayer::StaticMesh));
    EXPECT_EQ(info.layerCount, 1u);
    EXPECT_TRUE(info.HasLoadedLayers());
    EXPECT_EQ(info.loadedLayerCount, 1u);
    EXPECT_FLOAT_EQ(info.ClampedCellSize(), 64.0f);

    const IOStatistics ioStats = mgr.GetIOStatistics();
    EXPECT_TRUE(ioStats.HasReadBytes());
    EXPECT_GE(ioStats.totalBytesRead, info.dataSize);
    EXPECT_FALSE(ioStats.HasFailures());

    EXPECT_FALSE(mgr.GetCellInfo(CellCoord{999, 999}, info));
    EXPECT_FALSE(info.IsLoaded());

    CellData* cell = mgr.GetCell(loaded[0]);
    ASSERT_NE(cell, nullptr);
    ASSERT_TRUE(cell->IsLoaded());
    EXPECT_TRUE(cell->HasMemoryData());
    EXPECT_TRUE(cell->HasDiskData());
    EXPECT_FALSE(cell->IsPlaceholder());
    EXPECT_GT(cell->MemorySize(), 0ull);
    EXPECT_TRUE(cell->metadata.HasLayer(CellLayer::StaticMesh));
    EXPECT_EQ(cell->metadata.LayerCount(), 1u);
    EXPECT_TRUE(cell->HasLoadedLayers());
    EXPECT_EQ(cell->LoadedLayerCount(), 1u);

    auto it = cell->layers.find(CellLayer::StaticMesh);
    ASSERT_TRUE(it != cell->layers.end());
    EXPECT_TRUE(it->second.IsLoaded());
    EXPECT_TRUE(it->second.HasData());
    EXPECT_TRUE(it->second.HasSize());

    mgr.Shutdown();

    std::filesystem::remove_all(tmp);
}

TEST(WorldStreaming, LoadsLZ4CompressedCellViaAsyncIODecompression) {
    ExpectStreamingManagerLoadsCompressedCell(
        Next::Compression::Algorithm::LZ4,
        Next::Streaming::CompressionType::LZ4);
}

TEST(WorldStreaming, LoadsZstdCompressedCellViaAsyncIODecompression) {
    ExpectStreamingManagerLoadsCompressedCell(
        Next::Compression::Algorithm::Zstd,
        Next::Streaming::CompressionType::Zstd);
}

TEST(WorldStreaming, RejectsRawCellFileLargerThanMemoryBudgetBeforeAsyncRead) {
    using namespace Next::Streaming;
    using Next::Vec3;

    const CellCoord coord{0, 0};
    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "next_world_streaming_oversized_raw_cell";
    std::filesystem::remove_all(tmp);
    WriteCellFile(tmp, coord.x, coord.z, 2 * 1024 * 1024);

    StreamingManager mgr;
    StreamingManagerConfig cfg;
    cfg.memoryBudgetMB = 1;
    cfg.loadRadius = -1.0f;
    cfg.unloadRadius = 128.0f;
    cfg.maxConcurrentLoads = 4;
    cfg.maxConcurrentUnloads = 4;
    cfg.enablePrediction = false;
    cfg.cellDataDirectory = tmp.wstring();
    cfg.allowPlaceholderCellLoad = false;

    ASSERT_TRUE(mgr.Initialize(cfg));
    mgr.LoadCell(coord, 1.0f);
    mgr.Update(0.016f, Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, -1.0f), Vec3(0.0f, 0.0f, 0.0f));

    EXPECT_FALSE(mgr.IsCellLoaded(coord));
    StreamingCellInfo info;
    ASSERT_TRUE(mgr.GetCellInfo(coord, info));
    EXPECT_TRUE(info.IsError());
    EXPECT_FALSE(info.HasMemoryData());

    const StreamingStatistics stats = mgr.GetStatistics();
    EXPECT_EQ(stats.failedLoads, 1u);
    EXPECT_TRUE(stats.HasFailures());

    const IOStatistics ioStats = mgr.GetIOStatistics();
    EXPECT_EQ(ioStats.totalBytesRead, 0u);
    EXPECT_FALSE(ioStats.HasFailures());

    mgr.Shutdown();
    std::filesystem::remove_all(tmp);
}

TEST(WorldStreaming, RejectsCompressedCellDecompressedSizeLargerThanMemoryBudget) {
    using namespace Next::Streaming;
    using Next::Vec3;

    const CellCoord coord{0, 0};
    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "next_world_streaming_oversized_compressed_cell";
    std::filesystem::remove_all(tmp);
    WriteDeclaredCompressedCellFile(
        tmp,
        coord.x,
        coord.z,
        CompressionType::LZ4,
        16,
        2 * 1024 * 1024);

    StreamingManager mgr;
    StreamingManagerConfig cfg;
    cfg.memoryBudgetMB = 1;
    cfg.loadRadius = -1.0f;
    cfg.unloadRadius = 128.0f;
    cfg.maxConcurrentLoads = 4;
    cfg.maxConcurrentUnloads = 4;
    cfg.enablePrediction = false;
    cfg.cellDataDirectory = tmp.wstring();
    cfg.allowPlaceholderCellLoad = false;

    ASSERT_TRUE(mgr.Initialize(cfg));
    mgr.LoadCell(coord, 1.0f);

    StreamingCellInfo info;
    for (int i = 0; i < 200; ++i) {
        mgr.Update(0.016f, Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, -1.0f), Vec3(0.0f, 0.0f, 0.0f));
        if (mgr.GetCellInfo(coord, info) && info.IsError()) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    EXPECT_FALSE(mgr.IsCellLoaded(coord));
    ASSERT_TRUE(mgr.GetCellInfo(coord, info));
    EXPECT_TRUE(info.IsError());
    EXPECT_FALSE(info.HasMemoryData());

    const StreamingStatistics stats = mgr.GetStatistics();
    EXPECT_EQ(stats.failedLoads, 1u);
    EXPECT_TRUE(stats.HasFailures());

    const IOStatistics ioStats = mgr.GetIOStatistics();
    EXPECT_GT(ioStats.totalBytesRead, 0u);
    EXPECT_EQ(ioStats.totalBytesDecompressed, 0u);
    EXPECT_FALSE(ioStats.HasFailures());

    mgr.Shutdown();
    std::filesystem::remove_all(tmp);
}

TEST(WorldStreaming, LoadsCellsFromNestedDirectoryIndex) {
    using namespace Next::Streaming;
    using Next::Vec3;

    const CellCoord origin{0, 0};
    const std::filesystem::path tmp = std::filesystem::temp_directory_path() / "next_world_streaming_nested_cells";
    const std::filesystem::path nested = tmp / "region_0_0" / "lod0";
    std::filesystem::remove_all(tmp);
    WriteCellFile(nested, origin.x, origin.z, 4096);

    StreamingManager mgr;
    StreamingManagerConfig cfg;
    cfg.memoryBudgetMB = 64;
    cfg.loadRadius = 128.0f;
    cfg.unloadRadius = 160.0f;
    cfg.maxConcurrentLoads = 64;
    cfg.maxConcurrentUnloads = 64;
    cfg.enablePrediction = false;
    cfg.cellDataDirectory = tmp.wstring();
    cfg.allowPlaceholderCellLoad = false;

    ASSERT_TRUE(mgr.Initialize(cfg));

    for (int i = 0; i < 50 && !mgr.IsCellLoaded(origin); ++i) {
        mgr.Update(0.016f, Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, -1.0f), Vec3(0.0f, 0.0f, 0.0f));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    StreamingCellInfo info;
    ASSERT_TRUE(mgr.GetCellInfo(origin, info));
    EXPECT_TRUE(info.IsLoaded());
    EXPECT_TRUE(info.HasDiskData());
    EXPECT_FALSE(info.IsPlaceholder());
    EXPECT_FALSE(mgr.GetStatistics().HasFailures());
    EXPECT_FALSE(mgr.GetIOStatistics().HasFailures());

    mgr.Shutdown();
    std::filesystem::remove_all(tmp);
}

TEST(WorldStreaming, NormalizesWindowsStyleCellDirectoryOnPosix) {
#ifndef _WIN32
    using namespace Next::Streaming;
    using Next::Vec3;

    const std::filesystem::path tmp = std::filesystem::temp_directory_path() / "next_world_streaming_test_cells_backslash";
    std::filesystem::remove_all(tmp);

    for (int x = -2; x <= 2; ++x) {
        for (int z = -2; z <= 2; ++z) {
            WriteCellFile(tmp, x, z, 4096);
        }
    }

    std::wstring windowsStylePath = tmp.wstring();
    std::replace(windowsStylePath.begin(), windowsStylePath.end(), L'/', L'\\');

    StreamingManager mgr;
    StreamingManagerConfig cfg;
    cfg.memoryBudgetMB = 64;
    cfg.loadRadius = 128.0f;
    cfg.unloadRadius = 160.0f;
    cfg.maxConcurrentLoads = 64;
    cfg.maxConcurrentUnloads = 64;
    cfg.enablePrediction = false;
    cfg.cellDataDirectory = windowsStylePath;
    cfg.allowPlaceholderCellLoad = false;

    ASSERT_TRUE(mgr.Initialize(cfg));

    for (int i = 0; i < 50; ++i) {
        mgr.Update(0.016f, Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, -1.0f), Vec3(0.0f, 0.0f, 0.0f));
        if (!mgr.GetLoadedCells().empty()) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    EXPECT_FALSE(mgr.GetLoadedCells().empty());

    mgr.Shutdown();
    std::filesystem::remove_all(tmp);
#endif
}

TEST(WorldStreaming, UnloadsCellsOutsideRadius) {
    using namespace Next::Streaming;
    using Next::Vec3;

    StreamingManager mgr;
    StreamingManagerConfig cfg;
    cfg.memoryBudgetMB = 64;
    cfg.loadRadius = 128.0f;
    cfg.unloadRadius = 160.0f;
    cfg.maxConcurrentLoads = 64;
    cfg.maxConcurrentUnloads = 64;
    cfg.enablePrediction = false;

    ASSERT_TRUE(mgr.Initialize(cfg));

    mgr.Update(0.016f, Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, 0.0f));
    auto loaded0 = mgr.GetLoadedCells();
    ASSERT_FALSE(loaded0.empty());

    // Move the camera far enough that all previously loaded cells should be out of unload radius.
    mgr.Update(0.016f, Vec3(1024.0f, 0.0f, 0.0f), Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, 0.0f));
    auto loaded1 = mgr.GetLoadedCells();
    ASSERT_FALSE(loaded1.empty());

    std::unordered_set<CellCoord, CellCoordHash> set0(loaded0.begin(), loaded0.end());
    size_t intersection = 0;
    for (const auto& c : loaded1) {
        if (set0.count(c)) {
            intersection++;
        }
    }

    // The loaded set should largely change after a large teleport.
    EXPECT_LT(intersection, loaded0.size() / 4 + 1);

    mgr.Shutdown();
}

TEST(WorldStreaming, EvictsWhenOverBudget) {
    using namespace Next::Streaming;
    using Next::Vec3;

    StreamingManager mgr;
    StreamingManagerConfig cfg;
    cfg.memoryBudgetMB = 1;          // 1 MB budget
    cfg.loadRadius = 256.0f;         // load multiple cells
    cfg.unloadRadius = 512.0f;
    cfg.maxConcurrentLoads = 256;
    cfg.maxConcurrentUnloads = 256;
    cfg.enablePrediction = false;

    ASSERT_TRUE(mgr.Initialize(cfg));

    // A few updates to allow load + enforcement to run.
    for (int i = 0; i < 3; ++i) {
        mgr.Update(0.016f, Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, -1.0f), Vec3(0.0f, 0.0f, 0.0f));
    }

    // StreamingManager's framework load uses 256KB per cell; with a 1MB budget and 0.9 threshold,
    // eviction should kick in and keep utilization bounded.
    // Note: the eviction policy protects cells near the camera (default protected radius),
    // so utilization may clamp close to 1.0f for tiny budgets.
    EXPECT_LE(mgr.GetMemoryUtilization(), 1.05f);
    const EvictionPolicy::Statistics evictionStats = mgr.GetEvictionStatistics();
    EXPECT_TRUE(evictionStats.HasEvictions());
    EXPECT_TRUE(evictionStats.HasMemoryFreed());

    mgr.Shutdown();
}

TEST(WorldStreaming, LoadStartBudgetLimitsNewLoadsPerFrame) {
    using namespace Next::Streaming;
    using Next::Vec3;

    namespace fs = std::filesystem;
    const fs::path tmp = fs::temp_directory_path() / "next_stream_loadstart_budget";
    fs::remove_all(tmp);
    const std::array<CellCoord, 4> coords = {{{0, 0}, {1, 0}, {0, 1}, {1, 1}}};
    for (const CellCoord& c : coords) {
        WriteCellFile(tmp, c.x, c.z, 2048);
    }

    StreamingManager mgr;
    StreamingManagerConfig cfg;
    cfg.memoryBudgetMB = 64;
    cfg.loadRadius = 0.0f;          // don't auto-queue cells by proximity
    cfg.unloadRadius = 100000.0f;   // and don't auto-unload the requested cells
    cfg.enablePrediction = false;
    cfg.maxConcurrentLoads = 16;    // concurrency is not the limiter here...
    cfg.maxLoadStartsPerFrame = 1;  // ...the per-frame admission budget is
    cfg.cellDataDirectory = tmp.wstring();
    cfg.allowPlaceholderCellLoad = false;
    ASSERT_TRUE(mgr.Initialize(cfg));

    for (const CellCoord& c : coords) {
        mgr.LoadCell(c, 1.0f);
    }

    // Four loads are queued, but a single Update may start only one read+decompress pipeline.
    mgr.Update(0.016f, Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, -1.0f), Vec3(0.0f, 0.0f, 0.0f));
    EXPECT_EQ(mgr.GetStatistics().loadStartsThisFrame, 1u);

    mgr.Shutdown();
    fs::remove_all(tmp);
}

TEST(WorldStreaming, UploadByteBudgetLimitsCommittedCellsPerFrame) {
    using namespace Next::Streaming;
    using Next::Vec3;

    namespace fs = std::filesystem;
    const fs::path tmp = fs::temp_directory_path() / "next_stream_upload_budget";
    fs::remove_all(tmp);
    const std::array<CellCoord, 5> coords = {{{0, 0}, {1, 0}, {0, 1}, {1, 1}, {2, 0}}};
    for (const CellCoord& c : coords) {
        WriteCellFile(tmp, c.x, c.z, 4096);
    }

    StreamingManager mgr;
    StreamingManagerConfig cfg;
    cfg.memoryBudgetMB = 64;
    cfg.loadRadius = 0.0f;
    cfg.unloadRadius = 100000.0f;
    cfg.enablePrediction = false;
    cfg.maxConcurrentLoads = 16;     // let every cell read+decompress concurrently...
    cfg.maxUploadBytesPerFrame = 1;  // ...but commit at most one (1 byte < any cell) per frame
    cfg.cellDataDirectory = tmp.wstring();
    cfg.allowPlaceholderCellLoad = false;
    ASSERT_TRUE(mgr.Initialize(cfg));

    for (const CellCoord& c : coords) {
        mgr.LoadCell(c, 1.0f);
    }

    auto loadedCount = [&]() {
        uint32_t n = 0;
        for (const CellCoord& c : coords) {
            if (mgr.IsCellLoaded(c)) {
                ++n;
            }
        }
        return n;
    };

    uint32_t prev = 0;
    for (int i = 0; i < 800 && loadedCount() < coords.size(); ++i) {
        mgr.Update(0.016f, Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, -1.0f), Vec3(0.0f, 0.0f, 0.0f));
        const uint32_t now = loadedCount();
        // Under a sub-cell upload budget, at most one cell may commit per frame, regardless
        // of how many async loads finished decompressing this frame.
        EXPECT_LE(now - prev, 1u);
        prev = now;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // The budget defers commits; it never drops them. Everything loads eventually.
    EXPECT_EQ(loadedCount(), static_cast<uint32_t>(coords.size()));

    mgr.Shutdown();
    fs::remove_all(tmp);
}
