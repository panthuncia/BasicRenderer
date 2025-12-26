#include "Managers/Singletons/UpscalingManager.h"

#include <spdlog/spdlog.h>
#include <flecs.h>
#include <rhi.h>

#include "ThirdParty/FFX/dx12/ffx_api_dx12.hpp"
#include "slHooks.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Render/RenderContext.h"
#include "Resources/PixelBuffer.h"
#include "Scene/Scene.h"
#include "Utilities/MathUtils.h"
#include "Utilities/Utilities.h"

#include <sl.h>
#include <sl_consts.h>
#include <sl_dlss.h>
#include "rhi_interop_dx12.h"

PFunCreateDXGIFactory slCreateDXGIFactory = nullptr;
PFunCreateDXGIFactory1 slCreateDXGIFactory1 = nullptr;
PFunCreateDXGIFactory2 slCreateDXGIFactory2 = nullptr;
PFunDXGIGetDebugInterface1 slDXGIGetDebugInterface1 = nullptr;
PFunD3D12CreateDevice slD3D12CreateDevice = nullptr;
decltype(&slUpgradeInterface) slGetUpgradeInterface = nullptr;

void SlLogMessageCallback(sl::LogType level, const char* message) {
    //spdlog::info("Streamline Log: {}", message);
}

static ID3D12Device* dx12_device() { return rhi::dx12::get_device(DeviceManager::GetInstance().GetDevice()); }
static IDXGIFactory7* dx12_factory() { return rhi::dx12::get_factory(DeviceManager::GetInstance().GetDevice()); }
static IDXGIAdapter4* dx12_adapter() { return rhi::dx12::get_adapter(DeviceManager::GetInstance().GetDevice()); }

ffxFunctions ffxModule;

FfxApiResource getFFXResource(Resource* resource, const wchar_t* name, FfxApiResourceState state) {
	//auto desc = ffx::ApiGetResourceDescriptionDX12(resource->GetAPIResource(), FFX_API_RESOURCE_USAGE_READ_ONLY);
	auto ffxResource = ffxApiGetResourceDX12(rhi::dx12::get_resource(resource->GetAPIResource()), state);
    if (ffxResource.resource == nullptr) {
        spdlog::error("Failed to get FFX resource for resource");
	}
    return ffxResource;
}

bool CheckDLSSSupport(rhi::Device dev) {
    IDXGIAdapter4* ad = rhi::dx12::get_adapter(dev);
    if (!ad) {
        return false;
    }
    DXGI_ADAPTER_DESC desc{};
    if (FAILED(ad->GetDesc(&desc))) {
        return false;
    }

    sl::AdapterInfo ai{};
    ai.deviceLUID = reinterpret_cast<uint8_t*>(&desc.AdapterLuid);
    ai.deviceLUIDSizeInBytes = sizeof(LUID);

    sl::Result res = sl::Result::eOk;
    if (SL_FAILED(res, slIsFeatureSupported(sl::kFeatureDLSS, ai))) {
        return false;
    }
    return true;
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

void UpscalingManager::InitializeAdapter()
{
    auto dev = DeviceManager::GetInstance().GetDevice();
	m_dlssSupported = CheckDLSSSupport(dev); // TODO: Query from RHI
}

void UpscalingManager::ProxyDevice() { // TODO: RHI now handles this internally
    switch (m_upscalingMode)
    {
    case UpscalingMode::DLSS: {
        break;
    }
    case UpscalingMode::FSR3: {
        break;
    }
    default:
		break;
    }
}

bool UpscalingManager::InitFFX() {
    m_getRenderRes = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT2>("renderResolution");
    m_getOutputRes = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT2>("outputResolution");
    auto outputRes = m_getOutputRes();
    auto renderRes = m_getRenderRes();
	auto module = LoadLibrary(L"amd_fidelityfx_dx12.dll");
    if (module) {
        ffxLoadFunctions(&ffxModule, module);

        ffx::CreateBackendDX12Desc backendDesc{};
        backendDesc.device = rhi::dx12::get_device(DeviceManager::GetInstance().GetDevice());

        ffx::CreateContextDescUpscale createUpscaling;
        createUpscaling.maxUpscaleSize = { outputRes.x, outputRes.y };
        createUpscaling.maxRenderSize = { renderRes.x, renderRes.y };
        createUpscaling.flags = FFX_UPSCALE_ENABLE_AUTO_EXPOSURE | FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE;

        ffx::CreateContext(m_fsrUpscalingContext, nullptr, createUpscaling, backendDesc);

		return true;
    }
	return false;
}

DirectX::XMFLOAT2 UpscalingManager::GetJitter(uint64_t frameNumber) {

    switch (m_upscalingMode)
    {
    case UpscalingMode::None: {
        // No upscaling, no jitter
        return { 0.0f, 0.0f };
		break;
    }
    case UpscalingMode::DLSS: {
        unsigned int sequenceLength = 16;
        unsigned int sequenceIndex = frameNumber % sequenceLength;
        DirectX::XMFLOAT2 sequenceOffset = {
            2.0f * Halton(sequenceIndex + 1, 2) - 1.0f,
            2.0f * Halton(sequenceIndex + 1, 3) - 1.0f };
        return sequenceOffset;
        break;
    }
    case UpscalingMode::FSR3: {
        auto displayWidth = m_getOutputRes().x;
        auto renderWidth = m_getRenderRes().x;
        float jitterX = 0.0f, jitterY = 0.0f;
        ffx::ReturnCode                     retCode;
        int32_t                             jitterPhaseCount;
        ffx::QueryDescUpscaleGetJitterPhaseCount getJitterPhaseDesc{};
        getJitterPhaseDesc.displayWidth = displayWidth;
        getJitterPhaseDesc.renderWidth = renderWidth;
        getJitterPhaseDesc.pOutPhaseCount = &jitterPhaseCount;
        retCode = ffx::Query(m_fsrUpscalingContext, getJitterPhaseDesc);

        ffx::QueryDescUpscaleGetJitterOffset getJitterOffsetDesc{};
        getJitterOffsetDesc.index = frameNumber % jitterPhaseCount;
        getJitterOffsetDesc.phaseCount = jitterPhaseCount;
        getJitterOffsetDesc.pOutX = &jitterX;
        getJitterOffsetDesc.pOutY = &jitterY;

        retCode = ffx::Query(m_fsrUpscalingContext, getJitterOffsetDesc);

        return { jitterX, jitterY };
    }
    default:
		return { 0.0f, 0.0f };
		break;
    }
}

bool UpscalingManager::InitSL() {

    m_numFramesInFlight = SettingsManager::GetInstance().getSettingGetter<uint8_t>("numFramesInFlight")();
    m_frameTokens.resize(m_numFramesInFlight);
    for (uint32_t i = 0; i < m_numFramesInFlight; i++) {
        slGetNewFrameToken(m_frameTokens[i], &i);
    }

    return true;
}

void UpscalingManager::Setup() {
    auto outputRes = m_getOutputRes();
    auto renderRes = m_getRenderRes();
    switch (m_upscalingMode)
    {
    case UpscalingMode::None: {
        // No upscaling, just set the render resolution to the output resolution
        SettingsManager::GetInstance().getSettingSetter<DirectX::XMUINT2>("renderResolution")(outputRes);
        break;
	}
    case UpscalingMode::DLSS: {
        sl::DLSSOptimalSettings dlssSettings;
        sl::DLSSOptions dlssOptions = {};
        // These are populated based on user selection in the UI
        dlssOptions.mode = ToSLQualityMode(m_upscaleQualityMode);
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
    case UpscalingMode::FSR3: {
        DirectX::XMUINT2 optimalRenderRes = {};
        ffxQueryDescUpscaleGetRenderResolutionFromQualityMode queryDesc{};
        queryDesc.header.type = FFX_API_QUERY_DESC_TYPE_UPSCALE_GETRENDERRESOLUTIONFROMQUALITYMODE;
        queryDesc.qualityMode = ToFFXQualityMode(m_upscaleQualityMode);
		queryDesc.displayHeight = outputRes.y;
		queryDesc.displayWidth = outputRes.x;
        queryDesc.pOutRenderWidth = &optimalRenderRes.x;
        queryDesc.pOutRenderHeight = &optimalRenderRes.y;

		ffx::Query(m_fsrUpscalingContext, queryDesc);

        SettingsManager::GetInstance().getSettingSetter<DirectX::XMUINT2>("renderResolution")(optimalRenderRes);
        
        break;
    }
    }
}

void UpscalingManager::EvaluateDLSS(RenderContext& context, PixelBuffer* pHDRTarget, PixelBuffer* pUpscaledHDRTarget, PixelBuffer* pDepthTexture, PixelBuffer* pMotionVectors) {
    auto frameToken = m_frameTokens[context.frameIndex];
    auto myViewport = sl::ViewportHandle(0); // 0 is the default viewport
    auto renderRes = m_getRenderRes();
    auto outputRes = m_getOutputRes();

    sl::Constants consts = {};

    auto& camera = context.currentScene->GetPrimaryCamera().get<Components::Camera>();
    DirectX::XMMATRIX unjitteredProjectionInverse = XMMatrixInverse(nullptr, camera.info.unjitteredProjection);
    sl::float4x4 cameraViewToWorld;
    StoreFloat4x4(camera.info.viewInverse, cameraViewToWorld);
    sl::float4x4 cameraViewToWorldPrev;
    DirectX::XMMATRIX viewPrevInverse = XMMatrixInverse(nullptr, camera.info.prevView);
    StoreFloat4x4(viewPrevInverse, cameraViewToWorldPrev);
    sl::float4x4 cameraViewToPrevCameraView;
    sl::calcCameraToPrevCamera(cameraViewToPrevCameraView, cameraViewToWorld, cameraViewToWorldPrev);
    sl::float4x4 clipToPrevCameraView;

    StoreFloat4x4(camera.info.unjitteredProjection, consts.cameraViewToClip); // Projection matrix
    StoreFloat4x4(unjitteredProjectionInverse, consts.clipToCameraView); // Inverse projection matrix

    sl::matrixMul(clipToPrevCameraView, consts.clipToCameraView, cameraViewToPrevCameraView);

    sl::float4x4 cameraViewToClipPrev;
    StoreFloat4x4(camera.info.unjitteredProjection, cameraViewToClipPrev); // TODO: should we store the actual previous prjection matrix?
    sl::matrixMul(consts.clipToPrevClip, clipToPrevCameraView, cameraViewToClipPrev); // Transform between current and previous clip space
    sl::matrixFullInvert(consts.prevClipToClip, consts.clipToPrevClip); // Transform between previous and current clip space
	consts.jitterOffset.x = camera.jitterNDC.x; // Docs say this should be pixel space, but that causes screen shaking. Not sure what's wrong.
    consts.jitterOffset.y = -camera.jitterNDC.y;

    // Set motion vector scaling based on your setup
    //consts.mvecScale = { -1,1 }; // Values in eMotionVectors are in [-1,1] range
	consts.mvecScale = { -1.0f / renderRes.x, 1.0f / renderRes.y }; // Values in eMotionVectors are in pixel space // TODO: I don't think this is right, but {-1, 1} looks more wrong
    //consts.mvecScale = myCustomScaling; // Custom scaling to ensure values end up in [-1,1] range

    consts.cameraPinholeOffset = { 0, 0 };
    consts.cameraPos = { camera.info.positionWorldSpace.x, camera.info.positionWorldSpace.y, camera.info.positionWorldSpace.z };

    auto basisVectors = GetBasisVectors3f(camera.info.view);
    consts.cameraUp = { basisVectors.Up.x, basisVectors.Up.y, basisVectors.Up.z };
    consts.cameraRight = { basisVectors.Right.x, basisVectors.Right.y, basisVectors.Right.z };
    consts.cameraFwd = { basisVectors.Forward.x, basisVectors.Forward.y, basisVectors.Forward.z };

    consts.cameraNear = camera.info.zNear;
    consts.cameraFar = camera.info.zFar;
    consts.cameraFOV = camera.info.fov;
    consts.cameraAspectRatio = camera.info.aspectRatio;
    consts.depthInverted = sl::Boolean::eFalse;
    consts.cameraMotionIncluded = sl::Boolean::eTrue;
    consts.motionVectors3D = sl::Boolean::eFalse;
    consts.reset = sl::Boolean::eFalse;

    if (SL_FAILED(result, slSetConstants(consts, *frameToken, myViewport))) // constants are changing per frame so frame index is required
    {
        spdlog::error("Failed to set DLSS constants");
    }

    sl::Resource colorIn = { sl::ResourceType::eTex2d, (void*)rhi::dx12::get_resource(pHDRTarget->GetAPIResource()), nullptr, nullptr, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE };
    sl::Resource colorOut = { sl::ResourceType::eTex2d, rhi::dx12::get_resource(pUpscaledHDRTarget->GetAPIResource()), nullptr, nullptr, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE };
    sl::Resource depth = { sl::ResourceType::eTex2d, rhi::dx12::get_resource(pDepthTexture->GetAPIResource()), nullptr, nullptr, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE };
    sl::Resource mvec = { sl::ResourceType::eTex2d, rhi::dx12::get_resource(pMotionVectors->GetAPIResource()), nullptr, nullptr, D3D12_RESOURCE_STATE_UNORDERED_ACCESS };
    //sl::Resource exposure = { sl::ResourceType::Tex2d, myExposureBuffer, nullptr, nullptr, nullptr }; // TODO

    sl::Extent renderExtent = { 0, 0, renderRes.x, renderRes.y };
    sl::Extent upscaleExtent = { 0, 0, outputRes.x, outputRes.y };

    sl::ResourceTag colorInTag = sl::ResourceTag{ &colorIn, sl::kBufferTypeScalingInputColor, sl::ResourceLifecycle::eValidUntilPresent, &renderExtent };
    sl::ResourceTag colorOutTag = sl::ResourceTag{ &colorOut, sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eValidUntilPresent, &upscaleExtent };
    sl::ResourceTag depthTag = sl::ResourceTag{ &depth, sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilPresent, &renderExtent };
    sl::ResourceTag mvecTag = sl::ResourceTag{ &mvec, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilPresent, &renderExtent };

    const sl::BaseStructure* inputs[] = { &myViewport, &depthTag, &mvecTag, &colorInTag, &colorOutTag };

    if (SL_FAILED(result, slEvaluateFeature(sl::kFeatureDLSS, *frameToken, inputs, _countof(inputs), rhi::dx12::get_cmd_list(context.commandList))))
    {
        spdlog::error("DLSS evaluation failed!");
    }
    else
    {
        // IMPORTANT: Host is responsible for restoring state on the command list used
        //restoreState(myCmdList); ??
    }
}

void UpscalingManager::EvaluateFSR3(RenderContext& context, PixelBuffer* pHDRTarget, PixelBuffer* pUpscaledHDRTarget, PixelBuffer* pDepthTexture, PixelBuffer* pMotionVectors) {
    ffx::DispatchDescUpscale dispatchUpscale{};

    auto& camera = context.currentScene->GetPrimaryCamera().get<Components::Camera>();

    dispatchUpscale.commandList = rhi::dx12::get_cmd_list(context.commandList);

    dispatchUpscale.color = getFFXResource(pHDRTarget, L"UpscaleColorIn", FFX_API_RESOURCE_STATE_COMMON);
	dispatchUpscale.depth = getFFXResource(pDepthTexture, L"UpscaleDepth", FFX_API_RESOURCE_STATE_COMMON);
	dispatchUpscale.motionVectors = getFFXResource(pMotionVectors, L"UpscaleMotionVectors", FFX_API_RESOURCE_STATE_COMMON);
	dispatchUpscale.output = getFFXResource(pUpscaledHDRTarget, L"UpscaleColorOut", FFX_API_RESOURCE_STATE_COMMON);
    //dispatchUpscale.reactive;
    //dispatchUpscale.transparencyAndComposition;

	auto renderRes = m_getRenderRes();
	auto outputRes = m_getOutputRes();

    // Jitter is calculated earlier in the frame using a callback from the camera update
    dispatchUpscale.jitterOffset.x = camera.jitterPixelSpace.x;
    dispatchUpscale.jitterOffset.y = -camera.jitterPixelSpace.y;
	dispatchUpscale.motionVectorScale.x = -static_cast<float>(renderRes.x); // FFX expects left-handed, we use right-handed
    dispatchUpscale.motionVectorScale.y = static_cast<float>(renderRes.y);
    dispatchUpscale.reset = false;
    dispatchUpscale.enableSharpening = false;
    //dispatchUpscale.sharpness = m_Sharpness;

    // Engine keeps time in seconds, but FSR expects milliseconds
    dispatchUpscale.frameTimeDelta = static_cast<float>(context.deltaTime * 1000.f);

    //dispatchUpscale.preExposure = GetScene()->GetSceneExposure();
    dispatchUpscale.renderSize.width = renderRes.x;
    dispatchUpscale.renderSize.height = renderRes.y;
    dispatchUpscale.upscaleSize.width = outputRes.x;
    dispatchUpscale.upscaleSize.height = outputRes.y;

    // Setup camera params as required
    dispatchUpscale.cameraFovAngleVertical = camera.fov;

    bool s_InvertedDepth = false;
    if (s_InvertedDepth) // TODO: FFX docs says this is preferred. Why?
    {
        dispatchUpscale.cameraFar = camera.zNear;
        dispatchUpscale.cameraNear = FLT_MAX;
    }
    else
    {
        dispatchUpscale.cameraFar = camera.zFar;
        dispatchUpscale.cameraNear = camera.zNear;
    }

    ffx::Dispatch(m_fsrUpscalingContext, dispatchUpscale);
}

void UpscalingManager::EvaluateNone(RenderContext& context, PixelBuffer* pHDRTarget, PixelBuffer* pUpscaledHDRTarget, PixelBuffer* pDepthTexture, PixelBuffer* pMotionVectors) {
    UINT mipSlice = 0;
    UINT arraySlice = 0;
    UINT dstSubresource = CalcSubresource(
        /*MipSlice=*/mipSlice,
        /*ArraySlice=*/arraySlice,
        /*PlaneSlice=*/0,
        /*TotalMipCount=*/pUpscaledHDRTarget->GetNumSRVMipLevels(),
        /*ArraySize=*/1);

    rhi::TextureCopyRegion dst = {
        .texture = pUpscaledHDRTarget->GetAPIResource().GetHandle(),
        .mip = mipSlice,
        .arraySlice = arraySlice,
        .x = 0,
        .y = 0,
        .z = 0,
        .depth = 1,
    };
    rhi::TextureCopyRegion src = {
        .texture = pHDRTarget->GetAPIResource().GetHandle(),
        .mip = mipSlice,
        .arraySlice = arraySlice,
        .x = 0,
        .y = 0,
        .z = 0,
        .depth = 1,
    };

    context.commandList.CopyTextureRegion(dst, src);
}

void UpscalingManager::Evaluate(RenderContext& context, PixelBuffer* pHDRTarget, PixelBuffer* pUpscaledHDRTarget, PixelBuffer* pDepthTexture, PixelBuffer* pMotionVectors) {
    switch (m_upscalingMode)
    {
	    case UpscalingMode::None:
            EvaluateNone(context, pHDRTarget, pUpscaledHDRTarget, pDepthTexture, pMotionVectors);
			break;
        case UpscalingMode::DLSS:
			EvaluateDLSS(context, pHDRTarget, pUpscaledHDRTarget, pDepthTexture, pMotionVectors);
            break;
        case UpscalingMode::FSR3:
			EvaluateFSR3(context, pHDRTarget, pUpscaledHDRTarget, pDepthTexture, pMotionVectors);
            break;
	}
}

void UpscalingManager::Shutdown() {

}