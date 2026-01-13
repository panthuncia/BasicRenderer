#include "Managers/Singletons/UploadManager.h"

#include <rhi_helpers.h>
#include <rhi_debug.h>

#include "Resources/Buffers/Buffer.h"
#include "Resources/Resource.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Resources/ExternalBackingResource.h"
#include "Resources/MemoryStatisticsComponents.h"

void UploadManager::Initialize() {
	m_numFramesInFlight = SettingsManager::GetInstance()
		.getSettingGetter<uint8_t>("numFramesInFlight")();

	m_currentCapacity = 1024 * 1024 * 4; // 4MB

	m_pages.push_back({ Buffer::CreateShared(rhi::HeapType::Upload, kPageSize, false), 0 });

	// ring buffer pointers
	m_headOffset = 0;
	m_tailOffset = 0;
	m_frameStart.assign(m_numFramesInFlight, 0);

	getNumFramesInFlight =
		SettingsManager::GetInstance().getSettingGetter<uint8_t>("numFramesInFlight");

	m_activePage = 0;
	m_frameStart.resize(m_numFramesInFlight, 0);
}

namespace {
	size_t AlignUpSizeT(const size_t v, const size_t a) noexcept {
		return (v + (a - 1)) & ~(a - 1);
	}
}

// Coalescing / last-write-wins helpers (buffers)
bool UploadManager::RangesOverlap(const size_t a0, const size_t a1, const size_t b0, const size_t b1) noexcept {
	return (a0 < b1) && (b0 < a1);
}
bool UploadManager::RangeContains(const size_t outer0, const size_t outer1, const size_t inner0, const size_t inner1) noexcept {
	return outer0 <= inner0 && inner1 <= outer1;
}

void UploadManager::MapUpload(const std::shared_ptr<Resource>& uploadBuffer, const size_t mapSize, uint8_t** outMapped) noexcept {
	if (!outMapped) return;
	*outMapped = nullptr;
	if (!uploadBuffer) return;
	uploadBuffer->GetAPIResource().Map(reinterpret_cast<void**>(outMapped), 0, mapSize);
}

void UploadManager::UnmapUpload(const std::shared_ptr<Resource>& uploadBuffer) noexcept {
	if (!uploadBuffer) return;
	uploadBuffer->GetAPIResource().Unmap(0, 0);
}

bool UploadManager::TryCoalesceAppend(ResourceUpdate& last, const ResourceUpdate& next) noexcept {
	if (!last.active || !next.active) return false;
	if (last.resourceToUpdate != next.resourceToUpdate) return false;
	if (last.uploadBuffer.get() != next.uploadBuffer.get()) return false;

	// Must be contiguous in both destination and upload regions.
	if (last.dataBufferOffset + last.size != next.dataBufferOffset) return false;
	if (last.uploadBufferOffset + last.size != next.uploadBufferOffset) return false;

	last.size += next.size;

#if BUILD_TYPE == BUILD_TYPE_DEBUG
	// Preserve newest debug provenance.
	last.file = next.file;
	last.line = next.line;
	last.threadID = next.threadID;
	last.stackSize = next.stackSize;
	for (uint8_t i = 0; i < next.stackSize && i < ResourceUpdate::MaxStack; ++i) last.stack[i] = next.stack[i];
#endif
	return true;
}

void UploadManager::ApplyLastWriteWins(ResourceUpdate& newUpdate) noexcept
{
	if (!newUpdate.active) return;

	// We may expand newUpdate as we merge; track its current dst range.
	size_t new0 = newUpdate.dataBufferOffset;
	size_t new1 = newUpdate.dataBufferOffset + newUpdate.size;

	// TODO: A more efficient data structure for tracking updates could help here.
	for (int i = static_cast<int>(m_resourceUpdates.size()) - 1; i >= 0; --i) {
		auto& u = m_resourceUpdates[static_cast<size_t>(i)];
		if (!u.active) continue;
		if (u.resourceToUpdate != newUpdate.resourceToUpdate) continue;

		const size_t u0 = u.dataBufferOffset;
		const size_t u1 = u.dataBufferOffset + u.size;

		if (!RangesOverlap(u0, u1, new0, new1)) continue;

		// If an older update fully contains the new range, patch the old upload region with the (already written) new bytes and drop newUpdate.
		if (RangeContains(u0, u1, new0, new1)) {
			const size_t patchOffsetInU = new0 - u0;
			const size_t patchUploadOffset = u.uploadBufferOffset + patchOffsetInU;

			// Copy from newUpdate's upload bytes into u's upload bytes.
			uint8_t* uMapped = nullptr;
			uint8_t* nMapped = nullptr;

			MapUpload(u.uploadBuffer, patchUploadOffset + newUpdate.size, &uMapped);
			MapUpload(newUpdate.uploadBuffer, newUpdate.uploadBufferOffset + newUpdate.size, &nMapped);

			if (uMapped && nMapped) {
				memcpy(uMapped + patchUploadOffset, nMapped + newUpdate.uploadBufferOffset, newUpdate.size);
			}

			UnmapUpload(newUpdate.uploadBuffer);
			UnmapUpload(u.uploadBuffer);

#if BUILD_TYPE == BUILD_TYPE_DEBUG
			// Keep newest provenance.
			u.file = newUpdate.file;
			u.line = newUpdate.line;
			u.threadID = newUpdate.threadID;
			u.stackSize = newUpdate.stackSize;
			for (uint8_t j = 0; j < newUpdate.stackSize && j < ResourceUpdate::MaxStack; ++j) u.stack[j] = newUpdate.stack[j];
#endif

			newUpdate.active = false;
			return;
		}

		// If the old update is fully covered by the new range, we can drop the old one.
		if (RangeContains(new0, new1, u0, u1)) {
			u.active = false;
			continue;
		}

		// Partial overlap: create a union update that contains both ranges, with last-write-wins ordering.
		const size_t union0 = std::min(u0, new0);
		const size_t union1 = std::max(u1, new1);
		const size_t unionSize = union1 - union0;

		std::shared_ptr<Resource> unionUpload;
		size_t unionUploadOffset = 0;
		if (!AllocateUploadRegion(unionSize, /*alignment*/16, unionUpload, unionUploadOffset)) {
			// If we fail, just keep both updates
			continue;
		}

		// Map union + the two source regions and assemble the bytes:
		//  1. copy older bytes (u) into union
		//  2. overwrite with newer bytes (newUpdate) into union
		uint8_t* unionMapped = nullptr;
		MapUpload(unionUpload, unionUploadOffset + unionSize, &unionMapped);
		if (!unionMapped) {
			UnmapUpload(unionUpload);
			continue;
		}

		// Copy u -> union
		{
			uint8_t* uMapped = nullptr;
			MapUpload(u.uploadBuffer, u.uploadBufferOffset + u.size, &uMapped);
			if (uMapped) {
				const size_t dstOff = unionUploadOffset + (u0 - union0);
				memcpy(unionMapped + dstOff, uMapped + u.uploadBufferOffset, u.size);
			}
			UnmapUpload(u.uploadBuffer);
		}

		// Copy newUpdate -> union (overwrite)
		{
			uint8_t* nMapped = nullptr;
			MapUpload(newUpdate.uploadBuffer, newUpdate.uploadBufferOffset + newUpdate.size, &nMapped);
			if (nMapped) {
				const size_t dstOff = unionUploadOffset + (new0 - union0);
				memcpy(unionMapped + dstOff, nMapped + newUpdate.uploadBufferOffset, newUpdate.size);
			}
			UnmapUpload(newUpdate.uploadBuffer);
		}

		UnmapUpload(unionUpload);

		// Retire the overlapped old update; replace newUpdate with the union.
		u.active = false;

		newUpdate.uploadBuffer = unionUpload;
		newUpdate.uploadBufferOffset = unionUploadOffset;
		newUpdate.dataBufferOffset = union0;
		newUpdate.size = unionSize;

		new0 = union0;
		new1 = union1;
	}
}

bool UploadManager::AllocateUploadRegion(size_t size, size_t alignment, std::shared_ptr<Resource>& outUploadBuffer, size_t& outOffset)
{
	if (alignment == 0) alignment = 1;
	if (m_pages.empty()) {
		m_pages.push_back({ Buffer::CreateShared(rhi::HeapType::Upload, kPageSize, false), 0 });
		m_pages.back().buffer->ApplyMetadataComponentBundle(EntityComponentBundle().Set<MemoryStatisticsComponents::ResourceUsage>({ "Upload buffer" }));
		m_activePage = 0;
	}

	UploadPage* page = &m_pages[m_activePage];

	size_t alignedTail = AlignUpSizeT(page->tailOffset, alignment);

	// If it won't fit in the rest of this page, open a new page
	if (alignedTail + size > page->buffer->GetSize()) {
		++m_activePage;
		if (m_activePage >= m_pages.size()) {
			// allocate another fresh page sized to the request (at least kPageSize)
			size_t allocSize = std::max(kPageSize, size);
			m_pages.push_back({ Buffer::CreateShared(rhi::HeapType::Upload, allocSize, false), 0 });
			m_pages.back().buffer->ApplyMetadataComponentBundle(EntityComponentBundle().Set<MemoryStatisticsComponents::ResourceUsage>({ "Upload buffer" }));
		}
		page = &m_pages[m_activePage];
		page->tailOffset = 0;
		alignedTail = AlignUpSizeT(page->tailOffset, alignment);

		// If it still doesn't fit (should only happen if GetSize() < size), allocate a dedicated page.
		if (alignedTail + size > page->buffer->GetSize()) {
			size_t allocSize = std::max(kPageSize, size);
			m_pages.push_back({ Buffer::CreateShared(rhi::HeapType::Upload, allocSize, false), 0 });
			m_pages.back().buffer->ApplyMetadataComponentBundle(EntityComponentBundle().Set<MemoryStatisticsComponents::ResourceUsage>({ "Upload buffer" }));
			m_activePage = m_pages.size() - 1;
			page = &m_pages[m_activePage];
			page->tailOffset = 0;
			alignedTail = AlignUpSizeT(page->tailOffset, alignment);
		}
	}

	outOffset = alignedTail;
	page->tailOffset = alignedTail + size;
	outUploadBuffer = page->buffer;
	return true;
}


#if BUILD_TYPE == BUILD_TYPE_DEBUG
void UploadManager::UploadData(const void* data, size_t size, UploadTarget resourceToUpdate, size_t dataBufferOffset, const char* file, int line)
#else
void UploadManager::UploadData(const void* data, size_t size, UploadTarget resourceToUpdate, size_t dataBufferOffset)
#endif 
{
	if (size > kPageSize) {
		// break it into multiple sub uploads
		size_t done = 0;
		size_t dstOffset = dataBufferOffset;
		while (done < size) {
			size_t chunk = (std::min)(size - done, kPageSize);
			BUFFER_UPLOAD(
				reinterpret_cast<const uint8_t*>(data) + done,
				chunk,
				resourceToUpdate,
				dstOffset
			);
			done += chunk;
			dstOffset += chunk;
		}
		return;
	}

	UploadPage* page = &m_pages[m_activePage];

	// if it won't fit in the rest of this page, open a new page
	if (page->tailOffset + size > page->buffer->GetSize()) {
		++m_activePage;
		if (m_activePage >= m_pages.size()) {
			// allocate another fresh page
			size_t allocSize = std::max(kPageSize, size);
			auto device = DeviceManager::GetInstance().GetDevice();
			m_pages.push_back({ Buffer::CreateShared(rhi::HeapType::Upload, allocSize, false), 0 });
		}
		page = &m_pages[m_activePage];
		page->tailOffset = 0;
	}

	// now we're guaranteed space
	size_t uploadOffset = page->tailOffset;
	page->tailOffset += size;

	uint8_t* mapped = nullptr;
	page->buffer->GetAPIResource().Map(reinterpret_cast<void**>(&mapped), 0, size);
	memcpy(mapped + uploadOffset, data, size);
	page->buffer->GetAPIResource().Unmap(0, 0);

	// queue up the GPU copy
	ResourceUpdate update;
	update.size = size;
	update.resourceToUpdate = resourceToUpdate;
	update.uploadBuffer = page->buffer;
	update.uploadBufferOffset = uploadOffset;
	update.dataBufferOffset = dataBufferOffset;
#if BUILD_TYPE == BUILD_TYPE_DEBUG
	//update.stackTrace = std::stacktrace::current();
	unsigned int idOrRegistryIndex = 0;
	switch (resourceToUpdate.kind) {
		case UploadTarget::Kind::PinnedShared:
			idOrRegistryIndex = static_cast<unsigned int>(resourceToUpdate.pinned ? resourceToUpdate.pinned->GetGlobalResourceID() : ~0ull);
			break;
		case UploadTarget::Kind::RegistryHandle:
			idOrRegistryIndex = resourceToUpdate.h.GetKey().idx;
			break;
		default:
			break;
		}

	update.resourceIDOrRegistryIndex = idOrRegistryIndex;
	update.targetKind = resourceToUpdate.kind;
	update.file = file;
	update.line = line;
	update.frameIndex = (m_numFramesInFlight ? (GetInstance().getNumFramesInFlight()) : 0);
	update.threadID = std::this_thread::get_id();
#ifdef _WIN32
	void* frames[ResourceUpdate::MaxStack];
	USHORT captured = RtlCaptureStackBackTrace(1, ResourceUpdate::MaxStack, frames, nullptr);
	update.stackSize = static_cast<uint8_t>(captured);
	for (USHORT i = 0; i < captured; i++) update.stack[i] = frames[i];
#endif
#endif
	//ApplyLastWriteWins(update); // Too slow

	//if (!update.active) {
	//	return;
	//}

	// contiguous append coalescing against the most recent active update.
	for (int i = static_cast<int>(m_resourceUpdates.size()) - 1; i >= 0; --i) {
		auto& last = m_resourceUpdates[static_cast<size_t>(i)];
		if (!last.active) {
			continue;
		}
		if (TryCoalesceAppend(last, update)) {
			return; // merged into 'last'
		}
		break;
	}

	m_resourceUpdates.push_back(std::move(update));
}

#if BUILD_TYPE == BUILD_TYPE_DEBUG
void UploadManager::UploadTextureSubresources(
	UploadTarget target,
	rhi::Format fmt,
	uint32_t baseWidth,
	uint32_t baseHeight,
	uint32_t depthOrLayers,
	uint32_t mipLevels,
	uint32_t arraySize,
	const rhi::helpers::SubresourceData* srcSubresources,
	uint32_t srcCount,
	const char* file,
	int line)
#else
void UploadManager::UploadTextureSubresources(
	UploadTarget target,
	rhi::Format fmt,
	uint32_t baseWidth,
	uint32_t baseHeight,
	uint32_t depthOrLayers,
	uint32_t mipLevels,
	uint32_t arraySize,
	const rhi::helpers::SubresourceData* srcSubresources,
	uint32_t srcCount)
#endif
{
	if (!srcSubresources || srcCount == 0) return;

	rhi::Span<const rhi::helpers::SubresourceData> srcSpan{ srcSubresources, srcCount };

	const auto plan = rhi::helpers::PlanTextureUploadSubresources(
		fmt, baseWidth, baseHeight, depthOrLayers, mipLevels, arraySize, srcSpan);

	if (plan.totalSize == 0 || plan.footprints.empty()) {
		return;
	}

	// Allocate subregion from the upload pages with proper placement alignment.
	std::shared_ptr<Resource> uploadBuffer;
	size_t uploadBaseOffset = 0;
	AllocateUploadRegion(static_cast<size_t>(plan.totalSize), /*alignment*/512, uploadBuffer, uploadBaseOffset);

	uint8_t* mapped = nullptr;
	uploadBuffer->GetAPIResource().Map(reinterpret_cast<void**>(&mapped), 0, uploadBaseOffset + static_cast<size_t>(plan.totalSize));
	rhi::helpers::WriteTextureUploadSubresources(plan, srcSpan, mapped, static_cast<uint64_t>(uploadBaseOffset));
	uploadBuffer->GetAPIResource().Unmap(0, 0);

	// Queue up the GPU copies (one per subresource).
	for (const auto& fp : plan.footprints) {

		rhi::CopyableFootprint copyFootprint;
		copyFootprint.offset = static_cast<uint64_t>(uploadBaseOffset) + fp.offset;
		copyFootprint.rowPitch = fp.rowPitch;
		copyFootprint.width = fp.width;
		copyFootprint.height = fp.height;
		copyFootprint.depth = fp.depth;

		TextureUpdate update;
		update.texture = target;
		update.mip = fp.mip;
		update.slice = fp.arraySlice;
		update.footprint = copyFootprint;
		update.x = 0;
		update.y = 0;
		update.z = fp.zSlice;
		update.uploadBuffer = uploadBuffer;
#ifdef _DEBUG
		//update.stackTrace = std::stacktrace::current();
		update.file = file;
		update.line = line;
		update.threadID = std::this_thread::get_id();
#endif
		m_textureUpdates.push_back(std::move(update));
	}
}


void UploadManager::ProcessDeferredReleases(uint8_t frameIndex)
{
	// The page where this frame started uploading
	size_t retiringStart = m_frameStart[frameIndex];

	// Compute the minimum page start across all in flight frames
	size_t minStart = retiringStart;
	for (uint8_t f = 0; f < m_numFramesInFlight; ++f) {
		if (f == frameIndex) continue;
		minStart = std::min(minStart, m_frameStart[f]);
	}

	// Any page with index < minStart is no longer needed by anybody.
	// But leave at least one page alive
	if (minStart > 0) {
		// clamp so we don't delete our last page
		size_t eraseCount = std::min(minStart, m_pages.size() - 1);
		if (eraseCount > 0) {
			m_pages.erase(m_pages.begin(), m_pages.begin() + eraseCount);

			// Shift all of our indices down by eraseCount
			m_activePage -= eraseCount;
			for (auto& start : m_frameStart) {
				start = (start >= eraseCount ? start - eraseCount : 0);
			}
		}
	}

	// Now record "this frame's" new begin page for the next round:
	m_frameStart[frameIndex] = m_activePage;
}

void UploadManager::ProcessUploads(uint8_t frameIndex, rg::imm::ImmediateCommandList& commandList) {

	//rhi::debug::Begin(commandList.Get(), rhi::colors::Amber, "UploadManager::ProcessUploads");

	for (auto& update : m_resourceUpdates) {
		if (!update.active || !update.uploadBuffer || update.size == 0) continue;
		switch(update.resourceToUpdate.kind) {
		case UploadTarget::Kind::PinnedShared:
			commandList.CopyBufferRegion(
				update.resourceToUpdate.pinned,
				update.dataBufferOffset,
				update.uploadBuffer,
				update.uploadBufferOffset,
				update.size
			);
			break;
		case UploadTarget::Kind::RegistryHandle:
			commandList.CopyBufferRegion(
				m_ctx.registry->Resolve(update.resourceToUpdate.h),
				update.dataBufferOffset,
				update.uploadBuffer,
				update.uploadBufferOffset,
				update.size
			);
			break;
			
		}

	}

	for (auto& texUpdate : m_textureUpdates) {
		if (texUpdate.texture.kind == UploadTarget::Kind::PinnedShared) {
			commandList.CopyBufferToTexture(
				texUpdate.uploadBuffer,
				texUpdate.texture.pinned,
				texUpdate.mip,
				texUpdate.slice,
				texUpdate.footprint,
				texUpdate.x,
				texUpdate.y,
				texUpdate.z
			);
		} else {
			commandList.CopyBufferToTexture(
				texUpdate.uploadBuffer,
				m_ctx.registry->Resolve(texUpdate.texture.h),
				texUpdate.mip,
				texUpdate.slice,
				texUpdate.footprint,
				texUpdate.x,
				texUpdate.y,
				texUpdate.z
			);
		}
	}

	m_resourceUpdates.clear();
	m_textureUpdates.clear();
}

void UploadManager::QueueResourceCopy(const std::shared_ptr<Resource>& destination, const std::shared_ptr<Resource>& source, size_t size) {
	ResourceCopy copy;
	copy.source = source;
	copy.destination = destination;
	copy.size = size;
	queuedResourceCopies.push_back(copy);
}

void UploadManager::QueueCopyAndDiscard(
	const std::shared_ptr<Resource>& destination,
	std::unique_ptr<GpuBufferBacking> sourceToDiscard,
	size_t size)
{
	DiscardBufferCopy copy;
	copy.sourceOwned = ExternalBackingResource::CreateShared(std::move(sourceToDiscard));
	copy.destination = destination;
	copy.size = size;

	queuedDiscardCopies.push_back(std::move(copy));
}

void UploadManager::ExecuteResourceCopies(uint8_t frameIndex, rg::imm::ImmediateCommandList& commandList) {
	//rhi::debug::Begin(commandList.Get(), rhi::colors::Amber, "Upload Manager - resource copies");
	for (auto& copy : queuedResourceCopies) {
		// Perform the copy
		commandList.CopyBufferRegion(
			copy.destination,
			0,
			copy.source,
			0,
			copy.size);
	}

	for (auto& copy : queuedDiscardCopies) {
		// Perform the copy
		commandList.CopyBufferRegion(
			copy.destination,
			0,
			copy.sourceOwned,
			0,
			copy.size);

	}

	queuedResourceCopies.clear();
	queuedDiscardCopies.clear(); // Discards owned resources
}

void UploadManager::Cleanup() {
	m_pages.clear();
	m_resourceUpdates.clear();
	m_textureUpdates.clear();
	queuedResourceCopies.clear();
}