#pragma once

#include <directx/d3d12.h>
#include <memory>
#include <optional>
#include <functional>

#include <rhi.h>
#include <rhi_allocator.h>
#include "Resources/TrackedAllocation.h"

class DeviceManager {
public:
	struct TrackingHooks {
		std::function<TrackedEntityToken(flecs::entity existing)> createTrackingToken;
	};

    static DeviceManager& GetInstance();

	static void SetTrackingHooks(TrackingHooks hooks) {
		s_trackingHooks = std::move(hooks);
	}

	static void ResetTrackingHooks() {
		s_trackingHooks = {};
	}

	void Initialize(rhi::Device device);
	void Cleanup();
	rhi::Device GetDevice() {
        return m_device.Get();
    }

	rhi::Queue& GetGraphicsQueue() {
		return m_graphicsQueue;
	}

	rhi::Queue& GetComputeQueue() {
		return m_computeQueue;
	}

	rhi::Queue& GetCopyQueue() {
		return m_copyQueue;
	}

	rhi::ma::Allocator* GetAllocator() {
		return m_allocator;
	}

	// Create a resource and track its allocation with an entity.
	rhi::Result CreateResourceTracked(
		const rhi::ma::AllocationDesc& allocDesc,
		const rhi::ResourceDesc& resourceDesc,
		UINT32 numCastableFormats,
		const rhi::Format* pCastableFormats,
		TrackedHandle& outAllocation,
		std::optional<AllocationTrackDesc> trackDesc = std::nullopt
	) const noexcept;

	rhi::Result CreateAliasingResourceTracked(
		rhi::ma::Allocation& allocation,
		UINT64 allocationLocalOffset,
		const rhi::ResourceDesc& resourceDesc,
		UINT32 numCastableFormats,
		const rhi::Format* pCastableFormats,
		TrackedHandle& out,
		std::optional<AllocationTrackDesc> trackDesc) const noexcept;

	rhi::Result AllocateMemoryTracked(
		const rhi::ma::AllocationDesc& allocDesc,
		const rhi::ResourceAllocationInfo& allocationInfo,
		TrackedHandle& outAllocation,
		std::optional<AllocationTrackDesc> trackDesc = std::nullopt) const noexcept;

private:

    DeviceManager() = default;
	rhi::DevicePtr m_device;
	rhi::Queue m_graphicsQueue;
	rhi::Queue m_computeQueue;
	rhi::Queue m_copyQueue;
	rhi::ma::Allocator* m_allocator = nullptr;
	inline static TrackingHooks s_trackingHooks{};

};

inline DeviceManager& DeviceManager::GetInstance() {
	static DeviceManager instance;
	return instance;
}
