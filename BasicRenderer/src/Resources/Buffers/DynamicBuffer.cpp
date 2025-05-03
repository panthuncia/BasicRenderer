#include "Resources/Buffers/DynamicBuffer.h"

#include <spdlog/spdlog.h>

#include "DirectX/d3dx12.h"
#include "Resources/Buffers/BufferView.h"
#include "Managers/Singletons/DeletionManager.h"
#include "Managers/Singletons/UploadManager.h"

std::unique_ptr<BufferView> DynamicBuffer::Allocate(size_t size, size_t elementSize) {
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
            return std::move(BufferView::CreateUnique(this, offset, requiredSize, elementSize));
        }
    }

    // No suitable block found, need to grow the buffer

    // Delete the last block if it is free
    size_t newBlockSize = (std::max)(m_capacity, requiredSize);
    size_t growBy = newBlockSize;
    if (!m_memoryBlocks.empty() && m_memoryBlocks.back().isFree)
    {
        growBy -= m_memoryBlocks.back().size;
        m_memoryBlocks.pop_back();
    }
    size_t newCapacity = m_capacity + growBy;

    GrowBuffer(newCapacity);
    MemoryBlock newBlock;
    newBlock.isFree = true;
	newBlock.offset = m_capacity - newBlockSize;
	newBlock.size = newBlockSize;
    m_memoryBlocks.push_back(newBlock);
	spdlog::info("Growing buffer to {} bytes", newCapacity);
    // Try allocating again
    return Allocate(size, elementSize);
}

std::unique_ptr<BufferView> DynamicBuffer::AddData(const void* data, size_t size, size_t elementSize) {
	std::unique_ptr<BufferView> view = Allocate(size, elementSize);
    
	if (data != nullptr) {
		auto& uploadManager = UploadManager::GetInstance();
		uploadManager.UploadData(data, size, this, view->GetOffset());
	}

	return std::move(view);
}

void DynamicBuffer::UpdateView(BufferView* view, const void* data) {
	auto& uploadManager = UploadManager::GetInstance();
	uploadManager.UploadData(data, view->GetSize(), this, view->GetOffset());
}

void DynamicBuffer::Deallocate(const BufferView* view) {
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
    m_dataBuffer = Buffer::CreateShared(device.Get(), ResourceCPUAccessType::NONE, capacity, false, m_UAV);
    m_memoryBlocks.push_back({ 0, capacity, true });
}

void DynamicBuffer::GrowBuffer(size_t newSize) {
    auto& device = DeviceManager::GetInstance().GetDevice();
    auto newUploadBuffer = Buffer::CreateShared(device.Get(), ResourceCPUAccessType::WRITE, newSize, true, false);
    if (m_dataBuffer != nullptr) {
        DeletionManager::GetInstance().MarkForDelete(m_dataBuffer);
    }
    auto newDataBuffer = Buffer::CreateShared(device.Get(), ResourceCPUAccessType::NONE, newSize, false, m_UAV);
	UploadManager::GetInstance().QueueResourceCopy(newDataBuffer, m_dataBuffer, m_capacity);
	m_dataBuffer = newDataBuffer;

    size_t oldCapacity = m_capacity;
    size_t sizeDiff = newSize - m_capacity;
    m_capacity = newSize;
    onResized(m_globalResizableBufferID, m_elementSize, m_capacity/m_elementSize, m_byteAddress, this, m_UAV);
	SetName(m_name);
}