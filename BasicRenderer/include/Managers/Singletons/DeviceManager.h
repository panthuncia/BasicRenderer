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

    void Initialize(rhi::Device device, rhi::Queue graphicsQueue, rhi::Queue computeQueue, rhi::Queue copyQueue);
	void DiagnoseDeviceRemoval();
	rhi::Device& GetDevice() {
        return device;
    }

	rhi::Queue& GetGraphicsQueue() {
		return graphicsQueue;
	}

	rhi::Queue& GetComputeQueue() {
		return computeQueue;
	}

	rhi::Queue& GetCopyQueue() {
		return copyQueue;
	}

	bool GetMeshShadersSupported() {
		return m_meshShadersSupported;
	}

private:
	ComPtr<ID3D12DeviceRemovedExtendedData> dred;

    DeviceManager() = default;
	rhi::Device device;
	rhi::Queue graphicsQueue;
	rhi::Queue computeQueue;
	rhi::Queue copyQueue;
	bool m_meshShadersSupported = false;

    void CheckGPUFeatures();

};

inline DeviceManager& DeviceManager::GetInstance() {
    static DeviceManager instance;
    return instance;
}

inline void DeviceManager::Initialize(rhi::Device device, rhi::Queue graphicsQueue, rhi::Queue computeQueue, rhi::Queue copyQueue) {
    this->device = device;
	this->graphicsQueue = graphicsQueue;
	this->computeQueue = computeQueue;
	this->copyQueue = copyQueue;

	CheckGPUFeatures();
}

inline void DeviceManager::CheckGPUFeatures() {
	D3D12_FEATURE_DATA_D3D12_OPTIONS7 features = {}; // TODO: Query interface support in RHI
	rhi::dx12::get_device(device)->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &features, sizeof(features));
	m_meshShadersSupported = features.MeshShaderTier != D3D12_MESH_SHADER_TIER_NOT_SUPPORTED;

	if (SUCCEEDED(rhi::dx12::get_device(device)->QueryInterface(IID_PPV_ARGS(&dred)))) {
	}
	else {
		spdlog::warn("Failed to get DRED interface");
	}
}