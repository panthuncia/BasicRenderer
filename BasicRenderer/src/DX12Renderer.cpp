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
#include "RenderPass.h"
#include "RenderPasses/ForwardRenderPass.h"
#include "RenderPasses/ShadowPass.h"
#include "SettingsManager.h"
#include "RenderPasses/DebugRenderPass.h"
#include "RenderPasses/SkyboxRenderPass.h"
#include "RenderPasses/EnvironmentConversionPass.h"
#define VERIFY(expr) if (FAILED(expr)) { spdlog::error("Validation error!"); }


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

        // Check if the adapter is a software adapter (we skip these)
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
        {
            continue;
        }

        // Check if the adapter has more dedicated video memory than the current best
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
    SetSettings();
    LoadPipeline(hwnd, x_res, y_res);
    CreateGlobalResources();
}

void DX12Renderer::CreateGlobalResources() {
    m_shadowMaps = std::make_shared<ShadowMaps>(L"ShadowMaps");
    setShadowMaps(m_shadowMaps.get()); // To allow light manager to acccess shadow maps
}

void DX12Renderer::SetSettings() {
	auto& settingsManager = SettingsManager::GetInstance();

    uint8_t numDirectionalCascades = 4;
	float maxShadowDistance = 10.0f;
	settingsManager.registerSetting<uint8_t>("numDirectionalLightCascades", numDirectionalCascades);
    settingsManager.registerSetting<float>("maxShadowDistance", maxShadowDistance);
    settingsManager.registerSetting<std::vector<float>>("directionalLightCascadeSplits", calculateCascadeSplits(numDirectionalCascades, 0.1, 100, maxShadowDistance));
    settingsManager.registerSetting<uint16_t>("shadowResolution", 2048);
    settingsManager.registerSetting<float>("cameraSpeed", 1.5);
	settingsManager.registerSetting<ShadowMaps*>("currentShadowMapsResourceGroup", nullptr);
	settingsManager.registerSetting<bool>("wireframe", false);
	settingsManager.registerSetting<bool>("enableShadows", true);
	settingsManager.registerSetting<uint16_t>("skyboxResolution", 2048);
	settingsManager.registerSetting<std::function<void(ReadbackRequest&&)>>("readbackRequestHandler", [this](ReadbackRequest&& request) {
        SubmitReadbackRequest(std::move(request));
		});
	settingsManager.registerSetting<bool>("enableImageBasedLighting", true);
	setShadowMaps = settingsManager.getSettingSetter<ShadowMaps*>("currentShadowMapsResourceGroup");
    getShadowResolution = settingsManager.getSettingGetter<uint16_t>("shadowResolution");
    setCameraSpeed = settingsManager.getSettingSetter<float>("cameraSpeed");
	getCameraSpeed = settingsManager.getSettingGetter<float>("cameraSpeed");
	setWireframe = settingsManager.getSettingSetter<bool>("wireframe");
	getWireframe = settingsManager.getSettingGetter<bool>("wireframe");
	setShadowsEnabled = settingsManager.getSettingSetter<bool>("enableShadows");
	getShadowsEnabled = settingsManager.getSettingGetter<bool>("enableShadows");
	getSkyboxResolution = settingsManager.getSettingGetter<uint16_t>("skyboxResolution");
	setImageBasedLightingEnabled = settingsManager.getSettingSetter<bool>("enableImageBasedLighting");
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
    ComPtr<ID3D12Device> device;

    ThrowIfFailed(D3D12CreateDevice(
        bestAdapter.Get(),
        D3D_FEATURE_LEVEL_12_1,
        IID_PPV_ARGS(&device)));

    // Initialize device manager

#if defined(_DEBUG)
    ComPtr<ID3D12InfoQueue> infoQueue;
    if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&infoQueue)))) {
        infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
        infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
        infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, TRUE);
    }
#endif

    // Describe and create the command queue
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    ThrowIfFailed(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue)));

    DeviceManager::GetInstance().Initialize(device, commandQueue);
    PSOManager::getInstance().initialize();

    // Describe and create the swap chain
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.BufferCount = 2;
    swapChainDesc.Width = x_res;
    swapChainDesc.Height = y_res;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.SampleDesc.Count = 1;

    ComPtr<IDXGISwapChain1> swapChainTemp;
    ThrowIfFailed(factory->CreateSwapChainForHwnd(
        commandQueue.Get(),
        hwnd,
        &swapChainDesc,
        nullptr,
        nullptr,
        &swapChainTemp));

    ThrowIfFailed(swapChainTemp.As(&swapChain));

    // We do not support fullscreen transitions
    ThrowIfFailed(factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER));

    frameIndex = swapChain->GetCurrentBackBufferIndex();

    // Create descriptor heaps
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = 2;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtvHeap)));

    rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    // Create depth stencil descriptor heap
    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&dsvHeap)));

    // Create frame resources
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(rtvHeap->GetCPUDescriptorHandleForHeapStart());

    for (UINT n = 0; n < 2; n++) {
        ThrowIfFailed(swapChain->GetBuffer(n, IID_PPV_ARGS(&renderTargets[n])));
        device->CreateRenderTargetView(renderTargets[n].Get(), nullptr, rtvHandle);
        rtvHandle.Offset(1, rtvDescriptorSize);
    }

    // Create command allocator
    ThrowIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator)));


    ResourceManager::GetInstance().Initialize(commandQueue.Get());

    // Create the command list
    ThrowIfFailed(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator.Get(), nullptr, IID_PPV_ARGS(&commandList)));

    // Close the command list
    ThrowIfFailed(commandList->Close());

    // Create the fence
    ThrowIfFailed(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
    fenceValue = 1;

    // Create an event handle for the fence
    fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (fenceEvent == nullptr) {
        ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
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

    auto dsvHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    ThrowIfFailed(device->CreateCommittedResource(
        &dsvHeapProperties,
        D3D12_HEAP_FLAG_NONE,
        &depthStencilDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &depthOptimizedClearValue,
        IID_PPV_ARGS(&depthStencilBuffer)
    ));

    // Create the depth stencil view
    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
    device->CreateDepthStencilView(depthStencilBuffer.Get(), &dsvDesc, dsvHeap->GetCPUDescriptorHandleForHeapStart());
}


void DX12Renderer::WaitForPreviousFrame() {
    // Signal and increment the fence value
    const UINT64 fenceToWaitFor = fenceValue;
    ThrowIfFailed(commandQueue->Signal(fence.Get(), fenceToWaitFor));
    fenceValue++;

    // Wait until the previous frame is finished
    if (fence->GetCompletedValue() < fenceToWaitFor) {
        ThrowIfFailed(fence->SetEventOnCompletion(fenceToWaitFor, fenceEvent));
        WaitForSingleObject(fenceEvent, INFINITE);
    }

    frameIndex = swapChain->GetCurrentBackBufferIndex();
}

void DX12Renderer::Update(double elapsedSeconds) {

    if (rebuildRenderGraph) {
		CreateRenderGraph();
    }

    currentScene->GetCamera()->transform.applyMovement(movementState, elapsedSeconds);
    currentScene->GetCamera()->transform.rotatePitchYaw(verticalAngle, horizontalAngle);
    //spdlog::info("horizontal angle: {}", horizontalAngle);
    //spdlog::info("vertical angle: {}", verticalAngle);
    verticalAngle = 0;
    horizontalAngle = 0;

    currentScene->Update();
    auto camera = currentScene->GetCamera();

    ThrowIfFailed(commandAllocator->Reset());

    ResourceManager::GetInstance().UpdatePerFrameBuffer(camera->transform.getGlobalPosition(), camera->GetViewMatrix(), camera->GetProjectionMatrix(), currentScene->GetNumLights(), currentScene->GetLightBufferDescriptorIndex(), currentScene->GetPointCubemapMatricesDescriptorIndex(), currentScene->GetSpotMatricesDescriptorIndex(), currentScene->GetDirectionalCascadeMatricesDescriptorIndex());
    auto& resourceManager = ResourceManager::GetInstance();
    resourceManager.UpdateGPUBuffers();
    resourceManager.ExecuteResourceTransitions();
    ThrowIfFailed(commandList->Reset(commandAllocator.Get(), NULL));
}

void DX12Renderer::Render() {
    // Record all the commands we need to render the scene into the command list

    m_context.currentScene = currentScene.get();
	m_context.device = DeviceManager::GetInstance().GetDevice().Get();
    m_context.commandList = commandList.Get();
	m_context.commandQueue = commandQueue.Get();
    m_context.textureDescriptorHeap = ResourceManager::GetInstance().GetSRVDescriptorHeap().Get();
    m_context.samplerDescriptorHeap = ResourceManager::GetInstance().GetSamplerDescriptorHeap().Get();
    m_context.rtvHeap = rtvHeap.Get();
    m_context.dsvHeap = dsvHeap.Get();
    m_context.renderTargets = renderTargets;
    m_context.rtvDescriptorSize = rtvDescriptorSize;
    m_context.frameIndex = frameIndex;
    m_context.xRes = m_xRes;
    m_context.yRes = m_yRes;

    // Set necessary state
    auto& psoManager = PSOManager::getInstance();
    commandList->SetGraphicsRootSignature(psoManager.GetRootSignature().Get());
    // Bind the constant buffer to the root signature
    ID3D12DescriptorHeap* descriptorHeaps[] = {
        ResourceManager::GetInstance().GetSRVDescriptorHeap().Get(), // The texture descriptor heap
        ResourceManager::GetInstance().GetSamplerDescriptorHeap().Get()
    };

    commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

    // Indicate that the back buffer will be used as a render target
    CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(renderTargets[frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    commandList->ResourceBarrier(1, &barrier);
    commandList->Close();

    // Execute the command list
    ID3D12CommandList* ppCommandLists[] = { commandList.Get() };
    commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

	currentRenderGraph->Execute(m_context); // Main render graph execution

    commandList->Reset(commandAllocator.Get(), nullptr);

    // Indicate that the back buffer will now be used to present
    barrier = CD3DX12_RESOURCE_BARRIER::Transition(renderTargets[frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    commandList->ResourceBarrier(1, &barrier);

    ThrowIfFailed(commandList->Close());

    // Execute the command list
    ppCommandLists[0] = commandList.Get();
    commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

    // Present the frame
    ThrowIfFailed(swapChain->Present(1, 0));

    WaitForPreviousFrame();

    ProcessReadbackRequests();
}

void DX12Renderer::Cleanup() {
    WaitForPreviousFrame();
    CloseHandle(fenceEvent);
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

ComPtr<ID3D12Device>& DX12Renderer::GetDevice() {
    return device;
}

std::shared_ptr<Scene>& DX12Renderer::GetCurrentScene() {
    return currentScene;
}

void DX12Renderer::SetCurrentScene(std::shared_ptr<Scene> newScene) {
    currentScene = newScene;
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
        PSOManager::getInstance().ReloadShaders();
		});

    context.SetActionHandler(InputAction::X, [this](float magnitude, const InputData& inputData) {
		ToggleWireframe();
        });

    context.SetActionHandler(InputAction::Z, [this](float magnitude, const InputData& inputData) {
        ToggleShadows();
        });
}

void DX12Renderer::CreateRenderGraph() {
    currentRenderGraph = std::make_unique<RenderGraph>();

    auto forwardPass = std::make_shared<ForwardRenderPass>(getWireframe());
    auto forwardPassParameters = PassParameters();

    auto debugPassParameters = PassParameters();

    if (m_currentEnvironmentTexture != nullptr) {
        currentRenderGraph->AddResource(m_currentEnvironmentTexture);
		currentRenderGraph->AddResource(m_currentSkybox);
		currentRenderGraph->AddResource(m_environmentIrradiance);
        auto environmentConversionPass = std::make_shared<EnvironmentConversionPass>(m_currentEnvironmentTexture, m_currentSkybox, m_environmentIrradiance, m_environmentName);
        auto environmentConversionPassParameters = PassParameters();
        environmentConversionPassParameters.shaderResources.push_back(m_currentEnvironmentTexture);
        environmentConversionPassParameters.renderTargets.push_back(m_currentSkybox);
        environmentConversionPassParameters.renderTargets.push_back(m_environmentIrradiance);
        currentRenderGraph->AddPass(environmentConversionPass, environmentConversionPassParameters, "EnvironmentConversionPass");
    }

    if (m_shadowMaps != nullptr && getShadowsEnabled()) {
        currentRenderGraph->AddResource(m_shadowMaps);

        auto shadowPass = std::make_shared<ShadowPass>(m_shadowMaps);
        auto shadowPassParameters = PassParameters();
        shadowPassParameters.depthTextures.push_back(m_shadowMaps);
        forwardPassParameters.shaderResources.push_back(m_shadowMaps);
        debugPassParameters.shaderResources.push_back(m_shadowMaps);
        currentRenderGraph->AddPass(shadowPass, shadowPassParameters);
    }

	currentRenderGraph->AddPass(forwardPass, forwardPassParameters);

    if (m_currentSkybox != nullptr || m_currentEnvironmentTexture != nullptr) {
        currentRenderGraph->AddResource(m_currentSkybox);
        auto skyboxPass = std::make_shared<SkyboxRenderPass>(m_currentSkybox);
        auto skyboxPassParameters = PassParameters();
        skyboxPassParameters.shaderResources.push_back(m_currentSkybox);
        currentRenderGraph->AddPass(skyboxPass, skyboxPassParameters);
    }
	auto debugPass = std::make_shared<DebugRenderPass>();
	currentRenderGraph->AddPass(debugPass, debugPassParameters, "DebugPass");
	currentRenderGraph->Compile();
    currentRenderGraph->Setup(commandQueue.Get());

	rebuildRenderGraph = false;
}

void DX12Renderer::ToggleWireframe() {
	setWireframe(!getWireframe());
	rebuildRenderGraph = true;
}

void DX12Renderer::ToggleShadows() {
	setShadowsEnabled(!getShadowsEnabled());
	rebuildRenderGraph = true;
}

void DX12Renderer::SetEnvironment(std::wstring name) {
	// Check if this environment has been processed and cached. If it has, load the cache. If it hasn't, load the environment and process it.
    auto radiancePath = GetCacheFilePath(name + L"_radiance.dds");
    auto skyboxPath = GetCacheFilePath(name + L"_environment.dds");
    if (std::filesystem::exists(radiancePath) && std::filesystem::exists(skyboxPath)) {
		// Load the cached environment
        auto skybox = loadCubemapFromFile(skyboxPath);
		auto radiance = loadCubemapFromFile(radiancePath);
		SetSkybox(skybox);
		SetIrradiance(radiance);
    }
    else {
        auto skyHDR = loadTextureFromFile("textures/environment/"+ws2s(name)+".hdr");
		SetEnvironmentTexture(skyHDR, ws2s(name));
    }
}

void DX12Renderer::SetEnvironmentTexture(std::shared_ptr<Texture> texture, std::string environmentName) {
	m_environmentName = environmentName;
    m_currentEnvironmentTexture = texture;
    m_currentEnvironmentTexture->SetName(L"EnvironmentTexture");
    auto buffer = m_currentEnvironmentTexture->GetBuffer();
    // Create blank texture for skybox
	uint16_t skyboxResolution = getSkyboxResolution();
    auto envCubemap = PixelBuffer::CreateSingleTexture(skyboxResolution, skyboxResolution, buffer->GetChannels(), true, true, false, false);
    auto sampler = Sampler::GetDefaultSampler();
    auto skybox = std::make_shared<Texture>(envCubemap, sampler);
	SetSkybox(skybox);

    auto envRadianceCubemap = PixelBuffer::CreateSingleTexture(skyboxResolution, skyboxResolution, buffer->GetChannels(), true, true, false, false);
    auto irradiance = std::make_shared<Texture>(envRadianceCubemap, sampler);
	SetIrradiance(irradiance);
    if (currentRenderGraph != nullptr) {
        auto environmentPass = currentRenderGraph->GetPassByName("EnvironmentConversionPass");
        if (environmentPass != nullptr) {
            environmentPass->Invalidate();
        }
    }
    rebuildRenderGraph = true;
}

void DX12Renderer::SetSkybox(std::shared_ptr<Texture> texture) {
    m_currentSkybox = texture;
    m_currentSkybox->SetName(L"Skybox");
    rebuildRenderGraph = true;
}

void DX12Renderer::SetIrradiance(std::shared_ptr<Texture> texture) {
	m_environmentIrradiance = texture;
    m_environmentIrradiance->SetName(L"EnvironmentRadiance");
    auto& manager = ResourceManager::GetInstance();
	manager.setEnvironmentIrradianceMapIndex(m_environmentIrradiance->GetSRVDescriptorIndex());
	manager.setEnvironmentIrradianceMapSamplerIndex(m_environmentIrradiance->GetSamplerDescriptorIndex());
	rebuildRenderGraph = true;
}

void DX12Renderer::SubmitReadbackRequest(ReadbackRequest&& request) {
    std::lock_guard<std::mutex> lock(readbackRequestsMutex);
	m_readbackRequests.push_back(std::move(request));
}

std::vector<ReadbackRequest>& DX12Renderer::GetPendingReadbackRequests() {
    return m_readbackRequests;
}

void DX12Renderer::ProcessReadbackRequests() {
    std::lock_guard<std::mutex> lock(readbackRequestsMutex);
    for (auto& request : m_readbackRequests) {
        request.callback();
    }
    m_readbackRequests.clear();
}

void DX12Renderer::SetDebugTexture(Texture* texture) {
    if (currentRenderGraph == nullptr) {
        spdlog::warn("Cannot set debug texture before render graph exists");
        return;
    }
    auto pPass = currentRenderGraph->GetPassByName("DebugPass");
    if (pPass != nullptr) {
        auto pDebugPass = std::dynamic_pointer_cast<DebugRenderPass>(pPass);
        pDebugPass->SetTexture(texture);
    }
    else {
        spdlog::warn("Debug pass does not exist");
    }
}