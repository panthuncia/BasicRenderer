#include "Managers/Singletons/UpscalingManager.h"

#include <spdlog/spdlog.h>
#include <flecs.h>

#include "ThirdParty/Streamline/sl.h"
#include "slHooks.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Render/RenderContext.h"
#include "Resources/PixelBuffer.h"
#include "Scene/Scene.h"
#include "Utilities/MathUtils.h"

bool CheckDLSSSupport(IDXGIAdapter1* adapter) {
    DXGI_ADAPTER_DESC desc{};
    if (SUCCEEDED(adapter->GetDesc(&desc)))
    {
        sl::AdapterInfo adapterInfo{};
        adapterInfo.deviceLUID = (uint8_t*)&desc.AdapterLuid;
        adapterInfo.deviceLUIDSizeInBytes = sizeof(LUID);
        if (SL_FAILED(result, slIsFeatureSupported(sl::kFeatureDLSS, adapterInfo)))
        {
            // Requested feature is not supported on the system, fallback to the default method
            switch (result)
            {
            case sl::Result::eErrorOSOutOfDate:         // inform user to update OS
            case sl::Result::eErrorDriverOutOfDate:     // inform user to update driver
            case sl::Result::eErrorAdapterNotSupported:  // cannot use this adapter (older or non-NVDA GPU etc)
                return false;
            };
        }
        else
        {
            return true;
        }
    }
	return false;
}

inline void StoreFloat4x4(const DirectX::XMMATRIX& m, sl::float4x4& target, bool transpose = false)
{
    DirectX::XMMATRIX mTransposed = transpose ? DirectX::XMMatrixTranspose(m) : m;
    DirectX::XMStoreFloat4(
        reinterpret_cast<DirectX::XMFLOAT4*>(&target.row[0]),
        mTransposed.r[0]
    );
    DirectX::XMStoreFloat4(
        reinterpret_cast<DirectX::XMFLOAT4*>(&target.row[1]),
        mTransposed.r[1]
    );
    DirectX::XMStoreFloat4(
        reinterpret_cast<DirectX::XMFLOAT4*>(&target.row[2]),
        mTransposed.r[2]
    );
    DirectX::XMStoreFloat4(
        reinterpret_cast<DirectX::XMFLOAT4*>(&target.row[3]),
        mTransposed.r[3]
    );
}

void UpscalingManager::InitializeAdapter(Microsoft::WRL::ComPtr<IDXGIAdapter1>& adapter)
{
	m_currentAdapter = adapter;

    if (CheckDLSSSupport(adapter.Get()))
    {
		m_upscalingMode = UpscalingMode::DLSS;
    }
    else
    {
        m_upscalingMode = UpscalingMode::FSR3;
	}
}

void UpscalingManager::SetDevice(Microsoft::WRL::ComPtr<ID3D12Device10>& device) {
    switch (m_upscalingMode)
    {
        case UpscalingMode::DLSS:
        slSetD3DDevice(device.Get()); // Set the D3D device for Streamline
		break;
    }
}

void UpscalingManager::Setup() {
    m_getRenderRes = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT2>("renderResolution");
	m_getOutputRes = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT2>("outputResolution");

    m_numFramesInFlight = SettingsManager::GetInstance().getSettingGetter<uint8_t>("numFramesInFlight")();
    m_frameTokens.resize(m_numFramesInFlight);
    for (uint32_t i = 0; i < m_numFramesInFlight; i++) {
        slGetNewFrameToken(m_frameTokens[i], &i);
    }

    switch (m_upscalingMode)
    {
    case UpscalingMode::DLSS:
		auto outputRes = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT2>("outputResolution")();
        sl::DLSSOptimalSettings dlssSettings;
        sl::DLSSOptions dlssOptions = {};
        // These are populated based on user selection in the UI
        dlssOptions.mode = sl::DLSSMode::eMaxPerformance;
        dlssOptions.outputWidth = outputRes.x;
        dlssOptions.outputHeight = outputRes.y;
        // Now let's check what should our rendering resolution be
        if (SL_FAILED(result, slDLSSGetOptimalSettings(dlssOptions, dlssSettings)))
        {
            spdlog::error("DLSSGetOptimalSettings failed!");
        }
        // Setup rendering based on the provided values in the sl::DLSSSettings structure

        SettingsManager::GetInstance().getSettingSetter<DirectX::XMUINT2>("renderResolution")({ dlssSettings.optimalRenderWidth, dlssSettings.optimalRenderHeight });

        auto viewport = sl::ViewportHandle(0); // 0 is the default viewport

        // Set preferred Render Presets per Perf Quality Mode. These are typically set one time
        // and established while evaluating DLSS SR Image Quality for your Application.
        // It will be set to DSSPreset::eDefault if unspecified.
        // Please Refer to section 3.12 of the DLSS Programming Guide for details.
        dlssOptions.dlaaPreset = sl::DLSSPreset::ePresetK;
        dlssOptions.qualityPreset = sl::DLSSPreset::ePresetK;
        dlssOptions.balancedPreset = sl::DLSSPreset::ePresetK;
        dlssOptions.performancePreset = sl::DLSSPreset::ePresetK;
        dlssOptions.ultraPerformancePreset = sl::DLSSPreset::ePresetF;
        // These are populated based on user selection in the UI
        dlssOptions.outputWidth = outputRes.x;
        dlssOptions.outputHeight = outputRes.y;
        dlssOptions.sharpness = 0;
        dlssOptions.colorBuffersHDR = sl::Boolean::eTrue; // assuming HDR pipeline
        dlssOptions.useAutoExposure = sl::Boolean::eTrue; // autoexposure is not to be used if a proper exposure texture is available
        dlssOptions.alphaUpscalingEnabled = sl::Boolean::eFalse; // experimental alpha upscaling, enable to upscale alpha channel of color texture
        if (SL_FAILED(result, slDLSSSetOptions(viewport, dlssOptions)))
        {
            // Handle error here, check the logs
        }

        break;
    }
}

void UpscalingManager::EvaluateDLSS(const RenderContext& context, PixelBuffer* pHDRTarget, PixelBuffer* pUpscaledHDRTarget, PixelBuffer* pDepthTexture, PixelBuffer* pMotionVectors) {
    auto frameToken = m_frameTokens[context.frameIndex];
    auto myViewport = sl::ViewportHandle(0); // 0 is the default viewport
    auto renderRes = m_getRenderRes();
    auto outputRes = m_getOutputRes();

    sl::Constants consts = {};

    auto camera = context.currentScene->GetPrimaryCamera().get<Components::Camera>();
    DirectX::XMMATRIX unjitteredProjectionInverse = XMMatrixInverse(nullptr, camera->info.unjitteredProjection);
    sl::float4x4 cameraViewToWorld;
    StoreFloat4x4(camera->info.viewInverse, cameraViewToWorld);
    sl::float4x4 cameraViewToWorldPrev;
    DirectX::XMMATRIX viewPrevInverse = XMMatrixInverse(nullptr, camera->info.prevView);
    StoreFloat4x4(viewPrevInverse, cameraViewToWorldPrev);
    sl::float4x4 cameraViewToPrevCameraView;
    sl::calcCameraToPrevCamera(cameraViewToPrevCameraView, cameraViewToWorld, cameraViewToWorldPrev);
    sl::float4x4 clipToPrevCameraView;

    StoreFloat4x4(camera->info.unjitteredProjection, consts.cameraViewToClip); // Projection matrix
    StoreFloat4x4(unjitteredProjectionInverse, consts.clipToCameraView); // Inverse projection matrix

    sl::matrixMul(clipToPrevCameraView, consts.clipToCameraView, cameraViewToPrevCameraView);

    sl::float4x4 cameraViewToClipPrev;
    StoreFloat4x4(camera->info.unjitteredProjection, cameraViewToClipPrev); // TODO: should we store the actual previous prjection matrix?
    sl::matrixMul(consts.clipToPrevClip, clipToPrevCameraView, cameraViewToClipPrev); // Transform between current and previous clip space
    sl::matrixFullInvert(consts.prevClipToClip, consts.clipToPrevClip); // Transform between previous and current clip space
    consts.jitterOffset.x = camera->jitterNDC.x;
    consts.jitterOffset.y = -camera->jitterNDC.y;

    // Set motion vector scaling based on your setup
    //consts.mvecScale = { 1,1 }; // Values in eMotionVectors are in [-1,1] range
    consts.mvecScale = { 1.0f / renderRes.x, 1.0f / renderRes.y }; // Values in eMotionVectors are in pixel space
    consts.cameraPinholeOffset = { 0, 0 };
    //consts.mvecScale = myCustomScaling; // Custom scaling to ensure values end up in [-1,1] range

    consts.cameraPos = { camera->info.positionWorldSpace.x, camera->info.positionWorldSpace.y, camera->info.positionWorldSpace.z };

    auto basisVectors = GetBasisVectors3f(camera->info.view);
    consts.cameraUp = { basisVectors.Up.x, basisVectors.Up.y, basisVectors.Up.z };
    consts.cameraRight = { basisVectors.Right.x, basisVectors.Right.y, basisVectors.Right.z };
    consts.cameraFwd = { basisVectors.Forward.x, basisVectors.Forward.y, basisVectors.Forward.z };

    consts.cameraNear = camera->info.zNear;
    consts.cameraFar = camera->info.zFar;
    consts.cameraFOV = camera->info.fov;
    consts.cameraAspectRatio = camera->info.aspectRatio;
    consts.depthInverted = sl::Boolean::eFalse;
    consts.cameraMotionIncluded = sl::Boolean::eTrue;
    consts.motionVectors3D = sl::Boolean::eFalse;
    consts.reset = sl::Boolean::eFalse;

    if (SL_FAILED(result, slSetConstants(consts, *frameToken, myViewport))) // constants are changing per frame so frame index is required
    {
        spdlog::error("Failed to set DLSS constants");
    }

    sl::Resource colorIn = { sl::ResourceType::eTex2d, (void*)pHDRTarget->GetAPIResource(), nullptr, nullptr, D3D12_RESOURCE_STATE_COMMON };
    sl::Resource colorOut = { sl::ResourceType::eTex2d, pUpscaledHDRTarget->GetAPIResource(), nullptr, nullptr, D3D12_RESOURCE_STATE_COMMON };
    sl::Resource depth = { sl::ResourceType::eTex2d, pDepthTexture->GetAPIResource(), nullptr, nullptr, D3D12_RESOURCE_STATE_COMMON };
    sl::Resource mvec = { sl::ResourceType::eTex2d, pMotionVectors->GetAPIResource(), nullptr, nullptr, D3D12_RESOURCE_STATE_COMMON };
    //sl::Resource exposure = { sl::ResourceType::Tex2d, myExposureBuffer, nullptr, nullptr, nullptr }; // TODO

    sl::Extent renderExtent = { 0, 0, renderRes.x, renderRes.y };
    sl::Extent upscaleExtent = { 0, 0, outputRes.x, outputRes.y };

    sl::ResourceTag colorInTag = sl::ResourceTag{ &colorIn, sl::kBufferTypeScalingInputColor, sl::ResourceLifecycle::eValidUntilPresent, &renderExtent };
    sl::ResourceTag colorOutTag = sl::ResourceTag{ &colorOut, sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eValidUntilPresent, &upscaleExtent };
    sl::ResourceTag depthTag = sl::ResourceTag{ &depth, sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilPresent, &renderExtent };
    sl::ResourceTag mvecTag = sl::ResourceTag{ &mvec, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilPresent, &renderExtent };

    const sl::BaseStructure* inputs[] = { &myViewport, &depthTag, &mvecTag, &colorInTag, &colorOutTag };

    if (SL_FAILED(result, slEvaluateFeature(sl::kFeatureDLSS, *frameToken, inputs, _countof(inputs), context.commandList)))
    {
        spdlog::error("DLSS evaluation failed!");
    }
    else
    {
        // IMPORTANT: Host is responsible for restoring state on the command list used
        //restoreState(myCmdList); ??
    }
}

void UpscalingManager::Evaluate(const RenderContext& context, PixelBuffer* pHDRTarget, PixelBuffer* pUpscaledHDRTarget, PixelBuffer* pDepthTexture, PixelBuffer* pMotionVectors) {
    switch (m_upscalingMode)
    {
        case UpscalingMode::DLSS:
			EvaluateDLSS(context, pHDRTarget, pUpscaledHDRTarget, pDepthTexture, pMotionVectors);
            break;
        case UpscalingMode::FSR3:
            // FSR3 evaluation
            break;
	}
}

void UpscalingManager::Shutdown() {

}