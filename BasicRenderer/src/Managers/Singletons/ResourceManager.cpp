#include "Managers/Singletons/ResourceManager.h"

#include <rhi_helpers.h>

#include "Utilities/Utilities.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Resources/Buffers/DynamicStructuredBuffer.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Resources/Buffers/DynamicBuffer.h"
#include "Resources/Buffers/SortedUnsignedIntBuffer.h"
#include "Managers/Singletons/UploadManager.h"
void ResourceManager::Initialize() {
	//for (int i = 0; i < 3; i++) {
	//    frameResourceCopies[i] = std::make_unique<FrameResource>();
	//    frameResourceCopies[i]->Initialize();
	//}

	auto device = DeviceManager::GetInstance().GetDevice();
	m_cbvSrvUavHeap = std::make_shared<DescriptorHeap>(device, rhi::DescriptorHeapType::CbvSrvUav, 1000000 /*D3D12_MAX_SHADER_VISIBLE_DESCRIPTOR_HEAP_SIZE_TIER_1*/, true);
	m_samplerHeap = std::make_shared<DescriptorHeap>(device, rhi::DescriptorHeapType::Sampler, 2048, true);
	m_rtvHeap = std::make_shared<DescriptorHeap>(device, rhi::DescriptorHeapType::RTV, 10000, false);
	m_dsvHeap = std::make_shared<DescriptorHeap>(device, rhi::DescriptorHeapType::DSV, 10000, false);
	m_nonShaderVisibleHeap = std::make_shared<DescriptorHeap>(device, rhi::DescriptorHeapType::CbvSrvUav, 100000, false);

	m_perFrameBuffer = CreateIndexedConstantBuffer<PerFrameCB>(L"PerFrameCB");

	perFrameCBData.ambientLighting = DirectX::XMVectorSet(0.1f, 0.1f, 0.1f, 1.0f);
	perFrameCBData.numShadowCascades = SettingsManager::GetInstance().getSettingGetter<uint8_t>("numDirectionalLightCascades")();
	auto shadowCascadeSplits = SettingsManager::GetInstance().getSettingGetter<std::vector<float>>("directionalLightCascadeSplits")();
	switch (perFrameCBData.numShadowCascades) {
	case 1:
		perFrameCBData.shadowCascadeSplits = DirectX::XMVectorSet(shadowCascadeSplits[0], 0, 0, 0);
		break;
	case 2:
		perFrameCBData.shadowCascadeSplits = DirectX::XMVectorSet(shadowCascadeSplits[0], shadowCascadeSplits[1], 0, 0);
		break;
	case 3:
		perFrameCBData.shadowCascadeSplits = DirectX::XMVectorSet(shadowCascadeSplits[0], shadowCascadeSplits[1], shadowCascadeSplits[2], 0);
		break;
	case 4:
		perFrameCBData.shadowCascadeSplits = DirectX::XMVectorSet(shadowCascadeSplits[0], shadowCascadeSplits[1], shadowCascadeSplits[2], shadowCascadeSplits[3]);
	}

	auto result = device.CreateCommittedResource(rhi::helpers::ResourceDesc::Buffer(sizeof(UINT), rhi::HeapType::Upload), m_uavCounterReset);

	void* pMappedCounterReset = nullptr;
	
    m_uavCounterReset->Map(&pMappedCounterReset, 0, 0);
	ZeroMemory(pMappedCounterReset, sizeof(UINT));
	m_uavCounterReset->Unmap(0, 0);
}

rhi::DescriptorHeap ResourceManager::GetSRVDescriptorHeap() {
	return m_cbvSrvUavHeap->GetHeap();
}

rhi::DescriptorHeap ResourceManager::GetSamplerDescriptorHeap() {
	return m_samplerHeap->GetHeap();
}


void ResourceManager::UpdatePerFrameBuffer(UINT cameraIndex, UINT numLights, DirectX::XMUINT2 screenRes, DirectX::XMUINT3 clusterSizes, unsigned int frameIndex) {
	perFrameCBData.mainCameraIndex = cameraIndex;
	perFrameCBData.numLights = numLights;
	perFrameCBData.screenResX = screenRes.x;
	perFrameCBData.screenResY = screenRes.y;
	perFrameCBData.lightClusterGridSizeX = clusterSizes.x;
	perFrameCBData.lightClusterGridSizeY = clusterSizes.y;
	perFrameCBData.lightClusterGridSizeZ = clusterSizes.z;
	perFrameCBData.nearClusterCount = 4;
	perFrameCBData.clusterZSplitDepth = 6.0f;
	perFrameCBData.frameIndex = frameIndex % 64; // Wrap around every 64 frames

	QUEUE_UPLOAD(&perFrameCBData, sizeof(PerFrameCB), m_perFrameBuffer, 0);
}

UINT ResourceManager::CreateIndexedSampler(const rhi::SamplerDesc& samplerDesc) {
	auto device = DeviceManager::GetInstance().GetDevice();

	UINT index = m_samplerHeap->AllocateDescriptor();
	//D3D12_CPU_DESCRIPTOR_HANDLE handle = m_samplerHeap->GetCPUHandle(index);

	//device->CreateSampler(&samplerDesc, handle);
	device.CreateSampler({ m_samplerHeap->GetHeap().GetHandle(), index}, samplerDesc);
	return index;
}


std::shared_ptr<Buffer> ResourceManager::CreateBuffer(size_t bufferSize, void* pInitialData, bool UAV) {
	auto device = DeviceManager::GetInstance().GetDevice();
	auto dataBuffer = Buffer::CreateShared(rhi::HeapType::DeviceLocal, bufferSize, UAV);
	if (pInitialData) {
		QUEUE_UPLOAD(pInitialData, bufferSize, dataBuffer, 0);
	}

	return dataBuffer;
}


std::shared_ptr<DynamicBuffer> ResourceManager::CreateIndexedDynamicBuffer(size_t elementSize, size_t numElements, std::wstring name, bool byteAddress, bool UAV) {
#if defined(_DEBUG)
	assert(numElements > 0 && byteAddress ? elementSize == 1 : (elementSize > 0 && elementSize % 4 == 0));
	assert(byteAddress ? numElements % 4 == 0 : true);
#endif
	auto device = DeviceManager::GetInstance().GetDevice();

	size_t bufferSize = elementSize * numElements;
	{
		const size_t align = 4;
		const size_t rem = bufferSize % align;
		if (rem) bufferSize += (align - rem); // Align up to 4 bytes
	}
	// Create the dynamic structured buffer instance
	UINT bufferID = GetNextResizableBufferID();
	std::shared_ptr<DynamicBuffer> pDynamicBuffer = DynamicBuffer::CreateShared(byteAddress, elementSize, bufferID, bufferSize, name, UAV);

	pDynamicBuffer->SetOnResized([this](UINT bufferID, size_t typeSize, size_t capacity, bool byteAddress, DynamicBufferBase* buffer, bool UAV) {
		this->onDynamicBufferResized(bufferID, typeSize, capacity, byteAddress, buffer, UAV);
		});

	// Create an SRV for the buffer

	UINT index = m_cbvSrvUavHeap->AllocateDescriptor();

	device.CreateShaderResourceView(
		{ m_cbvSrvUavHeap->GetHeap().GetHandle(), index},
		pDynamicBuffer->GetBuffer()->GetAPIResource().GetHandle(),
		{
			.dimension = rhi::SrvDim::Buffer,
			.formatOverride = byteAddress ? rhi::Format::R32_Typeless : rhi::Format::Unknown,
			.buffer = {
				.kind = byteAddress ? rhi::BufferViewKind::Raw : rhi::BufferViewKind::Structured,
				.firstElement = 0,
				.numElements = static_cast<uint32_t>(byteAddress ? numElements / 4 : numElements),
				.structureByteStride = static_cast<uint32_t>(byteAddress ? 0 : elementSize)
			}
		});

	ShaderVisibleIndexInfo srvInfo;
	srvInfo.slot.index = index;
	srvInfo.slot.heap = m_cbvSrvUavHeap->GetHeap().GetHandle();

	pDynamicBuffer->SetSRVView(SRVViewType::Buffer, m_cbvSrvUavHeap, {{srvInfo}});

	if (UAV) {
		// Shader visible UAV
		unsigned int uavShaderVisibleIndex = m_cbvSrvUavHeap->AllocateDescriptor();
		device.CreateUnorderedAccessView(
			{ m_cbvSrvUavHeap->GetHeap().GetHandle(), uavShaderVisibleIndex},
			pDynamicBuffer->GetBuffer()->GetAPIResource().GetHandle(),
			{
				.dimension = rhi::UavDim::Buffer,
				.buffer = {
					.kind = byteAddress ? rhi::BufferViewKind::Raw : rhi::BufferViewKind::Structured,
					.firstElement = 0,
					.numElements = static_cast<uint32_t>(byteAddress ? numElements / 4 : numElements),
					.structureByteStride = static_cast<uint32_t>(byteAddress ? 0 : elementSize)
				}
			});

		ShaderVisibleIndexInfo uavInfo;
		uavInfo.slot.index = uavShaderVisibleIndex;
		uavInfo.slot.heap = m_cbvSrvUavHeap->GetHeap().GetHandle();
		pDynamicBuffer->SetUAVGPUDescriptors(m_cbvSrvUavHeap, {{uavInfo}}, 0);
	}

	return pDynamicBuffer;
}

std::shared_ptr<SortedUnsignedIntBuffer> ResourceManager::CreateIndexedSortedUnsignedIntBuffer(uint64_t capacity, std::wstring name) {
	auto device = DeviceManager::GetInstance().GetDevice();

	UINT bufferID = GetInstance().GetNextResizableBufferID();
	std::shared_ptr<SortedUnsignedIntBuffer> pBuffer = SortedUnsignedIntBuffer::CreateShared(bufferID, capacity, name);

	pBuffer->SetOnResized([this](UINT bufferID, UINT capacity, UINT numElements, DynamicBufferBase* buffer) {
		this->onDynamicStructuredBufferResized(bufferID, capacity, numElements, buffer, false);
		});

	// Create an SRV for the buffer
	UINT index = m_cbvSrvUavHeap->AllocateDescriptor();
	device.CreateShaderResourceView(
		{ m_cbvSrvUavHeap->GetHeap().GetHandle(), index},
		pBuffer->GetBuffer()->GetAPIResource().GetHandle(),
		{
			.dimension = rhi::SrvDim::Buffer,
			.formatOverride = rhi::Format::Unknown,
			.buffer = {
				.kind = rhi::BufferViewKind::Structured,
				.firstElement = 0,
				.numElements = static_cast<uint32_t>(capacity),
				.structureByteStride = 4
			}
		});

	ShaderVisibleIndexInfo srvInfo;
	srvInfo.slot.index = index;
	srvInfo.slot.heap = m_cbvSrvUavHeap->GetHeap().GetHandle();

	pBuffer->SetSRVView(SRVViewType::Buffer, m_cbvSrvUavHeap, {{srvInfo}});

	return pBuffer;
}

std::pair<rhi::ResourcePtr,rhi::HeapHandle> ResourceManager::CreateTextureResource(
	const TextureDescription& desc,
	rhi::HeapHandle placedResourceHeap) {

	auto device = DeviceManager::GetInstance().GetDevice();

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

	rhi::ResourcePtr textureResource;
	if (desc.allowAlias) {
		//textureResource = device.CreatePlacedResource(placedResourceHeap, 0, textureDesc); // TODO: handle offset
		throw std::runtime_error("Aliasing resources not implemented yet");
	}
	else {
		auto result = device.CreateCommittedResource(textureDesc, textureResource);
	}

	return std::make_pair(std::move(textureResource), placedResourceHeap);
}

void ResourceManager::UploadTextureData(rhi::Resource& dstTexture, const TextureDescription& desc, const std::vector<const stbi_uc*>& initialData, unsigned int mipLevels) {

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

void ResourceManager::Cleanup()
{
	m_perFrameBuffer.reset();
}