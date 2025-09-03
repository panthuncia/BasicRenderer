#include "Managers/Singletons/ResourceManager.h"

#include <rhi_helpers.h>

#include "Utilities/Utilities.h"
#include "DirectX/d3dx12.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Resources/Buffers/DynamicStructuredBuffer.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Resources/Buffers/DynamicBuffer.h"
#include "Resources/Buffers/SortedUnsignedIntBuffer.h"
#include "Managers/Singletons/UploadManager.h"
void ResourceManager::Initialize(rhi::Queue commandQueue) {
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

	perFrameBufferHandle = CreateIndexedConstantBuffer<PerFrameCB>(L"PerFrameCB");

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

	//InitializeUploadHeap();
	InitializeCopyCommandQueue();
	InitializeTransitionCommandList();
	SetTransitionCommandQueue(commandQueue);



	//auto clearDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(UINT));
	//auto clearHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	//ThrowIfFailed(device->CreateCommittedResource(
	//	&clearHeapProps,
	//	D3D12_HEAP_FLAG_NONE,
	//	&clearDesc,
	//	D3D12_RESOURCE_STATE_GENERIC_READ,
	//	nullptr,
	//	IID_PPV_ARGS(&m_uavCounterReset)));

	device.CreateCommittedResource(rhi::helpers::ResourceDesc::Buffer(sizeof(UINT), rhi::Memory::Upload));

	void* pMappedCounterReset = nullptr;
	
    m_uavCounterReset->Map(&pMappedCounterReset, 0, 0);
	ZeroMemory(pMappedCounterReset, sizeof(UINT));
	m_uavCounterReset->Unmap(0, 0);
}

rhi::DescriptorHeapHandle ResourceManager::GetSRVDescriptorHeap() {
	return m_cbvSrvUavHeap->GetHeap();
}

rhi::DescriptorHeapHandle ResourceManager::GetSamplerDescriptorHeap() {
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

	UploadManager::GetInstance().UploadData(&perFrameCBData, sizeof(PerFrameCB), perFrameBufferHandle.get(), 0);
}

void ResourceManager::WaitForCopyQueue() {
	auto device = DeviceManager::GetInstance().GetDevice();
	copyCommandQueue.Signal({ copyFence.Get(), ++copyFenceValue});
	if (device.TimelineCompletedValue(copyFence.Get()) < copyFenceValue) {
		device.TimelineHostWait({ copyFence.Get(), copyFenceValue});
	}
}

void ResourceManager::WaitForTransitionQueue() {
	auto device = DeviceManager::GetInstance().GetDevice();
	transitionCommandQueue.Signal({ transitionFence.Get(), ++transitionFenceValue});
	if (device.TimelineCompletedValue(transitionFence.Get()) < transitionFenceValue) {
		device.TimelineHostWait({ transitionFence.Get(), transitionFenceValue});
	}
}

void ResourceManager::InitializeCopyCommandQueue() {
	auto device = DeviceManager::GetInstance().GetDevice();

	copyCommandQueue = device.GetQueue(rhi::QueueKind::Graphics); // TODO: async copy queue
	copyCommandAllocator = device.CreateCommandAllocator(rhi::QueueKind::Graphics);
	copyCommandList = device.CreateCommandList(rhi::QueueKind::Graphics, copyCommandAllocator.Get());
	copyCommandList->End();

	copyFence = device.CreateTimeline(copyFenceValue, "CopyFence");
}

void ResourceManager::InitializeTransitionCommandList() {
	auto device = DeviceManager::GetInstance().GetDevice();

	transitionCommandQueue = device.GetQueue(rhi::QueueKind::Graphics);
	transitionCommandAllocator = device.CreateCommandAllocator(rhi::QueueKind::Graphics);
	transitionCommandList = device.CreateCommandList(rhi::QueueKind::Graphics, transitionCommandAllocator.Get());

	transitionFence = device.CreateTimeline(transitionFenceValue, "TransitionFence");
}

void ResourceManager::SetTransitionCommandQueue(rhi::Queue queue) {
	transitionCommandQueue = queue;
}

UINT ResourceManager::CreateIndexedSampler(const rhi::SamplerDesc& samplerDesc) {
	auto device = DeviceManager::GetInstance().GetDevice();

	UINT index = m_samplerHeap->AllocateDescriptor();
	//D3D12_CPU_DESCRIPTOR_HANDLE handle = m_samplerHeap->GetCPUHandle(index);

	//device->CreateSampler(&samplerDesc, handle);
	device.CreateSampler({ m_samplerHeap->GetHeap(), index}, samplerDesc);
	return index;
}

void ResourceManager::GetCopyCommandList(rhi::CommandListPtr& commandList, rhi::CommandAllocatorPtr& commandAllocator) {
	auto device = DeviceManager::GetInstance().GetDevice();

	// Create a new command allocator if none is available or reuse an existing one
	if (!commandAllocator ) {
		commandAllocator->Recycle();
		commandAllocator = device.CreateCommandAllocator(rhi::QueueKind::Graphics);
	}

	if (!commandList) {
		commandList->Recycle(commandAllocator.Get());
		commandList = device.CreateCommandList(rhi::QueueKind::Graphics, commandAllocator.Get());
	}
}

void ResourceManager::ExecuteAndWaitForCommandList(rhi::CommandListPtr& commandList, rhi::CommandAllocatorPtr& commandAllocator) {
	auto device = DeviceManager::GetInstance().GetDevice();
	static rhi::Queue copyCommandQueue;
	static rhi::TimelinePtr copyFence;
	static UINT64 copyFenceValue = 0;
	bool copyFenceEventCreated = false;

	// Create the command queue if it hasn't been created yet
	if (!copyCommandQueue) {
		copyCommandQueue = device.GetQueue(rhi::QueueKind::Graphics);
	}

	// Create a fence for synchronization if it hasn't been created yet
	if (!copyFenceEventCreated) {
		copyFence = device.CreateTimeline(copyFenceValue, "TempCopyFence");
		copyFenceEventCreated = true;
	}

	// Close the command list and execute it
	commandList->End();
	copyCommandQueue.Submit({ &commandList.Get(), 1}, {});

	// Increment the fence value and signal the fence
	++copyFenceValue;
	copyCommandQueue.Signal({ copyFence.Get(), copyFenceValue});

	// Wait until the fence is completed
	if (device.TimelineCompletedValue(copyFence.Get()) < copyFenceValue) {
		device.TimelineHostWait({ copyFence.Get(), copyFenceValue});
	}

	commandAllocator->Recycle();
}

std::shared_ptr<Buffer> ResourceManager::CreateBuffer(size_t bufferSize, void* pInitialData, bool UAV) {
	auto device = DeviceManager::GetInstance().GetDevice();
	auto dataBuffer = Buffer::CreateShared(device, ResourceCPUAccessType::NONE, bufferSize, false, UAV);
	if (pInitialData) {
		UploadManager::GetInstance().UploadData(pInitialData, bufferSize, dataBuffer.get(), 0);
	}

//	ResourceTransition transition = { dataBuffer.get(), ResourceState::UNKNOWN,  usageType };
//#if defined(_DEBUG)
//		transition.name = L"Buffer";
//#endif
//	QueueResourceTransition(transition);
	return dataBuffer;
}

//void ResourceManager::QueueResourceTransition(const ResourceTransition& transition) {
//	queuedResourceTransitions.push_back(transition);
//}

//void ResourceManager::ExecuteResourceTransitions() {
//	queuedResourceTransitions.clear();
//	return;
//	auto& device = DeviceManager::GetInstance().GetDevice();
//	auto& commandList = transitionCommandList;
//	auto& commandAllocator = transitionCommandAllocator;
//	if (queuedResourceTransitions.size() == 0) {
//		return;
//	}
//
//	auto hr = commandList->Reset(commandAllocator.Get(), nullptr);
//	if (FAILED(hr)) {
//		spdlog::error("Failed to reset command list");
//	}
//	std::vector<D3D12_RESOURCE_BARRIER> barriers;
//	for (auto& transition : queuedResourceTransitions) {
//		if (transition.resource == nullptr) {
//			spdlog::error("Resource is null in transition");
//			throw std::runtime_error("Resource is null");
//		}
//		auto& trans = transition.resource->GetTransitions(transition.beforeState, transition.afterState);
//		for (auto& barrier : trans) {
//			barriers.push_back(barrier);
//		}
//		transition.resource->SetState(transition.afterState);
//	}
//	transitionCommandList->ResourceBarrier(barriers.size(), barriers.data());
//
//	hr = commandList->Close();
//	if (FAILED(hr)) {
//		spdlog::error("Failed to close command list");
//	}
//
//	ID3D12CommandList* ppCommandLists[] = { transitionCommandList.Get() };
//	transitionCommandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);
//
//	//queuedResourceTransitions.clear();
//}

std::shared_ptr<DynamicBuffer> ResourceManager::CreateIndexedDynamicBuffer(size_t elementSize, size_t numElements, std::wstring name, bool byteAddress, bool UAV) {
#if defined(_DEBUG)
	assert(numElements > 0 && byteAddress ? elementSize == 1 : (elementSize > 0 && elementSize % 4 == 0));
	assert(byteAddress ? numElements % 4 == 0 : true);
#endif
	auto device = DeviceManager::GetInstance().GetDevice();

	size_t bufferSize = elementSize * numElements;
	bufferSize += bufferSize % 4; // Align to 4 bytes
	// Create the dynamic structured buffer instance
	UINT bufferID = GetNextResizableBufferID();
	std::shared_ptr<DynamicBuffer> pDynamicBuffer = DynamicBuffer::CreateShared(byteAddress, elementSize, bufferID, bufferSize, name, UAV);
//	ResourceTransition transition;
//	transition.resource = pDynamicBuffer.get();
//	transition.beforeState = ResourceState::UNKNOWN;
//	transition.afterState = usage;
//#if defined(_DEBUG)
//	transition.name = name;
//#endif
//	QueueResourceTransition(transition);
	pDynamicBuffer->SetOnResized([this](UINT bufferID, size_t typeSize, size_t capacity, bool byteAddress, DynamicBufferBase* buffer, bool UAV) {
		this->onDynamicBufferResized(bufferID, typeSize, capacity, byteAddress, buffer, UAV);
		});

	// Create an SRV for the buffer

	UINT index = m_cbvSrvUavHeap->AllocateDescriptor();

	device.CreateShaderResourceView(
		{ m_cbvSrvUavHeap->GetHeap(), index }, 
		{
			.dim = rhi::SrvDim::Buffer,
			.resource = pDynamicBuffer->GetBuffer()->GetAPIResource(),
			.formatOverride = byteAddress ? rhi::Format::R32_Typeless : rhi::Format::Unknown,
			.buffer = {
				.kind = byteAddress ? rhi::BufferViewKind::Raw : rhi::BufferViewKind::Structured,
				.firstElement = 0,
				.numElements = static_cast<uint32_t>(byteAddress ? numElements / 4 : numElements),
				.structureByteStride = static_cast<uint32_t>(byteAddress ? 0 : elementSize)
			}
		});

	ShaderVisibleIndexInfo srvInfo;
	srvInfo.index = index;

	pDynamicBuffer->SetSRVView(SRVViewType::Buffer, m_cbvSrvUavHeap, {{srvInfo}});

	if (UAV) {
		// Shader visible UAV
		unsigned int uavShaderVisibleIndex = m_cbvSrvUavHeap->AllocateDescriptor();
		device.CreateUnorderedAccessView(
			{ m_cbvSrvUavHeap->GetHeap(), uavShaderVisibleIndex },
			{
				.type = rhi::UAVType::Buffer,
				.resource = pDynamicBuffer->GetBuffer()->GetAPIResource(),
				.bufKind = byteAddress ? rhi::BufferViewKind::Raw : rhi::BufferViewKind::Structured,
				.bufFormat = byteAddress ? rhi::Format::R32_Typeless : rhi::Format::Unknown,
				.firstElement = 0,
				.numElements = static_cast<uint32_t>(byteAddress ? numElements / 4 : numElements),
				.structureByteStride = static_cast<uint32_t>(byteAddress ? 0 : elementSize)
			});

		ShaderVisibleIndexInfo uavInfo;
		uavInfo.index = uavShaderVisibleIndex;
		pDynamicBuffer->SetUAVGPUDescriptors(m_cbvSrvUavHeap, {{uavInfo}}, 0);
	}

	return pDynamicBuffer;
}

std::shared_ptr<SortedUnsignedIntBuffer> ResourceManager::CreateIndexedSortedUnsignedIntBuffer(uint64_t capacity, std::wstring name) {
	auto device = DeviceManager::GetInstance().GetDevice();

	UINT bufferID = GetInstance().GetNextResizableBufferID();
	std::shared_ptr<SortedUnsignedIntBuffer> pBuffer = SortedUnsignedIntBuffer::CreateShared(bufferID, capacity, name);
//	ResourceTransition transition;
//	transition.resource = pBuffer.get();
//	transition.beforeState = ResourceState::UNKNOWN;
//	transition.afterState = usage;
//#if defined(_DEBUG)
//	transition.name = name;
//#endif
	//QueueResourceTransition(transition);
	pBuffer->SetOnResized([this](UINT bufferID, UINT capacity, UINT numElements, DynamicBufferBase* buffer) {
		this->onDynamicStructuredBufferResized(bufferID, capacity, numElements, buffer, false);
		});

	// Create an SRV for the buffer
	UINT index = m_cbvSrvUavHeap->AllocateDescriptor();
	device.CreateShaderResourceView(
		{ m_cbvSrvUavHeap->GetHeap(), index },
		{
			.dim = rhi::SrvDim::Buffer,
			.resource = pBuffer->GetBuffer()->GetAPIResource(),
			.formatOverride = rhi::Format::Unknown,
			.buffer = {
				.kind = rhi::BufferViewKind::Structured,
				.firstElement = 0,
				.numElements = static_cast<uint32_t>(capacity),
				.structureByteStride = 4
			}
		});

	ShaderVisibleIndexInfo srvInfo;
	srvInfo.index = index;

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
		depthClearValue.format = desc.dsvFormat == rhi::Format::Unknown ? desc.format : desc.dsvFormat;
		depthClearValue.depthStencil.depth = desc.depthClearValue;
		depthClearValue.depthStencil.stencil = 0;
		clearValue = &depthClearValue;
	}
	else if (desc.hasRTV) {
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
			.depthOrLayers = desc.isCubemap ? 6 * arraySize : arraySize,
			.mipLevels = mipLevels,
			.sampleCount = 1,
			.initialLayout = rhi::ResourceLayout::Common,
			.optimizedClear = clearValue
		}
	};
	if (desc.hasRTV) {
		textureDesc.flags |= rhi::ResourceFlags::AllowRenderTarget;
	}
	if (desc.hasDSV) {
		textureDesc.flags |= rhi::ResourceFlags::AllowDepthStencil;
	}
	if (desc.hasUAV) {
		textureDesc.flags |= rhi::ResourceFlags::AllowUnorderedAccess;
	}
	// Create the texture resource

	rhi::ResourcePtr textureResource;
	if (desc.allowAlias) {
		//textureResource = device.CreatePlacedResource(placedResourceHeap, 0, textureDesc); // TODO: handle offset
		throw std::runtime_error("Aliasing resources not implemented yet");
	}
	else {
		textureResource = device.CreateCommittedResource(textureDesc);
	}

	return std::make_pair(textureResource, placedResourceHeap);
}

void ResourceManager::UploadTextureData(rhi::Resource& dstTexture, const TextureDescription& desc, const std::vector<const stbi_uc*>& initialData, unsigned int arraySize, unsigned int mipLevels) {

	// Encure the copy command list and allocator are ready
	GetCopyCommandList(commandList, commandAllocator);

	if (initialData.empty()) return;

	// NOTE: the original expression had a precedence bug.
	// Correct: effective array slices = arraySize * (isCubemap ? 6 : 1)
	const uint32_t faces = desc.isCubemap ? 6u : 1u;
	const uint32_t arraySlices = faces * static_cast<uint32_t>(arraySize);
	const uint32_t numSubres = arraySlices * static_cast<uint32_t>(mipLevels);

	// Build a dense SubresourceData table (nullptr entries are allowed; they’ll be skipped)
	std::vector<rhi::helpers::SubresourceData> srd(numSubres);
	std::vector<std::vector<stbi_uc>> expandedImages;   // keep storage alive during copy
	expandedImages.reserve(numSubres);

	// If caller passed fewer than numSubres pointers, pad with nullptrs.
	std::vector<const stbi_uc*> fullInitial(numSubres, nullptr);
	std::copy(initialData.begin(), initialData.end(), fullInitial.begin());

	int i = -1; // matches your original indexing over desc.imageDimensions[]
	for (uint32_t a = 0; a < arraySlices; ++a) {
		for (uint32_t m = 0; m < mipLevels; ++m) {
			++i;
			const uint32_t subIdx = m + a * mipLevels;

			const stbi_uc* imageData = fullInitial[subIdx];

			uint32_t width = std::max(1u, static_cast<uint32_t>(desc.imageDimensions[i].width >> m));
			uint32_t height = std::max(1u, static_cast<uint32_t>(desc.imageDimensions[i].height >> m));
			uint32_t channels = desc.channels;

			auto& out = srd[subIdx];

			// If provided pitches don't match raw (width*channels), treat as “pre-padded or compressed”
			if ((width * channels != desc.imageDimensions[i].rowPitch) ||
				(width * channels * height != desc.imageDimensions[i].slicePitch))
			{
				out.pData = imageData;
				out.rowPitch = desc.imageDimensions[i].rowPitch;
				out.slicePitch = desc.imageDimensions[i].slicePitch;
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
					out.rowPitch = width * channels;     // tightly packed
					out.slicePitch = out.rowPitch * height;
				}
				else {
					out.pData = nullptr;
					out.rowPitch = out.slicePitch = 0;
				}
			}
		}
	}

	// Record the uploads (helper creates an upload buffer, maps & packs rows, then emits the copies).
	// depthOrLayers = 1 for 2D/cube textures. (Use actual depth for 3D volumes.)
	const uint32_t baseW = desc.imageDimensions[0].width;
	const uint32_t baseH = desc.imageDimensions[0].height;

	auto device = DeviceManager::GetInstance().GetDevice();

	rhi::ResourcePtr upload = rhi::helpers::UpdateTextureSubresources(
		device,
		copyCommandList.Get(),
		dstTexture,
		desc.format,                               // rhi::Format
		baseW,
		baseH,
		/*depthOrLayers*/ 1,
		static_cast<uint32_t>(mipLevels),
		arraySlices,
		{ srd.data(), static_cast<uint32_t>(srd.size()) }
	);

	// Transition out of CopyDest (your original moved to COMMON)
	rhi::TextureBarrier tb{};
	tb.texture = dstTexture.GetHandle();
	tb.range = { /*baseMip*/0, static_cast<uint32_t>(mipLevels),
		/*baseLayer*/0, arraySlices };
	tb.beforeSync = rhi::ResourceSyncState::Copy;
	tb.afterSync = rhi::ResourceSyncState::All;         // generic
	tb.beforeAccess = rhi::ResourceAccessType::CopyDest;
	tb.afterAccess = rhi::ResourceAccessType::None;
	tb.beforeLayout = rhi::ResourceLayout::CopyDest;
	tb.afterLayout = rhi::ResourceLayout::Common;         // mirrors your D3D12 barrier

	rhi::BarrierBatch bb{};
	bb.textures = { &tb, 1 };
	copyCommandList->Barriers(bb);


	ExecuteAndWaitForCommandList(commandList, commandAllocator); // TODO - Replace this by using UploadManager

}