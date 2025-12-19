#pragma once

#include <stdint.h>
#include <string>
#include <memory>
#include <stacktrace>

#include <rhi.h>
#include <resource_states.h>

#include "Resources/GloballyIndexedResource.h"
#include "Utilities/Utilities.h"
#include "rhi_allocator.h"
#include "Managers/Singletons/DeletionManager.h"
#include "Resources/TrackedAllocation.h"

class RenderContext;

class Buffer : public GloballyIndexedResource{
public:
	static std::shared_ptr<Buffer> CreateShared(
		rhi::HeapType accessType,
		uint64_t bufferSize, 
		bool unorderedAccess = false) {
		auto sp = std::shared_ptr<Buffer>(new Buffer(accessType, bufferSize, unorderedAccess));
#if BUILD_TYPE == BUILD_DEBUG
		sp->m_creation = std::stacktrace::current();
#endif
		return sp;
	}
	static std::unique_ptr<Buffer> CreateUnique(
		rhi::HeapType accessType,
		uint64_t bufferSize,
		bool unorderedAccess = false) {
		auto sp = std::unique_ptr<Buffer>(new Buffer(accessType, bufferSize, unorderedAccess));
#if BUILD_TYPE == BUILD_DEBUG
		sp->m_creation = std::stacktrace::current();
#endif
		return sp;
	}

	~Buffer() {
		DeletionManager::GetInstance().MarkForDelete(std::move(m_bufferAllocation));
	}
	rhi::HeapType m_accessType;
	TrackedAllocation m_bufferAllocation;
	//rhi::ResourcePtr m_buffer;
	rhi::BarrierBatch GetEnhancedBarrierGroup(RangeSpec range, rhi::ResourceAccessType prevAccessType, rhi::ResourceAccessType newAccessType, rhi::ResourceLayout prevLayout, rhi::ResourceLayout newLayout, rhi::ResourceSyncState prevSyncState, rhi::ResourceSyncState newSyncState);
	size_t GetSize() const { return m_size; }

	rhi::Resource GetAPIResource() override { return m_bufferAllocation->GetResource(); }
protected:
	void OnSetName() override { m_bufferAllocation->GetResource().SetName(ws2s(name).c_str()); }
private:
#if BUILD_TYPE == BUILD_DEBUG
	std::stacktrace m_creation;
#endif
	size_t m_size = 0;
	rhi::BufferBarrier m_barrier = {};

	Buffer(
		rhi::HeapType accessType,
		uint64_t bufferSize, 
		bool unorderedAccess = false);
};