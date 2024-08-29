//
// Created by matth on 6/25/2024.
//

#include "DX12Renderer.h"

#define _USE_MATH_DEFINES
#include <math.h>

#include "Utilities.h"
#include "DirectX/d3dx12.h"
#include "DeviceManager.h"
#include "PSOManager.h"
#include "ResourceManager.h"

void DX12Renderer::Initialize(HWND hwnd) {
    LoadPipeline(hwnd);
    LoadAssets();
    CreateConstantBuffer();
}

void DX12Renderer::LoadPipeline(HWND hwnd) {
     UINT dxgiFactoryFlags = 0;

#if defined(_DEBUG)
    ComPtr<ID3D12Debug> debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
        debugController->EnableDebugLayer();
        dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
    }
#endif

    // Create DXGI factory
    ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));

    // Create device
    ThrowIfFailed(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device)));

    // Initialize device manager
    DeviceManager::getInstance().initialize(device);
    PSOManager::getInstance().initialize();
    ResourceManager::GetInstance().Initialize();

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

    // Describe and create the swap chain
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.BufferCount = 2;
    swapChainDesc.Width = 800;
    swapChainDesc.Height = 600;
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

    // This sample does not support fullscreen transitions
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
    ThrowIfFailed(device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&dsvHeap))); // Add this line

    // Create frame resources
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(rtvHeap->GetCPUDescriptorHandleForHeapStart());

    for (UINT n = 0; n < 2; n++) {
        ThrowIfFailed(swapChain->GetBuffer(n, IID_PPV_ARGS(&renderTargets[n])));
        device->CreateRenderTargetView(renderTargets[n].Get(), nullptr, rtvHandle);
        rtvHandle.Offset(1, rtvDescriptorSize);
    }

    // Create command allocator
    ThrowIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator)));

    // **Create the command list**
    ThrowIfFailed(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator.Get(), nullptr, IID_PPV_ARGS(&commandList)));

    // **Close the command list**
    ThrowIfFailed(commandList->Close());

    // **Create the fence**
    ThrowIfFailed(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
    fenceValue = 1;

    // **Create an event handle for the fence**
    fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (fenceEvent == nullptr) {
        ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
    }

    // Create the depth stencil buffer
    D3D12_RESOURCE_DESC depthStencilDesc = CD3DX12_RESOURCE_DESC::Tex2D(
        DXGI_FORMAT_D24_UNORM_S8_UINT, 800, 600,
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

void DX12Renderer::LoadAssets() {
    
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

void DX12Renderer::UpdateConstantBuffer() {
    // moved
    auto camera = currentScene->GetCamera();
    ResourceManager::GetInstance().UpdateConstantBuffers(camera->transform.getGlobalPosition(), camera->GetViewMatrix(), camera->GetProjectionMatrix());
}

void DX12Renderer::CreateConstantBuffer() {

    // moved
}

void DX12Renderer::Update(double elapsedSeconds) {

    currentScene->GetCamera()->transform.applyMovement(movementState, elapsedSeconds);
    currentScene->GetCamera()->transform.rotatePitchYaw(verticalAngle, horizontalAngle);
    //spdlog::info("horizontal angle: {}", horizontalAngle);
    //spdlog::info("vertical angle: {}", verticalAngle);
    verticalAngle = 0;
    horizontalAngle = 0;

    currentScene->Update();
    UpdateConstantBuffer();
}

void DX12Renderer::Render() {
    //Update buffers
    // Record all the commands we need to render the scene into the command list
    ThrowIfFailed(commandAllocator->Reset());
    ThrowIfFailed(commandList->Reset(commandAllocator.Get(), NULL));

    // Set necessary state
    auto& psoManager = PSOManager::getInstance();
    commandList->SetGraphicsRootSignature(psoManager.GetRootSignature().Get());
    // Bind the constant buffer to the root signature
    ID3D12DescriptorHeap* descriptorHeaps[] = {
        ResourceManager::GetInstance().GetDescriptorHeap().Get(), // The texture descriptor heap
        ResourceManager::GetInstance().GetSamplerDescriptorHeap().Get()
    };

    commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

    //commandList->SetGraphicsRootDescriptorTable(0, ResourceManager::getInstance().getGPUHandle()); // Bind descriptor table

    // Set viewports and scissor rect
    CD3DX12_VIEWPORT viewport = CD3DX12_VIEWPORT(0.0f, 0.0f, 800.0f, 600.0f);
    commandList->RSSetViewports(1, &viewport);
    CD3DX12_RECT rect = CD3DX12_RECT(0, 0, 800, 600);
    commandList->RSSetScissorRects(1, &rect);

    // Indicate that the back buffer will be used as a render target
    CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(renderTargets[frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    commandList->ResourceBarrier(1, &barrier);

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(rtvHeap->GetCPUDescriptorHandleForHeapStart(), frameIndex, rtvDescriptorSize);
    CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(dsvHeap->GetCPUDescriptorHandleForHeapStart());
    commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

    // Record commands
    const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
    commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
    commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    for (auto& pair : currentScene->GetOpaqueRenderableObjectIDMap()) {
        auto& renderable = pair.second;
        commandList->SetGraphicsRootConstantBufferView(1, renderable->GetConstantBuffer()->GetGPUVirtualAddress());

        for (auto& mesh : renderable->GetOpaqueMeshes()) {
            commandList->SetGraphicsRootConstantBufferView(2, mesh.GetPerMeshBuffer()->GetGPUVirtualAddress());
            auto pso = psoManager.GetPSO(mesh.material->psoFlags, mesh.material->blendState);
            commandList->SetPipelineState(pso.Get());
            D3D12_VERTEX_BUFFER_VIEW vertexBufferView = mesh.GetVertexBufferView();
            D3D12_INDEX_BUFFER_VIEW indexBufferView = mesh.GetIndexBufferView();

            commandList->IASetVertexBuffers(0, 1, &vertexBufferView);
            commandList->IASetIndexBuffer(&indexBufferView);

            commandList->DrawIndexedInstanced(mesh.GetIndexCount(), 1, 0, 0, 0);
        }
    }

    for (auto& pair : currentScene->GetTransparentRenderableObjectIDMap()) {
        auto& renderable = pair.second;
        commandList->SetGraphicsRootConstantBufferView(1, renderable->GetConstantBuffer()->GetGPUVirtualAddress());

        for (auto& mesh : renderable->GetTransparentMeshes()) {
            commandList->SetGraphicsRootConstantBufferView(2, mesh.GetPerMeshBuffer()->GetGPUVirtualAddress());
            auto pso = psoManager.GetPSO(mesh.material->psoFlags, mesh.material->blendState);
            commandList->SetPipelineState(pso.Get());
            D3D12_VERTEX_BUFFER_VIEW vertexBufferView = mesh.GetVertexBufferView();
            D3D12_INDEX_BUFFER_VIEW indexBufferView = mesh.GetIndexBufferView();

            commandList->IASetVertexBuffers(0, 1, &vertexBufferView);
            commandList->IASetIndexBuffer(&indexBufferView);

            commandList->DrawIndexedInstanced(mesh.GetIndexCount(), 1, 0, 0, 0);
        }
    }

    // Indicate that the back buffer will now be used to present
    barrier = CD3DX12_RESOURCE_BARRIER::Transition(renderTargets[frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    commandList->ResourceBarrier(1, &barrier);

    ThrowIfFailed(commandList->Close());

    // Execute the command list
    ID3D12CommandList* ppCommandLists[] = { commandList.Get() };
    commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

    // Present the frame
    ThrowIfFailed(swapChain->Present(1, 0));

    WaitForPreviousFrame();
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
        movementState.forwardMagnitude = magnitude;
        });

    context.SetActionHandler(InputAction::MoveBackward, [this](float magnitude, const InputData& inputData) {
        //spdlog::info("Moving forward!");
        movementState.backwardMagnitude = magnitude;
        });

    context.SetActionHandler(InputAction::MoveRight, [this](float magnitude, const InputData& inputData) {
        //spdlog::info("Moving right!");
        movementState.rightMagnitude = magnitude;
        });

    context.SetActionHandler(InputAction::MoveLeft, [this](float magnitude, const InputData& inputData) {
        //spdlog::info("Moving right!");
        movementState.leftMagnitude = magnitude;
        });

    context.SetActionHandler(InputAction::MoveUp, [this](float magnitude, const InputData& inputData) {
        //spdlog::info("Moving up!");
        movementState.upMagnitude = magnitude;
        });

    context.SetActionHandler(InputAction::MoveDown, [this](float magnitude, const InputData& inputData) {
        //spdlog::info("Moving up!");
        movementState.downMagnitude = magnitude;
        });

    context.SetActionHandler(InputAction::RotateCamera, [this](float magnitude, const InputData& inputData) {
        horizontalAngle += inputData.mouseDeltaX * 0.005;
        verticalAngle += inputData.mouseDeltaY * 0.005;
        const float upperBound = M_PI / 2 - 0.01; // Slightly less than 90 degrees
        const float lowerBound = -M_PI / 2 + 0.01; // Slightly more than -90 degrees
        verticalAngle = max(lowerBound, min(upperBound, verticalAngle));
        // TODO
        });

    context.SetActionHandler(InputAction::ZoomIn, [this](float magnitude, const InputData& inputData) {
        // TODO
        });

    context.SetActionHandler(InputAction::ZoomOut, [this](float magnitude, const InputData& inputData) {
        // TODO
        });
}