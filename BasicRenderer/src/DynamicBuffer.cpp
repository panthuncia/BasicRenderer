#include "DynamicBuffer.h"

#include "DirectX/d3dx12.h"
#include "BufferView.h"

std::unique_ptr<BufferView> DynamicBuffer::Allocate(size_t size, std::type_index type) {
    size_t requiredSize = size;

    // Search for a free block
    for (auto it = m_memoryBlocks.begin(); it != m_memoryBlocks.end(); ++it)
    {
        if (it->isFree && it->size >= requiredSize)
        {
            size_t remainingSize = it->size - requiredSize;
            size_t offset = it->offset;

            it->isFree = false;
            it->size = requiredSize;

            if (remainingSize > 0)
            {
                // Split the block
                m_memoryBlocks.insert(it + 1, { offset + requiredSize, remainingSize, true });
            }

            // Return BufferView
            return std::move(std::make_unique<BufferView>(this, offset, requiredSize, type));
        }
    }

    // No suitable block found, need to grow the buffer
    size_t newCapacity = m_capacity + (std::max)(m_capacity, requiredSize);
    GrowBuffer(newCapacity);

    // Try allocating again
    return Allocate(size, type);
}

void DynamicBuffer::Deallocate(const std::shared_ptr<BufferView>& view) {
    size_t offset = view->GetOffset();
    size_t size = view->GetSize();

    // Find the block
    for (auto it = m_memoryBlocks.begin(); it != m_memoryBlocks.end(); ++it)
    {
        if (it->offset == offset && it->size == size && !it->isFree)
        {
            it->isFree = true;

            // Coalesce with previous block if free
            if (it != m_memoryBlocks.begin())
            {
                auto prevIt = it - 1;
                if (prevIt->isFree)
                {
                    prevIt->size += it->size;
                    m_memoryBlocks.erase(it);
                    it = prevIt;
                }
            }

            // Coalesce with next block if free
            if ((it + 1) != m_memoryBlocks.end())
            {
                auto nextIt = it + 1;
                if (nextIt->isFree)
                {
                    it->size += nextIt->size;
                    m_memoryBlocks.erase(nextIt);
                }
            }

            break;
        }
    }
}

void DynamicBuffer::CreateBuffer(size_t capacity) {
    auto& device = DeviceManager::GetInstance().GetDevice();
    m_capacity = capacity;
    m_uploadBuffer = Buffer::CreateShared(device.Get(), ResourceCPUAccessType::WRITE, capacity, true);
    m_dataBuffer = Buffer::CreateShared(device.Get(), ResourceCPUAccessType::NONE, capacity, false);
    CD3DX12_RANGE readRange(0, 0);
    m_uploadBuffer->m_buffer->Map(0, &readRange, reinterpret_cast<void**>(&m_mappedData));
    m_memoryBlocks.push_back({ 0, capacity, true });
}

void DynamicBuffer::GrowBuffer(size_t newSize) {
    auto& device = DeviceManager::GetInstance().GetDevice();
    auto newUploadBuffer = Buffer::CreateShared(device.Get(), ResourceCPUAccessType::WRITE, newSize, true);
    m_dataBuffer = Buffer::CreateShared(device.Get(), ResourceCPUAccessType::NONE, newSize, false);

    void* newMappedData = nullptr;
    CD3DX12_RANGE readRange(0, 0);
    newUploadBuffer->m_buffer->Map(0, &readRange, reinterpret_cast<void**>(&newMappedData));
    memcpy(newMappedData, m_mappedData, m_capacity);
	m_mappedData = newMappedData;
	m_uploadBuffer = newUploadBuffer;
    size_t oldCapacity = m_capacity;
    size_t sizeDiff = newSize - m_capacity;
    m_capacity = newSize;
    // Update the memory blocks to reflect the new capacity
    m_memoryBlocks.push_back({ oldCapacity, sizeDiff, true });
    onResized(m_globalResizableBufferID, 1, m_capacity, m_dataBuffer);
}