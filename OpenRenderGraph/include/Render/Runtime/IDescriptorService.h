#pragma once

#include <memory>

#include <rhi.h>

#include "Render/Runtime/DescriptorServiceTypes.h"

class GloballyIndexedResource;

namespace rg::runtime {

class IDescriptorService {
public:
    virtual ~IDescriptorService() = default;

    virtual void Initialize() = 0;
    virtual void Cleanup() = 0;

    virtual void AssignDescriptorSlots(
        GloballyIndexedResource& target,
        rhi::Resource& apiResource,
        const DescriptorViewRequirements& req) = 0;

    virtual void ReserveDescriptorSlots(
        GloballyIndexedResource& target,
        const DescriptorViewRequirements& req) = 0;

    virtual void UpdateDescriptorContents(
        GloballyIndexedResource& target,
        rhi::Resource& apiResource,
        const DescriptorViewRequirements& req) = 0;

    virtual rhi::DescriptorHeap GetSRVDescriptorHeap() const = 0;
    virtual rhi::DescriptorHeap GetSamplerDescriptorHeap() const = 0;
    virtual UINT CreateIndexedSampler(const rhi::SamplerDesc& samplerDesc) = 0;
};

std::shared_ptr<IDescriptorService> CreateDefaultDescriptorService();

} // namespace rg::runtime
