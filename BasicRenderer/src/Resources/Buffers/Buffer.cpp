#include "Resources/Buffers/Buffer.h"

#include <rhi_helpers.h>

#include "Managers/Singletons/DeviceManager.h"

using namespace Microsoft::WRL;

Buffer::Buffer(
	rhi::HeapType accessType,
	uint64_t bufferSize, 
	bool unorderedAccess) : 
	GloballyIndexedResource(){
	m_accessType = accessType;

	rhi::ResourceDesc desc = rhi::helpers::ResourceDesc::Buffer(bufferSize);
	if (unorderedAccess) {
		desc.resourceFlags |= rhi::ResourceFlags::RF_AllowUnorderedAccess;
	}
	desc.heapType = accessType;
	auto device = DeviceManager::GetInstance().GetDevice();
	auto result = device.CreateCommittedResource(desc, m_buffer);

	m_size = bufferSize;
}

rhi::BarrierBatch Buffer::GetEnhancedBarrierGroup(RangeSpec range, rhi::ResourceAccessType prevAccessType, rhi::ResourceAccessType newAccessType, rhi::ResourceLayout prevLayout, rhi::ResourceLayout newLayout, rhi::ResourceSyncState prevSyncState, rhi::ResourceSyncState newSyncState) {

	rhi::BarrierBatch batch = {};
	m_barrier = rhi::BufferBarrier{
	   .buffer = GetAPIResource().GetHandle(),
	   .offset = 0,
	   .size = UINT64_MAX,
	   .beforeSync = prevSyncState,
	   .afterSync = newSyncState,
	   .beforeAccess = prevAccessType,
	   .afterAccess = newAccessType
	};
	batch.buffers = { &m_barrier };

	return batch;
}
