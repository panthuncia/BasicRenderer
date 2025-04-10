//
// Created by matth on 6/25/2024.
//

#include "DX12Renderer.h"

#define _USE_MATH_DEFINES
#include <math.h>
#include <dxgi1_6.h>
#include <atlbase.h>
#include <filesystem>

#include "Utilities.h"
#include "DirectX/d3dx12.h"
#include "DeviceManager.h"
#include "PSOManager.h"
#include "ResourceManager.h"
#include "RenderContext.h"
#include "RenderGraph.h"
#include "DynamicBuffer.h"
#include "RenderPass.h"
#include "RenderPasses/ForwardRenderPassUnified.h"
#include "RenderPasses/ShadowPass.h"
#include "RenderPasses/ShadowPassMS.h"
#include "RenderPasses/ShadowPassMSIndirect.h"
#include "SettingsManager.h"
#include "RenderPasses/DebugRenderPass.h"
#include "RenderPasses/SkyboxRenderPass.h"
#include "RenderPasses/EnvironmentConversionPass.h"
#include "RenderPasses/EnvironmentFilterPass.h"
#include "RenderPasses/BRDFIntegrationPass.h"
#include "RenderPasses/ClearUAVsPass.h"
#include "RenderPasses/frustrumCullingPass.h"
#include "RenderPasses/DebugSpheresPass.h"
#include "RenderPasses/PPLLFillPass.h"
#include "RenderPasses/PPLLFillPassMS.h"
#include "RenderPasses/PPLLFillPassMSIndirect.h"
#include "RenderPasses/PPLLResolvePass.h"
#include "RenderPasses/SkinningPass.h"
#include "ComputePass.h"
#include "TextureDescription.h"
#include "Menu.h"
#include "DeletionManager.h"
#include "UploadManager.h"
#include "NsightAftermathGpuCrashTracker.h"
#include "Aftermath/GFSDK_Aftermath.h"
#include "NsightAftermathHelpers.h"
#include "CommandSignatureManager.h"
#include "ECSManager.h"
#include "ECSSystems.h"
#include "IndirectCommandBufferManager.h"
#include "MathUtils.h"
#include "MovementState.h"
#include "AnimationController.h"
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
    CreateGlobalResources();

    // Initialize GPU resource managers
    m_pLightManager = LightManager::CreateUnique();
    m_pMeshManager = MeshManager::CreateUnique();
	m_pObjectManager = ObjectManager::CreateUnique();
	m_pIndirectCommandBufferManager = IndirectCommandBufferManager::CreateUnique();
	m_pCameraManager = CameraManager::CreateUnique();
	m_pLightManager->SetCameraManager(m_pCameraManager.get()); // Light manager needs access to camera manager for shadow cameras
	m_pLightManager->SetCommandBufferManager(m_pIndirectCommandBufferManager.get()); // Also for indirect command buffers

	m_managerInterface.SetManagers(m_pMeshManager.get(), m_pObjectManager.get(), m_pIndirectCommandBufferManager.get(), m_pCameraManager.get(), m_pLightManager.get());

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
            light->lightInfo.posWorldSpace = XMVectorSet(mOut.matrix.r[3].m128_f32[0],  // _41
                mOut.matrix.r[3].m128_f32[1],  // _42
                mOut.matrix.r[3].m128_f32[2],  // _43
                1.0f);
            XMVECTOR worldForward = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
            light->lightInfo.dirWorldSpace = XMVector3Normalize(XMVector3TransformNormal(worldForward, mOut.matrix));
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
	float maxShadowDistance = 10.0f;
	settingsManager.registerSetting<uint8_t>("numDirectionalLightCascades", numDirectionalCascades);
    settingsManager.registerSetting<float>("maxShadowDistance", maxShadowDistance);
    settingsManager.registerSetting<std::vector<float>>("directionalLightCascadeSplits", calculateCascadeSplits(numDirectionalCascades, 0.1, 100, maxShadowDistance));
    settingsManager.registerSetting<uint16_t>("shadowResolution", 2048);
    settingsManager.registerSetting<float>("cameraSpeed", 10);
	settingsManager.registerSetting<ShadowMaps*>("currentShadowMapsResourceGroup", nullptr);
	settingsManager.registerSetting<bool>("enableWireframe", false);
	settingsManager.registerSetting<bool>("enableShadows", true);
	settingsManager.registerSetting<uint16_t>("skyboxResolution", 2048);
	settingsManager.registerSetting<bool>("enableImageBasedLighting", true);
	settingsManager.registerSetting<bool>("enablePunctualLighting", true);
	settingsManager.registerSetting<std::string>("environmentName", "");
	settingsManager.registerSetting<unsigned int>("outputType", 0);
    settingsManager.registerSetting<bool>("allowTearing", false);
	settingsManager.registerSetting<bool>("drawBoundingSpheres", false);
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

    m_numFramesInFlight = getNumFramesInFlight();
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
        D3D12_MESSAGE_ID blockedIDs[] = { (D3D12_MESSAGE_ID)1356 }; // Barrier-only command lists
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
	dsvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    // Create depth stencil descriptor heap
    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.NumDescriptors = numFramesInFlight;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&dsvHeap)));

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


    // Create the depth stencil buffer
    D3D12_RESOURCE_DESC depthStencilDesc = CD3DX12_RESOURCE_DESC::Tex2D(
        DXGI_FORMAT_D24_UNORM_S8_UINT, x_res, y_res,
        1, 0, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
    );

    D3D12_CLEAR_VALUE depthOptimizedClearValue = {};
    depthOptimizedClearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthOptimizedClearValue.DepthStencil.Depth = 1.0f;
    depthOptimizedClearValue.DepthStencil.Stencil = 0;

    CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(dsvHeap->GetCPUDescriptorHandleForHeapStart());
    for (int i = 0; i < numFramesInFlight; i++){
        auto dsvHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
		ComPtr<ID3D12Resource> depthStencilBuffer;
        ThrowIfFailed(device->CreateCommittedResource(
            &dsvHeapProperties,
            D3D12_HEAP_FLAG_NONE,
            &depthStencilDesc,
            D3D12_RESOURCE_STATE_DEPTH_WRITE,
            &depthOptimizedClearValue,
            IID_PPV_ARGS(&depthStencilBuffer)
        ));

		depthStencilBuffers.push_back(depthStencilBuffer);
        // Create the depth stencil view
        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
        dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
        device->CreateDepthStencilView(depthStencilBuffers[i].Get(), &dsvDesc, dsvHandle);
        dsvHandle.Offset(1, dsvDescriptorSize);
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

void DX12Renderer::OnResize(UINT newWidth, UINT newHeight) {
    if (!device) return;

    // Wait for the GPU to complete all operations
	WaitForFrame(m_frameIndex);

    // Release the resources tied to the swap chain
    auto numFramesInFlight = getNumFramesInFlight();

    for (int i = 0; i < numFramesInFlight; ++i) {
        renderTargets[i].Reset();
		depthStencilBuffers[i].Reset();
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

    // Recreate the depth/stencil buffer
    D3D12_RESOURCE_DESC depthStencilDesc = CD3DX12_RESOURCE_DESC::Tex2D(
        DXGI_FORMAT_D24_UNORM_S8_UINT, newWidth, newHeight,
        1, 0, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);

    D3D12_CLEAR_VALUE depthOptimizedClearValue = {};
    depthOptimizedClearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthOptimizedClearValue.DepthStencil.Depth = 1.0f;
    depthOptimizedClearValue.DepthStencil.Stencil = 0;

    auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

    for (int i = 0; i < numFramesInFlight; i++) {
        ThrowIfFailed(device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &depthStencilDesc,
            D3D12_RESOURCE_STATE_DEPTH_WRITE,
            &depthOptimizedClearValue,
            IID_PPV_ARGS(&depthStencilBuffers[i])));

        // Create the depth stencil view
        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
        dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
        device->CreateDepthStencilView(depthStencilBuffers[i].Get(), &dsvDesc, dsvHeap->GetCPUDescriptorHandleForHeapStart());
    }
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
    resourceManager.UpdatePerFrameBuffer(cameraIndex, m_pLightManager->GetNumLights(), m_pLightManager->GetActiveLightIndicesBufferDescriptorIndex(), m_pLightManager->GetLightBufferDescriptorIndex(), m_pLightManager->GetPointCubemapMatricesDescriptorIndex(), m_pLightManager->GetSpotMatricesDescriptorIndex(), m_pLightManager->GetDirectionalCascadeMatricesDescriptorIndex());

	currentRenderGraph->Update();

	updateManager.ResetAllocators(m_frameIndex); // Reset allocators to avoid leaking memory
    updateManager.ExecuteResourceCopies(m_frameIndex, graphicsQueue.Get());// copies come before uploads to avoid overwriting data
	updateManager.ProcessUploads(m_frameIndex, graphicsQueue.Get());

    resourceManager.ExecuteResourceTransitions();
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
    m_context.dsvHeap = dsvHeap.Get();
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
	m_context.drawStats = *drawStats;

    // Indicate that the back buffer will be used as a render target
    CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    commandList->ResourceBarrier(1, &barrier);
    
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_context.rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_context.frameIndex, m_context.rtvDescriptorSize);
    CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(m_context.dsvHeap->GetCPUDescriptorHandleForHeapStart(), m_context.frameIndex, m_context.dsvDescriptorSize);
    //commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

    // Clear the render target
    const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
    commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
    commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

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

    auto& meshManager = m_pMeshManager;
    auto& objectManager = m_pObjectManager;
	auto meshResourceGroup = meshManager->GetResourceGroup();
	newGraph->AddResource(meshResourceGroup);

    auto& perObjectBuffer = objectManager->GetPerObjectBuffers();
	newGraph->AddResource(perObjectBuffer);
    auto& perMeshBuffer = meshManager->GetPerMeshBuffers();
	newGraph->AddResource(perMeshBuffer);

	auto& preSkinningVertices = meshManager->GetPreSkinningVertices();
	auto& postSkinningVertices = meshManager->GetPostSkinningVertices();
	auto& normalMatrixBuffer = objectManager->GetNormalMatrixBuffer();
	newGraph->AddResource(preSkinningVertices);
	newGraph->AddResource(postSkinningVertices);
	newGraph->AddResource(normalMatrixBuffer);

    bool useMeshShaders = getMeshShadersEnabled();
    if (!DeviceManager::GetInstance().GetMeshShadersSupported()) {
        useMeshShaders = false;
    }

    // Skinning
	auto skinningPass = std::make_shared<SkinningPass>();
	ComputePassParameters skinningPassParameters;
	skinningPassParameters.shaderResources.push_back(perObjectBuffer);
	skinningPassParameters.shaderResources.push_back(perMeshBuffer);
	skinningPassParameters.shaderResources.push_back(preSkinningVertices);
    skinningPassParameters.shaderResources.push_back(normalMatrixBuffer);
	skinningPassParameters.unorderedAccessViews.push_back(postSkinningVertices);
	newGraph->AddComputePass(skinningPass, skinningPassParameters, "SkinningPass");

	auto& cameraManager = m_pCameraManager;
    auto& cameraBuffer = cameraManager->GetCameraBuffer();
    newGraph->AddResource(cameraBuffer);

	auto& perFrameBuffer = ResourceManager::GetInstance().GetPerFrameBuffer();
	newGraph->AddResource(perFrameBuffer, true, ResourceState::CONSTANT);

    // Frustrum culling
	bool indirect = getIndirectDrawsEnabled();
	if (!DeviceManager::GetInstance().GetMeshShadersSupported()) { // Indirect draws only supported with mesh shaders
		indirect = false;
	}
	std::shared_ptr<ResourceGroup> indirectCommandBufferResourceGroup = nullptr;
    if (indirect) {
        // Clear UAVs
        indirectCommandBufferResourceGroup = m_pIndirectCommandBufferManager->GetResourceGroup();
        newGraph->AddResource(indirectCommandBufferResourceGroup);
        auto clearUAVsPass = std::make_shared<ClearUAVsPass>();
        RenderPassParameters clearUAVsPassParameters;
        clearUAVsPassParameters.copyTargets.push_back(indirectCommandBufferResourceGroup);
        newGraph->AddRenderPass(clearUAVsPass, clearUAVsPassParameters, "ClearUAVsPass");

        // Compute pass
        auto frustrumCullingPass = std::make_shared<FrustrumCullingPass>();
        ComputePassParameters frustrumCullingPassParameters;
        frustrumCullingPassParameters.shaderResources.push_back(perObjectBuffer);
        frustrumCullingPassParameters.shaderResources.push_back(perMeshBuffer);
		frustrumCullingPassParameters.shaderResources.push_back(cameraBuffer);
        frustrumCullingPassParameters.unorderedAccessViews.push_back(indirectCommandBufferResourceGroup);
        newGraph->AddComputePass(frustrumCullingPass, frustrumCullingPassParameters, "FrustrumCullingPass");
    }

    auto forwardPassParameters = RenderPassParameters();
	forwardPassParameters.shaderResources.push_back(perObjectBuffer);
	forwardPassParameters.shaderResources.push_back(perMeshBuffer);
    //forwardPassParameters.shaderResources.push_back(normalMatrixBuffer);
	forwardPassParameters.shaderResources.push_back(postSkinningVertices);
	forwardPassParameters.shaderResources.push_back(cameraBuffer);

    std::shared_ptr<RenderPass> forwardPass = nullptr;

    if (useMeshShaders) {
        forwardPassParameters.shaderResources.push_back(meshResourceGroup);
		if (indirect) { // Indirect draws only supported with mesh shaders, becasue I'm not writing a separate codepath for doing it the bad way
            forwardPass = std::make_shared<ForwardRenderPassUnified>(getWireframeEnabled(), true, true);
            forwardPassParameters.indirectArgumentBuffers.push_back(indirectCommandBufferResourceGroup);
        }
        else {
            forwardPass = std::make_shared<ForwardRenderPassUnified>(getWireframeEnabled(), true, false);
        }
	}
    else {
        forwardPass = std::make_shared<ForwardRenderPassUnified>(getWireframeEnabled(), false, false);
    }

    auto debugPassParameters = RenderPassParameters();

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

        ResourceManager::GetInstance().setEnvironmentBRDFLUTIndex(m_lutTexture->GetBuffer()->GetSRVInfo().index);
		ResourceManager::GetInstance().setEnvironmentBRDFLUTSamplerIndex(m_lutTexture->GetSamplerDescriptorIndex());

		auto brdfIntegrationPass = std::make_shared<BRDFIntegrationPass>(m_lutTexture);
		auto brdfIntegrationPassParameters = RenderPassParameters();
		brdfIntegrationPassParameters.renderTargets.push_back(m_lutTexture);
        newGraph->AddRenderPass(brdfIntegrationPass, brdfIntegrationPassParameters, "BRDFIntegrationPass");
    }

    newGraph->AddResource(m_lutTexture);
	forwardPassParameters.shaderResources.push_back(m_lutTexture);

    // Check if we've already computed this environment
    bool skipEnvironmentPass = false;
    if (currentRenderGraph != nullptr) {
        auto currentEnvironmentPass = currentRenderGraph->GetRenderPassByName("EnvironmentConversionPass");
        if (currentEnvironmentPass != nullptr) {
            if (!currentEnvironmentPass->IsInvalidated()) {
                skipEnvironmentPass = true;
                DeletionManager::GetInstance().MarkForDelete(m_currentEnvironmentTexture);
                m_currentEnvironmentTexture = nullptr;
            }
        }
    }

    if (m_currentEnvironmentTexture != nullptr && !skipEnvironmentPass) {

        newGraph->AddResource(m_currentEnvironmentTexture);
        //newGraph->AddResource(m_currentSkybox);
        //newGraph->AddResource(m_environmentIrradiance);
        auto environmentConversionPass = std::make_shared<EnvironmentConversionPass>(m_currentEnvironmentTexture, m_currentSkybox, m_environmentIrradiance, m_environmentName);
        auto environmentConversionPassParameters = RenderPassParameters();
        environmentConversionPassParameters.shaderResources.push_back(m_currentEnvironmentTexture);
        environmentConversionPassParameters.renderTargets.push_back(m_currentSkybox);
        environmentConversionPassParameters.renderTargets.push_back(m_environmentIrradiance);
        newGraph->AddRenderPass(environmentConversionPass, environmentConversionPassParameters, "EnvironmentConversionPass");

        //newGraph->AddResource(m_prefilteredEnvironment);
		auto environmentFilterPass = std::make_shared<EnvironmentFilterPass>(m_currentEnvironmentTexture, m_prefilteredEnvironment, m_environmentName);
		auto environmentFilterPassParameters = RenderPassParameters();
		environmentFilterPassParameters.shaderResources.push_back(m_currentEnvironmentTexture);
		environmentFilterPassParameters.renderTargets.push_back(m_prefilteredEnvironment);
        newGraph->AddRenderPass(environmentFilterPass, environmentFilterPassParameters, "EnvironmentFilterPass");
    }

    if (m_prefilteredEnvironment != nullptr) {
        newGraph->AddResource(m_prefilteredEnvironment);
		forwardPassParameters.shaderResources.push_back(m_prefilteredEnvironment);
    }
    if (m_environmentIrradiance != nullptr) {
        newGraph->AddResource(m_environmentIrradiance);
        forwardPassParameters.shaderResources.push_back(m_environmentIrradiance);
    }

    auto drawShadows = m_shadowMaps != nullptr && getShadowsEnabled();
    if (drawShadows) {
        newGraph->AddResource(m_shadowMaps);

        std::shared_ptr<RenderPass> shadowPass = nullptr;
        auto shadowPassParameters = RenderPassParameters();
        //shadowPassParameters.shaderResources.push_back(normalMatrixBuffer);
		shadowPassParameters.shaderResources.push_back(postSkinningVertices);

        if (useMeshShaders) { 
            shadowPassParameters.shaderResources.push_back(meshResourceGroup);
            if (indirect) {
                shadowPass = std::make_shared<ShadowPassMSIndirect>(m_shadowMaps);
                shadowPassParameters.indirectArgumentBuffers.push_back(indirectCommandBufferResourceGroup);
            }
            else {
				shadowPass = std::make_shared<ShadowPassMS>(m_shadowMaps);
            }
		}
		else {
			shadowPass = std::make_shared<ShadowPass>(m_shadowMaps);
		}
		shadowPassParameters.shaderResources.push_back(perObjectBuffer);
		shadowPassParameters.shaderResources.push_back(perMeshBuffer);
		shadowPassParameters.shaderResources.push_back(cameraBuffer);
        shadowPassParameters.depthTextures.push_back(m_shadowMaps);
        forwardPassParameters.shaderResources.push_back(m_shadowMaps);
        debugPassParameters.shaderResources.push_back(m_shadowMaps);
        newGraph->AddRenderPass(shadowPass, shadowPassParameters);
    }

    if (m_currentSkybox != nullptr) {
        newGraph->AddResource(m_currentSkybox);
        auto skyboxPass = std::make_shared<SkyboxRenderPass>(m_currentSkybox);
        auto skyboxPassParameters = RenderPassParameters();
        skyboxPassParameters.shaderResources.push_back(m_currentSkybox);
        newGraph->AddRenderPass(skyboxPass, skyboxPassParameters);
    }

    newGraph->AddRenderPass(forwardPass, forwardPassParameters);

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

    std::shared_ptr<RenderPass> pPPLLFillPass;
    auto PPLLFillPassParameters = RenderPassParameters();
    //PPLLFillPassParameters.shaderResources.push_back(normalMatrixBuffer);
	PPLLFillPassParameters.shaderResources.push_back(postSkinningVertices);
    if (indirect) {
        pPPLLFillPass = std::make_shared<PPLLFillPassMSIndirect>(getWireframeEnabled(), PPLLHeadPointerTexture, PPLLBuffer, PPLLCounter, numPPLLNodes);
		PPLLFillPassParameters.indirectArgumentBuffers.push_back(indirectCommandBufferResourceGroup);
    }
    else {
        if (useMeshShaders) {
            pPPLLFillPass = std::make_shared<PPLLFillPassMS>(getWireframeEnabled(), PPLLHeadPointerTexture, PPLLBuffer, PPLLCounter, numPPLLNodes);
        }
        else {
            pPPLLFillPass = std::make_shared<PPLLFillPass>(getWireframeEnabled(), PPLLHeadPointerTexture, PPLLBuffer, PPLLCounter, numPPLLNodes);
        }
    }
	PPLLFillPassParameters.shaderResources.push_back(perObjectBuffer);
	PPLLFillPassParameters.shaderResources.push_back(meshResourceGroup);
	PPLLFillPassParameters.shaderResources.push_back(perMeshBuffer);
    if (drawShadows) {
        PPLLFillPassParameters.shaderResources.push_back(m_shadowMaps);
    }
    PPLLFillPassParameters.shaderResources.push_back(m_prefilteredEnvironment);
	PPLLFillPassParameters.shaderResources.push_back(m_environmentIrradiance);
    PPLLFillPassParameters.shaderResources.push_back(meshResourceGroup);
	PPLLFillPassParameters.shaderResources.push_back(cameraBuffer);
	PPLLFillPassParameters.unorderedAccessViews.push_back(PPLLHeadPointerTexture);
	PPLLFillPassParameters.unorderedAccessViews.push_back(PPLLBuffer);
	PPLLFillPassParameters.unorderedAccessViews.push_back(PPLLCounter);
	newGraph->AddRenderPass(pPPLLFillPass, PPLLFillPassParameters);

	auto ResolvePass = std::make_shared<PPLLResolvePass>(PPLLHeadPointerTexture, PPLLBuffer);
	auto ResolvePassParameters = RenderPassParameters();
	ResolvePassParameters.shaderResources.push_back(PPLLHeadPointerTexture);
	ResolvePassParameters.shaderResources.push_back(PPLLBuffer);
	newGraph->AddRenderPass(ResolvePass, ResolvePassParameters);

	auto debugPass = std::make_shared<DebugRenderPass>();
	if (m_currentDebugTexture != nullptr) {
		debugPass->SetTexture(m_currentDebugTexture.get());
		debugPassParameters.shaderResources.push_back(m_shadowMaps);
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
	// Check if this environment has been processed and cached. If it has, load the cache. If it hasn't, load the environment and process it.
    auto radiancePath = GetCacheFilePath(name + L"_radiance.dds", L"environments");
    auto skyboxPath = GetCacheFilePath(name + L"_environment.dds", L"environments");
	auto prefilteredPath = GetCacheFilePath(name + L"_prefiltered.dds", L"environments");
    if (std::filesystem::exists(radiancePath) && std::filesystem::exists(skyboxPath) && std::filesystem::exists(prefilteredPath)) {
        if (m_currentEnvironmentTexture != nullptr) { // unset environment texture so render graph doesn't try to rebuld resources
            DeletionManager::GetInstance().MarkForDelete(m_currentEnvironmentTexture);
            m_currentEnvironmentTexture = nullptr;
        }
		// Load the cached environment
        auto skybox = loadCubemapFromFile(skyboxPath);
		auto radiance = loadCubemapFromFile(radiancePath);
        auto prefiltered = loadCubemapFromFile(prefilteredPath);
		SetSkybox(skybox);
		SetIrradiance(radiance);
		SetPrefilteredEnvironment(prefiltered);
        rebuildRenderGraph = true;
    }
    else {
        std::filesystem::path envpath = std::filesystem::path(GetExePath()) / L"textures" / L"environment" / (name+L".hdr");
        
        if (std::filesystem::exists(envpath)) {
            auto skyHDR = loadTextureFromFileSTBI(envpath.string());
            SetEnvironmentTexture(skyHDR, ws2s(name));
        }
        else {
            spdlog::error("Environment file not found: " + envpath.string());
        }
    }
}

void DX12Renderer::SetEnvironmentTexture(std::shared_ptr<Texture> texture, std::string environmentName) {
	m_environmentName = environmentName;
	if (m_currentEnvironmentTexture != nullptr) { // Don't delete resources mid-frame
        DeletionManager::GetInstance().MarkForDelete(m_currentEnvironmentTexture);
	}
    m_currentEnvironmentTexture = texture;
    m_currentEnvironmentTexture->SetName(L"EnvironmentTexture");
    auto buffer = m_currentEnvironmentTexture->GetBuffer();
    // Create blank texture for skybox
	uint16_t skyboxResolution = getSkyboxResolution();

	TextureDescription skyboxDesc;
    ImageDimensions dims;
    dims.height = skyboxResolution;
	dims.width = skyboxResolution;
	dims.rowPitch = skyboxResolution * 4;
	dims.slicePitch = skyboxResolution * skyboxResolution * 4;
    for (int i = 0; i < 6; i++) {
        skyboxDesc.imageDimensions.push_back(dims);
    }
    skyboxDesc.channels = buffer->GetChannels();
	skyboxDesc.isCubemap = true;
    skyboxDesc.hasRTV = true;
	skyboxDesc.format = DXGI_FORMAT_R8G8B8A8_UNORM;

    auto envCubemap = PixelBuffer::Create(skyboxDesc);
    auto sampler = Sampler::GetDefaultSampler();
    auto skybox = std::make_shared<Texture>(envCubemap, sampler);
	SetSkybox(skybox);

	TextureDescription irradianceDesc;
    for (int i = 0; i < 6; i++) {
        irradianceDesc.imageDimensions.push_back(dims);
    }
	irradianceDesc.channels = buffer->GetChannels();
	irradianceDesc.isCubemap = true;
	irradianceDesc.hasRTV = true;
	irradianceDesc.format = DXGI_FORMAT_R8G8B8A8_UNORM;

    auto envRadianceCubemap = PixelBuffer::Create(irradianceDesc);
    auto irradiance = std::make_shared<Texture>(envRadianceCubemap, sampler);
	SetIrradiance(irradiance);

	TextureDescription prefilteredDesc;
    for (int i = 0; i < 6; i++) {
        prefilteredDesc.imageDimensions.push_back(dims);
    }
	prefilteredDesc.channels = buffer->GetChannels();
	prefilteredDesc.isCubemap = true;
	prefilteredDesc.hasRTV = true;
	prefilteredDesc.format = DXGI_FORMAT_R8G8B8A8_UNORM;
	prefilteredDesc.generateMipMaps = true;

	auto prefilteredEnvironmentCubemap = PixelBuffer::Create(prefilteredDesc);
	auto prefilteredEnvironment = std::make_shared<Texture>(prefilteredEnvironmentCubemap, sampler);
	SetPrefilteredEnvironment(prefilteredEnvironment);

    if (currentRenderGraph != nullptr) {
        auto environmentPass = currentRenderGraph->GetRenderPassByName("EnvironmentConversionPass");
        if (environmentPass != nullptr) {
            environmentPass->Invalidate();
        }
		auto environmentFilterPass = currentRenderGraph->GetRenderPassByName("EnvironmentFilterPass");
        if (environmentFilterPass != nullptr) {
            environmentFilterPass->Invalidate();
        }
    }
    rebuildRenderGraph = true;
}

void DX12Renderer::SetSkybox(std::shared_ptr<Texture> texture) {
    if (m_currentSkybox != nullptr) { // Don't delete resources mid-frame
        DeletionManager::GetInstance().MarkForDelete(m_currentSkybox);
    }
    m_currentSkybox = texture;
    m_currentSkybox->SetName(L"Skybox");
    rebuildRenderGraph = true;
}

void DX12Renderer::SetIrradiance(std::shared_ptr<Texture> texture) {
    if (m_environmentIrradiance != nullptr) { // Don't delete resources mid-frame
        DeletionManager::GetInstance().MarkForDelete(m_environmentIrradiance);
    }
	m_environmentIrradiance = texture;
    m_environmentIrradiance->SetName(L"EnvironmentRadiance");
    auto& manager = ResourceManager::GetInstance();
	manager.setEnvironmentIrradianceMapIndex(m_environmentIrradiance->GetBuffer()->GetSRVInfo().index);
	manager.setEnvironmentIrradianceMapSamplerIndex(m_environmentIrradiance->GetSamplerDescriptorIndex());
	rebuildRenderGraph = true;
}

void DX12Renderer::SetPrefilteredEnvironment(std::shared_ptr<Texture> texture) {
	if (m_prefilteredEnvironment != nullptr) { // Don't delete resources mid-frame
        DeletionManager::GetInstance().MarkForDelete(m_prefilteredEnvironment);
	}
	m_prefilteredEnvironment = texture;
	m_prefilteredEnvironment->SetName(L"PrefilteredEnvironment");
	auto& manager = ResourceManager::GetInstance();
	manager.setPrefilteredEnvironmentMapIndex(m_prefilteredEnvironment->GetBuffer()->GetSRVInfo().index);
	manager.setPrefilteredEnvironmentMapSamplerIndex(m_prefilteredEnvironment->GetSamplerDescriptorIndex());
	rebuildRenderGraph = true;
}

void DX12Renderer::SetDebugTexture(std::shared_ptr<Texture> texture) {
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