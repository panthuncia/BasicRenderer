#pragma once

#include <directx/d3d12.h>
#include <wrl.h>
#include <memory>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <rhi.h>
#include <rhi_interop_dx12.h>

using Microsoft::WRL::ComPtr;

class DeviceManager {
public:
    static DeviceManager& GetInstance();

    void Initialize();
	void DiagnoseDeviceRemoval();
	rhi::Device GetDevice() {
        return device.Get();
    }

	rhi::Queue GetGraphicsQueue() {
		return device->GetQueue(rhi::QueueKind::Graphics);
	}

	rhi::Queue GetComputeQueue() {
		return device->GetQueue(rhi::QueueKind::Compute);
	}

	rhi::Queue GetCopyQueue() {
		return device->GetQueue(rhi::QueueKind::Copy);
	}

	bool GetMeshShadersSupported() {
		return m_meshShadersSupported;
	}

private:
	ComPtr<ID3D12DeviceRemovedExtendedData> dred;

    DeviceManager() = default;
	rhi::DevicePtr device;
	//rhi::Queue graphicsQueue;
	//rhi::Queue computeQueue;
	//rhi::Queue copyQueue;
	bool m_meshShadersSupported = false;

    void CheckGPUFeatures();

};

inline DeviceManager& DeviceManager::GetInstance() {
    static DeviceManager instance;
    return instance;
}

inline void DeviceManager::Initialize() {
	auto framesInFlight = SettingsManager::GetInstance().getSettingGetter<uint8_t>("framesInFlight")();
	this->device = rhi::CreateD3D12Device(rhi::DeviceCreateInfo{ .backend = rhi::Backend::D3D12, .framesInFlight = framesInFlight, .enableDebug = true });

	CheckGPUFeatures();
}

inline void DeviceManager::CheckGPUFeatures() {
	D3D12_FEATURE_DATA_D3D12_OPTIONS7 features = {}; // TODO: Query interface support in RHI
	rhi::dx12::get_device(device.Get())->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &features, sizeof(features));
	m_meshShadersSupported = features.MeshShaderTier != D3D12_MESH_SHADER_TIER_NOT_SUPPORTED;
}