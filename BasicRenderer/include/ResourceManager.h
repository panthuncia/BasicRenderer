#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <vector>
#include <stdexcept>
#include "DirectX/d3dx12.h"
#include "Buffers.h"
#include "FrameResource.h"
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
    std::unique_ptr<FrameResource>& GetFrameResource(UINT frameNum);

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