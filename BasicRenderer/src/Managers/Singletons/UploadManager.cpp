#include "Managers/Singletons/UploadManager.h"


#include "Buffers/Buffer.h"
#include "Resource.h"
#include "SettingsManager.h"
#include "Managers/Singletons/DeletionManager.h"
#include "utilities.h"
#include "Managers/Singletons/DeviceManager.h"

void UploadManager::Initialize() {
	auto& device = DeviceManager::GetInstance().GetDevice();
    unsigned int initialCapacity = 10000;
	m_currentCapacity = initialCapacity;
	m_uploadBuffer = Buffer::CreateShared(device.Get(), ResourceCPUAccessType::WRITE, initialCapacity, true, false);
    m_memoryBlocks.push_back({ 0, initialCapacity, true });
    auto& manager = DeviceManager::GetInstance();

	m_numFramesInFlight = SettingsManager::GetInstance().getSettingGetter<uint8_t>("numFramesInFlight")();
	m_pendingReleases.resize(m_numFramesInFlight);
	for (int i = 0; i < m_numFramesInFlight; i++) {
		ComPtr<ID3D12CommandAllocator> allocator;
		ComPtr<ID3D12GraphicsCommandList> commandList;
		ThrowIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
		ThrowIfFailed(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&commandList)));
		commandList->Close();
		m_commandAllocators.push_back(allocator);
		m_commandLists.push_back(commandList);
	}

    getNumFramesInFlight = SettingsManager::GetInstance().getSettingGetter<uint8_t>("numFramesInFlight");
    m_numFramesInFlight = getNumFramesInFlight();
}

void UploadManager::UploadData(const void* data, size_t size, Resource* resourceToUpdate, size_t dataBufferOffset) {
	//auto view = BufferView::CreateShared(m_uploadBuffer.get(), 0, size, typeid(T));

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

			// Copy the data to the upload buffer
            unsigned char* mappedData = nullptr;
			CD3DX12_RANGE readRange(0, 0);
			m_uploadBuffer->m_buffer->Map(0, &readRange, reinterpret_cast<void**>(&mappedData));

			memcpy(mappedData + offset, data, size);

			m_uploadBuffer->m_buffer->Unmap(0, nullptr);


			ResourceUpdate update;
			update.size = size;
			update.resourceToUpdate = resourceToUpdate;
			update.uploadBuffer = m_uploadBuffer;
			update.uploadBufferOffset = offset;
			update.dataBufferOffset = dataBufferOffset;
			m_resourceUpdates.push_back(update);

			return;
        }
    }

    // No suitable block found, need to grow the buffer

    // Delete the last block if it is free
    size_t newBlockSize = (std::max)(m_currentCapacity, requiredSize);
    size_t growBy = newBlockSize;
    if (!m_memoryBlocks.empty() && m_memoryBlocks.back().isFree)
    {
        growBy -= m_memoryBlocks.back().size;
        m_memoryBlocks.pop_back();
    }
    size_t newCapacity = m_currentCapacity + growBy;

    GrowBuffer(newCapacity);
    //spdlog::info("Growing buffer to {} bytes", newCapacity);
    // Try allocating again
    UploadData(data, size, resourceToUpdate, dataBufferOffset);
}

void UploadManager::ReleaseData(size_t size, size_t offset) {

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

void UploadManager::ProcessDeferredReleases(uint8_t frameIndex) {
	for (const auto& request : m_pendingReleases[frameIndex]) {
		ReleaseData(request.size, request.offset);
	}
	m_pendingReleases[frameIndex].clear();
}

void UploadManager::GrowBuffer(size_t newCapacity) {
    auto& device = DeviceManager::GetInstance().GetDevice();

    DeletionManager::GetInstance().MarkForDelete(m_uploadBuffer);
    m_uploadBuffer = Buffer::CreateShared(device.Get(), ResourceCPUAccessType::WRITE, newCapacity, true, false);
    m_memoryBlocks.clear();
    m_memoryBlocks.push_back({ 0, newCapacity, true });
	m_currentCapacity = newCapacity;
	for (auto& vec : m_pendingReleases) {
		vec.clear(); // Pending releases are no longer valid
	}
}

void UploadManager::ProcessUploads(uint8_t frameIndex, ID3D12CommandQueue* queue) {
	auto& commandAllocator = m_commandAllocators[frameIndex];
	auto& commandList = m_commandLists[frameIndex];
	commandList->Reset(commandAllocator.Get(), nullptr);
	auto maxNumTransitions = m_resourceUpdates.size();
	std::vector<D3D12_RESOURCE_BARRIER> barriers;
	barriers.reserve(maxNumTransitions);
	std::vector<D3D12_RESOURCE_STATES> initialD3D12States;
	initialD3D12States.reserve(maxNumTransitions);
	std::vector<ResourceState> initialStates;
	initialD3D12States.reserve(maxNumTransitions);
	std::vector<Resource*> deduplicatedResources;
	deduplicatedResources.reserve(maxNumTransitions);
	for (auto& update : m_resourceUpdates) {

		Resource* buffer = update.resourceToUpdate;
		ResourceState initialState = buffer->GetState();
		if (buffer->m_uploadInProgress) {
			continue;
		}
		auto startD3D12State = ResourceStateToD3D12(initialState);
		D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			buffer->GetAPIResource(),
			startD3D12State,
			D3D12_RESOURCE_STATE_COPY_DEST
		);
		barriers.push_back(barrier);
		initialD3D12States.push_back(startD3D12State);
		initialStates.push_back(initialState);
		deduplicatedResources.push_back(buffer);
		buffer->m_uploadInProgress = true;
	}
    commandList->ResourceBarrier(barriers.size(), barriers.data());
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

	int i = 0;
	for (auto& resource : deduplicatedResources) {
		D3D12_RESOURCE_STATES temp;
		temp = barriers[i].Transition.StateBefore;
		barriers[i].Transition.StateBefore = barriers[i].Transition.StateAfter;
		barriers[i].Transition.StateAfter = temp;
		i += 1;
		resource->m_uploadInProgress = false;

    }
	commandList->ResourceBarrier(barriers.size(), barriers.data());

	commandList->Close();

	ID3D12CommandList* commandLists[] = { commandList.Get() };
	queue->ExecuteCommandLists(1, commandLists);

	for (auto& update : m_resourceUpdates) {
		if (update.uploadBuffer == m_uploadBuffer) { // If these are not the same, the upload buffer was reallocated and will be deleted anyway
			m_pendingReleases[frameIndex].push_back({ update.size, update.uploadBufferOffset });
		}
	}
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
		auto startState = ResourceStateToD3D12(copy.source->GetState());
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
		commandList->ResourceBarrier(1, &barrier);

        commandList->CopyBufferRegion(
            copy.destination->GetAPIResource(),
            0,
            copy.source->GetAPIResource(),
            0,
            copy.size);

		barrier = CD3DX12_RESOURCE_BARRIER::Transition(
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
		commandList->ResourceBarrier(1, &barrier);
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