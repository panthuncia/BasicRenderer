#include "Managers/Singletons/FFXManager.h"

#include "Managers/Singletons/SettingsManager.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Resources/PixelBuffer.h"
#include "ThirdParty/FFX/host/ffx_sssr.h"
#include "ThirdParty/FFX/host/backends/dx12/ffx_dx12.h"
#include "Managers/Singletons/ResourceManager.h"
#include "Scene/Scene.h"

extern ffxFunctions ffxModule;

FfxResource getFFXResource(Resource* resource, const wchar_t* name, FfxResourceStates state) {
    auto desc = ffxGetResourceDescriptionDX12(resource->GetAPIResource(), FFX_RESOURCE_USAGE_READ_ONLY);
    auto ffxResource = ffxGetResourceDX12(resource->GetAPIResource(), desc, name, state);
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

    auto device = ffxGetDeviceDX12(DeviceManager::GetInstance().GetDevice().Get());
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
	sssrDesc.output = getFFXResource(pReflectionsTarget, L"Reflections", FFX_RESOURCE_STATE_UNORDERED_ACCESS);

    auto camera = context.currentScene->GetPrimaryCamera().get<Components::Camera>();
    auto invViewProjection = DirectX::XMMatrixInverse(nullptr, camera->info.viewProjection);
    auto prevViewProjection = DirectX::XMMatrixMultiply(camera->info.prevView, camera->info.prevJitteredProjection);

    DirectX::XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4*>(sssrDesc.invViewProjection), invViewProjection);
    DirectX::XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4*>(sssrDesc.projection), camera->info.jitteredProjection);
	DirectX::XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4*>(sssrDesc.invProjection), camera->info.projectionInverse);
	DirectX::XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4*>(sssrDesc.view), camera->info.view);
	DirectX::XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4*>(sssrDesc.invView), camera->info.viewInverse);
    DirectX::XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4*>(sssrDesc.prevViewProjection), prevViewProjection);
    sssrDesc.commandList = context.commandList;

    sssrDesc.renderSize = { m_getRenderRes().x, m_getRenderRes().y };
	sssrDesc.motionVectorScale = { -0.5, 0.5 };
    sssrDesc.normalUnPackMul = 1.0f;
	sssrDesc.normalUnPackAdd = 0.0f;
	sssrDesc.roughnessChannel = 1; // metallic roughness texture, roughness is in channel 1
    sssrDesc.isRoughnessPerceptual = false;
	sssrDesc.temporalStabilityFactor = 0.5f; // TODO: make everything below configurable
    sssrDesc.iblFactor = 0.2f;
	sssrDesc.depthBufferThickness = 0.01f;
	sssrDesc.roughnessThreshold = 0.2f;
	sssrDesc.varianceThreshold = 0.1f;
	sssrDesc.maxTraversalIntersections = 20;
    sssrDesc.minTraversalOccupancy = 10;
	sssrDesc.mostDetailedMip = 0;
    sssrDesc.samplesPerQuad = 1;
	sssrDesc.temporalVarianceGuidedTracingEnabled = true;

	ffxSssrContextDispatch(&m_sssrContext, &sssrDesc);
    
}