#pragma once

#include <wrl/client.h>
#include <rhi.h>
#include <queue>
#include <vector>

class DescriptorHeap {
public:
    DescriptorHeap(rhi::Device device, rhi::DescriptorHeapType type, uint32_t numDescriptors, bool shaderVisible = false);
    ~DescriptorHeap();

    // Non-copyable and non-movable
    DescriptorHeap(const DescriptorHeap&) = delete;
    DescriptorHeap& operator=(const DescriptorHeap&) = delete;

    CD3DX12_CPU_DESCRIPTOR_HANDLE GetCPUHandle(UINT index);
    CD3DX12_GPU_DESCRIPTOR_HANDLE GetGPUHandle(UINT index);
    ID3D12DescriptorHeap* GetHeap() const;

    UINT AllocateDescriptor();
    void ReleaseDescriptor(UINT index);

private:
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_heap;
    UINT m_descriptorSize;
    UINT m_numDescriptorsAllocated;
    std::queue<UINT> m_freeIndices;
    rhi::DescriptorHeapType m_type;
    bool m_shaderVisible;
};