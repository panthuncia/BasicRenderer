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
}

void PixelBuffer::Transition(ID3D12GraphicsCommandList* commandList, ResourceState fromState, ResourceState toState) {
    if (fromState == toState) return;

    D3D12_RESOURCE_STATES d3dFromState = ResourceStateToD3D12(fromState);
    D3D12_RESOURCE_STATES d3dToState = ResourceStateToD3D12(toState);

    // Create a resource barrier
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = handle.texture.Get();
    barrier.Transition.StateBefore = d3dFromState;
    barrier.Transition.StateAfter = d3dToState;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    commandList->ResourceBarrier(1, &barrier);

    currentState = toState;
}