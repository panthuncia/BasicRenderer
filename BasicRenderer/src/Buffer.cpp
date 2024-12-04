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
	uint8_t numDataBuffers,
	bool upload, 
	bool unorderedAccess) : 
	GloballyIndexedResource(){
	m_accessType = accessType;
	D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(TranslateAccessType(accessType));
	D3D12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);
	unorderedAccess ? bufferDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
	D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
	m_buffers.resize(numDataBuffers);
	m_transitions.resize(numDataBuffers);
	for (int i = 0; i < numDataBuffers; i++) {
		auto hr = device->CreateCommittedResource(
			&heapProps,
			D3D12_HEAP_FLAG_NONE,
			&bufferDesc,
			upload ? D3D12_RESOURCE_STATE_GENERIC_READ : D3D12_RESOURCE_STATE_COMMON,
			nullptr,
			IID_PPV_ARGS(&m_buffers[i]));

		if (FAILED(hr)) {
			spdlog::error("HRESULT failed with error code: {}", hr);
			throw std::runtime_error("HRESULT failed");
		}
		D3D12_RESOURCE_BARRIER barrier;
		barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = m_buffers[i].Get();
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		m_transitions[i].push_back(barrier);
	}
	m_numDataBuffers = numDataBuffers;
}

std::vector<D3D12_RESOURCE_BARRIER>& Buffer::GetTransitions(uint8_t frameIndex, ResourceState fromState, ResourceState toState) {
	if (fromState == toState) {
		return m_emptyTransitions;
	}

	D3D12_RESOURCE_STATES d3dFromState = ResourceStateToD3D12(fromState);
	D3D12_RESOURCE_STATES d3dToState = ResourceStateToD3D12(toState);
	m_transitions[frameIndex][0].Transition.StateBefore = d3dFromState;
	m_transitions[frameIndex][0].Transition.StateAfter = d3dToState;

	currentState = toState;

	return m_transitions[frameIndex];
}

void Buffer::OnSetName() {
	for (auto& buffer : m_buffers) {
		buffer->SetName(name.c_str());
	}
}