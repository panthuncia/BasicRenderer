#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <memory>

class DeviceManager {
public:
    static DeviceManager& getInstance();

    void initialize(Microsoft::WRL::ComPtr<ID3D12Device> device);

    Microsoft::WRL::ComPtr<ID3D12Device> getDevice() const;

private:
    DeviceManager() = default;
    Microsoft::WRL::ComPtr<ID3D12Device> device;
};

inline DeviceManager& DeviceManager::getInstance() {
    static DeviceManager instance;
    return instance;
}

inline void DeviceManager::initialize(Microsoft::WRL::ComPtr<ID3D12Device> device) {
    this->device = device;
}

inline Microsoft::WRL::ComPtr<ID3D12Device> DeviceManager::getDevice() const {
    return device;
}