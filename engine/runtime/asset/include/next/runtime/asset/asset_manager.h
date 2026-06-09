#pragma once

#include "next/runtime/asset/asset_handle.h"
#include "next/runtime/asset/asset_types.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <atomic>
#include <cstring>
#include <mutex>
#include <functional>

namespace Next {

// Forward declarations
class AssetData;
class PackageContainer;

/**
 * @brief Result of an asset load operation
 */
struct AssetLoadResult {
    AssetHandle handle;           ///< Handle to the loaded asset
    bool success;                 ///< true if load succeeded
    std::string errorMessage;     ///< Error message if load failed
};

/**
 * @brief Callback function type for async asset loading
 *
 * Called when an async load operation completes.
 */
using AssetLoadCallback = std::function<void(const AssetLoadResult&)>;

struct MeshAssetView {
    MeshHeader header{};
    const void* payload = nullptr;
    size_t payloadBytes = 0;
    // Owns a reference to the backing asset data (B3 fix): `payload` stays valid for the lifetime
    // of this view (and any copy of it), even if the asset is concurrently Release()d/unloaded.
    // Views constructed by hand (e.g. validation-only tests) may leave it empty.
    std::shared_ptr<const void> keepAlive;
};

struct TextureAssetView {
    TextureHeader header{};
    const void* pixels = nullptr;
    size_t pixelBytes = 0;
    // Owns a reference to the backing asset data (B3 fix): `pixels` stays valid for the lifetime
    // of this view (and any copy of it), even if the asset is concurrently Release()d/unloaded.
    std::shared_ptr<const void> keepAlive;
};

struct MaterialAssetView {
    MaterialHeader header{};
    const void* payload = nullptr;
    size_t payloadBytes = 0;
    // Owns a reference to the backing asset data (B3 fix): `payload` stays valid for the lifetime
    // of this view (and any copy of it), even if the asset is concurrently Release()d/unloaded.
    std::shared_ptr<const void> keepAlive;

    static bool FixedStringIsValid(const char* value, size_t capacity) {
        return value && capacity != 0 && value[0] != '\0' &&
               std::memchr(value, '\0', capacity) != nullptr;
    }

    static bool TextureRefMetadataIsValid(const TextureRef& ref) {
        return ref.type <= static_cast<uint32_t>(TextureRef::OCCLUSION) &&
               ref.slot <= static_cast<uint32_t>(TextureRef::OCCLUSION);
    }

    static bool MaterialParamMetadataIsValid(const MaterialParam& param) {
        return param.type <= static_cast<uint32_t>(MaterialParam::COLOR);
    }

    bool HasValidTextureRefs() const {
        if (header.textureCount == 0) {
            return true;
        }
        if (!payload || header.textureCount > payloadBytes / sizeof(TextureRef)) {
            return false;
        }

        const auto* refs = static_cast<const TextureRef*>(payload);
        uint32_t seenTypes = 0;
        uint32_t seenSlots = 0;
        for (uint32_t i = 0; i < header.textureCount; ++i) {
            if (!FixedStringIsValid(refs[i].name, sizeof(refs[i].name)) ||
                !TextureRefMetadataIsValid(refs[i])) {
                return false;
            }

            const uint32_t typeMask = 1u << refs[i].type;
            const uint32_t slotMask = 1u << refs[i].slot;
            if ((seenTypes & typeMask) != 0 || (seenSlots & slotMask) != 0) {
                return false;
            }
            seenTypes |= typeMask;
            seenSlots |= slotMask;
        }
        return true;
    }

    const TextureRef* GetTextureRefs() const {
        if (header.textureCount == 0 || !HasValidTextureRefs()) {
            return nullptr;
        }
        return static_cast<const TextureRef*>(payload);
    }

    bool HasValidParameters() const {
        if (header.textureCount > payloadBytes / sizeof(TextureRef)) {
            return false;
        }

        const size_t textureBytes = static_cast<size_t>(header.textureCount) * sizeof(TextureRef);
        if (header.parameterCount == 0) {
            return true;
        }
        if (!payload || textureBytes > payloadBytes) {
            return false;
        }

        const size_t remainingBytes = payloadBytes - textureBytes;
        if (header.parameterCount > remainingBytes / sizeof(MaterialParam)) {
            return false;
        }

        const auto* bytes = static_cast<const uint8_t*>(payload);
        const auto* params = reinterpret_cast<const MaterialParam*>(bytes + textureBytes);
        for (uint32_t i = 0; i < header.parameterCount; ++i) {
            if (!FixedStringIsValid(params[i].name, sizeof(params[i].name)) ||
                !MaterialParamMetadataIsValid(params[i])) {
                return false;
            }
        }
        return true;
    }

    const MaterialParam* GetParameters() const {
        if (header.parameterCount == 0 || !HasValidParameters()) {
            return nullptr;
        }

        const size_t textureBytes = static_cast<size_t>(header.textureCount) * sizeof(TextureRef);
        const auto* bytes = static_cast<const uint8_t*>(payload);
        return reinterpret_cast<const MaterialParam*>(bytes + textureBytes);
    }

    bool HasValidPayload() const {
        return HasValidTextureRefs() && HasValidParameters();
    }

    bool FindTextureRef(uint32_t type, TextureRef& outRef) const {
        const TextureRef* refs = GetTextureRefs();
        if (!refs) {
            return false;
        }

        for (uint32_t i = 0; i < header.textureCount; ++i) {
            if (refs[i].type == type) {
                outRef = refs[i];
                return true;
            }
        }
        return false;
    }
};

/**
 * @brief Statistics snapshot for the asset manager
 */
struct AssetStats {
    size_t loadedAssets = 0;       ///< Number of currently loaded assets
    size_t totalMemory = 0;        ///< Total memory used by loaded assets
    size_t pendingLoads = 0;       ///< Number of async loads in progress
    size_t pendingCallbacks = 0;   ///< Completed async loads waiting for PumpAsyncCallbacks
    size_t failedLoads = 0;        ///< Number of failed loads since startup

    size_t OutstandingAsyncOperationCount() const {
        return pendingLoads + pendingCallbacks;
    }

    bool HasLoadedAssets() const { return loadedAssets != 0; }
    bool HasMemoryUsage() const { return totalMemory != 0; }
    bool HasPendingLoads() const { return pendingLoads != 0; }
    bool HasPendingCallbacks() const { return pendingCallbacks != 0; }
    bool HasOutstandingAsyncOperations() const { return OutstandingAsyncOperationCount() != 0; }
    bool HasFailures() const { return failedLoads != 0; }
};

/**
 * @brief Central asset management system
 *
 * Handles loading, unloading, and lifetime management of game assets.
 * Supports both synchronous and asynchronous loading with reference counting.
 *
 * Features:
 * - Package-based asset storage
 * - Synchronous and asynchronous loading
 * - Automatic reference counting
 * - Memory usage tracking
 *
 * Usage:
 *   AssetManager::Instance().Initialize();
 *   AssetManager::Instance().LoadPackage("game_assets.pkg");
 *   auto texture = AssetManager::Instance().LoadAssetSync<Texture>("textures/player.png");
 */
class AssetManager {
public:
    /**
     * @brief Get the singleton instance
     */
    static AssetManager& Instance();

    /**
     * @brief Initialize the asset manager
     * @return true if successful
     */
    bool Initialize();

    /**
     * @brief Shutdown the asset manager
     *
     * Unloads all packages and assets.
     */
    void Shutdown();

    /**
     * @brief Load an asset package
     * @param packagePath Path to the package file
     * @return true if loaded successfully
     */
    bool LoadPackage(const std::string& packagePath);

    /**
     * @brief Unload an asset package
     * @param packageName Name of the package to unload
     */
    void UnloadPackage(const std::string& packageName);

    /**
     * @brief Synchronously load an asset
     *
     * Blocks until the asset is fully loaded.
     *
     * @param assetName Name/path of the asset within the package
     * @return Handle to the loaded asset
     */
    AssetHandle LoadAssetSync(const std::string& assetName);

    /**
     * @brief Asynchronously load an asset
     *
     * Returns immediately and calls the callback when loading completes.
     *
     * @param assetName Name/path of the asset within the package
     * @param callback Function to call when load completes
     */
    void LoadAssetAsync(const std::string& assetName, AssetLoadCallback callback);

    /**
     * @brief Dispatch completed async asset-load callbacks on the calling thread.
     *
     * Use from the main/game thread before touching renderer-facing state from
     * asset load callbacks.
     *
     * @param maxCallbacks Maximum callbacks to dispatch; 0 dispatches all queued callbacks.
     * @return Number of callbacks dispatched.
     */
    size_t PumpAsyncCallbacks(size_t maxCallbacks = 0);

    /**
     * @brief Get the number of completed async loads waiting for PumpAsyncCallbacks.
     */
    size_t GetPendingAsyncCallbackCount() const;

    /**
     * @brief Synchronously load a typed asset
     * @tparam T Asset type
     * @param assetName Name/path of the asset
     * @return Typed handle to the loaded asset
     */
    template<typename T>
    TypedAssetHandle<T> LoadAssetSync(const std::string& assetName) {
        return TypedAssetHandle<T>(LoadAssetSync(assetName));
    }

    /**
     * @brief Unload an asset by handle
     * @param handle Handle to the asset to unload
     */
    void UnloadAsset(const AssetHandle& handle);

    /**
     * @brief Unload an asset by name
     * @param assetName Name of the asset to unload
     */
    void UnloadAsset(const std::string& assetName);

    /**
     * @brief Check if an asset is currently loaded
     * @param assetName Name of the asset to check
     * @return true if the asset is loaded
     */
    bool IsAssetLoaded(const std::string& assetName) const;

    /**
     * @brief Get a handle to a loaded asset
     * @param assetName Name of the asset
     * @return Handle to the asset (invalid if not loaded)
     */
    AssetHandle GetAssetHandle(const std::string& assetName) const;

    /**
     * @brief Increment reference count for an asset
     * @param handle Handle to the asset
     */
    void AddRef(const AssetHandle& handle);

    /**
     * @brief Decrement reference count for an asset
     *
     * Asset is unloaded when reference count reaches zero.
     *
     * @param handle Handle to the asset
     */
    void Release(const AssetHandle& handle);

    /**
     * @brief Get the current reference count for an asset
     * @param handle Handle to the asset
     * @return Current reference count
     */
    uint32_t GetRefCount(const AssetHandle& handle) const;

    bool GetMeshAssetView(const AssetHandle& handle, MeshAssetView& outView) const;
    bool GetTextureAssetView(const AssetHandle& handle, TextureAssetView& outView) const;
    bool GetMaterialAssetView(const AssetHandle& handle, MaterialAssetView& outView) const;

    /**
     * @brief Get asset manager statistics
     * @return Statistics snapshot
     */
    AssetStats GetStats() const;

    /**
     * @brief Get access to a package container (advanced use)
     * @param packageName Name of the package
     * @return Shared pointer to the package container
     */
    std::shared_ptr<PackageContainer> GetPackage(const std::string& packageName) const;
    uint32_t GetPackageRefCount(const std::string& packageName) const;
    
private:
    AssetManager() = default;
    ~AssetManager() = default;
    AssetManager(const AssetManager&) = delete;
    AssetManager& operator=(const AssetManager&) = delete;
    
    // Internal loading implementation
    AssetHandle LoadAssetInternal(const std::string& assetName);
    void LoadAssetAsyncInternal(const std::string& assetName, AssetLoadCallback callback);
    std::shared_ptr<AssetData> FindAssetDataLocked(const AssetHandle& handle) const;
    
    // Package management
    std::shared_ptr<PackageContainer> LoadPackageInternal(const std::string& packagePath);
    
    // Member variables
    mutable std::mutex mutex_;
    mutable std::mutex callbackMutex_;
    std::unordered_map<std::string, std::shared_ptr<AssetData>> loadedAssets_;
    struct LoadedPackageEntry {
        std::shared_ptr<PackageContainer> package;
        uint32_t refCount = 0;
    };
    std::unordered_map<std::string, LoadedPackageEntry> loadedPackages_; // key: package name (stem)
    std::unordered_map<uint64_t, std::string> idToName_;
    struct CompletedAssetLoad {
        AssetLoadResult result;
        AssetLoadCallback callback;
    };
    std::vector<CompletedAssetLoad> completedLoads_;

    // Statistics
    std::atomic<size_t> loadedAssetsCount_{0};
    std::atomic<size_t> totalMemory_{0};
    std::atomic<size_t> pendingLoads_{0};
    std::atomic<size_t> failedLoads_{0};
};

} // namespace Next
