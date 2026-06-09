#include "next/runtime/asset/asset_manager.h"
#include "next/runtime/asset/package_container.h"
#include "next/runtime/asset/asset_types.h"
#include "next/jobsystem/job_system.h"
#include "next/foundation/logger.h"
#include "next/profiler/profiler.h"
#include "next/profiler/cpu_scope.h"
#include <algorithm>
#include <cstddef>
#include <fstream>
#include <cstring>
#include <exception>
#include <filesystem>
#include <limits>
#include <utility>

namespace Next {

namespace {

constexpr uint32_t kDxgiFormatR8G8B8A8Unorm = 28;

bool MultiplySizeChecked(size_t lhs, size_t rhs, size_t& out) {
    if (lhs != 0 && rhs > std::numeric_limits<size_t>::max() / lhs) {
        out = 0;
        return false;
    }

    out = lhs * rhs;
    return true;
}

bool AddSizeChecked(size_t lhs, size_t rhs, size_t& out) {
    if (rhs > std::numeric_limits<size_t>::max() - lhs) {
        out = 0;
        return false;
    }

    out = lhs + rhs;
    return true;
}

bool ValidateMeshPayloadSize(const MeshHeader& header,
                             size_t payloadSize,
                             const std::string& storageKey) {
    if (payloadSize != static_cast<size_t>(header.common.dataSize)) {
        NEXT_LOG_ERROR("Mesh payload size mismatch: %s (declared: %u, actual: %zu)",
                       storageKey.c_str(),
                       header.common.dataSize,
                       payloadSize);
        return false;
    }

    size_t vertexBytes = 0;
    const size_t indexSize =
        header.indexType == 0 ? sizeof(uint16_t) :
        header.indexType == 1 ? sizeof(uint32_t) : 0;
    if (indexSize == 0) {
        NEXT_LOG_ERROR("Mesh has invalid index type: %s (%u)", storageKey.c_str(), header.indexType);
        return false;
    }

    size_t indexBytes = 0;
    size_t submeshBytes = 0;
    size_t requiredBytes = 0;
    if (!MultiplySizeChecked(static_cast<size_t>(header.vertexCount),
                             static_cast<size_t>(header.vertexStride),
                             vertexBytes) ||
        !MultiplySizeChecked(static_cast<size_t>(header.indexCount),
                             indexSize,
                             indexBytes) ||
        !MultiplySizeChecked(static_cast<size_t>(header.materialCount),
                             sizeof(uint32_t) * 2u,
                             submeshBytes) ||
        !AddSizeChecked(vertexBytes, indexBytes, requiredBytes) ||
        !AddSizeChecked(requiredBytes, submeshBytes, requiredBytes)) {
        NEXT_LOG_ERROR("Mesh payload size overflow: %s", storageKey.c_str());
        return false;
    }

    if (payloadSize < requiredBytes) {
        NEXT_LOG_ERROR("Mesh payload too small: %s (required: %zu, actual: %zu)",
                       storageKey.c_str(),
                       requiredBytes,
                       payloadSize);
        return false;
    }

    return true;
}

bool ValidateMaterialPayloadSize(const MaterialHeader& header,
                                 size_t payloadSize,
                                 const std::string& storageKey) {
    if (payloadSize != static_cast<size_t>(header.common.dataSize)) {
        NEXT_LOG_ERROR("Material payload size mismatch: %s (declared: %u, actual: %zu)",
                       storageKey.c_str(),
                       header.common.dataSize,
                       payloadSize);
        return false;
    }

    size_t textureBytes = 0;
    size_t parameterBytes = 0;
    size_t requiredBytes = 0;
    if (!MultiplySizeChecked(static_cast<size_t>(header.textureCount),
                             sizeof(TextureRef),
                             textureBytes) ||
        !MultiplySizeChecked(static_cast<size_t>(header.parameterCount),
                             sizeof(MaterialParam),
                             parameterBytes) ||
        !AddSizeChecked(textureBytes, parameterBytes, requiredBytes)) {
        NEXT_LOG_ERROR("Material payload size overflow: %s", storageKey.c_str());
        return false;
    }

    if (payloadSize < requiredBytes) {
        NEXT_LOG_ERROR("Material payload too small: %s (required: %zu, actual: %zu)",
                       storageKey.c_str(),
                       requiredBytes,
                       payloadSize);
        return false;
    }

    return true;
}

uint32_t TextureBytesPerPixel(const TextureHeader& header) {
    if ((header.flags & TextureHeader::COMPRESSED) != 0) {
        return 0;
    }

    switch (header.format) {
        case kDxgiFormatR8G8B8A8Unorm:
            return 4;
        default:
            return 0;
    }
}

bool ValidateTexturePayloadSize(const TextureHeader& header,
                                size_t payloadSize,
                                const std::string& storageKey) {
    if (payloadSize != static_cast<size_t>(header.common.dataSize)) {
        NEXT_LOG_ERROR("Texture payload size mismatch: %s (declared: %u, actual: %zu)",
                       storageKey.c_str(),
                       header.common.dataSize,
                       payloadSize);
        return false;
    }

    const uint32_t bytesPerPixel = TextureBytesPerPixel(header);
    if (bytesPerPixel == 0) {
        return true;
    }

    const uint32_t depth = header.depth == 0 ? 1u : header.depth;
    size_t requiredBytes = bytesPerPixel;
    if (!MultiplySizeChecked(requiredBytes, static_cast<size_t>(header.width), requiredBytes) ||
        !MultiplySizeChecked(requiredBytes, static_cast<size_t>(header.height), requiredBytes) ||
        !MultiplySizeChecked(requiredBytes, static_cast<size_t>(depth), requiredBytes) ||
        !MultiplySizeChecked(requiredBytes, static_cast<size_t>(header.arraySize), requiredBytes)) {
        NEXT_LOG_ERROR("Texture payload size overflow: %s", storageKey.c_str());
        return false;
    }

    if (payloadSize < requiredBytes) {
        NEXT_LOG_ERROR("Texture payload too small: %s (required: %zu, actual: %zu)",
                       storageKey.c_str(),
                       requiredBytes,
                       payloadSize);
        return false;
    }

    return true;
}

} // namespace

// AssetData base class - holds actual asset data with reference counting
class AssetData {
public:
    AssetData(uint64_t id, AssetType type, std::string packageName, std::string name, size_t payloadSize)
        : id_(id)
        , type_(type)
        , packageName_(std::move(packageName))
        , name_(std::move(name))
        , refCount_(0)
        , payloadSize_(payloadSize) {}

    virtual ~AssetData() = default;

    uint64_t GetID() const { return id_; }
    AssetType GetType() const { return type_; }
    const std::string& GetPackageName() const { return packageName_; }
    const std::string& GetName() const { return name_; }

    void AddRef() { refCount_++; }
    uint32_t Release() { return --refCount_; }
    uint32_t GetRefCount() const { return refCount_.load(); }
    size_t GetPayloadSize() const { return payloadSize_; }

    virtual const void* GetPayload() const = 0;

private:
    uint64_t id_;
    AssetType type_;
    std::string packageName_;
    std::string name_;
    std::atomic<uint32_t> refCount_;
    size_t payloadSize_;
};

// Concrete asset data classes
class MeshData : public AssetData {
public:
    MeshData(uint64_t id, std::string packageName, const MeshHeader& header, const void* payload, size_t payloadSize)
        : AssetData(id, AssetType::Mesh, std::move(packageName), header.common.name, payloadSize)
        , header_(header) {
        data_.resize(payloadSize);
        memcpy(data_.data(), payload, payloadSize);
    }

    const void* GetPayload() const override { return data_.data(); }
    const MeshHeader& GetHeader() const { return header_; }

private:
    MeshHeader header_;
    std::vector<uint8_t> data_;
};

class TextureData : public AssetData {
public:
    TextureData(uint64_t id, std::string packageName, const TextureHeader& header, const void* payload, size_t payloadSize)
        : AssetData(id, AssetType::Texture, std::move(packageName), header.common.name, payloadSize)
        , header_(header) {
        data_.resize(payloadSize);
        memcpy(data_.data(), payload, payloadSize);
    }

    const void* GetPayload() const override { return data_.data(); }
    const TextureHeader& GetHeader() const { return header_; }

private:
    TextureHeader header_;
    std::vector<uint8_t> data_;
};

class MaterialData : public AssetData {
public:
    MaterialData(uint64_t id, std::string packageName, const MaterialHeader& header, const void* payload, size_t payloadSize)
        : AssetData(id, AssetType::Material, std::move(packageName), header.common.name, payloadSize)
        , header_(header) {
        data_.resize(payloadSize);
        memcpy(data_.data(), payload, payloadSize);
    }

    const void* GetPayload() const override { return data_.data(); }
    const MaterialHeader& GetHeader() const { return header_; }

private:
    MaterialHeader header_;
    std::vector<uint8_t> data_;
};

AssetManager& AssetManager::Instance() {
    static AssetManager instance;
    return instance;
}

bool AssetManager::Initialize() {
    NEXT_LOG_INFO("AssetManager initialized");
    return true;
}

void AssetManager::Shutdown() {
    NEXT_CPU_SCOPE("AssetManager::Shutdown");
    
    NEXT_LOG_INFO("AssetManager shutting down, unloading %zu assets", loadedAssetsCount_.load());
    
    {
        std::lock_guard<std::mutex> callbackLock(callbackMutex_);
        completedLoads_.clear();
    }

    // Unload all assets
    {
        std::lock_guard<std::mutex> lock(mutex_);
        
        for (auto& [name, data] : loadedAssets_) {
            NEXT_LOG_DEBUG("Unloading asset: %s", name.c_str());
        }
        
        loadedAssets_.clear();
        idToName_.clear();
        loadedPackages_.clear();
        loadedAssetsCount_ = 0;
        totalMemory_ = 0;
    }
    
    NEXT_LOG_INFO("AssetManager shutdown complete");
}

bool AssetManager::LoadPackage(const std::string& packagePath) {
    NEXT_CPU_SCOPE("AssetManager::LoadPackage");
    
    NEXT_LOG_INFO("Loading package: %s", packagePath.c_str());

    // Fast-path: if package already loaded, just bump refcount and avoid IO.
    std::string expectedName;
    try {
        expectedName = std::filesystem::path(packagePath).stem().string();
    } catch (...) {
        expectedName.clear();
    }

    if (!expectedName.empty()) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = loadedPackages_.find(expectedName);
        if (it != loadedPackages_.end()) {
            it->second.refCount++;
            NEXT_LOG_DEBUG("Package ref++: %s (ref=%u)", expectedName.c_str(), it->second.refCount);
            return true;
        }
    }

    auto package = LoadPackageInternal(packagePath);
    if (!package) {
        NEXT_LOG_ERROR("Failed to load package: %s", packagePath.c_str());
        failedLoads_++;
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = loadedPackages_.find(package->GetName());
    if (it != loadedPackages_.end()) {
        it->second.refCount++;
        NEXT_LOG_DEBUG("Package ref++ (race): %s (ref=%u)", package->GetName().c_str(), it->second.refCount);
        return true;
    }

    LoadedPackageEntry entry;
    entry.package = package;
    entry.refCount = 1;
    loadedPackages_[package->GetName()] = std::move(entry);

    NEXT_LOG_INFO("Package loaded successfully: %s (%u assets, ref=1)",
                  package->GetName().c_str(), package->GetAssetCount());
    return true;
}

void AssetManager::UnloadPackage(const std::string& packageName) {
    NEXT_CPU_SCOPE("AssetManager::UnloadPackage");
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = loadedPackages_.find(packageName);
    if (it == loadedPackages_.end()) {
        NEXT_LOG_WARNING("Package not found: %s", packageName.c_str());
        return;
    }

    if (it->second.refCount > 1) {
        it->second.refCount--;
        NEXT_LOG_DEBUG("Package ref--: %s (ref=%u)", packageName.c_str(), it->second.refCount);
        return;
    }

    // Final release: drop the package container. Assets already loaded are independent copies; keep them if referenced.
    loadedPackages_.erase(it);

    // Garbage collect unreferenced assets that originated from this package.
    for (auto aIt = loadedAssets_.begin(); aIt != loadedAssets_.end(); ) {
        const std::shared_ptr<AssetData>& a = aIt->second;
        if (a && a->GetPackageName() == packageName) {
            if (a->GetRefCount() == 0) {
                totalMemory_ -= a->GetPayloadSize();
                loadedAssetsCount_--;
                idToName_.erase(a->GetID());
                aIt = loadedAssets_.erase(aIt);
                continue;
            }
        }
        ++aIt;
    }

    NEXT_LOG_INFO("Package unloaded: %s (ref=0)", packageName.c_str());
}

AssetHandle AssetManager::LoadAssetSync(const std::string& assetName) {
    NEXT_CPU_SCOPE("AssetManager::LoadAssetSync");
    
    NEXT_LOG_DEBUG("Loading asset synchronously: %s", assetName.c_str());
    
    auto splitKey = [](const std::string& key, std::string& outPkg, std::string& outLocal) -> bool {
        const size_t pos = key.find("::");
        if (pos == std::string::npos) {
            return false;
        }
        outPkg = key.substr(0, pos);
        outLocal = key.substr(pos + 2);
        return !outPkg.empty() && !outLocal.empty();
    };

    auto resolveLoadedKey = [&](const std::string& key) -> std::string {
        // Exact match (already-qualified).
        auto itExact = loadedAssets_.find(key);
        if (itExact != loadedAssets_.end()) {
            return key;
        }

        // Unqualified: resolve by local name, but only if unique among loaded assets.
        std::string pkg;
        std::string local;
        if (splitKey(key, pkg, local)) {
            return {};
        }

        std::string foundKey;
        for (const auto& [k, a] : loadedAssets_) {
            if (!a) {
                continue;
            }
            if (a->GetName() == key) {
                if (!foundKey.empty() && foundKey != k) {
                    // Ambiguous: multiple loaded assets share the same local name.
                    return {};
                }
                foundKey = k;
            }
        }
        return foundKey;
    };

    // Check if already loaded
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const std::string loadedKey = resolveLoadedKey(assetName);
        if (!loadedKey.empty()) {
            auto it = loadedAssets_.find(loadedKey);
            if (it != loadedAssets_.end()) {
                it->second->AddRef();
                NEXT_LOG_DEBUG("Asset already loaded: %s (refcount: %u)",
                               loadedKey.c_str(), it->second->GetRefCount());
                return AssetHandle(it->second->GetID(), it->second.get());
            }
        }
    }
    
    // Find asset in loaded packages
    std::shared_ptr<PackageContainer> package;
    std::string packageName;
    std::string localName = assetName;
    {
        std::string requestedPkg;
        std::string requestedLocal;
        if (splitKey(assetName, requestedPkg, requestedLocal)) {
            packageName = requestedPkg;
            localName = requestedLocal;
        }
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!packageName.empty()) {
            auto it = loadedPackages_.find(packageName);
            if (it != loadedPackages_.end() && it->second.package && it->second.package->HasAsset(localName)) {
                package = it->second.package;
            }
        } else {
            // Unqualified: must be unique across loaded packages.
            std::string foundPkg;
            for (const auto& [pkgNameIt, entry] : loadedPackages_) {
                if (entry.package && entry.package->HasAsset(localName)) {
                    if (!foundPkg.empty() && foundPkg != pkgNameIt) {
                        NEXT_LOG_ERROR("Asset name is ambiguous across packages: %s (e.g. %s::%s and %s::%s). Please qualify with pkg::asset.",
                                       localName.c_str(),
                                       foundPkg.c_str(), localName.c_str(),
                                       pkgNameIt.c_str(), localName.c_str());
                        failedLoads_++;
                        return AssetHandle();
                    }
                    foundPkg = pkgNameIt;
                    package = entry.package;
                }
            }
            packageName = foundPkg;
        }
    }
    
    if (!package) {
        NEXT_LOG_ERROR("Asset not found in any loaded package: %s", assetName.c_str());
        failedLoads_++;
        return AssetHandle();
    }

    const std::string storageKey = packageName.empty() ? assetName : (packageName + "::" + localName);
    
    // Read asset data
    std::vector<uint8_t> assetData;
    if (!package->ReadAssetData(localName, assetData)) {
        NEXT_LOG_ERROR("Failed to read asset data: %s", storageKey.c_str());
        failedLoads_++;
        return AssetHandle();
    }
    
    if (assetData.size() < sizeof(AssetHeader)) {
        NEXT_LOG_ERROR("Asset data too small: %s", storageKey.c_str());
        failedLoads_++;
        return AssetHandle();
    }

    AssetHeader commonHeader;
    memcpy(&commonHeader, assetData.data(), sizeof(AssetHeader));

    if (!commonHeader.Validate()) {
        NEXT_LOG_ERROR("Invalid asset header: %s", storageKey.c_str());
        failedLoads_++;
        return AssetHandle();
    }

    std::shared_ptr<AssetData> data;
    // Stable content identity: the id is a hash of the canonical storage key, so the same
    // asset always resolves to the same id -- including across an unload/reload. Systems
    // holding a handle therefore keep it valid and transparently pick up a hot-reloaded
    // payload, which is the precondition for hot-reload in a real-code game with a live editor.
    uint64_t id = CalculateCRC64(storageKey.data(), storageKey.size());
    if (id == 0) {
        id = ~0ull; // 0 is reserved for the invalid handle; remap the (effectively impossible) hash-zero.
    }

    switch (commonHeader.assetType) {
        case AssetType::Mesh: {
            if (assetData.size() < sizeof(MeshHeader)) {
                NEXT_LOG_ERROR("Mesh data too small: %s", storageKey.c_str());
                failedLoads_++;
                return AssetHandle();
            }
            MeshHeader meshHeader;
            memcpy(&meshHeader, assetData.data(), sizeof(MeshHeader));
            if (!ValidateMeshHeader(meshHeader)) {
                failedLoads_++;
                return AssetHandle();
            }
            size_t payloadSize = assetData.size() - sizeof(MeshHeader);
            if (!ValidateMeshPayloadSize(meshHeader, payloadSize, storageKey)) {
                failedLoads_++;
                return AssetHandle();
            }
            data = std::make_shared<MeshData>(id, packageName, meshHeader, assetData.data() + sizeof(MeshHeader), payloadSize);
            break;
        }
        case AssetType::Texture: {
            if (assetData.size() < sizeof(TextureHeader)) {
                NEXT_LOG_ERROR("Texture data too small: %s", storageKey.c_str());
                failedLoads_++;
                return AssetHandle();
            }
            TextureHeader texHeader;
            memcpy(&texHeader, assetData.data(), sizeof(TextureHeader));
            if (!ValidateTextureHeader(texHeader)) {
                failedLoads_++;
                return AssetHandle();
            }
            size_t payloadSize = assetData.size() - sizeof(TextureHeader);
            if (!ValidateTexturePayloadSize(texHeader, payloadSize, storageKey)) {
                failedLoads_++;
                return AssetHandle();
            }
            data = std::make_shared<TextureData>(id, packageName, texHeader, assetData.data() + sizeof(TextureHeader), payloadSize);
            break;
        }
        case AssetType::Material: {
            if (assetData.size() < sizeof(MaterialHeader)) {
                NEXT_LOG_ERROR("Material data too small: %s", storageKey.c_str());
                failedLoads_++;
                return AssetHandle();
            }
            MaterialHeader matHeader;
            memcpy(&matHeader, assetData.data(), sizeof(MaterialHeader));
            if (!ValidateMaterialHeader(matHeader)) {
                failedLoads_++;
                return AssetHandle();
            }
            size_t payloadSize = assetData.size() - sizeof(MaterialHeader);
            if (!ValidateMaterialPayloadSize(matHeader, payloadSize, storageKey)) {
                failedLoads_++;
                return AssetHandle();
            }
            MaterialAssetView materialView{};
            materialView.header = matHeader;
            materialView.payload = assetData.data() + sizeof(MaterialHeader);
            materialView.payloadBytes = payloadSize;
            if (!materialView.HasValidPayload()) {
                NEXT_LOG_ERROR("Invalid material payload: %s", storageKey.c_str());
                failedLoads_++;
                return AssetHandle();
            }
            data = std::make_shared<MaterialData>(id, packageName, matHeader, assetData.data() + sizeof(MaterialHeader), payloadSize);
            break;
        }
        default:
            NEXT_LOG_ERROR("Unknown asset type: %u", static_cast<uint32_t>(commonHeader.assetType));
            failedLoads_++;
            return AssetHandle();
    }

    if (!data) {
        NEXT_LOG_ERROR("Failed to create asset data: %s", assetName.c_str());
        failedLoads_++;
        return AssetHandle();
    }

    // Store in loaded assets
    std::lock_guard<std::mutex> lock(mutex_);
    auto existingIt = loadedAssets_.find(storageKey);
    if (existingIt != loadedAssets_.end() && existingIt->second) {
        existingIt->second->AddRef();
        NEXT_LOG_DEBUG("Asset already loaded after IO race: %s (refcount: %u)",
                       storageKey.c_str(), existingIt->second->GetRefCount());
        return AssetHandle(existingIt->second->GetID(), existingIt->second.get());
    }

    // Guard against an (astronomically unlikely) hash collision: a different asset whose
    // canonical key hashes to the same id. Reject loudly rather than silently shadowing it.
    auto idCollision = idToName_.find(id);
    if (idCollision != idToName_.end() && idCollision->second != storageKey) {
        NEXT_LOG_ERROR("Asset id hash collision: '%s' and '%s' map to id %llu; rename one asset.",
                       idCollision->second.c_str(), storageKey.c_str(),
                       static_cast<unsigned long long>(id));
        failedLoads_++;
        return AssetHandle();
    }

    data->AddRef(); // Initial reference

    loadedAssets_[storageKey] = data;
    idToName_[id] = storageKey;

    loadedAssetsCount_++;
    totalMemory_ += data->GetPayloadSize();

    NEXT_LOG_INFO("Asset loaded successfully: %s (ID: %llu, size: %zu bytes)", storageKey.c_str(),
                  static_cast<unsigned long long>(id), data->GetPayloadSize());

    return AssetHandle(id, data.get());
}

void AssetManager::LoadAssetAsync(const std::string& assetName, AssetLoadCallback callback) {
    NEXT_CPU_SCOPE("AssetManager::LoadAssetAsync");
    
    pendingLoads_++;
    NEXT_LOG_DEBUG("Queueing async asset load: %s", assetName.c_str());

    auto loadTask = [this, assetName, callback = std::move(callback)]() mutable {
        NEXT_CPU_SCOPE("AssetManager::AsyncLoadTask");

        AssetLoadResult result;
        try {
            result.handle = LoadAssetSync(assetName);
            result.success = result.handle.IsValid();

            if (!result.success) {
                result.errorMessage = "Failed to load asset: " + assetName;
            }
        } catch (const std::exception& e) {
            result.success = false;
            result.errorMessage = "Exception while loading asset '" + assetName + "': " + e.what();
            failedLoads_++;
            NEXT_LOG_ERROR("%s", result.errorMessage.c_str());
        } catch (...) {
            result.success = false;
            result.errorMessage = "Unknown exception while loading asset: " + assetName;
            failedLoads_++;
            NEXT_LOG_ERROR("%s", result.errorMessage.c_str());
        }

        pendingLoads_--;

        if (callback) {
            CompletedAssetLoad completed;
            completed.result = std::move(result);
            completed.callback = std::move(callback);

            std::lock_guard<std::mutex> callbackLock(callbackMutex_);
            completedLoads_.push_back(std::move(completed));
        }
    };

    auto& jobSystem = JobSystem::Instance();
    if (!jobSystem.IsInitialized()) {
        loadTask();
        return;
    }

    jobSystem.Submit(std::move(loadTask), JobPriority::Normal, {}, "AssetLoad");
}

size_t AssetManager::PumpAsyncCallbacks(size_t maxCallbacks) {
    std::vector<CompletedAssetLoad> callbacks;

    {
        std::lock_guard<std::mutex> callbackLock(callbackMutex_);
        if (completedLoads_.empty()) {
            return 0;
        }

        const size_t callbackCount =
            maxCallbacks == 0 ? completedLoads_.size() : std::min(maxCallbacks, completedLoads_.size());
        callbacks.reserve(callbackCount);
        for (size_t i = 0; i < callbackCount; ++i) {
            callbacks.push_back(std::move(completedLoads_[i]));
        }
        completedLoads_.erase(completedLoads_.begin(),
                              completedLoads_.begin() + static_cast<std::ptrdiff_t>(callbackCount));
    }

    for (CompletedAssetLoad& completed : callbacks) {
        if (completed.callback) {
            try {
                completed.callback(completed.result);
            } catch (const std::exception& e) {
                NEXT_LOG_ERROR("Asset async callback threw an exception: %s", e.what());
            } catch (...) {
                NEXT_LOG_ERROR("Asset async callback threw an unknown exception");
            }
        }
    }

    return callbacks.size();
}

size_t AssetManager::GetPendingAsyncCallbackCount() const {
    std::lock_guard<std::mutex> callbackLock(callbackMutex_);
    return completedLoads_.size();
}

void AssetManager::UnloadAsset(const AssetHandle& handle) {
    if (!handle.IsValid()) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    auto it = idToName_.find(handle.GetID());
    if (it == idToName_.end()) {
        return;
    }

    // Make a copy of the name before we potentially erase it
    std::string name = it->second;
    auto assetIt = loadedAssets_.find(name);
    if (assetIt == loadedAssets_.end()) {
        return;
    }

    // Release reference and cleanup when 0
    uint32_t refs = assetIt->second->Release();
    if (refs == 0) {
        totalMemory_ -= assetIt->second->GetPayloadSize();
        loadedAssetsCount_--;
        loadedAssets_.erase(assetIt);
        idToName_.erase(it);
        NEXT_LOG_INFO("Asset unloaded: %s", name.c_str());
    } else {
        NEXT_LOG_DEBUG("Asset reference released: %s (refcount: %u)", name.c_str(), refs);
    }
}

void AssetManager::UnloadAsset(const std::string& assetName) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = loadedAssets_.find(assetName);
    if (it == loadedAssets_.end()) {
        // Unqualified: try resolve by local name if unique.
        std::string found;
        for (const auto& [k, a] : loadedAssets_) {
            if (a && a->GetName() == assetName) {
                if (!found.empty() && found != k) {
                    NEXT_LOG_WARNING("UnloadAsset ambiguous (multiple packages): %s. Use pkg::asset.", assetName.c_str());
                    return;
                }
                found = k;
            }
        }
        if (found.empty()) {
            NEXT_LOG_WARNING("Asset not found for unloading: %s", assetName.c_str());
            return;
        }
        it = loadedAssets_.find(found);
        if (it == loadedAssets_.end()) {
            NEXT_LOG_WARNING("Asset not found for unloading: %s", assetName.c_str());
            return;
        }
    }
    
    uint32_t refs = it->second->Release();
    if (refs == 0) {
        totalMemory_ -= it->second->GetPayloadSize();
        loadedAssetsCount_--;
        idToName_.erase(it->second->GetID());
        loadedAssets_.erase(it);
        NEXT_LOG_INFO("Asset unloaded: %s", assetName.c_str());
    } else {
        NEXT_LOG_DEBUG("Asset reference released: %s (refcount: %u)", 
                      assetName.c_str(), refs);
    }
}

bool AssetManager::IsAssetLoaded(const std::string& assetName) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (loadedAssets_.find(assetName) != loadedAssets_.end()) {
        return true;
    }
    // Unqualified: check by local name (unique not enforced for queries).
    for (const auto& [k, a] : loadedAssets_) {
        if (a && a->GetName() == assetName) {
            return true;
        }
    }
    return false;
}

AssetHandle AssetManager::GetAssetHandle(const std::string& assetName) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = loadedAssets_.find(assetName);
    if (it == loadedAssets_.end()) {
        // Unqualified: resolve by local name if unique among loaded assets.
        std::string found;
        for (const auto& [k, a] : loadedAssets_) {
            if (a && a->GetName() == assetName) {
                if (!found.empty() && found != k) {
                    return AssetHandle();
                }
                found = k;
            }
        }
        if (found.empty()) {
            return AssetHandle();
        }
        auto it2 = loadedAssets_.find(found);
        if (it2 == loadedAssets_.end()) {
            return AssetHandle();
        }
        return AssetHandle(it2->second->GetID(), it2->second.get());
    }
    
    return AssetHandle(it->second->GetID(), it->second.get());
}

void AssetManager::AddRef(const AssetHandle& handle) {
    if (!handle.IsValid()) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = idToName_.find(handle.GetID());
    if (it == idToName_.end()) {
        return;
    }
    
    const std::string& name = it->second;
    auto assetIt = loadedAssets_.find(name);
    if (assetIt == loadedAssets_.end()) {
        return;
    }
    
    assetIt->second->AddRef();
}

void AssetManager::Release(const AssetHandle& handle) {
    UnloadAsset(handle);
}

uint32_t AssetManager::GetRefCount(const AssetHandle& handle) const {
    if (!handle.IsValid()) {
        return 0;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = idToName_.find(handle.GetID());
    if (it == idToName_.end()) {
        return 0;
    }
    
    const std::string& name = it->second;
    auto assetIt = loadedAssets_.find(name);
    if (assetIt == loadedAssets_.end()) {
        return 0;
    }
    
    return assetIt->second->GetRefCount();
}

bool AssetManager::GetMeshAssetView(const AssetHandle& handle, MeshAssetView& outView) const {
    outView = {};

    std::lock_guard<std::mutex> lock(mutex_);
    std::shared_ptr<AssetData> data = FindAssetDataLocked(handle);
    if (!data || data->GetType() != AssetType::Mesh) {
        return false;
    }

    const auto* meshData = static_cast<const MeshData*>(data.get());
    outView.header = meshData->GetHeader();
    outView.payload = meshData->GetPayload();
    outView.payloadBytes = meshData->GetPayloadSize();
    // B3 fix: the view shares ownership of the asset data, so `payload` cannot dangle if the
    // asset is unloaded while the caller still holds the view.
    outView.keepAlive = std::move(data);
    return outView.payload != nullptr || outView.payloadBytes == 0;
}

bool AssetManager::GetTextureAssetView(const AssetHandle& handle, TextureAssetView& outView) const {
    outView = {};

    std::lock_guard<std::mutex> lock(mutex_);
    std::shared_ptr<AssetData> data = FindAssetDataLocked(handle);
    if (!data || data->GetType() != AssetType::Texture) {
        return false;
    }

    const auto* textureData = static_cast<const TextureData*>(data.get());
    outView.header = textureData->GetHeader();
    outView.pixels = textureData->GetPayload();
    outView.pixelBytes = textureData->GetPayloadSize();
    // B3 fix: see GetMeshAssetView.
    outView.keepAlive = std::move(data);
    return outView.pixels != nullptr || outView.pixelBytes == 0;
}

bool AssetManager::GetMaterialAssetView(const AssetHandle& handle, MaterialAssetView& outView) const {
    outView = {};

    std::lock_guard<std::mutex> lock(mutex_);
    std::shared_ptr<AssetData> data = FindAssetDataLocked(handle);
    if (!data || data->GetType() != AssetType::Material) {
        return false;
    }

    const auto* materialData = static_cast<const MaterialData*>(data.get());
    outView.header = materialData->GetHeader();
    outView.payload = materialData->GetPayload();
    outView.payloadBytes = materialData->GetPayloadSize();
    // B3 fix: see GetMeshAssetView.
    outView.keepAlive = std::move(data);
    return outView.payload != nullptr || outView.payloadBytes == 0;
}

AssetStats AssetManager::GetStats() const {
    AssetStats stats;
    stats.loadedAssets = loadedAssetsCount_.load();
    stats.totalMemory = totalMemory_.load();
    stats.pendingLoads = pendingLoads_.load();
    {
        std::lock_guard<std::mutex> callbackLock(callbackMutex_);
        stats.pendingCallbacks = completedLoads_.size();
    }
    stats.failedLoads = failedLoads_.load();
    return stats;
}

std::shared_ptr<PackageContainer> AssetManager::GetPackage(const std::string& packageName) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = loadedPackages_.find(packageName);
    if (it == loadedPackages_.end()) {
        return nullptr;
    }
    
    return it->second.package;
}

uint32_t AssetManager::GetPackageRefCount(const std::string& packageName) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = loadedPackages_.find(packageName);
    if (it == loadedPackages_.end()) {
        return 0;
    }
    return it->second.refCount;
}

std::shared_ptr<PackageContainer> AssetManager::LoadPackageInternal(const std::string& packagePath) {
    return PackageContainer::LoadFromFile(packagePath);
}

std::shared_ptr<AssetData> AssetManager::FindAssetDataLocked(const AssetHandle& handle) const {
    if (!handle.IsValid()) {
        return nullptr;
    }

    auto idIt = idToName_.find(handle.GetID());
    if (idIt == idToName_.end()) {
        return nullptr;
    }

    auto assetIt = loadedAssets_.find(idIt->second);
    if (assetIt == loadedAssets_.end()) {
        return nullptr;
    }

    return assetIt->second;
}

} // namespace Next
