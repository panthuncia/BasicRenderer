#include "Render/DescriptorHeap.h"

#include <spdlog/spdlog.h>

#include "Utilities/Utilities.h"

DescriptorHeap::DescriptorHeap(rhi::Device& device, rhi::DescriptorHeapType type, uint32_t numDescriptors, bool shaderVisible, std::string name)
    : m_type(type), m_shaderVisible(shaderVisible), m_numDescriptorsAllocated(0) {

	rhi::DescriptorHeapDesc heapDesc = {.type = type, .capacity = numDescriptors, .shaderVisible = shaderVisible, .debugName = name.c_str()};
    m_heap = device.CreateDescriptorHeap(heapDesc);

    m_descriptorSize = device.GetDescriptorHandleIncrementSize(type);
    m_totalSize = numDescriptors;
}

DescriptorHeap::~DescriptorHeap() {
}

rhi::DescriptorHeap DescriptorHeap::GetHeap() {
    return m_heap.Get();
}

UINT DescriptorHeap::AllocateDescriptor() {
    if (!m_freeIndices.empty()) {
        UINT freeIndex = m_freeIndices.front();
        m_freeIndices.pop();
        return freeIndex;
    }
    else if (m_numDescriptorsAllocated < m_totalSize) {
        return m_numDescriptorsAllocated++;
    }
    throw std::runtime_error("Out of descriptor heap space!");
}

void DescriptorHeap::ReleaseDescriptor(UINT index) {
//#if BUILD_TYPE == BUILD_TYPE_DEBUG
//    if (index == 0) {
//		spdlog::error("DescriptorHeap::ReleaseDescriptor: Attempting to release descriptor 0");
//    }
//    int32_t signedValue = static_cast<int32_t>(index);
//	// Disable signed/unsigned comparison warning for this line
//#pragma warning(suppress : 4018)
//	assert(signedValue >= 0 && signedValue < m_totalSize); // If this trggers, a descriptor is likely set but uninitialized
//#pragma warning(default : 4018)
//#endif
    m_freeIndices.push(index);
}