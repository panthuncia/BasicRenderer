#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <memory>

class DeviceManager {
public:
    static DeviceManager& GetInstance();

    void Initialize(Microsoft::WRL::ComPtr<ID3D12Device> device);

    Microsoft::WRL::ComPtr<ID3D12Device>& GetDevice() {
        return device;
    }

private:
    DeviceManager() = default;
    Microsoft::WRL::ComPtr<ID3D12Device> device;
};

inline DeviceManager& DeviceManager::GetInstance() {
    static DeviceManager instance;
    return instance;
}

inline void DeviceManager::Initialize(Microsoft::WRL::ComPtr<ID3D12Device> device) {
    this->device = device;
}
