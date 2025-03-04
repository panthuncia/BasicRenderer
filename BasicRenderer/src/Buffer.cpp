#include "Buffer.h"

#include <stdexcept>

#include "DirectX/d3dx12.h"
#include "spdlog/spdlog.h"
#include "RenderContext.h"
#include "ResourceStates.h"

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
	uint32_t bufferSize, 
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
	D3D12_RESOURCE_BARRIER barrier;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = m_buffer.Get();
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	m_transitions.push_back(barrier);

	m_barrierGroup.NumBarriers = 1;
	m_barrierGroup.Type = D3D12_BARRIER_TYPE_BUFFER;
	m_barrierGroup.pBufferBarriers = &m_bufferBarrier;

	m_bufferBarrier.pResource = m_buffer.Get();
	m_bufferBarrier.Offset = 0;
	m_bufferBarrier.Size = UINT64_MAX;

	m_barrierGroups.numBufferBarrierGroups = 1;
	m_barrierGroups.bufferBarriers = &m_barrierGroup;
}

std::vector<D3D12_RESOURCE_BARRIER>& Buffer::GetTransitions(ResourceState fromState, ResourceState toState) {
#if defined(_DEBUG)
	if (fromState != currentState) {
		throw(std::runtime_error("Buffer state mismatch"));
	}
	if (fromState == toState) {
		throw(std::runtime_error("Useless transition"));
	}
#endif

	D3D12_RESOURCE_STATES d3dFromState = ResourceStateToD3D12(fromState);
	D3D12_RESOURCE_STATES d3dToState = ResourceStateToD3D12(toState);
	m_transitions[0].Transition.StateBefore = d3dFromState;
	m_transitions[0].Transition.StateAfter = d3dToState;

	currentState = toState;

	return m_transitions;
}

BarrierGroups& Buffer::GetEnhancedBarrierGroup(ResourceState prevState, ResourceState newState, ResourceSyncState prevSyncState, ResourceSyncState newSyncState) {
#if defined(_DEBUG)
	if (prevState != currentState) {
		throw(std::runtime_error("Buffer state mismatch"));
	}
	if (prevSyncState != currentSyncState) {
		throw(std::runtime_error("Buffer sync state mismatch"));
	}
	if (prevState == newState) {
		throw(std::runtime_error("Useless transition"));
	}
#endif
	
	m_bufferBarrier.AccessBefore = ResourceStateToD3D12AccessType(prevState);
	m_bufferBarrier.AccessAfter = ResourceStateToD3D12AccessType(newState);
	m_bufferBarrier.SyncBefore = ResourceSyncStateToD3D12(prevSyncState);
	m_bufferBarrier.SyncAfter = ResourceSyncStateToD3D12(newSyncState);

	currentState = newState;
	currentSyncState = newSyncState;

	return m_barrierGroups;
}
