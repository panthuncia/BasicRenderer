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

    void Initialize(Microsoft::WRL::ComPtr<ID3D12Device> device, Microsoft::WRL::ComPtr<ID3D12CommandQueue> graphicsQueue,  Microsoft::WRL::ComPtr<ID3D12CommandQueue> computeQueue);
	void DiagnoseDeviceRemoval();
    Microsoft::WRL::ComPtr<ID3D12Device>& GetDevice() {
        return device;
    }

	ID3D12CommandQueue* GetGraphicsQueue() {
		return graphicsQueue.Get();
	}

	ID3D12CommandQueue* GetComputeQueue() {
		return computeQueue.Get();
	}

	bool GetMeshShadersSupported() {
		return m_meshShadersSupported;
	}

private:
	ComPtr<ID3D12DeviceRemovedExtendedData> dred;

    DeviceManager() = default;
    Microsoft::WRL::ComPtr<ID3D12Device> device = nullptr;
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> graphicsQueue = nullptr;
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> computeQueue = nullptr;
	bool m_meshShadersSupported = false;

    void CheckGPUFeatures();

};

inline DeviceManager& DeviceManager::GetInstance() {
    static DeviceManager instance;
    return instance;
}

inline void DeviceManager::Initialize(Microsoft::WRL::ComPtr<ID3D12Device> device, Microsoft::WRL::ComPtr<ID3D12CommandQueue> graphicsQueue, Microsoft::WRL::ComPtr<ID3D12CommandQueue> computeQueue) {
    this->device = device;
	this->graphicsQueue = graphicsQueue;
	this->computeQueue = computeQueue;
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