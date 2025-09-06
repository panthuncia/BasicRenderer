//
// Created by matth on 6/25/2024.
//

#include "Renderer.h"

#define _USE_MATH_DEFINES
#include <math.h>
#include <dxgi1_6.h>
#include <atlbase.h>
#include <filesystem>

#include <rhi_interop_dx12.h>

#include "Utilities/Utilities.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Managers/Singletons/PSOManager.h"
#include "Managers/Singletons/ResourceManager.h"
#include "Render/RenderContext.h"
#include "Render/RenderGraph.h"
#include "Render/PassBuilders.h"
#include "Resources/Buffers/DynamicBuffer.h"
#include "RenderPasses/Base/RenderPass.h"
#include "RenderPasses/ForwardRenderPass.h"
#include "RenderPasses/ShadowPass.h"
#include "Managers/Singletons/SettingsManager.h"
#include "RenderPasses/DebugRenderPass.h"
#include "RenderPasses/SkyboxRenderPass.h"
#include "RenderPasses/EnvironmentConversionPass.h"
#include "RenderPasses/EnvironmentFilterPass.h"
#include "RenderPasses/EnvironmentSHPass.h"
#include "RenderPasses/ClearUAVsPass.h"
#include "RenderPasses/ObjectCullingPass.h"
#include "RenderPasses/MeshletCullingPass.h"
#include "RenderPasses/DebugSpheresPass.h"
#include "RenderPasses/PPLLFillPass.h"
#include "RenderPasses/PPLLResolvePass.h"
#include "RenderPasses/SkinningPass.h"
#include "RenderPasses/Base/ComputePass.h"
#include "RenderPasses/ClusterGenerationPass.h"
#include "RenderPasses/LightCullingPass.h"
#include "RenderPasses/ZPrepass.h"
#include "RenderPasses/GTAO/XeGTAOFilterPass.h"
#include "RenderPasses/GTAO/XeGTAOMainPass.h"
#include "RenderPasses/GTAO/XeGTAODenoisePass.h"
#include "RenderPasses/DeferredRenderPass.h"
#include "RenderPasses/FidelityFX/Downsample.h"
#include "RenderPasses/PostProcessing/Tonemapping.h"
#include "RenderPasses/PostProcessing/Upscaling.h"
#include "RenderPasses/brdfIntegrationPass.h"
#include "RenderPasses/PostProcessing/ScreenSpaceReflectionsPass.h"
#include "RenderPasses/PostProcessing/SpecularIBLPass.h"
#include "RenderPasses/PostProcessing/luminanceHistogram.h"
#include "RenderPasses/PostProcessing/luminanceHistogramAverage.h"
#include "Resources/TextureDescription.h"
#include "Menu/Menu.h"
#include "Managers/Singletons/DeletionManager.h"
#include "Managers/Singletons/UploadManager.h"
#include "NsightAftermathGpuCrashTracker.h"
#include "Aftermath/GFSDK_Aftermath.h"
#include "NsightAftermathHelpers.h"
#include "Managers/Singletons/CommandSignatureManager.h"
#include "Managers/Singletons/ECSManager.h"
#include "Managers/IndirectCommandBufferManager.h"
#include "Utilities/MathUtils.h"
#include "Scene/MovementState.h"
#include "Animation/AnimationController.h"
#include "ThirdParty/XeGTAO.h"
#include "Managers/EnvironmentManager.h"
#include "Render/TonemapTypes.h"
#include "Managers/Singletons/StatisticsManager.h"
#include "../generated/BuiltinResources.h"
#include "Resources/ResourceIdentifier.h"
#include "Render/RenderGraphBuildHelper.h"
#include "Managers/Singletons/UpscalingManager.h"
#include "Managers/Singletons/FFXManager.h"
#include "slHooks.h"

#define VERIFY(expr) if (FAILED(expr)) { spdlog::error("Validation error!"); }

void D3D12DebugCallback(
    D3D12_MESSAGE_CATEGORY Category,
    D3D12_MESSAGE_SEVERITY Severity,
    D3D12_MESSAGE_ID ID,
    LPCSTR pDescription,
    void* pContext) {
    std::string message(pDescription);

    // Redirect messages to spdlog based on severity
    switch (Severity) {
    case D3D12_MESSAGE_SEVERITY_CORRUPTION:
        spdlog::critical("D3D12 CORRUPTION: {}", message);
        break;
    case D3D12_MESSAGE_SEVERITY_ERROR:
        spdlog::error("D3D12 ERROR: {}", message);
        break;
    case D3D12_MESSAGE_SEVERITY_WARNING:
        spdlog::warn("D3D12 WARNING: {}", message);
        break;
    case D3D12_MESSAGE_SEVERITY_INFO:
        spdlog::info("D3D12 INFO: {}", message);
        break;
    case D3D12_MESSAGE_SEVERITY_MESSAGE:
        spdlog::debug("D3D12 MESSAGE: {}", message);
        break;
    }
}

ComPtr<IDXGIAdapter1> GetMostPowerfulAdapter(IDXGIFactory7* factory)
{
    ComPtr<IDXGIAdapter1> adapter;
    ComPtr<IDXGIAdapter1> bestAdapter;
    SIZE_T maxDedicatedVideoMemory = 0;

    // Enumerate through all adapters
    for (UINT adapterIndex = 0;
        factory->EnumAdapters1(adapterIndex, &adapter) != DXGI_ERROR_NOT_FOUND;
        ++adapterIndex)
    {
        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);

        // Skip software adapters
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
        {
            continue;
        }

		// Find adapter with most video memory
        if (desc.DedicatedVideoMemory > maxDedicatedVideoMemory)
        {
            maxDedicatedVideoMemory = desc.DedicatedVideoMemory;
            bestAdapter = adapter;
        }
    }

    if (!bestAdapter)
    {
        throw std::runtime_error("No suitable GPU found.");
    }
    DXGI_ADAPTER_DESC1 desc = {};
    bestAdapter->GetDesc1(&desc);
	spdlog::info("Selected adapter: {}", ws2s(desc.Description));
    return bestAdapter;
}

void Renderer::Initialize(HWND hwnd, UINT x_res, UINT y_res) {

    auto& settingsManager = SettingsManager::GetInstance();
    settingsManager.registerSetting<uint8_t>("numFramesInFlight", 3);
    getNumFramesInFlight = settingsManager.getSettingGetter<uint8_t>("numFramesInFlight");
    settingsManager.registerSetting<DirectX::XMUINT2>("renderResolution", { x_res, y_res });
    settingsManager.registerSetting<DirectX::XMUINT2>("outputResolution", { x_res, y_res });
	UpscalingManager::GetInstance().InitSL(); // Must be called before LoadPipeline to initialize SL hooks
    LoadPipeline(hwnd, x_res, y_res);
    UpscalingManager::GetInstance().InitFFX(); // Needs device
    FFXManager::GetInstance().InitFFX();
    SetSettings();
    auto graphicsQueue = DeviceManager::GetInstance().GetGraphicsQueue();
    ResourceManager::GetInstance().Initialize(graphicsQueue);
    PSOManager::GetInstance().initialize();
    UploadManager::GetInstance().Initialize();
    DeletionManager::GetInstance().Initialize();
	CommandSignatureManager::GetInstance().Initialize();
    Menu::GetInstance().Initialize(hwnd, rhi::dx12::get_swapchain(m_swapChain.Get())); // TODO: VK imgui
	ReadbackManager::GetInstance().Initialize(m_readbackFence.Get());
	ECSManager::GetInstance().Initialize();
	StatisticsManager::GetInstance().Initialize();

    UpscalingManager::GetInstance().Setup();

    CreateTextures();
    CreateGlobalResources();

    // Initialize GPU resource managers
    m_pLightManager = LightManager::CreateUnique();
    m_pMeshManager = MeshManager::CreateUnique();
	m_pObjectManager = ObjectManager::CreateUnique();
	m_pIndirectCommandBufferManager = IndirectCommandBufferManager::CreateUnique();
	m_pCameraManager = CameraManager::CreateUnique();
	m_pEnvironmentManager = EnvironmentManager::CreateUnique();
	//ResourceManager::GetInstance().SetEnvironmentBufferDescriptorIndex(m_pEnvironmentManager->GetEnvironmentBufferSRVDescriptorIndex());
	m_pLightManager->SetCameraManager(m_pCameraManager.get()); // Light manager needs access to camera manager for shadow cameras
	m_pCameraManager->SetCommandBufferManager(m_pIndirectCommandBufferManager.get()); // Camera manager needs to make indirect command buffers
    m_pMeshManager->SetCameraManager(m_pCameraManager.get());

	m_managerInterface.SetManagers(m_pMeshManager.get(), m_pObjectManager.get(), m_pIndirectCommandBufferManager.get(), m_pCameraManager.get(), m_pLightManager.get(), m_pEnvironmentManager.get());

    auto& world = ECSManager::GetInstance().GetWorld();
    world.component<Components::GlobalMeshLibrary>().add(flecs::Exclusive);
	world.add<Components::GlobalMeshLibrary>();
    world.component<Components::DrawStats>("DrawStats").add(flecs::Exclusive);
    world.component<Components::ActiveScene>().add(flecs::OnInstantiate, flecs::Inherit);
	world.set<Components::DrawStats>({ 0, 0, 0, 0 });
	auto res = settingsManager.getSettingGetter<DirectX::XMUINT2>("renderResolution")();
	//RegisterAllSystems(world, m_pLightManager.get(), m_pMeshManager.get(), m_pObjectManager.get(), m_pIndirectCommandBufferManager.get(), m_pCameraManager.get());
    m_hierarchySystem =
        world.system<const Components::Position, const Components::Rotation, const Components::Scale, const Components::Matrix*, Components::Matrix>()
        .with<Components::Active>()
        .term_at(3).parent().cascade()
        .cached().cache_kind(flecs::QueryCacheAll)
        .each([&, res](flecs::entity entity, const Components::Position& position, const Components::Rotation& rotation, const Components::Scale& scale, const Components::Matrix* matrix, Components::Matrix& mOut) {
        XMMATRIX matRotation = XMMatrixRotationQuaternion(rotation.rot);
        XMMATRIX matTranslation = XMMatrixTranslationFromVector(position.pos);
        XMMATRIX matScale = XMMatrixScalingFromVector(scale.scale);
        mOut.matrix = (matScale * matRotation * matTranslation);
        if (matrix != nullptr) {
            mOut.matrix = mOut.matrix * matrix->matrix;
        }

        if (entity.has<Components::RenderableObject>() && entity.has<Components::ObjectDrawInfo>()) {
            Components::RenderableObject& object = entity.get_mut<Components::RenderableObject>();
            Components::ObjectDrawInfo& drawInfo = entity.get_mut<Components::ObjectDrawInfo>();

            object.perObjectCB.prevModelMatrix = object.perObjectCB.modelMatrix;
            object.perObjectCB.modelMatrix = mOut.matrix;
            m_managerInterface.GetObjectManager()->UpdatePerObjectBuffer(drawInfo.perObjectCBView.get(), object.perObjectCB);

            auto& modelMatrix = object.perObjectCB.modelMatrix;
            XMMATRIX upperLeft3x3 = XMMatrixSet(
                XMVectorGetX(modelMatrix.r[0]), XMVectorGetY(modelMatrix.r[0]), XMVectorGetZ(modelMatrix.r[0]), 0.0f,
                XMVectorGetX(modelMatrix.r[1]), XMVectorGetY(modelMatrix.r[1]), XMVectorGetZ(modelMatrix.r[1]), 0.0f,
                XMVectorGetX(modelMatrix.r[2]), XMVectorGetY(modelMatrix.r[2]), XMVectorGetZ(modelMatrix.r[2]), 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f
            );
            XMMATRIX normalMat = XMMatrixInverse(nullptr, upperLeft3x3);
            m_managerInterface.GetObjectManager()->UpdateNormalMatrixBuffer(drawInfo.normalMatrixView.get(), &normalMat);
        }

        if (entity.has<Components::Camera>() && entity.has<Components::RenderView>()) {
            auto cameraModel = RemoveScalingFromMatrix(mOut.matrix);

            Components::Camera& camera = entity.get_mut<Components::Camera>();

			auto view = XMMatrixInverse(nullptr, cameraModel);
			DirectX::XMMATRIX projection = camera.info.unjitteredProjection;
			camera.info.prevJitteredProjection = camera.info.jitteredProjection; // Save previous jittered projection matrix
            if (m_jitter && entity.has<Components::PrimaryCamera>()) {
                // Apply jitter
                auto jitterPixelSpace = UpscalingManager::GetInstance().GetJitter(m_totalFramesRendered);
                camera.jitterPixelSpace = jitterPixelSpace;
                DirectX::XMFLOAT2 jitterNDC = {
                    (jitterPixelSpace.x / res.x),
                    (jitterPixelSpace.y / res.y)
                };
				camera.jitterNDC = jitterNDC;
				auto jitterMatrix = DirectX::XMMatrixTranslation(jitterNDC.x, jitterNDC.y, 0.0f);
				projection = XMMatrixMultiply(projection, jitterMatrix); // Apply jitter to projection matrix
            }

			camera.info.jitteredProjection = projection; // Save jittered projection matrix
            camera.info.prevView = camera.info.view; // Save view from last frame

            camera.info.view = view;
			camera.info.viewInverse = cameraModel;
            camera.info.viewProjection = XMMatrixMultiply(camera.info.view, projection);
			camera.info.projectionInverse = XMMatrixInverse(nullptr, projection);

            auto pos = GetGlobalPositionFromMatrix(mOut.matrix);
            camera.info.positionWorldSpace = { pos.x, pos.y, pos.z, 1.0 };

            auto renderView = entity.get_mut<Components::RenderView>();
			m_managerInterface.GetCameraManager()->UpdatePerCameraBufferView(renderView.cameraBufferView.get(), camera.info);
        }

        if (entity.has<Components::Light>()) {
            Components::Light& light = entity.get_mut<Components::Light>();
            XMVECTOR worldForward = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
            light.lightInfo.dirWorldSpace = XMVector3Normalize(XMVector3TransformNormal(worldForward, mOut.matrix));
            light.lightInfo.posWorldSpace = XMVectorSet(mOut.matrix.r[3].m128_f32[0],  // _41
                mOut.matrix.r[3].m128_f32[1],  // _42
                mOut.matrix.r[3].m128_f32[2],  // _43
                1.0f);
            switch(light.lightInfo.type){
            case Components::LightType::Spot:
                light.lightInfo.boundingSphere = ComputeConeBoundingSphere(light.lightInfo.posWorldSpace, light.lightInfo.dirWorldSpace, light.lightInfo.maxRange, acos(light.lightInfo.outerConeAngle));
                break;
            case Components::LightType::Point:
                light.lightInfo.boundingSphere = { {mOut.matrix.r[3].m128_f32[0],  // _41
                    mOut.matrix.r[3].m128_f32[1],  // _42
                    mOut.matrix.r[3].m128_f32[2],  // _43
                    light.lightInfo.maxRange} };
            }

            if (light.lightInfo.shadowCaster) {
				const Components::LightViewInfo& viewInfo = entity.get<Components::LightViewInfo>();
				m_managerInterface.GetLightManager()->UpdateLightBufferView(viewInfo.lightBufferView.get(), light.lightInfo);
				m_managerInterface.GetLightManager()->UpdateLightViewInfo(entity);
            }
        }

            });
}

void Renderer::CreateGlobalResources() {
    m_coreResourceProvider.m_shadowMaps = std::make_shared<ShadowMaps>(L"ShadowMaps");
    m_coreResourceProvider.m_linearShadowMaps = std::make_shared<LinearShadowMaps>(L"linearShadowMaps");
    //m_shadowMaps->AddAliasedResource(m_downsampledShadowMaps.get());
	//m_downsampledShadowMaps->AddAliasedResource(m_shadowMaps.get());

    setShadowMaps(m_coreResourceProvider.m_shadowMaps.get()); // To allow light manager to acccess shadow maps. TODO: Is there a better way to structure this kind of access?
	setLinearShadowMaps(m_coreResourceProvider.m_linearShadowMaps.get());
}

void Renderer::SetSettings() {
	auto& settingsManager = SettingsManager::GetInstance();

    uint8_t numDirectionalCascades = 4;
	float maxShadowDistance = 30.0f;
	settingsManager.registerSetting<uint8_t>("numDirectionalLightCascades", numDirectionalCascades);
    settingsManager.registerSetting<float>("maxShadowDistance", maxShadowDistance);
    settingsManager.registerSetting<std::vector<float>>("directionalLightCascadeSplits", calculateCascadeSplits(numDirectionalCascades, 0.1f, 100, maxShadowDistance));
    settingsManager.registerSetting<uint16_t>("shadowResolution", 2048);
    settingsManager.registerSetting<float>("cameraSpeed", 10);
	settingsManager.registerSetting<ShadowMaps*>("currentShadowMapsResourceGroup", nullptr);
	settingsManager.registerSetting<LinearShadowMaps*>("currentLinearShadowMapsResourceGroup", nullptr);
	settingsManager.registerSetting<bool>("enableWireframe", false);
	settingsManager.registerSetting<bool>("enableShadows", true);
	settingsManager.registerSetting<uint16_t>("skyboxResolution", 2048);
    settingsManager.registerSetting<uint16_t>("reflectionCubemapResolution", 512);
	settingsManager.registerSetting<bool>("enableImageBasedLighting", true);
	settingsManager.registerSetting<bool>("enablePunctualLighting", true);
	settingsManager.registerSetting<std::string>("environmentName", "");
	settingsManager.registerSetting<unsigned int>("outputType", OutputType::COLOR);
	settingsManager.registerSetting<unsigned int>("tonemapType", TonemapType::AMD_LPM);
    settingsManager.registerSetting<bool>("allowTearing", false);
	settingsManager.registerSetting<bool>("drawBoundingSpheres", false);
    settingsManager.registerSetting<bool>("enableClusteredLighting", m_clusteredLighting);
    settingsManager.registerSetting<DirectX::XMUINT3>("lightClusterSize", m_lightClusterSize);
	settingsManager.registerSetting<bool>("enableDeferredRendering", m_deferredRendering);
    settingsManager.registerSetting<bool>("collectPipelineStatistics", false);
	// This feels like abuse of the settings manager, but it's the easiest way to get the renderable objects to the menu
    settingsManager.registerSetting<std::function<flecs::entity()>>("getSceneRoot", [this]() -> flecs::entity {
        return currentScene->GetRoot();
        });
    settingsManager.registerSetting<std::function<flecs::entity()>>("getSceneRoot", [this]() -> flecs::entity {
        return currentScene->GetRoot();
        });
    bool meshShaderSupported = DeviceManager::GetInstance().GetMeshShadersSupported();
	settingsManager.registerSetting<bool>("enableMeshShader", meshShaderSupported);
	settingsManager.registerSetting<bool>("enableIndirectDraws", meshShaderSupported);
	settingsManager.registerSetting<bool>("enableGTAO", true);
	settingsManager.registerSetting<bool>("enableOcclusionCulling", m_occlusionCulling);
	settingsManager.registerSetting<bool>("enableMeshletCulling", m_meshletCulling);
    settingsManager.registerSetting<bool>("enableBloom", m_bloom);
    settingsManager.registerSetting<bool>("enableJitter", m_jitter);
    settingsManager.registerSetting<std::function<std::shared_ptr<Scene>(std::shared_ptr<Scene>)>>("appendScene", [this](std::shared_ptr<Scene> scene) -> std::shared_ptr<Scene> {
        return AppendScene(scene);
        });
	settingsManager.registerSetting<UpscalingMode>("upscalingMode", UpscalingManager::GetInstance().GetCurrentUpscalingMode());
    settingsManager.registerSetting<UpscaleQualityMode>("upscalingQualityMode", UpscalingManager::GetInstance().GetCurrentUpscalingQualityMode());
	settingsManager.registerSetting<bool>("enableScreenSpaceReflections", m_screenSpaceReflections);
    settingsManager.registerSetting<bool>("useAsyncCompute", false);
	setShadowMaps = settingsManager.getSettingSetter<ShadowMaps*>("currentShadowMapsResourceGroup");
    setLinearShadowMaps = settingsManager.getSettingSetter<LinearShadowMaps*>("currentLinearShadowMapsResourceGroup");
    getShadowResolution = settingsManager.getSettingGetter<uint16_t>("shadowResolution");
    setCameraSpeed = settingsManager.getSettingSetter<float>("cameraSpeed");
	getCameraSpeed = settingsManager.getSettingGetter<float>("cameraSpeed");
	setWireframeEnabled = settingsManager.getSettingSetter<bool>("enableWireframe");
	getWireframeEnabled = settingsManager.getSettingGetter<bool>("enableWireframe");
	setShadowsEnabled = settingsManager.getSettingSetter<bool>("enableShadows");
	getShadowsEnabled = settingsManager.getSettingGetter<bool>("enableShadows");
	getSkyboxResolution = settingsManager.getSettingGetter<uint16_t>("skyboxResolution");
	setImageBasedLightingEnabled = settingsManager.getSettingSetter<bool>("enableImageBasedLighting");
	setEnvironment = settingsManager.getSettingSetter<std::string>("environmentName");
	getMeshShadersEnabled = settingsManager.getSettingGetter<bool>("enableMeshShader");
	getIndirectDrawsEnabled = settingsManager.getSettingGetter<bool>("enableIndirectDraws");
	getDrawBoundingSpheres = settingsManager.getSettingGetter<bool>("drawBoundingSpheres");
	getImageBasedLightingEnabled = settingsManager.getSettingGetter<bool>("enableImageBasedLighting");
    

    m_settingsSubscriptions.push_back(settingsManager.addObserver<bool>("enableShadows", [this](const bool& newValue) {
        // Trigger recompilation of the render graph when setting changes
        rebuildRenderGraph = true;
        }));
    m_settingsSubscriptions.push_back(settingsManager.addObserver<std::string>("environmentName", [this](const std::string& newValue) {
		SetEnvironmentInternal(s2ws(newValue));
		rebuildRenderGraph = true;
		}));
    m_settingsSubscriptions.push_back(settingsManager.addObserver<unsigned int>("outputType", [](const unsigned int& newValue) {
		ResourceManager::GetInstance().SetOutputType(newValue);
		}));
    m_settingsSubscriptions.push_back(settingsManager.addObserver<bool>("enableMeshShader", [this](const bool& newValue) {
		ToggleMeshShaders(newValue);
		rebuildRenderGraph = true;
		}));
    m_settingsSubscriptions.push_back(settingsManager.addObserver<bool>("enableWireframe", [this](const bool& newValue) {
		rebuildRenderGraph = true;
		}));
    m_settingsSubscriptions.push_back(settingsManager.addObserver<bool>("enableIndirectDraws", [this](const bool& newValue) {
		rebuildRenderGraph = true;
		}));
    m_settingsSubscriptions.push_back(settingsManager.addObserver<bool>("allowTearing", [this](const bool& newValue) {
		m_allowTearing = newValue;
		}));
    m_settingsSubscriptions.push_back(settingsManager.addObserver<bool>("drawBoundingSpheres", [this](const bool& newValue) {
		rebuildRenderGraph = true;
		}));
    m_settingsSubscriptions.push_back(settingsManager.addObserver<bool>("enableClusteredLighting", [this](const bool& newValue) {
		m_clusteredLighting = newValue;
		rebuildRenderGraph = true;
		}));
    m_settingsSubscriptions.push_back(settingsManager.addObserver<bool>("enableImageBasedLighting", [this](const bool& newValue) {
		m_imageBasedLighting = newValue;
		}));
    m_settingsSubscriptions.push_back(settingsManager.addObserver<bool>("enableGTAO", [this](const bool& newValue) {
		m_gtaoEnabled = newValue;
		rebuildRenderGraph = true;
		}));
    m_settingsSubscriptions.push_back(settingsManager.addObserver<bool>("enableDeferredRendering", [this](const bool& newValue) {
		m_deferredRendering = newValue;
		rebuildRenderGraph = true;
		}));
    m_settingsSubscriptions.push_back(settingsManager.addObserver<bool>("enableOcclusionCulling", [this](const bool& newValue) {
		m_occlusionCulling = newValue;
		rebuildRenderGraph = true;
		}));
    m_settingsSubscriptions.push_back(settingsManager.addObserver<bool>("enableMeshletCulling", [this](const bool& newValue) {
		m_meshletCulling = newValue;
		rebuildRenderGraph = true;
		}));
    m_settingsSubscriptions.push_back(settingsManager.addObserver<bool>("enableBloom", [this](const bool& newValue) {
        m_bloom = newValue;
        rebuildRenderGraph = true;
        }));
    m_settingsSubscriptions.push_back(settingsManager.addObserver<bool>("enableJitter", [this](const bool& newValue) {
        m_jitter = newValue;
        }));
    m_settingsSubscriptions.push_back(settingsManager.addObserver<float>("maxShadowDistance", [](const float& newValue) {
		auto& settingsManager = SettingsManager::GetInstance();
		auto numDirectionalCascades = settingsManager.getSettingGetter<uint8_t>("numDirectionalLightCascades")();
		auto maxShadowDistance = settingsManager.getSettingGetter<float>("maxShadowDistance")();
        settingsManager.getSettingSetter<std::vector<float>>("directionalLightCascadeSplits")(calculateCascadeSplits(numDirectionalCascades, 0.1f, 100, maxShadowDistance));
        }));
    m_settingsSubscriptions.push_back(settingsManager.addObserver<std::vector<float>>("directionalLightCascadeSplits", [](const std::vector<float>& newValue) {
		ResourceManager::GetInstance().SetDirectionalCascadeSplits(newValue);
		}));
    m_settingsSubscriptions.push_back(settingsManager.addObserver<UpscalingMode>("upscalingMode", [this](const UpscalingMode& newValue) {

        m_preFrameDeferredFunctions.defer([newValue, this]() { // Don't do this during a frame
            UpscalingManager::GetInstance().Shutdown();
            UpscalingManager::GetInstance().SetUpscalingMode(newValue);

            DeviceManager::GetInstance().Initialize(); // Re-init device manager with correct device 

            UpscalingManager::GetInstance().Setup();

            FFXManager::GetInstance().Shutdown();
            FFXManager::GetInstance().InitFFX();

            CreateTextures();
            rebuildRenderGraph = true;
            });
		}));
    m_settingsSubscriptions.push_back(settingsManager.addObserver<UpscaleQualityMode>("upscalingQualityMode", [this](const UpscaleQualityMode& newValue) {

        m_preFrameDeferredFunctions.defer([newValue, this]() { // Don't do this during a frame
            UpscalingManager::GetInstance().Shutdown();
            UpscalingManager::GetInstance().SetUpscalingQualityMode(newValue);
            UpscalingManager::GetInstance().Setup();
            FFXManager::GetInstance().Shutdown();
            FFXManager::GetInstance().InitFFX();
            CreateTextures();
            rebuildRenderGraph = true;
            });
        }));
	m_settingsSubscriptions.push_back(settingsManager.addObserver<bool>("enableScreenSpaceReflections", [this](const bool& newValue) {
		m_screenSpaceReflections = newValue;
		rebuildRenderGraph = true;
		}));
    m_settingsSubscriptions.push_back(settingsManager.addObserver<bool>("useAsyncCompute", [this](const bool& newValue) {
        rebuildRenderGraph = true;
		}));
    m_numFramesInFlight = getNumFramesInFlight();
}

void Renderer::ToggleMeshShaders(bool useMeshShaders) {
    // We need to:
    // 1. Remove all meshes in the global mesh library from the mesh manager
	// 2. Re-add them to the mesh manager
    // 3. Get all objects with mesh instances by querying the ECS
	// 4. Remove and re-add all instances to the mesh manager
	// 5. Remove and re-add all objects from the object manager to rebuild indirect draw info

	auto& world = ECSManager::GetInstance().GetWorld();
	auto& meshLibrary = world.get_mut<Components::GlobalMeshLibrary>().meshes;

	// Remove all meshes from the mesh manager
	for (auto& meshPair : meshLibrary) {
		auto& mesh = meshPair.second;
		m_pMeshManager->RemoveMesh(mesh.get());
	}
	// Re-add them to the mesh manager
	for (auto& meshPair : meshLibrary) {
		auto& mesh = meshPair.second;
        MaterialBuckets bucket = {};
        switch (mesh->material->GetBlendState()) {
        case BlendState::BLEND_STATE_OPAQUE:
			bucket = MaterialBuckets::Opaque;
			break;
		case BlendState::BLEND_STATE_MASK:
			bucket = MaterialBuckets::AlphaTest;
			break;
		case BlendState::BLEND_STATE_BLEND:
			bucket = MaterialBuckets::Blend;
			break;
        case BlendState::BLEND_STATE_UNKNOWN:
            spdlog::warn("Unknown blend state for mesh, defaulting to opaque");
            bucket = MaterialBuckets::Opaque; // Default to opaque if unknown
			break;
        }
        m_pMeshManager->AddMesh(mesh, bucket, useMeshShaders);
	}

	// Get all active objects with mesh instances by querying the ECS
    auto query = world.query_builder<Components::RenderableObject, Components::ObjectDrawInfo>().with<Components::Active>()
        .build();

    world.defer_begin();
    query.each([&](flecs::entity entity, const Components::RenderableObject& object, const Components::ObjectDrawInfo& drawInfo) {
        auto opaqueMeshInstances = entity.try_get<Components::OpaqueMeshInstances>();
        auto alphaTestMeshInstances = entity.try_get<Components::AlphaTestMeshInstances>();
        auto blendMeshInstances = entity.try_get<Components::BlendMeshInstances>();

        if (opaqueMeshInstances) {
            for (auto& meshInstance : opaqueMeshInstances->meshInstances) {
                m_pMeshManager->RemoveMeshInstance(meshInstance.get());
                m_pMeshManager->AddMeshInstance(meshInstance.get(), useMeshShaders);
            }
        }
        if (alphaTestMeshInstances) {
            for (auto& meshInstance : alphaTestMeshInstances->meshInstances) {
                m_pMeshManager->RemoveMeshInstance(meshInstance.get());
                m_pMeshManager->AddMeshInstance(meshInstance.get(), useMeshShaders);
            }
        }
        if (blendMeshInstances) {
            for (auto& meshInstance : blendMeshInstances->meshInstances) {
                m_pMeshManager->RemoveMeshInstance(meshInstance.get());
                m_pMeshManager->AddMeshInstance(meshInstance.get(), useMeshShaders);
            }
        }

		// Remove and re-add all objects from the object manager to rebuild indirect draw info
		m_pObjectManager->RemoveObject(&drawInfo);
		auto newDrawInfo = m_pObjectManager->AddObject(object.perObjectCB, opaqueMeshInstances, alphaTestMeshInstances, blendMeshInstances);
		entity.set<Components::ObjectDrawInfo>(newDrawInfo);
            });
    world.defer_end();
}

void EnableShaderBasedValidation() {
    CComPtr<ID3D12Debug> spDebugController0;
    CComPtr<ID3D12Debug1> spDebugController1;
    VERIFY(D3D12GetDebugInterface(IID_PPV_ARGS(&spDebugController0)));
    VERIFY(spDebugController0->QueryInterface(IID_PPV_ARGS(&spDebugController1)));
    spDebugController1->SetEnableGPUBasedValidation(true);
}

void Renderer::LoadPipeline(HWND hwnd, UINT x_res, UINT y_res) {
    UINT dxgiFactoryFlags = 0;

#if BUILD_TYPE == BUILD_TYPE_DEBUG
    ComPtr<ID3D12Debug> debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
        debugController->EnableDebugLayer();
        dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
    }
    EnableShaderBasedValidation();
#endif

#if defined(ENABLE_NSIGHT_AFTERMATH)
    m_gpuCrashTracker.Initialize();
#endif

    DeviceManager::GetInstance().Initialize();

	auto device = DeviceManager::GetInstance().GetDevice();
	m_swapChain = device.CreateSwapchain(hwnd, x_res, y_res, rhi::Format::R8G8B8A8_UNorm, m_numFramesInFlight, m_allowTearing);

    UpscalingManager::GetInstance().InitializeAdapter();

#if defined(ENABLE_NSIGHT_AFTERMATH)
    const uint32_t aftermathFlags =
        GFSDK_Aftermath_FeatureFlags_EnableMarkers |             // Enable event marker tracking.
        GFSDK_Aftermath_FeatureFlags_EnableResourceTracking |    // Enable tracking of resources.
        GFSDK_Aftermath_FeatureFlags_CallStackCapturing |        // Capture call stacks for all draw calls, compute dispatches, and resource copies.
        GFSDK_Aftermath_FeatureFlags_GenerateShaderDebugInfo;    // Generate debug information for shaders.

    AFTERMATH_CHECK_ERROR(GFSDK_Aftermath_DX12_Initialize(
        GFSDK_Aftermath_Version_API,
        aftermathFlags,
        device.Get()));
#endif

    // Create RTV descriptor heap
	rhi::DescriptorHeapDesc rtvHeapDesc = {};
    rtvHeapDesc.capacity = m_numFramesInFlight;
    rtvHeapDesc.type = rhi::DescriptorHeapType::RTV;
	rtvHeapDesc.shaderVisible = false;
	rtvHeapDesc.debugName = "RTV Descriptor Heap";
    rtvHeap = device.CreateDescriptorHeap(rtvHeapDesc);

    rtvDescriptorSize = device.GetDescriptorHandleIncrementSize(rhi::DescriptorHeapType::RTV);

    // Create frame resources
    renderTargets.resize(m_numFramesInFlight);
    for (UINT n = 0; n < m_numFramesInFlight; n++) {        
        renderTargets[n] = m_swapChain->Image(n);
    }

    // Create command allocator

	m_commandAllocators.resize(m_numFramesInFlight);
	m_commandLists.resize(m_numFramesInFlight);
    for (int i = 0; i < m_numFramesInFlight; i++) {
		m_commandAllocators[i] = device.CreateCommandAllocator(rhi::QueueKind::Graphics);
		m_commandLists[i] = device.CreateCommandList(rhi::QueueKind::Graphics, m_commandAllocators[i].Get());
        m_commandLists[i]->End();
    }

    // Create per-frame fence information
	m_frameFenceValues.resize(m_numFramesInFlight);
	for (int i = 0; i < m_numFramesInFlight; i++) {
		m_frameFenceValues[i] = 0;
	}

    m_frameFence = device.CreateTimeline();
	m_readbackFence = device.CreateTimeline();
}

void Renderer::CreateTextures() {
    auto resolution = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT2>("renderResolution")();
    // Create HDR color target
    TextureDescription hdrDesc;
    hdrDesc.arraySize = 1;
    hdrDesc.channels = 4; // RGBA
    hdrDesc.isCubemap = false;
    hdrDesc.hasRTV = true;
    hdrDesc.hasUAV = false;
    hdrDesc.format = rhi::Format::R16G16B16A16_Float; // HDR format
    hdrDesc.generateMipMaps = false; // For bloom downsampling
    hdrDesc.hasUAV = true;
    ImageDimensions dims;
    dims.height = resolution.y;
    dims.width = resolution.x;
    hdrDesc.imageDimensions.push_back(dims);
    auto hdrColorTarget = PixelBuffer::Create(hdrDesc);
    hdrColorTarget->SetName(L"Primary Camera HDR Color Target");
	m_coreResourceProvider.m_HDRColorTarget = hdrColorTarget;

    auto outputResolution = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT2>("outputResolution")();
    hdrDesc.imageDimensions[0].width = outputResolution.x;
    hdrDesc.imageDimensions[0].height = outputResolution.y;
    hdrDesc.generateMipMaps = true;
	auto upscaledHDRColorTarget = PixelBuffer::Create(hdrDesc);
	upscaledHDRColorTarget->SetName(L"Upscaled HDR Color Target");
	m_coreResourceProvider.m_upscaledHDRColorTarget = upscaledHDRColorTarget;

    TextureDescription motionVectors;
    motionVectors.arraySize = 1;
    motionVectors.channels = 2;
    motionVectors.isCubemap = false;
    motionVectors.hasRTV = true;
    motionVectors.format = rhi::Format::R16G16_Float;
    motionVectors.generateMipMaps = false;
    motionVectors.hasSRV = true;
    motionVectors.srvFormat = rhi::Format::R16G16_Float;
    ImageDimensions motionVectorsDims = { resolution.x, resolution.y, 0, 0 };
    motionVectors.imageDimensions.push_back(motionVectorsDims);
    auto motionVectorsBuffer = PixelBuffer::Create(motionVectors);
    motionVectorsBuffer->SetName(L"Motion Vectors");
	m_coreResourceProvider.m_gbufferMotionVectors = motionVectorsBuffer;
}

void Renderer::OnResize(UINT newWidth, UINT newHeight) {
    // Wait for the GPU to complete all operations
	WaitForFrame(m_frameIndex);

    // Release the resources tied to the swap chain
    auto numFramesInFlight = getNumFramesInFlight();

    // Resize the swap chain
	m_swapChain->ResizeBuffers(m_numFramesInFlight, newWidth, newHeight, rhi::Format::R8G8B8A8_UNorm, DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH); // TODO: Port flags to RHI

    m_frameIndex = static_cast<uint8_t>(m_swapChain->CurrentImageIndex());

	auto device = DeviceManager::GetInstance().GetDevice();
    // Recreate the render target views
    for (UINT n = 0; n < m_numFramesInFlight; n++) {
        renderTargets[n] = m_swapChain->Image(n);
		rhi::RtvDesc rtvDesc = {};
		rtvDesc.dimension = rhi::RtvDim::Texture2D;
		rtvDesc.formatOverride = rhi::Format::R8G8B8A8_UNorm;
		rtvDesc.range = { 0, 1, 0, 1 };
		rtvDesc.texture = renderTargets[n];
		device.CreateRenderTargetView({ rtvHeap->GetHandle(), n}, rtvDesc);
    }

	SettingsManager::GetInstance().getSettingSetter<DirectX::XMUINT2>("outputResolution")({ newWidth, newHeight });

    UpscalingManager::GetInstance().Shutdown();
	UpscalingManager::GetInstance().Setup();

    CreateTextures();



	//Rebuild the render graph
	rebuildRenderGraph = true;
}


void Renderer::WaitForFrame(uint8_t currentFrameIndex) {
	// Wait until the GPU has completed commands up to this fence point.
	auto device = DeviceManager::GetInstance().GetDevice();
	auto completedValue = m_frameFence->GetCompletedValue();
    if (completedValue < m_frameFenceValues[currentFrameIndex]) {
        m_frameFence->HostWait(m_frameFenceValues[currentFrameIndex]);
    }
}

void Renderer::Update(float elapsedSeconds) {
    WaitForFrame(m_frameIndex); // Wait for the previous iteration of the frame to finish

	auto& deviceManager = DeviceManager::GetInstance();
	auto graphicsQueue = deviceManager.GetGraphicsQueue();
	auto computeQueue = deviceManager.GetComputeQueue();
	StatisticsManager::GetInstance().OnFrameComplete(m_frameIndex, computeQueue); // Gather statistics for the last iteration of the frame
    StatisticsManager::GetInstance().OnFrameComplete(m_frameIndex, graphicsQueue); // Gather statistics for the last iteration of the frame

	m_preFrameDeferredFunctions.flush(); // Execute anything we deferred until now

    auto& updateManager = UploadManager::GetInstance();
    updateManager.ProcessDeferredReleases(m_frameIndex);

    if (rebuildRenderGraph) {
		CreateRenderGraph();
    }

	Components::Position& cameraPosition = currentScene->GetPrimaryCamera().get_mut<Components::Position>();
	Components::Rotation& cameraRotation = currentScene->GetPrimaryCamera().get_mut<Components::Rotation>();
	ApplyMovement(cameraPosition, cameraRotation, movementState, elapsedSeconds);
	RotatePitchYaw(cameraRotation, verticalAngle, horizontalAngle);

    //spdlog::info("horizontal angle: {}", horizontalAngle);
    //spdlog::info("vertical angle: {}", verticalAngle);
    verticalAngle = 0;
    horizontalAngle = 0;

    currentScene->Update();

    auto& world = ECSManager::GetInstance().GetWorld();
	world.progress();

    auto& camera = currentScene->GetPrimaryCamera();
    unsigned int cameraIndex = camera.get<Components::RenderView>().cameraBufferIndex;
	auto& commandAllocator = m_commandAllocators[m_frameIndex];
	auto& commandList = m_commandLists[m_frameIndex];


    commandAllocator->Recycle();
    auto& resourceManager = ResourceManager::GetInstance();
    auto res = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT2>("renderResolution")();
    resourceManager.UpdatePerFrameBuffer(cameraIndex, m_pLightManager->GetNumLights(), { res.x, res.y }, m_lightClusterSize, m_frameIndex);

	currentRenderGraph->Update();

	updateManager.ResetAllocators(m_frameIndex); // Reset allocators to avoid leaking memory
    updateManager.ExecuteResourceCopies(m_frameIndex, graphicsQueue);// copies come before uploads to avoid overwriting data
	updateManager.ProcessUploads(m_frameIndex, graphicsQueue);

    //resourceManager.ExecuteResourceTransitions();
    commandList->Recycle(commandAllocator.Get());
}

void Renderer::Render() {
    auto deltaTime = m_frameTimer.tick();
    // Record all the commands we need to render the scene into the command list
    auto& commandAllocator = m_commandAllocators[m_frameIndex];
    auto& commandList = m_commandLists[m_frameIndex];

	auto& world = ECSManager::GetInstance().GetWorld();
	const Components::DrawStats& drawStats = world.get<Components::DrawStats>();
    auto renderRes = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT2>("renderResolution")();
    auto outputRes = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT2>("outputResolution")();

	auto& deviceManager = DeviceManager::GetInstance();

    m_context.currentScene = currentScene.get();
	m_context.device = deviceManager.GetDevice();
    //m_context.commandList = commandList.Get();
	m_context.commandQueue = deviceManager.GetGraphicsQueue();
    m_context.textureDescriptorHeap = ResourceManager::GetInstance().GetSRVDescriptorHeap();
    m_context.samplerDescriptorHeap = ResourceManager::GetInstance().GetSamplerDescriptorHeap();
    m_context.rtvHeap = rtvHeap.Get();
    m_context.rtvDescriptorSize = rtvDescriptorSize;
    m_context.dsvDescriptorSize = dsvDescriptorSize;
    m_context.frameIndex = m_frameIndex;
    m_context.frameFenceValue = m_currentFrameFenceValue;
    m_context.renderResolution = { renderRes.x, renderRes.y };
	m_context.outputResolution = { outputRes.x, outputRes.y };
	m_context.cameraManager = m_pCameraManager.get();
	m_context.objectManager = m_pObjectManager.get();
	m_context.meshManager = m_pMeshManager.get();
	m_context.indirectCommandBufferManager = m_pIndirectCommandBufferManager.get();
	m_context.lightManager = m_pLightManager.get();
	m_context.environmentManager = m_pEnvironmentManager.get();
	m_context.drawStats = drawStats;
	m_context.deltaTime = deltaTime;

    unsigned int globalPSOFlags = 0;
    if (m_imageBasedLighting) {
        globalPSOFlags |= PSOFlags::PSO_IMAGE_BASED_LIGHTING;
    }
	if (m_clusteredLighting) {
		globalPSOFlags |= PSOFlags::PSO_CLUSTERED_LIGHTING;
	}
	if (m_deferredRendering) {
		globalPSOFlags |= PSOFlags::PSO_DEFERRED;
	}
    if (m_screenSpaceReflections) {
        globalPSOFlags |= PSOFlags::PSO_SCREENSPACE_REFLECTIONS;
    }
	m_context.globalPSOFlags = globalPSOFlags;

    // TODO: Incorporate this into the render graph
    // Indicate that the back buffer will be used as a render target
	rhi::TextureBarrier rtvBarrier = {};
	rtvBarrier.afterAccess = rhi::ResourceAccessType::RenderTarget;
	rtvBarrier.afterLayout = rhi::ResourceLayout::RenderTarget;
	rtvBarrier.afterSync = rhi::ResourceSyncState::All;
	rtvBarrier.beforeAccess = rhi::ResourceAccessType::Common;
	rtvBarrier.beforeLayout = rhi::ResourceLayout::Common;
	rtvBarrier.beforeSync = rhi::ResourceSyncState::All;

	rtvBarrier.texture = renderTargets[m_frameIndex];
    rhi::BarrierBatch batch = {};
	batch.textures = { &rtvBarrier };

    commandList->Barriers(batch);
   
    commandList->End();

    // Execute the command list
    auto graphicsQueue = deviceManager.GetGraphicsQueue();
    graphicsQueue.Submit({ &commandList.Get() });

	currentRenderGraph->Execute(m_context); // Main render graph execution

	Menu::GetInstance().Render(m_context); // Render menu

    commandList->Recycle(commandAllocator.Get());

    // Indicate that the back buffer will now be used to present
	rtvBarrier.afterAccess = rhi::ResourceAccessType::Common;
	rtvBarrier.afterLayout = rhi::ResourceLayout::Common;
	rtvBarrier.afterSync = rhi::ResourceSyncState::All;
	rtvBarrier.beforeAccess = rhi::ResourceAccessType::RenderTarget;
	rtvBarrier.beforeLayout = rhi::ResourceLayout::RenderTarget;
	rtvBarrier.beforeSync = rhi::ResourceSyncState::All;
	rtvBarrier.texture = renderTargets[m_frameIndex];
	batch.textures = { &rtvBarrier };
	commandList->Barriers(batch);

    commandList->End();

    // Execute the command list
    graphicsQueue.Submit({ &commandList.Get() });

    // Present the frame
    m_swapChain->Present(!m_allowTearing);

    AdvanceFrameIndex();

    SignalFence(graphicsQueue, m_frameIndex);

	ReadbackManager::GetInstance().ProcessReadbackRequests(); // Save images to disk if requested

    DeletionManager::GetInstance().ProcessDeletions();
}

void Renderer::SignalFence(rhi::Queue commandQueue, uint8_t frameIndexToSignal) {
    // Signal the fence
    m_currentFrameFenceValue++;
	commandQueue.Signal({ m_frameFence->GetHandle(), m_currentFrameFenceValue });

    // Store the fence value for the current frame
    m_frameFenceValues[frameIndexToSignal] = m_currentFrameFenceValue;
}

void Renderer::AdvanceFrameIndex() {
    m_frameIndex = (m_frameIndex + 1) % m_numFramesInFlight;
    m_totalFramesRendered += 1;
}

void Renderer::FlushCommandQueue() {
    // Create a fence and an event to wait on

	auto device = DeviceManager::GetInstance().GetDevice();
    rhi::TimelinePtr flushFence = device.CreateTimeline();

	auto graphicsQueue = DeviceManager::GetInstance().GetGraphicsQueue();
    auto computeQueue = DeviceManager::GetInstance().GetComputeQueue();

    // Signal the fence and wait
    graphicsQueue.Signal({ flushFence->GetHandle(), 1 });
	computeQueue.Signal({ flushFence->GetHandle(), 2 });
    
	flushFence->HostWait(1);
    flushFence->HostWait(2);
}

void Renderer::StallPipeline() {
    for (uint8_t i = 0; i < m_numFramesInFlight; ++i) {
        WaitForFrame(i);
    }
    FlushCommandQueue();
}

void Renderer::Cleanup() {
    spdlog::info("In cleanup");
    // Wait for all GPU frames to complete
	StallPipeline();
    currentRenderGraph.reset();
	currentScene.reset();
	m_pIndirectCommandBufferManager.reset();
	m_pCameraManager.reset();
	m_pLightManager.reset();
	m_pMeshManager.reset();
	m_pObjectManager.reset();
    m_hierarchySystem.destruct();
    m_settingsSubscriptions.clear();
    Material::DestroyDefaultMaterial();
    DeletionManager::GetInstance().Cleanup();
}

void Renderer::CheckDebugMessages() {
    //ComPtr<ID3D12InfoQueue> infoQueue;
    //if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&infoQueue)))) {
    //    UINT64 messageCount = infoQueue->GetNumStoredMessages();
    //    for (UINT64 i = 0; i < messageCount; ++i) {
    //        SIZE_T messageLength = 0;
    //        infoQueue->GetMessage(i, nullptr, &messageLength);
    //        D3D12_MESSAGE* pMessage = (D3D12_MESSAGE*)malloc(messageLength);
    //        infoQueue->GetMessage(i, pMessage, &messageLength);
    //        std::cerr << "D3D12 Debug Message: " << pMessage->pDescription << std::endl;
    //        free(pMessage);
    //    }
    //    infoQueue->ClearStoredMessages();
    //}
}

void Renderer::SetEnvironment(std::string environmentName) {
	setEnvironment(environmentName);
}

std::shared_ptr<Scene>& Renderer::GetCurrentScene() {
    return currentScene;
}

void Renderer::SetCurrentScene(std::shared_ptr<Scene> newScene) {
	if (currentScene) {
		DeletionManager::GetInstance().MarkForDelete(currentScene);
	}
	newScene->GetRoot().add<Components::ActiveScene>();
    currentScene = newScene;
    //currentScene->SetDepthMap(m_depthMap);
    currentScene->Activate(m_managerInterface);
	rebuildRenderGraph = true;
}

std::shared_ptr<Scene> Renderer::AppendScene(std::shared_ptr<Scene> scene) {
	return GetCurrentScene()->AppendScene(scene);
}

InputManager& Renderer::GetInputManager() {
    return inputManager;
}

void Renderer::SetInputMode(InputMode mode) {
    static WASDContext wasdContext;
    static OrbitalCameraContext orbitalContext;
    switch (mode) {
    case InputMode::wasd:
        inputManager.SetInputContext(&wasdContext);
        break;
    case InputMode::orbital:
        inputManager.SetInputContext(&orbitalContext);
        break;
    }
    SetupInputHandlers();
}

void Renderer::MoveForward() {
    spdlog::info("Moving forward!");
}

void Renderer::SetupInputHandlers() {
	auto& context = *inputManager.GetCurrentContext();
    context.SetActionHandler(InputAction::MoveForward, [this](float magnitude, const InputData& inputData) {
        //spdlog::info("Moving forward!");
        movementState.forwardMagnitude = magnitude * getCameraSpeed();
        });

    context.SetActionHandler(InputAction::MoveBackward, [this](float magnitude, const InputData& inputData) {
        //spdlog::info("Moving forward!");
        movementState.backwardMagnitude = magnitude * getCameraSpeed();
        });

    context.SetActionHandler(InputAction::MoveRight, [this](float magnitude, const InputData& inputData) {
        //spdlog::info("Moving right!");
        movementState.rightMagnitude = magnitude * getCameraSpeed();
        });

    context.SetActionHandler(InputAction::MoveLeft, [this](float magnitude, const InputData& inputData) {
        //spdlog::info("Moving right!");
        movementState.leftMagnitude = magnitude * getCameraSpeed();
        });

    context.SetActionHandler(InputAction::MoveUp, [this](float magnitude, const InputData& inputData) {
        //spdlog::info("Moving up!");
        movementState.upMagnitude = magnitude * getCameraSpeed();
        });

    context.SetActionHandler(InputAction::MoveDown, [this](float magnitude, const InputData& inputData) {
        //spdlog::info("Moving up!");
        movementState.downMagnitude = magnitude * getCameraSpeed();
        });

    context.SetActionHandler(InputAction::RotateCamera, [this](float magnitude, const InputData& inputData) {
        horizontalAngle -= static_cast<float>(inputData.mouseDeltaX) * 0.005f;
        verticalAngle -= static_cast<float>(inputData.mouseDeltaY) * 0.005f;
        });

    context.SetActionHandler(InputAction::ZoomIn, [](float magnitude, const InputData& inputData) {
        // TODO
        });

    context.SetActionHandler(InputAction::ZoomOut, [](float magnitude, const InputData& inputData) {
        // TODO
        });

	context.SetActionHandler(InputAction::Reset, [](float magnitude, const InputData& inputData) {
        PSOManager::GetInstance().ReloadShaders();
		});

    context.SetActionHandler(InputAction::X, [](float magnitude, const InputData& inputData) {
        });

    context.SetActionHandler(InputAction::Z, [](float magnitude, const InputData& inputData) {
        });
}

void Renderer::CreateRenderGraph() {
    StallPipeline();

    // TODO: Find a better way to handle resources like this
    m_coreResourceProvider.m_primaryCameraMeshletBitfield = currentScene->GetPrimaryCamera().get<Components::RenderView>().meshletBitfieldBuffer;

    // TODO: Primary camera and current environment will change, and I'd rather not recompile the graph every time that happens.
    // How should we manage swapping out their resources? DynamicResource could work, but the ResourceGroup/independantly managed resource
    // part of the compiler would need to become aware of DynamicResource.

    // TODO: Some of these resources don't really need to be recreated (GTAO, etc.)
    // Instead, just create them externally and register them

	std::shared_ptr<RenderGraph> newGraph = std::make_shared<RenderGraph>();
    newGraph->RegisterProvider(m_pMeshManager.get());
    newGraph->RegisterProvider(m_pObjectManager.get());
    newGraph->RegisterProvider(m_pCameraManager.get());
    newGraph->RegisterProvider(m_pLightManager.get());
    newGraph->RegisterProvider(m_pEnvironmentManager.get());
    newGraph->RegisterProvider(m_pIndirectCommandBufferManager.get());
    newGraph->RegisterProvider(&m_coreResourceProvider);

	auto& depth = currentScene->GetPrimaryCamera().get<Components::DepthMap>();
    std::shared_ptr<PixelBuffer> depthTexture = depth.depthMap;

    newGraph->RegisterResource(Builtin::PrimaryCamera::DepthTexture, depthTexture);
    newGraph->RegisterResource(Builtin::PrimaryCamera::LinearDepthMap, depth.linearDepthMap);

    auto& view = currentScene->GetPrimaryCamera().get<Components::RenderView>();
    newGraph->RegisterResource(Builtin::PrimaryCamera::IndirectCommandBuffers::Opaque, view.indirectCommandBuffers.opaqueIndirectCommandBuffer);
	newGraph->RegisterResource(Builtin::PrimaryCamera::IndirectCommandBuffers::AlphaTest, view.indirectCommandBuffers.alphaTestIndirectCommandBuffer);
	newGraph->RegisterResource(Builtin::PrimaryCamera::IndirectCommandBuffers::Blend, view.indirectCommandBuffers.blendIndirectCommandBuffer);
	newGraph->RegisterResource(Builtin::PrimaryCamera::IndirectCommandBuffers::MeshletFrustrumCulling, view.indirectCommandBuffers.meshletCullingIndirectCommandBuffer);
	newGraph->RegisterResource(Builtin::PrimaryCamera::IndirectCommandBuffers::MeshletCullingReset, view.indirectCommandBuffers.meshletCullingResetIndirectCommandBuffer);
    //newGraph->AddResource(depthTexture, false);https://www.linkedin.com/in/matthew-gomes-857a4h/
    //newGraph->AddResource(depth->linearDepthMap);
    bool useMeshShaders = getMeshShadersEnabled();
    if (!DeviceManager::GetInstance().GetMeshShadersSupported()) {
        useMeshShaders = false;
    }

    BuildBRDFIntegrationPass(newGraph.get());

    // Skinning comes before Z prepass
    newGraph->BuildComputePass("SkinningPass")
        .Build<SkinningPass>();


    bool indirect = getIndirectDrawsEnabled();
    if (!DeviceManager::GetInstance().GetMeshShadersSupported()) { // Indirect draws only supported with mesh shaders
        indirect = false;
    }

    CreateGBufferResources(newGraph.get());

    if (indirect) {
        if (m_occlusionCulling) {
            BuildOcclusionCullingPipeline(newGraph.get());
        }
        BuildGeneralCullingPipeline(newGraph.get());
    }

    // Z prepass goes before light clustering for when active cluster determination is implemented
    BuildZPrepass(newGraph.get());

    // GTAO pass
    if (m_gtaoEnabled) {
		RegisterGTAOResources(newGraph.get());
        BuildGTAOPipeline(newGraph.get(), currentScene->GetPrimaryCamera().try_get<Components::Camera>());
    }

	if (m_clusteredLighting) {  // TODO: active cluster determination using Z prepass
        BuildLightClusteringPipeline(newGraph.get());
    }

    BuildEnvironmentPipeline(newGraph.get());

    auto debugPassBuilder = newGraph->BuildRenderPass("DebugPass");

    auto drawShadows = m_coreResourceProvider.m_shadowMaps != nullptr && getShadowsEnabled();
    if (drawShadows) {
        BuildMainShadowPass(newGraph.get());
        debugPassBuilder.WithShaderResource(Builtin::PrimaryCamera::LinearDepthMap);
    }
	
    if (m_currentEnvironment != nullptr) {
        newGraph->RegisterResource(Builtin::Environment::CurrentCubemap, m_currentEnvironment->GetEnvironmentCubemap());
        newGraph->RegisterResource(Builtin::Environment::CurrentPrefilteredCubemap, m_currentEnvironment->GetEnvironmentPrefilteredCubemap());
        newGraph->BuildRenderPass("SkyboxPass")
            .Build<SkyboxRenderPass>();
    }

    BuildPrimaryPass(newGraph.get(), m_currentEnvironment.get());

    BuildPPLLPipeline(newGraph.get());

	// Start of post-processing passes

	if (m_screenSpaceReflections && m_deferredRendering) { // SSSR requires deferred rendering for gbuffer
        BuildSSRPasses(newGraph.get());
    }

	auto adaptedLuminanceBuffer = ResourceManager::GetInstance().CreateIndexedStructuredBuffer(1, sizeof(float), false, true, false);
    newGraph->RegisterResource(Builtin::PostProcessing::AdaptedLuminance, adaptedLuminanceBuffer);
	auto histogramBuffer = ResourceManager::GetInstance().CreateIndexedStructuredBuffer(255, sizeof(uint32_t), false, true, false);
	newGraph->RegisterResource(Builtin::PostProcessing::LuminanceHistogram, histogramBuffer);

    newGraph->BuildComputePass("luminanceHistogramPass")
        .Build<LuminanceHistogramPass>();
    newGraph->BuildComputePass("LuminanceAveragePass")
		.Build<LuminanceHistogramAveragePass>();

    newGraph->BuildRenderPass("UpscalingPass")
		.Build<UpscalingPass>();

    if (m_bloom) {
        BuildBloomPipeline(newGraph.get());
    }

    newGraph->BuildRenderPass("TonemappingPass")
        .Build<TonemappingPass>();

    debugPassBuilder.Build<DebugRenderPass>();
	if (m_coreResourceProvider.m_currentDebugTexture != nullptr) {
		auto debugRenderPass = newGraph->GetRenderPassByName("DebugPass");
		std::shared_ptr<DebugRenderPass> debugPass = std::dynamic_pointer_cast<DebugRenderPass>(debugRenderPass);
        if (debugPass) {
            debugPass->SetTexture(m_coreResourceProvider.m_currentDebugTexture.get());
        }
	}

    if (getDrawBoundingSpheres()) {
		newGraph->BuildRenderPass("DebugSpherePass")
			.Build<DebugSpherePass>();
    }

    newGraph->Compile();
    newGraph->Setup();

    DeletionManager::GetInstance().MarkForDelete(currentRenderGraph);
	currentRenderGraph = std::move(newGraph);

	Menu::GetInstance().SetRenderGraph(currentRenderGraph);
	rebuildRenderGraph = false;
}

void Renderer::SetEnvironmentInternal(std::wstring name) {

    std::filesystem::path envpath = std::filesystem::path(GetExePath()) / L"textures" / L"environment" / (name+L".hdr");

    if (std::filesystem::exists(envpath)) {
		m_preFrameDeferredFunctions.defer([envpath, name, this]() { // Don't change this during rendering
            m_currentEnvironment = m_pEnvironmentManager->CreateEnvironment(name);
            m_pEnvironmentManager->SetFromHDRI(m_currentEnvironment.get(), envpath.string());
            ResourceManager::GetInstance().SetActiveEnvironmentIndex(m_currentEnvironment->GetEnvironmentIndex());
			});
    }
    else {
        spdlog::error("Environment file not found: " + envpath.string());
    }
}

void Renderer::SetDebugTexture(std::shared_ptr<PixelBuffer> texture) {
    m_coreResourceProvider.m_currentDebugTexture = texture;
	if (currentRenderGraph == nullptr) {
		return;
	}
    auto pPass = currentRenderGraph->GetRenderPassByName("DebugPass");
    if (pPass != nullptr) {
        auto pDebugPass = std::dynamic_pointer_cast<DebugRenderPass>(pPass);
        pDebugPass->SetTexture(texture.get());
    }
    else {
        spdlog::warn("Debug pass does not exist");
    }
}