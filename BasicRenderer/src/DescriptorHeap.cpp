#include "DescriptorHeap.h"
#include "Utilities.h"

DescriptorHeap::DescriptorHeap(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t numDescriptors, bool shaderVisible)
    : m_type(type), m_shaderVisible(shaderVisible), m_numDescriptorsAllocated(0) {
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.NumDescriptors = numDescriptors;
    heapDesc.Type = type;
    heapDesc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_heap)));
    m_descriptorSize = device->GetDescriptorHandleIncrementSize(type);
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

ID3D12DescriptorHeap* DescriptorHeap::GetHeap() const {
    return m_heap.Get();
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
    m_freeIndices.push(index);
}