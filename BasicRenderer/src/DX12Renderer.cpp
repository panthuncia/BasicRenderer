//
// Created by matth on 6/25/2024.
//

#include "DX12Renderer.h"

#include <d3dcompiler.h>

#include "Utilities.h"
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
    // Create root signature
    // Descriptor range for PerFrame buffer using a descriptor table
    D3D12_DESCRIPTOR_RANGE1 descRange = {};
    descRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    descRange.NumDescriptors = 1;
    descRange.BaseShaderRegister = 0; // b0 for PerFrame
    descRange.RegisterSpace = 0;
    descRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE1 srvDescRange = {};
    srvDescRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvDescRange.NumDescriptors = 1;
    srvDescRange.BaseShaderRegister = 2; // b2 for lights
    srvDescRange.RegisterSpace = 0;
    srvDescRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // Root parameter for descriptor table (PerFrame buffer)
    D3D12_ROOT_PARAMETER1 parameters[3] = {};

    // PerFrame buffer as a descriptor table
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[0].DescriptorTable.NumDescriptorRanges = 1;
    parameters[0].DescriptorTable.pDescriptorRanges = &descRange;
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // PerMesh buffer as a direct root CBV
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[1].Descriptor.ShaderRegister = 1; // b1 for PerMesh
    parameters[1].Descriptor.RegisterSpace = 0;
    parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[2].DescriptorTable.NumDescriptorRanges = 1;
    parameters[2].DescriptorTable.pDescriptorRanges = &srvDescRange;
    parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // Root Signature Description
    D3D12_ROOT_SIGNATURE_DESC1 rootSignatureDesc = {};
    rootSignatureDesc.NumParameters = 3; // three parameters: two descriptor tables and one direct CBV
    rootSignatureDesc.pParameters = parameters;
    rootSignatureDesc.NumStaticSamplers = 0;
    rootSignatureDesc.pStaticSamplers = nullptr;
    rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    // Serialize and create the root signature
    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC versionedDesc;
    versionedDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    versionedDesc.Desc_1_1 = rootSignatureDesc;
    ThrowIfFailed(D3D12SerializeVersionedRootSignature(&versionedDesc, &signature, &error));
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
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
    };


    D3D12_RASTERIZER_DESC rasterizerDesc = {};
    rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;
    rasterizerDesc.CullMode = D3D12_CULL_MODE_BACK; // Enable back-face culling
    rasterizerDesc.FrontCounterClockwise = TRUE; // Define the front face as counter-clockwise
    rasterizerDesc.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    rasterizerDesc.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    rasterizerDesc.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    rasterizerDesc.DepthClipEnable = TRUE;
    rasterizerDesc.MultisampleEnable = FALSE;
    rasterizerDesc.AntialiasedLineEnable = FALSE;
    rasterizerDesc.ForcedSampleCount = 0;
    rasterizerDesc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
    psoDesc.pRootSignature = rootSignature.Get();
    psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader.Get());
    psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader.Get());
    psoDesc.RasterizerState = rasterizerDesc;
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    for (int i = 1; i < 8; ++i) {
        psoDesc.RTVFormats[i] = DXGI_FORMAT_UNKNOWN;
    }
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleDesc.Quality = 0;
    psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

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
        {{-1.0f, -1.0f, -1.0f}, {-1.0f, -1.0f, -1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
        {{1.0f,  -1.0f, -1.0f}, {1.0f,  -1.0f, -1.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},
        {{ 1.0f,  1.0f, -1.0f}, {-1.0f, -1.0f, -1.0f}, {0.0f, 0.0f, 1.0f, 1.0f}},
        {{ -1.0f, 1.0f, -1.0f}, { 1.0f,  1.0f, -1.0f}, {1.0f, 1.0f, 0.0f, 1.0f}},
        {{-1.0f, -1.0f,  1.0f}, {-1.0f, -1.0f,  1.0f}, {0.0f, 1.0f, 1.0f, 1.0f}},
        {{1.0f,  -1.0f,  1.0f}, {1.0f,  -1.0f,  1.0f}, {1.0f, 0.0f, 1.0f, 1.0f}},
        {{ 1.0f,  1.0f,  1.0f}, { 1.0f,  1.0f,  1.0f}, {0.5f, 0.5f, 0.5f, 1.0f}},
        {{ -1.0f, 1.0f,  1.0f}, { -1.0f, 1.0f,  1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}},
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
    perMeshCBData.model = DirectX::XMMatrixMultiply(DirectX::XMMatrixRotationY(DirectX::XM_PIDIV4), DirectX::XMMatrixTranslation(0, 1, -2));
    perFrameCBData.view = DirectX::XMMatrixLookAtLH(
        DirectX::XMVectorSet(0.0f, 2.0f, -5.0f, 1.0f), // Eye position
        DirectX::XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f),  // Focus point
        DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f)   // Up direction
    );
    perFrameCBData.projection = DirectX::XMMatrixPerspectiveFovLH(
        DirectX::XM_PIDIV2, // Field of View
        800.0f / 600.0f,    // Aspect ratio
        0.1f,               // Near clipping plane
        100.0f              // Far clipping plane
    );
    memcpy(pPerFrameConstantBuffer, &perFrameCBData, sizeof(perFrameCBData));
    memcpy(pPerMeshConstantBuffer, &perMeshCBData, sizeof(perMeshCBData));
}

void DX12Renderer::CreateConstantBuffer() {

    // Create descriptor heap for perFrame buffer
    D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc = {};
    cbvHeapDesc.NumDescriptors = 2;
    cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(device->CreateDescriptorHeap(&cbvHeapDesc, IID_PPV_ARGS(&perFrameCBVHeap)));

    // Describe and create a constant buffer and constant buffer view (CBV)
    D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    UINT perFrameBufferSize = (sizeof(PerFrameCB) + 255) & ~255; // CBV size is required to be 256-byte aligned.
    D3D12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(perFrameBufferSize);

    ThrowIfFailed(device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&perFrameConstantBuffer)));

    // Map the constant buffer and initialize it
    D3D12_RANGE readRange(0, 0); // We do not intend to read from this resource on the CPU.
    ThrowIfFailed(perFrameConstantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pPerFrameConstantBuffer)));

    // Initialize the constant buffer data
    memcpy(pPerFrameConstantBuffer, &perFrameCBData, sizeof(perFrameCBData));

    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
    cbvDesc.BufferLocation = perFrameConstantBuffer->GetGPUVirtualAddress();
    cbvDesc.SizeInBytes = perFrameBufferSize; // CBV size is required to be 256-byte aligned.
    device->CreateConstantBufferView(&cbvDesc, perFrameCBVHeap->GetCPUDescriptorHandleForHeapStart());

    UINT constantBufferSize = sizeof(PerMeshCB);

    // Describe and create a constant buffer
    heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(constantBufferSize);

    ThrowIfFailed(device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&perMeshConstantBuffer)));

    // Map the constant buffer and initialize it
    //D3D12_RANGE readRange(0, 0); // We do not intend to read from this resource on the CPU.
    ThrowIfFailed(perMeshConstantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pPerMeshConstantBuffer)));

    // Initialize the constant buffer data
    memcpy(pPerMeshConstantBuffer, &perMeshCBData, sizeof(perMeshCBData));

    // Create structured buffer for lights
    LightInfo light;
    light.properties = DirectX::XMVectorSetInt(0, 0, 0, 0 );
    light.posWorldSpace = DirectX::XMVectorSet(1.0, 1.0, 1.0, 1.0);
    light.dirWorldSpace = DirectX::XMVectorSet(0.0, 0.0, 0.0, 0.0);
    light.attenuation = DirectX::XMVectorSet(1.0, 0.01, 0.0032, 10.0);
    light.color = DirectX::XMVectorSet(0.0, 1.0, 0.0, 1.0);
    lightsData.push_back(light);

    D3D12_RESOURCE_DESC structuredBufferDesc = {};
    structuredBufferDesc.Alignment = 0;
    structuredBufferDesc.DepthOrArraySize = 1;
    structuredBufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    structuredBufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
    structuredBufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    structuredBufferDesc.Height = 1;
    structuredBufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    structuredBufferDesc.MipLevels = 1;
    structuredBufferDesc.SampleDesc.Count = 1;
    structuredBufferDesc.SampleDesc.Quality = 0;
    structuredBufferDesc.Width = sizeof(LightInfo) * lightsData.size(); // Size of all lights

    //CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
    ThrowIfFailed(device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &structuredBufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&lightBuffer)));

    // Upload light data to the buffer
    void* mappedData;
    //CD3DX12_RANGE readRange(0, 0);
    ThrowIfFailed(lightBuffer->Map(0, &readRange, &mappedData));
    memcpy(mappedData, lightsData.data(), sizeof(LightInfo) * lightsData.size());
    lightBuffer->Unmap(0, nullptr);

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Buffer.FirstElement = 0;
    srvDesc.Buffer.NumElements = static_cast<UINT>(lightsData.size());
    srvDesc.Buffer.StructureByteStride = sizeof(LightInfo);
    srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

    D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = perFrameCBVHeap->GetCPUDescriptorHandleForHeapStart(); 
    srvHandle.ptr += device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV); // Move to the next slot
    device->CreateShaderResourceView(lightBuffer.Get(), &srvDesc, srvHandle);
}

void DX12Renderer::Update() {
    currentScene.Update();
}

void DX12Renderer::Render() {
    //Update buffers
    UpdateConstantBuffer();
    // Record all the commands we need to render the scene into the command list
    ThrowIfFailed(commandAllocator->Reset());
    ThrowIfFailed(commandList->Reset(commandAllocator.Get(), pipelineState.Get()));

    // Set necessary state
    commandList->SetGraphicsRootSignature(rootSignature.Get());
    // Bind the constant buffer to the root signature
    commandList->SetDescriptorHeaps(1, perFrameCBVHeap.GetAddressOf());

    commandList->SetGraphicsRootConstantBufferView(1, perMeshConstantBuffer->GetGPUVirtualAddress());

    commandList->SetGraphicsRootDescriptorTable(0, perFrameCBVHeap->GetGPUDescriptorHandleForHeapStart()); // Bind descriptor table for PerFrame

    D3D12_GPU_DESCRIPTOR_HANDLE gpuSrvHandle = perFrameCBVHeap->GetGPUDescriptorHandleForHeapStart();
    gpuSrvHandle.ptr += device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    commandList->SetGraphicsRootDescriptorTable(2, gpuSrvHandle);

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

    for (auto& pair : currentScene.getRenderableObjectIDMap()) {
        auto& renderable = pair.second;
        for (auto& mesh : renderable->getMeshes()) {
            D3D12_VERTEX_BUFFER_VIEW vertexBufferView = mesh.GetVertexBufferView();
            D3D12_INDEX_BUFFER_VIEW indexBufferView = mesh.GetIndexBufferView();

            // Pass the addresses of the local variables
            commandList->IASetVertexBuffers(0, 1, &vertexBufferView);
            commandList->IASetIndexBuffer(&indexBufferView);

            // Draw the cube
            commandList->DrawIndexedInstanced(cubeMesh->GetIndexCount(), 1, 0, 0, 0);
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

Scene& DX12Renderer::GetCurrentScene() {
    return currentScene;
}