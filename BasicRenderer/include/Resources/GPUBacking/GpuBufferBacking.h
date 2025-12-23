#pragma once

#include <stdint.h>
#include <memory>
#include <stacktrace>

#include <rhi.h>
#include <resource_states.h>

#include "Utilities/Utilities.h"
#include "Managers/Singletons/DeletionManager.h"
#include "Resources/TrackedAllocation.h"

class RenderContext;

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

	~GpuBufferBacking() {
		DeletionManager::GetInstance().MarkForDelete(std::move(m_bufferAllocation));
	}
	rhi::HeapType m_accessType;
	TrackedHandle m_bufferAllocation;
	//rhi::ResourcePtr m_buffer;
	rhi::BarrierBatch GetEnhancedBarrierGroup(RangeSpec range, rhi::ResourceAccessType prevAccessType, rhi::ResourceAccessType newAccessType, rhi::ResourceLayout prevLayout, rhi::ResourceLayout newLayout, rhi::ResourceSyncState prevSyncState, rhi::ResourceSyncState newSyncState);
	size_t GetSize() const { return m_size; }

	rhi::Resource GetAPIResource() { return m_bufferAllocation.GetResource(); }
	void SetName(const char* name);

	void ApplyMetadataComponentBundle(const EntityComponentBundle& bundle) {
		m_bufferAllocation.ApplyComponentBundle(bundle);
	}

private:
#if BUILD_TYPE == BUILD_DEBUG
	std::stacktrace m_creation;
#endif
	size_t m_size = 0;
	rhi::BufferBarrier m_barrier = {};

	GpuBufferBacking(
		rhi::HeapType accessType,
		uint64_t bufferSize,
		uint64_t owningResourceID,
		bool unorderedAccess = false,
		const char* name = nullptr);
};