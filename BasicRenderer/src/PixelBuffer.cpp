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
    SetSRVDescriptor(handle.srvUavHeap, handle.SRVInfo);
    if(desc.hasUAV)
        SetUAVGPUDescriptor(handle.srvUavHeap, handle.UAVInfo);
    if(desc.hasNonShaderVisibleUAV)
        SetUAVCPUDescriptor(handle.uavCPUHeap, handle.NSVUAVInfo);
	if (desc.hasRTV)
		SetRTVDescriptors(handle.rtvHeap, handle.RTVInfo);
	if (desc.hasDSV)
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

    m_barrierGroup.NumBarriers = 1;
    m_barrierGroup.Type = D3D12_BARRIER_TYPE_TEXTURE;
    m_barrierGroup.pTextureBarriers = &m_textureBarrier;

    m_textureBarrier.pResource = handle.texture.Get();
	m_textureBarrier.Flags = D3D12_TEXTURE_BARRIER_FLAG_NONE;
	m_textureBarrier.Subresources = CD3DX12_BARRIER_SUBRESOURCE_RANGE(0xffffffff); // TODO: would more fine-grained transitions be better?
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

D3D12_BARRIER_GROUP& PixelBuffer::GetEnhancedBarrierGroup(ResourceState prevState, ResourceState newState, ResourceSyncState prevSyncState, ResourceSyncState newSyncState) {
    m_textureBarrier.AccessBefore = ResourceStateToD3D12AccessType(prevState);
    m_textureBarrier.AccessAfter = ResourceStateToD3D12AccessType(newState);
    m_textureBarrier.SyncBefore = ResourceSyncStateToD3D12(prevSyncState);
    m_textureBarrier.SyncAfter = ResourceSyncStateToD3D12(newSyncState);
	m_textureBarrier.LayoutBefore = ResourceStateToD3D12GraphicsBarrierLayout(prevState);
	m_textureBarrier.LayoutAfter = ResourceStateToD3D12GraphicsBarrierLayout(newState);

    return m_barrierGroup;
}