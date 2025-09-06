#pragma once
#include <memory>
#include "Managers/Singletons/ResourceManager.h"

namespace rhi {

    inline uint32_t float_bits_norm(float f) noexcept {
        uint32_t u;
        std::memcpy(&u, &f, sizeof(u));
        // canonicalize -0.0 to +0.0 so they compare equal
        if ((u & 0x7fffffffU) == 0) u = 0;
        return u;
    }

    inline bool uses_border(AddressMode u, AddressMode v, AddressMode w) noexcept {
        return (u == AddressMode::Border) ||
            (v == AddressMode::Border) ||
            (w == AddressMode::Border);
    }

    inline SamplerDesc canonicalize(SamplerDesc s) noexcept {
        // If compare is disabled, compareOp is irrelevant
        if (!s.compareEnable) {
            s.compareOp = CompareOp::Always;
        }

        // Anisotropy disabled when <= 1: normalize to 1
        if (s.maxAnisotropy <= 1u) s.maxAnisotropy = 1u;

        const bool needBorder = uses_border(s.addressU, s.addressV, s.addressW);

        // If no axis uses Border, border preset/color are irrelevant -> normalize
        if (!needBorder) {
            s.borderPreset = BorderPreset::TransparentBlack;
            s.borderColor[0] = s.borderColor[1] = s.borderColor[2] = s.borderColor[3] = 0.0f;
        }
        else {
            // If preset is not Custom, borderColor is irrelevant -> normalize
            if (s.borderPreset != BorderPreset::Custom) {
                s.borderColor[0] = s.borderColor[1] = s.borderColor[2] = s.borderColor[3] = 0.0f;
            }
        }

        return s;
    }

    struct SamplerDescEq {
        bool operator()(const SamplerDesc& a_, const SamplerDesc& b_) const noexcept {
            SamplerDesc a = canonicalize(a_);
            SamplerDesc b = canonicalize(b_);

            if (a.minFilter != b.minFilter) return false;
            if (a.magFilter != b.magFilter) return false;
            if (a.mipFilter != b.mipFilter) return false;

            if (a.addressU != b.addressU) return false;
            if (a.addressV != b.addressV) return false;
            if (a.addressW != b.addressW) return false;

            if (a.mipLodBias != b.mipLodBias) return false;
            if (a.minLod != b.minLod) return false;
            if (a.maxLod != b.maxLod) return false;

            if (a.maxAnisotropy != b.maxAnisotropy) return false;
            if (a.compareEnable != b.compareEnable) return false;
            if (a.compareOp != b.compareOp) return false;
            if (a.reduction != b.reduction) return false;

            if (a.borderPreset != b.borderPreset) return false;

            if (a.unnormalizedCoordinates != b.unnormalizedCoordinates) return false;

            // Only relevant when preset == Custom AND a border mode is used
            if (a.borderPreset == BorderPreset::Custom &&
                uses_border(a.addressU, a.addressV, a.addressW)) {
                for (int i = 0; i < 4; ++i) {
                    if (float_bits_norm(a.borderColor[i]) != float_bits_norm(b.borderColor[i]))
                        return false;
                }
            }

            return true;
        }
    };

    struct SamplerDescHash {
        size_t operator()(SamplerDesc s) const noexcept {
            s = canonicalize(s);
            auto mix = [](size_t& seed, size_t v) {
                seed ^= v + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
                };

            size_t h = 0;
            // Filters
            mix(h, std::hash<uint8_t>{}(static_cast<uint8_t>(s.minFilter)));
            mix(h, std::hash<uint8_t>{}(static_cast<uint8_t>(s.magFilter)));
            mix(h, std::hash<uint8_t>{}(static_cast<uint8_t>(s.mipFilter)));

            mix(h, std::hash<uint8_t>{}(static_cast<uint8_t>(s.addressU)));
            mix(h, std::hash<uint8_t>{}(static_cast<uint8_t>(s.addressV)));
            mix(h, std::hash<uint8_t>{}(static_cast<uint8_t>(s.addressW)));

            mix(h, float_bits_norm(s.mipLodBias));
            mix(h, float_bits_norm(s.minLod));
            mix(h, float_bits_norm(s.maxLod));

            mix(h, std::hash<uint32_t>{}(s.maxAnisotropy));
            mix(h, std::hash<uint8_t>{}(static_cast<uint8_t>(s.compareEnable)));
            mix(h, std::hash<uint8_t>{}(static_cast<uint8_t>(s.compareOp)));
            mix(h, std::hash<uint8_t>{}(static_cast<uint8_t>(s.reduction)));

            const bool needBorder = uses_border(s.addressU, s.addressV, s.addressW);
            mix(h, std::hash<uint8_t>{}(static_cast<uint8_t>(s.borderPreset)));
            if (needBorder && s.borderPreset == BorderPreset::Custom) {
                mix(h, float_bits_norm(s.borderColor[0]));
                mix(h, float_bits_norm(s.borderColor[1]));
                mix(h, float_bits_norm(s.borderColor[2]));
                mix(h, float_bits_norm(s.borderColor[3]));
            }

            // Vulkan-only flag (DX12 ignores it, but it changes behavior on VK)
            mix(h, std::hash<uint8_t>{}(static_cast<uint8_t>(s.unnormalizedCoordinates)));

            return h;
        }
    };

}

class Sampler {
public:
    static std::shared_ptr<Sampler> CreateSampler(rhi::SamplerDesc samplerDesc) {
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

    static std::shared_ptr<Sampler> GetDefaultSampler();
	static std::shared_ptr<Sampler> GetDefaultShadowSampler();

private:
    UINT m_index; // Index of the sampler in the descriptor heap
    rhi::SamplerDesc m_samplerDesc; // Descriptor of the sampler
    Sampler(rhi::SamplerDesc samplerDesc)
        : m_index(0), m_samplerDesc(samplerDesc) {
        m_index = ResourceManager::GetInstance().CreateIndexedSampler(m_samplerDesc);
    }

    static std::shared_ptr<Sampler> m_defaultSampler;
	static std::shared_ptr<Sampler> m_defaultShadowSampler;
	static std::unordered_map<rhi::SamplerDesc, std::shared_ptr<Sampler>, rhi::SamplerDescHash, rhi::SamplerDescEq> m_samplerCache;
};
