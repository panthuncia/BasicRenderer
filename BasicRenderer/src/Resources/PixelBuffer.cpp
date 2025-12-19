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
PixelBuffer::CreateShared(const TextureDescription& desc,
	const std::vector<const stbi_uc*>& initialData,
	PixelBuffer* aliasTarget)
{
	auto pb = std::make_shared<PixelBuffer>(CreateTag{});
	pb->initialize(desc, initialData, aliasTarget);
#if BUILD_TYPE == BUILD_DEBUG
	pb->m_creation = std::stacktrace::current();
#endif
	return pb;
}

void PixelBuffer::initialize(const TextureDescription& desc,
    const std::vector<const stbi_uc*>& initialData,
    PixelBuffer* aliasTarget)
{
    ResourceManager& rm = ResourceManager::GetInstance();

    // create the raw resource
    auto [texture, placedHeap] =
		rm.CreateTextureResource(desc, aliasTarget != nullptr ? aliasTarget->GetPlacedResourceHeap() : rhi::HeapHandle());
    m_textureAllocation    = std::move(texture);
    m_placedResourceHeap = placedHeap;

	m_width  = desc.imageDimensions[0].width;
	m_height = desc.imageDimensions[0].height;
	m_mipLevels = desc.generateMipMaps ? CalculateMipLevels(static_cast<uint16_t>(m_width), static_cast<uint16_t>(m_height)) : 1;
	m_arraySize = desc.isCubemap ? 6 * desc.arraySize : (desc.isArray ? desc.arraySize : 1);
	m_format = desc.format;
	if (desc.padInternalResolution) {
		m_internalWidth = std::max(1u, static_cast<unsigned int>(std::pow(2, std::ceil(std::log2(desc.imageDimensions[0].width)))));
		m_internalHeight = std::max(1u, static_cast<unsigned int>(std::pow(2, std::ceil(std::log2(desc.imageDimensions[0].height)))));
	}
	else {
		m_internalHeight = desc.imageDimensions[0].height;
		m_internalWidth = desc.imageDimensions[0].width;
	}

    // Upload initial data if any
	auto resource = m_textureAllocation->GetResource();
	if (!initialData.empty()) {
		rm.UploadTextureData(resource, desc, initialData, m_mipLevels);
	}

    size_t subCount = m_mipLevels * m_arraySize;

	m_clearColor.type = rhi::ClearValueType::Color;
	m_clearColor.format = desc.format;
	m_clearColor.depthStencil.depth = desc.depthClearValue;
	if (desc.hasDSV) {
		m_clearColor.type = rhi::ClearValueType::DepthStencil; // TODO: Will we ever need both on one texture?
	}
	else {
		for (int i = 0; i < 4; i++) {
			m_clearColor.rgba[i] = desc.clearColor[i];
		}
	}

	// Create SRV
	auto device = DeviceManager::GetInstance().GetDevice();
	const auto& cbvSrvUavHeap = rm.GetCBVSRVUAVHeap();
	const auto& nonShaderVisibleHeap = rm.GetNonShaderVisibleHeap();
	const auto& rtvHeap = rm.GetRTVHeap();
	const auto& dsvHeap = rm.GetDSVHeap();
	auto srvInfo = CreateShaderResourceViewsPerMip(
		device,
		resource,
		desc.srvFormat == rhi::Format::Unknown ? desc.format : desc.srvFormat,
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
			device,
			resource,
			desc.srvFormat == rhi::Format::Unknown ? desc.format : desc.srvFormat,
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
			device,
			resource,
			desc.uavFormat == rhi::Format::Unknown ? desc.format : desc.uavFormat,
			cbvSrvUavHeap.get(),
			m_mipLevels,
			desc.isArray,
			m_arraySize,
			0,
			desc.isCubemap
		);
		SetUAVGPUDescriptors(cbvSrvUavHeap, uavInfo);
	}

	// Non-shader visible UAV
	std::vector<std::vector<NonShaderVisibleIndexInfo>> nonShaderUavInfo;
	if (desc.hasNonShaderVisibleUAV) {
		nonShaderUavInfo = CreateNonShaderVisibleUnorderedAccessViewsPerMip(
			device,
			resource,
			desc.uavFormat == rhi::Format::Unknown ? desc.format : desc.uavFormat,
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
			device,
			resource,
			desc.rtvFormat == rhi::Format::Unknown ? desc.format : desc.rtvFormat,
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
			device,
			resource,
			dsvHeap.get(),
			desc.dsvFormat == rhi::Format::Unknown ? desc.format : desc.dsvFormat,
			desc.isCubemap,
			desc.isArray,
			desc.arraySize,
			m_mipLevels
		);
		SetDSVDescriptors(dsvHeap, dsvInfos);
	}
}


rhi::BarrierBatch PixelBuffer::GetEnhancedBarrierGroup(RangeSpec range, rhi::ResourceAccessType prevAccessType, rhi::ResourceAccessType newAccessType, rhi::ResourceLayout prevLayout, rhi::ResourceLayout newLayout, rhi::ResourceSyncState prevSyncState, rhi::ResourceSyncState newSyncState) {

	rhi::BarrierBatch batch = {};

	auto resolvedRange = ResolveRangeSpec(range, m_mipLevels, m_arraySize);

	m_barrier.afterAccess = newAccessType;
	m_barrier.beforeAccess = prevAccessType;
	m_barrier.afterLayout = newLayout;
	m_barrier.beforeLayout = prevLayout;
	m_barrier.afterSync = newSyncState;
	m_barrier.beforeSync = prevSyncState;
	m_barrier.discard = false;
	m_barrier.range = { resolvedRange.firstMip, resolvedRange.mipCount, resolvedRange.firstSlice, resolvedRange.sliceCount };
	m_barrier.texture = m_textureAllocation->GetResource().GetHandle();

	batch.textures = { &m_barrier };

    return batch;
}