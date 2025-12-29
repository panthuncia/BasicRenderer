#include "Resources/GPUBacking/GpuBufferBacking.h"

#include <rhi_helpers.h>
#include <spdlog/spdlog.h>

#include "Managers/Singletons/DeviceManager.h"
#include "Resources/MemoryStatisticsComponents.h"

using namespace Microsoft::WRL;

std::mutex GpuBufferBacking::s_liveMutex{};
std::unordered_map<const GpuBufferBacking*, GpuBufferBacking::LiveAllocInfo> GpuBufferBacking::s_liveAllocs{};

GpuBufferBacking::GpuBufferBacking(
	const rhi::HeapType accessType,
	const uint64_t bufferSize,
	uint64_t owningResourceID,
	const bool unorderedAccess,
	const char* name) {
    m_accessType = accessType;

    rhi::ResourceDesc desc = rhi::helpers::ResourceDesc::Buffer(bufferSize);
    if (unorderedAccess) {
        desc.resourceFlags |= rhi::ResourceFlags::RF_AllowUnorderedAccess;
    }
    desc.heapType = accessType;
    auto device = DeviceManager::GetInstance().GetDevice();

    rhi::ResourceAllocationInfo allocInfo;
    device.GetResourceAllocationInfo(&desc, 1, &allocInfo);

    AllocationTrackDesc trackDesc(owningResourceID);
    EntityComponentBundle allocationBundle;
    if (name != nullptr) {
        allocationBundle.Set<MemoryStatisticsComponents::ResourceName>({ name });
    }

    allocationBundle
        .Set<MemoryStatisticsComponents::MemSizeBytes>({ allocInfo.sizeInBytes })
        .Set<MemoryStatisticsComponents::ResourceType>({ rhi::ResourceType::Buffer })
        .Set<MemoryStatisticsComponents::ResourceID>({ owningResourceID });
    trackDesc.attach = allocationBundle;

    rhi::ma::AllocationDesc allocationDesc;
    allocationDesc.heapType = accessType;
    DeviceManager::GetInstance().CreateResourceTracked(
        allocationDesc,
        desc,
        0,
        nullptr,
        m_bufferAllocation,
        trackDesc);

    m_size = bufferSize;

    RegisterLiveAlloc();
    UpdateLiveAllocName(name);
}

GpuBufferBacking::~GpuBufferBacking() {
    UnregisterLiveAlloc();
    DeletionManager::GetInstance().MarkForDelete(std::move(m_bufferAllocation));
}

void GpuBufferBacking::SetName(const char* name)
{
	m_bufferAllocation.ApplyComponentBundle(EntityComponentBundle().Set<MemoryStatisticsComponents::ResourceName>({ name }));
	m_bufferAllocation.GetResource().SetName(name);
    UpdateLiveAllocName(name);
}

rhi::BarrierBatch GpuBufferBacking::GetEnhancedBarrierGroup(RangeSpec range, rhi::ResourceAccessType prevAccessType, rhi::ResourceAccessType newAccessType, rhi::ResourceLayout prevLayout, rhi::ResourceLayout newLayout, rhi::ResourceSyncState prevSyncState, rhi::ResourceSyncState newSyncState) {

	rhi::BarrierBatch batch = {};
	m_barrier = rhi::BufferBarrier{
	   .buffer = GetAPIResource().GetHandle(),
	   .offset = 0,
	   .size = UINT64_MAX,
	   .beforeSync = prevSyncState,
	   .afterSync = newSyncState,
	   .beforeAccess = prevAccessType,
	   .afterAccess = newAccessType
	};
	batch.buffers = { &m_barrier };

	return batch;
}


void GpuBufferBacking::RegisterLiveAlloc() {
    std::scoped_lock lock(s_liveMutex);
    LiveAllocInfo info{};
    info.size = m_size;
    //info.uav = m_bufferAllocation.GetResource().IsValid() && m_bufferAllocation.GetResource().GetDesc().resourceFlags & rhi::ResourceFlags::RF_AllowUnorderedAccess;
    s_liveAllocs[this] = info;
}

void GpuBufferBacking::UnregisterLiveAlloc() {
    std::scoped_lock lock(s_liveMutex);
	if (s_liveAllocs.find(this) == s_liveAllocs.end()) { // If an error occurs here, it means something is being destructed after this global was destroyed.
        spdlog::warn("GpuBufferBacking being destroyed but not found in live allocations!");
    }
    else {
        s_liveAllocs.erase(this);
    }
}

void GpuBufferBacking::UpdateLiveAllocName(const char* name) {
    std::scoped_lock lock(s_liveMutex);
    auto it = s_liveAllocs.find(this);
    if (it != s_liveAllocs.end()) {
        it->second.name = name ? name : "";
    }
}

unsigned int GpuBufferBacking::DumpLiveBuffers(size_t maxSizeBytes) {
    std::scoped_lock lock(s_liveMutex);
    for (const auto& [ptr, info] : s_liveAllocs) {
        if (info.size <= maxSizeBytes) {
            spdlog::warn("Live buffer still tracked: size={} bytes, name='{}'", info.size, info.name);
        }
    }
	return static_cast<unsigned int>(s_liveAllocs.size());
}
