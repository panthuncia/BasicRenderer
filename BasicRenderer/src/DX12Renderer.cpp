//
// Created by matth on 6/25/2024.
//

#include "DX12Renderer.h"

#include <d3dcompiler.h>

#include "utilities.h"
#include "DirectX/d3dx12.h"

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
}

void DX12Renderer::LoadAssets() {
    // Create root signature
    D3D12_ROOT_PARAMETER parameter;
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameter.Descriptor.ShaderRegister = 0; // corresponds to 'b0'
    parameter.Descriptor.RegisterSpace = 0; // default register space
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL; // or just VERTEX if only used in vertex shader

    D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc;
    rootSignatureDesc.NumParameters = 1;
    rootSignatureDesc.pParameters = &parameter;
    rootSignatureDesc.NumStaticSamplers = 0;
    rootSignatureDesc.pStaticSamplers = nullptr;
    rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    ThrowIfFailed(D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error));
    ThrowIfFailed(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&rootSignature)));

    // Create the pipeline state, which includes compiling and loading shaders
    ComPtr<ID3DBlob> vertexShader;
    ComPtr<ID3DBlob> pixelShader;

#if defined(_DEBUG)
    UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    UINT compileFlags = 0;
#endif

    HRESULT hr = D3DCompileFromFile(L"shaders/shaders.hlsl", nullptr, nullptr, "VSMain", "vs_5_0", compileFlags, 0, &vertexShader, &error);
    if (FAILED(hr)) {
        if (error) {
            std::string errMsg(static_cast<char*>(error->GetBufferPointer()), error->GetBufferSize());
            std::cerr << "Vertex shader compilation error: " << errMsg << std::endl;
        }
        ThrowIfFailed(hr);
    }

    hr = D3DCompileFromFile(L"shaders/shaders.hlsl", nullptr, nullptr, "PSMain", "ps_5_0", compileFlags, 0, &pixelShader, &error);
    if (FAILED(hr)) {
        if (error) {
            std::string errMsg(static_cast<char*>(error->GetBufferPointer()), error->GetBufferSize());
            std::cerr << "Pixel shader compilation error: " << errMsg << std::endl;
        }
        ThrowIfFailed(hr);
    }
    if (vertexShader->GetBufferSize() == 0 || pixelShader->GetBufferSize() == 0) {
        std::cerr << "Shader bytecode is empty" << std::endl;
        throw std::runtime_error("Shader bytecode is empty");
    }
    // Define the vertex input layout
    D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
    psoDesc.pRootSignature = rootSignature.Get();
    psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader.Get());
    psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader.Get());
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState.DepthEnable = FALSE;
    psoDesc.DepthStencilState.StencilEnable = FALSE;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    for (int i = 1; i < 8; ++i) {
        psoDesc.RTVFormats[i] = DXGI_FORMAT_UNKNOWN;
    }
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleDesc.Quality = 0;
    psoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN; // Use this if not using a depth-stencil view

    psoDesc.CachedPSO = { nullptr, 0 };
    psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

    // Optional shaders (if not used, set to {nullptr, 0})
    psoDesc.DS = { nullptr, 0 };
    psoDesc.HS = { nullptr, 0 };
    psoDesc.GS = { nullptr, 0 };
    psoDesc.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
    psoDesc.NodeMask = 0;

    HRESULT hr1 = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState));
    if (FAILED(hr)) {
        CheckDebugMessages();
        ThrowIfFailed(hr);
    }

    std::vector<Vertex> vertices = {
        {{-1.0f, -1.0f, -1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
        {{1.0f,  -1.0f, -1.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},
        {{ 1.0f,  1.0f, -1.0f}, {0.0f, 0.0f, 1.0f, 1.0f}},
        {{ -1.0f, 1.0f, -1.0f}, {1.0f, 1.0f, 0.0f, 1.0f}},
        {{-1.0f, -1.0f,  1.0f}, {0.0f, 1.0f, 1.0f, 1.0f}},
        {{1.0f,  -1.0f,  1.0f}, {1.0f, 0.0f, 1.0f, 1.0f}},
        {{ 1.0f,  1.0f,  1.0f}, {0.5f, 0.5f, 0.5f, 1.0f}},
        {{ -1.0f, 1.0f,  1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}},
    };

    std::vector<UINT16> indices = {
        0, 1, 3, 3, 1, 2,
        1, 5, 2, 2, 5, 6,
        5, 4, 6, 6, 4, 7,
        4, 0, 7, 7, 0, 3,
        3, 2, 7, 7, 2, 6,
        4, 5, 0, 0, 5, 1
    };

    cubeMesh = std::make_unique<Mesh>(device.Get(), vertices, indices);
}


void DX12Renderer::WaitForPreviousFrame() {
    // Signal and increment the fence value
    print("In wait for frame");
    const UINT64 fenceToWaitFor = fenceValue;
    ThrowIfFailed(commandQueue->Signal(fence.Get(), fenceToWaitFor));
    print("Command queue signal");
    fenceValue++;

    // Wait until the previous frame is finished
    if (fence->GetCompletedValue() < fenceToWaitFor) {
        ThrowIfFailed(fence->SetEventOnCompletion(fenceToWaitFor, fenceEvent));
        WaitForSingleObject(fenceEvent, INFINITE);
        print("Iwait for object");
    }

    frameIndex = swapChain->GetCurrentBackBufferIndex();
}

void DX12Renderer::UpdateConstantBuffer() {
    cbData.model = DirectX::XMMatrixTranslation(0, 1, -2);
    cbData.view = DirectX::XMMatrixLookAtLH(
        DirectX::XMVectorSet(0.0f, 2.0f, -5.0f, 1.0f), // Eye position
        DirectX::XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f),  // Focus point
        DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f)   // Up direction
    );
    cbData.projection = DirectX::XMMatrixPerspectiveFovLH(
        DirectX::XM_PIDIV2, // Field of View
        800.0f / 600.0f,    // Aspect ratio
        0.1f,               // Near clipping plane
        100.0f              // Far clipping plane
    );
    memcpy(pConstantBuffer, &cbData, sizeof(cbData));
}

void DX12Renderer::CreateConstantBuffer() {
    const UINT constantBufferSize = sizeof(ConstantBuffer);

    // Describe and create a constant buffer view (CBV)
    D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    D3D12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(constantBufferSize);

    ThrowIfFailed(device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&constantBuffer)));

    // Map the constant buffer and initialize it
    D3D12_RANGE readRange(0, 0); // We do not intend to read from this resource on the CPU.
    ThrowIfFailed(constantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pConstantBuffer)));

    // Initialize the constant buffer data
    memcpy(pConstantBuffer, &cbData, sizeof(cbData));
}

void DX12Renderer::Render() {
    //Update buffers
    UpdateConstantBuffer();
    // Record all the commands we need to render the scene into the command list
    ThrowIfFailed(commandAllocator->Reset());
    ThrowIfFailed(commandList->Reset(commandAllocator.Get(), pipelineState.Get()));

    // Set necessary state
    commandList->SetGraphicsRootSignature(rootSignature.Get());
    CD3DX12_VIEWPORT viewport = CD3DX12_VIEWPORT(0.0f, 0.0f, 800.0f, 600.0f);
    commandList->RSSetViewports(1, &viewport);
    CD3DX12_RECT rect = CD3DX12_RECT(0, 0, 800, 600);
    commandList->RSSetScissorRects(1, &rect);

    // Bind the constant buffer to the root signature
    commandList->SetGraphicsRootConstantBufferView(0, constantBuffer->GetGPUVirtualAddress());

    // Indicate that the back buffer will be used as a render target
    CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(renderTargets[frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    commandList->ResourceBarrier(1, &barrier);

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(rtvHeap->GetCPUDescriptorHandleForHeapStart(), frameIndex, rtvDescriptorSize);
    commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

    // Record commands
    const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
    commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView = cubeMesh->GetVertexBufferView();
    D3D12_INDEX_BUFFER_VIEW indexBufferView = cubeMesh->GetIndexBufferView();

    // Pass the addresses of the local variables
    commandList->IASetVertexBuffers(0, 1, &vertexBufferView);
    commandList->IASetIndexBuffer(&indexBufferView);

    // Draw the cube
    commandList->DrawIndexedInstanced(cubeMesh->GetIndexCount(), 1, 0, 0, 0);

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