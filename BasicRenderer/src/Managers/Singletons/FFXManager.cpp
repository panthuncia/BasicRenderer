#include "Managers/Singletons/FFXManager.h"

#include "Managers/Singletons/SettingsManager.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Resources/PixelBuffer.h"
#include "ThirdParty/FFX/host/ffx_sssr.h"
#include "ThirdParty/FFX/host/backends/dx12/ffx_dx12.h"
#include "Managers/Singletons/ResourceManager.h"
#include "Scene/Scene.h"
#include "rhi_interop_dx12.h"

extern ffxFunctions ffxModule;

FfxResource getFFXResource(Resource* resource, const wchar_t* name, FfxResourceStates state) {
    auto desc = ffxGetResourceDescriptionDX12(rhi::dx12::get_resource(resource->GetAPIResource()), FFX_RESOURCE_USAGE_READ_ONLY);
    auto ffxResource = ffxGetResourceDX12(rhi::dx12::get_resource(resource->GetAPIResource()), desc, name, state);
    if (ffxResource.resource == nullptr) {
        spdlog::error("Failed to get FFX resource for resource");
    }
    return ffxResource;
}

bool FFXManager::InitFFX() {
    m_getRenderRes = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT2>("renderResolution");
    m_getOutputRes = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT2>("outputResolution");
    auto outputRes = m_getOutputRes();
    auto renderRes = m_getRenderRes();

    auto device = ffxGetDeviceDX12(rhi::dx12::get_device(DeviceManager::GetInstance().GetDevice()));
    auto scratchMemorySize = ffxGetScratchMemorySizeDX12(10);

    // Why can't FFX allocate this itself?
    m_pScratchMemory = malloc(scratchMemorySize);

    if (!m_pScratchMemory) {
        spdlog::error("Failed to allocate scratch memory for FFX SSRR");
        return false;
	}

    memset(m_pScratchMemory, 0, scratchMemorySize);

    ffxGetInterfaceDX12(&m_backendInterface, device, m_pScratchMemory, scratchMemorySize, 1);

	FfxSssrContextDescription sssrDesc{};
	sssrDesc.backendInterface = m_backendInterface;
    sssrDesc.normalsHistoryBufferFormat = FFX_SURFACE_FORMAT_R32G32B32A32_TYPELESS;
	sssrDesc.renderSize = { renderRes.x, renderRes.y };

    ffxSssrContextCreate(&m_sssrContext, &sssrDesc);

    return true;
}

void FFXManager::Shutdown() {
	ffxSssrContextDestroy(&m_sssrContext);
    if (m_pScratchMemory) {
        free(m_pScratchMemory);
        m_pScratchMemory = nullptr;
    }
}

void FFXManager::EvaluateSSSR(const RenderContext& context,
    PixelBuffer* pHDRTarget,
    PixelBuffer* pDepthTexture,
    PixelBuffer* pNormals,
    PixelBuffer* pMetallicRoughness,
    PixelBuffer* pMotionVectors,
    PixelBuffer* pEnvironmentCubemap,
    PixelBuffer* pBRDFLUT,
    PixelBuffer* pReflectionsTarget) {

	FfxSssrDispatchDescription sssrDesc{};
	sssrDesc.brdfTexture = getFFXResource(pBRDFLUT, L"BRDFLUT", FFX_RESOURCE_STATE_COMMON);
	sssrDesc.color = getFFXResource(pHDRTarget, L"Depth", FFX_RESOURCE_STATE_COMMON);
	sssrDesc.depth = getFFXResource(pDepthTexture, L"Depth", FFX_RESOURCE_STATE_COMMON);
	sssrDesc.environmentMap = getFFXResource(pEnvironmentCubemap, L"EnvironmentMap", FFX_RESOURCE_STATE_COMMON);
	sssrDesc.materialParameters = getFFXResource(pMetallicRoughness, L"MaterialParameters", FFX_RESOURCE_STATE_COMMON);
	sssrDesc.motionVectors = getFFXResource(pMotionVectors, L"MotionVectors", FFX_RESOURCE_STATE_COMMON);
	sssrDesc.normal = getFFXResource(pNormals, L"Normals", FFX_RESOURCE_STATE_COMMON);
	sssrDesc.brdfTexture = getFFXResource(pBRDFLUT, L"BRDFLUT", FFX_RESOURCE_STATE_COMMON);
	sssrDesc.output = getFFXResource(pReflectionsTarget, L"Reflections", FFX_RESOURCE_STATE_COMMON);

    auto& camera = context.currentScene->GetPrimaryCamera().get<Components::Camera>();
    auto invViewProjection = DirectX::XMMatrixInverse(nullptr, camera.info.viewProjection);
    auto prevViewProjection = DirectX::XMMatrixMultiply(camera.info.prevView, camera.info.prevJitteredProjection);

    DirectX::XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4*>(sssrDesc.invViewProjection), invViewProjection);
    DirectX::XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4*>(sssrDesc.projection), camera.info.jitteredProjection);
	DirectX::XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4*>(sssrDesc.invProjection), camera.info.projectionInverse);
	DirectX::XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4*>(sssrDesc.view), camera.info.view);
	DirectX::XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4*>(sssrDesc.invView), camera.info.viewInverse);
    DirectX::XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4*>(sssrDesc.prevViewProjection), prevViewProjection);
    sssrDesc.commandList = rhi::dx12::get_cmd_list(context.commandList);
    auto renderSize = m_getRenderRes();
    sssrDesc.renderSize = { renderSize.x, renderSize.y };
	sssrDesc.motionVectorScale = { -1.f, 1.f }; // TODO: I think these should be -.5f, .5f, but that produces black fringing in motion
    sssrDesc.normalUnPackMul = 1.0f;
	sssrDesc.normalUnPackAdd = 0.0f;
	sssrDesc.roughnessChannel = 1; // metallic roughness texture, roughness is in channel 1
    sssrDesc.isRoughnessPerceptual = true;
	sssrDesc.temporalStabilityFactor = 0.7f; // TODO: make everything below configurable
    sssrDesc.iblFactor = 1.0f;
	sssrDesc.depthBufferThickness = 0.015f;
	sssrDesc.roughnessThreshold = 0.3f;
	sssrDesc.varianceThreshold = 0.1f;
	sssrDesc.maxTraversalIntersections = 128;
    sssrDesc.minTraversalOccupancy = 4;
	sssrDesc.mostDetailedMip = 0;
    sssrDesc.samplesPerQuad = 1;
	sssrDesc.temporalVarianceGuidedTracingEnabled = true;

	ffxSssrContextDispatch(&m_sssrContext, &sssrDesc);
    
}