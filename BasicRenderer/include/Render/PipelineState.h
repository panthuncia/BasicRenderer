#pragma once

#include <d3d12.h>
#include <wrl/client.h>
#include <stdint.h>
#include <unordered_map>
#include <string>

#include "Resources/ResourceIdentifier.h"

class PipelineState {
public:
	PipelineState(Microsoft::WRL::ComPtr<ID3D12PipelineState> pso,
		uint64_t resourceIDsHash, 
		std::vector<ResourceIdentifier> resourceDescriptorSlotMap) :
		m_resourceIDsHash(resourceIDsHash), 
		m_pso(pso),
		m_resourceDescriptorSlotMap(resourceDescriptorSlotMap){}
	PipelineState() = default;
	ID3D12PipelineState* GetAPIPipelineState() const {
		return m_pso.Get();
	}
	uint64_t GetResourceIDsHash() const {
		return m_resourceIDsHash;
	}
	std::vector<ResourceIdentifier>& GetResourceDescriptorSlotMap() const {
		return m_resourceDescriptorSlotMap;
	}
private:
	uint64_t m_resourceIDsHash = 0;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pso;
	std::unordered_map<std::string, unsigned int> m_resourceSlots;
	//std::unordered_map<ResourceIdentifier, unsigned int> m_resourceDescriptorSlotMap;
	std::vector<ResourceIdentifier> m_resourceDescriptorSlotMap; // Descriptor slots are always 0->n
};