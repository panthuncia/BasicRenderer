#pragma once
#include <directx/d3d12.h>
#include <memory>
#include "Managers/Singletons/ResourceManager.h"

struct SamplerDescHasher {
    std::size_t operator()(const D3D12_SAMPLER_DESC& desc) const {
        std::size_t h = 0;
        // Hash each relevant field and combine
        hash_combine(h, std::hash<UINT>()(static_cast<UINT>(desc.Filter)));
        hash_combine(h, std::hash<UINT>()(static_cast<UINT>(desc.AddressU)));
        hash_combine(h, std::hash<UINT>()(static_cast<UINT>(desc.AddressV)));
        hash_combine(h, std::hash<UINT>()(static_cast<UINT>(desc.AddressW)));
        hash_combine(h, std::hash<FLOAT>()(desc.MipLODBias));
        hash_combine(h, std::hash<UINT>()(desc.MaxAnisotropy));
        hash_combine(h, std::hash<D3D12_COMPARISON_FUNC>()(desc.ComparisonFunc));
        for(int i = 0; i < 4; ++i) {
            hash_combine(h, std::hash<FLOAT>()(desc.BorderColor[i]));
        }
        hash_combine(h, std::hash<FLOAT>()(desc.MinLOD));
        hash_combine(h, std::hash<FLOAT>()(desc.MaxLOD));
        return h;
    }
};

struct SamplerDescComparator {
    bool operator()(const D3D12_SAMPLER_DESC& a, const D3D12_SAMPLER_DESC& b) const {
        // Check each field for equality
        if (a.Filter != b.Filter ||
            a.AddressU != b.AddressU || 
            a.AddressV != b.AddressV ||
            a.AddressW != b.AddressW ||
            a.MipLODBias != b.MipLODBias ||
            a.MaxAnisotropy != b.MaxAnisotropy ||
            a.ComparisonFunc != b.ComparisonFunc ||
            a.MinLOD != b.MinLOD ||
            a.MaxLOD != b.MaxLOD)
        {
            return false;
        }
        for (int i = 0; i < 4; ++i) {
            if (a.BorderColor[i] != b.BorderColor[i]) {
                return false;
            }
        }
        return true;
    }
};

class Sampler {
public:
    static std::shared_ptr<Sampler> CreateSampler(D3D12_SAMPLER_DESC samplerDesc) {
		auto it = m_samplerCache.find(samplerDesc);
		if (it != m_samplerCache.end()) {
			return it->second;
		}
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
	static std::unordered_map<D3D12_SAMPLER_DESC, std::shared_ptr<Sampler>, SamplerDescHasher, SamplerDescComparator> m_samplerCache;
};
