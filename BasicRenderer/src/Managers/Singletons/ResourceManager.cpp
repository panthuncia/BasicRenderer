#include "Managers/Singletons/ResourceManager.h"
#include "Utilities/Utilities.h"
#include "DirectX/d3dx12.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Resources/Buffers/DynamicStructuredBuffer.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Resources/Buffers/DynamicBuffer.h"
#include "Resources/Buffers/SortedUnsignedIntBuffer.h"
#include "Managers/Singletons/UploadManager.h"
void ResourceManager::Initialize(ID3D12CommandQueue* commandQueue) {
	//for (int i = 0; i < 3; i++) {
	//    frameResourceCopies[i] = std::make_unique<FrameResource>();
	//    frameResourceCopies[i]->Initialize();
	//}

	auto& device = DeviceManager::GetInstance().GetDevice();
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

	auto clearDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(UINT));
	auto clearHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	ThrowIfFailed(device->CreateCommittedResource(
		&clearHeapProps,
		D3D12_HEAP_FLAG_NONE,
		&clearDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&m_uavCounterReset)));

	UINT8* pMappedCounterReset = nullptr;
	CD3DX12_RANGE readRange(0, 0);        // We do not intend to read from this resource on the CPU.
	ThrowIfFailed(m_uavCounterReset->Map(0, &readRange, reinterpret_cast<void**>(&pMappedCounterReset)));
	ZeroMemory(pMappedCounterReset, sizeof(UINT));
	m_uavCounterReset->Unmap(0, nullptr);
}

CD3DX12_CPU_DESCRIPTOR_HANDLE ResourceManager::GetSRVCPUHandle(UINT index) {
	return m_cbvSrvUavHeap->GetCPUHandle(index);
}

CD3DX12_GPU_DESCRIPTOR_HANDLE ResourceManager::GetSRVGPUHandle(UINT index) {
	return m_cbvSrvUavHeap->GetGPUHandle(index);
}

ComPtr<ID3D12DescriptorHeap> ResourceManager::GetSRVDescriptorHeap() {
	return m_cbvSrvUavHeap->GetHeap();
}

ComPtr<ID3D12DescriptorHeap> ResourceManager::GetSamplerDescriptorHeap() {
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
	ThrowIfFailed(copyCommandQueue->Signal(copyFence.Get(), ++copyFenceValue));
	if (copyFence->GetCompletedValue() < copyFenceValue) {
		ThrowIfFailed(copyFence->SetEventOnCompletion(copyFenceValue, copyFenceEvent));
		WaitForSingleObject(copyFenceEvent, INFINITE);
	}
}

void ResourceManager::WaitForTransitionQueue() {
	ThrowIfFailed(transitionCommandQueue->Signal(transitionFence.Get(), ++transitionFenceValue));
	if (transitionFence->GetCompletedValue() < transitionFenceValue) {
		ThrowIfFailed(transitionFence->SetEventOnCompletion(transitionFenceValue, transitionFenceEvent));
		WaitForSingleObject(transitionFenceEvent, INFINITE);
	}
}

void ResourceManager::InitializeCopyCommandQueue() {
	auto& device = DeviceManager::GetInstance().GetDevice();

	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	ThrowIfFailed(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&copyCommandQueue)));

	ThrowIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&copyCommandAllocator)));
	ThrowIfFailed(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, copyCommandAllocator.Get(), nullptr, IID_PPV_ARGS(&copyCommandList)));
	copyCommandList->Close();

	ThrowIfFailed(device->CreateFence(copyFenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&copyFence)));
	copyFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	if (copyFenceEvent == nullptr) {
		ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
	}
}

void ResourceManager::InitializeTransitionCommandList() {
	auto& device = DeviceManager::GetInstance().GetDevice();

	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

	ThrowIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&transitionCommandAllocator)));
	ThrowIfFailed(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, transitionCommandAllocator.Get(), nullptr, IID_PPV_ARGS(&transitionCommandList)));
	transitionCommandList->Close();

	ThrowIfFailed(device->CreateFence(transitionFenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&transitionFence)));
	transitionFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	if (transitionFenceEvent == nullptr) {
		ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
	}
}

void ResourceManager::SetTransitionCommandQueue(ID3D12CommandQueue* queue) {
	transitionCommandQueue = queue;
}

UINT ResourceManager::CreateIndexedSampler(const D3D12_SAMPLER_DESC& samplerDesc) {
	auto& device = DeviceManager::GetInstance().GetDevice();

	UINT index = m_samplerHeap->AllocateDescriptor();
	D3D12_CPU_DESCRIPTOR_HANDLE handle = m_samplerHeap->GetCPUHandle(index);

	device->CreateSampler(&samplerDesc, handle);

	return index;
}

D3D12_CPU_DESCRIPTOR_HANDLE ResourceManager::getSamplerCPUHandle(UINT index) const {
	return m_samplerHeap->GetCPUHandle(index);
}

void ResourceManager::GetCopyCommandList(ComPtr<ID3D12GraphicsCommandList>& commandList, ComPtr<ID3D12CommandAllocator>& commandAllocator) {
	auto& device = DeviceManager::GetInstance().GetDevice();

	// Create a new command allocator if none is available or reuse an existing one
	if (!commandAllocator || FAILED(commandAllocator->Reset())) {
		ThrowIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator)));
	}

	if (!commandList || FAILED(commandList->Reset(commandAllocator.Get(), nullptr))) {
		ThrowIfFailed(device->CreateCommandList(
			0,
			D3D12_COMMAND_LIST_TYPE_DIRECT,
			commandAllocator.Get(),
			nullptr,
			IID_PPV_ARGS(&commandList)
		));
	}
}

void ResourceManager::ExecuteAndWaitForCommandList(ComPtr<ID3D12GraphicsCommandList>& commandList, ComPtr<ID3D12CommandAllocator>& commandAllocator) {
	auto& device = DeviceManager::GetInstance().GetDevice();
	static ComPtr<ID3D12CommandQueue> copyCommandQueue;
	static ComPtr<ID3D12Fence> copyFence;
	static HANDLE copyFenceEvent = nullptr;
	static UINT64 copyFenceValue = 0;

	// Create the command queue if it hasn't been created yet
	if (!copyCommandQueue) {
		D3D12_COMMAND_QUEUE_DESC queueDesc = {};
		queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		ThrowIfFailed(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&copyCommandQueue)));
	}

	// Create a fence for synchronization if it hasn't been created yet
	if (!copyFence) {
		ThrowIfFailed(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&copyFence)));
		copyFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		if (!copyFenceEvent) {
			ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
		}
	}

	// Close the command list and execute it
	ThrowIfFailed(commandList->Close());
	ID3D12CommandList* ppCommandLists[] = { commandList.Get() };
	copyCommandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

	// Increment the fence value and signal the fence
	++copyFenceValue;
	ThrowIfFailed(copyCommandQueue->Signal(copyFence.Get(), copyFenceValue));

	// Wait until the fence is completed
	if (copyFence->GetCompletedValue() < copyFenceValue) {
		ThrowIfFailed(copyFence->SetEventOnCompletion(copyFenceValue, copyFenceEvent));
		WaitForSingleObject(copyFenceEvent, INFINITE);
	}

	ThrowIfFailed(commandAllocator->Reset());
}

std::shared_ptr<Buffer> ResourceManager::CreateBuffer(size_t bufferSize, void* pInitialData, bool UAV) {
	auto& device = DeviceManager::GetInstance().GetDevice();
	auto dataBuffer = Buffer::CreateShared(device.Get(), ResourceCPUAccessType::NONE, bufferSize, false, UAV);
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
	auto& device = DeviceManager::GetInstance().GetDevice();

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
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = byteAddress ? DXGI_FORMAT_R32_TYPELESS : DXGI_FORMAT_UNKNOWN;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	srvDesc.Buffer.FirstElement = 0;
	srvDesc.Buffer.NumElements = static_cast<uint32_t>(byteAddress? numElements / 4 : numElements);
	srvDesc.Buffer.StructureByteStride = static_cast<uint32_t>(byteAddress ? 0 : elementSize);
	srvDesc.Buffer.Flags = byteAddress ? D3D12_BUFFER_SRV_FLAG_RAW : D3D12_BUFFER_SRV_FLAG_NONE;

	UINT index = m_cbvSrvUavHeap->AllocateDescriptor();
	bufferIDDescriptorIndexMap[bufferID] = index;
	CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandle = m_cbvSrvUavHeap->GetCPUHandle(index);
	device->CreateShaderResourceView(pDynamicBuffer->GetBuffer()->m_buffer.Get(), &srvDesc, cpuHandle);

	CD3DX12_GPU_DESCRIPTOR_HANDLE gpuHandle = m_cbvSrvUavHeap->GetGPUHandle(index);
	ShaderVisibleIndexInfo srvInfo;
	srvInfo.index = index;
	srvInfo.gpuHandle = gpuHandle;

	pDynamicBuffer->SetSRVView(SRVViewType::Buffer, m_cbvSrvUavHeap, {{srvInfo}});

	if (UAV) {
		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.Format = DXGI_FORMAT_UNKNOWN;
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
		uavDesc.Buffer.NumElements = static_cast<uint32_t>(numElements);
		uavDesc.Buffer.StructureByteStride = static_cast<uint32_t>(elementSize);
		uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
		uavDesc.Buffer.CounterOffsetInBytes = 0;

		// Shader visible UAV
		unsigned int uavShaderVisibleIndex = m_cbvSrvUavHeap->AllocateDescriptor();
		D3D12_CPU_DESCRIPTOR_HANDLE uavShaderVisibleHandle = m_cbvSrvUavHeap->GetCPUHandle(uavShaderVisibleIndex);
		device->CreateUnorderedAccessView(pDynamicBuffer->GetBuffer()->m_buffer.Get(), pDynamicBuffer->GetBuffer()->m_buffer.Get(), &uavDesc, uavShaderVisibleHandle);

		ShaderVisibleIndexInfo uavInfo;
		uavInfo.index = uavShaderVisibleIndex;
		uavInfo.gpuHandle = m_cbvSrvUavHeap->GetGPUHandle(uavShaderVisibleIndex);
		pDynamicBuffer->SetUAVGPUDescriptors(m_cbvSrvUavHeap, {{uavInfo}}, 0);
	}

	return pDynamicBuffer;
}

std::shared_ptr<SortedUnsignedIntBuffer> ResourceManager::CreateIndexedSortedUnsignedIntBuffer(uint64_t capacity, std::wstring name) {
	auto& device = DeviceManager::GetInstance().GetDevice();

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
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	srvDesc.Buffer.FirstElement = 0;
	srvDesc.Buffer.NumElements = static_cast<uint32_t>(capacity);
	srvDesc.Buffer.StructureByteStride = 4;
	srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

	UINT index = m_cbvSrvUavHeap->AllocateDescriptor();
	bufferIDDescriptorIndexMap[bufferID] = index;
	CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandle = m_cbvSrvUavHeap->GetCPUHandle(index);
	device->CreateShaderResourceView(pBuffer->GetBuffer()->m_buffer.Get(), &srvDesc, cpuHandle);

	CD3DX12_GPU_DESCRIPTOR_HANDLE gpuHandle = m_cbvSrvUavHeap->GetGPUHandle(index);
	ShaderVisibleIndexInfo srvInfo;
	srvInfo.index = index;
	srvInfo.gpuHandle = gpuHandle;

	pBuffer->SetSRVView(SRVViewType::Buffer, m_cbvSrvUavHeap, {{srvInfo}});

	return pBuffer;
}

std::pair<ComPtr<ID3D12Resource>,ComPtr<ID3D12Heap>> ResourceManager::CreateTextureResource(
	const TextureDescription& desc,
	ComPtr<ID3D12Heap> placedResourceHeap) {

	auto& device = DeviceManager::GetInstance().GetDevice();

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

	auto textureDesc = CreateTextureResourceDesc(
		desc.format,
		static_cast<uint32_t>(width),
		static_cast<uint32_t>(height),
		arraySize,
		mipLevels,
		desc.isCubemap,
		desc.hasRTV,
		desc.hasDSV,
		desc.hasUAV
	);

	// Handle clear values for RTV and DSV
	D3D12_CLEAR_VALUE* clearValue = nullptr;
	D3D12_CLEAR_VALUE depthClearValue = {};
	D3D12_CLEAR_VALUE colorClearValue = {};
	if (desc.hasDSV) {
		depthClearValue.Format = desc.dsvFormat == DXGI_FORMAT_UNKNOWN ? desc.format : desc.dsvFormat;
		depthClearValue.DepthStencil.Depth = desc.depthClearValue;
		depthClearValue.DepthStencil.Stencil = 0;
		clearValue = &depthClearValue;
	}
	else if (desc.hasRTV) {
		colorClearValue.Format = desc.rtvFormat == DXGI_FORMAT_UNKNOWN ? desc.format : desc.rtvFormat;
		colorClearValue.Color[0] = desc.clearColor[0];
		colorClearValue.Color[1] = desc.clearColor[1];
		colorClearValue.Color[2] = desc.clearColor[2];
		colorClearValue.Color[3] = desc.clearColor[3];
		clearValue = &colorClearValue;
	}

	// Create the texture resource

	ComPtr<ID3D12Resource> textureResource;
	if (desc.allowAlias) {
		textureResource = CreatePlacedTextureResource(
			device.Get(),
			textureDesc,
			clearValue,
			D3D12_HEAP_TYPE_DEFAULT,
			placedResourceHeap,
			D3D12_BARRIER_LAYOUT_COMMON
		);
	}
	else {
		textureResource = CreateCommittedTextureResource(
			device.Get(),
			textureDesc,
			clearValue
		);
	}

	return std::make_pair(textureResource, placedResourceHeap);
}

void ResourceManager::UploadTextureData(ID3D12Resource* pResource, const TextureDescription& desc, const std::vector<const stbi_uc*>& initialData, unsigned int arraySize, unsigned int mipLevels) {
	// Handle initial data upload if provided
	if (!initialData.empty()) {
		auto& device = DeviceManager::GetInstance().GetDevice();
		// Ensure initialData has the correct size
		uint32_t numTextures = arraySize * desc.isCubemap ? 6 : 1;
		uint32_t numSubresources = numTextures * mipLevels;

		// Create an upload heap
		UINT64 uploadBufferSize = GetRequiredIntermediateSize(pResource, 0, numSubresources);
		CD3DX12_RESOURCE_DESC uploadBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize);
		D3D12_HEAP_PROPERTIES uploadHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
		ComPtr<ID3D12Resource> textureUploadHeap;
		ThrowIfFailed(device->CreateCommittedResource(
			&uploadHeapProps,
			D3D12_HEAP_FLAG_NONE,
			&uploadBufferDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&textureUploadHeap)));

		// Prepare the subresource data
		std::vector<D3D12_SUBRESOURCE_DATA> subresourceData(numSubresources);
		std::vector<std::vector<stbi_uc>> expandedImages;

		std::vector<const stbi_uc*> fullInitialData(numSubresources, nullptr);
		std::copy(initialData.begin(), initialData.end(), fullInitialData.begin());

		int i = -1;
		for (uint32_t arraySlice = 0; arraySlice < numTextures; ++arraySlice) {
			for (uint32_t mip = 0; mip < mipLevels; ++mip) {
				i++;
				UINT subresourceIndex = mip + arraySlice * mipLevels;
				D3D12_SUBRESOURCE_DATA& subData = subresourceData[subresourceIndex];

				const stbi_uc* imageData = fullInitialData[subresourceIndex];

				UINT width = static_cast<uint32_t>(desc.imageDimensions[i].width >> mip);
				UINT height = static_cast<uint32_t>(desc.imageDimensions[i].height >> mip);
				UINT channels = desc.channels;
				if ((width * channels != desc.imageDimensions[i].rowPitch) || (width * channels * height != desc.imageDimensions[i].slicePitch)) // Probably compressed texture
				{
					subData.pData = imageData;
					subData.RowPitch = desc.imageDimensions[i].rowPitch;
					subData.SlicePitch = desc.imageDimensions[i].slicePitch;
				}
				else {
					if (imageData != nullptr) {
						// Expand image data if channels == 3
						const stbi_uc* imagePtr = imageData;
						if (channels == 3) {
							// Expand image data and store it in the container
							expandedImages.emplace_back(ExpandImageData(imageData, width, height));
							imagePtr = expandedImages.back().data();
							channels = 4; // Update channels after expansion
						}

						subData.pData = imagePtr;
						subData.RowPitch = width * channels;
						subData.SlicePitch = subData.RowPitch * height;
					}
					else {
						// For missing data, set pData to nullptr
						subData.pData = nullptr;
						subData.RowPitch = 0;
						subData.SlicePitch = 0;
					}
				}
			}
		}

		// Record commands to upload data
		GetCopyCommandList(commandList, commandAllocator);

		// Update only subresources with valid data
		std::vector<D3D12_SUBRESOURCE_DATA> validSubresourceData;
		UINT firstValidSubresource = UINT_MAX;
		for (UINT j = 0; j < subresourceData.size(); ++j) {
			if (subresourceData[j].pData != nullptr) {
				if (firstValidSubresource == UINT_MAX) {
					firstValidSubresource = j;
				}
				validSubresourceData.push_back(subresourceData[j]);
			}
		}

		if (!validSubresourceData.empty()) {
			UpdateSubresources(
				commandList.Get(),
				pResource,
				textureUploadHeap.Get(),
				0,
				firstValidSubresource,
				static_cast<UINT>(validSubresourceData.size()),
				validSubresourceData.data()
			);
		}

		CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			pResource,
			D3D12_RESOURCE_STATE_COPY_DEST,
			D3D12_RESOURCE_STATE_COMMON
		);
		commandList->ResourceBarrier(1, &barrier);

		ExecuteAndWaitForCommandList(commandList, commandAllocator); // TODO - Replace this by using UploadManager
	}
}