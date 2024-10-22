#pragma once

#include <directx/d3d12.h>
#include <wrl.h>
#include <memory>

class DeviceManager {
public:
    static DeviceManager& GetInstance();

    void Initialize(Microsoft::WRL::ComPtr<ID3D12Device> device, Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue);

    Microsoft::WRL::ComPtr<ID3D12Device>& GetDevice() {
        return device;
    }

	Microsoft::WRL::ComPtr<ID3D12CommandQueue>& GetCommandQueue() {
		return commandQueue;
	}

private:
    DeviceManager() = default;
    Microsoft::WRL::ComPtr<ID3D12Device> device = nullptr;
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue = nullptr;
};

inline DeviceManager& DeviceManager::GetInstance() {
    static DeviceManager instance;
    return instance;
}

inline void DeviceManager::Initialize(Microsoft::WRL::ComPtr<ID3D12Device> device, Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue) {
    this->device = device;
	this->commandQueue = queue;
}
