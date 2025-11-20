#include "Managers/Singletons/UploadManager.h"

#include <rhi_helpers.h>
#include <rhi_debug.h>

#include "Resources/Buffers/Buffer.h"
#include "Resources/Resource.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Managers/Singletons/DeletionManager.h"
#include "Utilities/Utilities.h"
#include "Managers/Singletons/DeviceManager.h"

void UploadManager::Initialize() {
	auto device = DeviceManager::GetInstance().GetDevice();

	m_numFramesInFlight = SettingsManager::GetInstance()
		.getSettingGetter<uint8_t>("numFramesInFlight")();

	m_currentCapacity = 1024 * 1024 * 4; // 4MB

	m_pages.push_back({ Buffer::CreateShared(device, rhi::Memory::Upload, kPageSize, false), 0});

	// ring buffer pointers
	m_headOffset = 0;
	m_tailOffset = 0;
	m_frameStart.assign(m_numFramesInFlight, 0);

	// create one allocator+list per frame
	for (int i = 0; i < m_numFramesInFlight; i++) {
		m_commandAllocators.push_back(device.CreateCommandAllocator(rhi::QueueKind::Graphics));
		m_commandLists.push_back(device.CreateCommandList(rhi::QueueKind::Graphics, m_commandAllocators.back().Get()));
		m_commandLists.back()->End();
	}

	getNumFramesInFlight =
		SettingsManager::GetInstance().getSettingGetter<uint8_t>("numFramesInFlight");

	m_activePage = 0;
	m_frameStart.resize(m_numFramesInFlight, 0);
}

#ifdef _DEBUG
void UploadManager::UploadData(const void* data, size_t size, Resource* resourceToUpdate, size_t dataBufferOffset, const char* file, int line)
#else
void UploadManager::UploadData(const void* data, size_t size, Resource* resourceToUpdate, size_t dataBufferOffset)
#endif 
{
	if (size > kPageSize) {
		// break it into multiple sub uploads
		size_t done     = 0;
		size_t dstOffset = dataBufferOffset;
		while (done < size) {
			size_t chunk = (std::min)(size - done, kPageSize);
			QUEUE_UPLOAD(
				reinterpret_cast<const uint8_t*>(data) + done,
				chunk,
				resourceToUpdate,
				dstOffset
			);
			done     += chunk;
			dstOffset += chunk;
		}
		return;
	}

	UploadPage* page = &m_pages[m_activePage];

	// if it won’t fit in the rest of this page, open a new page
	if (page->tailOffset + size > page->buffer->GetSize()) {
		++m_activePage;
		if (m_activePage >= m_pages.size()) {
			// allocate another fresh page
			size_t allocSize = std::max(kPageSize, size);
			auto device = DeviceManager::GetInstance().GetDevice();
			m_pages.push_back({ Buffer::CreateShared(device, rhi::Memory::Upload, allocSize, false), 0});
		}
		page = &m_pages[m_activePage];
		page->tailOffset = 0;
	}

	// now we're guaranteed space
	size_t uploadOffset = page->tailOffset;
	page->tailOffset += size;

	uint8_t* mapped = nullptr;
	page->buffer->m_buffer->Map(reinterpret_cast<void**>(&mapped), 0, size);
	memcpy(mapped + uploadOffset, data, size);
	page->buffer->m_buffer->Unmap(0, 0);

	// queue up the GPU copy
	ResourceUpdate update;
	update.size = size;
	update.resourceToUpdate = resourceToUpdate;
	update.uploadBuffer = page->buffer;
	update.uploadBufferOffset = uploadOffset;
	update.dataBufferOffset = dataBufferOffset;
#ifdef _DEBUG
	update.resourceID = resourceToUpdate ? resourceToUpdate->GetGlobalResourceID() : ~0ull;
	update.file = file;
	update.line = line;
	update.frameIndex = (m_numFramesInFlight ? (uint8_t)(GetInstance().getNumFramesInFlight()) : 0);
	update.threadID = std::this_thread::get_id();
#ifdef _WIN32
	void* frames[ResourceUpdate::MaxStack];
	USHORT captured = RtlCaptureStackBackTrace(1, ResourceUpdate::MaxStack, frames, nullptr);
	update.stackSize = (uint8_t)captured;
	for (USHORT i = 0; i < captured; i++) update.stack[i] = frames[i];
#endif
#endif
	m_resourceUpdates.push_back(update);
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

			// 4) Shift all of our indices down by eraseCount
			m_activePage -= eraseCount;
			for (auto &start : m_frameStart) {
				start = (start >= eraseCount ? start - eraseCount : 0);
			}
		}
	}

	// Now record "this frame's" new begin page for the next round:
	m_frameStart[frameIndex] = m_activePage;

	m_commandAllocators[frameIndex]->Recycle();
}

void UploadManager::ProcessUploads(uint8_t frameIndex, rhi::Queue queue) {
	auto& commandAllocator = m_commandAllocators[frameIndex];
	auto& commandList = m_commandLists[frameIndex];
	commandList->Recycle(commandAllocator.Get());
	rhi::debug::Begin(commandList.Get(), rhi::colors::Amber, "UploadManager::ProcessUploads");
	// TODO: Should we do any barriers here?
	unsigned int i = 0;
	for (auto& update : m_resourceUpdates) {
		Resource* buffer = update.resourceToUpdate;
		commandList->CopyBufferRegion(
			buffer->GetAPIResource().GetHandle(),
			update.dataBufferOffset,
			update.uploadBuffer->GetAPIResource().GetHandle(),
			update.uploadBufferOffset,
			update.size
		);
		++i;
	}
	rhi::debug::End(commandList.Get());

	commandList->End();

	queue.Submit({ &commandList.Get()});

	m_resourceUpdates.clear();
}

void UploadManager::QueueResourceCopy(const std::shared_ptr<Resource>& destination, const std::shared_ptr<Resource>& source, size_t size) {
	ResourceCopy copy;
    copy.source = source;
    copy.destination = destination;
	copy.size = size;
    queuedResourceCopies.push_back(copy);
}

void UploadManager::ExecuteResourceCopies(uint8_t frameIndex, rhi::Queue queue) {
	auto& commandAllocator = m_commandAllocators[frameIndex];
	auto& commandList = m_commandLists[frameIndex];
	commandList->Recycle(commandAllocator.Get());
	rhi::debug::Begin(commandList.Get(), rhi::colors::Amber, "Upload Manager - resource copies");
	for (auto& copy : queuedResourceCopies) {

		// Copy initial state of the resources
		auto initialSourceState = copy.source->GetStateTracker()->GetSegments();
		auto initialDestinationState = copy.destination->GetStateTracker()->GetSegments();

		RangeSpec range = {};
		ResourceState sourceState;
		sourceState.access = rhi::ResourceAccessType::CopySource;
		sourceState.layout = rhi::ResourceLayout::CopySource;
		sourceState.sync = rhi::ResourceSyncState::Copy;

		ResourceState destinationState;
		destinationState.access = rhi::ResourceAccessType::CopyDest;
		destinationState.layout = rhi::ResourceLayout::CopyDest;
		destinationState.sync = rhi::ResourceSyncState::Copy;

		// Compute transitions to bring full resources into COPY_SOURCE and COPY_DEST
		std::vector<ResourceTransition> transitions;
		copy.source->GetStateTracker()->Apply(range, copy.source.get(), sourceState, transitions);
		copy.destination->GetStateTracker()->Apply(range, copy.destination.get(), destinationState, transitions);

		std::vector<D3D12_BARRIER_GROUP> barriers;
		std::vector<rhi::BarrierBatch> sourceTransitions;
		for (auto& transition : transitions) {
			auto sourceTransition = transition.pResource->GetEnhancedBarrierGroup(transition.range, transition.prevAccessType, transition.newAccessType, transition.prevLayout, transition.newLayout, transition.prevSyncState, transition.newSyncState);
			sourceTransitions.push_back(sourceTransition);
		}
		
		auto batch = rhi::helpers::CombineBarrierBatches(sourceTransitions);

		commandList->Barriers(batch.View());

		// Perform the copy
        commandList->CopyBufferRegion(
            copy.destination->GetAPIResource().GetHandle(),
            0,
            copy.source->GetAPIResource().GetHandle(),
            0,
            copy.size);

		barriers.clear();
		transitions.clear();

		// Copy back the initial state of the resources
		for (auto& segment : initialSourceState) {
			copy.source->GetStateTracker()->Apply(segment.rangeSpec, copy.source.get(), segment.state, transitions);
		}
		for (auto& segment : initialDestinationState) {
			copy.destination->GetStateTracker()->Apply(segment.rangeSpec, copy.destination.get(), segment.state, transitions);
		}

		sourceTransitions.clear();
		batch.Clear();
		for (auto& transition : transitions) {
			auto sourceTransition = transition.pResource->GetEnhancedBarrierGroup(transition.range, transition.prevAccessType, transition.newAccessType, transition.prevLayout, transition.newLayout, transition.prevSyncState, transition.newSyncState);
			sourceTransitions.push_back(std::move(sourceTransition));
		}
		
		batch = rhi::helpers::CombineBarrierBatches(sourceTransitions);

		commandList->Barriers(batch.View());
	}
	rhi::debug::End(commandList.Get());
	commandList->End();

	queue.Submit({ &commandList.Get() });

	queuedResourceCopies.clear();
}

void UploadManager::ResetAllocators(uint8_t frameIndex) {
	auto& commandAllocator = m_commandAllocators[frameIndex];
	commandAllocator->Recycle();
}