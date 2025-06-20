//
// Created by matth on 6/25/2024.
//

#include "DX12Renderer.h"

#define _USE_MATH_DEFINES
#include <math.h>
#include <dxgi1_6.h>
#include <atlbase.h>
#include <filesystem>

#include "Utilities/Utilities.h"
#include "DirectX/d3dx12.h"
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
#include "RenderPasses/PostProcessing/Bloom.h"
#include "RenderPasses/PostProcessing/Upscaling.h"
#include "Resources/TextureDescription.h"
#include "Menu.h"
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

void DX12Renderer::Initialize(HWND hwnd, UINT x_res, UINT y_res) {

    auto& settingsManager = SettingsManager::GetInstance();
    settingsManager.registerSetting<uint8_t>("numFramesInFlight", 3);
    getNumFramesInFlight = settingsManager.getSettingGetter<uint8_t>("numFramesInFlight");
    settingsManager.registerSetting<DirectX::XMUINT2>("renderResolution", { x_res, y_res });
    settingsManager.registerSetting<DirectX::XMUINT2>("outputResolution", { x_res, y_res });
    LoadPipeline(hwnd, x_res, y_res);
    SetSettings();
    ResourceManager::GetInstance().Initialize(graphicsQueue.Get());
    PSOManager::GetInstance().initialize();
    UploadManager::GetInstance().Initialize();
    DeletionManager::GetInstance().Initialize();
	CommandSignatureManager::GetInstance().Initialize();
    Menu::GetInstance().Initialize(hwnd, device, graphicsQueue, swapChain);
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
        .each([&](flecs::entity entity, const Components::Position& position, const Components::Rotation& rotation, const Components::Scale& scale, const Components::Matrix* matrix, Components::Matrix& mOut) {
        XMMATRIX matRotation = XMMatrixRotationQuaternion(rotation.rot);
        XMMATRIX matTranslation = XMMatrixTranslationFromVector(position.pos);
        XMMATRIX matScale = XMMatrixScalingFromVector(scale.scale);
        mOut.matrix = (matScale * matRotation * matTranslation);
        if (matrix != nullptr) {
            mOut.matrix = mOut.matrix * matrix->matrix;
        }

        if (entity.has<Components::RenderableObject>() && entity.has<Components::ObjectDrawInfo>()) {
            Components::RenderableObject* object = entity.get_mut<Components::RenderableObject>();
            Components::ObjectDrawInfo* drawInfo = entity.get_mut<Components::ObjectDrawInfo>();

            object->perObjectCB.prevModelMatrix = object->perObjectCB.modelMatrix;
            object->perObjectCB.modelMatrix = mOut.matrix;
            m_managerInterface.GetObjectManager()->UpdatePerObjectBuffer(drawInfo->perObjectCBView.get(), object->perObjectCB);

            auto& modelMatrix = object->perObjectCB.modelMatrix;
            XMMATRIX upperLeft3x3 = XMMatrixSet(
                XMVectorGetX(modelMatrix.r[0]), XMVectorGetY(modelMatrix.r[0]), XMVectorGetZ(modelMatrix.r[0]), 0.0f,
                XMVectorGetX(modelMatrix.r[1]), XMVectorGetY(modelMatrix.r[1]), XMVectorGetZ(modelMatrix.r[1]), 0.0f,
                XMVectorGetX(modelMatrix.r[2]), XMVectorGetY(modelMatrix.r[2]), XMVectorGetZ(modelMatrix.r[2]), 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f
            );
            XMMATRIX normalMat = XMMatrixInverse(nullptr, upperLeft3x3);
            m_managerInterface.GetObjectManager()->UpdateNormalMatrixBuffer(drawInfo->normalMatrixView.get(), &normalMat);
        }

        if (entity.has<Components::Camera>() && entity.has<Components::RenderView>()) {
            auto cameraModel = RemoveScalingFromMatrix(mOut.matrix);

            Components::Camera* camera = entity.get_mut<Components::Camera>();

			auto view = XMMatrixInverse(nullptr, cameraModel);
			DirectX::XMMATRIX projection = camera->info.unjitteredProjection;
            if (m_jitter && entity.has<Components::PrimaryCamera>()) {
                // Apply jitter
                unsigned int sequenceLength = 16;
				unsigned int sequenceIndex = m_totalFramesRendered % sequenceLength;
                DirectX::XMFLOAT2 sequenceOffset = { 
                    2.0f * Halton(sequenceIndex + 1, 2) - 1.0f, 
                    2.0f * Halton(sequenceIndex + 1, 3) - 1.0f };
                camera->jitterPixelSpace = sequenceOffset;

				//sequenceOffset.x = (sequenceOffset.x - 0.5) * 2.0f; // Scale to [-1, 1] range
				//sequenceOffset.y = (sequenceOffset.y - 0.5) * 2.0f; // Scale to [-1, 1] range
                DirectX::XMFLOAT2 jitterNDC = {
                    (sequenceOffset.x / res.x),
                    (sequenceOffset.y / res.y)
                };
				camera->jitterNDC = jitterNDC;
				auto jitterMatrix = DirectX::XMMatrixTranslation(jitterNDC.x, jitterNDC.y, 0.0f);
				projection = XMMatrixMultiply(projection, jitterMatrix); // Apply jitter to projection matrix
            }

			camera->info.jitteredProjection = projection; // Save jittered projection matrix
            camera->info.prevView = camera->info.view; // Save view from last frame

            camera->info.view = view;
			camera->info.viewInverse = cameraModel;
            camera->info.viewProjection = XMMatrixMultiply(camera->info.view, projection);
			camera->info.projectionInverse = XMMatrixInverse(nullptr, projection);

            auto pos = GetGlobalPositionFromMatrix(mOut.matrix);
            camera->info.positionWorldSpace = { pos.x, pos.y, pos.z, 1.0 };

            auto renderView = entity.get_mut<Components::RenderView>();
			m_managerInterface.GetCameraManager()->UpdatePerCameraBufferView(renderView->cameraBufferView.get(), camera->info);
        }

        if (entity.has<Components::Light>()) {
            Components::Light* light = entity.get_mut<Components::Light>();
            XMVECTOR worldForward = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
            light->lightInfo.dirWorldSpace = XMVector3Normalize(XMVector3TransformNormal(worldForward, mOut.matrix));
            light->lightInfo.posWorldSpace = XMVectorSet(mOut.matrix.r[3].m128_f32[0],  // _41
                mOut.matrix.r[3].m128_f32[1],  // _42
                mOut.matrix.r[3].m128_f32[2],  // _43
                1.0f);
            switch(light->lightInfo.type){
            case Components::LightType::Spot:
                light->lightInfo.boundingSphere = ComputeConeBoundingSphere(light->lightInfo.posWorldSpace, light->lightInfo.dirWorldSpace, light->lightInfo.maxRange, acos(light->lightInfo.outerConeAngle));
                break;
            case Components::LightType::Point:
                light->lightInfo.boundingSphere = { {mOut.matrix.r[3].m128_f32[0],  // _41
                    mOut.matrix.r[3].m128_f32[1],  // _42
                    mOut.matrix.r[3].m128_f32[2],  // _43
                    light->lightInfo.maxRange} };
            }

            if (light->lightInfo.shadowCaster) {
				const Components::LightViewInfo* viewInfo = entity.get<Components::LightViewInfo>();
				m_managerInterface.GetLightManager()->UpdateLightBufferView(viewInfo->lightBufferView.get(), light->lightInfo);
				m_managerInterface.GetLightManager()->UpdateLightViewInfo(entity);
            }
        }

            });
}

void DX12Renderer::CreateGlobalResources() {
    m_coreResourceProvider.m_shadowMaps = std::make_shared<ShadowMaps>(L"ShadowMaps");
    m_coreResourceProvider.m_linearShadowMaps = std::make_shared<LinearShadowMaps>(L"linearShadowMaps");
    //m_shadowMaps->AddAliasedResource(m_downsampledShadowMaps.get());
	//m_downsampledShadowMaps->AddAliasedResource(m_shadowMaps.get());

    setShadowMaps(m_coreResourceProvider.m_shadowMaps.get()); // To allow light manager to acccess shadow maps. TODO: Is there a better way to structure this kind of access?
	setLinearShadowMaps(m_coreResourceProvider.m_linearShadowMaps.get());
}

void DX12Renderer::SetSettings() {
	auto& settingsManager = SettingsManager::GetInstance();

    uint8_t numDirectionalCascades = 4;
	float maxShadowDistance = 30.0f;
	settingsManager.registerSetting<uint8_t>("numDirectionalLightCascades", numDirectionalCascades);
    settingsManager.registerSetting<float>("maxShadowDistance", maxShadowDistance);
    settingsManager.registerSetting<std::vector<float>>("directionalLightCascadeSplits", calculateCascadeSplits(numDirectionalCascades, 0.1, 100, maxShadowDistance));
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
	settingsManager.registerSetting<unsigned int>("tonemapType", TonemapType::REINHARD_JODIE);
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


    settingsManager.addObserver<bool>("enableShadows", [this](const bool& newValue) {
        // Trigger recompilation of the render graph when setting changes
        rebuildRenderGraph = true;
        });
	settingsManager.addObserver<std::string>("environmentName", [this](const std::string& newValue) {
		SetEnvironmentInternal(s2ws(newValue));
		rebuildRenderGraph = true;
		});
	settingsManager.addObserver<unsigned int>("outputType", [this](const unsigned int& newValue) {
		ResourceManager::GetInstance().SetOutputType(newValue);
		});
	settingsManager.addObserver<unsigned int>("tonemapType", [this](const unsigned int& newValue) {
		ResourceManager::GetInstance().SetTonemapType(newValue);
		});
	settingsManager.addObserver<bool>("enableMeshShader", [this](const bool& newValue) {
		ToggleMeshShaders(newValue);
		rebuildRenderGraph = true;
		});
	settingsManager.addObserver<bool>("enableWireframe", [this](const bool& newValue) {
		rebuildRenderGraph = true;
		});
	settingsManager.addObserver<bool>("enableIndirectDraws", [this](const bool& newValue) {
		rebuildRenderGraph = true;
		});
	settingsManager.addObserver<bool>("allowTearing", [this](const bool& newValue) {
		m_allowTearing = newValue;
		});
	settingsManager.addObserver<bool>("drawBoundingSpheres", [this](const bool& newValue) {
		rebuildRenderGraph = true;
		});
	settingsManager.addObserver<bool>("enableClusteredLighting", [this](const bool& newValue) {
		m_clusteredLighting = newValue;
		rebuildRenderGraph = true;
		});
	settingsManager.addObserver<bool>("enableImageBasedLighting", [this](const bool& newValue) {
		m_imageBasedLighting = newValue;
		});
	settingsManager.addObserver<bool>("enableGTAO", [this](const bool& newValue) {
		m_gtaoEnabled = newValue;
		rebuildRenderGraph = true;
		});
	settingsManager.addObserver<bool>("enableDeferredRendering", [this](const bool& newValue) {
		m_deferredRendering = newValue;
		rebuildRenderGraph = true;
		});
	settingsManager.addObserver<bool>("enableOcclusionCulling", [this](const bool& newValue) {
		m_occlusionCulling = newValue;
		rebuildRenderGraph = true;
		});
	settingsManager.addObserver<bool>("enableMeshletCulling", [this](const bool& newValue) {
		m_meshletCulling = newValue;
		rebuildRenderGraph = true;
		});
	settingsManager.addObserver<bool>("enableBloom", [this](const bool& newValue) {
		m_bloom = newValue;
		rebuildRenderGraph = true;
		});
    settingsManager.addObserver<bool>("enableJitter", [this](const bool& newValue) {
        m_jitter = newValue;
        });
	settingsManager.addObserver<float>("maxShadowDistance", [this](const float& newValue) {
		auto& settingsManager = SettingsManager::GetInstance();
		auto numDirectionalCascades = settingsManager.getSettingGetter<uint8_t>("numDirectionalLightCascades")();
		auto maxShadowDistance = settingsManager.getSettingGetter<float>("maxShadowDistance")();
        settingsManager.getSettingSetter<std::vector<float>>("directionalLightCascadeSplits")(calculateCascadeSplits(numDirectionalCascades, 0.1, 100, maxShadowDistance));
        });
	settingsManager.addObserver<std::vector<float>>("directionalLightCascadeSplits", [this](const std::vector<float>& newValue) {
		ResourceManager::GetInstance().SetDirectionalCascadeSplits(newValue);
		});
    m_numFramesInFlight = getNumFramesInFlight();
}

void DX12Renderer::ToggleMeshShaders(bool useMeshShaders) {
    // We need to:
    // 1. Remove all meshes in the global mesh library from the mesh manager
	// 2. Re-add them to the mesh manager
    // 3. Get all objects with mesh instances by querying the ECS
	// 4. Remove and re-add all instances to the mesh manager
	// 5. Remove and re-add all objects from the object manager to rebuild indirect draw info

	auto& world = ECSManager::GetInstance().GetWorld();
	auto& meshLibrary = world.get_mut<Components::GlobalMeshLibrary>()->meshes;

	// Remove all meshes from the mesh manager
	for (auto& meshPair : meshLibrary) {
		auto& mesh = meshPair.second;
		m_pMeshManager->RemoveMesh(mesh.get());
	}
	// Re-add them to the mesh manager
	for (auto& meshPair : meshLibrary) {
		auto& mesh = meshPair.second;
        MaterialBuckets bucket;
        switch (mesh->material->m_blendState) {
        case BlendState::BLEND_STATE_OPAQUE:
			bucket = MaterialBuckets::Opaque;
			break;
		case BlendState::BLEND_STATE_MASK:
			bucket = MaterialBuckets::AlphaTest;
			break;
		case BlendState::BLEND_STATE_BLEND:
			bucket = MaterialBuckets::Blend;
			break;
        }
        m_pMeshManager->AddMesh(mesh, bucket, useMeshShaders);
	}

	// Get all active objects with mesh instances by querying the ECS
    auto query = world.query_builder<Components::RenderableObject, Components::ObjectDrawInfo>().with<Components::Active>()
        .build();

    world.defer_begin();
    query.each([&](flecs::entity entity, const Components::RenderableObject& object, const Components::ObjectDrawInfo& drawInfo) {
        auto opaqueMeshInstances = entity.get<Components::OpaqueMeshInstances>();
        auto alphaTestMeshInstances = entity.get<Components::AlphaTestMeshInstances>();
        auto blendMeshInstances = entity.get<Components::BlendMeshInstances>();

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

void DX12Renderer::LoadPipeline(HWND hwnd, UINT x_res, UINT y_res) {
    UINT dxgiFactoryFlags = 0;

#if BUILD_TYPE == BUILD_TYPE_DEBUG
    ComPtr<ID3D12Debug> debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
        debugController->EnableDebugLayer();
        dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
    }
    EnableShaderBasedValidation();
#endif

    // Create DXGI factory

    ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));

	IDXGIFactory7* proxyFactory = factory.Get();
    if (SL_FAILED(result, slUpgradeInterface((void**) & proxyFactory)))
    {
        spdlog::error("SL factory upgrade failed!");
    }
    else {
        factory = proxyFactory;
	}

    m_currentAdapter = GetMostPowerfulAdapter(factory.Get());
    
    UpscalingManager::GetInstance().InitializeAdapter(m_currentAdapter);

    // Create device

#if defined(ENABLE_NSIGHT_AFTERMATH)
    m_gpuCrashTracker.Initialize();
#endif

    ThrowIfFailed(D3D12CreateDevice(
        m_currentAdapter.Get(),
        D3D_FEATURE_LEVEL_12_0,
        IID_PPV_ARGS(&device)));

    ID3D12Device10* proxyDevice = device.Get();
    if (SL_FAILED(result, slUpgradeInterface((void**) & proxyDevice)))
    {
        spdlog::error("SL device upgrade failed!");
    }
    else {
        device = proxyDevice;
    }

	UpscalingManager::GetInstance().SetDevice(device);

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

#if defined(_DEBUG)
    ComPtr<ID3D12InfoQueue1> infoQueue;
    if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&infoQueue)))) {
        infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
        infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
        infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, TRUE);

        DWORD callbackCookie = 0;
        infoQueue->RegisterMessageCallback([](D3D12_MESSAGE_CATEGORY category, D3D12_MESSAGE_SEVERITY severity, D3D12_MESSAGE_ID id, LPCSTR description, void* context) {
            // Log or print the debug messages,
            spdlog::error("D3D12 Debug Message: {}", description);
            }, D3D12_MESSAGE_CALLBACK_FLAG_NONE, nullptr, &callbackCookie);
    }
#endif

    // Disable unwanted warnings
    ComPtr<ID3D12InfoQueue> warningInfoQueue;
    if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&warningInfoQueue))))
    {
        D3D12_INFO_QUEUE_FILTER filter = {};
        D3D12_MESSAGE_ID blockedIDs[] = { (D3D12_MESSAGE_ID)1356, (D3D12_MESSAGE_ID)1328 }; // Barrier-only command lists, ps output type mismatch
        filter.DenyList.NumIDs = _countof(blockedIDs);
        filter.DenyList.pIDList = blockedIDs;

        warningInfoQueue->AddStorageFilterEntries(&filter);
    }

    // Describe and create the command queue
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    ThrowIfFailed(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&graphicsQueue)));

	// Compute queue
	D3D12_COMMAND_QUEUE_DESC computeQueueDesc = {};
	computeQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	computeQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
	ThrowIfFailed(device->CreateCommandQueue(&computeQueueDesc, IID_PPV_ARGS(&computeQueue)));

    // Initialize device manager and PSO manager
    DeviceManager::GetInstance().Initialize(device, graphicsQueue, computeQueue);

    // Describe and create the swap chain
    auto numFramesInFlight = getNumFramesInFlight();
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.BufferCount = numFramesInFlight;
    swapChainDesc.Width = x_res;
    swapChainDesc.Height = y_res;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

    ComPtr<IDXGISwapChain1> swapChainTemp;
    ThrowIfFailed(factory->CreateSwapChainForHwnd(
        graphicsQueue.Get(),
        hwnd,
        &swapChainDesc,
        nullptr,
        nullptr,
        &swapChainTemp));

    ThrowIfFailed(swapChainTemp.As(&swapChain));

    // We do not support fullscreen transitions
    ThrowIfFailed(factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER));

    m_frameIndex = swapChain->GetCurrentBackBufferIndex();

    // Create RTV descriptor heap
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = numFramesInFlight;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtvHeap)));

    rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    // Create frame resources
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(rtvHeap->GetCPUDescriptorHandleForHeapStart());
    renderTargets.resize(numFramesInFlight);
    for (UINT n = 0; n < numFramesInFlight; n++) {
        ThrowIfFailed(swapChain->GetBuffer(n, IID_PPV_ARGS(&renderTargets[n])));
        device->CreateRenderTargetView(renderTargets[n].Get(), nullptr, rtvHandle);
        rtvHandle.Offset(1, rtvDescriptorSize);
    }

    // Create command allocator

    for (int i = 0; i < numFramesInFlight; i++) {
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList7> commandList;
        ThrowIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
        ThrowIfFailed(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&commandList)));
        commandList->Close();
        m_commandAllocators.push_back(allocator);
        m_commandLists.push_back(commandList);
    }

    // Create per-frame fence information
	m_frameFenceValues.resize(numFramesInFlight);
	for (int i = 0; i < numFramesInFlight; i++) {
		m_frameFenceValues[i] = 0;
	}
    ThrowIfFailed(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_frameFence)));
    m_frameFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (m_frameFenceEvent == nullptr) {
        ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
    }

    ThrowIfFailed(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_readbackFence)));
}

void DX12Renderer::CreateTextures() {
    auto resolution = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT2>("renderResolution")();
    // Create HDR color target
    TextureDescription hdrDesc;
    hdrDesc.arraySize = 1;
    hdrDesc.channels = 4; // RGBA
    hdrDesc.isCubemap = false;
    hdrDesc.hasRTV = true;
    hdrDesc.hasUAV = false;
    hdrDesc.format = DXGI_FORMAT_R16G16B16A16_FLOAT; // HDR format
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
    motionVectors.format = DXGI_FORMAT_R16G16_FLOAT;
    motionVectors.generateMipMaps = false;
    motionVectors.hasSRV = true;
    motionVectors.srvFormat = DXGI_FORMAT_R16G16_FLOAT;
    ImageDimensions motionVectorsDims = { resolution.x, resolution.y, 0, 0 };
    motionVectors.imageDimensions.push_back(motionVectorsDims);
    auto motionVectorsBuffer = PixelBuffer::Create(motionVectors);
    motionVectorsBuffer->SetName(L"Motion Vectors");
	m_coreResourceProvider.m_gbufferMotionVectors = motionVectorsBuffer;
}

void DX12Renderer::OnResize(UINT newWidth, UINT newHeight) {
    if (!device) return;
    // Wait for the GPU to complete all operations
	WaitForFrame(m_frameIndex);

    // Release the resources tied to the swap chain
    auto numFramesInFlight = getNumFramesInFlight();

    for (int i = 0; i < numFramesInFlight; ++i) {
        renderTargets[i].Reset();
    }

    // Resize the swap chain
    ThrowIfFailed(swapChain->ResizeBuffers(
        2, // Buffer count
        newWidth, newHeight,
        DXGI_FORMAT_R8G8B8A8_UNORM,
        DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH));

    m_frameIndex = swapChain->GetCurrentBackBufferIndex();

    // Recreate the render target views
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(rtvHeap->GetCPUDescriptorHandleForHeapStart());
    for (UINT i = 0; i < numFramesInFlight; i++) {
        ThrowIfFailed(swapChain->GetBuffer(i, IID_PPV_ARGS(&renderTargets[i])));
        device->CreateRenderTargetView(renderTargets[i].Get(), nullptr, rtvHandle);
        rtvHandle.Offset(1, rtvDescriptorSize);
    }

	SettingsManager::GetInstance().getSettingSetter<DirectX::XMUINT2>("outputResolution")({ newWidth, newHeight });

    UpscalingManager::GetInstance().Shutdown();
	UpscalingManager::GetInstance().Setup();

    CreateTextures();



	//Rebuild the render graph
	rebuildRenderGraph = true;
}


void DX12Renderer::WaitForFrame(uint8_t currentFrameIndex) {
    // Check if the fence value for the current frame is complete
    if (m_frameFence->GetCompletedValue() < m_frameFenceValues[currentFrameIndex]) {
        // Set the event to be triggered when the GPU reaches the required fence value
        ThrowIfFailed(m_frameFence->SetEventOnCompletion(m_frameFenceValues[currentFrameIndex], m_frameFenceEvent));

        // Wait for the event
        WaitForSingleObject(m_frameFenceEvent, INFINITE);
    }
}

void DX12Renderer::Update(double elapsedSeconds) {
    WaitForFrame(m_frameIndex); // Wait for the previous iteration of the frame to finish

	StatisticsManager::GetInstance().OnFrameComplete(m_frameIndex, computeQueue.Get()); // Gather statistics for the last iteration of the frame
    StatisticsManager::GetInstance().OnFrameComplete(m_frameIndex, graphicsQueue.Get()); // Gather statistics for the last iteration of the frame

	m_preFrameDeferredFunctions.flush(); // Execute anything we deferred until now

    auto& updateManager = UploadManager::GetInstance();
    updateManager.ProcessDeferredReleases(m_frameIndex);

    if (rebuildRenderGraph) {
		CreateRenderGraph();
    }

	Components::Position& cameraPosition = *currentScene->GetPrimaryCamera().get_mut<Components::Position>();
	Components::Rotation& cameraRotation = *currentScene->GetPrimaryCamera().get_mut<Components::Rotation>();
	ApplyMovement(cameraPosition, cameraRotation, movementState, elapsedSeconds);
	RotatePitchYaw(cameraRotation, verticalAngle, horizontalAngle);

    //spdlog::info("horizontal angle: {}", horizontalAngle);
    //spdlog::info("vertical angle: {}", verticalAngle);
    verticalAngle = 0;
    horizontalAngle = 0;

    currentScene->Update();

    auto& world = ECSManager::GetInstance().GetWorld();
	world.progress();

    auto camera = currentScene->GetPrimaryCamera();
    unsigned int cameraIndex = camera.get<Components::RenderView>()->cameraBufferIndex;
	auto& commandAllocator = m_commandAllocators[m_frameIndex];
	auto& commandList = m_commandLists[m_frameIndex];


    ThrowIfFailed(commandAllocator->Reset());
    auto& resourceManager = ResourceManager::GetInstance();
    auto res = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT2>("renderResolution")();
    resourceManager.UpdatePerFrameBuffer(cameraIndex, m_pLightManager->GetNumLights(), { res.x, res.y }, m_lightClusterSize, m_frameIndex);

	currentRenderGraph->Update();

	updateManager.ResetAllocators(m_frameIndex); // Reset allocators to avoid leaking memory
    updateManager.ExecuteResourceCopies(m_frameIndex, graphicsQueue.Get());// copies come before uploads to avoid overwriting data
	updateManager.ProcessUploads(m_frameIndex, graphicsQueue.Get());

    //resourceManager.ExecuteResourceTransitions();
    ThrowIfFailed(commandList->Reset(commandAllocator.Get(), NULL));
}

void DX12Renderer::Render() {
    // Record all the commands we need to render the scene into the command list
    auto& commandAllocator = m_commandAllocators[m_frameIndex];
    auto& commandList = m_commandLists[m_frameIndex];

	auto& world = ECSManager::GetInstance().GetWorld();
	const Components::DrawStats* drawStats = world.get<Components::DrawStats>();
    auto renderRes = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT2>("renderResolution")();
    auto outputRes = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT2>("outputResolution")();

    m_context.currentScene = currentScene.get();
	m_context.device = DeviceManager::GetInstance().GetDevice().Get();
    //m_context.commandList = commandList.Get();
	m_context.commandQueue = graphicsQueue.Get();
    m_context.textureDescriptorHeap = ResourceManager::GetInstance().GetSRVDescriptorHeap().Get();
    m_context.samplerDescriptorHeap = ResourceManager::GetInstance().GetSamplerDescriptorHeap().Get();
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
	m_context.drawStats = *drawStats;

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
	m_context.globalPSOFlags = globalPSOFlags;

    // Indicate that the back buffer will be used as a render target
    CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    commandList->ResourceBarrier(1, &barrier);
   
    commandList->Close();

    // Execute the command list
    ID3D12CommandList* ppCommandLists[] = { commandList.Get() };
    graphicsQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

	currentRenderGraph->Execute(m_context); // Main render graph execution

	Menu::GetInstance().Render(m_context); // Render menu

    commandList->Reset(commandAllocator.Get(), nullptr);

    // Indicate that the back buffer will now be used to present
    barrier = CD3DX12_RESOURCE_BARRIER::Transition(renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    commandList->ResourceBarrier(1, &barrier);

    ThrowIfFailed(commandList->Close());

    // Execute the command list
    ppCommandLists[0] = commandList.Get();
    graphicsQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

    // Present the frame
    ThrowIfFailed(swapChain->Present(0, m_allowTearing ? DXGI_PRESENT_ALLOW_TEARING : 0));

    AdvanceFrameIndex();

    SignalFence(graphicsQueue, m_frameIndex);

	ReadbackManager::GetInstance().ProcessReadbackRequests(); // Save images to disk if requested

    DeletionManager::GetInstance().ProcessDeletions();
}

void DX12Renderer::SignalFence(ComPtr<ID3D12CommandQueue> commandQueue, uint8_t frameIndexToSignal) {
    // Signal the fence
    m_currentFrameFenceValue++;
    ThrowIfFailed(commandQueue->Signal(m_frameFence.Get(), m_currentFrameFenceValue));

    // Store the fence value for the current frame
    m_frameFenceValues[frameIndexToSignal] = m_currentFrameFenceValue;
}

void DX12Renderer::AdvanceFrameIndex() {
    m_frameIndex = (m_frameIndex + 1) % m_numFramesInFlight;
    m_totalFramesRendered += 1;
}

void DX12Renderer::FlushCommandQueue() {
    // Create a fence and an event to wait on
    Microsoft::WRL::ComPtr<ID3D12Fence> flushFence;
    ThrowIfFailed(DeviceManager::GetInstance().GetDevice()->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&flushFence)));

    HANDLE flushEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!flushEvent) {
        ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
    }

    // Signal the fence and wait
    const UINT64 flushValue = 1;
    ThrowIfFailed(graphicsQueue->Signal(flushFence.Get(), flushValue));
    if (flushFence->GetCompletedValue() < flushValue) {
        ThrowIfFailed(flushFence->SetEventOnCompletion(flushValue, flushEvent));
        WaitForSingleObject(flushEvent, INFINITE);
    }

	ThrowIfFailed(computeQueue->Signal(flushFence.Get(), flushValue+1));
	if (flushFence->GetCompletedValue() < flushValue+1) {
		ThrowIfFailed(flushFence->SetEventOnCompletion(flushValue+1, flushEvent));
		WaitForSingleObject(flushEvent, INFINITE);
	}

    CloseHandle(flushEvent);
}

void DX12Renderer::StallPipeline() {
    for (int i = 0; i < m_numFramesInFlight; ++i) {
        WaitForFrame(i);
    }
    FlushCommandQueue();
}

void DX12Renderer::Cleanup() {
    spdlog::info("In cleanup");
    // Wait for all GPU frames to complete
	StallPipeline();
    CloseHandle(m_frameFenceEvent);
    currentRenderGraph.reset();
	currentScene.reset();
	m_pIndirectCommandBufferManager.reset();
	m_pCameraManager.reset();
	m_pLightManager.reset();
	m_pMeshManager.reset();
	m_pObjectManager.reset();
    m_hierarchySystem.destruct();
    DeletionManager::GetInstance().Cleanup();
}

void DX12Renderer::CheckDebugMessages() {
    ComPtr<ID3D12InfoQueue> infoQueue;
    if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&infoQueue)))) {
        UINT64 messageCount = infoQueue->GetNumStoredMessages();
        for (UINT64 i = 0; i < messageCount; ++i) {
            SIZE_T messageLength = 0;
            infoQueue->GetMessage(i, nullptr, &messageLength);
            D3D12_MESSAGE* pMessage = (D3D12_MESSAGE*)malloc(messageLength);
            infoQueue->GetMessage(i, pMessage, &messageLength);
            std::cerr << "D3D12 Debug Message: " << pMessage->pDescription << std::endl;
            free(pMessage);
        }
        infoQueue->ClearStoredMessages();
    }
}

void DX12Renderer::SetEnvironment(std::string environmentName) {
	setEnvironment(environmentName);
}

ComPtr<ID3D12Device10>& DX12Renderer::GetDevice() {
    return device;
}

std::shared_ptr<Scene>& DX12Renderer::GetCurrentScene() {
    return currentScene;
}

void DX12Renderer::SetCurrentScene(std::shared_ptr<Scene> newScene) {
	if (currentScene) {
		DeletionManager::GetInstance().MarkForDelete(currentScene);
	}
	auto& ecs_world = ECSManager::GetInstance().GetWorld();
	newScene->GetRoot().add<Components::ActiveScene>();
    currentScene = newScene;
    //currentScene->SetDepthMap(m_depthMap);
    currentScene->Activate(m_managerInterface);
	rebuildRenderGraph = true;
}

std::shared_ptr<Scene> DX12Renderer::AppendScene(std::shared_ptr<Scene> scene) {
	return GetCurrentScene()->AppendScene(scene);
}

InputManager& DX12Renderer::GetInputManager() {
    return inputManager;
}

void DX12Renderer::SetInputMode(InputMode mode) {
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
    SetupInputHandlers(inputManager, *inputManager.GetCurrentContext());
}

void DX12Renderer::MoveForward() {
    spdlog::info("Moving forward!");
}

void DX12Renderer::SetupInputHandlers(InputManager& inputManager, InputContext& context) {
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
        horizontalAngle -= inputData.mouseDeltaX * 0.005;
        verticalAngle -= inputData.mouseDeltaY * 0.005;
        });

    context.SetActionHandler(InputAction::ZoomIn, [this](float magnitude, const InputData& inputData) {
        // TODO
        });

    context.SetActionHandler(InputAction::ZoomOut, [this](float magnitude, const InputData& inputData) {
        // TODO
        });

	context.SetActionHandler(InputAction::Reset, [this](float magnitude, const InputData& inputData) {
        PSOManager::GetInstance().ReloadShaders();
		});

    context.SetActionHandler(InputAction::X, [this](float magnitude, const InputData& inputData) {
        });

    context.SetActionHandler(InputAction::Z, [this](float magnitude, const InputData& inputData) {
        });
}

void DX12Renderer::CreateRenderGraph() {
    StallPipeline();

    // TODO: Find a better way to handle resources like this
    m_coreResourceProvider.m_primaryCameraMeshletBitfield = currentScene->GetPrimaryCamera().get<Components::RenderView>()->meshletBitfieldBuffer;

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

	auto depth = currentScene->GetPrimaryCamera().get<Components::DepthMap>();
    std::shared_ptr<PixelBuffer> depthTexture = depth->depthMap;

    newGraph->RegisterResource(Builtin::PrimaryCamera::DepthTexture, depthTexture);
    newGraph->RegisterResource(Builtin::PrimaryCamera::LinearDepthMap, depth->linearDepthMap);

    auto view = currentScene->GetPrimaryCamera().get<Components::RenderView>();
    newGraph->RegisterResource(Builtin::PrimaryCamera::IndirectCommandBuffers::Opaque, view->indirectCommandBuffers.opaqueIndirectCommandBuffer);
	newGraph->RegisterResource(Builtin::PrimaryCamera::IndirectCommandBuffers::AlphaTest, view->indirectCommandBuffers.alphaTestIndirectCommandBuffer);
	newGraph->RegisterResource(Builtin::PrimaryCamera::IndirectCommandBuffers::Blend, view->indirectCommandBuffers.blendIndirectCommandBuffer);
	newGraph->RegisterResource(Builtin::PrimaryCamera::IndirectCommandBuffers::MeshletFrustrumCulling, view->indirectCommandBuffers.meshletCullingIndirectCommandBuffer);
	newGraph->RegisterResource(Builtin::PrimaryCamera::IndirectCommandBuffers::MeshletCullingReset, view->indirectCommandBuffers.meshletCullingResetIndirectCommandBuffer);
    //newGraph->AddResource(depthTexture, false);
    //newGraph->AddResource(depth->linearDepthMap);
    bool useMeshShaders = getMeshShadersEnabled();
    if (!DeviceManager::GetInstance().GetMeshShadersSupported()) {
        useMeshShaders = false;
    }

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
        BuildGTAOPipeline(newGraph.get(), currentScene->GetPrimaryCamera().get<Components::Camera>());
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
        newGraph->BuildRenderPass("SkyboxPass")
            .Build<SkyboxRenderPass>();
    }

    BuildPrimaryPass(newGraph.get(), m_currentEnvironment.get());

    BuildPPLLPipeline(newGraph.get());

	// Start of post-processing passes
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
		auto debugSphereBuilder = newGraph->BuildRenderPass("DebugSpherePass")
			.Build<DebugSpherePass>();
    }

    newGraph->Compile();
    newGraph->Setup();

    DeletionManager::GetInstance().MarkForDelete(currentRenderGraph);
	currentRenderGraph = std::move(newGraph);

	rebuildRenderGraph = false;
}

void DX12Renderer::SetEnvironmentInternal(std::wstring name) {

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

	// Check if this environment has been processed and cached. If it has, load the cache. If it hasn't, load the environment and process it.
 //   auto radiancePath = GetCacheFilePath(name + L"_radiance.dds", L"environments");
 //   auto skyboxPath = GetCacheFilePath(name + L"_environment.dds", L"environments");
	//auto prefilteredPath = GetCacheFilePath(name + L"_prefiltered.dds", L"environments");
 //   if (std::filesystem::exists(radiancePath) && std::filesystem::exists(skyboxPath) && std::filesystem::exists(prefilteredPath)) {
 //       if (m_currentEnvironmentTexture != nullptr) { // unset environment texture so render graph doesn't try to rebuld resources
 //           DeletionManager::GetInstance().MarkForDelete(m_currentEnvironmentTexture);
 //           m_currentEnvironmentTexture = nullptr;
 //       }
	//	// Load the cached environment
 //       auto skybox = loadCubemapFromFile(skyboxPath);
	//	auto radiance = loadCubemapFromFile(radiancePath);
 //       auto prefiltered = loadCubemapFromFile(prefilteredPath);
	//	SetSkybox(skybox);
	//	SetIrradiance(radiance);
	//	SetPrefilteredEnvironment(prefiltered);
 //       rebuildRenderGraph = true;
 //   }
 //   else {
 //       std::filesystem::path envpath = std::filesystem::path(GetExePath()) / L"textures" / L"environment" / (name+L".hdr");
 //       
 //       if (std::filesystem::exists(envpath)) {
 //           auto skyHDR = loadTextureFromFileSTBI(envpath.string());
 //           SetEnvironmentTexture(skyHDR, ws2s(name));
 //       }
 //       else {
 //           spdlog::error("Environment file not found: " + envpath.string());
 //       }
 //   }
}

void DX12Renderer::SetDebugTexture(std::shared_ptr<PixelBuffer> texture) {
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