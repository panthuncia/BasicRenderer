#include "ResourceManager.h"
#include "Utilities.h"
#include "DirectX/d3dx12.h"
#include "DeviceManager.h"
#include "DynamicStructuredBuffer.h"
#include "SettingsManager.h"
#include "DynamicBuffer.h"
#include "SortedUnsignedIntBuffer.h"
void ResourceManager::Initialize(ID3D12CommandQueue* commandQueue) {
	//for (int i = 0; i < 3; i++) {
	//    frameResourceCopies[i] = std::make_unique<FrameResource>();
	//    frameResourceCopies[i]->Initialize();
	//}

	auto& device = DeviceManager::GetInstance().GetDevice();
	m_cbvSrvUavHeap = std::make_shared<DescriptorHeap>(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, D3D12_MAX_SHADER_VISIBLE_DESCRIPTOR_HEAP_SIZE_TIER_1, true);
	m_samplerHeap = std::make_shared<DescriptorHeap>(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 2048, true);
	m_rtvHeap = std::make_shared<DescriptorHeap>(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 10000, false);
	m_dsvHeap = std::make_shared<DescriptorHeap>(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 10000, false);

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

	//InitializeUploadHeap();
	InitializeCopyCommandQueue();
	InitializeTransitionCommandList();
	SetTransitionCommandQueue(commandQueue);
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

void ResourceManager::UpdateGPUBuffers() {

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
	for (DynamicBufferBase* dynamicBufferHandle : dynamicBuffersToUpdate) {
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

	for (ViewedDynamicBufferBase* buffer : dynamicBuffersToUpdateViews) {

		const auto& bufferViewsToUpdate = buffer->GetDirtyViews();
		if (bufferViewsToUpdate.empty()) {
			continue;
		}

		auto startState = ResourceStateToD3D12(buffer->m_dataBuffer->GetState());
		D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			buffer->m_dataBuffer->m_buffer.Get(),
			startState,
			D3D12_RESOURCE_STATE_COPY_DEST
		);
		copyCommandList->ResourceBarrier(1, &barrier);

		for (const auto& bufferView : bufferViewsToUpdate) {
			copyCommandList->CopyBufferRegion(
				buffer->m_dataBuffer->m_buffer.Get(),
				bufferView->GetOffset(),
				buffer->m_uploadBuffer->m_buffer.Get(),
				bufferView->GetOffset(),
				bufferView->GetSize()
			);
		}
		barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			buffer->m_dataBuffer->m_buffer.Get(),
			D3D12_RESOURCE_STATE_COPY_DEST,
			startState
		);
		copyCommandList->ResourceBarrier(1, &barrier);

		buffer->ClearDirtyViews();
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

BufferHandle ResourceManager::CreateBuffer(size_t bufferSize, ResourceState usageType, void* pInitialData, bool UAV) {
	auto& device = DeviceManager::GetInstance().GetDevice();
	BufferHandle handle;
	handle.uploadBuffer = Buffer::CreateShared(device.Get(), ResourceCPUAccessType::WRITE, bufferSize, true, false);
	handle.dataBuffer = Buffer::CreateShared(device.Get(), ResourceCPUAccessType::NONE, bufferSize, false, UAV);
	if (pInitialData) {
		UpdateBuffer(handle, pInitialData, bufferSize);
	}

	QueueResourceTransition({ handle.dataBuffer.get(), ResourceState::UNKNOWN,  usageType });
	return handle;
}

void ResourceManager::UpdateBuffer(BufferHandle& bufferHandle, void* pData, size_t size) {
	if (bufferHandle.uploadBuffer && bufferHandle.dataBuffer) {
		void* mappedData;
		D3D12_RANGE readRange(0, 0);
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

	auto hr = commandList->Reset(commandAllocator.Get(), nullptr);
	if (FAILED(hr)) {
		spdlog::error("Failed to reset command list");
	}
	for (auto& transition : queuedResourceTransitions) {
		if (transition.resource == nullptr) {
			spdlog::error("Resource is null in transition");
			throw std::runtime_error("Resource is null");
		}
		transition.resource->Transition(transitionCommandList.Get(), transition.beforeState, transition.afterState);
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

std::shared_ptr<DynamicBuffer> ResourceManager::CreateIndexedDynamicBuffer(size_t elementSize, size_t numElements, ResourceState usage, std::wstring name, bool byteAddress) {
#if defined(_DEBUG)
	assert(numElements > 0 && byteAddress ? elementSize == 1 : (elementSize > 0 && elementSize % 4 == 0));
	assert(byteAddress ? numElements % 4 == 0 : true);
#endif
	auto& device = DeviceManager::GetInstance().GetDevice();

	size_t bufferSize = elementSize * numElements;
	bufferSize += bufferSize % 4; // Align to 4 bytes
	// Create the dynamic structured buffer instance
	UINT bufferID = GetNextResizableBufferID();
	std::shared_ptr<DynamicBuffer> pDynamicBuffer = DynamicBuffer::CreateShared(byteAddress, elementSize, bufferID, bufferSize, name);
	ResourceTransition transition;
	transition.resource = pDynamicBuffer.get();
	transition.beforeState = ResourceState::UNKNOWN;
	transition.afterState = usage;
	QueueResourceTransition(transition);
	pDynamicBuffer->SetOnResized([this](UINT bufferID, size_t typeSize, size_t capacity, bool byteAddress, std::shared_ptr<Buffer>& buffer) {
		this->onDynamicBufferResized(bufferID, typeSize, capacity, byteAddress, buffer);
		});

	// Create an SRV for the buffer
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = byteAddress ? DXGI_FORMAT_R32_TYPELESS : DXGI_FORMAT_UNKNOWN;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	srvDesc.Buffer.FirstElement = 0;
	srvDesc.Buffer.NumElements = byteAddress? numElements / 4 : numElements;
	srvDesc.Buffer.StructureByteStride = byteAddress ? 0 : elementSize;
	srvDesc.Buffer.Flags = byteAddress ? D3D12_BUFFER_SRV_FLAG_RAW : D3D12_BUFFER_SRV_FLAG_NONE;

	UINT index = m_cbvSrvUavHeap->AllocateDescriptor();
	bufferIDDescriptorIndexMap[bufferID] = index;
	CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandle = m_cbvSrvUavHeap->GetCPUHandle(index);
	device->CreateShaderResourceView(pDynamicBuffer->GetBuffer()->m_buffer.Get(), &srvDesc, cpuHandle);

	CD3DX12_GPU_DESCRIPTOR_HANDLE gpuHandle = m_cbvSrvUavHeap->GetGPUHandle(index);
	ShaderVisibleIndexInfo srvInfo;
	srvInfo.index = index;
	srvInfo.gpuHandle = gpuHandle;
	srvInfo.cpuHandle = cpuHandle;

	pDynamicBuffer->SetSRVDescriptor(m_cbvSrvUavHeap, srvInfo);

	return pDynamicBuffer;
}

std::shared_ptr<SortedUnsignedIntBuffer> ResourceManager::CreateIndexedSortedUnsignedIntBuffer(ResourceState usage, UINT capacity, std::wstring name) {
	auto& device = DeviceManager::GetInstance().GetDevice();

	UINT bufferID = GetNextResizableBufferID();
	std::shared_ptr<SortedUnsignedIntBuffer> pBuffer = SortedUnsignedIntBuffer::CreateShared(bufferID, capacity, name);
	ResourceTransition transition;
	transition.resource = pBuffer.get();
	transition.beforeState = ResourceState::UNKNOWN;
	transition.afterState = usage;
	QueueResourceTransition(transition);
	pBuffer->SetOnResized([this](UINT bufferID, UINT capacity, UINT numElements, std::shared_ptr<Buffer>& buffer) {
		this->onDynamicStructuredBufferResized(bufferID, capacity, numElements, buffer);
		});

	// Create an SRV for the buffer
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	srvDesc.Buffer.FirstElement = 0;
	srvDesc.Buffer.NumElements = capacity;
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
	srvInfo.cpuHandle = cpuHandle;

	pBuffer->SetSRVDescriptor(m_cbvSrvUavHeap, srvInfo);

	return pBuffer;
}