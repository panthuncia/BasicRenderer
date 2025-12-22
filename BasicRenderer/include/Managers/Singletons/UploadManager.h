#pragma once
#include <wrl/client.h>
#include <vector>
#include <memory>
#include <functional>
#include <rhi.h>
#include <thread>

#include "rhi_helpers.h"
#include "Resources/ResourceStateTracker.h"
#include "Resources/Buffers/GpuBufferBacking.h"

class Buffer;
class Resource;

class ResourceUpdate {
public:
	ResourceUpdate() = default;
	size_t size{};
	std::shared_ptr<Resource> resourceToUpdate{};
	std::shared_ptr<Resource> uploadBuffer;
	size_t uploadBufferOffset{};
	size_t dataBufferOffset{};
	bool active = true;
#ifdef _DEBUG
	uint64_t resourceID{};
	const char* file{};
	int line{};
	uint8_t frameIndex{};
	std::thread::id threadID;
	static constexpr int MaxStack = 8;
	void* stack[MaxStack]{};
	uint8_t stackSize{};
#endif
};

class TextureUpdate {
public:
	TextureUpdate() = default;
	rhi::BufferTextureCopyFootprint copy{};
	std::shared_ptr<Resource> uploadBuffer;
#ifdef _DEBUG
	const char* file{};
	int line{};
	std::thread::id threadID;
#endif
};

#ifdef _DEBUG
#define BUFFER_UPLOAD(data,size,res,offset) \
    UploadManager::GetInstance().UploadData((data),(size),(res),(offset),__FILE__,__LINE__)
#define TEXTURE_UPLOAD_SUBRESOURCES(dstTexture,fmt,baseWidth,baseHeight,depthOrLayers,mipLevels,arraySize,srcSubresources,srcCount) \
	UploadManager::GetInstance().UploadTextureSubresources((dstTexture),(fmt),(baseWidth),(baseHeight),(depthOrLayers),(mipLevels),(arraySize),(srcSubresources),(srcCount),__FILE__,__LINE__)
#else
#define BUFFER_UPLOAD(data,size,res,offset) \
    UploadManager::GetInstance().UploadData((data),(size),(res),(offset))
#define TEXTURE_UPLOAD_SUBRESOURCES(dstTexture,fmt,baseWidth,baseHeight,depthOrLayers,mipLevels,arraySize,srcSubresources,srcCount) \
	UploadManager::GetInstance().UploadTextureSubresources((dstTexture),(fmt),(baseWidth),(baseHeight),(depthOrLayers),(mipLevels),(arraySize),(srcSubresources),(srcCount))
#endif

struct ResourceCopy {
	std::shared_ptr<Resource> source;
	std::shared_ptr<Resource> destination;
	size_t size;
};

struct DiscardBufferCopy {
	std::unique_ptr<GpuBufferBacking> sourceOwned; // keeps old backing alive until executed
	SymbolicTracker 			sourceBarrierState; // Needed to transition old resource before copy
	std::shared_ptr<Resource>         destination; // new resource
	size_t                            size = 0;
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
#ifdef _DEBUG
	void UploadData(const void* data, size_t size, std::shared_ptr<Resource> resourceToUpdate, size_t dataBufferOffset, const char* file, int line);
	void UploadTextureSubresources(
		rhi::Resource& dstTexture,
		rhi::Format fmt,
		uint32_t baseWidth,
		uint32_t baseHeight,
		uint32_t depthOrLayers,
		uint32_t mipLevels,
		uint32_t arraySize,
		const rhi::helpers::SubresourceData* srcSubresources,
		uint32_t srcCount,
		const char* file,
		int line);
#else
	void UploadData(const void* data, size_t size, std::shared_ptr<Resource> resourceToUpdate, size_t dataBufferOffset);
	void UploadTextureSubresources(
		rhi::Resource& dstTexture,
		rhi::Format fmt,
		uint32_t baseWidth,
		uint32_t baseHeight,
		uint32_t depthOrLayers,
		uint32_t mipLevels,
		uint32_t arraySize,
		const rhi::helpers::SubresourceData* srcSubresources,
		uint32_t srcCount);
#endif	
	void ProcessUploads(uint8_t frameIndex, rhi::Queue queue);
	void QueueResourceCopy(const std::shared_ptr<Resource>& destination, const std::shared_ptr<Resource>& source, size_t size);
	void QueueCopyAndDiscard(const std::shared_ptr<Resource>& destination,
		std::unique_ptr<GpuBufferBacking> sourceToDiscard,
		SymbolicTracker sourceBarrierState,
		size_t size);
	void ExecuteResourceCopies(uint8_t frameIndex, rhi::Queue queue);
	void ResetAllocators(uint8_t frameIndex);
	void ProcessDeferredReleases(uint8_t frameIndex);
	void Cleanup();
private:
	UploadManager() = default;
	bool AllocateUploadRegion(size_t size, size_t alignment, std::shared_ptr<Resource>& outUploadBuffer, size_t& outOffset);

	// Coalescing / last-write-wins helpers
	static bool RangesOverlap(size_t a0, size_t a1, size_t b0, size_t b1) noexcept;
	static bool RangeContains(size_t outer0, size_t outer1, size_t inner0, size_t inner1) noexcept;

	static bool TryCoalesceAppend(ResourceUpdate& last, const ResourceUpdate& next) noexcept;
	bool TryPatchContainedUpdate(const void* src, size_t size, std::shared_ptr<Resource> resourceToUpdate, size_t dataBufferOffset
#ifdef _DEBUG
		, const char* file, int line
#endif
	) noexcept;

	// Mutates newUpdate (may expand into a union update); may mark old updates inactive; may deactivate newUpdate if patched into an older containing update.
	void ApplyLastWriteWins(ResourceUpdate& newUpdate) noexcept;

	static void MapUpload(const std::shared_ptr<Resource>& uploadBuffer, size_t mapSize, uint8_t** outMapped) noexcept;
	static void UnmapUpload(const std::shared_ptr<Resource>& uploadBuffer) noexcept;

	size_t                 m_currentCapacity = 0;
	size_t                 m_headOffset = 0;   // oldest in flight allocation
	size_t                 m_tailOffset = 0;   // where next allocation comes from
	std::vector<UploadPage>    m_pages;
	size_t                     m_activePage = 0;
	static constexpr size_t    kPageSize = 256 * 1024 * 1024; // 256 MB
	static constexpr size_t    kMaxPageSize = 4294967296; // 4 GB
	static constexpr size_t	   maxSingleUploadSize = 4294967296; // 4 GB
	std::vector<size_t>           m_frameStart;      // size = numFramesInFlight

	uint8_t m_numFramesInFlight = 0;
	rhi::Queue m_commandQueue;
	std::vector<rhi::CommandAllocatorPtr> m_commandAllocators;
	std::vector<rhi::CommandListPtr> m_commandLists;

	std::function<uint8_t()> getNumFramesInFlight;
	std::vector<ResourceUpdate> m_resourceUpdates;
	std::vector<TextureUpdate> m_textureUpdates;

	std::vector<ResourceCopy> queuedResourceCopies;
	std::vector<DiscardBufferCopy> queuedDiscardCopies;
};

inline UploadManager& UploadManager::GetInstance() {
	static UploadManager instance;
	return instance;
}