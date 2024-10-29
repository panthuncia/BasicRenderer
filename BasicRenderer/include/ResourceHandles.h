#pragma once

#include <memory>
#include <directx/d3d12.h>
#include <wrl/client.h>

#include "DirectX/d3dx12.h"
#include "DynamicStructuredBuffer.h"
#include "LazyDynamicStructuredBuffer.h"
#include "BufferHandle.h"
#include "Concepts/HasIsValid.h"
#include "HeapIndexInfo.h"

class DynamicBuffer;
class DescriptorHeap;

// TODO: old, mostly not needed

template<typename T>
struct DynamicStructuredBufferHandle {
    std::shared_ptr<DynamicStructuredBuffer<T>> buffer; // The actual resource buffer
};

template<HasIsValid T>
struct LazyDynamicStructuredBufferHandle {
    std::shared_ptr<LazyDynamicStructuredBuffer<T>> buffer; // The actual resource buffer
};

struct DynamicBufferHandle {
	std::shared_ptr<DynamicBuffer> buffer;
};

template<typename T>
struct TextureHandle {
    Microsoft::WRL::ComPtr<ID3D12Resource> texture; // Texture resource
    ShaderVisibleIndexInfo SRVInfo;
    std::vector<NonShaderVisibleIndexInfo> RTVInfo;
    std::vector<NonShaderVisibleIndexInfo> DSVInfo;
    std::shared_ptr<DescriptorHeap> srvHeap;
    std::shared_ptr<DescriptorHeap> rtvHeap;
    std::shared_ptr<DescriptorHeap> dsvHeap;
};