#pragma once

#include <stdexcept>

#include "Render/Runtime/IDescriptorService.h"

namespace rg::runtime {

inline IDescriptorService*& DescriptorServiceSlot() {
    static IDescriptorService* service = nullptr;
    return service;
}

inline void SetActiveDescriptorService(IDescriptorService* service) {
    DescriptorServiceSlot() = service;
}

inline IDescriptorService* GetActiveDescriptorService() {
    return DescriptorServiceSlot();
}

inline UINT CreateIndexedSamplerFromActiveDescriptorService(const rhi::SamplerDesc& samplerDesc) {
    auto* service = GetActiveDescriptorService();
    if (!service) {
        throw std::runtime_error("Descriptor service is not active for sampler creation");
    }
    return service->CreateIndexedSampler(samplerDesc);
}

inline rhi::DescriptorHeap GetActiveSRVDescriptorHeap() {
    auto* service = GetActiveDescriptorService();
    if (!service) {
        throw std::runtime_error("Descriptor service is not active for SRV descriptor heap access");
    }
    return service->GetSRVDescriptorHeap();
}

inline rhi::DescriptorHeap GetActiveSamplerDescriptorHeap() {
    auto* service = GetActiveDescriptorService();
    if (!service) {
        throw std::runtime_error("Descriptor service is not active for sampler descriptor heap access");
    }
    return service->GetSamplerDescriptorHeap();
}

} // namespace rg::runtime
