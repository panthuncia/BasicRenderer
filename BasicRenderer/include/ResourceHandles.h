#pragma once

#include "d3d12.h"
#include "wrl.h"

#include "DynamicStructuredBuffer.h"

template<typename T>
struct BufferHandle {
    UINT index; // Index in the descriptor heap
    ComPtr<ID3D12Resource> buffer; // The actual resource buffer
};

template<typename T>
struct DynamicBufferHandle {
    UINT index; // Index in the descriptor heap
    DynamicStructuredBuffer<T> buffer; // The actual resource buffer
};

template<typename T>
struct TextureHandle {
    UINT index; // Index in the descriptor heap
    Microsoft::WRL::ComPtr<ID3D12Resource> texture; // Texture resource
    CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandle; // CPU descriptor handle
    CD3DX12_GPU_DESCRIPTOR_HANDLE gpuHandle; // GPU descriptor handle
};