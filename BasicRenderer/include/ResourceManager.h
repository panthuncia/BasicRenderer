#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <vector>
#include <stdexcept>
#include "DirectX/d3dx12.h"
#include "Buffers.h"
#include "FrameResource.h"
#include "spdlog/spdlog.h"
using namespace Microsoft::WRL;

template<typename T>
struct BufferHandle {
    UINT index; // Index in the descriptor heap
    ComPtr<ID3D12Resource> buffer; // The actual resource buffer
};


class ResourceManager {
public:
    static ResourceManager& GetInstance() {
        static ResourceManager instance;
        return instance;
    }

    void Initialize();

    CD3DX12_CPU_DESCRIPTOR_HANDLE GetCPUHandle();
    CD3DX12_GPU_DESCRIPTOR_HANDLE GetGPUHandle();
    ComPtr<ID3D12DescriptorHeap> GetDescriptorHeap();
    ComPtr<ID3D12DescriptorHeap> GetSamplerDescriptorHeap();
    UINT AllocateDescriptor();
    void UpdateConstantBuffers(DirectX::XMFLOAT3 eyeWorld, DirectX::XMMATRIX viewMatrix, DirectX::XMMATRIX projectionMatrix);

    template<typename T>
    BufferHandle<T> CreateIndexedConstantBuffer() {
        static_assert(std::is_standard_layout<T>::value, "T must be a standard layout type for constant buffers.");

        auto device = DeviceManager::getInstance().getDevice();

        // Calculate the size of the buffer to be 256-byte aligned
        UINT bufferSize = (sizeof(T) + 255) & ~255;

        // Create the buffer
        D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        D3D12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);
        ComPtr<ID3D12Resource> buffer;
        auto hr = device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&buffer));

        if (FAILED(hr)) {
            spdlog::error("HRESULT failed with error code: {}", hr);
            throw std::runtime_error("HRESULT failed");
        }

        // Create a descriptor for the buffer
        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
        cbvDesc.BufferLocation = buffer->GetGPUVirtualAddress();
        cbvDesc.SizeInBytes = bufferSize;

        UINT index = AllocateDescriptor();
        D3D12_CPU_DESCRIPTOR_HANDLE handle = GetCPUHandle();

        device->CreateConstantBufferView(&cbvDesc, handle);

        return { index, buffer };
    }

    template<typename T>
    void UpdateIndexedConstantBuffer(BufferHandle<T>& handle, const T& data) {
        void* mappedData;
        D3D12_RANGE readRange(0, 0); // We do not intend to read from this resource on the CPU.
        handle.buffer->Map(0, &readRange, &mappedData);
        memcpy(mappedData, &data, sizeof(T));
        handle.buffer->Unmap(0, nullptr);
    }

    template<typename T>
    BufferHandle<T> CreateIndexedStructuredBuffer(UINT numElements) {
        auto device = DeviceManager::getInstance().getDevice();
        UINT elementSize = sizeof(T);
        UINT bufferSize = numElements * elementSize;

        D3D12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);
        D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD); // TODO: Make a real upload system

        ComPtr<ID3D12Resource> buffer;
        auto hr = device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(&buffer));

        if (FAILED(hr)) {
            spdlog::error("HRESULT failed with error code: {}", hr);
            throw std::runtime_error("HRESULT failed");
        }

        UINT index = AllocateDescriptor();
        BufferHandle<DirectX::XMMATRIX> handle = { index, buffer };

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.NumElements = numElements;
        srvDesc.Buffer.StructureByteStride = sizeof(T);
        srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

        D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = GetCPUHandle();
        device->CreateShaderResourceView(handle.buffer.Get(), &srvDesc, srvHandle);

        return handle;
    }

    template<typename T>
    void UpdateStructuredBuffer(BufferHandle<T>& handle, T* data, UINT startIndex, UINT numElements) {
        if (handle.buffer == nullptr) {
            spdlog::error("Buffer not initialized.");
            throw std::runtime_error("Buffer not initialized.");
        }

        // Calculate the size of the data to update
        UINT elementSize = sizeof(T);
        UINT updateSize = numElements * elementSize;
        UINT offset = startIndex * elementSize;

        // Map the buffer
        void* mappedData = nullptr;
        D3D12_RANGE readRange = { offset, offset + updateSize }; // We specify the range we might read, which is none in this case
        HRESULT hr = handle.buffer->Map(0, &readRange, &mappedData);
        if (FAILED(hr)) {
            spdlog::error("Failed to map buffer with HRESULT: {}", hr);
            throw std::runtime_error("Failed to map buffer.");
        }

        // Copy new data to the buffer
        memcpy(static_cast<char*>(mappedData) + offset, data, updateSize);

        // Unmap the buffer
        D3D12_RANGE writtenRange = { offset, offset + updateSize };
        handle.buffer->Unmap(0, &writtenRange);
    }

    std::unique_ptr<FrameResource>& GetFrameResource(UINT frameNum);

    UINT CreateIndexedSampler(const D3D12_SAMPLER_DESC& samplerDesc);
    D3D12_CPU_DESCRIPTOR_HANDLE getCPUHandleForSampler(UINT index) const;

    std::unique_ptr<FrameResource> currentFrameResource;

private:
    ResourceManager() : descriptorSize(0), numAllocatedDescriptors(0) {}
    void InitializeUploadHeap();
    void WaitForCopyQueue();
    void InitializeCopyCommandQueue();
    
    std::unique_ptr<FrameResource> frameResourceCopies[3];

    ComPtr<ID3D12DescriptorHeap> descriptorHeap;
    UINT descriptorSize;
    UINT numAllocatedDescriptors;

    ComPtr<ID3D12DescriptorHeap> samplerHeap;
    UINT samplerDescriptorSize;
    UINT numAllocatedSamplerDescriptors;

    std::vector<LightInfo> lightsData;
    ComPtr<ID3D12Resource> lightBuffer;

    Microsoft::WRL::ComPtr<ID3D12Resource> uploadHeap;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> copyCommandQueue;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> copyCommandAllocator;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> copyCommandList;
    Microsoft::WRL::ComPtr<ID3D12Fence> copyFence;
    HANDLE copyFenceEvent;
    UINT64 copyFenceValue = 0;

    ComPtr<ID3D12Resource> perFrameConstantBuffer;
    UINT8* pPerFrameConstantBuffer;
    PerFrameCB perFrameCBData;
    UINT currentFrameIndex;
};