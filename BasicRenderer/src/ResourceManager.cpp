#include "ResourceManager.h"
#include "Utilities.h"
#include "DirectX/d3dx12.h"
#include "DeviceManager.h"
#include "DynamicStructuredBuffer.h"

void ResourceManager::Initialize() {
    //for (int i = 0; i < 3; i++) {
    //    frameResourceCopies[i] = std::make_unique<FrameResource>();
    //    frameResourceCopies[i]->Initialize();
    //}

    auto& device = DeviceManager::GetInstance().GetDevice();
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.NumDescriptors = D3D12_MAX_SHADER_VISIBLE_DESCRIPTOR_HEAP_SIZE_TIER_1;// numDescriptors;
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&descriptorHeap)));

    descriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    UINT perFrameBufferSize = (sizeof(PerFrameCB) + 255) & ~255; // CBV size is required to be 256-byte aligned.
    D3D12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(perFrameBufferSize);

    D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    ThrowIfFailed(device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&perFrameConstantBuffer)));

    perFrameCBData.ambientLighting = XMVectorSet(0.1, 0.1, 0.1, 1.0);

    // Map the constant buffer and initialize it

    InitializeUploadHeap();
    InitializeCopyCommandQueue();
	InitializeTransitionCommandQueue();

    // Create CBV
    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
    cbvDesc.BufferLocation = perFrameConstantBuffer->GetGPUVirtualAddress();
    cbvDesc.SizeInBytes = perFrameBufferSize; // CBV size is required to be 256-byte aligned.
    device->CreateConstantBufferView(&cbvDesc, descriptorHeap->GetCPUDescriptorHandleForHeapStart());

    numAllocatedDescriptors++;

    // Initialize Sampler Descriptor Heap
    D3D12_DESCRIPTOR_HEAP_DESC samplerHeapDesc = {};
    samplerHeapDesc.NumDescriptors = 2048;
    samplerHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    samplerHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE; // Must be visible to shaders
    ThrowIfFailed(device->CreateDescriptorHeap(&samplerHeapDesc, IID_PPV_ARGS(&samplerHeap)));

    samplerDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    numAllocatedSamplerDescriptors = 0;

}

CD3DX12_CPU_DESCRIPTOR_HANDLE ResourceManager::GetCPUHandle(UINT index) {
    return CD3DX12_CPU_DESCRIPTOR_HANDLE(descriptorHeap->GetCPUDescriptorHandleForHeapStart(), index, descriptorSize);
}

CD3DX12_GPU_DESCRIPTOR_HANDLE ResourceManager::GetGPUHandle(UINT index) {
    return CD3DX12_GPU_DESCRIPTOR_HANDLE(descriptorHeap->GetGPUDescriptorHandleForHeapStart(), index, descriptorSize);
}

ComPtr<ID3D12DescriptorHeap> ResourceManager::GetDescriptorHeap() {
    return descriptorHeap;
}

ComPtr<ID3D12DescriptorHeap> ResourceManager::GetSamplerDescriptorHeap() {
    return samplerHeap;
}

UINT ResourceManager::AllocateDescriptor() {
    if (!freeDescriptors.empty()) {
        UINT freeIndex = freeDescriptors.front();
        freeDescriptors.pop();
        return freeIndex;
    }
    else {
        if (numAllocatedDescriptors >= descriptorHeap->GetDesc().NumDescriptors) {
            throw std::runtime_error("Out of descriptor heap space!");
        }
        return numAllocatedDescriptors++;
    }
}

void ResourceManager::ReleaseDescriptor(UINT index) {
    freeDescriptors.push(index);
}


void ResourceManager::UpdateConstantBuffers(DirectX::XMFLOAT3 eyeWorld, DirectX::XMMATRIX viewMatrix, DirectX::XMMATRIX projectionMatrix, UINT numLights, UINT lightBufferIndex) {
    //DirectX::XMFLOAT4 eyeWorld = { 0.0f, 2.0f, -5.0f, 1.0f };
    //perFrameCBData.view = DirectX::XMMatrixLookAtLH(
    //    DirectX::XMLoadFloat4(&eyeWorld), // Eye position
    //    DirectX::XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f),  // Focus point
    //    DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f)   // Up direction
    //);
    perFrameCBData.viewMatrix = viewMatrix;
    //perFrameCBData.projection = DirectX::XMMatrixPerspectiveFovLH(
    //    DirectX::XM_PIDIV2, // Field of View
    //    800.0f / 600.0f,    // Aspect ratio
    //    0.1f,               // Near clipping plane
    //    100.0f              // Far clipping plane
    //);
    perFrameCBData.projectionMatrix = projectionMatrix;
    perFrameCBData.eyePosWorldSpace = DirectX::XMLoadFloat3(&eyeWorld);
    perFrameCBData.numLights = numLights;
    perFrameCBData.lightBufferIndex = lightBufferIndex;
    // Map the upload heap and copy new data to it
    void* pUploadData;
    D3D12_RANGE readRange(0, 0);
    ThrowIfFailed(uploadHeap->Map(0, &readRange, &pUploadData));
    memcpy(pUploadData, &perFrameCBData, sizeof(perFrameCBData));
    uploadHeap->Unmap(0, nullptr);

    // Reset and record the copy command list
    ThrowIfFailed(copyCommandAllocator->Reset());
    ThrowIfFailed(copyCommandList->Reset(copyCommandAllocator.Get(), nullptr));
    copyCommandList->CopyBufferRegion(perFrameConstantBuffer.Get(), 0, uploadHeap.Get(), 0, sizeof(perFrameCBData));
    ThrowIfFailed(copyCommandList->Close());

    // Execute the copy command list
    ID3D12CommandList* ppCommandLists[] = { copyCommandList.Get() };
    copyCommandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

    // Wait for the copy queue to finish
    WaitForCopyQueue();
}

void ResourceManager::InitializeUploadHeap() {
    auto& device = DeviceManager::GetInstance().GetDevice();
    D3D12_HEAP_PROPERTIES uploadHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    D3D12_RESOURCE_DESC uploadBufferDesc = CD3DX12_RESOURCE_DESC::Buffer((sizeof(PerFrameCB) + 255) & ~255);
    ThrowIfFailed(device->CreateCommittedResource(
        &uploadHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &uploadBufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&uploadHeap)));
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

std::unique_ptr<FrameResource>& ResourceManager::GetFrameResource(UINT frameNum) {
    return frameResourceCopies[frameNum % 3];
}

UINT ResourceManager::CreateIndexedSampler(const D3D12_SAMPLER_DESC& samplerDesc) {
    auto& device = DeviceManager::GetInstance().GetDevice();

    if (numAllocatedSamplerDescriptors >= samplerHeap->GetDesc().NumDescriptors) {
        throw std::runtime_error("Exceeded the maximum number of samplers that can be allocated in the sampler heap.");
    }

    UINT index = numAllocatedSamplerDescriptors++;
    D3D12_CPU_DESCRIPTOR_HANDLE handle = samplerHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += index * samplerDescriptorSize;

    device->CreateSampler(&samplerDesc, handle);

    return index;
}

D3D12_CPU_DESCRIPTOR_HANDLE ResourceManager::getCPUHandleForSampler(UINT index) const {
    auto handle = samplerHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += index * samplerDescriptorSize;
    return handle;
}

TextureHandle<PixelBuffer> ResourceManager::CreateTexture(const stbi_uc* image, int width, int height, int channels, bool sRGB) {
    auto& device = DeviceManager::GetInstance().GetDevice();

    // Describe and create the texture resource

    DXGI_FORMAT textureFormat;
    std::vector<stbi_uc> expandedImage;
    switch (channels) {
    case 1:
        textureFormat = DXGI_FORMAT_R8_UNORM;
        break;
    case 3:
        textureFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        expandedImage.resize(width * height * 4); // 4 channels for RGBA
        for (int i = 0; i < width * height; ++i) {
            expandedImage[i * 4] = image[i * 3];     // R
            expandedImage[i * 4 + 1] = image[i * 3 + 1]; // G
            expandedImage[i * 4 + 2] = image[i * 3 + 2]; // B
            expandedImage[i * 4 + 3] = 255;          // A
        }
        image = expandedImage.data(); // Use expanded data
        channels = 4; // Update the number of channels
        break;
    case 4:
        textureFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        break;
    default:
        throw std::invalid_argument("Unsupported channel count");
    }

    CD3DX12_RESOURCE_DESC textureDesc = CD3DX12_RESOURCE_DESC::Tex2D(textureFormat, width, height);
    D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    ComPtr<ID3D12Resource> textureResource;
    ThrowIfFailed(device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &textureDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&textureResource)));

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

    // Allocate descriptor and create shader resource view
    UINT descriptorIndex = AllocateDescriptor();
    CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandle = GetCPUHandle(descriptorIndex);
    CD3DX12_GPU_DESCRIPTOR_HANDLE gpuHandle = GetGPUHandle(descriptorIndex);

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = textureFormat;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

    device->CreateShaderResourceView(textureResource.Get(), &srvDesc, cpuHandle);

    return { descriptorIndex, textureResource, cpuHandle, gpuHandle };
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

void ResourceManager::UpdateGPUBuffers(){
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
    for (auto& bufferHandle : buffersToUpdate) {
        // Ensure both buffers are valid
        if (bufferHandle.uploadBuffer && bufferHandle.dataBuffer) {
            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            barrier.Transition.pResource = bufferHandle.dataBuffer->m_buffer.Get();
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_GENERIC_READ;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

            // Transition the data buffer to a state suitable for copying into it
            copyCommandList->ResourceBarrier(1, &barrier);

            // Perform the copy
            copyCommandList->CopyResource(bufferHandle.dataBuffer->m_buffer.Get(), bufferHandle.uploadBuffer->m_buffer.Get());

            // Transition back to the original state
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;
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
}

BufferHandle ResourceManager::CreateBuffer(size_t bufferSize, ResourceUsageType usageType, void* pInitialData) {
	auto& device = DeviceManager::GetInstance().GetDevice();
	BufferHandle handle;
	handle.uploadBuffer = std::make_shared<Buffer>(device.Get(), ResourceCPUAccessType::WRITE, bufferSize, ResourceUsageType::UPLOAD);
	handle.dataBuffer = std::make_shared<Buffer>(device.Get(), ResourceCPUAccessType::NONE, bufferSize, usageType);
	if (pInitialData) {
		UpdateBuffer(handle, pInitialData, bufferSize);
	}
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
    switch (usageType) {
        case ResourceUsageType::INDEX:
            state = D3D12_RESOURCE_STATE_INDEX_BUFFER;
            break;
        case ResourceUsageType::VERTEX:
            state = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
            break;
    }
	QueueResourceTransition({ handle.dataBuffer->m_buffer.Get(), D3D12_RESOURCE_STATE_COMMON,  state});
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
    HRESULT hr = commandAllocator->Reset();
    if (FAILED(hr)) {
        spdlog::error("Failed to reset command allocator");
    }

    hr = commandList->Reset(commandAllocator.Get(), nullptr);
    if (FAILED(hr)) {
        spdlog::error("Failed to reset command list");
    }

	for (auto& transition : queuedResourceTransitions) {
		D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(transition.resource, transition.beforeState, transition.afterState);
		commandList->ResourceBarrier(1, &barrier);
	}

    hr = commandList->Close();
    if (FAILED(hr)) {
        spdlog::error("Failed to close command list");
    }

    ID3D12CommandList* ppCommandLists[] = { transitionCommandList.Get() };
    transitionCommandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);
    WaitForTransitionQueue();

	queuedResourceTransitions.clear();
}