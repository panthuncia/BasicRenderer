#include "ResourceManager.h"
#include "Utilities.h"
#include "DirectX/d3dx12.h"
#include "DeviceManager.h"

void ResourceManager::initialize() {
    for (int i = 0; i < 3; i++) {
        frameResourceCopies[i] = std::make_unique<FrameResource>();
        frameResourceCopies[i]->Initialize();
    }

    auto device = DeviceManager::getInstance().getDevice();
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

    // Map the constant buffer and initialize it

    InitializeUploadHeap();
    InitializeCopyCommandQueue();

    // Create CBV
    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
    cbvDesc.BufferLocation = perFrameConstantBuffer->GetGPUVirtualAddress();
    cbvDesc.SizeInBytes = perFrameBufferSize; // CBV size is required to be 256-byte aligned.
    device->CreateConstantBufferView(&cbvDesc, descriptorHeap->GetCPUDescriptorHandleForHeapStart());


    // Create structured buffer for lights
    LightInfo light;
    light.properties = DirectX::XMVectorSetInt(2, 0, 0, 0);
    light.posWorldSpace = DirectX::XMVectorSet(3.0, 3.0, 3.0, 1.0);
    light.dirWorldSpace = DirectX::XMVectorSet(1.0, 1.0, 1.0, 1.0);
    light.attenuation = DirectX::XMVectorSet(1.0, 0.01, 0.0032, 10.0);
    light.color = DirectX::XMVectorSet(1.0, 1.0, 1.0, 1.0);
    lightsData.push_back(light);

    D3D12_RESOURCE_DESC structuredBufferDesc = {};
    structuredBufferDesc.Alignment = 0;
    structuredBufferDesc.DepthOrArraySize = 1;
    structuredBufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    structuredBufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
    structuredBufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    structuredBufferDesc.Height = 1;
    structuredBufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    structuredBufferDesc.MipLevels = 1;
    structuredBufferDesc.SampleDesc.Count = 1;
    structuredBufferDesc.SampleDesc.Quality = 0;
    structuredBufferDesc.Width = sizeof(LightInfo) * lightsData.size(); // Size of all lights

    ThrowIfFailed(device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &structuredBufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&lightBuffer)));

    // Upload light data to the buffer
    void* mappedData;
    CD3DX12_RANGE readRange(0, 0);
    ThrowIfFailed(lightBuffer->Map(0, &readRange, &mappedData));
    memcpy(mappedData, lightsData.data(), sizeof(LightInfo) * lightsData.size());
    lightBuffer->Unmap(0, nullptr);

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Buffer.FirstElement = 0;
    srvDesc.Buffer.NumElements = static_cast<UINT>(lightsData.size());
    srvDesc.Buffer.StructureByteStride = sizeof(LightInfo);
    srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

    D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
    srvHandle.ptr += device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV); // Move to the next slot
    device->CreateShaderResourceView(lightBuffer.Get(), &srvDesc, srvHandle);
}

CD3DX12_CPU_DESCRIPTOR_HANDLE ResourceManager::getCPUHandle() {
    return CD3DX12_CPU_DESCRIPTOR_HANDLE(descriptorHeap->GetCPUDescriptorHandleForHeapStart(), numAllocatedDescriptors, descriptorSize);
}

CD3DX12_GPU_DESCRIPTOR_HANDLE ResourceManager::getGPUHandle() {
    return CD3DX12_GPU_DESCRIPTOR_HANDLE(descriptorHeap->GetGPUDescriptorHandleForHeapStart(), numAllocatedDescriptors, descriptorSize);
}

ComPtr<ID3D12DescriptorHeap> ResourceManager::getDescriptorHeap() {
    return descriptorHeap;
}

UINT ResourceManager::allocateDescriptor() {
    return numAllocatedDescriptors++;
}

void ResourceManager::UpdateConstantBuffers() {
    DirectX::XMFLOAT4 eyeWorld = { 0.0f, 2.0f, -5.0f, 1.0f };
    perFrameCBData.view = DirectX::XMMatrixLookAtLH(
        DirectX::XMLoadFloat4(&eyeWorld), // Eye position
        DirectX::XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f),  // Focus point
        DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f)   // Up direction
    );
    perFrameCBData.projection = DirectX::XMMatrixPerspectiveFovLH(
        DirectX::XM_PIDIV2, // Field of View
        800.0f / 600.0f,    // Aspect ratio
        0.1f,               // Near clipping plane
        100.0f              // Far clipping plane
    );
    perFrameCBData.eyePosWorldSpace = DirectX::XMLoadFloat4(&eyeWorld);
    
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
    auto device = DeviceManager::getInstance().getDevice();
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

void ResourceManager::InitializeCopyCommandQueue() {
    auto device = DeviceManager::getInstance().getDevice();

    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_COPY;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    ThrowIfFailed(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&copyCommandQueue)));

    ThrowIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY, IID_PPV_ARGS(&copyCommandAllocator)));
    ThrowIfFailed(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COPY, copyCommandAllocator.Get(), nullptr, IID_PPV_ARGS(&copyCommandList)));
    copyCommandList->Close();

    ThrowIfFailed(device->CreateFence(copyFenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&copyFence)));
    copyFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (copyFenceEvent == nullptr) {
        ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
    }
}

std::unique_ptr<FrameResource>& ResourceManager::GetFrameResource(UINT frameNum) {
    return frameResourceCopies[frameNum % 3];
}