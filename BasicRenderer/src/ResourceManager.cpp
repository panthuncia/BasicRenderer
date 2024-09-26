#include "ResourceManager.h"
#include "Utilities.h"
#include "DirectX/d3dx12.h"
#include "DeviceManager.h"
#include "DynamicStructuredBuffer.h"
#include "SettingsManager.h"
void ResourceManager::Initialize(ID3D12CommandQueue* commandQueue, ID3D12CommandAllocator* commandAllocator) {
	//for (int i = 0; i < 3; i++) {
	//    frameResourceCopies[i] = std::make_unique<FrameResource>();
	//    frameResourceCopies[i]->Initialize();
	//}

	auto& device = DeviceManager::GetInstance().GetDevice();
	m_cbvSrvUavHeap = std::make_unique<DescriptorHeap>(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, D3D12_MAX_SHADER_VISIBLE_DESCRIPTOR_HEAP_SIZE_TIER_1, true);
	m_samplerHeap = std::make_unique<DescriptorHeap>(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 2048, true);
	m_rtvHeap = std::make_unique<DescriptorHeap>(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 10000, false);
	m_dsvHeap = std::make_unique<DescriptorHeap>(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 10000, false);

	perFrameBufferHandle = CreateIndexedConstantBuffer<PerFrameCB>(L"PerFrameCB");

	perFrameCBData.ambientLighting = XMVectorSet(0.1, 0.1, 0.1, 1.0);
	perFrameCBData.numShadowCascades = SettingsManager::GetInstance().getSettingGetter<uint8_t>("numDirectionalLightCascades")();
	auto shadowCascadeSplits = SettingsManager::GetInstance().getSettingGetter<std::vector<float>>("directionalLightCascadeSplits")();
	switch (perFrameCBData.numShadowCascades) {
	case 1:
		perFrameCBData.shadowCascadeSplits = XMVectorSet(shadowCascadeSplits[0], 0, 0, 0);
		break;
	case 2:
		perFrameCBData.shadowCascadeSplits = XMVectorSet(shadowCascadeSplits[0], shadowCascadeSplits[1], 0, 0);
		break;
	case 3:
		perFrameCBData.shadowCascadeSplits = XMVectorSet(shadowCascadeSplits[0], shadowCascadeSplits[1], shadowCascadeSplits[2], 0);
		break;
	case 4:
		perFrameCBData.shadowCascadeSplits = XMVectorSet(shadowCascadeSplits[0], shadowCascadeSplits[1], shadowCascadeSplits[2], shadowCascadeSplits[3]);
	}

	// Map the constant buffer and initialize it

	//InitializeUploadHeap();
	InitializeCopyCommandQueue();
	//InitializeTransitionCommandQueue();
	SetTransitionCommandQueue(commandQueue, commandAllocator);
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

//UINT ResourceManager::AllocateDescriptor() {
//    if (!freeDescriptors.empty()) {
//        UINT freeIndex = freeDescriptors.front();
//        freeDescriptors.pop();
//        return freeIndex;
//    }
//    else {
//        if (numAllocatedDescriptors >= descriptorHeap->GetDesc().NumDescriptors) {
//            throw std::runtime_error("Out of descriptor heap space!");
//        }
//        return numAllocatedDescriptors++;
//    }
//}
//
//void ResourceManager::ReleaseDescriptor(UINT index) {
//    freeDescriptors.push(index);
//}


void ResourceManager::UpdatePerFrameBuffer(DirectX::XMFLOAT3 eyeWorld, DirectX::XMMATRIX viewMatrix, DirectX::XMMATRIX projectionMatrix, UINT numLights, UINT lightBufferIndex, UINT pointCubemapMatricesBufferIndex, UINT spotMatricesBufferIndex, UINT directionalCascadeMatricesBufferIndex) {

	perFrameCBData.viewMatrix = viewMatrix;
	perFrameCBData.projectionMatrix = projectionMatrix;
	perFrameCBData.eyePosWorldSpace = DirectX::XMLoadFloat3(&eyeWorld);
	perFrameCBData.numLights = numLights;
	perFrameCBData.lightBufferIndex = lightBufferIndex;
	perFrameCBData.pointLightCubemapBufferIndex = pointCubemapMatricesBufferIndex;
	perFrameCBData.spotLightMatrixBufferIndex = spotMatricesBufferIndex;
	perFrameCBData.directionalLightCascadeBufferIndex = directionalCascadeMatricesBufferIndex;

	UpdateConstantBuffer(perFrameBufferHandle, perFrameCBData);
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

void ResourceManager::InitializeTransitionCommandQueue() {
	auto& device = DeviceManager::GetInstance().GetDevice();

	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	ThrowIfFailed(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&transitionCommandQueue)));

	ThrowIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&transitionCommandAllocator)));
	ThrowIfFailed(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, transitionCommandAllocator.Get(), nullptr, IID_PPV_ARGS(&transitionCommandList)));
	transitionCommandList->Close();

	ThrowIfFailed(device->CreateFence(transitionFenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&transitionFence)));
	transitionFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	if (transitionFenceEvent == nullptr) {
		ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
	}
}

void ResourceManager::SetTransitionCommandQueue(ID3D12CommandQueue* queue, ID3D12CommandAllocator* allocator) {
	transitionCommandQueue = queue;
	transitionCommandAllocator = allocator;

	auto& device = DeviceManager::GetInstance().GetDevice();

	ThrowIfFailed(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, transitionCommandAllocator.Get(), nullptr, IID_PPV_ARGS(&transitionCommandList)));
	transitionCommandList->Close();
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

TextureHandle<PixelBuffer> ResourceManager::CreateTextureFromImage(const stbi_uc* image, int width, int height, int channels, bool sRGB) {
	auto& device = DeviceManager::GetInstance().GetDevice();

	// Describe and create the texture resource

	DXGI_FORMAT textureFormat = DetermineTextureFormat(channels, sRGB, false);

	std::vector<stbi_uc> expandedImage;
	if (channels == 3) {
		expandedImage = ExpandImageData(image, width, height);
		image = expandedImage.data();
		channels = 4;
	}

	auto textureDesc = CreateTextureResourceDesc(textureFormat, width, height);

	auto textureResource = CreateCommittedTextureResource(device.Get(), textureDesc);

	// Create an upload heap
	const UINT64 uploadBufferSize = GetRequiredIntermediateSize(textureResource.Get(), 0, 1);
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

	// Upload the texture data to the GPU
	D3D12_SUBRESOURCE_DATA textureData = {};
	textureData.pData = image;
	textureData.RowPitch = width * channels; // Calculate based on the number of channels
	textureData.SlicePitch = textureData.RowPitch * height;

	// Initialize copy command list, used to copy from the upload heap to the default heap
	GetCopyCommandList(commandList, commandAllocator);

	UpdateSubresources(commandList.Get(), textureResource.Get(), textureUploadHeap.Get(), 0, 0, 1, &textureData);

	CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(textureResource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	commandList->ResourceBarrier(1, &barrier);

	ExecuteAndWaitForCommandList(commandList, commandAllocator);

	auto SRVInfo = CreateShaderResourceView(device.Get(), textureResource.Get(), textureFormat, m_cbvSrvUavHeap.get());

	TextureHandle<PixelBuffer> handle;
	handle.texture = textureResource;
	handle.SRVInfo = SRVInfo;
	return handle;
}

TextureHandle<PixelBuffer> ResourceManager::CreateCubemapFromImages(const std::array<const stbi_uc*, 6>& images, int width, int height, int channels, bool sRGB) {

	auto& device = DeviceManager::GetInstance().GetDevice();

	DXGI_FORMAT textureFormat = DetermineTextureFormat(channels, sRGB, false);

	std::array<const stbi_uc*, 6> imageData = images;
	std::vector<std::vector<stbi_uc>> expandedImages(6);
	if (channels == 3) {
		for (int i = 0; i < 6; ++i) {
			expandedImages[i] = ExpandImageData(images[i], width, height);
			imageData[i] = expandedImages[i].data();
		}
		channels = 4;
	}

	auto textureDesc = CreateTextureResourceDesc(
		textureFormat,
		width,
		height,
		6,      // Array size is 6 for cubemap faces
		1,      // Mip levels
		true);  // isCubemap = true

	auto textureResource = CreateCommittedTextureResource(device.Get(), textureDesc);

	const UINT64 uploadBufferSize = GetRequiredIntermediateSize(textureResource.Get(), 0, 6);

	ComPtr<ID3D12Resource> textureUploadHeap;
	{
		CD3DX12_HEAP_PROPERTIES uploadHeapProps(D3D12_HEAP_TYPE_UPLOAD);
		CD3DX12_RESOURCE_DESC uploadBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize);
		ThrowIfFailed(device->CreateCommittedResource(
			&uploadHeapProps,
			D3D12_HEAP_FLAG_NONE,
			&uploadBufferDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&textureUploadHeap)));
	}

	std::vector<D3D12_SUBRESOURCE_DATA> subresourceData(6);
	for (int i = 0; i < 6; ++i) {
		D3D12_SUBRESOURCE_DATA& subData = subresourceData[i];
		subData.pData = imageData[i];
		subData.RowPitch = width * channels;
		subData.SlicePitch = subData.RowPitch * height;
	}

	GetCopyCommandList(commandList, commandAllocator);

	UpdateSubresources(commandList.Get(), textureResource.Get(), textureUploadHeap.Get(), 0, 0, 6, subresourceData.data());

	CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
		textureResource.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST,
		D3D12_RESOURCE_STATE_COMMON);
	commandList->ResourceBarrier(1, &barrier);


	ExecuteAndWaitForCommandList(commandList, commandAllocator);

	auto srvInfo = CreateShaderResourceView(
		device.Get(),
		textureResource.Get(),
		textureFormat,
		m_cbvSrvUavHeap.get(),
		true);  // isCubemap = true

	TextureHandle<PixelBuffer> handle;
	handle.texture = textureResource;
	handle.SRVInfo = srvInfo;

	return handle;
}

TextureHandle<PixelBuffer> ResourceManager::CreateTexture(int width, int height, int channels, bool isCubemap, bool RTV, bool DSV, bool UAV, ResourceState initialState) {
	auto& device = DeviceManager::GetInstance().GetDevice();

	DXGI_FORMAT textureFormat = DetermineTextureFormat(channels, false, DSV);

	auto textureDesc = CreateTextureResourceDesc(
		textureFormat,
		width,
		height,
		isCubemap ? 6 : 1,
		1,
		isCubemap,
		RTV,
		DSV);

	D3D12_CLEAR_VALUE* clearValue = nullptr;
	D3D12_CLEAR_VALUE depthClearValue = {};
	D3D12_CLEAR_VALUE colorClearValue = {};
	if (DSV) {
		depthClearValue.Format = DXGI_FORMAT_D32_FLOAT;
		depthClearValue.DepthStencil.Depth = 1.0f;
		depthClearValue.DepthStencil.Stencil = 0;
		clearValue = &depthClearValue;
	}
	else if (RTV && channels == 1) {
		colorClearValue.Format = DXGI_FORMAT_R32_FLOAT;
		colorClearValue.Color[0] = 1.0f;
		clearValue = &colorClearValue;
	}

	auto textureResource = CreateCommittedTextureResource(
		device.Get(),
		textureDesc,
		clearValue);

	auto srvInfo = CreateShaderResourceView(
		device.Get(),
		textureResource.Get(),
		textureFormat == DXGI_FORMAT_R32_TYPELESS ? DXGI_FORMAT_R32_FLOAT : textureFormat,
		m_cbvSrvUavHeap.get(),
		isCubemap);

	std::vector<NonShaderVisibleIndexInfo> rtvInfos;
	if (RTV) {
		rtvInfos = CreateRenderTargetViews(
			device.Get(),
			textureResource.Get(),
			textureFormat,
			m_rtvHeap.get(),
			isCubemap);
	}
	std::vector<NonShaderVisibleIndexInfo> dsvInfos;
	if (DSV) {
		dsvInfos = CreateDepthStencilViews(
			device.Get(),
			textureResource.Get(),
			m_dsvHeap.get(),
			isCubemap);
	}

	TextureHandle<PixelBuffer> handle;
	handle.texture = textureResource;
	handle.SRVInfo = srvInfo;
	handle.RTVInfo = rtvInfos;
	handle.DSVInfo = dsvInfos;

	return handle;
}

TextureHandle<PixelBuffer> ResourceManager::CreateTextureArray(int width, int height, int channels, uint32_t length, bool isCubemap, bool RTV, bool DSV, bool UAV, ResourceState initialState) {
	auto& device = DeviceManager::GetInstance().GetDevice();

	DXGI_FORMAT textureFormat = DetermineTextureFormat(channels, false, DSV);

	auto textureDesc = CreateTextureResourceDesc(
		textureFormat,
		width,
		height,
		length,
		1,
		isCubemap,
		RTV,
		DSV);

	D3D12_CLEAR_VALUE* clearValue = nullptr;
	D3D12_CLEAR_VALUE depthClearValue = {};
	D3D12_CLEAR_VALUE colorClearValue = {};
	if (DSV) {
		depthClearValue.Format = DXGI_FORMAT_D32_FLOAT;
		depthClearValue.DepthStencil.Depth = 1.0f;
		depthClearValue.DepthStencil.Stencil = 0;
		clearValue = &depthClearValue;
	}
	else if (RTV && channels == 1) {
		colorClearValue.Format = DXGI_FORMAT_R32_FLOAT;
		colorClearValue.Color[0] = 1.0f;
		clearValue = &colorClearValue;
	}

	auto textureResource = CreateCommittedTextureResource(
		device.Get(),
		textureDesc,
		clearValue);

	auto srvInfo = CreateShaderResourceView(
		device.Get(),
		textureResource.Get(),
		textureFormat == DXGI_FORMAT_R32_TYPELESS ? DXGI_FORMAT_R32_FLOAT : textureFormat,
		m_cbvSrvUavHeap.get(),
		isCubemap,
		true,
		length);

	std::vector<NonShaderVisibleIndexInfo> rtvInfos;
	if (RTV) {
		rtvInfos = CreateRenderTargetViews(
			device.Get(),
			textureResource.Get(),
			textureFormat,
			m_rtvHeap.get(),
			isCubemap,
			true,
			length);
	}

	std::vector<NonShaderVisibleIndexInfo> dsvInfos;
	if (DSV) {
		dsvInfos = CreateDepthStencilViews(
			device.Get(),
			textureResource.Get(),
			m_dsvHeap.get(),
			isCubemap,
			true,
			length);
	}

	TextureHandle<PixelBuffer> handle;
	handle.texture = textureResource;
	handle.SRVInfo = srvInfo;
	handle.RTVInfo = rtvInfos;
	handle.DSVInfo = dsvInfos;

	return handle;
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

void ResourceManager::UpdateGPUBuffers() {
	if (buffersToUpdate.size() == 0) {
		return;
	}

	// Reset the command allocator
	HRESULT hr = copyCommandAllocator->Reset();
	if (FAILED(hr)) {
		spdlog::error("Failed to reset command allocator");
	}

	hr = copyCommandList->Reset(copyCommandAllocator.Get(), nullptr);
	if (FAILED(hr)) {
		spdlog::error("Failed to reset command list");
	}
	for (BufferHandle& bufferHandle : buffersToUpdate) {
		// Ensure both buffers are valid
		if (bufferHandle.uploadBuffer && bufferHandle.dataBuffer) {
			auto startState = ResourceStateToD3D12(bufferHandle.dataBuffer->GetState());
			D3D12_RESOURCE_BARRIER barrier = {};
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			barrier.Transition.pResource = bufferHandle.dataBuffer->m_buffer.Get();
			barrier.Transition.StateBefore = startState;
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
			barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

			// Transition the data buffer to a state suitable for copying into it
			copyCommandList->ResourceBarrier(1, &barrier);

			// Perform the copy
			copyCommandList->CopyResource(bufferHandle.dataBuffer->m_buffer.Get(), bufferHandle.uploadBuffer->m_buffer.Get());

			// Transition back to the original state
			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
			barrier.Transition.StateAfter = startState;
			copyCommandList->ResourceBarrier(1, &barrier);
		}
	}
	for (std::shared_ptr<DynamicBufferBase> dynamicBufferHandle : dynamicBuffersToUpdate) {
		// Ensure both buffers are valid
		if (dynamicBufferHandle->m_uploadBuffer && dynamicBufferHandle->m_dataBuffer) {
			auto startState = ResourceStateToD3D12(dynamicBufferHandle->m_dataBuffer->GetState());
			D3D12_RESOURCE_BARRIER barrier = {};
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			barrier.Transition.pResource = dynamicBufferHandle->m_dataBuffer->m_buffer.Get();
			barrier.Transition.StateBefore = startState;
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
			barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

			// Transition the data buffer to a state suitable for copying into it
			copyCommandList->ResourceBarrier(1, &barrier);

			// Perform the copy
			copyCommandList->CopyResource(dynamicBufferHandle->m_dataBuffer->m_buffer.Get(), dynamicBufferHandle->m_uploadBuffer->m_buffer.Get());

			// Transition back to the original state
			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
			barrier.Transition.StateAfter = startState;
			copyCommandList->ResourceBarrier(1, &barrier);
		}
	}
	hr = copyCommandList->Close();
	if (FAILED(hr)) {
		spdlog::error("Failed to close command list");
	}
	// Execute the copy command list
	ID3D12CommandList* ppCommandLists[] = { copyCommandList.Get() };
	copyCommandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);
	WaitForCopyQueue();
	buffersToUpdate.clear();
	dynamicBuffersToUpdate.clear();
}

BufferHandle ResourceManager::CreateBuffer(size_t bufferSize, ResourceState usageType, void* pInitialData) {
	auto& device = DeviceManager::GetInstance().GetDevice();
	BufferHandle handle;
	handle.uploadBuffer = Buffer::CreateShared(device.Get(), ResourceCPUAccessType::WRITE, bufferSize, true);
	handle.dataBuffer = Buffer::CreateShared(device.Get(), ResourceCPUAccessType::NONE, bufferSize, false);
	if (pInitialData) {
		UpdateBuffer(handle, pInitialData, bufferSize);
	}

	QueueResourceTransition({ handle.dataBuffer.get(), ResourceState::UNKNOWN,  usageType });
	return handle;
}

void ResourceManager::UpdateBuffer(BufferHandle& bufferHandle, void* pData, size_t size) {
	if (bufferHandle.uploadBuffer && bufferHandle.dataBuffer) {
		void* mappedData;
		D3D12_RANGE readRange(0, 0); // We do not intend to read from this resource on the CPU.
		bufferHandle.uploadBuffer->m_buffer->Map(0, &readRange, &mappedData);
		memcpy(mappedData, pData, size);
		bufferHandle.uploadBuffer->m_buffer->Unmap(0, nullptr);
		buffersToUpdate.push_back(bufferHandle);
	}
}

void ResourceManager::QueueResourceTransition(const ResourceTransition& transition) {
	queuedResourceTransitions.push_back(transition);
}

void ResourceManager::ExecuteResourceTransitions() {
	auto& device = DeviceManager::GetInstance().GetDevice();
	auto& commandList = transitionCommandList;
	auto& commandAllocator = transitionCommandAllocator;
	if (queuedResourceTransitions.size() == 0) {
		return;
	}

	// Reset the command allocator
	//HRESULT hr = commandAllocator->Reset();
	//if (FAILED(hr)) {
	//    spdlog::error("Failed to reset command allocator");
	//}

	auto hr = commandList->Reset(commandAllocator.Get(), nullptr);
	if (FAILED(hr)) {
		spdlog::error("Failed to reset command list");
	}
	RenderContext context;
	context.commandList = transitionCommandList.Get();
	for (auto& transition : queuedResourceTransitions) {
		if (transition.resource == nullptr) {
			spdlog::error("Resource is null in transition");
			throw std::runtime_error("Resource is null");
		}
		transition.resource->Transition(context, transition.beforeState, transition.afterState);
		transition.resource->SetState(transition.afterState);
	}

	hr = commandList->Close();
	if (FAILED(hr)) {
		spdlog::error("Failed to close command list");
	}

	ID3D12CommandList* ppCommandLists[] = { transitionCommandList.Get() };
	transitionCommandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

	queuedResourceTransitions.clear();
}