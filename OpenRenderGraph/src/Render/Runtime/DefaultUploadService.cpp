#include "Render/Runtime/IUploadService.h"

#include "Managers/Singletons/UploadManager.h"

namespace rg::runtime {

namespace {
class DefaultUploadService final : public IUploadService {
public:
    void Initialize() override {
        UploadManager::GetInstance().Initialize();
    }

    void SetUploadResolveContext(ResourceRegistry* registry, uint64_t epoch) override {
        UploadManager::UploadResolveContext context{};
        context.registry = registry;
        context.epoch = epoch;
        UploadManager::GetInstance().SetUploadResolveContext(context);
    }

    std::shared_ptr<RenderPass> GetUploadPass() const override {
        return UploadManager::GetInstance().GetUploadPass();
    }

    void UploadDataToShared(const void* data, size_t size, std::shared_ptr<Resource> resourceToUpdate, size_t dataBufferOffset, const char* file, int line) override {
#if BUILD_TYPE == BUILD_TYPE_DEBUG
        UploadManager::GetInstance().UploadData(data, size, UploadManager::UploadTarget::FromShared(std::move(resourceToUpdate)), dataBufferOffset, file, line);
#else
        (void)file;
        (void)line;
        UploadManager::GetInstance().UploadData(data, size, UploadManager::UploadTarget::FromShared(std::move(resourceToUpdate)), dataBufferOffset);
#endif
    }

    void UploadDataToHandle(const void* data, size_t size, const ResourceRegistry::RegistryHandle& resourceToUpdate, size_t dataBufferOffset, const char* file, int line) override {
#if BUILD_TYPE == BUILD_TYPE_DEBUG
        UploadManager::GetInstance().UploadData(data, size, UploadManager::UploadTarget::FromHandle(resourceToUpdate), dataBufferOffset, file, line);
#else
        (void)file;
        (void)line;
        UploadManager::GetInstance().UploadData(data, size, UploadManager::UploadTarget::FromHandle(resourceToUpdate), dataBufferOffset);
#endif
    }

    void UploadTextureSubresourcesToShared(
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
        int line) override {
#if BUILD_TYPE == BUILD_TYPE_DEBUG
        UploadManager::GetInstance().UploadTextureSubresources(
            UploadManager::UploadTarget::FromShared(std::move(target)),
            fmt,
            baseWidth,
            baseHeight,
            depthOrLayers,
            mipLevels,
            arraySize,
            srcSubresources,
            srcCount,
            file,
            line);
#else
        (void)file;
        (void)line;
        UploadManager::GetInstance().UploadTextureSubresources(
            UploadManager::UploadTarget::FromShared(std::move(target)),
            fmt,
            baseWidth,
            baseHeight,
            depthOrLayers,
            mipLevels,
            arraySize,
            srcSubresources,
            srcCount);
#endif
    }

    void UploadTextureSubresourcesToHandle(
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
        int line) override {
#if BUILD_TYPE == BUILD_TYPE_DEBUG
        UploadManager::GetInstance().UploadTextureSubresources(
            UploadManager::UploadTarget::FromHandle(target),
            fmt,
            baseWidth,
            baseHeight,
            depthOrLayers,
            mipLevels,
            arraySize,
            srcSubresources,
            srcCount,
            file,
            line);
#else
        (void)file;
        (void)line;
        UploadManager::GetInstance().UploadTextureSubresources(
            UploadManager::UploadTarget::FromHandle(target),
            fmt,
            baseWidth,
            baseHeight,
            depthOrLayers,
            mipLevels,
            arraySize,
            srcSubresources,
            srcCount);
#endif
    }

    void QueueResourceCopy(const std::shared_ptr<Resource>& destination, const std::shared_ptr<Resource>& source, size_t size) override {
        UploadManager::GetInstance().QueueResourceCopy(destination, source, size);
    }

    void ProcessDeferredReleases(uint8_t frameIndex) override {
        UploadManager::GetInstance().ProcessDeferredReleases(frameIndex);
    }

    void Cleanup() override {
        UploadManager::GetInstance().Cleanup();
    }
};
}

std::shared_ptr<IUploadService> CreateDefaultUploadService() {
    return std::make_shared<DefaultUploadService>();
}

}
