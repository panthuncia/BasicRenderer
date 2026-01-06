#pragma once

#include <stdint.h>
#include <memory>
#include <mutex>
#include <stacktrace>
#include <unordered_map>

#include <rhi.h>
#include <resource_states.h>

#include "Utilities/Utilities.h"
#include "Managers/Singletons/DeletionManager.h"
#include "Resources/TrackedAllocation.h"

struct RenderContext;

// Represents the GPU-side backing storage for a buffer resource.
// Should only be owned by logical resources (Resource or derived classes).
class GpuBufferBacking {
public:
	static std::unique_ptr<GpuBufferBacking> CreateUnique(
		rhi::HeapType accessType,
		uint64_t bufferSize,
		uint64_t owningResourceID,
		bool unorderedAccess = false,
		const char* name = nullptr) {
		auto sp = std::unique_ptr<GpuBufferBacking>(new GpuBufferBacking(accessType, bufferSize, owningResourceID, unorderedAccess, name));
#if BUILD_TYPE == BUILD_DEBUG
		sp->m_creation = std::stacktrace::current();
#endif
		return sp;
	}

	~GpuBufferBacking();
	rhi::HeapType m_accessType;
	TrackedHandle m_bufferAllocation;
	//rhi::ResourcePtr m_buffer;
	rhi::BarrierBatch GetEnhancedBarrierGroup(RangeSpec range, rhi::ResourceAccessType prevAccessType, rhi::ResourceAccessType newAccessType, rhi::ResourceLayout prevLayout, rhi::ResourceLayout newLayout, rhi::ResourceSyncState prevSyncState, rhi::ResourceSyncState newSyncState);
	size_t GetSize() const { return m_size; }

	rhi::Resource GetAPIResource() { return m_bufferAllocation.GetResource(); }
	void SetName(const char* name);
	// Debug helper: dumps any live buffers that haven't been destroyed yet.
	static unsigned int DumpLiveBuffers();

	void ApplyMetadataComponentBundle(const EntityComponentBundle& bundle) {
		m_bufferAllocation.ApplyComponentBundle(bundle);
	}

	SymbolicTracker* GetStateTracker() {
		return &m_stateTracker;
	}

private:
#if BUILD_TYPE == BUILD_DEBUG
	std::stacktrace m_creation;
#endif
	size_t m_size = 0;
	rhi::BufferBarrier m_barrier = {};

	SymbolicTracker m_stateTracker;

	GpuBufferBacking(
		rhi::HeapType accessType,
		uint64_t bufferSize,
		uint64_t owningResourceID,
		bool unorderedAccess = false,
		const char* name = nullptr);

	void RegisterLiveAlloc();
	void UnregisterLiveAlloc();
	void UpdateLiveAllocName(const char* name);

	struct LiveAllocInfo {
		size_t size = 0;
		std::string name;
	};

	inline static std::mutex s_liveMutex;
	inline static std::unordered_map<const GpuBufferBacking*, LiveAllocInfo> s_liveAllocs;
};