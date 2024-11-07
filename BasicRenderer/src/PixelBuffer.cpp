#include "PixelBuffer.h"
#include <string>
#include <stdexcept>

#include "DeviceManager.h"
#include "Utilities.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"
#include "ResourceManager.h"
#include "RenderContext.h"

PixelBuffer::PixelBuffer(const TextureDescription& desc, const std::vector<const stbi_uc*> initialData) {
    ResourceManager& resourceManager = ResourceManager::GetInstance();

    handle = resourceManager.CreateTexture(desc, initialData);
    SetSRVDescriptor(handle.srvHeap, handle.SRVInfo);
	SetRTVDescriptors(handle.rtvHeap, handle.RTVInfo);
	SetDSVDescriptors(handle.dsvHeap, handle.DSVInfo);
    currentState = ResourceState::UNKNOWN;
	m_width = desc.width;
	m_height = desc.height;
	m_channels = desc.channels;
	m_format = desc.format;

	D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.pResource = handle.texture.Get();
    
	m_transitions.push_back(barrier);
}

std::vector<D3D12_RESOURCE_BARRIER>& PixelBuffer::GetTransitions (ResourceState fromState, ResourceState toState) {
    if (fromState == toState) return m_emptyTransitions;

    D3D12_RESOURCE_STATES d3dFromState = ResourceStateToD3D12(fromState);
    D3D12_RESOURCE_STATES d3dToState = ResourceStateToD3D12(toState);

    m_transitions[0].Transition.StateBefore = d3dFromState;
    m_transitions[0].Transition.StateAfter = d3dToState;

    currentState = toState;
	return m_transitions;
}