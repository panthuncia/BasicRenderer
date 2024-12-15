#pragma once

#include <directx/d3d12.h>
#include <wrl.h>
#include <memory>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

using Microsoft::WRL::ComPtr;

class DeviceManager {
public:
    static DeviceManager& GetInstance();

    void Initialize(Microsoft::WRL::ComPtr<ID3D12Device> device, Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue);
	void DiagnoseDeviceRemoval();
    Microsoft::WRL::ComPtr<ID3D12Device>& GetDevice() {
        return device;
    }

	Microsoft::WRL::ComPtr<ID3D12CommandQueue>& GetCommandQueue() {
		return commandQueue;
	}

	bool GetMeshShadersSupported() {
		return m_meshShadersSupported;
	}

private:
	ComPtr<ID3D12DeviceRemovedExtendedData> dred;

    DeviceManager() = default;
    Microsoft::WRL::ComPtr<ID3D12Device> device = nullptr;
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue = nullptr;
	bool m_meshShadersSupported = false;

    void CheckGPUFeatures();

};

inline DeviceManager& DeviceManager::GetInstance() {
    static DeviceManager instance;
    return instance;
}

inline void DeviceManager::Initialize(Microsoft::WRL::ComPtr<ID3D12Device> device, Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue) {
    this->device = device;
	this->commandQueue = queue;
	CheckGPUFeatures();

	if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&dred)))) {
	}
	else {
		spdlog::warn("Failed to get DRED interface");
	}
}

inline void DeviceManager::CheckGPUFeatures() {
	D3D12_FEATURE_DATA_D3D12_OPTIONS7 features = {};
	device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &features, sizeof(features));
	m_meshShadersSupported = features.MeshShaderTier != D3D12_MESH_SHADER_TIER_NOT_SUPPORTED;
}