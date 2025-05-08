#include "Resources/PixelBuffer.h"
#include <string>
#include <stdexcept>

#include "Managers/Singletons/DeviceManager.h"
#include "Utilities/Utilities.h"
#define STB_IMAGE_IMPLEMENTATION
#include "ThirdParty/stb/stb_image.h"
#include "Managers/Singletons/ResourceManager.h"
#include "Render/RenderContext.h"

PixelBuffer::PixelBuffer(const TextureDescription& desc, const std::vector<const stbi_uc*> initialData, PixelBuffer* textureToAlias) {
    ResourceManager& resourceManager = ResourceManager::GetInstance();

    handle = resourceManager.CreateTexture(desc, initialData);
    SetSRVDescriptors(handle.srvUavHeap, handle.SRVInfo);
    if(desc.hasUAV)
        SetUAVGPUDescriptors(handle.srvUavHeap, handle.UAVInfo);
    if(desc.hasNonShaderVisibleUAV)
        SetUAVCPUDescriptors(handle.uavCPUHeap, handle.NSVUAVInfo);
	if (desc.hasRTV)
		SetRTVDescriptors(handle.rtvHeap, handle.RTVInfo);
	if (desc.hasDSV)
	    SetDSVDescriptors(handle.dsvHeap, handle.DSVInfo);
	m_width = desc.imageDimensions[0].width;
	m_height = desc.imageDimensions[0].height;
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

	m_barrierGroups.numTextureBarrierGroups = 1;
	m_barrierGroups.textureBarriers = &m_barrierGroup;

    m_hasLayout = true;
}


BarrierGroups& PixelBuffer::GetEnhancedBarrierGroup(ResourceAccessType prevAccessType, ResourceAccessType newAccessType, ResourceLayout prevLayout, ResourceLayout newLayout, ResourceSyncState prevSyncState, ResourceSyncState newSyncState) {
#if defined(_DEBUG)
    //if (prevAccessType) {
    //    throw(std::runtime_error("Texture state mismatch"));
    //}
    //if (prevSyncState != currentSyncState) {
    //    throw(std::runtime_error("Texture sync state mismatch"));
    //}
    //if (prevState == newState) {
    //    throw(std::runtime_error("Useless transition"));
    //}
#endif
    m_textureBarrier.AccessBefore = ResourceAccessTypeToD3D12(prevAccessType);
    m_textureBarrier.AccessAfter = ResourceAccessTypeToD3D12(newAccessType);
    m_textureBarrier.SyncBefore = ResourceSyncStateToD3D12(prevSyncState);
    m_textureBarrier.SyncAfter = ResourceSyncStateToD3D12(newSyncState);
	m_textureBarrier.LayoutBefore = (D3D12_BARRIER_LAYOUT)prevLayout;
    m_textureBarrier.LayoutAfter = (D3D12_BARRIER_LAYOUT)newLayout;

    m_currentAccessType = newAccessType;
    m_currentLayout = newLayout;
    m_prevSyncState = newSyncState;

    return m_barrierGroups;
}