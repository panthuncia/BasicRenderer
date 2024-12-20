#pragma once
#include <directx/d3d12.h>
#include <memory>
#include "ResourceManager.h"

class Sampler {
public:
    static std::shared_ptr<Sampler> CreateSampler(D3D12_SAMPLER_DESC samplerDesc) {
		return std::shared_ptr<Sampler>(new Sampler(samplerDesc));
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

    D3D12_CPU_DESCRIPTOR_HANDLE getHandle() const {
        return ResourceManager::GetInstance().getSamplerCPUHandle(m_index);
    }

    static std::shared_ptr<Sampler> GetDefaultSampler();
	static std::shared_ptr<Sampler> GetDefaultShadowSampler();

private:
    UINT m_index; // Index of the sampler in the descriptor heap
    D3D12_SAMPLER_DESC m_samplerDesc; // Descriptor of the sampler
    Sampler(D3D12_SAMPLER_DESC samplerDesc)
        : m_index(0), m_samplerDesc(samplerDesc) {
        m_index = ResourceManager::GetInstance().CreateIndexedSampler(m_samplerDesc);
    }

    static std::shared_ptr<Sampler> m_defaultSampler;
	static std::shared_ptr<Sampler> m_defaultShadowSampler;
};
