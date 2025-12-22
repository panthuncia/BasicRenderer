#include "Resources/Buffers/SortedUnsignedIntBuffer.h"

#include <algorithm>

#include "Managers/Singletons/ResourceManager.h"
#include "Managers/Singletons/UploadManager.h"

void SortedUnsignedIntBuffer::Insert(unsigned int element) {
    // Resize the buffer if necessary
    if (m_data.size() >= m_capacity) {
        GrowBuffer(m_capacity * 2);
    }

    // Find the insertion point
    auto it = std::lower_bound(m_data.begin(), m_data.end(), element);
    // Prevent duplicates
    if (it != m_data.end() && *it == element) {
        return; // already present
    }

    uint32_t index = static_cast<uint32_t>(std::distance(m_data.begin(), it));
    m_data.insert(it, element);

    // Update the earliest modified index
    if (index < m_earliestModifiedIndex) {
        m_earliestModifiedIndex = index;
    }

    // Upload the entire suffix so GPU content matches the CPU vector after the insertion shift
    const unsigned int* src = m_data.data() + index;
    const uint32_t count = static_cast<uint32_t>(m_data.size() - index);
    BUFFER_UPLOAD(src, sizeof(unsigned int) * count, shared_from_this(), index * sizeof(unsigned int));
}

void SortedUnsignedIntBuffer::Remove(unsigned int element) {
    // Find the element
    auto it = std::lower_bound(m_data.begin(), m_data.end(), element);

    if (it != m_data.end() && *it == element) {
        const uint32_t index = static_cast<uint32_t>(std::distance(m_data.begin(), it));

        // Erase from CPU
        m_data.erase(it);

        // Update the earliest modified index
        if (index < m_earliestModifiedIndex) {
            m_earliestModifiedIndex = index;
        }

        // Shift left in GPU: upload suffix starting at 'index'
        if (!m_data.empty() && index < m_data.size()) {
            const unsigned int* src = m_data.data() + index;
            const uint32_t count = static_cast<uint32_t>(m_data.size() - index);
            BUFFER_UPLOAD(src, sizeof(unsigned int) * count, shared_from_this(), index * sizeof(unsigned int));
        }

        // Zero out the last stale slot (not strictly required if readers clamp to Size())
        if (m_data.size() < m_capacity) {
            const unsigned int zero = 0u;
            const uint32_t lastSlot = static_cast<uint32_t>(m_data.size());
            BUFFER_UPLOAD(&zero, sizeof(unsigned int), shared_from_this(), lastSlot * sizeof(unsigned int));
        }
    }
}

void SortedUnsignedIntBuffer::CreateBuffer(uint64_t capacity) {
    auto device = DeviceManager::GetInstance().GetDevice();
    m_capacity = capacity;
    m_dataBuffer = GpuBufferBacking::CreateUnique(rhi::HeapType::DeviceLocal, capacity * sizeof(unsigned int), GetGlobalResourceID(), m_UAV);
    AssignDescriptorSlots();
}

void SortedUnsignedIntBuffer::GrowBuffer(uint64_t newSize) {
    auto device = DeviceManager::GetInstance().GetDevice();
    auto newDataBuffer = GpuBufferBacking::CreateUnique(rhi::HeapType::DeviceLocal, newSize * sizeof(unsigned int), GetGlobalResourceID(), m_UAV);
	// Copy existing data to new buffer and discard old buffer after copy
    UploadManager::GetInstance().QueueCopyAndDiscard(shared_from_this(), std::move(m_dataBuffer), *GetStateTracker(), m_capacity * sizeof(unsigned int));
    m_dataBuffer = std::move(newDataBuffer);

    m_capacity = newSize;
    AssignDescriptorSlots();
    SetName(name);
}

void SortedUnsignedIntBuffer::AssignDescriptorSlots()
{
    auto& rm = ResourceManager::GetInstance();

    ResourceManager::ViewRequirements req{};
    ResourceManager::ViewRequirements::BufferViews b{};

    const uint32_t numElements = static_cast<uint32_t>(m_capacity);

    b.createCBV = false;
    b.createSRV = true;
    b.createUAV = false;
    b.createNonShaderVisibleUAV = false;
    b.uavCounterOffset = 0;

    b.srvDesc = rhi::SrvDesc{
        .dimension = rhi::SrvDim::Buffer,
        .formatOverride = rhi::Format::Unknown,
        .buffer = {
            .kind = rhi::BufferViewKind::Structured,
            .firstElement = 0,
            .numElements = numElements,
            .structureByteStride = 4,
        },
    };


    req.views = b;
    auto resource = m_dataBuffer->GetAPIResource();
    rm.AssignDescriptorSlots(*this, resource, req);
}