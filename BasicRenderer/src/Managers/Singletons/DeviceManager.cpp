#include "Managers/Singletons/DeviceManager.h"

#include <spdlog/spdlog.h>
#include <rhi_interop_dx12.h>

#include "Managers/Singletons/SettingsManager.h"

namespace br {

namespace {
bool IsStreamlineDisabledByEnvironment() {
    char* value = nullptr;
    size_t len = 0;
    if (_dupenv_s(&value, &len, "BASICRENDERER_DISABLE_STREAMLINE") != 0 || value == nullptr) {
        return false;
    }
    const bool disabled = value[0] == '1' || value[0] == 't' || value[0] == 'T' || value[0] == 'y' || value[0] == 'Y';
    free(value);
    return disabled;
}
}

DeviceManager& DeviceManager::GetInstance() {
    static DeviceManager instance;
    return instance;
}

void DeviceManager::Initialize() {
    auto numFramesInFlight = SettingsManager::GetInstance().getSettingGetter<uint8_t>("numFramesInFlight")();
    bool enableStreamline = !IsStreamlineDisabledByEnvironment();
    try {
        enableStreamline = SettingsManager::GetInstance().getSettingGetter<bool>("enableStreamline")();
    }
    catch (const std::exception&) {
        enableStreamline = !IsStreamlineDisabledByEnvironment();
    }
    if (IsStreamlineDisabledByEnvironment()) {
        enableStreamline = false;
    }

    bool enableDebug = false;
//#if BUILD_TYPE == BUILD_DEBUG
    enableDebug = true;
//#endif

    spdlog::info("DeviceManager::Initialize enableStreamline={} enableDebug={} framesInFlight={}", enableStreamline, enableDebug, numFramesInFlight);

    rhi::CreateD3D12Device(
        rhi::DeviceCreateInfo{ .backend = rhi::Backend::D3D12, .framesInFlight = numFramesInFlight, .enableDebug = enableDebug },
        m_device,
        enableStreamline);

    m_graphicsQueue = m_device->GetQueue(rhi::QueueKind::Graphics);
    m_computeQueue = m_device->GetQueue(rhi::QueueKind::Compute);
    m_copyQueue = m_device->GetQueue(rhi::QueueKind::Copy);

    CheckGPUFeatures();
}

void DeviceManager::Cleanup() {
    m_graphicsQueue.Reset();
    m_computeQueue.Reset();
    m_copyQueue.Reset();
    m_meshShadersSupported = false;

    if (m_device) {
        m_device.Reset();
    }
}

void DeviceManager::CheckGPUFeatures() {
    D3D12_FEATURE_DATA_D3D12_OPTIONS7 features = {};
    rhi::dx12::get_device(m_device.Get())->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &features, sizeof(features));
    m_meshShadersSupported = features.MeshShaderTier != D3D12_MESH_SHADER_TIER_NOT_SUPPORTED;
}

}
