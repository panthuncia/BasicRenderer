#include "Render/DescriptorHeap.h"

#include <spdlog/spdlog.h>

#include "Utilities/Utilities.h"

DescriptorHeap::DescriptorHeap(rhi::Device& device, rhi::DescriptorHeapType type, uint32_t numDescriptors, bool shaderVisible, std::string name)
    : m_type(type), m_shaderVisible(shaderVisible), m_numDescriptorsAllocated(0) {

	rhi::DescriptorHeapDesc heapDesc = {.type = type, .capacity = numDescriptors, .shaderVisible = shaderVisible, .debugName = name.c_str()};
    m_heap = device.CreateDescriptorHeap(heapDesc);

    m_descriptorSize = device.GetDescriptorHandleIncrementSize(type);
}

DescriptorHeap::~DescriptorHeap() {
}

CD3DX12_CPU_DESCRIPTOR_HANDLE DescriptorHeap::GetCPUHandle(UINT index) {
    return CD3DX12_CPU_DESCRIPTOR_HANDLE(m_heap->GetCPUDescriptorHandleForHeapStart(), index, m_descriptorSize);
}

CD3DX12_GPU_DESCRIPTOR_HANDLE DescriptorHeap::GetGPUHandle(UINT index) {
    assert(m_shaderVisible && "GPU handles requested from a non-shader visible heap!");
    return CD3DX12_GPU_DESCRIPTOR_HANDLE(m_heap->GetGPUDescriptorHandleForHeapStart(), index, m_descriptorSize);
}

rhi::DescriptorHeapHandle DescriptorHeap::GetHeap() const {
    return m_heap;
}

UINT DescriptorHeap::AllocateDescriptor() {
    if (!m_freeIndices.empty()) {
        UINT freeIndex = m_freeIndices.front();
        m_freeIndices.pop();
        return freeIndex;
    }
    else if (m_numDescriptorsAllocated < m_heap->GetDesc().NumDescriptors) {
        return m_numDescriptorsAllocated++;
    }
    throw std::runtime_error("Out of descriptor heap space!");
}

void DescriptorHeap::ReleaseDescriptor(UINT index) {
#if BUILD_TYPE == BUILD_TYPE_DEBUG
    if (index == 0) {
		spdlog::error("DescriptorHeap::ReleaseDescriptor: Attempting to release descriptor 0");
    }
    int32_t signedValue = static_cast<int32_t>(index);
	assert(signedValue >= 0 && signedValue < static_cast<int32_t>(m_heap->GetDesc().NumDescriptors)); // If this trggers, a descriptor is likely set but uninitialized
#endif
    m_freeIndices.push(index);
}