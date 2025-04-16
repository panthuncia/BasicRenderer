#pragma once
#include <wrl/client.h>
#include <d3d12.h>
#include <vector>
#include <memory>
#include <functional>

#include "Resources/Buffers/BufferView.h"
#include "Resources/Buffers/MemoryBlock.h"

class Buffer;
class Resource;

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

struct ReleaseRequest {
	size_t size;
	uint64_t offset;
};

class UploadManager {
public:
	static UploadManager& GetInstance();
	void Initialize();
	void UploadData(const void* data, size_t size, Resource* resourceToUpdate, size_t dataBufferOffset);
	void ProcessUploads(uint8_t frameIndex, ID3D12CommandQueue* queue);
	void QueueResourceCopy(const std::shared_ptr<Resource>& destination, const std::shared_ptr<Resource>& source, size_t size);
	void ExecuteResourceCopies(uint8_t frameIndex, ID3D12CommandQueue* queue);
	void ResetAllocators(uint8_t frameIndex);
	void ProcessDeferredReleases(uint8_t frameIndex);

private:
	UploadManager() = default;
	void ReleaseData(size_t size, size_t offset);
	void GrowBuffer(size_t newCapacity);

	size_t m_currentCapacity = 0;
	std::shared_ptr<Buffer> m_uploadBuffer;
	std::vector<MemoryBlock> m_memoryBlocks;
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_commandQueue;
	std::vector<Microsoft::WRL::ComPtr<ID3D12CommandAllocator>> m_commandAllocators;
	std::vector<Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>> m_commandLists;

	std::function<uint8_t()> getNumFramesInFlight;
	std::vector<ResourceUpdate> m_resourceUpdates;
	std::vector<std::vector<ReleaseRequest>> m_pendingReleases;

	std::vector<ResourceCopy> queuedResourceCopies;

	uint8_t m_numFramesInFlight = 0;

};

inline UploadManager& UploadManager::GetInstance() {
	static UploadManager instance;
	return instance;
}