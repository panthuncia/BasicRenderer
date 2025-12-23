#include "Resources/Buffers/DynamicBuffer.h"

#include <spdlog/spdlog.h>

#include "Resources/Buffers/BufferView.h"
#include "Managers/Singletons/ResourceManager.h"
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
            auto viewedWeak = std::weak_ptr(
                std::dynamic_pointer_cast<DynamicBuffer>(Resource::weak_from_this().lock())
            );
            // Return BufferView
            return BufferView::CreateUnique(viewedWeak, offset, requiredSize, elementSize);
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

std::unique_ptr<BufferView> DynamicBuffer::AddData(const void* data, size_t size, size_t elementSize, size_t fullAllocationSize) {
	size_t actualSize = size;
    if (fullAllocationSize != 0) {
		actualSize = fullAllocationSize;
		if (actualSize < size) {
			spdlog::warn("Full allocation size is smaller than the data size. Using data size instead.");
			actualSize = size;
		}
    }
	std::unique_ptr<BufferView> view = Allocate(actualSize, elementSize);
    
	if (data != nullptr) {
        BUFFER_UPLOAD(data, size, shared_from_this(), view->GetOffset());
	}

	return std::move(view);
}

void DynamicBuffer::UpdateView(BufferView* view, const void* data) {
    BUFFER_UPLOAD(data, view->GetSize(), shared_from_this(), view->GetOffset());
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

void DynamicBuffer::AssignDescriptorSlots()
{
    auto& rm = ResourceManager::GetInstance();

    ResourceManager::ViewRequirements req{};
    ResourceManager::ViewRequirements::BufferViews b{};

    const uint32_t viewElements =
        static_cast<uint32_t>(m_byteAddress ? (m_capacity / 4) : m_capacity/m_elementSize);

    b.createCBV = false;
    b.createSRV = true;
    b.createUAV = m_UAV;
    b.createNonShaderVisibleUAV = false;
    b.uavCounterOffset = 0;

    // SRV
    b.srvDesc = rhi::SrvDesc{
        .dimension = rhi::SrvDim::Buffer,
        .formatOverride = m_byteAddress ? rhi::Format::R32_Typeless : rhi::Format::Unknown,
        .buffer = {
            .kind = m_byteAddress ? rhi::BufferViewKind::Raw : rhi::BufferViewKind::Structured,
            .firstElement = 0,
            .numElements = viewElements,
            .structureByteStride = static_cast<uint32_t>(m_byteAddress ? 0 : m_elementSize),
        },
    };

    // UAV
    b.uavDesc = rhi::UavDesc{
        .dimension = rhi::UavDim::Buffer,
        .buffer = {
            .kind = m_byteAddress ? rhi::BufferViewKind::Raw : rhi::BufferViewKind::Structured,
            .firstElement = 0,
            .numElements = viewElements,
            .structureByteStride = static_cast<uint32_t>(m_byteAddress ? 0 : m_elementSize),
        },
    };

    req.views = b;
    auto resource = m_dataBuffer->GetAPIResource();
    rm.AssignDescriptorSlots(*this, resource, req);
}

void DynamicBuffer::CreateBuffer(size_t capacity) {
    auto device = DeviceManager::GetInstance().GetDevice();
    m_capacity = capacity;
    m_dataBuffer = GpuBufferBacking::CreateUnique(rhi::HeapType::DeviceLocal, capacity, GetGlobalResourceID(), m_UAV);
    m_memoryBlocks.push_back({ 0, capacity, true });

    for (const auto& bundle : m_metadataBundles) {
        m_dataBuffer->ApplyMetadataComponentBundle(bundle);
    }

	AssignDescriptorSlots();
}

void DynamicBuffer::GrowBuffer(size_t newSize) {
    auto device = DeviceManager::GetInstance().GetDevice();
    auto newDataBuffer = GpuBufferBacking::CreateUnique(rhi::HeapType::DeviceLocal, newSize, GetGlobalResourceID(), m_UAV);
	UploadManager::GetInstance().QueueCopyAndDiscard(shared_from_this(), std::move(m_dataBuffer), *GetStateTracker(), m_capacity);
	m_dataBuffer = std::move(newDataBuffer);

    m_capacity = newSize;

    for (const auto& bundle : m_metadataBundles) {
        m_dataBuffer->ApplyMetadataComponentBundle(bundle);
    }

    AssignDescriptorSlots();

	SetName(m_name);
}