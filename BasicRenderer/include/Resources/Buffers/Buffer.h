#pragma once

#include <directx/d3d12.h>
#include <wrl/client.h>
#include <stdint.h>
#include <string>
#include <memory>
#include "Render/DescriptorHeap.h"
#include "Resources/HeapIndexInfo.h"
#include "Resources/GloballyIndexedResource.h"
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
		size_t bufferSize, 
		bool upload,
		bool unorderedAccess) {
		return std::shared_ptr<Buffer>(new Buffer(device, accessType, bufferSize, upload, unorderedAccess));
	}
	static std::unique_ptr<Buffer> CreateUnique(
		ID3D12Device* device, 
		ResourceCPUAccessType accessType, 
		size_t bufferSize,
		bool upload,
		bool unorderedAccess) {
		return std::unique_ptr<Buffer>(new Buffer(device, accessType, bufferSize, upload, unorderedAccess));
	}

	~Buffer() {
		m_buffer.Reset();
	}
	ResourceCPUAccessType m_accessType;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_buffer;
	BarrierGroups& GetEnhancedBarrierGroup(RangeSpec range, ResourceAccessType prevAccessType, ResourceAccessType newAccessType, ResourceLayout prevLayout, ResourceLayout newLayout, ResourceSyncState prevSyncState, ResourceSyncState newSyncState);
	size_t GetSize() const { return m_size; }

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
	size_t m_size = 0;

	Buffer(ID3D12Device* device, 
		ResourceCPUAccessType accessType, 
		size_t bufferSize, 
		bool upload = false, bool unorderedAccess = false);
};