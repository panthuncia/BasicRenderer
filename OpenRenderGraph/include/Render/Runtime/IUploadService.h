#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include <rhi.h>
#include <rhi_helpers.h>

#include "Render/ResourceRegistry.h"

class ResourceRegistry;
class RenderPass;
class Resource;

namespace rg::runtime {

class IUploadService {
public:
    virtual ~IUploadService() = default;

    virtual void Initialize() = 0;
    virtual void SetUploadResolveContext(ResourceRegistry* registry, uint64_t epoch = 0) = 0;
    virtual std::shared_ptr<RenderPass> GetUploadPass() const = 0;
    virtual void UploadDataToShared(const void* data, size_t size, std::shared_ptr<Resource> resourceToUpdate, size_t dataBufferOffset, const char* file, int line) = 0;
    virtual void UploadDataToHandle(const void* data, size_t size, const ResourceRegistry::RegistryHandle& resourceToUpdate, size_t dataBufferOffset, const char* file, int line) = 0;
    virtual void UploadTextureSubresourcesToShared(
        std::shared_ptr<Resource> target,
        rhi::Format fmt,
        uint32_t baseWidth,
        uint32_t baseHeight,
        uint32_t depthOrLayers,
        uint32_t mipLevels,
        uint32_t arraySize,
        const rhi::helpers::SubresourceData* srcSubresources,
        uint32_t srcCount,
        const char* file,
        int line) = 0;
    virtual void UploadTextureSubresourcesToHandle(
        const ResourceRegistry::RegistryHandle& target,
        rhi::Format fmt,
        uint32_t baseWidth,
        uint32_t baseHeight,
        uint32_t depthOrLayers,
        uint32_t mipLevels,
        uint32_t arraySize,
        const rhi::helpers::SubresourceData* srcSubresources,
        uint32_t srcCount,
        const char* file,
        int line) = 0;
    virtual void QueueResourceCopy(const std::shared_ptr<Resource>& destination, const std::shared_ptr<Resource>& source, size_t size) = 0;
    virtual void ProcessDeferredReleases(uint8_t frameIndex) = 0;
    virtual void Cleanup() = 0;
};

std::shared_ptr<IUploadService> CreateDefaultUploadService();

}
