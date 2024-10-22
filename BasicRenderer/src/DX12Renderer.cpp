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
#include "RenderPasses/ForwardRenderPassMS.h"
#include "RenderPasses/ShadowPass.h"
#include "SettingsManager.h"
#include "RenderPasses/DebugRenderPass.h"
#include "RenderPasses/SkyboxRenderPass.h"
#include "RenderPasses/EnvironmentConversionPass.h"
#include "RenderPasses/EnvironmentFilterPass.h"
#include "RenderPasses/BRDFIntegrationPass.h"
#include "TextureDescription.h"
#include "Menu.h"
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
    SetSettings();
    LoadPipeline(hwnd, x_res, y_res);
    CreateGlobalResources();
	Menu::GetInstance().Initialize(hwnd, device, commandQueue, swapChain);
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
    settingsManager.registerSetting<float>("cameraSpeed", 1.5);
	settingsManager.registerSetting<ShadowMaps*>("currentShadowMapsResourceGroup", nullptr);
	settingsManager.registerSetting<bool>("wireframe", false);
	settingsManager.registerSetting<bool>("enableShadows", true);
	settingsManager.registerSetting<uint16_t>("skyboxResolution", 2048);
	settingsManager.registerSetting<std::function<void(ReadbackRequest&&)>>("readbackRequestHandler", [this](ReadbackRequest&& request) {
        SubmitReadbackRequest(std::move(request));
		});
	settingsManager.registerSetting<bool>("enableImageBasedLighting", true);
	settingsManager.registerSetting<bool>("enablePunctualLighting", true);
	settingsManager.registerSetting<std::string>("environmentName", "");
	settingsManager.registerSetting<unsigned int>("outputType", 0);
	// This feels like abuse of the settings manager, but it's the easiest way to get the renderable objects to the menu
	settingsManager.registerSetting<std::function<std::unordered_map<UINT, std::shared_ptr<RenderableObject>>& ()>>("getRenderableObjects", [this]() -> std::unordered_map<UINT, std::shared_ptr<RenderableObject>>& {
		return currentScene->GetRenderableObjectIDMap();
		});
    settingsManager.registerSetting<std::function<SceneNode&()>>("getSceneRoot", [this]() -> SceneNode& {
        return currentScene->GetRoot();
        });
	settingsManager.registerSetting<std::function<std::shared_ptr<SceneNode>(Scene& scene)>>("appendScene", [this](Scene& scene) -> std::shared_ptr<SceneNode> {
		return AppendScene(scene);
		});
    settingsManager.registerSetting<std::function<void(std::shared_ptr<void>)>>("markForDelete", [this](std::shared_ptr<void> node) {
		MarkForDelete(node);
        });
	setShadowMaps = settingsManager.getSettingSetter<ShadowMaps*>("currentShadowMapsResourceGroup");
    getShadowResolution = settingsManager.getSettingGetter<uint16_t>("shadowResolution");
    setCameraSpeed = settingsManager.getSettingSetter<float>("cameraSpeed");
	getCameraSpeed = settingsManager.getSettingGetter<float>("cameraSpeed");
	setWireframe = settingsManager.getSettingSetter<bool>("wireframe");
	getWireframe = settingsManager.getSettingGetter<bool>("wireframe");
	setShadowsEnabled = settingsManager.getSettingSetter<bool>("enableShadows");
	getShadowsEnabled = settingsManager.getSettingGetter<bool>("enableShadows");
    settingsManager.addObserver<bool>("enableShadows", [this](const bool& newValue) {
            // Trigger recompilation of the render graph when setting changes
		    rebuildRenderGraph = true;
        });
	getSkyboxResolution = settingsManager.getSettingGetter<uint16_t>("skyboxResolution");
	setImageBasedLightingEnabled = settingsManager.getSettingSetter<bool>("enableImageBasedLighting");
	setEnvironment = settingsManager.getSettingSetter<std::string>("environmentName");
	settingsManager.addObserver<std::string>("environmentName", [this](const std::string& newValue) {
		SetEnvironmentInternal(s2ws(newValue));
		});
	settingsManager.addObserver<unsigned int>("outputType", [this](const unsigned int& newValue) {
		ResourceManager::GetInstance().SetOutputType(newValue);
		});
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

    ThrowIfFailed(D3D12CreateDevice(
        bestAdapter.Get(),
        D3D_FEATURE_LEVEL_12_1,
        IID_PPV_ARGS(&device)));

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

    // Initialize device manager and PSO manager
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

    // Create RTV descriptor heap
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

    // Create a fence
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

void DX12Renderer::OnResize(UINT newWidth, UINT newHeight) {
    if (!device) return;

    // Wait for the GPU to complete all operations
	WaitForPreviousFrame();

    // Release the resources tied to the swap chain
    for (int i = 0; i < 2; ++i) {
        renderTargets[i].Reset();
    }
    depthStencilBuffer.Reset();

    // Resize the swap chain
    ThrowIfFailed(swapChain->ResizeBuffers(
        2, // Buffer count
        newWidth, newHeight,
        DXGI_FORMAT_R8G8B8A8_UNORM,
        DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH));

    frameIndex = swapChain->GetCurrentBackBufferIndex();

    // Recreate the render target views
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(rtvHeap->GetCPUDescriptorHandleForHeapStart());
    for (UINT i = 0; i < 2; i++) {
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
    ThrowIfFailed(device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &depthStencilDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &depthOptimizedClearValue,
        IID_PPV_ARGS(&depthStencilBuffer)));

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

    // Indicate that the back buffer will be used as a render target
    CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(renderTargets[frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    commandList->ResourceBarrier(1, &barrier);
    
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_context.rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_context.frameIndex, m_context.rtvDescriptorSize);
    CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(m_context.dsvHeap->GetCPUDescriptorHandleForHeapStart());
    commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

    // Clear the render target
    const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
    commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
    commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    commandList->Close();

    // Execute the command list
    ID3D12CommandList* ppCommandLists[] = { commandList.Get() };
    commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

	currentRenderGraph->Execute(m_context); // Main render graph execution

	Menu::GetInstance().Render(m_context); // Render menu

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

	ProcessReadbackRequests(); // Save images to disk if requested

    m_stuffToDelete.clear(); // Deferred deletion
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
    currentScene = newScene;
    currentScene->Activate();
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
        PSOManager::getInstance().ReloadShaders();
		});

    context.SetActionHandler(InputAction::X, [this](float magnitude, const InputData& inputData) {
		ToggleWireframe();
        });

    context.SetActionHandler(InputAction::Z, [this](float magnitude, const InputData& inputData) {
        });
}

void DX12Renderer::CreateRenderGraph() {
    auto newGraph = std::make_unique<RenderGraph>();

	auto meshResourceGroup = currentScene->GetMeshManager()->GetResourceGroup();
	newGraph->AddResource(meshResourceGroup);

    auto forwardPass = std::make_shared<ForwardRenderPassMS>(getWireframe());
    auto forwardPassParameters = PassParameters();
	forwardPassParameters.shaderResources.push_back(meshResourceGroup);

    auto debugPassParameters = PassParameters();

    if (m_lutTexture == nullptr) {
		TextureDescription lutDesc;
		lutDesc.width = 512;
		lutDesc.height = 512;
		lutDesc.channels = 2;
		lutDesc.isCubemap = false;
		lutDesc.hasRTV = true;
		lutDesc.format = DXGI_FORMAT_R16G16_FLOAT;
		auto lutBuffer = PixelBuffer::Create(lutDesc);
		auto sampler = Sampler::GetDefaultSampler();
		m_lutTexture = std::make_shared<Texture>(lutBuffer, sampler);
		m_lutTexture->SetName(L"LUTTexture");

        ResourceManager::GetInstance().setEnvironmentBRDFLUTIndex(m_lutTexture->GetSRVDescriptorIndex());
		ResourceManager::GetInstance().setEnvironmentBRDFLUTSamplerIndex(m_lutTexture->GetSamplerDescriptorIndex());

		auto brdfIntegrationPass = std::make_shared<BRDFIntegrationPass>(m_lutTexture);
		auto brdfIntegrationPassParameters = PassParameters();
		brdfIntegrationPassParameters.renderTargets.push_back(m_lutTexture);
        newGraph->AddPass(brdfIntegrationPass, brdfIntegrationPassParameters, "BRDFIntegrationPass");
    }

    newGraph->AddResource(m_lutTexture);
	forwardPassParameters.shaderResources.push_back(m_lutTexture);

    // Check if we've already computed this environment
    bool skipEnvironmentPass = false;
    if (currentRenderGraph != nullptr) {
        auto currentEnvironmentPass = currentRenderGraph->GetPassByName("EnvironmentConversionPass");
        if (currentEnvironmentPass != nullptr) {
            if (!currentEnvironmentPass->IsInvalidated()) {
                skipEnvironmentPass = true;
				MarkForDelete(m_currentEnvironmentTexture);
                m_currentEnvironmentTexture = nullptr;
            }
        }
    }

    if (m_currentEnvironmentTexture != nullptr && !skipEnvironmentPass) {

        newGraph->AddResource(m_currentEnvironmentTexture);
        newGraph->AddResource(m_currentSkybox);
        newGraph->AddResource(m_environmentIrradiance);
        auto environmentConversionPass = std::make_shared<EnvironmentConversionPass>(m_currentEnvironmentTexture, m_currentSkybox, m_environmentIrradiance, m_environmentName);
        auto environmentConversionPassParameters = PassParameters();
        environmentConversionPassParameters.shaderResources.push_back(m_currentEnvironmentTexture);
        environmentConversionPassParameters.renderTargets.push_back(m_currentSkybox);
        environmentConversionPassParameters.renderTargets.push_back(m_environmentIrradiance);
        newGraph->AddPass(environmentConversionPass, environmentConversionPassParameters, "EnvironmentConversionPass");

        newGraph->AddResource(m_prefilteredEnvironment);
		auto environmentFilterPass = std::make_shared<EnvironmentFilterPass>(m_currentEnvironmentTexture, m_prefilteredEnvironment, m_environmentName);
		auto environmentFilterPassParameters = PassParameters();
		environmentFilterPassParameters.shaderResources.push_back(m_currentEnvironmentTexture);
		environmentFilterPassParameters.renderTargets.push_back(m_prefilteredEnvironment);
        newGraph->AddPass(environmentFilterPass, environmentFilterPassParameters, "EnvironmentFilterPass");
    }

    if (m_prefilteredEnvironment != nullptr) {
        newGraph->AddResource(m_prefilteredEnvironment);
		forwardPassParameters.shaderResources.push_back(m_prefilteredEnvironment);
    }

    if (m_shadowMaps != nullptr && getShadowsEnabled()) {
        newGraph->AddResource(m_shadowMaps);

        auto shadowPass = std::make_shared<ShadowPass>(m_shadowMaps);
        auto shadowPassParameters = PassParameters();
        shadowPassParameters.depthTextures.push_back(m_shadowMaps);
		shadowPassParameters.shaderResources.push_back(meshResourceGroup);
        forwardPassParameters.shaderResources.push_back(m_shadowMaps);
        debugPassParameters.shaderResources.push_back(m_shadowMaps);
        newGraph->AddPass(shadowPass, shadowPassParameters);
    }

    if (m_currentSkybox != nullptr) {
        newGraph->AddResource(m_currentSkybox);
        auto skyboxPass = std::make_shared<SkyboxRenderPass>(m_currentSkybox);
        auto skyboxPassParameters = PassParameters();
        skyboxPassParameters.shaderResources.push_back(m_currentSkybox);
        newGraph->AddPass(skyboxPass, skyboxPassParameters);
    }

    newGraph->AddPass(forwardPass, forwardPassParameters);

	auto debugPass = std::make_shared<DebugRenderPass>();
    newGraph->AddPass(debugPass, debugPassParameters, "DebugPass");
    newGraph->Compile();
    newGraph->Setup(commandQueue.Get());

	currentRenderGraph = std::move(newGraph);

	rebuildRenderGraph = false;
}

void DX12Renderer::ToggleWireframe() {
	setWireframe(!getWireframe());
	rebuildRenderGraph = true;
}

void DX12Renderer::SetEnvironmentInternal(std::wstring name) {
	// Check if this environment has been processed and cached. If it has, load the cache. If it hasn't, load the environment and process it.
    auto radiancePath = GetCacheFilePath(name + L"_radiance.dds", L"environments");
    auto skyboxPath = GetCacheFilePath(name + L"_environment.dds", L"environments");
	auto prefilteredPath = GetCacheFilePath(name + L"_prefiltered.dds", L"environments");
    if (std::filesystem::exists(radiancePath) && std::filesystem::exists(skyboxPath) && std::filesystem::exists(prefilteredPath)) {
        if (m_currentEnvironmentTexture != nullptr) { // unset environment texture so render graph doesn't try to rebuld resources
            MarkForDelete(m_currentEnvironmentTexture);
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
            auto skyHDR = loadTextureFromFile(envpath.string());
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
		MarkForDelete(m_currentEnvironmentTexture);
	}
    m_currentEnvironmentTexture = texture;
    m_currentEnvironmentTexture->SetName(L"EnvironmentTexture");
    auto buffer = m_currentEnvironmentTexture->GetBuffer();
    // Create blank texture for skybox
	uint16_t skyboxResolution = getSkyboxResolution();

	TextureDescription skyboxDesc;
	skyboxDesc.width = skyboxResolution;
	skyboxDesc.height = skyboxResolution;
	skyboxDesc.channels = buffer->GetChannels();
	skyboxDesc.isCubemap = true;
    skyboxDesc.hasRTV = true;
	skyboxDesc.format = DXGI_FORMAT_R8G8B8A8_UNORM;

    auto envCubemap = PixelBuffer::Create(skyboxDesc);
    auto sampler = Sampler::GetDefaultSampler();
    auto skybox = std::make_shared<Texture>(envCubemap, sampler);
	SetSkybox(skybox);

	TextureDescription irradianceDesc;
    irradianceDesc.width = skyboxResolution;
	irradianceDesc.height = skyboxResolution;
	irradianceDesc.channels = buffer->GetChannels();
	irradianceDesc.isCubemap = true;
	irradianceDesc.hasRTV = true;
	irradianceDesc.format = DXGI_FORMAT_R8G8B8A8_UNORM;

    auto envRadianceCubemap = PixelBuffer::Create(irradianceDesc);
    auto irradiance = std::make_shared<Texture>(envRadianceCubemap, sampler);
	SetIrradiance(irradiance);

	TextureDescription prefilteredDesc;
	prefilteredDesc.width = skyboxResolution;
	prefilteredDesc.height = skyboxResolution;
	prefilteredDesc.channels = buffer->GetChannels();
	prefilteredDesc.isCubemap = true;
	prefilteredDesc.hasRTV = true;
	prefilteredDesc.format = DXGI_FORMAT_R8G8B8A8_UNORM;
	prefilteredDesc.generateMipMaps = true;

	auto prefilteredEnvironmentCubemap = PixelBuffer::Create(prefilteredDesc);
	auto prefilteredEnvironment = std::make_shared<Texture>(prefilteredEnvironmentCubemap, sampler);
	SetPrefilteredEnvironment(prefilteredEnvironment);

    if (currentRenderGraph != nullptr) {
        auto environmentPass = currentRenderGraph->GetPassByName("EnvironmentConversionPass");
        if (environmentPass != nullptr) {
            environmentPass->Invalidate();
        }
		auto environmentFilterPass = currentRenderGraph->GetPassByName("EnvironmentFilterPass");
        if (environmentFilterPass != nullptr) {
            environmentFilterPass->Invalidate();
        }
    }
    rebuildRenderGraph = true;
}

void DX12Renderer::SetSkybox(std::shared_ptr<Texture> texture) {
    if (m_currentSkybox != nullptr) { // Don't delete resources mid-frame
        MarkForDelete(m_currentSkybox);
    }
    m_currentSkybox = texture;
    m_currentSkybox->SetName(L"Skybox");
    rebuildRenderGraph = true;
}

void DX12Renderer::SetIrradiance(std::shared_ptr<Texture> texture) {
    if (m_environmentIrradiance != nullptr) { // Don't delete resources mid-frame
        MarkForDelete(m_environmentIrradiance);
    }
	m_environmentIrradiance = texture;
    m_environmentIrradiance->SetName(L"EnvironmentRadiance");
    auto& manager = ResourceManager::GetInstance();
	manager.setEnvironmentIrradianceMapIndex(m_environmentIrradiance->GetSRVDescriptorIndex());
	manager.setEnvironmentIrradianceMapSamplerIndex(m_environmentIrradiance->GetSamplerDescriptorIndex());
	rebuildRenderGraph = true;
}

void DX12Renderer::SetPrefilteredEnvironment(std::shared_ptr<Texture> texture) {
	if (m_prefilteredEnvironment != nullptr) { // Don't delete resources mid-frame
		MarkForDelete(m_prefilteredEnvironment);
	}
	m_prefilteredEnvironment = texture;
	m_prefilteredEnvironment->SetName(L"PrefilteredEnvironment");
	auto& manager = ResourceManager::GetInstance();
	manager.setPrefilteredEnvironmentMapIndex(m_prefilteredEnvironment->GetSRVDescriptorIndex());
	manager.setPrefilteredEnvironmentMapSamplerIndex(m_prefilteredEnvironment->GetSamplerDescriptorIndex());
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

std::shared_ptr<SceneNode> DX12Renderer::AppendScene(Scene& scene) {
	return currentScene->AppendScene(scene);
}