#include "next/runtime/asset/package_container.h"
#include "next/compression/compression.h"
#include "next/foundation/logger.h"
#include "next/profiler/cpu_scope.h"
#include <array>
#include <fstream>
#include <algorithm>
#include <cstring>
#include <exception>
#include <limits>

namespace Next {

namespace {

constexpr uint64_t kCrc64EcmaPolynomial = 0x42F0E1EBA9EA3693ULL;
constexpr size_t kChecksumChunkSize = 64 * 1024;

size_t AssetPayloadOffset(AssetType type) {
    switch (type) {
        case AssetType::Mesh:
            return sizeof(MeshHeader);
        case AssetType::Texture:
            return sizeof(TextureHeader);
        case AssetType::Material:
            return sizeof(MaterialHeader);
        default:
            return 0;
    }
}

bool FixedStringIsNullTerminated(const char* value, size_t size) {
    return std::memchr(value, '\0', size) != nullptr;
}

uint64_t UpdateCRC64(uint64_t crc, const uint8_t* bytes, size_t size) {
    if (!bytes && size != 0) {
        return crc;
    }

    for (size_t i = 0; i < size; ++i) {
        crc ^= static_cast<uint64_t>(bytes[i]) << 56;
        for (int j = 0; j < 8; ++j) {
            if ((crc & 0x8000000000000000ULL) != 0) {
                crc = (crc << 1) ^ kCrc64EcmaPolynomial;
            } else {
                crc <<= 1;
            }
        }
    }

    return crc;
}

bool SeekAbsolute(std::ifstream& file, size_t offset) {
    if (offset > static_cast<size_t>(std::numeric_limits<std::streamoff>::max())) {
        return false;
    }

    file.clear();
    file.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    return static_cast<bool>(file);
}

bool ReadExact(std::ifstream& file, void* output, size_t size) {
    if (size == 0) {
        return true;
    }
    if (size > static_cast<size_t>(std::numeric_limits<std::streamsize>::max())) {
        return false;
    }

    const auto requested = static_cast<std::streamsize>(size);
    file.read(reinterpret_cast<char*>(output), requested);
    return file.gcount() == requested && !file.bad();
}

bool CalculatePackagePayloadChecksum(std::ifstream& file,
                                     size_t payloadOffset,
                                     size_t payloadSize,
                                     uint64_t& outChecksum) {
    if (!SeekAbsolute(file, payloadOffset)) {
        return false;
    }

    std::array<uint8_t, kChecksumChunkSize> buffer{};
    size_t remaining = payloadSize;
    uint64_t crc = 0;

    while (remaining > 0) {
        const size_t chunkSize = std::min(remaining, buffer.size());
        if (!ReadExact(file, buffer.data(), chunkSize)) {
            return false;
        }

        crc = UpdateCRC64(crc, buffer.data(), chunkSize);
        remaining -= chunkSize;
    }

    outChecksum = crc;
    return true;
}

} // namespace

PackageContainer::~PackageContainer() {
    NEXT_LOG_DEBUG("Package container destroyed: %s", name_.c_str());

    // mappedFile_ owns memory via MappedFile::ownedData (unique_ptr). Do NOT delete mappedFile_->data here,
    // otherwise we'll double-free when ownedData is destroyed.
    mappedFile_.reset();
    dataSection_ = nullptr;
    dataSectionSize_ = 0;
}

std::shared_ptr<PackageContainer> PackageContainer::LoadFromFile(const std::string& filePath) {
    NEXT_CPU_SCOPE("PackageContainer::LoadFromFile");
    
    std::shared_ptr<PackageContainer> container(new PackageContainer());
    if (!container->Initialize(filePath)) {
        NEXT_LOG_ERROR("Failed to initialize package container from: %s", filePath.c_str());
        return nullptr;
    }
    
    return container;
}

bool PackageContainer::Initialize(const std::string& filePath) {
    filePath_ = filePath;
    
    // Extract name from file path.
    const size_t slashPos = filePath.find_last_of("/\\");
    const size_t nameStart = slashPos == std::string::npos ? 0 : slashPos + 1;
    const size_t dotPos = filePath.find_last_of('.');
    if (dotPos != std::string::npos && dotPos > nameStart) {
        name_ = filePath.substr(nameStart, dotPos - nameStart);
    } else {
        name_ = filePath.substr(nameStart);
    }
    
    // For CP3, we'll use simple file I/O instead of memory mapping
    // In a real implementation, we would use memory-mapped files
    
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        NEXT_LOG_ERROR("Failed to open package file: %s", filePath.c_str());
        return false;
    }

    file.seekg(0, std::ios::end);
    const std::streampos endPos = file.tellg();
    if (endPos < static_cast<std::streamoff>(sizeof(PackageHeader))) {
        NEXT_LOG_ERROR("Package file too small: %s", filePath.c_str());
        return false;
    }
    const size_t fileSize = static_cast<size_t>(endPos);
    file.seekg(0, std::ios::beg);
    
    // Read package header
    file.read(reinterpret_cast<char*>(&packageHeader_), sizeof(PackageHeader));
    if (!file) {
        NEXT_LOG_ERROR("Failed to read package header: %s", filePath.c_str());
        return false;
    }
    
    if (!packageHeader_.Validate()) {
        NEXT_LOG_ERROR("Invalid package header in: %s", filePath.c_str());
        return false;
    }

    if (packageHeader_.checksum != 0) {
        uint64_t calculatedChecksum = 0;
        if (!CalculatePackagePayloadChecksum(file,
                                             sizeof(PackageHeader),
                                             fileSize - sizeof(PackageHeader),
                                             calculatedChecksum)) {
            NEXT_LOG_ERROR("Failed to read package payload for checksum: %s", filePath.c_str());
            return false;
        }

        if (calculatedChecksum != packageHeader_.checksum) {
            NEXT_LOG_ERROR("Package checksum mismatch for %s (expected: %llx, got: %llx)",
                           filePath.c_str(),
                           packageHeader_.checksum,
                           calculatedChecksum);
            return false;
        }

        file.clear();
    }

    const size_t indexOffset = packageHeader_.indexOffset;
    const size_t dataOffset = packageHeader_.dataOffset;
    const size_t assetCount = packageHeader_.assetCount;
    if (indexOffset > fileSize || dataOffset > fileSize) {
        NEXT_LOG_ERROR("Package offsets outside file bounds: %s", filePath.c_str());
        return false;
    }

    if (assetCount == 0) {
        NEXT_LOG_ERROR("Package has no assets: %s", filePath.c_str());
        return false;
    }

    if (assetCount > std::numeric_limits<size_t>::max() / sizeof(AssetEntry)) {
        NEXT_LOG_ERROR("Package asset index too large: %s", filePath.c_str());
        return false;
    }

    const size_t assetIndexBytes = assetCount * sizeof(AssetEntry);
    if (assetIndexBytes > dataOffset - indexOffset) {
        NEXT_LOG_ERROR("Package asset index overlaps data section: %s", filePath.c_str());
        return false;
    }

    if (assetIndexBytes > static_cast<size_t>(std::numeric_limits<std::streamsize>::max())) {
        NEXT_LOG_ERROR("Package asset index exceeds readable stream size: %s", filePath.c_str());
        return false;
    }
    
    NEXT_LOG_INFO("Loading package: %s (version: %u, assets: %u)", 
                 name_.c_str(), packageHeader_.version, packageHeader_.assetCount);
    
    // Read asset index
    try {
        assetEntries_.resize(assetCount);
    } catch (const std::exception& e) {
        NEXT_LOG_ERROR("Failed to allocate package asset index: %s", e.what());
        return false;
    }

    if (!SeekAbsolute(file, indexOffset) ||
        !ReadExact(file, assetEntries_.data(), assetIndexBytes)) {
        NEXT_LOG_ERROR("Failed to read package asset index: %s", filePath.c_str());
        return false;
    }
    
    // Validate asset entries before trusting their offsets for allocation or reads.
    dataSectionSize_ = 0;
    nameToIndex_.clear();
    const size_t packageDataBytes = fileSize - dataOffset;
    // Upper bound for any single allocation derived from package metadata; also the
    // cap for a compressed entry's declared decompressed size.
    constexpr size_t MAX_REASONABLE_DATA_SIZE = 4ULL * 1024ULL * 1024ULL * 1024ULL; // 4GB
    for (size_t i = 0; i < assetEntries_.size(); ++i) {
        const AssetEntry& entry = assetEntries_[i];
        const uint32_t typeValue = static_cast<uint32_t>(entry.assetType);
        if (typeValue == static_cast<uint32_t>(AssetType::Unknown) ||
            typeValue >= static_cast<uint32_t>(AssetType::Count)) {
            NEXT_LOG_ERROR("Invalid asset type in package index: %u", typeValue);
            return false;
        }

        if (!FixedStringIsNullTerminated(entry.name, sizeof(entry.name)) || entry.name[0] == '\0') {
            NEXT_LOG_ERROR("Invalid asset name in package index");
            return false;
        }

        // minAssetSize is the minimum *logical* (decompressed) size: enough to hold the
        // type-specific header. assetSize is the *stored* size, which may be smaller than
        // this when the entry is compressed.
        const size_t minAssetSize = AssetPayloadOffset(entry.assetType);
        const bool isCompressed =
            entry.compressionAlgorithm != static_cast<uint32_t>(Compression::Algorithm::None);

        if (isCompressed) {
            const auto algorithm = static_cast<Compression::Algorithm>(entry.compressionAlgorithm);
            if (!Compression::IsAvailable(algorithm)) {
                NEXT_LOG_ERROR("Asset compressed with unavailable codec (%u): %s",
                               entry.compressionAlgorithm, entry.name);
                return false;
            }
            // Stored bytes are the compressed payload, so they must equal assetSize.
            if (entry.compressedSize != entry.assetSize) {
                NEXT_LOG_ERROR("Compressed asset stored-size mismatch: %s", entry.name);
                return false;
            }
            if (entry.decompressedSize < minAssetSize) {
                NEXT_LOG_ERROR("Compressed asset decompressed size too small: %s", entry.name);
                return false;
            }
            // No explicit upper bound is needed here: decompressedSize is a uint32_t and is
            // therefore inherently below the 4 GiB allocation cap (MAX_REASONABLE_DATA_SIZE).
        } else {
            if (entry.assetSize < minAssetSize) {
                NEXT_LOG_ERROR("Asset entry too small in package index: %s", entry.name);
                return false;
            }
            if (entry.compressedSize != 0) {
                NEXT_LOG_ERROR("Uncompressed asset entry has nonzero compressedSize: %s", entry.name);
                return false;
            }
            if (entry.decompressedSize != entry.assetSize) {
                NEXT_LOG_ERROR("Asset entry decompressed size mismatch: %s", entry.name);
                return false;
            }
        }

        const size_t entryOffset = entry.dataOffset;
        const size_t entrySize = entry.assetSize;
        if (entryOffset > packageDataBytes || entrySize > packageDataBytes - entryOffset) {
            NEXT_LOG_ERROR("Asset entry outside data section: %s", entry.name);
            return false;
        }

        const std::string entryName(entry.name);
        if (nameToIndex_.find(entryName) != nameToIndex_.end()) {
            NEXT_LOG_ERROR("Duplicate asset name in package: %s", entryName.c_str());
            return false;
        }
        nameToIndex_[entryName] = i;

        dataSectionSize_ = std::max(dataSectionSize_, entryOffset + entrySize);
    }

    // Security: Validate dataSectionSize_ to prevent allocation issues
    if (dataSectionSize_ == 0) {
        NEXT_LOG_ERROR("Invalid data section size: 0 bytes");
        return false;
    }
    if (dataSectionSize_ > MAX_REASONABLE_DATA_SIZE) {
        NEXT_LOG_ERROR("Data section size too large: %zu bytes (max: %zu bytes)",
                      dataSectionSize_, MAX_REASONABLE_DATA_SIZE);
        return false;
    }

    NEXT_LOG_DEBUG("Package data section size: %zu bytes", dataSectionSize_);

    // For CP3, we read the data section into an owned buffer. A later implementation can swap this for mmap.
    try {
        mappedFile_ = std::make_unique<MappedFile>();
        mappedFile_->size = dataSectionSize_;
        mappedFile_->ownedData = std::make_unique<uint8_t[]>(dataSectionSize_);
        mappedFile_->data = mappedFile_->ownedData.get();
    } catch (const std::exception& e) {
        NEXT_LOG_ERROR("Failed to allocate memory for package data: %s", e.what());
        mappedFile_.reset();
        return false;
    }

    if (!SeekAbsolute(file, dataOffset) ||
        !ReadExact(file, mappedFile_->data, dataSectionSize_)) {
        NEXT_LOG_ERROR("Failed to read package data section");
        mappedFile_.reset();
        dataSection_ = nullptr;
        dataSectionSize_ = 0;
        return false;
    }

    dataSection_ = reinterpret_cast<const uint8_t*>(mappedFile_->data);
    
    file.close();
    
    NEXT_LOG_INFO("Package loaded successfully: %s with %zu assets",
                 name_.c_str(), assetEntries_.size());
    
    return true;
}

bool PackageContainer::HasAsset(const std::string& assetName) const {
    return nameToIndex_.find(assetName) != nameToIndex_.end();
}

AssetType PackageContainer::GetAssetType(const std::string& assetName) const {
    auto it = nameToIndex_.find(assetName);
    if (it == nameToIndex_.end()) {
        return AssetType::Unknown;
    }
    
    return assetEntries_[it->second].assetType;
}

const AssetEntry* PackageContainer::GetAssetEntry(const std::string& assetName) const {
    auto it = nameToIndex_.find(assetName);
    if (it == nameToIndex_.end()) {
        return nullptr;
    }
    
    return &assetEntries_[it->second];
}

bool PackageContainer::ReadAssetData(const std::string& assetName, std::vector<uint8_t>& outData) const {
    NEXT_CPU_SCOPE("PackageContainer::ReadAssetData");
    
    auto it = nameToIndex_.find(assetName);
    if (it == nameToIndex_.end()) {
        NEXT_LOG_ERROR("Asset not found in package: %s", assetName.c_str());
        return false;
    }
    
    const AssetEntry& entry = assetEntries_[it->second];
    
    const size_t entryOffset = entry.dataOffset;
    const size_t storedSize = entry.assetSize;
    if (entryOffset > dataSectionSize_ || storedSize > dataSectionSize_ - entryOffset) {
        NEXT_LOG_ERROR("Asset data out of bounds: %s", assetName.c_str());
        return false;
    }

    const uint8_t* storedBytes = dataSection_ + entryOffset;
    const bool isCompressed =
        entry.compressionAlgorithm != static_cast<uint32_t>(Compression::Algorithm::None);
    if (isCompressed) {
        // decompressedSize is a uint32_t, so the allocation below is inherently bounded.
        const size_t decompressedSize = entry.decompressedSize;
        outData.resize(decompressedSize);
        const Compression::Result result = Compression::Decompress(
            static_cast<Compression::Algorithm>(entry.compressionAlgorithm),
            storedBytes,
            storedSize,
            outData.data(),
            outData.size());
        if (!result.Succeeded() || result.bytesWritten != decompressedSize) {
            NEXT_LOG_ERROR("Failed to decompress asset: %s (%s)",
                           assetName.c_str(), result.message.c_str());
            return false;
        }
    } else {
        outData.resize(storedSize);
        memcpy(outData.data(), storedBytes, storedSize);
    }

    if (outData.size() < sizeof(AssetHeader)) {
        NEXT_LOG_ERROR("Asset data too small for header: %s", assetName.c_str());
        return false;
    }

    AssetHeader header{};
    memcpy(&header, outData.data(), sizeof(AssetHeader));
    if (!header.Validate()) {
        NEXT_LOG_ERROR("Invalid asset header in package data: %s", assetName.c_str());
        return false;
    }

    if (header.assetType != entry.assetType) {
        NEXT_LOG_ERROR("Asset type mismatch between package index and data: %s", assetName.c_str());
        return false;
    }

    if (std::strcmp(header.name, entry.name) != 0) {
        NEXT_LOG_ERROR("Asset name mismatch between package index and data: %s", assetName.c_str());
        return false;
    }

    const size_t payloadOffset = AssetPayloadOffset(header.assetType);
    if (payloadOffset == 0 ||
        outData.size() < payloadOffset ||
        outData.size() - payloadOffset != header.dataSize) {
        NEXT_LOG_ERROR("Asset payload size mismatch: %s", assetName.c_str());
        return false;
    }

    if (header.checksum != 0 && !ValidateAssetChecksum(header, outData.data() + payloadOffset)) {
        return false;
    }
    
    NEXT_LOG_DEBUG("Read asset data: %s (%u bytes)", assetName.c_str(), entry.assetSize);
    return true;
}

bool PackageContainer::ReadAssetHeader(const std::string& assetName, AssetHeader& outHeader) const {
    std::vector<uint8_t> assetData;
    if (!ReadAssetData(assetName, assetData)) {
        return false;
    }
    
    if (assetData.size() < sizeof(AssetHeader)) {
        NEXT_LOG_ERROR("Asset data too small for header: %s", assetName.c_str());
        return false;
    }
    
    memcpy(&outHeader, assetData.data(), sizeof(AssetHeader));
    return true;
}

std::vector<std::string> PackageContainer::GetAssetNames() const {
    std::vector<std::string> names;
    names.reserve(assetEntries_.size());
    
    for (const auto& entry : assetEntries_) {
        names.push_back(entry.name);
    }
    
    return names;
}

std::vector<std::string> PackageContainer::GetAssetsByType(AssetType type) const {
    std::vector<std::string> names;
    
    for (const auto& entry : assetEntries_) {
        if (entry.assetType == type) {
            names.push_back(entry.name);
        }
    }
    
    return names;
}

bool PackageContainer::Validate() const {
    if (!packageHeader_.Validate()) {
        return false;
    }
    
    if (assetEntries_.size() != packageHeader_.assetCount) {
        return false;
    }
    
    // Check for duplicate names
    std::unordered_map<std::string, bool> seenNames;
    for (const auto& entry : assetEntries_) {
        if (seenNames.find(entry.name) != seenNames.end()) {
            NEXT_LOG_ERROR("Duplicate asset name in package: %s", entry.name);
            return false;
        }
        seenNames[entry.name] = true;
    }
    
    return true;
}

} // namespace Next
