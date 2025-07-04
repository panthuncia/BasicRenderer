#include "Resources/Buffers/Buffer.h"

#include <stdexcept>

#include "DirectX/d3dx12.h"
#include "spdlog/spdlog.h"
#include "Render/RenderContext.h"
#include "Resources/ResourceStates.h"

using namespace Microsoft::WRL;

D3D12_HEAP_TYPE TranslateAccessType(ResourceCPUAccessType accessType) {
	switch (accessType) {
	case ResourceCPUAccessType::READ:
		return D3D12_HEAP_TYPE_READBACK;
	case ResourceCPUAccessType::WRITE:
		return D3D12_HEAP_TYPE_UPLOAD;
	case ResourceCPUAccessType::READ_WRITE:
		return D3D12_HEAP_TYPE_UPLOAD;
	case ResourceCPUAccessType::NONE:
		return D3D12_HEAP_TYPE_DEFAULT;
	}
}

Buffer::Buffer(
	ID3D12Device* device, 
	ResourceCPUAccessType accessType, 
	size_t bufferSize, 
	bool upload, bool unorderedAccess) : 
	GloballyIndexedResource(){
	m_accessType = accessType;
	D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(TranslateAccessType(accessType));
	D3D12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);
	unorderedAccess ? bufferDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
	D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
	auto hr = device->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&bufferDesc,
		upload ? D3D12_RESOURCE_STATE_GENERIC_READ : D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(&m_buffer));

	if (FAILED(hr)) {
		spdlog::error("HRESULT failed with error code: {}", hr);
		throw std::runtime_error("HRESULT failed");
	}
	//D3D12_RESOURCE_BARRIER barrier;
	//barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	//barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	//barrier.Transition.pResource = m_buffer.Get();
	//barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	//m_transitions.push_back(barrier);

	//m_barrierGroup.NumBarriers = 1;
	//m_barrierGroup.Type = D3D12_BARRIER_TYPE_BUFFER;
	//m_barrierGroup.pBufferBarriers = &m_bufferBarrier;

	//m_bufferBarrier.pResource = m_buffer.Get();
	//m_bufferBarrier.Offset = 0;
	//m_bufferBarrier.Size = UINT64_MAX;

	//m_barrierGroups.numBufferBarrierGroups = 1;
	//m_barrierGroups.bufferBarriers = &m_barrierGroup;

	m_size = bufferSize;

	m_subresourceAccessTypes.push_back(ResourceAccessType::COMMON);
	m_subresourceLayouts.push_back(ResourceLayout::LAYOUT_COMMON);
	m_subresourceSyncStates.push_back(ResourceSyncState::ALL);
}

BarrierGroups Buffer::GetEnhancedBarrierGroup(RangeSpec range, ResourceAccessType prevAccessType, ResourceAccessType newAccessType, ResourceLayout prevLayout, ResourceLayout newLayout, ResourceSyncState prevSyncState, ResourceSyncState newSyncState) {
#if defined(_DEBUG)
	//if (prevState != currentState) {
	//	throw(std::runtime_error("Buffer state mismatch"));
	//}
	//if (prevSyncState != currentSyncState) {
	//	throw(std::runtime_error("Buffer sync state mismatch"));
	//}
	//if (prevState == newState) {
	//	throw(std::runtime_error("Useless transition"));
	//}
#endif

	BarrierGroups barrierGroups = {};
	
	barrierGroups.bufferBarrierDescs.push_back({});

	auto& bufferBarrierDesc = barrierGroups.bufferBarrierDescs[0];
	bufferBarrierDesc.AccessBefore = ResourceAccessTypeToD3D12(prevAccessType);
	bufferBarrierDesc.AccessAfter = ResourceAccessTypeToD3D12(newAccessType);
	bufferBarrierDesc.SyncBefore = ResourceSyncStateToD3D12(prevSyncState);
	bufferBarrierDesc.SyncAfter = ResourceSyncStateToD3D12(newSyncState);
	bufferBarrierDesc.Offset = 0;
	bufferBarrierDesc.Size = UINT64_MAX;
	bufferBarrierDesc.pResource = m_buffer.Get();

	D3D12_BARRIER_GROUP group;
	group.NumBarriers = 1;
	group.Type = D3D12_BARRIER_TYPE_BUFFER;
	group.pBufferBarriers = barrierGroups.bufferBarrierDescs.data();
	barrierGroups.bufferBarriers.push_back(group);

	m_subresourceAccessTypes[0] = newAccessType;
	m_subresourceLayouts[0] = newLayout;
	m_subresourceSyncStates[0] = newSyncState;

	return barrierGroups;
}
