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

struct UploadPage {
	std::shared_ptr<Buffer> buffer;
	size_t                  tailOffset = 0;
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

	size_t                 m_currentCapacity = 0;
	size_t                 m_headOffset      = 0;   // oldest in flight allocation
	size_t                 m_tailOffset      = 0;   // where next allocation comes from
	std::vector<UploadPage>    m_pages;
	size_t                     m_activePage = 0;
	static constexpr size_t    kPageSize    = 256 * 1024 * 1024; // 256 MB
	static constexpr size_t    kMaxPageSize = 4294967296; // 4 GB
	static constexpr size_t	   maxSingleUploadSize = 4294967296; // 4 GB
	std::vector<size_t>           m_frameStart;      // size = numFramesInFlight

	uint8_t m_numFramesInFlight = 0;
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_commandQueue;
	std::vector<Microsoft::WRL::ComPtr<ID3D12CommandAllocator>> m_commandAllocators;
	std::vector<Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList7>> m_commandLists;

	std::function<uint8_t()> getNumFramesInFlight;
	std::vector<ResourceUpdate> m_resourceUpdates;

	std::vector<ResourceCopy> queuedResourceCopies;
};

inline UploadManager& UploadManager::GetInstance() {
	static UploadManager instance;
	return instance;
}