#pragma once
#include <wrl/client.h>
#include <vector>
#include <memory>
#include <functional>
#include <rhi.h>
#include <thread>

#include "rhi_helpers.h"
#include "Resources/ResourceStateTracker.h"
#include "Resources/GPUBacking/GpuBufferBacking.h"
#include "Render/ResourceRegistry.h"
#include "RenderPasses/Base/RenderPass.h"

class Buffer;
class Resource;
class ExternalBackingResource;

#if BUILD_TYPE == BUILD_TYPE_DEBUG
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
	std::shared_ptr<ExternalBackingResource> sourceOwned; // keeps old backing alive until executed
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

	struct UploadResolveContext {
		ResourceRegistry* registry = nullptr;
		uint64_t epoch = 0; // optional
	};

	struct UploadTarget {
		enum class Kind { RegistryHandle, PinnedShared };

		Kind kind{};
		ResourceRegistry::RegistryHandle h{};
		std::shared_ptr<Resource> pinned{};   // keep-alive for non-registry targets

		static UploadTarget FromHandle(ResourceRegistry::RegistryHandle handle) { // registry lookup
			UploadTarget t; t.kind = Kind::RegistryHandle; t.h = handle; return t;
		}
		static UploadTarget FromShared(std::shared_ptr<Resource> p) { // keep-alive
			UploadTarget t; t.kind = Kind::PinnedShared; t.pinned = std::move(p); return t;
		}

		bool operator ==(const UploadTarget& other) const {
			if (kind != other.kind) return false;
			if (kind == Kind::RegistryHandle) {
				return h.GetKey().idx == other.h.GetKey().idx && 
					h.GetGeneration() == other.h.GetGeneration() && 
					h.GetEpoch() == other.h.GetEpoch();
			}
			else {
				return pinned == other.pinned;
			}
		}
	};

	class ResourceUpdate {
	public:
		ResourceUpdate() = default;
		size_t size{};
		UploadTarget resourceToUpdate{};
		std::shared_ptr<Resource> uploadBuffer;
		size_t uploadBufferOffset{};
		size_t dataBufferOffset{};
		bool active = true;
#if BUILD_TYPE == BUILD_TYPE_DEBUG
		uint64_t resourceIDOrRegistryIndex{};
		UploadTarget::Kind targetKind{};
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
		Resource* texture;
		uint32_t mip;
		uint32_t slice;
		rhi::CopyableFootprint footprint;
		uint32_t x;
		uint32_t y;
		uint32_t z;
		std::shared_ptr<Resource> uploadBuffer;
#if BUILD_TYPE == BUILD_TYPE_DEBUG
		const char* file{};
		int line{};
		std::thread::id threadID;
#endif
	};

	static UploadManager& GetInstance();
	void Initialize();
#if BUILD_TYPE == BUILD_TYPE_DEBUG
	void UploadData(const void* data, size_t size, UploadTarget resourceToUpdate, size_t dataBufferOffset, const char* file, int line);
	void UploadTextureSubresources(
		Resource*,
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
	void UploadData(const void* data, size_t size, UploadTarget resourceToUpdate, size_t dataBufferOffset);
	void UploadTextureSubresources(
		Resource* dstTexture,
		rhi::Format fmt,
		uint32_t baseWidth,
		uint32_t baseHeight,
		uint32_t depthOrLayers,
		uint32_t mipLevels,
		uint32_t arraySize,
		const rhi::helpers::SubresourceData* srcSubresources,
		uint32_t srcCount);
#endif	
	void ProcessUploads(uint8_t frameIndex, rg::imm::ImmediateCommandList& commandList);
	void QueueResourceCopy(const std::shared_ptr<Resource>& destination, const std::shared_ptr<Resource>& source, size_t size);
	void QueueCopyAndDiscard(const std::shared_ptr<Resource>& destination,
		std::unique_ptr<GpuBufferBacking> sourceToDiscard,
		size_t size);
	void ExecuteResourceCopies(uint8_t frameIndex, rg::imm::ImmediateCommandList& commandList);
	void ProcessDeferredReleases(uint8_t frameIndex);
	void SetUploadResolveContext(UploadResolveContext ctx) { m_ctx = ctx; }
	std::shared_ptr<RenderPass> GetUploadPass() const { return m_uploadPass; }
	void Cleanup();
private:

	class UploadPass : public RenderPass {
	public:
		UploadPass() {
		}

		void Setup() override {

		}

		void ExecuteImmediate(ImmediateContext& context) override {

			GetInstance().ExecuteResourceCopies(context.frameIndex, context.list);// copies come before uploads to avoid overwriting data
			GetInstance().ProcessUploads(context.frameIndex, context.list);
		}

		PassReturn Execute(RenderContext& context) override {
			return {};
		}

		void Cleanup(RenderContext& context) override {
			// Cleanup if necessary
		}

		void SetReadbackFence(rhi::Timeline fence) {
			m_readbackFence = fence;
		}

	private:
		rhi::Timeline m_readbackFence;
		UINT64 m_fenceValue = 0;
	};

	UploadManager() {
		m_uploadPass = std::make_shared<UploadPass>();
	}
	bool AllocateUploadRegion(size_t size, size_t alignment, std::shared_ptr<Resource>& outUploadBuffer, size_t& outOffset);

	// Coalescing / last-write-wins helpers
	static bool RangesOverlap(size_t a0, size_t a1, size_t b0, size_t b1) noexcept;
	static bool RangeContains(size_t outer0, size_t outer1, size_t inner0, size_t inner1) noexcept;

	static bool TryCoalesceAppend(ResourceUpdate& last, const ResourceUpdate& next) noexcept;

	// Mutates newUpdate (may expand into a union update); may mark old updates inactive; may deactivate newUpdate if patched into an older containing update.
	void ApplyLastWriteWins(ResourceUpdate& newUpdate) noexcept;

	static void MapUpload(const std::shared_ptr<Resource>& uploadBuffer, size_t mapSize, uint8_t** outMapped) noexcept;
	static void UnmapUpload(const std::shared_ptr<Resource>& uploadBuffer) noexcept;

	Resource* ResolveTarget(const UploadTarget& t) {
		if (t.kind == UploadTarget::Kind::PinnedShared) return t.pinned.get();

		// Registry handle
		if (!m_ctx.registry) throw std::runtime_error("UploadManager has no registry context this frame");
		return m_ctx.registry->Resolve(t.h); // or view->Resolve(h)
	}

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

	std::function<uint8_t()> getNumFramesInFlight;
	std::vector<ResourceUpdate> m_resourceUpdates;
	std::vector<TextureUpdate> m_textureUpdates;

	std::vector<ResourceCopy> queuedResourceCopies;
	std::vector<DiscardBufferCopy> queuedDiscardCopies;

	UploadResolveContext m_ctx{};
	std::shared_ptr<UploadPass> m_uploadPass;

};

inline UploadManager& UploadManager::GetInstance() {
	static UploadManager instance;
	return instance;
}