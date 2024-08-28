#pragma once
#include <d3d12.h>
#include "ResourceManager.h"

class Sampler {
public:
    Sampler(D3D12_SAMPLER_DESC samplerDesc)
        : m_index(0), m_samplerDesc(samplerDesc) {
        createSampler();
    }

    ~Sampler() {
    }

    // Disallow copy and assignment
    Sampler(const Sampler&) = delete;
    Sampler& operator=(const Sampler&) = delete;

    // Get the index of the sampler in the descriptor heap
    UINT GetDescriptorIndex() const {
        return m_index;
    }

    // Get the CPU handle for the sampler from the ResourceManager
    D3D12_CPU_DESCRIPTOR_HANDLE getHandle() const {
        return ResourceManager::GetInstance().getCPUHandleForSampler(m_index);
    }

private:
    UINT m_index; // Index of the sampler in the descriptor heap
    D3D12_SAMPLER_DESC m_samplerDesc; // Descriptor of the sampler

    void createSampler() {
        m_index = ResourceManager::GetInstance().CreateIndexedSampler(m_samplerDesc);
    }
};
