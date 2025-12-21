#include "Resources/Buffers/Buffer.h"

#include <rhi_helpers.h>

#include "Managers/Singletons/DeviceManager.h"

using namespace Microsoft::WRL;

Buffer::Buffer(
	const rhi::HeapType accessType,
	const uint64_t bufferSize, 
	const bool unorderedAccess) : 
	GloballyIndexedResource(){
	m_accessType = accessType;

	rhi::ResourceDesc desc = rhi::helpers::ResourceDesc::Buffer(bufferSize);
	if (unorderedAccess) {
		desc.resourceFlags |= rhi::ResourceFlags::RF_AllowUnorderedAccess;
	}
	desc.heapType = accessType;
	auto device = DeviceManager::GetInstance().GetDevice();

	rhi::ResourceAllocationInfo allocInfo;
	device.GetResourceAllocationInfo(&desc, 1, &allocInfo);

	AllocationTrackDesc trackDesc(static_cast<int>(GetGlobalResourceID()));
	EntityComponentBundle allocationBundle;
	allocationBundle
		.Set<MemoryStatisticsComponents::MemSizeBytes>({ allocInfo.sizeInBytes })
		.Set<MemoryStatisticsComponents::ResourceType>({ rhi::ResourceType::Buffer });
	trackDesc.attach = allocationBundle;

	rhi::ma::AllocationDesc allocationDesc;
	allocationDesc.heapType = accessType;
	DeviceManager::GetInstance().CreateResourceTracked(
		allocationDesc,
		desc,
		0,
		nullptr,
		m_bufferAllocation,
		trackDesc);

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
