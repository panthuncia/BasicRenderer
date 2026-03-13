#include "Resources/Buffers/PagePool.h"

#include <cassert>
#include <spdlog/spdlog.h>

#include "Resources/Buffers/DynamicBuffer.h"
#include "Render/Runtime/UploadServiceAccess.h"

// PagePool implementation
PagePool::PagePool(const Config& config)
	: m_config(config)
{
	assert(m_config.pageSize > 0 && (m_config.pageSize & (m_config.pageSize - 1)) == 0
		   && "pageSize must be a power of two");
	assert(m_config.slabSize >= m_config.pageSize);
	assert(m_config.maxSlabs > 0);

	m_pagesPerSlab = static_cast<uint32_t>(m_config.slabSize / m_config.pageSize);

	// Create the page table buffer (initially empty, grows as slabs are added).
	m_pageTableBuffer = DynamicBuffer::CreateShared(
		sizeof(PageTableEntry),
		/*capacity=*/m_pagesPerSlab, // start with room for one slab
		m_config.debugName + "::PageTable");
	m_pageTableBuffer->SetUploadPolicyTag(rg::runtime::UploadPolicyTag::Immediate);

	// Resource group for slab buffers (render graph auto-invalidation).
	m_slabResourceGroup = std::make_shared<ResourceGroup>(m_config.debugName + "::Slabs");

	// Pre-allocate all slabs if requested.
	if (m_config.preAllocate) {
		for (uint32_t i = 0; i < m_config.maxSlabs; ++i) {
			if (!AllocateNewSlab()) {
				spdlog::error("PagePool: pre-allocation failed at slab {}", i);
				break;
			}
		}
		spdlog::info("PagePool: pre-allocated {} slabs ({} total pages)",
					 static_cast<uint32_t>(m_slabs.size()), m_totalPageCapacity);
	}
}

PagePool::~PagePool() = default;

// Slab management
bool PagePool::AllocateNewSlab() {
	if (static_cast<uint32_t>(m_slabs.size()) >= m_config.maxSlabs) {
		spdlog::error("PagePool: cannot allocate new slab- maxSlabs ({}) reached", m_config.maxSlabs);
		return false;
	}

	const uint32_t slabIndex = static_cast<uint32_t>(m_slabs.size());
	Slab slab;
	slab.buffer = DynamicBuffer::CreateShared(
		/*elementSize=*/1,
		/*capacity=*/m_config.slabSize,
		m_config.debugName + "::Slab" + std::to_string(slabIndex),
		/*byteAddress=*/true,
		/*UAV=*/false);
	slab.buffer->SetUploadPolicyTag(rg::runtime::UploadPolicyTag::Immediate);

	m_slabs.push_back(std::move(slab));
	m_totalPageCapacity += m_pagesPerSlab;

	// Register new slab in the resource group for render graph tracking.
	m_slabResourceGroup->AddResource(m_slabs.back().buffer);

	// Extend the CPU page table mirror.
	const uint32_t oldCapacity = static_cast<uint32_t>(m_pageTableCpu.size());
	m_pageTableCpu.resize(m_totalPageCapacity, PageTableEntry{});

	// Fill page table entries for the new slab.
	const uint32_t firstGlobal = slabIndex * m_pagesPerSlab;
	for (uint32_t i = 0; i < m_pagesPerSlab; ++i) {
		auto& entry = m_pageTableCpu[firstGlobal + i];
		entry.slabIndex = slabIndex;
		entry.slabByteOffset = static_cast<uint32_t>(static_cast<uint64_t>(i) * m_config.pageSize);
	}

	m_pageTableDirty = true;

	spdlog::info("PagePool: allocated slab {} ({:.1f} MB, {} pages)",
				 slabIndex,
				 static_cast<double>(m_config.slabSize) / (1024.0 * 1024.0),
				 m_pagesPerSlab);
	return true;
}

// Upload
void PagePool::UploadToPage(uint32_t globalPageID, uint32_t intraPageByteOffset,
							const void* data, size_t dataSize) {
	const uint32_t si = PageToSlabIndex(globalPageID);
	assert(si < m_slabs.size());

	const uint64_t slabOffset = PageToSlabByteOffset(globalPageID) + intraPageByteOffset;
	assert(intraPageByteOffset + dataSize <= m_config.pageSize);

	auto& slab = m_slabs[si];
	BUFFER_UPLOAD(data, dataSize,
				  rg::runtime::UploadTarget::FromShared(slab.buffer),
				  slabOffset);
}

// Page table
void PagePool::UpdatePageTableEntries(uint32_t firstGlobalPageID, uint32_t count) {
	// The page table entries are already correct in m_pageTableCpu (set at slab creation).
	// This method exists for future use when pages might be remapped.
	(void)firstGlobalPageID;
	(void)count;
	m_pageTableDirty = true;
}

void PagePool::FlushPageTableUpdates() {
	if (!m_pageTableDirty || m_pageTableCpu.empty()) return;

	// Ensure the GPU-side page table buffer is large enough.
	// DynamicBuffer will grow as needed via AddData / UpdateView.
	// We re-upload the entire table for simplicity.
	const size_t tableBytes = m_pageTableCpu.size() * sizeof(PageTableEntry);

	// If the buffer has no existing view, we need to allocate one.
	// For simplicity we'll just upload the raw data. The page table buffer
	// was created with structured element size = sizeof(PageTableEntry).
	// We use the BUFFER_UPLOAD macro which uploads to an offset.
	if (m_pageTableBuffer->Size() < tableBytes) {
		// Recreate with a larger capacity
		m_pageTableBuffer = DynamicBuffer::CreateShared(
			sizeof(PageTableEntry),
			m_pageTableCpu.size(),
			m_config.debugName + "::PageTable");
		m_pageTableBuffer->SetUploadPolicyTag(rg::runtime::UploadPolicyTag::Immediate);
	}

	BUFFER_UPLOAD(m_pageTableCpu.data(), tableBytes,
				  rg::runtime::UploadTarget::FromShared(m_pageTableBuffer),
				  0);

	m_pageTableDirty = false;
}

// Accessors
uint32_t PagePool::GetSlabCount() const {
	return static_cast<uint32_t>(m_slabs.size());
}

std::shared_ptr<DynamicBuffer> PagePool::GetSlab(uint32_t slabIndex) const {
	assert(slabIndex < m_slabs.size());
	return m_slabs[slabIndex].buffer;
}

uint32_t PagePool::GetSlabDescriptorIndex(const PageAllocation& alloc) const {
	if (!alloc.IsValid()) return 0u;
	const uint32_t si = PageToSlabIndex(alloc.firstPageID);
	assert(si < m_slabs.size());
	return m_slabs[si].buffer->GetSRVInfo(0).slot.index;
}

std::shared_ptr<DynamicBuffer> PagePool::GetPageTableBuffer() const {
	return m_pageTableBuffer;
}

uint32_t PagePool::GetTotalPageCount() const {
	return m_totalPageCapacity;
}
