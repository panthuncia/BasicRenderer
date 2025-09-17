#pragma once

#include <stdint.h>
#include <string>
#include <memory>
#include <rhi.h>

#include <resource_states.h>

#include "Render/DescriptorHeap.h"
#include "Resources/HeapIndexInfo.h"
#include "Resources/GloballyIndexedResource.h"
#include "Utilities/Utilities.h"

class RenderContext;

class Buffer : public GloballyIndexedResource{
public:
	static std::shared_ptr<Buffer> CreateShared(
		rhi::Device& device,
		rhi::Memory accessType, 
		uint64_t bufferSize, 
		bool unorderedAccess = false) {
		return std::shared_ptr<Buffer>(new Buffer(device, accessType, bufferSize, unorderedAccess));
	}
	static std::unique_ptr<Buffer> CreateUnique(
		rhi::Device& device, 
		rhi::Memory accessType, 
		uint64_t bufferSize,
		bool unorderedAccess = false) {
		return std::unique_ptr<Buffer>(new Buffer(device, accessType, bufferSize, unorderedAccess));
	}

	~Buffer() {
		m_buffer.Reset();
	}
	rhi::Memory m_accessType;
	rhi::ResourcePtr m_buffer;
	rhi::BarrierBatch GetEnhancedBarrierGroup(RangeSpec range, rhi::ResourceAccessType prevAccessType, rhi::ResourceAccessType newAccessType, rhi::ResourceLayout prevLayout, rhi::ResourceLayout newLayout, rhi::ResourceSyncState prevSyncState, rhi::ResourceSyncState newSyncState);
	size_t GetSize() const { return m_size; }

	rhi::Resource GetAPIResource() override { return m_buffer.Get(); }
protected:
	void OnSetName() override { m_buffer->SetName(ws2s(name).c_str()); }
private:

	size_t m_size = 0;
	rhi::BufferBarrier m_barrier = {};

	Buffer(rhi::Device& device, 
		rhi::Memory accessType, 
		uint64_t bufferSize, 
		bool unorderedAccess = false);
};