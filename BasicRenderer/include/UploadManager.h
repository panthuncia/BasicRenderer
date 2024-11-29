#pragma once
#include <wrl/client.h>
#include <d3d12.h>

#include "BufferView.h"
#include "MemoryBlock.h"


class ResourceUpdate {
public:
	ResourceUpdate() = default;
	size_t size;
	Resource* resourceToUpdate;
	std::shared_ptr<Resource> uploadBuffer;
	size_t uploadBufferOffset;
	size_t dataBufferOffset;
};

struct ResourceCopy {
	std::shared_ptr<Resource> source;
	std::shared_ptr<Resource> destination;
	size_t size;
};

class UploadManager {
public:
	static UploadManager& GetInstance();
	void Initialize();
	void UploadData(const void* data, size_t size, Resource* resourceToUpdate, uint8_t numResources, size_t dataBufferOffset);
	void ProcessUploads(uint8_t frameIndex, ID3D12CommandQueue* queue);
	void QueueResourceCopy(const std::shared_ptr<Resource>& destination, const std::shared_ptr<Resource>& source, size_t size);
	void ExecuteResourceCopies(ID3D12CommandQueue* queue);
	void ResetAllocators();
private:
	UploadManager() = default;
	void ReleaseData(size_t size, size_t offset);
	void GrowBuffer(size_t newCapacity);

	size_t m_currentCapacity = 0;
	std::shared_ptr<Buffer> m_uploadBuffer;
	std::vector<MemoryBlock> m_memoryBlocks;
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_commandQueue;
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_commandAllocator;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_commandList;
	std::vector<Microsoft::WRL::ComPtr<ID3D12Fence>> m_fences;

	std::function<uint8_t()> getNumFramesInFlight;
	uint8_t m_numFramesInFlight;
	std::vector<std::vector<ResourceUpdate>> m_frameResourceUpdates;

	std::vector<ResourceCopy> queuedResourceCopies;

};

inline UploadManager& UploadManager::GetInstance() {
	static UploadManager instance;
	return instance;
}