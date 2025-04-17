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
	m_uploadBuffer = Buffer::CreateShared(
		device.Get(),
		ResourceCPUAccessType::WRITE,
		m_currentCapacity,
		/*isUpload*/true,
		/*isReadBack*/false
	);

	// ring buffer pointers
	m_headOffset = 0;
	m_tailOffset = 0;
	m_frameStart.assign(m_numFramesInFlight, 0);

	// create one allocator+list per frame
	for (int i = 0; i < m_numFramesInFlight; i++) {
		ComPtr<ID3D12CommandAllocator> alloc;
		ComPtr<ID3D12GraphicsCommandList> list;
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
}

void UploadManager::UploadData(const void* data, size_t size, Resource* resourceToUpdate, size_t dataBufferOffset) {
	// check space before wrap
	auto freeAtEnd = (m_headOffset > m_tailOffset)
		? m_headOffset - m_tailOffset
		: m_currentCapacity - m_tailOffset;

	size_t uploadOffset = 0;
	if (freeAtEnd >= size) {
		// allocate straight
		uploadOffset = m_tailOffset;
		m_tailOffset += size;
	} else {
		// try wrapping to front
		if (m_headOffset < size) {
			// not enough free space anywhere, grow
			GrowBuffer(m_currentCapacity + (std::max)(m_currentCapacity, size));
			return UploadData(data, size, resourceToUpdate, dataBufferOffset);
		}
		// wrap
		uploadOffset = 0;
		m_tailOffset = size;
	}

	uint8_t* mapped = nullptr;
	CD3DX12_RANGE readRange(0,0);
	m_uploadBuffer->m_buffer->Map(0, &readRange, reinterpret_cast<void**>(&mapped));
	memcpy(mapped + uploadOffset, data, size);
	m_uploadBuffer->m_buffer->Unmap(0, nullptr);

	// queue up the GPU copy
	ResourceUpdate update;
	update.size = size;
	update.resourceToUpdate = resourceToUpdate;
	update.uploadBuffer = m_uploadBuffer;
	update.uploadBufferOffset = uploadOffset;
	update.dataBufferOffset = dataBufferOffset;
	m_resourceUpdates.push_back(update);
}

void UploadManager::ProcessDeferredReleases(uint8_t frameIndex) {
	// reclaim the region that frameIndex last allocated
	m_headOffset = m_frameStart[frameIndex];

	// record the start of this frame’s allocations
	m_frameStart[frameIndex] = m_tailOffset;

	m_commandAllocators[frameIndex]->Reset();
}

void UploadManager::GrowBuffer(size_t newCapacity) {
    auto& device = DeviceManager::GetInstance().GetDevice();

    DeletionManager::GetInstance().MarkForDelete(m_uploadBuffer);
    m_uploadBuffer = Buffer::CreateShared(device.Get(), ResourceCPUAccessType::WRITE, newCapacity, true, false);

	m_currentCapacity = newCapacity;
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