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
