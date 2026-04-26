#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <rhi.h>
#include <string>
#include <vector>

namespace br {

enum class DirectStorageQueueKind : uint8_t {
    SystemMemory = 0,
    Gpu,
};

struct DirectStorageCapabilities {
    bool sdkAvailable = false;
    bool initialized = false;
    bool enabled = false;
    bool runtimeDisabled = false;
    bool systemMemoryQueueAvailable = false;
    bool gpuQueueAvailable = false;
    bool supportsQueue2 = false;
    bool supportsQueue3 = false;
    uint32_t stagingBufferSizeBytes = 0;
};

struct DirectStorageOpenFileResult {
    bool success = false;
    bool cached = false;
    std::string message;
};

struct DirectStorageTextureRegionCopy {
    uint64_t sourceOffset = 0;
    uint32_t sourceSizeBytes = 0;
    uint32_t uncompressedSizeBytes = 0;
    uint32_t subresourceIndex = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t depth = 1;
};

struct DirectStorageBufferRegionCopy {
    uint64_t sourceOffset = 0;
    uint32_t sourceSizeBytes = 0;
    uint32_t uncompressedSizeBytes = 0;
    rhi::Resource destinationResource{};
    uint64_t destinationOffset = 0;
};


class DirectStorageManager {
public:
    static DirectStorageManager& GetInstance();

    void Initialize();
    void Cleanup();

    bool IsInitialized() const {
        return m_initialized;
    }

    bool IsAvailable() const {
        return m_available;
    }

    bool IsEnabled() const {
        return m_enabled;
    }

    bool IsRuntimeDisabled() const {
        return m_runtimeDisabled;
    }

    uint32_t GetStagingBufferSizeBytes() const {
        return m_stagingBufferSizeBytes;
    }

    const std::string& GetStatusMessage() const {
        return m_statusMessage;
    }

    DirectStorageCapabilities GetCapabilities() const;
    bool CanServiceQueue(DirectStorageQueueKind queueKind) const;

    DirectStorageOpenFileResult PrimeFileHandle(const std::wstring& path);
    void ReleaseFileHandle(const std::wstring& path);
    bool HasPrimedFileHandle(const std::wstring& path) const;
    bool ReadFileRegionToMemory(
        const std::wstring& path,
        uint64_t sourceOffset,
        uint32_t sourceSizeBytes,
        std::vector<std::byte>& outData,
        std::string* outMessage = nullptr);
    bool ReadFileToMemory(const std::wstring& path, std::vector<std::byte>& outData, std::string* outMessage = nullptr);
    bool UploadBufferRegionsFromFile(
        const std::wstring& path,
        const std::vector<DirectStorageBufferRegionCopy>& regions,
        std::string* outMessage = nullptr);
    bool UploadTextureSubresourcesFromFile(
        const std::wstring& path,
        rhi::Resource destinationResource,
        uint64_t sourceOffset,
        uint32_t sourceSizeBytes,
        std::string* outMessage = nullptr);
    bool UploadTextureRegionsFromFile(
        const std::wstring& path,
        rhi::Resource destinationResource,
        const std::vector<DirectStorageTextureRegionCopy>& regions,
        std::string* outMessage = nullptr);

private:
    DirectStorageManager() = default;

    struct Impl;

    std::unique_ptr<Impl> m_impl;
    bool m_initialized = false;
    bool m_available = false;
    bool m_enabled = false;
    bool m_runtimeDisabled = false;
    uint32_t m_stagingBufferSizeBytes = 0;
    std::string m_statusMessage;
};

}

using DirectStorageManager = br::DirectStorageManager;
using DirectStorageQueueKind = br::DirectStorageQueueKind;
using DirectStorageBufferRegionCopy = br::DirectStorageBufferRegionCopy;