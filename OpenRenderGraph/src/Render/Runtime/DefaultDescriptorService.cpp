#include "Render/Runtime/IDescriptorService.h"

#include "Managers/Singletons/DescriptorHeapManager.h"

namespace rg::runtime {

namespace {
class DefaultDescriptorService final : public IDescriptorService {
public:
    void Initialize() override {
        DescriptorHeapManager::GetInstance().Initialize();
    }

    void Cleanup() override {
        DescriptorHeapManager::GetInstance().Cleanup();
    }

    void AssignDescriptorSlots(
        GloballyIndexedResource& target,
        rhi::Resource& apiResource,
        const DescriptorViewRequirements& req) override {
        DescriptorHeapManager::GetInstance().AssignDescriptorSlots(target, apiResource, req);
    }

    void ReserveDescriptorSlots(
        GloballyIndexedResource& target,
        const DescriptorViewRequirements& req) override {
        DescriptorHeapManager::GetInstance().ReserveDescriptorSlots(target, req);
    }

    void UpdateDescriptorContents(
        GloballyIndexedResource& target,
        rhi::Resource& apiResource,
        const DescriptorViewRequirements& req) override {
        DescriptorHeapManager::GetInstance().UpdateDescriptorContents(target, apiResource, req);
    }

    rhi::DescriptorHeap GetSRVDescriptorHeap() const override {
        return DescriptorHeapManager::GetInstance().GetSRVDescriptorHeap();
    }

    rhi::DescriptorHeap GetSamplerDescriptorHeap() const override {
        return DescriptorHeapManager::GetInstance().GetSamplerDescriptorHeap();
    }

    UINT CreateIndexedSampler(const rhi::SamplerDesc& samplerDesc) override {
        return DescriptorHeapManager::GetInstance().CreateIndexedSampler(samplerDesc);
    }
};
} // namespace

std::shared_ptr<IDescriptorService> CreateDefaultDescriptorService() {
    return std::make_shared<DefaultDescriptorService>();
}

} // namespace rg::runtime
