#pragma once

#include <wrl/client.h>
#include <rhi.h>
#include <queue>
#include <vector>

class DescriptorHeap {
public:
    DescriptorHeap(rhi::Device& device, rhi::DescriptorHeapType type, uint32_t numDescriptors, bool shaderVisible = false, std::string name = "Descriptor Heap");
    ~DescriptorHeap();

    // Non-copyable and non-movable
    DescriptorHeap(const DescriptorHeap&) = delete;
    DescriptorHeap& operator=(const DescriptorHeap&) = delete;

    rhi::DescriptorHeap GetHeap();

    UINT AllocateDescriptor();
    void ReleaseDescriptor(UINT index);

private:
    rhi::DescriptorHeapPtr m_heap;
    UINT m_descriptorSize;
    UINT m_numDescriptorsAllocated;
    uint32_t m_totalSize;
    std::queue<UINT> m_freeIndices;
    rhi::DescriptorHeapType m_type;
    bool m_shaderVisible;
};