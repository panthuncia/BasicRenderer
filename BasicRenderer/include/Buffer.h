#pragma once

#include <directx/d3d12.h>
#include <wrl/client.h>
#include <stdint.h>
#include <string>
#include <memory>
#include "DescriptorHeap.h"
#include "HeapIndexInfo.h"
#include "GloballyIndexedResource.h"
enum class ResourceCPUAccessType {
	READ,
	WRITE,
	READ_WRITE,
	NONE
};

D3D12_HEAP_TYPE TranslateAccessType(ResourceCPUAccessType accessType);

class RenderContext;

class Buffer : public GloballyIndexedResource{
public:
	static std::shared_ptr<Buffer> CreateShared(
		ID3D12Device* device,
		ResourceCPUAccessType accessType, 
		uint32_t bufferSize, 
		uint8_t numDataBuffers,
		bool upload,
		bool unorderedAccess) {
		return std::shared_ptr<Buffer>(new Buffer(device, accessType, bufferSize, numDataBuffers, upload, unorderedAccess));
	}
	static std::unique_ptr<Buffer> CreateUnique(
		ID3D12Device* device, 
		ResourceCPUAccessType accessType, 
		uint32_t bufferSize,
		uint8_t numDataBuffers,
		bool upload,
		bool unorderedAccess) {
		return std::unique_ptr<Buffer>(new Buffer(device, accessType, bufferSize, numDataBuffers, upload, unorderedAccess));
	}

	~Buffer() = default;
	ResourceCPUAccessType m_accessType;
	std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> m_buffers;
	std::vector<D3D12_RESOURCE_BARRIER>& GetTransitions(uint8_t frameIndex, ResourceState prevState, ResourceState newState);
	ID3D12Resource* GetAPIResource(uint8_t frameIndex) const override { return m_buffers[frameIndex].Get(); }
	uint8_t GetNumDataBuffers() const { return m_numDataBuffers; }
protected:
	void OnSetName() override;
private:
	std::vector<std::vector<D3D12_RESOURCE_BARRIER>> m_transitions;
	std::vector<D3D12_RESOURCE_BARRIER> m_emptyTransitions = {};
	Buffer(ID3D12Device* device, 
		ResourceCPUAccessType accessType, 
		uint32_t bufferSize, 
		uint8_t numDataBuffers,
		bool upload = false, 
		bool unorderedAccess = false);
	uint8_t m_numDataBuffers = 0;
};