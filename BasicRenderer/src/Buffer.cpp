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