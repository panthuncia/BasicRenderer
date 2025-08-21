#include "Resources/PixelBuffer.h"
#include <string>
#include <stdexcept>

#include "Managers/Singletons/DeviceManager.h"
#include "Utilities/Utilities.h"
#define STB_IMAGE_IMPLEMENTATION
#include "ThirdParty/stb/stb_image.h"
#include "Managers/Singletons/ResourceManager.h"
#include "Render/RenderContext.h"

std::shared_ptr<PixelBuffer>
PixelBuffer::Create(const TextureDescription& desc,
    const std::vector<const stbi_uc*>& initialData,
    PixelBuffer* aliasTarget)
{
    auto pb = std::shared_ptr<PixelBuffer>(new PixelBuffer());
    pb->initialize(desc, initialData, aliasTarget);
    return pb;
}


void PixelBuffer::initialize(const TextureDescription& desc,
    const std::vector<const stbi_uc*>& initialData,
    PixelBuffer* aliasTarget)
{
    ResourceManager& rm = ResourceManager::GetInstance();

    // 1) create the raw resource
    auto [texture, placedHeap] =
        rm.CreateTextureResource(desc, aliasTarget != nullptr ? aliasTarget->GetPlacedResourceHeap() : nullptr);
    m_texture    = texture;
    m_placedResourceHeap = placedHeap;

	m_width  = desc.imageDimensions[0].width;
	m_height = desc.imageDimensions[0].height;
	m_mipLevels   = desc.generateMipMaps ? CalculateMipLevels(static_cast<uint16_t>(m_width), static_cast<uint16_t>(m_height)) : 1;
	m_arraySize   = desc.isCubemap ? 6 * desc.arraySize : (desc.isArray ? desc.arraySize : 1);

	if (desc.padInternalResolution) {
		m_internalWidth = std::max(1u, static_cast<unsigned int>(std::pow(2, std::ceil(std::log2(desc.imageDimensions[0].width)))));
		m_internalHeight = std::max(1u, static_cast<unsigned int>(std::pow(2, std::ceil(std::log2(desc.imageDimensions[0].height)))));
	}
	else {
		m_internalHeight = desc.imageDimensions[0].height;
		m_internalWidth = desc.imageDimensions[0].width;
	}

    // Upload initial data if any
    if (!initialData.empty())
        rm.UploadTextureData(m_texture.Get(), desc, initialData, m_arraySize, m_mipLevels);

    size_t subCount = m_mipLevels * m_arraySize;

    m_clearColor = { desc.clearColor[0],
        desc.clearColor[1],
        desc.clearColor[2],
        desc.clearColor[3] };

	// Create SRV
	auto& device = DeviceManager::GetInstance().GetDevice();
	const auto& cbvSrvUavHeap = rm.GetCBVSRVUAVHeap();
	const auto& nonShaderVisibleHeap = rm.GetNonShaderVisibleHeap();
	const auto& rtvHeap = rm.GetRTVHeap();
	const auto& dsvHeap = rm.GetDSVHeap();
	auto srvInfo = CreateShaderResourceViewsPerMip(
		device.Get(),
		m_texture.Get(),
		desc.srvFormat == DXGI_FORMAT_UNKNOWN ? desc.format : desc.srvFormat,
		cbvSrvUavHeap.get(),
		m_mipLevels,
		desc.isCubemap,
		desc.isArray,
		desc.arraySize
	);

	SRVViewType srvViewType = SRVViewType::Invalid;
	if (desc.isArray) {
		srvViewType = desc.isCubemap ? SRVViewType::TextureCubeArray : SRVViewType::Texture2DArray;
	}
	else if (desc.isCubemap) {
		srvViewType = SRVViewType::TextureCube;
	}
	else {
		srvViewType = SRVViewType::Texture2D;
	}

	SetDefaultSRVViewType(srvViewType);
	SetSRVView(srvViewType, cbvSrvUavHeap, srvInfo);

	if (desc.isCubemap) { // Set up SRV to view this as an array
		std::vector<std::vector<ShaderVisibleIndexInfo>> secondarySrvInfos;
		secondarySrvInfos = CreateShaderResourceViewsPerMip(
			device.Get(),
			m_texture.Get(),
			desc.srvFormat == DXGI_FORMAT_UNKNOWN ? desc.format : desc.srvFormat,
			cbvSrvUavHeap.get(),
			m_mipLevels,
			false, // isCubemap
			desc.isArray, // TODO: Do cubemap arrays work with this?
			6 // arraySize for cubemap array
		);
		SetSRVView(SRVViewType::Texture2DArray, cbvSrvUavHeap, secondarySrvInfos);
	}

	// UAV
	std::vector<std::vector<ShaderVisibleIndexInfo>> uavInfo;
	if (desc.hasUAV) {
		uavInfo = CreateUnorderedAccessViewsPerMip(
			device.Get(),
			m_texture.Get(),
			desc.uavFormat == DXGI_FORMAT_UNKNOWN ? desc.format : desc.uavFormat,
			cbvSrvUavHeap.get(),
			m_mipLevels,
			desc.isArray,
			m_arraySize,
			0
		);
		SetUAVGPUDescriptors(cbvSrvUavHeap, uavInfo);
	}

	// Non-shader visible UAV
	std::vector<std::vector<NonShaderVisibleIndexInfo>> nonShaderUavInfo;
	if (desc.hasNonShaderVisibleUAV) {
		nonShaderUavInfo = CreateNonShaderVisibleUnorderedAccessViewsPerMip(
			device.Get(),
			m_texture.Get(),
			desc.uavFormat == DXGI_FORMAT_UNKNOWN ? desc.format : desc.uavFormat,
			nonShaderVisibleHeap.get(),
			m_mipLevels,
			desc.isArray,
			desc.arraySize,
			0
		);
		SetUAVCPUDescriptors(nonShaderVisibleHeap, nonShaderUavInfo);
	}

	// Create RTVs if needed
	std::vector<std::vector<NonShaderVisibleIndexInfo>> rtvInfos;
	if (desc.hasRTV) {
		rtvInfos = CreateRenderTargetViews(
			device.Get(),
			m_texture.Get(),
			desc.rtvFormat == DXGI_FORMAT_UNKNOWN ? desc.format : desc.rtvFormat,
			rtvHeap.get(),
			desc.isCubemap,
			desc.isArray,
			desc.arraySize,
			m_mipLevels
		);
		SetRTVDescriptors(rtvHeap, rtvInfos);
	}

	// Create DSVs if needed
	std::vector<std::vector<NonShaderVisibleIndexInfo>> dsvInfos;
	if (desc.hasDSV) {
		dsvInfos = CreateDepthStencilViews(
			device.Get(),
			m_texture.Get(),
			dsvHeap.get(),
			desc.dsvFormat == DXGI_FORMAT_UNKNOWN ? desc.format : desc.dsvFormat,
			desc.isCubemap,
			desc.isArray,
			desc.arraySize,
			m_mipLevels
		);
		SetDSVDescriptors(dsvHeap, dsvInfos);
	}
}


BarrierGroups PixelBuffer::GetEnhancedBarrierGroup(RangeSpec range, ResourceAccessType prevAccessType, ResourceAccessType newAccessType, ResourceLayout prevLayout, ResourceLayout newLayout, ResourceSyncState prevSyncState, ResourceSyncState newSyncState) {
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
    BarrierGroups barrierGroups = {};

    barrierGroups.textureBarrierDescs.push_back({});
    auto& textureBarrierDesc = barrierGroups.textureBarrierDescs[0];
    textureBarrierDesc.AccessBefore = ResourceAccessTypeToD3D12(prevAccessType);
    textureBarrierDesc.AccessAfter = ResourceAccessTypeToD3D12(newAccessType);
    textureBarrierDesc.SyncBefore = ResourceSyncStateToD3D12(prevSyncState);
    textureBarrierDesc.SyncAfter = ResourceSyncStateToD3D12(newSyncState);
    textureBarrierDesc.LayoutBefore = ResourceLayoutToD3D12(prevLayout);
    textureBarrierDesc.LayoutAfter = ResourceLayoutToD3D12(newLayout);
	textureBarrierDesc.Flags = D3D12_TEXTURE_BARRIER_FLAG_NONE;
    textureBarrierDesc.pResource = m_texture.Get();

	auto resolvedRange = ResolveRangeSpec(range, m_mipLevels, m_arraySize);

    textureBarrierDesc.Subresources = CD3DX12_BARRIER_SUBRESOURCE_RANGE(resolvedRange.firstMip, resolvedRange.mipCount, resolvedRange.firstSlice, resolvedRange.sliceCount);

    D3D12_BARRIER_GROUP group;
	group.NumBarriers = 1;
	group.Type = D3D12_BARRIER_TYPE_TEXTURE;
	group.pTextureBarriers = barrierGroups.textureBarrierDescs.data();
	barrierGroups.textureBarriers.push_back(group);

    return barrierGroups;
}