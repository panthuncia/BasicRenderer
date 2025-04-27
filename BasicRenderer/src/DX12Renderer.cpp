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
#include "RenderPasses/BRDFIntegrationPass.h"
#include "RenderPasses/ClearUAVsPass.h"
#include "RenderPasses/frustrumCullingPass.h"
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

ComPtr<IDXGIAdapter1> GetMostPowerfulAdapter()
{
    ComPtr<IDXGIFactory6> factory;
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    if (FAILED(hr))
    {
        throw std::runtime_error("Failed to create DXGI factory.");
    }

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
    m_xRes = x_res;
    m_yRes = y_res;
    auto& settingsManager = SettingsManager::GetInstance();
    settingsManager.registerSetting<uint8_t>("numFramesInFlight", 3);
    getNumFramesInFlight = settingsManager.getSettingGetter<uint8_t>("numFramesInFlight");
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
    CreateTextures();
    CreateGlobalResources();

    // Initialize GPU resource managers
    m_pLightManager = LightManager::CreateUnique();
    m_pMeshManager = MeshManager::CreateUnique();
	m_pObjectManager = ObjectManager::CreateUnique();
	m_pIndirectCommandBufferManager = IndirectCommandBufferManager::CreateUnique();
	m_pCameraManager = CameraManager::CreateUnique();
	m_pEnvironmentManager = EnvironmentManager::CreateUnique();
	ResourceManager::GetInstance().SetEnvironmentBufferDescriptorIndex(m_pEnvironmentManager->GetEnvironmentBufferSRVDescriptorIndex());
	m_pLightManager->SetCameraManager(m_pCameraManager.get()); // Light manager needs access to camera manager for shadow cameras
	m_pLightManager->SetCommandBufferManager(m_pIndirectCommandBufferManager.get()); // Also for indirect command buffers

	m_managerInterface.SetManagers(m_pMeshManager.get(), m_pObjectManager.get(), m_pIndirectCommandBufferManager.get(), m_pCameraManager.get(), m_pLightManager.get(), m_pEnvironmentManager.get());

    auto& world = ECSManager::GetInstance().GetWorld();
    world.component<Components::GlobalMeshLibrary>().add(flecs::Exclusive);
	world.add<Components::GlobalMeshLibrary>();
    world.component<Components::DrawStats>("DrawStats").add(flecs::Exclusive);
    world.component<Components::ActiveScene>().add(flecs::OnInstantiate, flecs::Inherit);
	world.set<Components::DrawStats>({ 0, 0, 0, 0 });
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
            auto& modelMatrix = object->perObjectCB.modelMatrix;
            modelMatrix = mOut.matrix;
            m_managerInterface.GetObjectManager()->UpdatePerObjectBuffer(drawInfo->perObjectCBView.get(), object->perObjectCB);

            XMMATRIX upperLeft3x3 = XMMatrixSet(
                XMVectorGetX(modelMatrix.r[0]), XMVectorGetY(modelMatrix.r[0]), XMVectorGetZ(modelMatrix.r[0]), 0.0f,
                XMVectorGetX(modelMatrix.r[1]), XMVectorGetY(modelMatrix.r[1]), XMVectorGetZ(modelMatrix.r[1]), 0.0f,
                XMVectorGetX(modelMatrix.r[2]), XMVectorGetY(modelMatrix.r[2]), XMVectorGetZ(modelMatrix.r[2]), 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f
            );
            XMMATRIX normalMat = XMMatrixInverse(nullptr, upperLeft3x3);
            m_managerInterface.GetObjectManager()->UpdateNormalMatrixBuffer(drawInfo->normalMatrixView.get(), &normalMat);
        }

        if (entity.has<Components::Camera>() && entity.has<Components::CameraBufferView>()) {
            auto inverseMatrix = XMMatrixInverse(nullptr, mOut.matrix);

            Components::Camera* camera = entity.get_mut<Components::Camera>();
            camera->info.view = RemoveScalingFromMatrix(inverseMatrix);
            camera->info.viewProjection = XMMatrixMultiply(camera->info.view, camera->info.projection);

            auto pos = GetGlobalPositionFromMatrix(mOut.matrix);
            camera->info.positionWorldSpace = { pos.x, pos.y, pos.z, 1.0 };

            auto cameraBufferView = entity.get_mut<Components::CameraBufferView>();
			m_managerInterface.GetCameraManager()->UpdatePerCameraBufferView(cameraBufferView->view.get(), camera->info);
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
                    1.0f}, light->lightInfo.maxRange };
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
    m_shadowMaps = std::make_shared<ShadowMaps>(L"ShadowMaps");
    setShadowMaps(m_shadowMaps.get()); // To allow light manager to acccess shadow maps. TODO: Is there a better way to structure this kind of access?
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
	settingsManager.registerSetting<bool>("enableWireframe", false);
	settingsManager.registerSetting<bool>("enableShadows", true);
	settingsManager.registerSetting<uint16_t>("skyboxResolution", 2048);
    settingsManager.registerSetting<uint16_t>("reflectionCubemapResolution", 512);
	settingsManager.registerSetting<bool>("enableImageBasedLighting", true);
	settingsManager.registerSetting<bool>("enablePunctualLighting", true);
	settingsManager.registerSetting<std::string>("environmentName", "");
	settingsManager.registerSetting<unsigned int>("outputType", 0);
    settingsManager.registerSetting<bool>("allowTearing", false);
	settingsManager.registerSetting<bool>("drawBoundingSpheres", false);
    settingsManager.registerSetting<bool>("enableClusteredLighting", true);
    settingsManager.registerSetting<DirectX::XMUINT3>("lightClusterSize", m_lightClusterSize);
	// This feels like abuse of the settings manager, but it's the easiest way to get the renderable objects to the menu
    settingsManager.registerSetting<std::function<flecs::entity()>>("getSceneRoot", [this]() -> flecs::entity {
        return currentScene->GetRoot();
        });
    settingsManager.registerSetting<std::function<flecs::entity()>>("getSceneRoot", [this]() -> flecs::entity {
        return currentScene->GetRoot();
        });
    bool meshShadereSupported = DeviceManager::GetInstance().GetMeshShadersSupported();
	settingsManager.registerSetting<bool>("enableMeshShader", meshShadereSupported);
	settingsManager.registerSetting<bool>("enableIndirectDraws", meshShadereSupported);
	settingsManager.registerSetting<bool>("enableGTAO", true);
	setShadowMaps = settingsManager.getSettingSetter<ShadowMaps*>("currentShadowMapsResourceGroup");
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
		});
	settingsManager.addObserver<unsigned int>("outputType", [this](const unsigned int& newValue) {
		ResourceManager::GetInstance().SetOutputType(newValue);
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

#if defined(_DEBUG)
    ComPtr<ID3D12Debug> debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
        debugController->EnableDebugLayer();
        dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
    }
    EnableShaderBasedValidation();
#endif

    // Create DXGI factory
    ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));

    // Create device
    ComPtr<IDXGIAdapter1> bestAdapter = GetMostPowerfulAdapter();

    //m_gpuCrashTracker.Initialize();

    ThrowIfFailed(D3D12CreateDevice(
        bestAdapter.Get(),
        D3D_FEATURE_LEVEL_12_0,
        IID_PPV_ARGS(&device)));

    //const uint32_t aftermathFlags =
    //    GFSDK_Aftermath_FeatureFlags_EnableMarkers |             // Enable event marker tracking.
    //    GFSDK_Aftermath_FeatureFlags_EnableResourceTracking |    // Enable tracking of resources.
    //    GFSDK_Aftermath_FeatureFlags_CallStackCapturing |        // Capture call stacks for all draw calls, compute dispatches, and resource copies.
    //    GFSDK_Aftermath_FeatureFlags_GenerateShaderDebugInfo;    // Generate debug information for shaders.

    //AFTERMATH_CHECK_ERROR(GFSDK_Aftermath_DX12_Initialize(
    //    GFSDK_Aftermath_Version_API,
    //    aftermathFlags,
    //    device.Get()));

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
        D3D12_MESSAGE_ID blockedIDs[] = { (D3D12_MESSAGE_ID)1356 }; // Barrier-only command lists, ps output type mismatch
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
    TextureDescription depthStencilDesc;
    ImageDimensions dimensions;
    dimensions.width = m_xRes;
    dimensions.height = m_yRes;
    depthStencilDesc.imageDimensions.push_back(dimensions);
    depthStencilDesc.format = DXGI_FORMAT_R32_TYPELESS;
    depthStencilDesc.hasSRV = true;
    depthStencilDesc.srvFormat = DXGI_FORMAT_R32_FLOAT;
    depthStencilDesc.arraySize = 1;
    depthStencilDesc.channels = 1;
    depthStencilDesc.generateMipMaps = false;
    depthStencilDesc.hasDSV = true;
	depthStencilDesc.dsvFormat = DXGI_FORMAT_D32_FLOAT;

    m_depthStencilBuffer = PixelBuffer::Create(depthStencilDesc);
    m_depthStencilBuffer->SetName(L"DepthStencilBuffer");
}

void DX12Renderer::OnResize(UINT newWidth, UINT newHeight) {
    if (!device) return;
	m_xRes = newWidth;
	m_yRes = newHeight;
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
    unsigned int cameraIndex = camera.get<Components::CameraBufferView>()->index;
	auto& commandAllocator = m_commandAllocators[m_frameIndex];
	auto& commandList = m_commandLists[m_frameIndex];


    ThrowIfFailed(commandAllocator->Reset());
    auto& resourceManager = ResourceManager::GetInstance();
    resourceManager.UpdatePerFrameBuffer(cameraIndex, m_pLightManager->GetNumLights(), m_pLightManager->GetActiveLightIndicesBufferDescriptorIndex(), m_pLightManager->GetLightBufferDescriptorIndex(), m_pLightManager->GetPointCubemapMatricesDescriptorIndex(), m_pLightManager->GetSpotMatricesDescriptorIndex(), m_pLightManager->GetDirectionalCascadeMatricesDescriptorIndex(), { m_xRes, m_yRes }, m_lightClusterSize);

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

    m_context.currentScene = currentScene.get();
	m_context.device = DeviceManager::GetInstance().GetDevice().Get();
    m_context.commandList = commandList.Get();
	m_context.commandQueue = graphicsQueue.Get();
    m_context.textureDescriptorHeap = ResourceManager::GetInstance().GetSRVDescriptorHeap().Get();
    m_context.samplerDescriptorHeap = ResourceManager::GetInstance().GetSamplerDescriptorHeap().Get();
    m_context.rtvHeap = rtvHeap.Get();
	m_context.pPrimaryDepthBuffer = m_depthStencilBuffer.get();
    m_context.renderTargets = renderTargets.data();
    m_context.rtvDescriptorSize = rtvDescriptorSize;
    m_context.dsvDescriptorSize = dsvDescriptorSize;
    m_context.frameIndex = m_frameIndex;
    m_context.frameFenceValue = m_currentFrameFenceValue;
    m_context.xRes = m_xRes;
    m_context.yRes = m_yRes;
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
	m_context.globalPSOFlags = globalPSOFlags;

    // Indicate that the back buffer will be used as a render target
    CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    commandList->ResourceBarrier(1, &barrier);
    
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_context.rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_context.frameIndex, m_context.rtvDescriptorSize);
	auto& dsvHandle = m_depthStencilBuffer->GetDSVInfos()[0].cpuHandle;
    //commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

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

ComPtr<ID3D12Device>& DX12Renderer::GetDevice() {
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
    currentScene->Activate(m_managerInterface);
	rebuildRenderGraph = true;
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

    auto newGraph = std::make_unique<RenderGraph>();
    std::shared_ptr<PixelBuffer> depthTexture = m_depthStencilBuffer;
	newGraph->AddResource(depthTexture, false, ResourceState::DEPTH_WRITE);
    auto& meshManager = m_pMeshManager;
    auto& objectManager = m_pObjectManager;
	auto meshResourceGroup = meshManager->GetResourceGroup();
    auto& perObjectBuffer = objectManager->GetPerObjectBuffers();
    auto& perMeshBuffer = meshManager->GetPerMeshBuffers();
	auto& preSkinningVertices = meshManager->GetPreSkinningVertices();
	auto& postSkinningVertices = meshManager->GetPostSkinningVertices();
	auto& normalMatrixBuffer = objectManager->GetNormalMatrixBuffer();
	auto& environmentsBuffer = m_pEnvironmentManager->GetEnvironmentInfoBuffer();
    auto& environmentPrefilteredGroup = m_pEnvironmentManager->GetEnvironmentPrefilteredCubemapGroup();

    bool useMeshShaders = getMeshShadersEnabled();
    if (!DeviceManager::GetInstance().GetMeshShadersSupported()) {
        useMeshShaders = false;
    }

    //auto& perFrameBuffer = ResourceManager::GetInstance().GetPerFrameBuffer();
    //newGraph->AddResource(perFrameBuffer, true, ResourceState::CONSTANT);

    auto& cameraManager = m_pCameraManager;
    auto& cameraBuffer = cameraManager->GetCameraBuffer();
    auto& lightBufferResourceGroup = m_pLightManager->GetLightBufferResourceGroup();

    std::shared_ptr<Buffer> clusterBuffer;
    std::shared_ptr<Buffer> lightPagesBuffer;
    if (m_clusteredLighting) {
        clusterBuffer = m_pLightManager->GetClusterBuffer();
        lightPagesBuffer = m_pLightManager->GetLightPagesBuffer();
        newGraph->AddResource(clusterBuffer, false, ResourceState::UNORDERED_ACCESS);
        newGraph->AddResource(lightPagesBuffer, false, ResourceState::UNORDERED_ACCESS);
    }
    newGraph->AddResource(cameraBuffer);
    newGraph->AddResource(lightBufferResourceGroup);
    newGraph->AddResource(preSkinningVertices);
    newGraph->AddResource(postSkinningVertices);
    newGraph->AddResource(normalMatrixBuffer);
    newGraph->AddResource(perMeshBuffer);
    newGraph->AddResource(perObjectBuffer);
    newGraph->AddResource(meshResourceGroup);
	newGraph->AddResource(environmentsBuffer);
	newGraph->AddResource(environmentPrefilteredGroup);

    // Frustrum culling goes before Z prepass
    bool indirect = getIndirectDrawsEnabled();
    if (!DeviceManager::GetInstance().GetMeshShadersSupported()) { // Indirect draws only supported with mesh shaders
        indirect = false;
    }

    std::shared_ptr<ResourceGroup> indirectCommandBufferResourceGroup = nullptr;
    if (indirect) {
        // Clear UAVs
        indirectCommandBufferResourceGroup = m_pIndirectCommandBufferManager->GetResourceGroup();
        newGraph->AddResource(indirectCommandBufferResourceGroup);

        newGraph->BuildRenderPass("ClearUAVsPass")
            .WithCopyDest(indirectCommandBufferResourceGroup)
            .Build<ClearUAVsPass>();

		newGraph->BuildComputePass("FrustrumCullingPass")
			.WithShaderResource(perObjectBuffer, perMeshBuffer, cameraBuffer)
			.WithUnorderedAccess(indirectCommandBufferResourceGroup)
			.Build<FrustrumCullingPass>();
    }

    // Skinning comes before Z prepass
	newGraph->BuildComputePass("SkinningPass")
		.WithShaderResource(perObjectBuffer, perMeshBuffer, preSkinningVertices, normalMatrixBuffer)
		.WithUnorderedAccess(postSkinningVertices)
		.Build<SkinningPass>();

    // Z prepass goes before light clustering for when active cluster determination is implemented

    TextureDescription normalsWorldSpaceDesc;
	normalsWorldSpaceDesc.arraySize = 1;
	normalsWorldSpaceDesc.channels = 4;
	normalsWorldSpaceDesc.isCubemap = false;
	normalsWorldSpaceDesc.hasRTV = true;
	normalsWorldSpaceDesc.format = DXGI_FORMAT_R10G10B10A2_UNORM;
	normalsWorldSpaceDesc.generateMipMaps = false;
    normalsWorldSpaceDesc.hasSRV = true;
	normalsWorldSpaceDesc.srvFormat = DXGI_FORMAT_R10G10B10A2_UNORM;
	ImageDimensions dims = { m_xRes, m_yRes, 0, 0 };
	normalsWorldSpaceDesc.imageDimensions.push_back(dims);
	auto normalsWorldSpace = PixelBuffer::Create(normalsWorldSpaceDesc);
	normalsWorldSpace->SetName(L"Normals World Space");
	newGraph->AddResource(normalsWorldSpace, false, ResourceState::RENDER_TARGET);

	TextureDescription albedoDesc;
	albedoDesc.arraySize = 1;
	albedoDesc.channels = 4;
	albedoDesc.isCubemap = false;
	albedoDesc.hasRTV = true;
	albedoDesc.format = DXGI_FORMAT_R8G8B8A8_UNORM;
	albedoDesc.generateMipMaps = false;
	albedoDesc.hasSRV = true;
	albedoDesc.srvFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	ImageDimensions albedoDims = { m_xRes, m_yRes, 0, 0 };
	albedoDesc.imageDimensions.push_back(albedoDims);
	auto albedo = PixelBuffer::Create(albedoDesc);
	albedo->SetName(L"Albedo");
	newGraph->AddResource(albedo, false, ResourceState::RENDER_TARGET);

    auto zBuilder = newGraph->BuildRenderPass("ZPrepass")
        .WithShaderResource(perObjectBuffer, perMeshBuffer, postSkinningVertices, cameraBuffer)
        .WithRenderTarget(normalsWorldSpace, albedo)
        .WithDepthTarget(depthTexture);

	if (useMeshShaders) {
		zBuilder.WithShaderResource(meshResourceGroup);
		if (indirect) {
			zBuilder.WithIndirectArguments(indirectCommandBufferResourceGroup);
		}
	}
    zBuilder.Build<ZPrepass>(normalsWorldSpace, albedo, getWireframeEnabled(), useMeshShaders, indirect);

    auto debugPassParameters = RenderPassParameters();

    // GTAO pass
    std::shared_ptr<PixelBuffer> outputAO;
    if (m_gtaoEnabled) {
        TextureDescription workingDepthsDesc;
        workingDepthsDesc.arraySize = 1;
        workingDepthsDesc.channels = 1;
        workingDepthsDesc.isCubemap = false;
        workingDepthsDesc.hasRTV = false;
        workingDepthsDesc.hasUAV = true;
        workingDepthsDesc.format = DXGI_FORMAT_R32_FLOAT;
        workingDepthsDesc.generateMipMaps = true;
        ImageDimensions dims1 = { m_xRes, m_yRes, 0, 0 };
        workingDepthsDesc.imageDimensions.push_back(dims1);
        auto workingDepths = PixelBuffer::Create(workingDepthsDesc);
        workingDepths->SetName(L"GTAO Working Depths");

        TextureDescription workingEdgesDesc;
        workingEdgesDesc.arraySize = 1;
        workingEdgesDesc.channels = 1;
        workingEdgesDesc.isCubemap = false;
        workingEdgesDesc.hasRTV = false;
        workingEdgesDesc.hasUAV = true;
        workingEdgesDesc.format = DXGI_FORMAT_R8_UNORM;
        workingEdgesDesc.generateMipMaps = false;
        workingEdgesDesc.imageDimensions.push_back(dims1);
        auto workingEdges = PixelBuffer::Create(workingEdgesDesc);
        workingEdges->SetName(L"GTAO Working Edges");

        TextureDescription workingAOTermDesc;
        workingAOTermDesc.arraySize = 1;
        workingAOTermDesc.channels = 1;
        workingAOTermDesc.isCubemap = false;
        workingAOTermDesc.hasRTV = false;
        workingAOTermDesc.hasUAV = true;
        workingAOTermDesc.format = DXGI_FORMAT_R8_UINT;
        workingAOTermDesc.generateMipMaps = false;
        workingAOTermDesc.imageDimensions.push_back(dims1);
        auto workingAOTerm1 = PixelBuffer::Create(workingAOTermDesc);
        workingAOTerm1->SetName(L"GTAO Working AO Term 1");
        auto workingAOTerm2 = PixelBuffer::Create(workingAOTermDesc);
        workingAOTerm2->SetName(L"GTAO Working AO Term 2");
        outputAO = PixelBuffer::Create(workingAOTermDesc);
        outputAO->SetName(L"GTAO Output AO Term");
        newGraph->AddResource(workingAOTerm1, false, ResourceState::UNORDERED_ACCESS);
        newGraph->AddResource(workingAOTerm2, false, ResourceState::UNORDERED_ACCESS);
        newGraph->AddResource(outputAO, false, ResourceState::UNORDERED_ACCESS);
        newGraph->AddResource(workingDepths, false, ResourceState::UNORDERED_ACCESS);
        newGraph->AddResource(workingEdges, false, ResourceState::UNORDERED_ACCESS);

        auto GTAOConstantBuffer = ResourceManager::GetInstance().CreateIndexedConstantBuffer<GTAOInfo>(L"GTAO constants");
        newGraph->AddResource(GTAOConstantBuffer, false, ResourceState::CONSTANT);

        // Point-clamp sampler
        D3D12_SAMPLER_DESC samplerDesc = {};
        samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplerDesc.MipLODBias = 0.0f;
        samplerDesc.MaxAnisotropy = 1;
        //samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;

        auto samplerIndex = ResourceManager::GetInstance().CreateIndexedSampler(samplerDesc);

        auto cameraInfo = currentScene->GetPrimaryCamera().get<Components::Camera>();
        GTAOInfo gtaoInfo;
        XeGTAO::GTAOSettings gtaoSettings;
        XeGTAO::GTAOConstants& gtaoConstants = gtaoInfo.g_GTAOConstants; // Intel's GTAO constants
        XeGTAO::GTAOUpdateConstants(gtaoConstants, m_xRes, m_yRes, gtaoSettings, false, 0, *cameraInfo);
        // Bindless indices
        gtaoInfo.g_samplerPointClampDescriptorIndex = samplerIndex;

        // Filter pass
        gtaoInfo.g_srcRawDepthDescriptorIndex = depthTexture->GetSRVInfo()[0].index;
        gtaoInfo.g_outWorkingDepthMIP0DescriptorIndex = workingDepths->GetUAVShaderVisibleInfo()[0].index;
        gtaoInfo.g_outWorkingDepthMIP1DescriptorIndex = workingDepths->GetUAVShaderVisibleInfo()[1].index;
        gtaoInfo.g_outWorkingDepthMIP2DescriptorIndex = workingDepths->GetUAVShaderVisibleInfo()[2].index;
        gtaoInfo.g_outWorkingDepthMIP3DescriptorIndex = workingDepths->GetUAVShaderVisibleInfo()[3].index;
        gtaoInfo.g_outWorkingDepthMIP4DescriptorIndex = workingDepths->GetUAVShaderVisibleInfo()[4].index;

        // Main pass
        gtaoInfo.g_srcWorkingDepthDescriptorIndex = workingDepths->GetSRVInfo()[0].index;
        gtaoInfo.g_srcNormalmapDescriptorIndex = normalsWorldSpace->GetSRVInfo()[0].index;
        // TODO: Hilbert lookup table
        gtaoInfo.g_outWorkingAOTermDescriptorIndex = workingAOTerm1->GetUAVShaderVisibleInfo()[0].index;
        gtaoInfo.g_outWorkingEdgesDescriptorIndex = workingEdges->GetUAVShaderVisibleInfo()[0].index;

        // Denoise pass
        gtaoInfo.g_srcWorkingEdgesDescriptorIndex = workingEdges->GetSRVInfo()[0].index;
        gtaoInfo.g_outFinalAOTermDescriptorIndex = outputAO->GetUAVShaderVisibleInfo()[0].index;

        debugPassParameters.shaderResources.push_back(outputAO);
        SetDebugTexture(outputAO);

        UploadManager::GetInstance().UploadData(&gtaoInfo, sizeof(GTAOInfo), GTAOConstantBuffer.get(), 0);

        newGraph->BuildComputePass("GTAOFilterPass") // Depth filter pass
            .WithShaderResource(normalsWorldSpace, m_depthStencilBuffer)
            .WithUnorderedAccess(workingDepths)
            .Build<GTAOFilterPass>(GTAOConstantBuffer);

        newGraph->BuildComputePass("GTAOMainPass") // Main pass
            .WithShaderResource(normalsWorldSpace, workingDepths)
            .WithUnorderedAccess(workingEdges, workingAOTerm1)
            .Build<GTAOMainPass>(GTAOConstantBuffer);

        newGraph->BuildComputePass("GTAODenoisePass") // Denoise pass
            .WithShaderResource(workingEdges, workingAOTerm1)
            .WithUnorderedAccess(outputAO)
            .Build<GTAODenoisePass>(GTAOConstantBuffer, workingAOTerm1->GetSRVInfo()[0].index);

    }

	if (m_clusteredLighting) {  // TODO: active cluster determination using Z prepass
        // light pages counter
        auto lightPagesCounter = ResourceManager::GetInstance().CreateIndexedStructuredBuffer(1, sizeof(unsigned int), ResourceState::UNORDERED_ACCESS, false, true, false);
        lightPagesCounter->SetName(L"Light Pages Counter");
        newGraph->AddResource(lightPagesCounter, false, ResourceState::UNORDERED_ACCESS);

		newGraph->BuildComputePass("ClusterGenerationPass")
			.WithShaderResource(cameraBuffer)
			.WithUnorderedAccess(clusterBuffer)
			.Build<ClusterGenerationPass>(clusterBuffer);

		newGraph->BuildComputePass("LightCullingPass")
			.WithShaderResource(cameraBuffer, lightBufferResourceGroup)
			.WithUnorderedAccess(clusterBuffer, lightPagesBuffer, lightPagesCounter)
			.Build<LightCullingPass>(clusterBuffer, lightPagesBuffer, lightPagesCounter);
    }
    
    auto forwardBuilder = newGraph->BuildRenderPass("ForwardPass")
        .WithShaderResource(perObjectBuffer, perMeshBuffer, postSkinningVertices, cameraBuffer, m_pEnvironmentManager->GetEnvironmentPrefilteredCubemapGroup())
        .WithDepthTarget(depthTexture);

    if (m_clusteredLighting) {
		forwardBuilder.WithShaderResource(clusterBuffer, lightPagesBuffer);
    }

    if (m_gtaoEnabled) {
        forwardBuilder.WithShaderResource(outputAO);
    }

    if (useMeshShaders) {
		forwardBuilder.WithShaderResource(meshResourceGroup);
		if (indirect) { // Indirect draws only supported with mesh shaders, becasue I'm not writing a separate codepath for doing it the bad way
			forwardBuilder.WithIndirectArguments(indirectCommandBufferResourceGroup);
        }
	}

    if (m_lutTexture == nullptr) {
		TextureDescription lutDesc;
        ImageDimensions dims;
		dims.height = 512;
		dims.width = 512;
		dims.rowPitch = 512 * 2 * sizeof(float);
		dims.slicePitch = dims.rowPitch * 512;
		lutDesc.imageDimensions.push_back(dims);
		lutDesc.channels = 2;
		lutDesc.isCubemap = false;
		lutDesc.hasRTV = true;
		lutDesc.format = DXGI_FORMAT_R16G16_FLOAT;
		auto lutBuffer = PixelBuffer::Create(lutDesc);
		auto sampler = Sampler::GetDefaultSampler();
		m_lutTexture = std::make_shared<Texture>(lutBuffer, sampler);
		m_lutTexture->SetName(L"LUTTexture");

        ResourceManager::GetInstance().SetEnvironmentBRDFLUTIndex(m_lutTexture->GetBuffer()->GetSRVInfo()[0].index);
		ResourceManager::GetInstance().SetEnvironmentBRDFLUTSamplerIndex(m_lutTexture->GetSamplerDescriptorIndex());

		newGraph->BuildRenderPass("BRDFIntegrationPass")
			.WithRenderTarget(m_lutTexture)
			.Build<BRDFIntegrationPass>(m_lutTexture);
    }

    newGraph->AddResource(m_lutTexture);
	forwardBuilder.WithShaderResource(m_lutTexture);

    // Check if we've already computed this environment
    //bool skipEnvironmentPass = false;
    //if (currentRenderGraph != nullptr) {
    //    auto currentEnvironmentPass = currentRenderGraph->GetRenderPassByName("EnvironmentConversionPass");
    //    if (currentEnvironmentPass != nullptr) {
    //        if (!currentEnvironmentPass->IsInvalidated()) {
    //            skipEnvironmentPass = true;
    //            DeletionManager::GetInstance().MarkForDelete(m_currentEnvironmentTexture);
    //            m_currentEnvironmentTexture = nullptr;
    //        }
    //    }
    //}


    newGraph->AddResource(m_pEnvironmentManager->GetWorkingHDRIGroup());
    newGraph->AddResource(m_pEnvironmentManager->GetWorkingEnvironmentCubemapGroup());
    //newGraph->AddResource(m_environmentIrradiance);

	newGraph->BuildRenderPass("EnvironmentConversionPass")
		.WithShaderResource(m_pEnvironmentManager->GetWorkingHDRIGroup())
		.WithRenderTarget(m_pEnvironmentManager->GetWorkingEnvironmentCubemapGroup())
		.Build<EnvironmentConversionPass>();
        
    newGraph->BuildComputePass("EnvironmentFilterPass")
        .WithShaderResource(m_pEnvironmentManager->GetWorkingEnvironmentCubemapGroup())
        .WithUnorderedAccess(environmentsBuffer)
		.Build<EnvironmentSHPass>();

    if (m_currentEnvironment != nullptr) {
		newGraph->AddResource(m_currentEnvironment->GetEnvironmentCubemap());
		forwardBuilder.WithShaderResource(m_currentEnvironment->GetEnvironmentCubemap());
    }

	auto& lightViewResourceGroup = m_pLightManager->GetLightViewInfoResourceGroup();
	newGraph->AddResource(lightViewResourceGroup);

    auto drawShadows = m_shadowMaps != nullptr && getShadowsEnabled();
    if (drawShadows) {
        newGraph->AddResource(m_shadowMaps);

		auto shadowBuilder = newGraph->BuildRenderPass("ShadowPass")
			.WithShaderResource(perObjectBuffer, perMeshBuffer, postSkinningVertices, cameraBuffer, lightViewResourceGroup)
			.WithDepthTarget(m_shadowMaps);

		if (useMeshShaders) {
			shadowBuilder.WithShaderResource(meshResourceGroup);
			if (indirect) {
				shadowBuilder.WithIndirectArguments(indirectCommandBufferResourceGroup);
			}
		}
		shadowBuilder.Build<ShadowPass>(getWireframeEnabled(), useMeshShaders, indirect);

        debugPassParameters.shaderResources.push_back(m_shadowMaps);
    }

    if (m_currentEnvironment != nullptr) {
		newGraph->BuildRenderPass("SkyboxPass")
			.WithShaderResource(m_currentEnvironment->GetEnvironmentCubemap())
			.WithDepthTarget(depthTexture)
			.Build<SkyboxRenderPass>(m_currentEnvironment->GetEnvironmentCubemap());
    }
    forwardBuilder.WithShaderResource(normalsWorldSpace, albedo);
	forwardBuilder.Build<ForwardRenderPass>(getWireframeEnabled(), useMeshShaders, indirect, m_gtaoEnabled ? outputAO->GetSRVInfo()[0].index : 0, normalsWorldSpace->GetSRVInfo()[0].index, albedo->GetSRVInfo()[0].index);

    static const size_t aveFragsPerPixel = 12;
    auto numPPLLNodes = m_xRes * m_yRes * aveFragsPerPixel;
	static const size_t PPLLNodeSize = 24; // two uints, four floats
    TextureDescription desc;
	ImageDimensions dimensions;
	dimensions.width = m_xRes;
	dimensions.height = m_yRes;
	dimensions.rowPitch = m_xRes * sizeof(unsigned int);
	dimensions.slicePitch = dimensions.rowPitch * m_yRes;
	desc.imageDimensions.push_back(dimensions);
    desc.channels = 1;
    desc.format = DXGI_FORMAT_R32_UINT;
    desc.hasRTV = false;
    desc.hasUAV = true;
	desc.hasNonShaderVisibleUAV = true;
    desc.initialState = ResourceState::PIXEL_SRV;
	auto PPLLHeadPointerTexture = PixelBuffer::Create(desc);
	PPLLHeadPointerTexture->SetName(L"PPLLHeadPointerTexture");
    auto PPLLBuffer = ResourceManager::GetInstance().CreateIndexedStructuredBuffer(numPPLLNodes, PPLLNodeSize, ResourceState::UNORDERED_ACCESS, false, true, false);
	PPLLBuffer->SetName(L"PPLLBuffer");
    auto PPLLCounter = ResourceManager::GetInstance().CreateIndexedStructuredBuffer(1, sizeof(unsigned int), ResourceState::UNORDERED_ACCESS, false, true, false);
	PPLLCounter->SetName(L"PPLLCounter");
	newGraph->AddResource(PPLLHeadPointerTexture);
	newGraph->AddResource(PPLLBuffer);
	newGraph->AddResource(PPLLCounter);

    auto PPLLFillBuilder = newGraph->BuildRenderPass("PPFillPass")
        .WithUnorderedAccess(PPLLHeadPointerTexture, PPLLBuffer, PPLLCounter)
        .WithShaderResource(lightBufferResourceGroup, postSkinningVertices, perObjectBuffer, perMeshBuffer, m_pEnvironmentManager->GetEnvironmentPrefilteredCubemapGroup(), m_pEnvironmentManager->GetEnvironmentInfoBuffer(), cameraBuffer, outputAO, normalsWorldSpace);
    if (drawShadows) {
		PPLLFillBuilder.WithShaderResource(m_shadowMaps);
    }
	if (m_clusteredLighting) {
		PPLLFillBuilder.WithShaderResource(clusterBuffer, lightPagesBuffer);
	}
	if (useMeshShaders) {
		PPLLFillBuilder.WithShaderResource(meshResourceGroup);
		if (indirect) {
			PPLLFillBuilder.WithIndirectArguments(indirectCommandBufferResourceGroup);
		}
	}
	PPLLFillBuilder.Build<PPLLFillPass>(getWireframeEnabled(), PPLLHeadPointerTexture, PPLLBuffer, PPLLCounter, numPPLLNodes, useMeshShaders, indirect, m_gtaoEnabled ? outputAO->GetSRVInfo()[0].index : 0, normalsWorldSpace->GetSRVInfo()[0].index);


    newGraph->BuildRenderPass("PPLLResolvePass")
        .WithShaderResource(PPLLHeadPointerTexture, PPLLBuffer)
        .Build<PPLLResolvePass>(PPLLHeadPointerTexture, PPLLBuffer);

	auto debugPass = std::make_shared<DebugRenderPass>();
	if (m_currentDebugTexture != nullptr) {
		debugPass->SetTexture(m_currentDebugTexture.get());
		//debugPassParameters.shaderResources.push_back(m_shadowMaps);
	}
    newGraph->AddRenderPass(debugPass, debugPassParameters, "DebugPass");

    if (getDrawBoundingSpheres()) {
        auto debugSpherePass = std::make_shared<DebugSpherePass>();
        newGraph->AddRenderPass(debugSpherePass, debugPassParameters, "DebugSpherePass");
    }

    newGraph->Compile();
    newGraph->Setup();

	currentRenderGraph = std::move(newGraph);

	rebuildRenderGraph = false;
}

void DX12Renderer::SetEnvironmentInternal(std::wstring name) {

    std::filesystem::path envpath = std::filesystem::path(GetExePath()) / L"textures" / L"environment" / (name+L".hdr");

    if (std::filesystem::exists(envpath)) {
        auto skyHDR = loadTextureFromFileSTBI(envpath.string());
        m_currentEnvironment = m_pEnvironmentManager->CreateEnvironment();
        m_currentEnvironment->SetFromHDRI(skyHDR);
        ResourceManager::GetInstance().SetActiveEnvironmentIndex(m_currentEnvironment->GetEnvironmentIndex());
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
    m_currentDebugTexture = texture;
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