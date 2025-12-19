#pragma once

#include <directx/d3d12.h>
#include <memory>
#include <spdlog/spdlog.h>
#include <optional>
#include <flecs.h>

#include <rhi.h>
#include <rhi_interop_dx12.h>
#include <rhi_allocator.h>
#include "Managers/Singletons/ECSManager.h"
#include "Resources/ResourceIdentifier.h"
#include "Resources/TrackedAllocation.h"

#include "Managers/Singletons/SettingsManager.h"

class DeviceManager {
public:
    static DeviceManager& GetInstance();

    void Initialize();
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

	bool GetMeshShadersSupported() {
		return m_meshShadersSupported;
	}

	// Create a resource and track its allocation with an entity.
	rhi::Result CreateResourceTracked(
		const rhi::ma::AllocationDesc& allocDesc,
		const rhi::ResourceDesc& resourceDesc,
		UINT32 numCastableFormats,
		const rhi::Format* pCastableFormats,
		TrackedAllocation& outAllocation,
		std::optional<AllocationTrackDesc> trackDesc = std::nullopt
	) const noexcept;

private:

    DeviceManager() = default;
	rhi::DevicePtr m_device;
	rhi::Queue m_graphicsQueue;
	rhi::Queue m_computeQueue;
	rhi::Queue m_copyQueue;
	bool m_meshShadersSupported = false;
	rhi::ma::Allocator* m_allocator;

    void CheckGPUFeatures();

};

namespace MemoryStatisticsComponents
{
	struct MemSizeBytes{
		uint64_t size;
	};
}

inline rhi::Result DeviceManager::CreateResourceTracked(
	const rhi::ma::AllocationDesc& allocDesc, 
	const rhi::ResourceDesc& resourceDesc, 
	UINT32 numCastableFormats, 
	const rhi::Format* pCastableFormats, 
	TrackedAllocation& outAllocation,
	std::optional<AllocationTrackDesc> trackDesc) const noexcept
{
	rhi::ma::AllocationPtr alloc;
	const auto result = m_allocator->CreateResource(&allocDesc, &resourceDesc, numCastableFormats, pCastableFormats, alloc);

	// Create or reuse entity
	flecs::entity e;
	AllocationTrackDesc track;
	if (trackDesc.has_value()) {
		track = trackDesc.value();
		e = track.existing;
	}
	if (!e.is_alive()) {
		e = ECSManager::GetInstance().GetWorld().entity();
	}

	// Attach base info (size, kind, etc.)
	const uint64_t sizeBytes = 0; // TODO
	e.set<MemoryStatisticsComponents::MemSizeBytes>({ sizeBytes });
	if (track.id) e.set<ResourceIdentifier>(track.id.value());

	// Apply caller-provided bundle
	track.attach.ApplyTo(e);

	// Return wrapper that will delete entity when allocation is destroyed
	outAllocation = { std::move(alloc), std::move(e) };
	return result;
}

inline DeviceManager& DeviceManager::GetInstance() {
    static DeviceManager instance;
    return instance;
}

inline void DeviceManager::Initialize() {
	auto numFramesInFlight = SettingsManager::GetInstance().getSettingGetter<uint8_t>("numFramesInFlight")();
	bool enableDebug = false;
#if BUILD_TYPE == BUILD_DEBUG
	enableDebug = true;
#endif
	m_device = rhi::CreateD3D12Device(rhi::DeviceCreateInfo{ .backend = rhi::Backend::D3D12, .framesInFlight = numFramesInFlight, .enableDebug = enableDebug });
	m_graphicsQueue = m_device->GetQueue(rhi::QueueKind::Graphics);
	m_computeQueue = m_device->GetQueue(rhi::QueueKind::Compute);
	m_copyQueue = m_device->GetQueue(rhi::QueueKind::Copy);
	CheckGPUFeatures();

	rhi::ma::AllocatorDesc desc;
	desc.device = m_device.Get();
	rhi::ma::CreateAllocator(&desc, &m_allocator);
}

inline void DeviceManager::CheckGPUFeatures() {
	D3D12_FEATURE_DATA_D3D12_OPTIONS7 features = {}; // TODO: Use query interface in RHI
	rhi::dx12::get_device(m_device.Get())->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &features, sizeof(features));
	m_meshShadersSupported = features.MeshShaderTier != D3D12_MESH_SHADER_TIER_NOT_SUPPORTED;
}