#pragma once

#include <memory>
#include <d3d12.h>
#include <wrl/client.h>

#include "DirectX/d3dx12.h"
#include "DynamicStructuredBuffer.h"
#include "BufferHandle.h"


template<typename T>
struct DynamicBufferHandle {
    UINT index; // Index in the descriptor heap
    DynamicStructuredBuffer<T> buffer; // The actual resource buffer
};

struct ShaderVisibleIndexInfo {
    int index = -1; // Index in the descriptor heap
    CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandle; // CPU descriptor handle
    CD3DX12_GPU_DESCRIPTOR_HANDLE gpuHandle; // GPU descriptor handle
};

struct NonShaderVisibleIndexInfo {
    int index = -1; // Index in the descriptor heap
    CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandle; // CPU descriptor handle
};

template<typename T>
struct TextureHandle {
    Microsoft::WRL::ComPtr<ID3D12Resource> texture; // Texture resource
    ShaderVisibleIndexInfo SRVInfo;
    std::vector<NonShaderVisibleIndexInfo> RTVInfo;
    std::vector<NonShaderVisibleIndexInfo> DSVInfo;
};