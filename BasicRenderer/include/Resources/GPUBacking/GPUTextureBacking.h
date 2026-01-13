#pragma once
#include <rhi.h>
#include <stacktrace>
#include <mutex>

#include "ThirdParty/stb/stb_image.h"

#include "Resources/TextureDescription.h"
#include "Utilities/Utilities.h"
#include "Managers/Singletons/DeletionManager.h"
using Microsoft::WRL::ComPtr;

class GpuTextureBacking {
public:
	// Don't use this.
	struct CreateTag {}; // TODO: Figure out why 'new' isn't working on PixelBuffer

	static std::unique_ptr<GpuTextureBacking>
		CreateUnique(const TextureDescription& desc,
			uint64_t owningResourceID,
			const char* name = nullptr);

	explicit GpuTextureBacking(CreateTag);
	~GpuTextureBacking();
	unsigned int GetWidth() const { return m_width; }
	unsigned int GetHeight() const { return m_height; }
	unsigned int GetChannels() const { return m_channels; }
	rhi::Resource GetTexture() {
		return m_textureHandle.GetResource();
	}
	rhi::BarrierBatch GetEnhancedBarrierGroup(RangeSpec range, rhi::ResourceAccessType prevAccessType, rhi::ResourceAccessType newAccessType, rhi::ResourceLayout prevLayout, rhi::ResourceLayout newLayout, rhi::ResourceSyncState prevSyncState, rhi::ResourceSyncState newSyncState);

	SymbolicTracker* GetStateTracker() {
		return &m_stateTracker;
	}

	void SetName(const char* newName);
	// Debug helper: dumps any live textures that haven't been destroyed yet.
	static unsigned int DumpLiveTextures();

	rhi::Resource GetAPIResource() { return m_textureHandle.GetResource(); }

	//rhi::HeapHandle GetPlacedResourceHeap() const {
	//	return m_placedResourceHeap;
	//}

	void ApplyMetadataComponentBundle(const EntityComponentBundle& bundle) {
		m_textureHandle.ApplyComponentBundle(bundle);
	}

	const rhi::ClearValue& GetClearColor() const {
		return m_clearColor;
	}
	rhi::Format GetFormat() const {
		return m_format;
	}

	unsigned int GetInternalWidth() const {
		return m_internalWidth;
	}
	unsigned int GetInternalHeight() const {
		return m_internalHeight;
	}

	unsigned int GetMipLevels() const {
		return m_mipLevels;
	}

	unsigned int GetArraySize() const {
		return m_arraySize;
	}

private:
#if BUILD_TYPE == BUILD_DEBUG
	std::stacktrace m_creation;
#endif
	void initialize(const TextureDescription& desc,
		uint64_t owningResourceID,
		const char* name);

	void RegisterLiveAlloc();
	void UnregisterLiveAlloc();
	void UpdateLiveAllocName(const char* name);

	struct LiveAllocInfo {
		std::string name;
	};

	inline static std::mutex s_liveMutex;
	inline static std::unordered_map<const GpuTextureBacking*, LiveAllocInfo> s_liveAllocs;

	unsigned int m_width;
	unsigned int m_height;
	unsigned int m_channels;
	unsigned int m_mipLevels;
	unsigned int m_arraySize;
	TrackedHandle m_textureHandle;
	rhi::Format m_format;
	TextureDescription m_desc;
	rhi::ClearValue m_clearColor;

	//rhi::HeapHandle m_placedResourceHeap; // If this is a placed resource, this is the heap it was created in

	// Enhanced barriers
	rhi::TextureBarrier m_barrier = {};

	unsigned int m_internalWidth = 0; // Internal width, used for padding textures to power of two
	unsigned int m_internalHeight = 0; // Internal height, used for padding textures to power of two

	SymbolicTracker m_stateTracker;
};