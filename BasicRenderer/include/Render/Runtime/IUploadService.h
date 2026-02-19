#pragma once

#include <cstdint>
#include <memory>

class ResourceRegistry;
class RenderPass;

namespace rg::runtime {

class IUploadService {
public:
    virtual ~IUploadService() = default;

    virtual void Initialize() = 0;
    virtual void SetUploadResolveContext(ResourceRegistry* registry, uint64_t epoch = 0) = 0;
    virtual std::shared_ptr<RenderPass> GetUploadPass() const = 0;
    virtual void ProcessDeferredReleases(uint8_t frameIndex) = 0;
    virtual void Cleanup() = 0;
};

std::shared_ptr<IUploadService> CreateDefaultUploadService();

}
