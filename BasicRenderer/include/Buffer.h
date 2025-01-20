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
		bool upload,
		bool unorderedAccess) {
		return std::shared_ptr<Buffer>(new Buffer(device, accessType, bufferSize, upload, unorderedAccess));
	}
	static std::unique_ptr<Buffer> CreateUnique(
		ID3D12Device* device, 
		ResourceCPUAccessType accessType, 
		uint32_t bufferSize,
		bool upload,
		bool unorderedAccess) {
		return std::unique_ptr<Buffer>(new Buffer(device, accessType, bufferSize, upload, unorderedAccess));
	}

	~Buffer() {
		m_buffer.Reset();
	}
	ResourceCPUAccessType m_accessType;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_buffer;
	std::vector<D3D12_RESOURCE_BARRIER>& GetTransitions(ResourceState prevState, ResourceState newState);
	BarrierGroups& GetEnhancedBarrierGroup(ResourceState prevState, ResourceState newState, ResourceSyncState prevSyncState, ResourceSyncState newSyncState);


	ID3D12Resource* GetAPIResource() const override { return m_buffer.Get(); }
protected:
	void OnSetName() override { m_buffer->SetName(name.c_str()); }
private:
	// Old barriers
	std::vector<D3D12_RESOURCE_BARRIER> m_transitions;
	std::vector<D3D12_RESOURCE_BARRIER> m_emptyTransitions = {};

	// Enhanced barriers
	D3D12_BUFFER_BARRIER m_bufferBarrier;
	D3D12_BARRIER_GROUP m_barrierGroup = {};
	BarrierGroups m_barrierGroups;

	Buffer(ID3D12Device* device, 
		ResourceCPUAccessType accessType, 
		uint32_t bufferSize, 
		bool upload = false, bool unorderedAccess = false);
};