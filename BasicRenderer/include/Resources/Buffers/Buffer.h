#pragma once

#include <wrl/client.h>
#include <stdint.h>
#include <string>
#include <memory>

#include <resource_states.h>

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
		rhi::Device& device,
		ResourceCPUAccessType accessType, 
		uint64_t bufferSize, 
		bool upload,
		bool unorderedAccess) {
		return std::shared_ptr<Buffer>(new Buffer(device, accessType, bufferSize, upload, unorderedAccess));
	}
	static std::unique_ptr<Buffer> CreateUnique(
		rhi::Device& device, 
		ResourceCPUAccessType accessType, 
		uint64_t bufferSize,
		bool upload,
		bool unorderedAccess) {
		return std::unique_ptr<Buffer>(new Buffer(device, accessType, bufferSize, upload, unorderedAccess));
	}

	~Buffer() {
		m_buffer.Reset();
	}
	ResourceCPUAccessType m_accessType;
	rhi::ResourcePtr m_buffer;
	rhi::BarrierBatch GetEnhancedBarrierGroup(RangeSpec range, rhi::ResourceAccessType prevAccessType, rhi::ResourceAccessType newAccessType, rhi::ResourceLayout prevLayout, rhi::ResourceLayout newLayout, rhi::ResourceSyncState prevSyncState, rhi::ResourceSyncState newSyncState);
	size_t GetSize() const { return m_size; }

	rhi::ResourceHandle GetAPIResource() const override { return m_buffer.Get(); }
protected:
	void OnSetName() override { m_buffer->SetName(name.c_str()); }
private:
	// Old barriers
	//std::vector<D3D12_RESOURCE_BARRIER> m_transitions;
	//std::vector<D3D12_RESOURCE_BARRIER> m_emptyTransitions = {};

	// Enhanced barriers
	//D3D12_BUFFER_BARRIER m_bufferBarrier;
	//D3D12_BARRIER_GROUP m_barrierGroup = {};
	//BarrierGroups m_barrierGroups;
	size_t m_size = 0;

	Buffer(ID3D12Device* device, 
		ResourceCPUAccessType accessType, 
		uint64_t bufferSize, 
		bool upload = false, bool unorderedAccess = false);
};