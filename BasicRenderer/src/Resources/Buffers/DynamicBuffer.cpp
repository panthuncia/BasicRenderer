#include "Resources/Buffers/DynamicBuffer.h"

#include <spdlog/spdlog.h>

#include "Resources/Buffers/BufferView.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Resources/ExternalBackingResource.h"
#include "Resources/GPUBacking/GpuBufferBacking.h"
#include "Render/Runtime/UploadServiceAccess.h"
#include "Render/Runtime/UploadPolicyServiceAccess.h"

std::unique_ptr<BufferView> DynamicBuffer::Allocate(size_t size, size_t elementSize) {
	size_t requiredSize = size;

	// Search for a free block using the size-indexed set - O(log n)
	auto freeIt = m_freeBlocks.lower_bound({ requiredSize, 0 });
	if (freeIt != m_freeBlocks.end())
	{
		size_t blockOffset = freeIt->second;

		// Remove from free index
		m_freeBlocks.erase(freeIt);

		// Update block in the offset map
		auto& block = m_blocksByOffset[blockOffset];
		size_t remainingSize = block.size - requiredSize;

		block.isFree = false;
		block.size = requiredSize;

		if (remainingSize > 0)
		{
			// Split the block
			size_t newOffset = blockOffset + requiredSize;
			m_blocksByOffset[newOffset] = { newOffset, remainingSize, true };
			m_freeBlocks.insert({ remainingSize, newOffset });
		}

		// Cache the weak pointer to avoid repeated dynamic_pointer_cast
		if (!m_weakPtrCached) {
			m_cachedWeakPtr = std::weak_ptr(
				std::dynamic_pointer_cast<DynamicBuffer>(Resource::weak_from_this().lock())
			);
			m_weakPtrCached = true;
		}
        // Return BufferView
        return BufferView::CreateUnique(m_cachedWeakPtr, blockOffset, requiredSize, elementSize);
	}

	// No suitable block found, need to grow the buffer

	// Absorb the last block if it is free
	size_t newBlockSize = (std::max)(m_capacity, requiredSize);
	size_t growBy = newBlockSize;
	if (!m_blocksByOffset.empty())
	{
		auto lastIt = std::prev(m_blocksByOffset.end());
		if (lastIt->second.isFree)
		{
			growBy -= lastIt->second.size;
			m_freeBlocks.erase({ lastIt->second.size, lastIt->second.offset });
			m_blocksByOffset.erase(lastIt);
		}
	}
	size_t newCapacity = m_capacity + growBy;

	GrowBuffer(newCapacity);
	size_t newOffset = m_capacity - newBlockSize;
	m_blocksByOffset[newOffset] = { newOffset, newBlockSize, true };
	m_freeBlocks.insert({ newBlockSize, newOffset });
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
        StageOrUpload(data, size, view->GetOffset());
	}

    return view;
}

void DynamicBuffer::UpdateView(BufferView* view, const void* data) {
    StageOrUpload(data, view->GetSize(), view->GetOffset());
}

void DynamicBuffer::StageOrUpload(const void* data, size_t size, size_t offset) {
    if (GetUploadPolicyTag() != rg::runtime::UploadPolicyTag::Immediate
        && rg::runtime::GetActiveUploadPolicyService() == nullptr) {
        BUFFER_UPLOAD(data, size, rg::runtime::UploadTarget::FromShared(shared_from_this()), offset);
        return;
    }

    SyncUploadPolicyState();
    EnsureUploadPolicyRegistration();

#if BUILD_TYPE == BUILD_TYPE_DEBUG
    const bool staged = m_uploadPolicyState.StageWrite(data, size, offset, GetBufferSize(), __FILE__, __LINE__);
#else
    const bool staged = m_uploadPolicyState.StageWrite(data, size, offset, GetBufferSize());
#endif
    if (staged) {
        return;
    }

    BUFFER_UPLOAD(data, size, rg::runtime::UploadTarget::FromShared(shared_from_this()), offset);
}

void DynamicBuffer::Deallocate(const BufferView* view) {
    if (view == nullptr) {
        return;
    }

    size_t offset = view->GetOffset();
    size_t size = view->GetSize();

    // Find the block by offset - O(log n)
    auto it = m_blocksByOffset.find(offset);
    if (it == m_blocksByOffset.end() || it->second.size != size || it->second.isFree)
    {
        return;
    }

    it->second.isFree = true;

    // Coalesce with next block if free
    auto nextIt = std::next(it);
    if (nextIt != m_blocksByOffset.end() && nextIt->second.isFree)
    {
        m_freeBlocks.erase({ nextIt->second.size, nextIt->second.offset });
        it->second.size += nextIt->second.size;
        m_blocksByOffset.erase(nextIt);
    }

    // Coalesce with previous block if free
    if (it != m_blocksByOffset.begin())
    {
        auto prevIt = std::prev(it);
        if (prevIt->second.isFree)
        {
            m_freeBlocks.erase({ prevIt->second.size, prevIt->second.offset });
            prevIt->second.size += it->second.size;
            m_blocksByOffset.erase(it);
            it = prevIt;
        }
    }

    // Add the (possibly coalesced) block to the free index
    m_freeBlocks.insert({ it->second.size, it->second.offset });
}

void DynamicBuffer::AssignDescriptorSlots()
{
    BufferBase::DescriptorRequirements requirements{};

    const uint32_t viewElements =
        static_cast<uint32_t>(m_byteAddress ? (m_capacity / 4) : m_capacity/m_elementSize);

    requirements.createCBV = false;
    requirements.createSRV = true;
    requirements.createUAV = m_UAV;
    requirements.createNonShaderVisibleUAV = false;
    requirements.uavCounterOffset = 0;

    // SRV
    requirements.srvDesc = rhi::SrvDesc{
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
    requirements.uavDesc = rhi::UavDesc{
        .dimension = rhi::UavDim::Buffer,
        .buffer = {
            .kind = m_byteAddress ? rhi::BufferViewKind::Raw : rhi::BufferViewKind::Structured,
            .firstElement = 0,
            .numElements = viewElements,
            .structureByteStride = static_cast<uint32_t>(m_byteAddress ? 0 : m_elementSize),
        },
    };

    SetDescriptorRequirements(requirements);
}

void DynamicBuffer::CreateBuffer(size_t capacity) {
	auto device = DeviceManager::GetInstance().GetDevice();
	m_capacity = capacity;
	auto newDataBuffer = GpuBufferBacking::CreateUnique(rhi::HeapType::DeviceLocal, capacity, GetGlobalResourceID(), m_UAV);
	SetBacking(std::move(newDataBuffer), capacity);
	m_uploadPolicyState.OnBufferResized(GetBufferSize());
	m_blocksByOffset[0] = { 0, capacity, true };
	m_freeBlocks.insert({ capacity, 0 });

	for (const auto& bundle : m_metadataBundles) {
		ApplyMetadataToBacking(bundle);
	}

	AssignDescriptorSlots();
}

void DynamicBuffer::GrowBuffer(size_t newSize) {
    auto device = DeviceManager::GetInstance().GetDevice();
    auto newDataBuffer = GpuBufferBacking::CreateUnique(rhi::HeapType::DeviceLocal, newSize, GetGlobalResourceID(), m_UAV);
    if (m_dataBuffer) {
        auto oldBackingResource = ExternalBackingResource::CreateShared(std::move(m_dataBuffer));
        if (auto* uploadService = rg::runtime::GetActiveUploadService()) {
            uploadService->QueueResourceCopy(shared_from_this(), oldBackingResource, m_capacity);
        }
    }
	SetBacking(std::move(newDataBuffer), newSize);
    m_uploadPolicyState.OnBufferResized(GetBufferSize());

    m_capacity = newSize;

    for (const auto& bundle : m_metadataBundles) {
        ApplyMetadataToBacking(bundle);
    }

    AssignDescriptorSlots();

	SetName(m_name);
}