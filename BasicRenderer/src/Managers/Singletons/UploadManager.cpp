#include "Managers/Singletons/UploadManager.h"


#include "Resources/Buffers/Buffer.h"
#include "Resources/Resource.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Managers/Singletons/DeletionManager.h"
#include "Utilities/Utilities.h"
#include "Managers/Singletons/DeviceManager.h"

void UploadManager::Initialize() {
	auto& device = DeviceManager::GetInstance().GetDevice();

	m_numFramesInFlight = SettingsManager::GetInstance()
		.getSettingGetter<uint8_t>("numFramesInFlight")();

	m_currentCapacity = 1024 * 1024 * 4; // 4MB

	m_pages.push_back({ Buffer::CreateShared(device.Get(), ResourceCPUAccessType::WRITE, kPageSize, true, false), 0});

	// ring buffer pointers
	m_headOffset = 0;
	m_tailOffset = 0;
	m_frameStart.assign(m_numFramesInFlight, 0);

	// create one allocator+list per frame
	for (int i = 0; i < m_numFramesInFlight; i++) {
		ComPtr<ID3D12CommandAllocator> alloc;
		ComPtr<ID3D12GraphicsCommandList7> list;
		ThrowIfFailed(device->CreateCommandAllocator(
			D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc)));
		ThrowIfFailed(device->CreateCommandList(
			0, D3D12_COMMAND_LIST_TYPE_DIRECT,
			alloc.Get(), nullptr, IID_PPV_ARGS(&list)));
		list->Close();

		m_commandAllocators.push_back(alloc);
		m_commandLists.push_back(list);
	}

	getNumFramesInFlight =
		SettingsManager::GetInstance().getSettingGetter<uint8_t>("numFramesInFlight");

	m_activePage = 0;
	m_frameStart.resize(m_numFramesInFlight, 0);
}

void UploadManager::UploadData(const void* data, size_t size, Resource* resourceToUpdate, size_t dataBufferOffset) {

	if (size > kPageSize) {
		// break it into multiple sub uploads
		size_t done     = 0;
		size_t dstOffset = dataBufferOffset;
		while (done < size) {
			size_t chunk = (std::min)(size - done, kPageSize);
			UploadData(
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
			auto& device = DeviceManager::GetInstance().GetDevice();
			m_pages.push_back({ Buffer::CreateShared(device.Get(), ResourceCPUAccessType::WRITE, allocSize, true, false), 0});
		}
		page = &m_pages[m_activePage];
		page->tailOffset = 0;
	}

	// now we're guaranteed space
	size_t uploadOffset = page->tailOffset;
	page->tailOffset += size;

	uint8_t* mapped = nullptr;
	CD3DX12_RANGE readRange(0,0);
	page->buffer->m_buffer->Map(0, &readRange, reinterpret_cast<void**>(&mapped));
	memcpy(mapped + uploadOffset, data, size);
	page->buffer->m_buffer->Unmap(0, nullptr);

	// queue up the GPU copy
	ResourceUpdate update;
	update.size = size;
	update.resourceToUpdate = resourceToUpdate;
	update.uploadBuffer = page->buffer;
	update.uploadBufferOffset = uploadOffset;
	update.dataBufferOffset = dataBufferOffset;
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

	m_commandAllocators[frameIndex]->Reset();
}

void UploadManager::ProcessUploads(uint8_t frameIndex, ID3D12CommandQueue* queue) {
	auto& commandAllocator = m_commandAllocators[frameIndex];
	auto& commandList = m_commandLists[frameIndex];
	commandList->Reset(commandAllocator.Get(), nullptr);
	//auto maxNumTransitions = m_resourceUpdates.size();
	//std::vector<D3D12_RESOURCE_BARRIER> barriers;
	//barriers.reserve(maxNumTransitions);
	//std::vector<D3D12_RESOURCE_STATES> initialD3D12States;
	//initialD3D12States.reserve(maxNumTransitions);
	//std::vector<ResourceState> initialStates;
	//initialD3D12States.reserve(maxNumTransitions);
	//std::vector<Resource*> deduplicatedResources;
	//deduplicatedResources.reserve(maxNumTransitions);
	//for (auto& update : m_resourceUpdates) {

	//	Resource* buffer = update.resourceToUpdate;
	//	ResourceState initialState = buffer->GetState();
	//	if (buffer->m_uploadInProgress) {
	//		continue;
	//	}
	//	auto startD3D12State = ResourceStateToD3D12(initialState);
	//	D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
	//		buffer->GetAPIResource(),
	//		startD3D12State,
	//		D3D12_RESOURCE_STATE_COPY_DEST
	//	);
	//	barriers.push_back(barrier);
	//	initialD3D12States.push_back(startD3D12State);
	//	initialStates.push_back(initialState);
	//	deduplicatedResources.push_back(buffer);
	//	buffer->m_uploadInProgress = true;
	//}
 //   commandList->ResourceBarrier(barriers.size(), barriers.data());
	for (auto& update : m_resourceUpdates) {
		Resource* buffer = update.resourceToUpdate;
		commandList->CopyBufferRegion(
			buffer->GetAPIResource(),
			update.dataBufferOffset,
			update.uploadBuffer->GetAPIResource(),
			update.uploadBufferOffset,
			update.size
		);
	}

	//int i = 0;
	//for (auto& resource : deduplicatedResources) {
	//	D3D12_RESOURCE_STATES temp;
	//	temp = barriers[i].Transition.StateBefore;
	//	barriers[i].Transition.StateBefore = barriers[i].Transition.StateAfter;
	//	barriers[i].Transition.StateAfter = temp;
	//	i += 1;
	//	resource->m_uploadInProgress = false;

 //   }
	//commandList->ResourceBarrier(barriers.size(), barriers.data());

	commandList->Close();

	ID3D12CommandList* commandLists[] = { commandList.Get() };
	queue->ExecuteCommandLists(1, commandLists);

	m_resourceUpdates.clear();
}

void UploadManager::QueueResourceCopy(const std::shared_ptr<Resource>& destination, const std::shared_ptr<Resource>& source, size_t size) {
	ResourceCopy copy;
    copy.source = source;
    copy.destination = destination;
	copy.size = size;
    queuedResourceCopies.push_back(copy);
}

void UploadManager::ExecuteResourceCopies(uint8_t frameIndex, ID3D12CommandQueue* queue) {
	auto& commandAllocator = m_commandAllocators[frameIndex];
	auto& commandList = m_commandLists[frameIndex];
	commandList->Reset(commandAllocator.Get(), nullptr);
	for (auto& copy : queuedResourceCopies) {
		/*auto startState = ResourceStateToD3D12(copy.source->GetState());
		D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			copy.source->GetAPIResource(),
			startState,
			D3D12_RESOURCE_STATE_COPY_SOURCE
		);
		commandList->ResourceBarrier(1, &barrier);

		barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			copy.destination->GetAPIResource(),
			ResourceStateToD3D12(copy.destination->GetState()),
			D3D12_RESOURCE_STATE_COPY_DEST
		);
		commandList->ResourceBarrier(1, &barrier);*/

		auto sourceIntialAccessType = copy.source->GetCurrentAccessType();
		auto sourceInitialLayout = copy.source->GetCurrentLayout();
		auto sourceInitialSyncState = copy.source->GetPrevSyncState();
		auto destinationInitialAccessType = copy.destination->GetCurrentAccessType();
		auto destinationInitialLayout = copy.destination->GetCurrentLayout();
		auto destinationInitialSyncState = copy.destination->GetPrevSyncState();

		auto& sourceTransition = copy.source->GetEnhancedBarrierGroup(sourceIntialAccessType, ResourceAccessType::COPY_SOURCE, sourceInitialLayout, ResourceLayout::LAYOUT_COPY_SOURCE, sourceInitialSyncState, ResourceSyncState::COPY);
		auto& destinationTransition = copy.destination->GetEnhancedBarrierGroup(destinationInitialAccessType, ResourceAccessType::COPY_DEST, destinationInitialLayout, ResourceLayout::LAYOUT_COPY_DEST, destinationInitialSyncState, ResourceSyncState::COPY);

		std::vector<D3D12_BARRIER_GROUP> barriers;
		barriers.reserve(barriers.size() + sourceTransition.numBufferBarrierGroups + sourceTransition.numTextureBarrierGroups + sourceTransition.numGlobalBarrierGroups + destinationTransition.numBufferBarrierGroups + destinationTransition.numGlobalBarrierGroups + destinationTransition.numTextureBarrierGroups);
		barriers.insert(barriers.end(), sourceTransition.bufferBarriers, sourceTransition.bufferBarriers + sourceTransition.numBufferBarrierGroups);
		barriers.insert(barriers.end(), sourceTransition.textureBarriers, sourceTransition.textureBarriers + sourceTransition.numTextureBarrierGroups);
		barriers.insert(barriers.end(), sourceTransition.globalBarriers, sourceTransition.globalBarriers + sourceTransition.numGlobalBarrierGroups);
		barriers.insert(barriers.end(), destinationTransition.bufferBarriers, destinationTransition.bufferBarriers + destinationTransition.numBufferBarrierGroups);
		barriers.insert(barriers.end(), destinationTransition.textureBarriers, destinationTransition.textureBarriers + destinationTransition.numTextureBarrierGroups);
		barriers.insert(barriers.end(), destinationTransition.globalBarriers, destinationTransition.globalBarriers + destinationTransition.numGlobalBarrierGroups);

		commandList->Barrier(barriers.size(), barriers.data());

        commandList->CopyBufferRegion(
            copy.destination->GetAPIResource(),
            0,
            copy.source->GetAPIResource(),
            0,
            copy.size);

		barriers.clear();

		sourceTransition = copy.source->GetEnhancedBarrierGroup(ResourceAccessType::COPY_SOURCE, sourceIntialAccessType, ResourceLayout::LAYOUT_COPY_SOURCE, sourceInitialLayout, ResourceSyncState::COPY, sourceInitialSyncState);
		destinationTransition = copy.destination->GetEnhancedBarrierGroup(ResourceAccessType::COPY_DEST, destinationInitialAccessType, ResourceLayout::LAYOUT_COPY_DEST, destinationInitialLayout, ResourceSyncState::COPY, destinationInitialSyncState);

		barriers.reserve(barriers.size() + sourceTransition.numBufferBarrierGroups + sourceTransition.numTextureBarrierGroups + sourceTransition.numGlobalBarrierGroups + destinationTransition.numBufferBarrierGroups + destinationTransition.numGlobalBarrierGroups + destinationTransition.numTextureBarrierGroups);
		barriers.insert(barriers.end(), sourceTransition.bufferBarriers, sourceTransition.bufferBarriers + sourceTransition.numBufferBarrierGroups);
		barriers.insert(barriers.end(), sourceTransition.textureBarriers, sourceTransition.textureBarriers + sourceTransition.numTextureBarrierGroups);
		barriers.insert(barriers.end(), sourceTransition.globalBarriers, sourceTransition.globalBarriers + sourceTransition.numGlobalBarrierGroups);
		barriers.insert(barriers.end(), destinationTransition.bufferBarriers, destinationTransition.bufferBarriers + destinationTransition.numBufferBarrierGroups);
		barriers.insert(barriers.end(), destinationTransition.textureBarriers, destinationTransition.textureBarriers + destinationTransition.numTextureBarrierGroups);
			
		barriers.insert(barriers.end(), destinationTransition.globalBarriers, destinationTransition.globalBarriers + destinationTransition.numGlobalBarrierGroups);

		commandList->Barrier(barriers.size(), barriers.data());

		/*barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			copy.source->GetAPIResource(),
			D3D12_RESOURCE_STATE_COPY_SOURCE,
			startState
		);
		commandList->ResourceBarrier(1, &barrier);

		barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			copy.destination->GetAPIResource(),
			D3D12_RESOURCE_STATE_COPY_DEST,
			ResourceStateToD3D12(copy.destination->GetState())
		);
		commandList->ResourceBarrier(1, &barrier);*/
	}
	commandList->Close();

	ID3D12CommandList* commandLists[] = { commandList.Get() };
	queue->ExecuteCommandLists(1, commandLists);

	queuedResourceCopies.clear();
}

void UploadManager::ResetAllocators(uint8_t frameIndex) {
	auto& commandAllocator = m_commandAllocators[frameIndex];
	commandAllocator->Reset();
}