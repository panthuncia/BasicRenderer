#pragma once

#include <memory>
#include <directx/d3d12.h>
#include <wrl/client.h>

#include "DirectX/d3dx12.h"
#include "Resources/HeapIndexInfo.h"

class DescriptorHeap;

template<typename T>
struct TextureHandle {
    Microsoft::WRL::ComPtr<ID3D12Resource> texture; // Texture resource
    ShaderVisibleIndexInfo SRVInfo;
	ShaderVisibleIndexInfo UAVInfo;
	NonShaderVisibleIndexInfo NSVUAVInfo;
    std::vector<NonShaderVisibleIndexInfo> RTVInfo;
    std::vector<NonShaderVisibleIndexInfo> DSVInfo;
    std::shared_ptr<DescriptorHeap> srvUavHeap;
    std::shared_ptr<DescriptorHeap> uavCPUHeap;
    std::shared_ptr<DescriptorHeap> rtvHeap;
    std::shared_ptr<DescriptorHeap> dsvHeap;
};