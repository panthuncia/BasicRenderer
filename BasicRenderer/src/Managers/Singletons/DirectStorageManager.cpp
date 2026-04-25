#include "Managers/Singletons/DirectStorageManager.h"

#include <cstdlib>
#include <filesystem>
#include <limits>
#include <mutex>
#include <unordered_map>
#include <utility>

#include <spdlog/spdlog.h>

#include "Managers/Singletons/DeviceManager.h"
#include "Managers/Singletons/SettingsManager.h"
#include <rhi_interop.h>
#include <rhi_interop_dx12.h>

#if BASICRENDERER_HAS_DIRECTSTORAGE
#include <dstorage.h>
#include <wrl/client.h>
#endif

namespace br {

namespace {

class ScopedWinHandle {
public:
    explicit ScopedWinHandle(HANDLE handle = nullptr) noexcept
        : m_handle(handle) {
    }

    ~ScopedWinHandle() {
        if (m_handle != nullptr) {
            CloseHandle(m_handle);
        }
    }

    ScopedWinHandle(const ScopedWinHandle&) = delete;
    ScopedWinHandle& operator=(const ScopedWinHandle&) = delete;

    ScopedWinHandle(ScopedWinHandle&& other) noexcept
        : m_handle(other.m_handle) {
        other.m_handle = nullptr;
    }

    ScopedWinHandle& operator=(ScopedWinHandle&& other) noexcept {
        if (this != &other) {
            if (m_handle != nullptr) {
                CloseHandle(m_handle);
            }
            m_handle = other.m_handle;
            other.m_handle = nullptr;
        }
        return *this;
    }

    HANDLE get() const noexcept {
        return m_handle;
    }

private:
    HANDLE m_handle = nullptr;
};

bool IsDirectStorageDisabledByEnvironment() {
    char* value = nullptr;
    size_t len = 0;
    if (_dupenv_s(&value, &len, "BASICRENDERER_DISABLE_DIRECTSTORAGE") != 0 || value == nullptr) {
        return false;
    }

    const bool disabled = value[0] == '1' || value[0] == 't' || value[0] == 'T' || value[0] == 'y' || value[0] == 'Y';
    free(value);
    return disabled;
}

constexpr uint32_t kDefaultDirectStorageStagingBufferSizeBytes = 128u * 1024u * 1024u;

}

struct DirectStorageManager::Impl {
#if BASICRENDERER_HAS_DIRECTSTORAGE
    Microsoft::WRL::ComPtr<IDStorageFactory> factory;
    Microsoft::WRL::ComPtr<IDStorageQueue> systemMemoryQueue;
    Microsoft::WRL::ComPtr<IDStorageQueue> gpuQueue;
    mutable std::mutex fileCacheMutex;
    std::mutex systemQueueMutex;
    std::mutex gpuQueueMutex;
    std::unordered_map<std::wstring, Microsoft::WRL::ComPtr<IDStorageFile>> fileCache;
    bool hasQueue2 = false;
    bool hasQueue3 = false;
#endif
};

DirectStorageManager& DirectStorageManager::GetInstance() {
    static DirectStorageManager instance;
    return instance;
}

void DirectStorageManager::Initialize() {
    if (m_initialized) {
        return;
    }

    m_initialized = true;
    m_available = false;
    m_enabled = false;
    m_runtimeDisabled = IsDirectStorageDisabledByEnvironment();
    m_stagingBufferSizeBytes = 0;
    m_statusMessage.clear();

    bool settingsEnabled = true;
    try {
        settingsEnabled = SettingsManager::GetInstance().getSettingGetter<bool>("enableDirectStorage")();
    }
    catch (const std::exception&) {
        settingsEnabled = true;
    }

    if (!settingsEnabled) {
        m_runtimeDisabled = true;
        m_statusMessage = "disabled by runtime setting enableDirectStorage";
        spdlog::info("DirectStorageManager: {}", m_statusMessage);
        return;
    }

    if (m_runtimeDisabled) {
        m_statusMessage = "disabled by BASICRENDERER_DISABLE_DIRECTSTORAGE";
        spdlog::info("DirectStorageManager: {}", m_statusMessage);
        return;
    }

#if !BASICRENDERER_HAS_DIRECTSTORAGE
    m_statusMessage = "SDK not configured at build time; using CPU upload fallback";
    spdlog::info("DirectStorageManager: {}", m_statusMessage);
    return;
#else
    auto nativeDevice = rhi::dx12::get_device(DeviceManager::GetInstance().GetDevice());
    if (nativeDevice == nullptr) {
        m_statusMessage = "native D3D12 device unavailable; using CPU upload fallback";
        spdlog::warn("DirectStorageManager: {}", m_statusMessage);
        return;
    }

    auto impl = std::make_unique<Impl>();

    HRESULT hr = DStorageGetFactory(IID_PPV_ARGS(impl->factory.ReleaseAndGetAddressOf()));
    if (FAILED(hr)) {
        m_statusMessage = "DStorageGetFactory failed; using CPU upload fallback";
        spdlog::warn("DirectStorageManager: {} hr=0x{:08X}", m_statusMessage, static_cast<unsigned int>(hr));
        return;
    }

#if BUILD_TYPE == BUILD_TYPE_DEBUG || BUILD_TYPE == BUILD_TYPE_RELEASE_DEBUG
    impl->factory->SetDebugFlags(DSTORAGE_DEBUG_SHOW_ERRORS | DSTORAGE_DEBUG_BREAK_ON_ERROR);
#endif

    m_stagingBufferSizeBytes = kDefaultDirectStorageStagingBufferSizeBytes;
    hr = impl->factory->SetStagingBufferSize(m_stagingBufferSizeBytes);
    if (FAILED(hr)) {
        m_statusMessage = "SetStagingBufferSize failed; using CPU upload fallback";
        spdlog::warn("DirectStorageManager: {} hr=0x{:08X}", m_statusMessage, static_cast<unsigned int>(hr));
        return;
    }

    DSTORAGE_QUEUE_DESC systemQueueDesc{};
    systemQueueDesc.Capacity = DSTORAGE_MAX_QUEUE_CAPACITY;
    systemQueueDesc.Priority = DSTORAGE_PRIORITY_NORMAL;
    systemQueueDesc.SourceType = DSTORAGE_REQUEST_SOURCE_FILE;

    hr = impl->factory->CreateQueue(&systemQueueDesc, IID_PPV_ARGS(impl->systemMemoryQueue.ReleaseAndGetAddressOf()));
    if (FAILED(hr)) {
        m_statusMessage = "failed to create system-memory DirectStorage queue; using CPU upload fallback";
        spdlog::warn("DirectStorageManager: {} hr=0x{:08X}", m_statusMessage, static_cast<unsigned int>(hr));
        return;
    }

    DSTORAGE_QUEUE_DESC gpuQueueDesc{};
    gpuQueueDesc.Capacity = DSTORAGE_MAX_QUEUE_CAPACITY;
    gpuQueueDesc.Priority = DSTORAGE_PRIORITY_NORMAL;
    gpuQueueDesc.SourceType = DSTORAGE_REQUEST_SOURCE_FILE;
    gpuQueueDesc.Device = nativeDevice;

    hr = impl->factory->CreateQueue(&gpuQueueDesc, IID_PPV_ARGS(impl->gpuQueue.ReleaseAndGetAddressOf()));
    if (FAILED(hr)) {
        m_statusMessage = "failed to create GPU DirectStorage queue; using CPU upload fallback";
        spdlog::warn("DirectStorageManager: {} hr=0x{:08X}", m_statusMessage, static_cast<unsigned int>(hr));
        return;
    }

    Microsoft::WRL::ComPtr<IDStorageQueue2> queue2;
    impl->hasQueue2 = SUCCEEDED(impl->gpuQueue.As(&queue2));

    Microsoft::WRL::ComPtr<IDStorageQueue3> queue3;
    impl->hasQueue3 = SUCCEEDED(impl->gpuQueue.As(&queue3));

    m_impl = std::move(impl);
    m_available = true;
    m_enabled = true;
    m_statusMessage = "initialized DirectStorage factory and queues";

    spdlog::info(
        "DirectStorageManager: {} stagingBufferMiB={} queue2={} queue3={}",
        m_statusMessage,
        m_stagingBufferSizeBytes / (1024u * 1024u),
        m_impl->hasQueue2,
        m_impl->hasQueue3);
#endif
}

void DirectStorageManager::Cleanup() {
    m_impl.reset();
    m_initialized = false;
    m_available = false;
    m_enabled = false;
    m_runtimeDisabled = false;
    m_stagingBufferSizeBytes = 0;
    m_statusMessage.clear();
}

DirectStorageCapabilities DirectStorageManager::GetCapabilities() const {
    DirectStorageCapabilities capabilities{};
    capabilities.sdkAvailable = BASICRENDERER_HAS_DIRECTSTORAGE != 0;
    capabilities.initialized = m_initialized;
    capabilities.enabled = m_enabled;
    capabilities.runtimeDisabled = m_runtimeDisabled;
    capabilities.stagingBufferSizeBytes = m_stagingBufferSizeBytes;

#if BASICRENDERER_HAS_DIRECTSTORAGE
    capabilities.systemMemoryQueueAvailable = m_impl != nullptr && m_impl->systemMemoryQueue != nullptr;
    capabilities.gpuQueueAvailable = m_impl != nullptr && m_impl->gpuQueue != nullptr;
    capabilities.supportsQueue2 = m_impl != nullptr && m_impl->hasQueue2;
    capabilities.supportsQueue3 = m_impl != nullptr && m_impl->hasQueue3;
#endif

    return capabilities;
}

bool DirectStorageManager::CanServiceQueue(DirectStorageQueueKind queueKind) const {
    const auto capabilities = GetCapabilities();
    switch (queueKind) {
    case DirectStorageQueueKind::SystemMemory:
        return capabilities.systemMemoryQueueAvailable;
    case DirectStorageQueueKind::Gpu:
        return capabilities.gpuQueueAvailable;
    default:
        return false;
    }
}

DirectStorageOpenFileResult DirectStorageManager::PrimeFileHandle(const std::wstring& path) {
    DirectStorageOpenFileResult result{};
    if (path.empty()) {
        result.message = "path is empty";
        return result;
    }

    if (!m_enabled || m_impl == nullptr) {
        result.message = m_statusMessage.empty() ? "DirectStorage is not enabled" : m_statusMessage;
        return result;
    }

#if !BASICRENDERER_HAS_DIRECTSTORAGE
    result.message = "DirectStorage SDK is not available in this build";
    return result;
#else
    {
        std::scoped_lock lock(m_impl->fileCacheMutex);
        if (m_impl->fileCache.find(path) != m_impl->fileCache.end()) {
            result.success = true;
            result.cached = true;
            result.message = "file handle already cached";
            return result;
        }
    }

    Microsoft::WRL::ComPtr<IDStorageFile> file;
    const HRESULT hr = m_impl->factory->OpenFile(path.c_str(), IID_PPV_ARGS(file.ReleaseAndGetAddressOf()));
    if (FAILED(hr)) {
        result.message = "failed to open file handle";
        spdlog::warn("DirectStorageManager: failed to prime file handle for '{}' hr=0x{:08X}", std::filesystem::path(path).string(), static_cast<unsigned int>(hr));
        return result;
    }

    {
        std::scoped_lock lock(m_impl->fileCacheMutex);
        m_impl->fileCache.emplace(path, std::move(file));
    }

    result.success = true;
    result.cached = false;
    result.message = "file handle cached";
    return result;
#endif
}

bool DirectStorageManager::ReadFileToMemory(const std::wstring& path, std::vector<std::byte>& outData, std::string* outMessage) {
    outData.clear();

    if (outMessage) {
        outMessage->clear();
    }

    if (path.empty()) {
        if (outMessage) {
            *outMessage = "path is empty";
        }
        return false;
    }

    if (!CanServiceQueue(DirectStorageQueueKind::SystemMemory)) {
        if (outMessage) {
            *outMessage = m_statusMessage.empty() ? "DirectStorage system-memory queue unavailable" : m_statusMessage;
        }
        return false;
    }

#if !BASICRENDERER_HAS_DIRECTSTORAGE
    if (outMessage) {
        *outMessage = "DirectStorage SDK is not available in this build";
    }
    return false;
#else
    auto primeResult = PrimeFileHandle(path);
    if (!primeResult.success) {
        if (outMessage) {
            *outMessage = primeResult.message;
        }
        return false;
    }

    Microsoft::WRL::ComPtr<IDStorageFile> file;
    {
        std::scoped_lock fileLock(m_impl->fileCacheMutex);
        auto it = m_impl->fileCache.find(path);
        if (it == m_impl->fileCache.end() || it->second == nullptr) {
            if (outMessage) {
                *outMessage = "cached DirectStorage file handle missing";
            }
            return false;
        }
        file = it->second;
    }

    BY_HANDLE_FILE_INFORMATION fileInfo{};
    const HRESULT fileInfoHr = file->GetFileInformation(&fileInfo);
    if (FAILED(fileInfoHr)) {
        if (outMessage) {
            *outMessage = "failed to query file information";
        }
        return false;
    }

    const uint64_t fileSize = (static_cast<uint64_t>(fileInfo.nFileSizeHigh) << 32u) | static_cast<uint64_t>(fileInfo.nFileSizeLow);
    if (fileSize == 0u) {
        return true;
    }

    if (fileSize > static_cast<uint64_t>((std::numeric_limits<uint32_t>::max)())) {
        if (outMessage) {
            *outMessage = "file too large for current DirectStorage read path";
        }
        return false;
    }

    outData.resize(static_cast<size_t>(fileSize));

    auto nativeDevice = rhi::dx12::get_device(DeviceManager::GetInstance().GetDevice());
    if (nativeDevice == nullptr) {
        outData.clear();
        if (outMessage) {
            *outMessage = "native D3D12 device unavailable for DirectStorage fence";
        }
        return false;
    }

    Microsoft::WRL::ComPtr<ID3D12Fence> fence;
    const HRESULT createFenceHr = nativeDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.ReleaseAndGetAddressOf()));
    if (FAILED(createFenceHr)) {
        outData.clear();
        if (outMessage) {
            *outMessage = "failed to create DirectStorage completion fence";
        }
        return false;
    }

    ScopedWinHandle completionEvent(CreateEvent(nullptr, FALSE, FALSE, nullptr));
    if (completionEvent.get() == nullptr) {
        outData.clear();
        if (outMessage) {
            *outMessage = "failed to create completion event";
        }
        return false;
    }

    constexpr uint64_t kFenceValue = 1;
    const HRESULT setEventHr = fence->SetEventOnCompletion(kFenceValue, completionEvent.get());
    if (FAILED(setEventHr)) {
        outData.clear();
        if (outMessage) {
            *outMessage = "failed to arm DirectStorage completion event";
        }
        return false;
    }

    {
        std::scoped_lock queueLock(m_impl->systemQueueMutex);

        DSTORAGE_REQUEST request{};
        request.Options.SourceType = DSTORAGE_REQUEST_SOURCE_FILE;
        request.Options.DestinationType = DSTORAGE_REQUEST_DESTINATION_MEMORY;
        request.Options.CompressionFormat = DSTORAGE_COMPRESSION_FORMAT_NONE;
        request.Source.File.Source = file.Get();
        request.Source.File.Offset = 0;
        request.Source.File.Size = static_cast<uint32_t>(fileSize);
        request.Destination.Memory.Buffer = outData.data();
        request.Destination.Memory.Size = static_cast<uint32_t>(fileSize);
        request.UncompressedSize = static_cast<uint32_t>(fileSize);
        request.CancellationTag = reinterpret_cast<uint64_t>(this);

        m_impl->systemMemoryQueue->EnqueueRequest(&request);
        m_impl->systemMemoryQueue->EnqueueSignal(fence.Get(), kFenceValue);
        m_impl->systemMemoryQueue->Submit();

        const DWORD waitResult = WaitForSingleObject(completionEvent.get(), INFINITE);
        if (waitResult != WAIT_OBJECT_0) {
            outData.clear();
            if (outMessage) {
                *outMessage = "DirectStorage wait failed";
            }
            return false;
        }

        DSTORAGE_ERROR_RECORD errorRecord{};
        m_impl->systemMemoryQueue->RetrieveErrorRecord(&errorRecord);
        if (FAILED(errorRecord.FirstFailure.HResult)) {
            outData.clear();
            if (outMessage) {
                *outMessage = "DirectStorage request failed";
            }
            spdlog::warn(
                "DirectStorageManager: request failed for '{}' hr=0x{:08X}",
                std::filesystem::path(path).string(),
                static_cast<unsigned int>(errorRecord.FirstFailure.HResult));
            return false;
        }
    }

    if (outMessage) {
        *outMessage = "read file through DirectStorage system-memory queue";
    }
    return true;
#endif
}

bool DirectStorageManager::UploadTextureRegionsFromFile(
    const std::wstring& path,
    rhi::Resource destinationResource,
    const std::vector<DirectStorageTextureRegionCopy>& regions,
    std::string* outMessage) {
    if (outMessage) {
        outMessage->clear();
    }

    if (path.empty()) {
        if (outMessage) {
            *outMessage = "path is empty";
        }
        return false;
    }

    if (!destinationResource.IsValid()) {
        if (outMessage) {
            *outMessage = "destination resource is invalid";
        }
        return false;
    }

    if (regions.empty()) {
        if (outMessage) {
            *outMessage = "no texture regions were provided";
        }
        return false;
    }

    if (!CanServiceQueue(DirectStorageQueueKind::Gpu)) {
        if (outMessage) {
            *outMessage = m_statusMessage.empty() ? "DirectStorage GPU queue unavailable" : m_statusMessage;
        }
        return false;
    }

#if !BASICRENDERER_HAS_DIRECTSTORAGE
    if (outMessage) {
        *outMessage = "DirectStorage SDK is not available in this build";
    }
    return false;
#else
    auto primeResult = PrimeFileHandle(path);
    if (!primeResult.success) {
        if (outMessage) {
            *outMessage = primeResult.message;
        }
        return false;
    }

    Microsoft::WRL::ComPtr<IDStorageFile> file;
    {
        std::scoped_lock fileLock(m_impl->fileCacheMutex);
        auto it = m_impl->fileCache.find(path);
        if (it == m_impl->fileCache.end() || it->second == nullptr) {
            if (outMessage) {
                *outMessage = "cached DirectStorage file handle missing";
            }
            return false;
        }
        file = it->second;
    }

    rhi::D3D12ResourceInfo resourceInfo{};
    if (!rhi::QueryNativeResource(destinationResource, rhi::RHI_IID_D3D12_RESOURCE, &resourceInfo, sizeof(resourceInfo)) || resourceInfo.resource == nullptr) {
        if (outMessage) {
            *outMessage = "failed to query native D3D12 texture resource";
        }
        return false;
    }

    auto* nativeResource = static_cast<ID3D12Resource*>(resourceInfo.resource);
    const D3D12_RESOURCE_DESC destinationDesc = nativeResource->GetDesc();
    const uint32_t subresourceCount = static_cast<uint32_t>(destinationDesc.MipLevels) * static_cast<uint32_t>(destinationDesc.DepthOrArraySize);
    if (subresourceCount == 0) {
        if (outMessage) {
            *outMessage = "destination texture has zero subresources";
        }
        return false;
    }

    Microsoft::WRL::ComPtr<ID3D12Device> nativeDevice;
    const HRESULT getDeviceHr = nativeResource->GetDevice(IID_PPV_ARGS(nativeDevice.ReleaseAndGetAddressOf()));
    if (FAILED(getDeviceHr)) {
        if (outMessage) {
            *outMessage = "failed to get native D3D12 device from texture resource";
        }
        return false;
    }

    Microsoft::WRL::ComPtr<ID3D12Fence> fence;
    const HRESULT createFenceHr = nativeDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.ReleaseAndGetAddressOf()));
    if (FAILED(createFenceHr)) {
        if (outMessage) {
            *outMessage = "failed to create DirectStorage GPU completion fence";
        }
        return false;
    }

    ScopedWinHandle completionEvent(CreateEvent(nullptr, FALSE, FALSE, nullptr));
    if (completionEvent.get() == nullptr) {
        if (outMessage) {
            *outMessage = "failed to create DirectStorage GPU completion event";
        }
        return false;
    }

    constexpr uint64_t kFenceValue = 1;
    const HRESULT setEventHr = fence->SetEventOnCompletion(kFenceValue, completionEvent.get());
    if (FAILED(setEventHr)) {
        if (outMessage) {
            *outMessage = "failed to arm DirectStorage GPU completion event";
        }
        return false;
    }

    {
        std::scoped_lock queueLock(m_impl->gpuQueueMutex);

        for (const auto& region : regions) {
            if (region.sourceSizeBytes == 0 || region.uncompressedSizeBytes == 0 || region.width == 0 || region.height == 0 || region.depth == 0) {
                if (outMessage) {
                    *outMessage = "invalid texture region request";
                }
                return false;
            }

            DSTORAGE_REQUEST request{};
            request.Options.SourceType = DSTORAGE_REQUEST_SOURCE_FILE;
            request.Options.DestinationType = DSTORAGE_REQUEST_DESTINATION_TEXTURE_REGION;
            request.Options.CompressionFormat = DSTORAGE_COMPRESSION_FORMAT_NONE;
            request.Source.File.Source = file.Get();
            request.Source.File.Offset = region.sourceOffset;
            request.Source.File.Size = region.sourceSizeBytes;
            request.Destination.Texture.Resource = nativeResource;
            request.Destination.Texture.SubresourceIndex = region.subresourceIndex;

            D3D12_BOX destinationBox{};
            destinationBox.left = 0;
            destinationBox.top = 0;
            destinationBox.front = 0;
            destinationBox.right = region.width;
            destinationBox.bottom = region.height;
            destinationBox.back = region.depth;

            request.Destination.Texture.Region = destinationBox;
            request.UncompressedSize = region.uncompressedSizeBytes;
            request.CancellationTag = reinterpret_cast<uint64_t>(this);

            m_impl->gpuQueue->EnqueueRequest(&request);
        }

        m_impl->gpuQueue->EnqueueSignal(fence.Get(), kFenceValue);
        m_impl->gpuQueue->Submit();

        const DWORD waitResult = WaitForSingleObject(completionEvent.get(), INFINITE);
        if (waitResult != WAIT_OBJECT_0) {
            if (outMessage) {
                *outMessage = "DirectStorage GPU wait failed";
            }
            return false;
        }

        DSTORAGE_ERROR_RECORD errorRecord{};
        m_impl->gpuQueue->RetrieveErrorRecord(&errorRecord);
        if (FAILED(errorRecord.FirstFailure.HResult)) {
            if (outMessage) {
                *outMessage = "DirectStorage GPU upload failed";
            }
            spdlog::warn(
                "DirectStorageManager: GPU upload failed for '{}' hr=0x{:08X}",
                std::filesystem::path(path).string(),
                static_cast<unsigned int>(errorRecord.FirstFailure.HResult));
            return false;
        }
    }

    if (outMessage) {
        *outMessage = "uploaded texture subresources through DirectStorage GPU queue";
    }
    return true;
#endif
}

bool DirectStorageManager::UploadTextureSubresourcesFromFile(
    const std::wstring& path,
    rhi::Resource destinationResource,
    uint64_t sourceOffset,
    uint32_t sourceSizeBytes,
    std::string* outMessage) {
    if (outMessage) {
        outMessage->clear();
    }

    if (path.empty()) {
        if (outMessage) {
            *outMessage = "path is empty";
        }
        return false;
    }

    if (!destinationResource.IsValid()) {
        if (outMessage) {
            *outMessage = "destination resource is invalid";
        }
        return false;
    }

    if (sourceSizeBytes == 0) {
        if (outMessage) {
            *outMessage = "source size is zero";
        }
        return false;
    }

    if (!CanServiceQueue(DirectStorageQueueKind::Gpu)) {
        if (outMessage) {
            *outMessage = m_statusMessage.empty() ? "DirectStorage GPU queue unavailable" : m_statusMessage;
        }
        return false;
    }

#if !BASICRENDERER_HAS_DIRECTSTORAGE
    if (outMessage) {
        *outMessage = "DirectStorage SDK is not available in this build";
    }
    return false;
#else
    auto primeResult = PrimeFileHandle(path);
    if (!primeResult.success) {
        if (outMessage) {
            *outMessage = primeResult.message;
        }
        return false;
    }

    Microsoft::WRL::ComPtr<IDStorageFile> file;
    {
        std::scoped_lock fileLock(m_impl->fileCacheMutex);
        auto it = m_impl->fileCache.find(path);
        if (it == m_impl->fileCache.end() || it->second == nullptr) {
            if (outMessage) {
                *outMessage = "cached DirectStorage file handle missing";
            }
            return false;
        }
        file = it->second;
    }

    rhi::D3D12ResourceInfo resourceInfo{};
    if (!rhi::QueryNativeResource(destinationResource, rhi::RHI_IID_D3D12_RESOURCE, &resourceInfo, sizeof(resourceInfo)) || resourceInfo.resource == nullptr) {
        if (outMessage) {
            *outMessage = "failed to query native D3D12 texture resource";
        }
        return false;
    }

    auto* nativeResource = static_cast<ID3D12Resource*>(resourceInfo.resource);
    const D3D12_RESOURCE_DESC destinationDesc = nativeResource->GetDesc();
    const uint32_t subresourceCount = static_cast<uint32_t>(destinationDesc.MipLevels) * static_cast<uint32_t>(destinationDesc.DepthOrArraySize);
    if (subresourceCount == 0) {
        if (outMessage) {
            *outMessage = "destination texture has zero subresources";
        }
        return false;
    }

    Microsoft::WRL::ComPtr<ID3D12Device> nativeDevice;
    const HRESULT getDeviceHr = nativeResource->GetDevice(IID_PPV_ARGS(nativeDevice.ReleaseAndGetAddressOf()));
    if (FAILED(getDeviceHr)) {
        if (outMessage) {
            *outMessage = "failed to get native D3D12 device from texture resource";
        }
        return false;
    }

    Microsoft::WRL::ComPtr<ID3D12Fence> fence;
    const HRESULT createFenceHr = nativeDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.ReleaseAndGetAddressOf()));
    if (FAILED(createFenceHr)) {
        if (outMessage) {
            *outMessage = "failed to create DirectStorage GPU completion fence";
        }
        return false;
    }

    ScopedWinHandle completionEvent(CreateEvent(nullptr, FALSE, FALSE, nullptr));
    if (completionEvent.get() == nullptr) {
        if (outMessage) {
            *outMessage = "failed to create DirectStorage GPU completion event";
        }
        return false;
    }

    constexpr uint64_t kFenceValue = 1;
    const HRESULT setEventHr = fence->SetEventOnCompletion(kFenceValue, completionEvent.get());
    if (FAILED(setEventHr)) {
        if (outMessage) {
            *outMessage = "failed to arm DirectStorage GPU completion event";
        }
        return false;
    }

    {
        std::scoped_lock queueLock(m_impl->gpuQueueMutex);

        DSTORAGE_REQUEST request{};
        request.Options.SourceType = DSTORAGE_REQUEST_SOURCE_FILE;
        request.Options.DestinationType = DSTORAGE_REQUEST_DESTINATION_MULTIPLE_SUBRESOURCES_RANGE;
        request.Options.CompressionFormat = DSTORAGE_COMPRESSION_FORMAT_NONE;
        request.Source.File.Source = file.Get();
        request.Source.File.Offset = sourceOffset;
        request.Source.File.Size = sourceSizeBytes;
        request.Destination.MultipleSubresourcesRange.Resource = nativeResource;
        request.Destination.MultipleSubresourcesRange.FirstSubresource = 0;
        request.Destination.MultipleSubresourcesRange.NumSubresources = subresourceCount;
        request.UncompressedSize = sourceSizeBytes;
        request.CancellationTag = reinterpret_cast<uint64_t>(this);

        m_impl->gpuQueue->EnqueueRequest(&request);
        m_impl->gpuQueue->EnqueueSignal(fence.Get(), kFenceValue);
        m_impl->gpuQueue->Submit();

        const DWORD waitResult = WaitForSingleObject(completionEvent.get(), INFINITE);
        if (waitResult != WAIT_OBJECT_0) {
            if (outMessage) {
                *outMessage = "DirectStorage GPU wait failed";
            }
            return false;
        }

        DSTORAGE_ERROR_RECORD errorRecord{};
        m_impl->gpuQueue->RetrieveErrorRecord(&errorRecord);
        if (FAILED(errorRecord.FirstFailure.HResult)) {
            if (outMessage) {
                *outMessage = "DirectStorage GPU upload failed";
            }
            spdlog::warn(
                "DirectStorageManager: conditioned GPU upload failed for '{}' hr=0x{:08X}",
                std::filesystem::path(path).string(),
                static_cast<unsigned int>(errorRecord.FirstFailure.HResult));
            return false;
        }
    }

    if (outMessage) {
        *outMessage = "uploaded conditioned texture cache through DirectStorage GPU queue";
    }
    return true;
#endif
}

void DirectStorageManager::ReleaseFileHandle(const std::wstring& path) {
#if BASICRENDERER_HAS_DIRECTSTORAGE
    if (m_impl == nullptr || path.empty()) {
        return;
    }

    std::scoped_lock lock(m_impl->fileCacheMutex);
    m_impl->fileCache.erase(path);
#else
    (void)path;
#endif
}

bool DirectStorageManager::HasPrimedFileHandle(const std::wstring& path) const {
#if BASICRENDERER_HAS_DIRECTSTORAGE
    if (m_impl == nullptr || path.empty()) {
        return false;
    }

    std::scoped_lock lock(m_impl->fileCacheMutex);
    return m_impl->fileCache.find(path) != m_impl->fileCache.end();
#else
    (void)path;
    return false;
#endif
}

}