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

    void Initialize(Microsoft::WRL::ComPtr<ID3D12Device10> device, Microsoft::WRL::ComPtr<ID3D12CommandQueue> graphicsQueue,  Microsoft::WRL::ComPtr<ID3D12CommandQueue> computeQueue, Microsoft::WRL::ComPtr<ID3D12CommandQueue> pCopyQueue);
	void DiagnoseDeviceRemoval();
    Microsoft::WRL::ComPtr<ID3D12Device10>& GetDevice() {
        return device;
    }

	ID3D12CommandQueue* GetGraphicsQueue() {
		return graphicsQueue.Get();
	}

	ID3D12CommandQueue* GetComputeQueue() {
		return computeQueue.Get();
	}

	ID3D12CommandQueue* GetCopyQueue() {
		return copyQueue.Get();
	}

	bool GetMeshShadersSupported() {
		return m_meshShadersSupported;
	}

private:
	ComPtr<ID3D12DeviceRemovedExtendedData> dred;

    DeviceManager() = default;
    Microsoft::WRL::ComPtr<ID3D12Device10> device = nullptr;
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> graphicsQueue = nullptr;
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> computeQueue = nullptr;
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> copyQueue = nullptr;
	bool m_meshShadersSupported = false;

    void CheckGPUFeatures();

};

inline DeviceManager& DeviceManager::GetInstance() {
    static DeviceManager instance;
    return instance;
}

inline void DeviceManager::Initialize(Microsoft::WRL::ComPtr<ID3D12Device10> pDevice, Microsoft::WRL::ComPtr<ID3D12CommandQueue> pGraphicsQueue, Microsoft::WRL::ComPtr<ID3D12CommandQueue> pComputeQueue, Microsoft::WRL::ComPtr<ID3D12CommandQueue> pCopyQueue) {
    this->device = pDevice;
	this->graphicsQueue = pGraphicsQueue;
	this->computeQueue = pComputeQueue;
	this->copyQueue = pCopyQueue;

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