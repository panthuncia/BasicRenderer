#include "Resources/Buffers/Buffer.h"

#include <stdexcept>
#include <rhi_helpers.h>

#include "DirectX/d3dx12.h"
#include "spdlog/spdlog.h"
#include "Render/RenderContext.h"
#include "Resources/ResourceStates.h"

using namespace Microsoft::WRL;

Buffer::Buffer(
	rhi::Device& device, 
	rhi::Memory accessType, 
	uint64_t bufferSize, 
	bool unorderedAccess) : 
	GloballyIndexedResource(){
	m_accessType = accessType;

	rhi::ResourceDesc desc = rhi::helpers::ResourceDesc::Buffer(bufferSize);
	if (unorderedAccess) {
		desc.flags |= rhi::ResourceFlags::AllowUnorderedAccess;
	}
	desc.memory = accessType;
	m_buffer = device.CreateCommittedResource(desc);

	m_size = bufferSize;

	m_subresourceAccessTypes.push_back(rhi::ResourceAccessType::Common);
	m_subresourceLayouts.push_back(rhi::ResourceLayout::Common);
	m_subresourceSyncStates.push_back(rhi::ResourceSyncState::All);
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
