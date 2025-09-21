#pragma once

#include <rhi.h>
#include <stdint.h>
#include <unordered_map>
#include <string>

#include "Resources/ResourceIdentifier.h"
#include <Utilities/HashMix.h>

struct PipelineResources {
	std::vector<ResourceIdentifier> mandatoryResourceDescriptorSlots;
	std::vector<ResourceIdentifier> optionalResourceDescriptorSlots;
};

class PipelineState {
public:
	PipelineState(rhi::PipelinePtr pso,
		uint64_t resourceIDsHash, 
		PipelineResources resources) :
		m_resourceIDsHash(resourceIDsHash), 
		m_pso(std::move(pso)),
		m_pipelineResources(resources){}
	PipelineState() : m_pso({}) {};
	const rhi::Pipeline& GetAPIPipelineState() const {
		return m_pso.Get();
	}
	uint64_t GetResourceIDsHash() const {
		return m_resourceIDsHash;
	}
	const PipelineResources& GetResourceDescriptorSlots() const {
		return m_pipelineResources;
	}
private:
	uint64_t m_resourceIDsHash = 0;
	rhi::PipelinePtr m_pso;
	std::unordered_map<std::string, unsigned int> m_resourceSlots;
	PipelineResources m_pipelineResources; // Descriptor slots are always 0->n, mandatory first, then optional
};

struct PSOKey {
	uint64_t rootSigId;
	uint64_t shaderProgramId; // compiled with feature defines for this family variant
	uint32_t rasterDepthBlendBits;
	uint32_t rtFormatBits;
	bool operator==(const PSOKey&) const = default;
};
struct PSOKeyHash { size_t operator()(PSOKey const& k) const noexcept { return util::hash_mix(k); } };