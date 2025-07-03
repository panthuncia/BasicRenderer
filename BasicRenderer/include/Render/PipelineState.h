#pragma once

#include <d3d12.h>
#include <wrl/client.h>
#include <stdint.h>
#include <unordered_map>
#include <string>

class PipelineState {
public:
	PipelineState(Microsoft::WRL::ComPtr<ID3D12PipelineState> pso,
		uint64_t resourceIDsHash, 
		std::unordered_map<std::string, unsigned int> resourceDescriptorSlotMap) : 
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
private:
	uint64_t m_resourceIDsHash = 0;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pso;
	std::unordered_map<std::string, unsigned int> m_resourceSlots;
	std::unordered_map<std::string, unsigned int> m_resourceDescriptorSlotMap;
};