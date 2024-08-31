#pragma once

#include <windows.h>
#include <iostream>
#include <wrl.h>
#include <d3dcompiler.h>

#include "RenderableObject.h"
#include "GlTFLoader.h"

void ThrowIfFailed(HRESULT hr);

template<typename... Args>
void print(Args... args) {
    (std::cout << ... << args) << std::endl;
}

Microsoft::WRL::ComPtr<ID3DBlob> CompileShader(const std::wstring& filename, const std::string& entryPoint, const std::string& target);

std::shared_ptr<RenderableObject> RenderableFromData(MeshData meshData, std::string name);

template<typename T>
ComPtr<ID3D12Resource> CreateConstantBuffer(T* pInitialData) {
    static_assert(std::is_standard_layout<T>::value, "T must be a standard layout type for constant buffers.");

    auto& device = DeviceManager::GetInstance().GetDevice();

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

    if (pInitialData != nullptr) {
        void* mappedData;
        D3D12_RANGE readRange(0, 0); // We do not intend to read from this resource on the CPU.
        buffer->Map(0, &readRange, &mappedData);
        memcpy(mappedData, pInitialData, sizeof(T));
        buffer->Unmap(0, nullptr);
    }

    return buffer;
}

XMMATRIX RemoveScalingFromMatrix(XMMATRIX& initialMatrix);