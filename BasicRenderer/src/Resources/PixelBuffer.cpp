#include "Resources/PixelBuffer.h"
#include <string>
#include <stdexcept>

#include "Managers/Singletons/DeviceManager.h"
#include "Utilities/Utilities.h"
#define STB_IMAGE_IMPLEMENTATION
#include "ThirdParty/stb/stb_image.h"
#include "Managers/Singletons/ResourceManager.h"
#include "Managers/Singletons/UploadManager.h"
#include "Resources/MemoryStatisticsComponents.h"

void UploadTextureData(rhi::Resource& dstTexture, const TextureDescription& desc, const std::vector<const stbi_uc*>& initialData, unsigned int mipLevels) {

	if (initialData.empty()) return;

	// effective array slices = arraySize * (isCubemap ? 6 : 1)
	const uint32_t faces = desc.isCubemap ? 6u : 1u;
	const uint32_t arraySlices = faces * static_cast<uint32_t>(desc.arraySize);
	const uint32_t numSubres = arraySlices * static_cast<uint32_t>(mipLevels);

	// Build a dense SubresourceData table (nullptr entries are allowed; they'll be skipped)
	std::vector<rhi::helpers::SubresourceData> srd(numSubres);
	std::vector<std::vector<stbi_uc>> expandedImages;   // keep storage alive during copy
	expandedImages.reserve(numSubres);

	// If caller passed fewer than numSubres pointers, pad with nullptrs.
	std::vector<const stbi_uc*> fullInitial(numSubres, nullptr);
	std::copy(initialData.begin(), initialData.end(), fullInitial.begin());

	int i = -1;
	for (uint32_t a = 0; a < arraySlices; ++a) {
		for (uint32_t m = 0; m < mipLevels; ++m) {
			++i;
			const uint32_t subIdx = m + a * mipLevels;

			const stbi_uc* imageData = fullInitial[subIdx];

			uint32_t width = std::max(1u, static_cast<uint32_t>(desc.imageDimensions[i].width >> m));
			uint32_t height = std::max(1u, static_cast<uint32_t>(desc.imageDimensions[i].height >> m));
			uint32_t channels = desc.channels;

			auto& out = srd[subIdx];

			// If provided pitches don't match raw (width*channels), treat as "pre-padded or compressed"
			if ((width * channels != desc.imageDimensions[i].rowPitch) ||
				(width * channels * height != desc.imageDimensions[i].slicePitch))
			{
				out.pData = imageData;
				out.rowPitch = static_cast<uint32_t>(desc.imageDimensions[i].rowPitch);
				out.slicePitch = static_cast<uint32_t>(desc.imageDimensions[i].slicePitch);
			}
			else {
				if (imageData) {
					const stbi_uc* ptr = imageData;
					if (channels == 3) {
						// Expand to RGBA8
						expandedImages.emplace_back(ExpandImageData(imageData, width, height));
						ptr = expandedImages.back().data();
						channels = 4;
					}
					out.pData = ptr;
					out.rowPitch = width * channels; // tightly packed
					out.slicePitch = out.rowPitch * height;
				}
				else {
					out.pData = nullptr;
					out.rowPitch = out.slicePitch = 0;
				}
			}
		}
	}

	const uint32_t baseW = desc.imageDimensions[0].width;
	const uint32_t baseH = desc.imageDimensions[0].height;

	auto device = DeviceManager::GetInstance().GetDevice();

	TEXTURE_UPLOAD_SUBRESOURCES(
		dstTexture,
		desc.format,
		baseW,
		baseH,
		/*depthOrLayers*/ 1,
		static_cast<uint32_t>(mipLevels),
		arraySlices,
		srd.data(),
		static_cast<uint32_t>(srd.size()));
}

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

		// Determine the number of mip levels
	uint16_t mipLevels = desc.generateMipMaps ? CalculateMipLevels(desc.imageDimensions[0].width, desc.imageDimensions[0].height) : 1;

	// Determine the array size
	uint32_t arraySize = desc.arraySize;
	if (!desc.isArray && !desc.isCubemap) {
		arraySize = 1;
	}

	// Create the texture resource description
	auto width = desc.imageDimensions[0].width;
	auto height = desc.imageDimensions[0].height;
	if (desc.padInternalResolution) { // Pad the width and height to the next power of two
		width = std::max(1u, static_cast<unsigned int>(std::pow(2, std::ceil(std::log2(width)))));
		height = std::max(1u, static_cast<unsigned int>(std::pow(2, std::ceil(std::log2(height)))));
	}

	if (width > std::numeric_limits<uint32_t>().max() || height > std::numeric_limits<uint32_t>().max()) {
		spdlog::error("Texture dimensions above uint32_max not implemented");
	}

	// Handle clear values for RTV and DSV
	rhi::ClearValue* clearValue = nullptr;
	rhi::ClearValue depthClearValue = {};
	rhi::ClearValue colorClearValue = {};
	if (desc.hasDSV) {
		depthClearValue.type = rhi::ClearValueType::DepthStencil;
		depthClearValue.format = desc.dsvFormat == rhi::Format::Unknown ? desc.format : desc.dsvFormat;
		depthClearValue.depthStencil.depth = desc.depthClearValue;
		depthClearValue.depthStencil.stencil = 0;
		clearValue = &depthClearValue;
	}
	else if (desc.hasRTV) {
		colorClearValue.type = rhi::ClearValueType::Color;
		colorClearValue.format = desc.rtvFormat == rhi::Format::Unknown ? desc.format : desc.rtvFormat;
		colorClearValue.rgba[0] = desc.clearColor[0];
		colorClearValue.rgba[1] = desc.clearColor[1];
		colorClearValue.rgba[2] = desc.clearColor[2];
		colorClearValue.rgba[3] = desc.clearColor[3];
		clearValue = &colorClearValue;
	}

	rhi::ResourceDesc textureDesc{
		.type = rhi::ResourceType::Texture2D,
		.texture = {
			.format = desc.format,
			.width = static_cast<uint32_t>(width),
			.height = static_cast<uint32_t>(height),
			.depthOrLayers = static_cast<uint16_t>(desc.isCubemap ? 6 * arraySize : arraySize),
			.mipLevels = mipLevels,
			.sampleCount = 1,
			.initialLayout = rhi::ResourceLayout::Common,
			.optimizedClear = clearValue
		}
	};
	if (desc.hasRTV) {
		textureDesc.resourceFlags |= rhi::ResourceFlags::RF_AllowRenderTarget;
	}
	if (desc.hasDSV) {
		textureDesc.resourceFlags |= rhi::ResourceFlags::RF_AllowDepthStencil;
	}
	if (desc.hasUAV) {
		textureDesc.resourceFlags |= rhi::ResourceFlags::RF_AllowUnorderedAccess;
	}
	// Create the texture resource

	//rhi::ResourcePtr textureResource;
	if (desc.allowAlias) {
		//textureResource = device.CreatePlacedResource(placedResourceHeap, 0, textureDesc); // TODO: handle offset
		throw std::runtime_error("Aliasing resources not implemented yet");
	}
	else {

		rhi::ma::AllocationDesc allocationDesc;
		allocationDesc.heapType = rhi::HeapType::DeviceLocal;

		DeviceManager::GetInstance().CreateResourceTracked(
			allocationDesc,
			textureDesc,
			0,
			nullptr,
			m_textureHandle);

		//auto result = device.CreateCommittedResource(textureDesc, textureResource);
	}

	auto device = DeviceManager::GetInstance().GetDevice();

	rhi::ResourceAllocationInfo allocInfo;
	device.GetResourceAllocationInfo(&textureDesc, 1, &allocInfo);
	EntityComponentBundle allocationBundle;
	allocationBundle
		.Set<MemoryStatisticsComponents::MemSizeBytes>({ allocInfo.sizeInBytes })
		.Set<MemoryStatisticsComponents::ResourceType>({ rhi::ResourceType::Buffer })
		.Set<MemoryStatisticsComponents::ResourceName>({ ws2s(GetName()) });
	m_textureHandle.ApplyComponentBundle(allocationBundle);

    m_placedResourceHeap = aliasTarget ? aliasTarget->GetPlacedResourceHeap() : rhi::HeapHandle();

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
	if (!initialData.empty()) {
		UploadTextureData(m_textureHandle.GetResource(), desc, initialData, m_mipLevels);
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

	// Create and assign descriptors through ResourceManager.
	{
		ResourceManager::ViewRequirements views;
		ResourceManager::ViewRequirements::TextureViews texViews;
		texViews.mipLevels = m_mipLevels;
		texViews.isCubemap = desc.isCubemap;
		texViews.isArray = desc.isArray;
		texViews.arraySize = desc.arraySize;
		texViews.totalArraySlices = m_arraySize;

		texViews.baseFormat = desc.format;
		texViews.srvFormat = desc.srvFormat;
		texViews.uavFormat = desc.uavFormat;
		texViews.rtvFormat = desc.rtvFormat;
		texViews.dsvFormat = desc.dsvFormat;

		texViews.createSRV = true;
		texViews.createUAV = desc.hasUAV;
		texViews.createNonShaderVisibleUAV = desc.hasNonShaderVisibleUAV;
		texViews.createRTV = desc.hasRTV;
		texViews.createDSV = desc.hasDSV;

		// if cubemap, also create Texture2DArray SRV.
		texViews.createCubemapAsArraySRV = desc.isCubemap;

		texViews.uavFirstMip = 0;

		views.views = texViews;
		rm.AssignDescriptorSlots(*this, m_textureHandle.GetResource(), views);
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
	m_barrier.texture = m_textureHandle.GetResource().GetHandle();

	batch.textures = { &m_barrier };

    return batch;
}