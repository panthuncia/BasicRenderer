#include "FrameResource.h"

#include "DeviceManager.h"
#include "Utilities.h"

FrameResource::~FrameResource() {}

void FrameResource::Initialize() {
    auto device = DeviceManager::getInstance().getDevice();
    ThrowIfFailed(device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(CmdListAlloc.GetAddressOf())));
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.NumDescriptors = D3D12_MAX_SHADER_VISIBLE_DESCRIPTOR_HEAP_SIZE_TIER_1;// numDescriptors;
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&descriptorHeap)));

    descriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    //D3D12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(perFrameBufferSize);

    frameConstantBuffer.Initialize();

    // Map the constant buffer and initialize it

    //InitializeUploadHeap();
    //InitializeCopyCommandQueue();

    // Create CBV
    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
    cbvDesc.BufferLocation = frameConstantBuffer.constantBuffer->GetGPUVirtualAddress();
    cbvDesc.SizeInBytes = frameConstantBuffer.size; // CBV size is required to be 256-byte aligned.
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

    D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
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