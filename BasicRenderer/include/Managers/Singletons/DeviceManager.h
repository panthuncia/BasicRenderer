#pragma once

#include <directx/d3d12.h>
#include <wrl.h>
#include <memory>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <rhi.h>
#include <rhi_interop_dx12.h>

#include "Managers/Singletons/SettingsManager.h"

using Microsoft::WRL::ComPtr;

class DeviceManager {
public:
    static DeviceManager& GetInstance();

    void Initialize();
	void DiagnoseDeviceRemoval();
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

	bool GetMeshShadersSupported() {
		return m_meshShadersSupported;
	}

private:
	ComPtr<ID3D12DeviceRemovedExtendedData> dred;

    DeviceManager() = default;
	rhi::DevicePtr m_device;
	rhi::Queue m_graphicsQueue;
	rhi::Queue m_computeQueue;
	rhi::Queue m_copyQueue;
	bool m_meshShadersSupported = false;

    void CheckGPUFeatures();

};

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
}

inline void DeviceManager::CheckGPUFeatures() {
	D3D12_FEATURE_DATA_D3D12_OPTIONS7 features = {}; // TODO: Query interface support in RHI
	rhi::dx12::get_device(m_device.Get())->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &features, sizeof(features));
	m_meshShadersSupported = features.MeshShaderTier != D3D12_MESH_SHADER_TIER_NOT_SUPPORTED;
}