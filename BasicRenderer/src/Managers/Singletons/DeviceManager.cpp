#include "Managers/Singletons/DeviceManager.h"

#include <spdlog/spdlog.h>
#include <rhi_debug.h>
#include <rhi_interop_dx12.h>

#include "Managers/Singletons/SettingsManager.h"

namespace br {

namespace {
void LogInstrumentationDiagnostics(rhi::Device device) {
    auto diagnostics = rhi::debug::GetInstrumentationDiagnostics(device);
    for (const auto& diagnostic : diagnostics) {
        switch (diagnostic.severity) {
        case rhi::DebugInstrumentationDiagnosticSeverity::Info:
            spdlog::info("DeviceManager ReShape diagnostic: {}", diagnostic.message);
            break;
        case rhi::DebugInstrumentationDiagnosticSeverity::Warning:
            spdlog::warn("DeviceManager ReShape diagnostic: {}", diagnostic.message);
            break;
        case rhi::DebugInstrumentationDiagnosticSeverity::Error:
        default:
            spdlog::error("DeviceManager ReShape diagnostic: {}", diagnostic.message);
            break;
        }
    }
}

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

bool IsDiagnosticsBuild() {
#if BUILD_TYPE == BUILD_TYPE_DEBUG //|| BUILD_TYPE == BUILD_TYPE_RELEASE_DEBUG
    return true;
#else
    return false;
#endif
}
}

DeviceManager& DeviceManager::GetInstance() {
    static DeviceManager instance;
    return instance;
}

void DeviceManager::Initialize() {
    auto& settingsManager = SettingsManager::GetInstance();
    auto numFramesInFlight = settingsManager.getSettingGetter<uint8_t>("numFramesInFlight")();
    bool enableStreamline = !IsStreamlineDisabledByEnvironment();
    try {
        enableStreamline = settingsManager.getSettingGetter<bool>("enableStreamline")();
    }
    catch (const std::exception&) {
        enableStreamline = !IsStreamlineDisabledByEnvironment();
    }
    if (IsStreamlineDisabledByEnvironment()) {
        enableStreamline = false;
    }

    const bool enableDebug = false;// IsDiagnosticsBuild();

    bool enableRuntimeInstrumentation = false;
    bool enableSynchronousRecording = false;
    bool enableTexelAddressing = true;
    try {
        enableRuntimeInstrumentation = settingsManager.getSettingGetter<bool>("enableReShape")();
    }
    catch (const std::exception&) {
        enableRuntimeInstrumentation = false;
    }

    try {
        enableSynchronousRecording = settingsManager.getSettingGetter<bool>("reshapeSynchronousRecording")();
    }
    catch (const std::exception&) {
        enableSynchronousRecording = false;
    }

    try {
        enableTexelAddressing = settingsManager.getSettingGetter<bool>("reshapeTexelAddressing")();
    }
    catch (const std::exception&) {
        enableTexelAddressing = true;
    }

    spdlog::info(
        "DeviceManager::Initialize enableStreamline={} enableDebug={} enableReShape={} reshapeSync={} reshapeTexel={} framesInFlight={}",
        enableStreamline,
        enableDebug,
        enableRuntimeInstrumentation,
        enableSynchronousRecording,
        enableTexelAddressing,
        numFramesInFlight);

    rhi::CreateD3D12Device(
        rhi::DeviceCreateInfo{
            .backend = rhi::Backend::D3D12,
            .framesInFlight = numFramesInFlight,
            .enableDebug = enableDebug,
            .instrumentation = {
                .enableRuntimeInstrumentation = enableRuntimeInstrumentation,
                .enableSynchronousRecording = enableSynchronousRecording,
                .enableTexelAddressing = enableTexelAddressing,
            },
        },
        m_device,
        enableStreamline);

    m_graphicsQueue = m_device->GetQueue(rhi::QueueKind::Graphics);
    m_computeQueue = m_device->GetQueue(rhi::QueueKind::Compute);
    m_copyQueue = m_device->GetQueue(rhi::QueueKind::Copy);

    rhi::DebugInstrumentationCapabilities instrumentationCapabilities{};
    if (rhi::IsOk(rhi::debug::GetInstrumentationCapabilities(m_device.Get(), instrumentationCapabilities))) {
        spdlog::info(
            "DeviceManager::Initialize ReShape caps buildEnabled={} installSupported={} globalSupported={} syncSupported={} featureCount={}",
            instrumentationCapabilities.backendBuildEnabled,
            instrumentationCapabilities.installSupported,
            instrumentationCapabilities.globalInstrumentationSupported,
            instrumentationCapabilities.synchronousRecordingSupported,
            instrumentationCapabilities.featureCount);
    }

        rhi::DebugInstrumentationState instrumentationState{};
        if (rhi::IsOk(rhi::debug::GetInstrumentationState(m_device.Get(), instrumentationState))) {
            spdlog::info(
                "DeviceManager::Initialize ReShape state requested={} active={} sync={} texel={} featureMask=0x{:X}",
                instrumentationState.requested,
                instrumentationState.active,
                instrumentationState.synchronousRecording,
                instrumentationState.texelAddressingEnabled,
                instrumentationState.globalFeatureMask);
        }

        auto instrumentationFeatures = rhi::debug::GetInstrumentationFeatures(m_device.Get());
        spdlog::info("DeviceManager::Initialize ReShape queried feature count={}", instrumentationFeatures.size());
        for (const auto& feature : instrumentationFeatures) {
            spdlog::info(
                "DeviceManager::Initialize ReShape feature bit=0x{:X} name='{}' description='{}'",
                feature.featureBit,
                feature.name,
                feature.description);
        }

        if (enableRuntimeInstrumentation && instrumentationState.active && instrumentationFeatures.empty()) {
            spdlog::warn("DeviceManager::Initialize requested ReShape instrumentation, but no backend features were discovered after startup queries.");
        }

        LogInstrumentationDiagnostics(m_device.Get());

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
