#include "Resources/GPUBacking/GPUTextureBacking.h"
#include <string>
#include <stdexcept>

#include "Managers/Singletons/DeviceManager.h"
#include "Utilities/Utilities.h"
#define STB_IMAGE_IMPLEMENTATION
#include "ThirdParty/stb/stb_image.h"
#include "Managers/Singletons/ResourceManager.h"
#include "Managers/Singletons/UploadManager.h"
#include "Resources/MemoryStatisticsComponents.h"

GpuTextureBacking::GpuTextureBacking(CreateTag)
{

}

GpuTextureBacking::~GpuTextureBacking()
{
	UnregisterLiveAlloc();
	DeletionManager::GetInstance().MarkForDelete(std::move(m_textureHandle));
}

std::unique_ptr<GpuTextureBacking>
GpuTextureBacking::CreateUnique(const TextureDescription& desc,
	uint64_t owningResourceID,
	const char* name)
{
	auto pb = std::make_unique<GpuTextureBacking>(CreateTag{});
	pb->initialize(desc, owningResourceID, name);
#if BUILD_TYPE == BUILD_DEBUG
	pb->m_creation = std::stacktrace::current();
#endif
	return std::move(pb);
}

void GpuTextureBacking::initialize(const TextureDescription& desc,
	uint64_t owningResourceID,
	const char* name)
{
	m_desc = desc;
	ResourceManager& rm = ResourceManager::GetInstance();

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

	AllocationTrackDesc trackDesc(static_cast<int>(owningResourceID));
	EntityComponentBundle allocationBundle;
	if (name != nullptr) {
		allocationBundle.Set<MemoryStatisticsComponents::ResourceName>({ name });
	}

	auto device = DeviceManager::GetInstance().GetDevice();

	rhi::ResourceAllocationInfo allocInfo;
	device.GetResourceAllocationInfo(&textureDesc, 1, &allocInfo);

	allocationBundle
		.Set<MemoryStatisticsComponents::MemSizeBytes>({ allocInfo.sizeInBytes })
		.Set<MemoryStatisticsComponents::ResourceType>({ rhi::ResourceType::Texture2D });
	//.Set<MemoryStatisticsComponents::ResourceID>({ owningResourceID });
	trackDesc.attach = allocationBundle;

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
			m_textureHandle,
			trackDesc);

		//auto result = device.CreateCommittedResource(textureDesc, textureResource);
	}

	//m_placedResourceHeap = aliasTarget ? aliasTarget->GetPlacedResourceHeap() : rhi::HeapHandle();

	m_width = desc.imageDimensions[0].width;
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

	RegisterLiveAlloc();
	UpdateLiveAllocName(name);

}

void GpuTextureBacking::SetName(const char* newName)
{
	m_textureHandle.ApplyComponentBundle(EntityComponentBundle().Set<MemoryStatisticsComponents::ResourceName>({ newName }));
	m_textureHandle.GetResource().SetName(newName);
	UpdateLiveAllocName(newName);
}

rhi::BarrierBatch GpuTextureBacking::GetEnhancedBarrierGroup(RangeSpec range, rhi::ResourceAccessType prevAccessType, rhi::ResourceAccessType newAccessType, rhi::ResourceLayout prevLayout, rhi::ResourceLayout newLayout, rhi::ResourceSyncState prevSyncState, rhi::ResourceSyncState newSyncState) {

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

void GpuTextureBacking::RegisterLiveAlloc() {
	std::scoped_lock lock(s_liveMutex);
	LiveAllocInfo info{};
	s_liveAllocs[this] = info;
}

void GpuTextureBacking::UnregisterLiveAlloc() {
	std::scoped_lock lock(s_liveMutex);
	if (s_liveAllocs.find(this) == s_liveAllocs.end()) { // If an error occurs here, it means something is being destructed after this global was destroyed.
		spdlog::warn("GpuBufferBacking being destroyed but not found in live allocations!");
	}
	else {
		s_liveAllocs.erase(this);
	}
}

void GpuTextureBacking::UpdateLiveAllocName(const char* name) {
	std::scoped_lock lock(s_liveMutex);
	auto it = s_liveAllocs.find(this);
	if (it != s_liveAllocs.end()) {
		it->second.name = name ? name : "";
	}
}

unsigned int GpuTextureBacking::DumpLiveTextures() {
	std::scoped_lock lock(s_liveMutex);
	for (const auto& [ptr, info] : s_liveAllocs) {
		spdlog::warn("Live texture still tracked: name='{}'", info.name);
	}
	return static_cast<unsigned int>(s_liveAllocs.size());
}