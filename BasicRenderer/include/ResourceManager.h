#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <vector>
#include <stdexcept>
#include "DirectX/d3dx12.h"
#include "Buffers.h"

using namespace Microsoft::WRL;

class ResourceManager {
public:
    static ResourceManager& getInstance() {
        static ResourceManager instance;
        return instance;
    }

    void initialize();

    CD3DX12_CPU_DESCRIPTOR_HANDLE getCPUHandle();
    CD3DX12_GPU_DESCRIPTOR_HANDLE getGPUHandle();
    ComPtr<ID3D12DescriptorHeap> getDescriptorHeap();
    UINT allocateDescriptor();
    void UpdateConstantBuffers();

private:
    ResourceManager() : descriptorSize(0), numAllocatedDescriptors(0) {}

    ComPtr<ID3D12DescriptorHeap> descriptorHeap;
    UINT descriptorSize;
    UINT numAllocatedDescriptors;

    std::vector<LightInfo> lightsData;
    ComPtr<ID3D12Resource> lightBuffer;

    ComPtr<ID3D12Resource> perFrameConstantBuffer;
    UINT8* pPerFrameConstantBuffer;
    PerFrameCB perFrameCBData;
};