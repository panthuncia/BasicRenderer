#include "Texture.h"
#include <string>
#include <stdexcept>

#include "DeviceManager.h"
#include "Utilities.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"
#include "ResourceManager.h"

Texture::Texture(const stbi_uc* image, int width, int height, bool sRGB) {
    auto device = DeviceManager::getInstance().getDevice();

    // Describe and create the texture resource
    CD3DX12_RESOURCE_DESC textureDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, width, height);
    D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
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
    textureData.RowPitch = width * 4;
    textureData.SlicePitch = textureData.RowPitch * height;

    // TODO: Instead of one queue/allocator/list per texture, use an upload thread with one of each
    ComPtr<ID3D12CommandQueue> commandQueue;
    ComPtr<ID3D12CommandAllocator> commandAllocator;
    ComPtr<ID3D12GraphicsCommandList> commandList;

    // Create command queue

    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ThrowIfFailed(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue)));

    // Create command allocator
    ThrowIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator)));

    // **Create the command list**
    ThrowIfFailed(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator.Get(), nullptr, IID_PPV_ARGS(&commandList)));

    UpdateSubresources(commandList.Get(), textureResource.Get(), textureUploadHeap.Get(), 0, 0, 1, &textureData);
    CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(textureResource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    commandList->ResourceBarrier(1, &barrier);

    ThrowIfFailed(commandList->Close());
    ID3D12CommandList* ppCommandLists[] = { commandList.Get() };
    commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

    // Create a fence for synchronization
    ComPtr<ID3D12Fence> fence;
    ThrowIfFailed(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
    HANDLE fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (fenceEvent == nullptr) {
        ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
    }

    // Signal and wait for the fence
    const UINT64 fenceValue = 1;
    ThrowIfFailed(commandQueue->Signal(fence.Get(), fenceValue));
    ThrowIfFailed(fence->SetEventOnCompletion(fenceValue, fenceEvent));
    WaitForSingleObject(fenceEvent, INFINITE);
    CloseHandle(fenceEvent);

    // Allocate descriptor and create shader resource view
    ResourceManager& resourceManager = ResourceManager::GetInstance();
    descriptorIndex = resourceManager.AllocateDescriptor();
    cpuHandle = resourceManager.GetCPUHandle();
    gpuHandle = resourceManager.GetGPUHandle();

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

    device->CreateShaderResourceView(textureResource.Get(), &srvDesc, cpuHandle);
}

UINT Texture::GetDescriptorIndex() {
    return descriptorIndex;
}