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
    m_desc = desc;
    handle = resourceManager.CreateTexture(desc, initialData);
    m_clearColor = { desc.clearColor[0], desc.clearColor[1], desc.clearColor[2], desc.clearColor[3] };
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

	m_mipLevels = desc.generateMipMaps ? CalculateMipLevels(m_width, m_height) : 1;
	m_arraySize = desc.arraySize;

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

	m_subresourceAccessTypes.resize(m_mipLevels * m_arraySize, ResourceAccessType::COMMON);
	m_subresourceLayouts.resize(m_mipLevels * m_arraySize, ResourceLayout::LAYOUT_COMMON);
	m_subresourceSyncStates.resize(m_mipLevels * m_arraySize, ResourceSyncState::ALL);
}


BarrierGroups& PixelBuffer::GetEnhancedBarrierGroup(RangeSpec range, ResourceAccessType prevAccessType, ResourceAccessType newAccessType, ResourceLayout prevLayout, ResourceLayout newLayout, ResourceSyncState prevSyncState, ResourceSyncState newSyncState) {
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

	auto resolvedRange = ResolveRangeSpec(range, m_mipLevels, m_arraySize);

	//for (unsigned int i = subresourceStart; i < subresourceEnd; i++) {
	//	m_subresourceAccessTypes[i] = newAccessType;
	//	m_subresourceLayouts[i] = newLayout;
	//	m_subresourceSyncStates[i] = newSyncState;
	//}

    if (resolvedRange.mipCount == m_mipLevels) {
        m_textureBarrier.Subresources = CD3DX12_BARRIER_SUBRESOURCE_RANGE(0xffffffff);
    }
    else {
        m_textureBarrier.Subresources = CD3DX12_BARRIER_SUBRESOURCE_RANGE(resolvedRange.firstMip, resolvedRange.mipCount, resolvedRange.firstSlice, resolvedRange.sliceCount);
    }
    return m_barrierGroups;
}