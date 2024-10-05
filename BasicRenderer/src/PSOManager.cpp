#include "PSOManager.h"

#include <fstream>

#include "DirectX/d3dx12.h"
#include "Utilities.h"
#include "DeviceManager.h"

#pragma comment(lib, "dxcompiler.lib")
//#pragma comment(lib, "dxil.lib")

void PSOManager::initialize() {
    createRootSignature();

    HMODULE dxcompiler = LoadLibrary(L"dxcompiler.dll");
    if (!dxcompiler)
    {

        throw std::runtime_error("Failed to load dxcompiler.dll");
    }

    // Get DxcCreateInstance function
    auto DxcCreateInstance = reinterpret_cast<DxcCreateInstanceProc>(
        GetProcAddress(dxcompiler, "DxcCreateInstance"));
    if (!DxcCreateInstance)
    {
        throw std::runtime_error("Failed to get DxcCreateInstance function");
    }
    // Create compiler and library instances
    DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(pUtils.GetAddressOf()));
    DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(pCompiler.GetAddressOf()));

    CreateDebugRootSignature();
    CreateDebugPSO();
    CreateSkyboxRootSignature();
    CreateSkyboxPSO();
    CreateEnvironmentConversionRootSignature();
	CreateEnvironmentConversionPSO();
}

void PSOManager::CreateDebugRootSignature() {
    CD3DX12_DESCRIPTOR_RANGE1 debugDescriptorRangeSRV;
    debugDescriptorRangeSRV.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0); // t0 in the shader

    CD3DX12_ROOT_PARAMETER1 debugRootParameters[2];
    debugRootParameters[0].InitAsDescriptorTable(1, &debugDescriptorRangeSRV, D3D12_SHADER_VISIBILITY_PIXEL); // Pixel shader will use the SRV
	debugRootParameters[1].InitAsConstants(16, 0, 0, D3D12_SHADER_VISIBILITY_VERTEX); // Vertex shader will use the constant buffer (b0

    D3D12_STATIC_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplerDesc.MipLODBias = 0.0f;
    samplerDesc.MaxAnisotropy = 1;
    samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    samplerDesc.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    samplerDesc.MinLOD = 0.0f;
    samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
    samplerDesc.ShaderRegister = 0;  // Corresponds to s0 in the shader
    samplerDesc.RegisterSpace = 0;
    samplerDesc.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
    rootSignatureDesc.Init_1_1(_countof(debugRootParameters), debugRootParameters, 1, &samplerDesc, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> serializedRootSig;
    ComPtr<ID3DBlob> errorBlob;
    HRESULT hr = D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1_1, &serializedRootSig, &errorBlob);
    if (FAILED(hr)) {
        if (errorBlob) {
            OutputDebugStringA((char*)errorBlob->GetBufferPointer());
        }
        throw std::runtime_error("Failed to serialize root signature");
    }

    auto& device = DeviceManager::GetInstance().GetDevice();
    hr = device->CreateRootSignature(0, serializedRootSig->GetBufferPointer(), serializedRootSig->GetBufferSize(), IID_PPV_ARGS(&debugRootSignature));
    if (FAILED(hr)) {
        throw std::runtime_error("Failed to create root signature");
    }
}

void PSOManager::CreateSkyboxRootSignature() {

    CD3DX12_ROOT_PARAMETER1 skyboxRootParameters[3];

    skyboxRootParameters[0].InitAsConstants(16, 1, 0, D3D12_SHADER_VISIBILITY_VERTEX); // modified view matrix
    skyboxRootParameters[1].InitAsConstants(1, 2, 0, D3D12_SHADER_VISIBILITY_PIXEL); // Skybox texture index
    skyboxRootParameters[2].InitAsConstants(1, 3, 0, D3D12_SHADER_VISIBILITY_PIXEL); // Skybox sampler index

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
    rootSignatureDesc.Init_1_1(_countof(skyboxRootParameters), skyboxRootParameters, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT | D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED | D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED);

    ComPtr<ID3DBlob> serializedRootSig;
    ComPtr<ID3DBlob> errorBlob;
    HRESULT hr = D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1_1, &serializedRootSig, &errorBlob);
    if (FAILED(hr)) {
        if (errorBlob) {
            OutputDebugStringA((char*)errorBlob->GetBufferPointer());
        }
        throw std::runtime_error("Failed to serialize root signature");
    }

    auto& device = DeviceManager::GetInstance().GetDevice();
    hr = device->CreateRootSignature(0, serializedRootSig->GetBufferPointer(), serializedRootSig->GetBufferSize(), IID_PPV_ARGS(&skyboxRootSignature));
    if (FAILED(hr)) {
        throw std::runtime_error("Failed to create root signature");
    }
}

void PSOManager::CreateEnvironmentConversionRootSignature() {
    CD3DX12_DESCRIPTOR_RANGE1 environmentDescriptorRangeSRV;
    environmentDescriptorRangeSRV.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0); // t0 in the shader

    CD3DX12_ROOT_PARAMETER1 environmentRootParameters[5];
    environmentRootParameters[0].InitAsDescriptorTable(1, &environmentDescriptorRangeSRV, D3D12_SHADER_VISIBILITY_PIXEL); // Pixel shader will use the SRV
    environmentRootParameters[1].InitAsConstants(16, 1, 0, D3D12_SHADER_VISIBILITY_VERTEX); // Vertex shader will use the constant buffer (b1)
	environmentRootParameters[2].InitAsConstants(1, 2, 0, D3D12_SHADER_VISIBILITY_PIXEL); // Integration range start
	environmentRootParameters[3].InitAsConstants(1, 3, 0, D3D12_SHADER_VISIBILITY_PIXEL); // Integration range end
    environmentRootParameters[4].InitAsConstants(1, 4, 0, D3D12_SHADER_VISIBILITY_PIXEL); // Normalization factor

    D3D12_STATIC_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplerDesc.MipLODBias = 0.0f;
    samplerDesc.MaxAnisotropy = 1;
    samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    samplerDesc.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    samplerDesc.MinLOD = 0.0f;
    samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
    samplerDesc.ShaderRegister = 0;  // Corresponds to s0 in the shader
    samplerDesc.RegisterSpace = 0;
    samplerDesc.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
    rootSignatureDesc.Init_1_1(_countof(environmentRootParameters), environmentRootParameters, 1, &samplerDesc, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> serializedRootSig;
    ComPtr<ID3DBlob> errorBlob;
    HRESULT hr = D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1_1, &serializedRootSig, &errorBlob);
    if (FAILED(hr)) {
        if (errorBlob) {
            OutputDebugStringA((char*)errorBlob->GetBufferPointer());
        }
        throw std::runtime_error("Failed to serialize root signature");
    }

    auto& device = DeviceManager::GetInstance().GetDevice();
    hr = device->CreateRootSignature(0, serializedRootSig->GetBufferPointer(), serializedRootSig->GetBufferSize(), IID_PPV_ARGS(&environmentConversionRootSignature));
    if (FAILED(hr)) {
        throw std::runtime_error("Failed to create root signature");
    }
}

void PSOManager::CreateDebugPSO() {
    // Compile shaders
    Microsoft::WRL::ComPtr<ID3DBlob> vertexShader;
    Microsoft::WRL::ComPtr<ID3DBlob> pixelShader;
    CompileShader(L"shaders/debug.hlsl", L"VSMain", L"vs_6_6", {}, vertexShader);
    CompileShader(L"shaders/debug.hlsl", L"PSMain", L"ps_6_6", {}, pixelShader);

    static D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
    { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    D3D12_INPUT_LAYOUT_DESC inputLayoutDesc = {};
    inputLayoutDesc.pInputElementDescs = inputElementDescs;
    inputLayoutDesc.NumElements = _countof(inputElementDescs);

    D3D12_RASTERIZER_DESC rasterizerDesc = {};
    rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;
    rasterizerDesc.CullMode = D3D12_CULL_MODE_NONE; // No culling for full-screen triangle
    rasterizerDesc.FrontCounterClockwise = FALSE;
    rasterizerDesc.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    rasterizerDesc.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    rasterizerDesc.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    rasterizerDesc.DepthClipEnable = TRUE;
    rasterizerDesc.MultisampleEnable = FALSE;
    rasterizerDesc.AntialiasedLineEnable = FALSE;
    rasterizerDesc.ForcedSampleCount = 0;
    rasterizerDesc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

    D3D12_BLEND_DESC blendDesc = {};
    blendDesc.RenderTarget[0].BlendEnable = FALSE;
    blendDesc.RenderTarget[0].LogicOpEnable = FALSE;
    blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
    blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    D3D12_DEPTH_STENCIL_DESC depthStencilDesc = {};
    depthStencilDesc.DepthEnable = FALSE;
    depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    depthStencilDesc.StencilEnable = FALSE;
    depthStencilDesc.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
    depthStencilDesc.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
    depthStencilDesc.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
    depthStencilDesc.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    depthStencilDesc.BackFace = depthStencilDesc.FrontFace;

    DXGI_FORMAT renderTargetFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = inputLayoutDesc;   // No input layout needed for full-screen triangle
    psoDesc.pRootSignature = debugRootSignature.Get();
    psoDesc.VS = { vertexShader->GetBufferPointer(), vertexShader->GetBufferSize() };
    psoDesc.PS = { pixelShader->GetBufferPointer(), pixelShader->GetBufferSize() };
    psoDesc.RasterizerState = rasterizerDesc;
    psoDesc.BlendState = blendDesc;
    psoDesc.DepthStencilState = depthStencilDesc;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = renderTargetFormat;
    psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleDesc.Quality = 0;
    psoDesc.InputLayout = inputLayoutDesc;

    auto& device = DeviceManager::GetInstance().GetDevice();
    auto hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&debugPSO));
    if (FAILED(hr)) {
        throw std::runtime_error("Failed to create debug PSO");
    }
}

void PSOManager::CreateSkyboxPSO() {
    // Compile shaders
    Microsoft::WRL::ComPtr<ID3DBlob> vertexShader;
    Microsoft::WRL::ComPtr<ID3DBlob> pixelShader;
    CompileShader(L"shaders/skybox.hlsl", L"VSMain", L"vs_6_6", {}, vertexShader);
    CompileShader(L"shaders/skybox.hlsl", L"PSMain", L"ps_6_6", {}, pixelShader);

    static D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_INPUT_LAYOUT_DESC inputLayoutDesc = {};
    inputLayoutDesc.pInputElementDescs = inputElementDescs;
    inputLayoutDesc.NumElements = _countof(inputElementDescs);

    D3D12_RASTERIZER_DESC rasterizerDesc = {};
    rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;
    rasterizerDesc.CullMode = D3D12_CULL_MODE_NONE; // No culling for full-screen triangle
    rasterizerDesc.FrontCounterClockwise = FALSE;
    rasterizerDesc.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    rasterizerDesc.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    rasterizerDesc.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    rasterizerDesc.DepthClipEnable = TRUE;
    rasterizerDesc.MultisampleEnable = FALSE;
    rasterizerDesc.AntialiasedLineEnable = FALSE;
    rasterizerDesc.ForcedSampleCount = 0;
    rasterizerDesc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

    D3D12_BLEND_DESC blendDesc = {};
    blendDesc.RenderTarget[0].BlendEnable = FALSE;
    blendDesc.RenderTarget[0].LogicOpEnable = FALSE;
    blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
    blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    D3D12_DEPTH_STENCIL_DESC depthStencilDesc = {};
    depthStencilDesc.DepthEnable = TRUE;
    depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO; // Prevent skybox from writing to the depth buffer
    depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS;

    DXGI_FORMAT renderTargetFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = inputLayoutDesc;   // No input layout needed for full-screen triangle
    psoDesc.pRootSignature = skyboxRootSignature.Get();
    psoDesc.VS = { vertexShader->GetBufferPointer(), vertexShader->GetBufferSize() };
    psoDesc.PS = { pixelShader->GetBufferPointer(), pixelShader->GetBufferSize() };
    psoDesc.RasterizerState = rasterizerDesc;
    psoDesc.BlendState = blendDesc;
    psoDesc.DepthStencilState = depthStencilDesc;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = renderTargetFormat;
    psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleDesc.Quality = 0;
    psoDesc.InputLayout = inputLayoutDesc;

    auto& device = DeviceManager::GetInstance().GetDevice();
    auto hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&skyboxPSO));
    if (FAILED(hr)) {
        throw std::runtime_error("Failed to create skybox PSO");
    }
}

void PSOManager::CreateEnvironmentConversionPSO() {
    // Compile shaders
    Microsoft::WRL::ComPtr<ID3DBlob> vertexShader;
    Microsoft::WRL::ComPtr<ID3DBlob> pixelShader;
    CompileShader(L"shaders/envToCubemap.hlsl", L"VSMain", L"vs_6_6", {}, vertexShader);
    CompileShader(L"shaders/envToCubemap.hlsl", L"PSMain", L"ps_6_6", {}, pixelShader);

    static D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_INPUT_LAYOUT_DESC inputLayoutDesc = {};
    inputLayoutDesc.pInputElementDescs = inputElementDescs;
    inputLayoutDesc.NumElements = _countof(inputElementDescs);

    D3D12_RASTERIZER_DESC rasterizerDesc = {};
    rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;
    rasterizerDesc.CullMode = D3D12_CULL_MODE_NONE; // No culling for full-screen triangle
    rasterizerDesc.FrontCounterClockwise = FALSE;
    rasterizerDesc.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    rasterizerDesc.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    rasterizerDesc.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    rasterizerDesc.DepthClipEnable = TRUE;
    rasterizerDesc.MultisampleEnable = FALSE;
    rasterizerDesc.AntialiasedLineEnable = FALSE;
    rasterizerDesc.ForcedSampleCount = 0;
    rasterizerDesc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

    D3D12_BLEND_DESC blendDesc = {};
	blendDesc.IndependentBlendEnable = TRUE;
    blendDesc.RenderTarget[0].BlendEnable = FALSE;
    blendDesc.RenderTarget[0].LogicOpEnable = FALSE;
    blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
    blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    blendDesc.RenderTarget[1].BlendEnable = TRUE;
    blendDesc.RenderTarget[1].SrcBlend = D3D12_BLEND_ONE;
    blendDesc.RenderTarget[1].DestBlend = D3D12_BLEND_ONE;
    blendDesc.RenderTarget[1].BlendOp = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[1].SrcBlendAlpha = D3D12_BLEND_ONE;
    blendDesc.RenderTarget[1].DestBlendAlpha = D3D12_BLEND_ZERO;
    blendDesc.RenderTarget[1].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[1].LogicOpEnable = FALSE;
    blendDesc.RenderTarget[1].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    D3D12_DEPTH_STENCIL_DESC depthStencilDesc = {};
    depthStencilDesc.DepthEnable = FALSE; 
    depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS;

    DXGI_FORMAT renderTargetFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = inputLayoutDesc;   // No input layout needed for full-screen triangle
    psoDesc.pRootSignature = environmentConversionRootSignature.Get();
    psoDesc.VS = { vertexShader->GetBufferPointer(), vertexShader->GetBufferSize() };
    psoDesc.PS = { pixelShader->GetBufferPointer(), pixelShader->GetBufferSize() };
    psoDesc.RasterizerState = rasterizerDesc;
    psoDesc.BlendState = blendDesc;
    psoDesc.DepthStencilState = depthStencilDesc;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 2;
    psoDesc.RTVFormats[0] = renderTargetFormat;
	psoDesc.RTVFormats[1] = renderTargetFormat;
    //psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleDesc.Quality = 0;
    psoDesc.InputLayout = inputLayoutDesc;

    auto& device = DeviceManager::GetInstance().GetDevice();
    auto hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&environmentConversionPSO));
    if (FAILED(hr)) {
        throw std::runtime_error("Failed to create skybox PSO");
    }
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> PSOManager::GetSkyboxPSO() {
	return skyboxPSO;
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> PSOManager::GetDebugPSO() {
	return debugPSO;
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> PSOManager::GetEnvironmentConversionPSO() {
    return environmentConversionPSO;
}

Microsoft::WRL::ComPtr<ID3D12RootSignature> PSOManager::GetEnvironmentConversionRootSignature() {
	return environmentConversionRootSignature;
}

Microsoft::WRL::ComPtr<ID3D12RootSignature> PSOManager::GetDebugRootSignature() {
	return debugRootSignature;
}

Microsoft::WRL::ComPtr<ID3D12RootSignature> PSOManager::GetSkyboxRootSignature() {
	return skyboxRootSignature;
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> PSOManager::GetPSO(UINT psoFlags, BlendState blendState, bool wireframe) {
    PSOKey key(psoFlags, blendState, wireframe);
    if (m_psoCache.find(key) == m_psoCache.end()) {
        m_psoCache[key] = CreatePSO(psoFlags, blendState, wireframe);
    }
    return m_psoCache[key];
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> PSOManager::CreatePSO(UINT psoFlags, BlendState blendState, bool wireframe) {
    // Define shader macros
    auto defines = GetShaderDefines(psoFlags);

    // Compile shaders
    Microsoft::WRL::ComPtr<ID3DBlob> vertexShader;
    Microsoft::WRL::ComPtr<ID3DBlob> pixelShader;
    CompileShader(L"shaders/shaders.hlsl", L"VSMain", L"vs_6_6", defines, vertexShader);
    CompileShader(L"shaders/shaders.hlsl", L"PSMain", L"ps_6_6", defines, pixelShader);

    // Define the vertex input layout
    UINT byte = 0;
    std::vector<D3D12_INPUT_ELEMENT_DESC> inputElementDescs = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };
    byte += 24;
    if (psoFlags & PSOFlags::VERTEX_COLORS) {
        inputElementDescs.push_back({ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, byte, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 });
        byte += 16;
    } // TODO: Vertex colors are not yet supported with more complex vertex types
    else {
        if (psoFlags & PSOFlags::BASE_COLOR_TEXTURE || psoFlags & PSOFlags::NORMAL_MAP || psoFlags & PSOFlags::AO_TEXTURE || psoFlags & PSOFlags::EMISSIVE_TEXTURE) {
            inputElementDescs.push_back({ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, byte, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 });
            byte += 8;
        }
        if (psoFlags & PSOFlags::NORMAL_MAP) {
            inputElementDescs.push_back({ "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, byte, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 });
            byte += 12;
            inputElementDescs.push_back({ "BINORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, byte, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 });
            byte += 12;
        }
        if (psoFlags & PSOFlags::SKINNED) {
            inputElementDescs.push_back({ "TEXCOORD", 1, DXGI_FORMAT_R32G32B32A32_UINT, 0, byte, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 });
            byte += 16;
            inputElementDescs.push_back({ "TEXCOORD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, byte, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 });
            byte += 16;
        }
    }
    // Create the pipeline state object
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { inputElementDescs.data(), static_cast<unsigned int>(inputElementDescs.size())};
    psoDesc.pRootSignature = rootSignature.Get();
    psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader.Get());
    if (!(psoFlags & PSOFlags::SHADOW)) {
        psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader.Get());
    }
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    if (wireframe) {
        psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
    }
    psoDesc.RasterizerState.FrontCounterClockwise = true;
    if (psoFlags & PSOFlags::DOUBLE_SIDED) {
        psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    }
    psoDesc.BlendState = GetBlendDesc(blendState);
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
	if (psoFlags & PSOFlags::SHADOW) {
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R32_FLOAT;
	}
	else {
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	}

    psoDesc.SampleDesc.Count = 1;
    psoDesc.DSVFormat = psoFlags & PSOFlags::SHADOW ? DXGI_FORMAT_D32_FLOAT : DXGI_FORMAT_D24_UNORM_S8_UINT;

    Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
    auto& device = DeviceManager::GetInstance().GetDevice();
    ThrowIfFailed(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso)));

    return pso;
}

std::vector<DxcDefine> PSOManager::GetShaderDefines(UINT psoFlags) {
    std::vector<DxcDefine> defines = {};
    if (psoFlags & PSOFlags::VERTEX_COLORS) {
        DxcDefine macro;
        macro.Value = L"1";
        macro.Name = L"VERTEX_COLORS";
        defines.insert(defines.begin(), macro);
    }
    if (psoFlags & PSOFlags::BASE_COLOR_TEXTURE || psoFlags & PSOFlags::NORMAL_MAP || psoFlags & PSOFlags::AO_TEXTURE || psoFlags & PSOFlags::EMISSIVE_TEXTURE) {
        DxcDefine macro;
        macro.Value = L"1";
        macro.Name = L"TEXTURED";
        defines.insert(defines.begin(), macro);
    }
    if (psoFlags & PSOFlags::BASE_COLOR_TEXTURE) {
        DxcDefine macro;
        macro.Value = L"1";
        macro.Name = L"BASE_COLOR_TEXTURE";
        defines.insert(defines.begin(), macro);
    }
    if (psoFlags & PSOFlags::NORMAL_MAP) {
        DxcDefine macro;
        macro.Value = L"1";
        macro.Name = L"NORMAL_MAP";
        defines.insert(defines.begin(), macro);
    }
    if (psoFlags & PSOFlags::AO_TEXTURE) {
        DxcDefine macro;
        macro.Value = L"1";
        macro.Name = L"AO_TEXTURE";
        defines.insert(defines.begin(), macro);
    }
    if (psoFlags & PSOFlags::EMISSIVE_TEXTURE) {
        DxcDefine macro;
        macro.Value = L"1";
        macro.Name = L"EMISSIVE_TEXTURE";
        defines.insert(defines.begin(), macro);
    }
    if (psoFlags & PSOFlags::PBR) {
        DxcDefine macro;
        macro.Value = L"1";
        macro.Name = L"PBR";
        defines.insert(defines.begin(), macro);
    }
    if (psoFlags & PSOFlags::PBR_MAPS) {
        DxcDefine macro;
        macro.Value = L"1";
        macro.Name = L"PBR_MAPS";
        defines.insert(defines.begin(), macro);
    }
    if (psoFlags & PSOFlags::SKINNED) {
        DxcDefine macro;
        macro.Value = L"1";
        macro.Name = L"SKINNED";
        defines.insert(defines.begin(), macro);
    }
    if (psoFlags & PSOFlags::DOUBLE_SIDED) {
        DxcDefine macro;
        macro.Value = L"1";
        macro.Name = L"DOUBLE_SIDED";
        defines.insert(defines.begin(), macro);
    }
    if (psoFlags & PSOFlags::PARALLAX) {
        DxcDefine macro;
        macro.Value = L"1";
        macro.Name = L"PARALLAX";
        defines.insert(defines.begin(), macro);
    }
    if (psoFlags & PSOFlags::SHADOW) {
        DxcDefine macro;
        macro.Value = L"1";
        macro.Name = L"SHADOW";
        defines.insert(defines.begin(), macro);
    }
	if (psoFlags & PSOFlags::IMAGE_BASED_LIGHTING) {
		DxcDefine macro;
		macro.Value = L"1";
		macro.Name = L"IMAGE_BASED_LIGHTING";
		defines.insert(defines.begin(), macro);
	}
    return defines;
}

void PSOManager::CompileShader(const std::wstring& filename, const std::wstring& entryPoint, const std::wstring& target, std::vector<DxcDefine> defines, Microsoft::WRL::ComPtr<ID3DBlob>& shaderBlob) {
    ComPtr<IDxcBlobEncoding> sourceBlob;
    ComPtr<IDxcResult> result;
    ComPtr<IDxcIncludeHandler> includeHandler;

    UINT32 codePage = CP_UTF8;
    pUtils->LoadFile(filename.c_str(), &codePage, &sourceBlob);
    //library->CreateIncludeHandler(&includeHandler);

    DxcBuffer sourceBuffer;
    sourceBuffer.Ptr = sourceBlob->GetBufferPointer();
    sourceBuffer.Size = sourceBlob->GetBufferSize();
    sourceBuffer.Encoding = 0;

    std::vector<LPCWSTR> arguments;
    // -E for the entry point (eg. 'main')
    arguments.push_back(L"-E");
    arguments.push_back(entryPoint.c_str());

    // -T for the target profile (eg. 'ps_6_6')
    arguments.push_back(L"-T");
    arguments.push_back(target.c_str());

    arguments.push_back(DXC_ARG_WARNINGS_ARE_ERRORS); //-WX
    arguments.push_back(DXC_ARG_DEBUG); //-Zi
    arguments.push_back(DXC_ARG_DEBUG_NAME_FOR_SOURCE); //-Zss
    arguments.push_back(DXC_ARG_SKIP_OPTIMIZATIONS);

    for (const auto& define : defines)
    {
        arguments.push_back(L"-D");
        arguments.push_back(define.Name);
    }

    print("Compiling with arguments: ");
    for (auto& arg : arguments) {
        std::wcout << arg << " ";
    }
    std::wcout << std::endl;

    // Compile the shader
    HRESULT hr = pCompiler->Compile(
        &sourceBuffer,
        arguments.data(),
        arguments.size(),
        nullptr,
        IID_PPV_ARGS(result.GetAddressOf()));

    if (FAILED(hr)) {
        ComPtr<IDxcBlobUtf8> pErrors;
        result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(pErrors.GetAddressOf()), nullptr);
        if (pErrors && pErrors->GetStringLength() > 0) {
            spdlog::error("Shader compilation failed: {}", pErrors->GetStringPointer());
        }
        ThrowIfFailed(hr);
        return;
    }

    // Check for errors in the result
    ComPtr<IDxcBlobUtf8> pErrors;
    result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(pErrors.GetAddressOf()), nullptr);
    if (pErrors && pErrors->GetStringLength() > 0) {
        spdlog::error("Shader compilation warnings/errors: {}", pErrors->GetStringPointer());

        if (strstr(pErrors->GetStringPointer(), "error") != nullptr) {
            // If errors exist, treat this as a failure
            return;
        }
    }

    //PDB data
    ComPtr<IDxcBlob> pDebugData;
    ComPtr<IDxcBlobUtf16> pDebugDataPath;
    result->GetOutput(DXC_OUT_PDB, IID_PPV_ARGS(pDebugData.GetAddressOf()), pDebugDataPath.GetAddressOf());
    // Get the debug data path
    const wchar_t* debugDataPath = reinterpret_cast<const wchar_t*>(pDebugDataPath->GetStringPointer());
    std::wcout << "Suggested pdb path:" << debugDataPath << std::endl;

    // Get the debug data buffer
    const void* debugDataBuffer = pDebugData->GetBufferPointer();
    size_t debugDataSize = pDebugData->GetBufferSize();

    // Open a file stream to write the debug data
    std::ofstream file(debugDataPath, std::ios::binary);
    if (!file.is_open()) {
        // Error
        return;
    }

    file.write(reinterpret_cast<const char*>(debugDataBuffer), debugDataSize);
    file.close();

    result->GetResult(reinterpret_cast<IDxcBlob**>(shaderBlob.GetAddressOf()));
}

void PSOManager::createRootSignature() {
    // Root parameters
    D3D12_ROOT_PARAMETER1 parameters[4] = {};

    // PerMesh buffer as a direct root CBV
    
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[0].Descriptor.ShaderRegister = 1; // b1 for PerObject
    parameters[0].Descriptor.RegisterSpace = 0;
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;


    // PerMesh buffer as a direct root CBV
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[1].Descriptor.ShaderRegister = 2; // b2 for PerMesh
    parameters[1].Descriptor.RegisterSpace = 0;
    parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // First integer root constant, used for shadow light ID
    parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[2].Constants.ShaderRegister = 3; // b3 for first integer root constant
    parameters[2].Constants.RegisterSpace = 0;
    parameters[2].Constants.Num32BitValues = 1; // Single integer
    parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // Second integer root constant, used for shadow light view offset
    parameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[3].Constants.ShaderRegister = 4; // b4 for second integer root constant
    parameters[3].Constants.RegisterSpace = 0;
    parameters[3].Constants.Num32BitValues = 1; // Single integer
    parameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // Root Signature Description
    D3D12_ROOT_SIGNATURE_DESC1 rootSignatureDesc = {};
    rootSignatureDesc.NumParameters = _countof(parameters);
    rootSignatureDesc.pParameters = parameters;
    rootSignatureDesc.NumStaticSamplers = 0;
    rootSignatureDesc.pStaticSamplers = nullptr;
    rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT| D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED | D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED;

    // Serialize and create the root signature
    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC versionedDesc;
    versionedDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    versionedDesc.Desc_1_1 = rootSignatureDesc;
    ThrowIfFailed(D3D12SerializeVersionedRootSignature(&versionedDesc, &signature, &error));
    auto& device = DeviceManager::GetInstance().GetDevice();
    ThrowIfFailed(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&rootSignature)));
}

ComPtr<ID3D12RootSignature> PSOManager::GetRootSignature() {
    return rootSignature;
}

void PSOManager::ReloadShaders() {
	m_psoCache.clear();
}

D3D12_BLEND_DESC PSOManager::GetBlendDesc(BlendState blendState) {
    switch (blendState) {
    case BlendState::BLEND_STATE_OPAQUE:{
        D3D12_BLEND_DESC opaqueBlendDesc = {};
        opaqueBlendDesc.AlphaToCoverageEnable = FALSE;
        opaqueBlendDesc.IndependentBlendEnable = FALSE;

        for (UINT i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i) {
            opaqueBlendDesc.RenderTarget[i].BlendEnable = FALSE;
            opaqueBlendDesc.RenderTarget[i].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        }
        return opaqueBlendDesc;
    }
    case BlendState::BLEND_STATE_MASK: {
        D3D12_BLEND_DESC maskBlendDesc = {};
        maskBlendDesc.AlphaToCoverageEnable = TRUE; // Enable Alpha-to-Coverage for multi-sampling anti-aliasing.
        maskBlendDesc.IndependentBlendEnable = FALSE;

        for (UINT i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i) {
            maskBlendDesc.RenderTarget[i].BlendEnable = FALSE; // No standard blending needed.
            maskBlendDesc.RenderTarget[i].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        }
        return maskBlendDesc;
    }
    case BlendState::BLEND_STATE_BLEND: {
        D3D12_BLEND_DESC blendBlendDesc = {};
        blendBlendDesc.AlphaToCoverageEnable = FALSE;
        blendBlendDesc.IndependentBlendEnable = FALSE;

        for (UINT i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i) {
            blendBlendDesc.RenderTarget[i].BlendEnable = TRUE;
            blendBlendDesc.RenderTarget[i].SrcBlend = D3D12_BLEND_SRC_ALPHA;
            blendBlendDesc.RenderTarget[i].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
            blendBlendDesc.RenderTarget[i].BlendOp = D3D12_BLEND_OP_ADD;
            blendBlendDesc.RenderTarget[i].SrcBlendAlpha = D3D12_BLEND_ONE;
            blendBlendDesc.RenderTarget[i].DestBlendAlpha = D3D12_BLEND_ZERO;
            blendBlendDesc.RenderTarget[i].BlendOpAlpha = D3D12_BLEND_OP_ADD;
            blendBlendDesc.RenderTarget[i].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        }
        return blendBlendDesc;
    }
    }
    spdlog::warn("Blend state not set, defaulting to opaque");
    D3D12_BLEND_DESC opaqueBlendDesc = {};
    opaqueBlendDesc.AlphaToCoverageEnable = FALSE;
    opaqueBlendDesc.IndependentBlendEnable = FALSE;

    for (UINT i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i) {
        opaqueBlendDesc.RenderTarget[i].BlendEnable = FALSE;
        opaqueBlendDesc.RenderTarget[i].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    }
    return opaqueBlendDesc;
}