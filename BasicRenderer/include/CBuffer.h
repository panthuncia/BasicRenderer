#pragma once

#include <wrl/client.h>
#include <d3d12.h>
#include <DirectXMath.h>
#include "DeviceManager.h"
using Microsoft::WRL::ComPtr;

template <typename T>
class CBuffer {
public:
    ComPtr<ID3D12Resource> constantBuffer;
    UINT8* pConstantBuffer;
    T bufferData;
    UINT size;

    CBuffer() : pConstantBuffer(nullptr) {}

    void Initialize() {
        auto& device = DeviceManager::GetInstance().GetDevice();

        D3D12_HEAP_PROPERTIES heapProperties = {};
        heapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
        heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heapProperties.CreationNodeMask = 1;
        heapProperties.VisibleNodeMask = 1;

        size = (sizeof(T) + 255) & ~255;

        D3D12_RESOURCE_DESC resourceDesc = {};
        resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resourceDesc.Alignment = 0;
        resourceDesc.Width = size;
        resourceDesc.Height = 1;
        resourceDesc.DepthOrArraySize = 1;
        resourceDesc.MipLevels = 1;
        resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
        resourceDesc.SampleDesc.Count = 1;
        resourceDesc.SampleDesc.Quality = 0;
        resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

        HRESULT hr = device->CreateCommittedResource(
            &heapProperties,
            D3D12_HEAP_FLAG_NONE,
            &resourceDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&constantBuffer)
        );

        if (FAILED(hr)) {
            return;
        }

        // Map the constant buffer
        D3D12_RANGE readRange = {};
        hr = constantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pConstantBuffer));
        if (FAILED(hr)) {
            return;
        }

        // Initialize the buffer with default data
        memcpy(pConstantBuffer, &bufferData, sizeof(T));
    }

    void UpdateBuffer(ID3D12GraphicsCommandList* commandList) {
        memcpy(pConstantBuffer, &bufferData, sizeof(T));
    }

    ~CBuffer() {
        if (constantBuffer) {
            constantBuffer->Unmap(0, nullptr);
        }
    }
};