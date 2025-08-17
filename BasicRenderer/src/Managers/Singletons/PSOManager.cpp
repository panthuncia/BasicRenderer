#include "Managers/Singletons/PSOManager.h"

#include <fstream>
#include <spdlog/spdlog.h>
#include <tree_sitter/api.h>

#include <directx/d3dx12.h>
#include "Utilities/Utilities.h"
#include "Managers/Singletons/DeviceManager.h"

#pragma comment(lib, "dxcompiler.lib")

extern "C" const TSLanguage* tree_sitter_hlsl();

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
}

PipelineState PSOManager::GetPSO(UINT psoFlags, BlendState blendState, bool wireframe) {
    PSOKey key(psoFlags, blendState, wireframe);
    if (m_psoCache.find(key) == m_psoCache.end()) {
        m_psoCache[key] = CreatePSO(psoFlags, blendState, wireframe);
    }
    return m_psoCache[key];
}

PipelineState PSOManager::GetShadowPSO(UINT psoFlags, BlendState blendState, bool wireframe) {
    PSOKey key(psoFlags, blendState, wireframe);
    if (m_shadowPSOCache.find(key) == m_shadowPSOCache.end()) {
        m_shadowPSOCache[key] = CreateShadowPSO(psoFlags, blendState, wireframe);
    }
    return m_shadowPSOCache[key];
}

PipelineState PSOManager::GetShadowMeshPSO(UINT psoFlags, BlendState blendState, bool wireframe) {
    PSOKey key(psoFlags, blendState, wireframe);
    if (m_shadowMeshPSOCache.find(key) == m_shadowMeshPSOCache.end()) {
        m_shadowMeshPSOCache[key] = CreateShadowMeshPSO(psoFlags, blendState, wireframe);
    }
    return m_shadowMeshPSOCache[key];
}

PipelineState PSOManager::GetPrePassPSO(UINT psoFlags, BlendState blendState, bool wireframe) {
    PSOKey key(psoFlags, blendState, wireframe);
    if (m_prePassPSOCache.find(key) == m_prePassPSOCache.end()) {
        m_prePassPSOCache[key] = CreatePrePassPSO(psoFlags, blendState, wireframe);
    }
    return m_prePassPSOCache[key];
}

PipelineState PSOManager::GetMeshPrePassPSO(UINT psoFlags, BlendState blendState, bool wireframe) {
    PSOKey key(psoFlags, blendState, wireframe);
    if (m_meshPrePassPSOCache.find(key) == m_meshPrePassPSOCache.end()) {
        m_meshPrePassPSOCache[key] = CreateMeshPrePassPSO(psoFlags, blendState, wireframe);
    }
    return m_meshPrePassPSOCache[key];
}

PipelineState PSOManager::GetPPLLPSO(UINT psoFlags, BlendState blendState, bool wireframe) {
    PSOKey key(psoFlags, blendState, wireframe);
    if (m_PPLLPSOCache.find(key) == m_PPLLPSOCache.end()) {
        m_PPLLPSOCache[key] = CreatePPLLPSO(psoFlags, blendState, wireframe);
    }
    return m_PPLLPSOCache[key];
}

PipelineState PSOManager::GetMeshPSO(UINT psoFlags, BlendState blendState, bool wireframe) {
    PSOKey key(psoFlags, blendState, wireframe);
    if (m_meshPSOCache.find(key) == m_meshPSOCache.end()) {
        m_meshPSOCache[key] = CreateMeshPSO(psoFlags, blendState, wireframe);
    }
    return m_meshPSOCache[key];
}

PipelineState PSOManager::GetMeshPPLLPSO(UINT psoFlags, BlendState blendState, bool wireframe) {
    PSOKey key(psoFlags, blendState, wireframe);
    if (m_meshPPLLPSOCache.find(key) == m_meshPPLLPSOCache.end()) {
        m_meshPPLLPSOCache[key] = CreateMeshPPLLPSO(psoFlags, blendState, wireframe);
    }
    return m_meshPPLLPSOCache[key];
}

PipelineState PSOManager::GetDeferredPSO(UINT psoFlags) {
    if (m_deferredPSOCache.find(psoFlags) == m_deferredPSOCache.end()) {
        m_deferredPSOCache[psoFlags] = CreateDeferredPSO(psoFlags);
    }
    return m_deferredPSOCache[psoFlags];
}

PipelineState PSOManager::CreatePSO(UINT psoFlags, BlendState blendState, bool wireframe) {
    // Define shader macros
    auto defines = GetShaderDefines(psoFlags);

    // Compile shaders
    Microsoft::WRL::ComPtr<ID3DBlob> vertexShader;
    Microsoft::WRL::ComPtr<ID3DBlob> pixelShader;

	ShaderInfoBundle shaderInfoBundle;
    shaderInfoBundle.vertexShader = { L"shaders/shaders.hlsl", L"VSMain", L"vs_6_6" };
	shaderInfoBundle.pixelShader = { L"shaders/shaders.hlsl", L"PSMain", L"ps_6_6" };
	shaderInfoBundle.defines = defines;
	auto compiledBundle = CompileShaders(shaderInfoBundle);
	vertexShader = compiledBundle.vertexShader;
	pixelShader = compiledBundle.pixelShader;

    // Create the pipeline state object
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { nullptr, 0 }; // We use vertex pulling
    psoDesc.pRootSignature = rootSignature.Get();
    psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader.Get());
    //if (!(psoFlags & PSOFlags::PSO_SHADOW)) {
    psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader.Get());
    //}
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    if (wireframe) {
        psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
    }
    psoDesc.RasterizerState.FrontCounterClockwise = true;
    if (psoFlags & PSOFlags::PSO_DOUBLE_SIDED) {
        psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    }
    psoDesc.BlendState = GetBlendDesc(blendState);
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_EQUAL;
	psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = psoFlags & PSOFlags::PSO_SHADOW ? 0 : 1;
    if(!(psoFlags & PSOFlags::PSO_SHADOW)) {
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    }

    psoDesc.SampleDesc.Count = 1;
    psoDesc.DSVFormat = psoFlags & PSOFlags::PSO_SHADOW ? DXGI_FORMAT_D32_FLOAT : DXGI_FORMAT_D32_FLOAT;

    Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
    auto& device = DeviceManager::GetInstance().GetDevice();
    ThrowIfFailed(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso)));

    return { pso, compiledBundle.resourceIDsHash, compiledBundle.resourceDescriptorSlots };
}

PipelineState PSOManager::CreateShadowPSO(UINT psoFlags, BlendState blendState, bool wireframe) {
    // Define shader macros
    auto defines = GetShaderDefines(psoFlags);

    // Compile shaders
    Microsoft::WRL::ComPtr<ID3DBlob> vertexShader;
    Microsoft::WRL::ComPtr<ID3DBlob> pixelShader;

    ShaderInfoBundle shaderInfoBundle;
    shaderInfoBundle.vertexShader = { L"shaders/shaders.hlsl", L"VSMain", L"vs_6_6" };
    shaderInfoBundle.pixelShader = { L"shaders/shaders.hlsl", L"PSMain", L"ps_6_6" };
    shaderInfoBundle.defines = defines;
    auto compiledBundle = CompileShaders(shaderInfoBundle);
	vertexShader = compiledBundle.vertexShader;
	pixelShader = compiledBundle.pixelShader;

    // Create the pipeline state object
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { nullptr, 0 }; // We use vertex pulling
    psoDesc.pRootSignature = rootSignature.Get();
    psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader.Get());
    //if (!(psoFlags & PSOFlags::PSO_SHADOW)) {
    psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader.Get());
    //}
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    if (wireframe) {
        psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
    }
    psoDesc.RasterizerState.FrontCounterClockwise = true;
    if (psoFlags & PSOFlags::PSO_DOUBLE_SIDED) {
        psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    }
    psoDesc.BlendState = GetBlendDesc(blendState);
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;

    psoDesc.RTVFormats[0] = DXGI_FORMAT_R32_FLOAT; // Linear depth

    psoDesc.SampleDesc.Count = 1;
    psoDesc.DSVFormat = psoFlags & PSOFlags::PSO_SHADOW ? DXGI_FORMAT_D32_FLOAT : DXGI_FORMAT_D32_FLOAT;

    Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
    auto& device = DeviceManager::GetInstance().GetDevice();
    ThrowIfFailed(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso)));

    return { pso, compiledBundle.resourceIDsHash, compiledBundle.resourceDescriptorSlots };
}


PipelineState PSOManager::CreatePrePassPSO(UINT psoFlags, BlendState blendState, bool wireframe) {
    // Define shader macros
    auto defines = GetShaderDefines(psoFlags | PSO_PREPASS);

    // Compile shaders
    Microsoft::WRL::ComPtr<ID3DBlob> vertexShader;
    Microsoft::WRL::ComPtr<ID3DBlob> pixelShader;

    ShaderInfoBundle shaderInfoBundle;
    shaderInfoBundle.vertexShader = { L"shaders/shaders.hlsl", L"VSMain", L"vs_6_6" };
    shaderInfoBundle.pixelShader = { L"shaders/shaders.hlsl", L"PrepassPSMain", L"ps_6_6" };
    shaderInfoBundle.defines = defines;
    auto compiledBundle = CompileShaders(shaderInfoBundle);
	vertexShader = compiledBundle.vertexShader;
	pixelShader = compiledBundle.pixelShader;

    // Create the pipeline state object
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { nullptr, 0 }; // We use vertex pulling
    psoDesc.pRootSignature = rootSignature.Get();
    psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader.Get());
	psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader.Get());
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    if (wireframe) {
        psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
    }
    psoDesc.RasterizerState.FrontCounterClockwise = true;
    if (psoFlags & PSOFlags::PSO_DOUBLE_SIDED) {
        psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    }
    psoDesc.BlendState = GetBlendDesc(blendState);
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    if (psoFlags & PSO_DEFERRED) {
        psoDesc.NumRenderTargets = 6;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R32G32B32A32_FLOAT; // Normals
		psoDesc.RTVFormats[1] = DXGI_FORMAT_R16G16_FLOAT; // Motion vector
		psoDesc.RTVFormats[2] = DXGI_FORMAT_R32_FLOAT; // Depth
        psoDesc.RTVFormats[3] = DXGI_FORMAT_R8G8B8A8_UNORM; // Albedo
        psoDesc.RTVFormats[4] = DXGI_FORMAT_R8G8_UNORM; // Metallic and Roughness
		psoDesc.RTVFormats[5] = DXGI_FORMAT_R16G16B16A16_FLOAT; // Emissive
	}
	else {
		psoDesc.NumRenderTargets = 3;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R32G32B32A32_FLOAT;
		psoDesc.RTVFormats[1] = DXGI_FORMAT_R16G16_FLOAT; // Motion vector
        psoDesc.RTVFormats[2] = DXGI_FORMAT_R32_FLOAT; // Depth
	}

    psoDesc.SampleDesc.Count = 1;
    psoDesc.DSVFormat = psoFlags & PSOFlags::PSO_SHADOW ? DXGI_FORMAT_D32_FLOAT : DXGI_FORMAT_D32_FLOAT;

    Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
    auto& device = DeviceManager::GetInstance().GetDevice();
    ThrowIfFailed(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso)));

    return { pso, compiledBundle.resourceIDsHash, compiledBundle.resourceDescriptorSlots };
}

PipelineState PSOManager::CreatePPLLPSO(UINT psoFlags, BlendState blendState, bool wireframe) {
    // Define shader macros
    auto defines = GetShaderDefines(psoFlags);

    // Compile shaders
    Microsoft::WRL::ComPtr<ID3DBlob> vertexShader;
    Microsoft::WRL::ComPtr<ID3DBlob> pixelShader;

    ShaderInfoBundle shaderInfoBundle;
    shaderInfoBundle.vertexShader = { L"shaders/shaders.hlsl", L"VSMain", L"vs_6_6" };
    shaderInfoBundle.pixelShader = { L"shaders/PPLL.hlsl", L"PPLLFillPS", L"ps_6_6" };
    shaderInfoBundle.defines = defines;
    auto compiledBundle = CompileShaders(shaderInfoBundle);
    vertexShader = compiledBundle.vertexShader;
    pixelShader = compiledBundle.pixelShader;

    // Create the pipeline state object
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { nullptr, 0 }; // We use vertex pulling
    psoDesc.pRootSignature = rootSignature.Get();
    psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader.Get());
    //if (!(psoFlags & PSOFlags::PSO_SHADOW)) {
    psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader.Get());
    //}
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    if (wireframe) {
        psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
    }
    psoDesc.RasterizerState.FrontCounterClockwise = true;
    if (psoFlags & PSOFlags::PSO_DOUBLE_SIDED) {
        psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    }
    psoDesc.BlendState = GetBlendDesc(blendState);
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    if (psoFlags & PSOFlags::PSO_SHADOW) {
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R32_FLOAT;
    }
    else {
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    }

    psoDesc.SampleDesc.Count = 1;
    psoDesc.DSVFormat = psoFlags & PSOFlags::PSO_SHADOW ? DXGI_FORMAT_D32_FLOAT : DXGI_FORMAT_D32_FLOAT;

    Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
    auto& device = DeviceManager::GetInstance().GetDevice();
    ThrowIfFailed(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso)));

    return { pso, compiledBundle.resourceIDsHash, compiledBundle.resourceDescriptorSlots };
}

PipelineState PSOManager::CreateMeshPSO(
    UINT psoFlags, BlendState blendState, bool wireframe) {
    // Define shader macros
    auto defines = GetShaderDefines(psoFlags);

    // Compile shaders
	Microsoft::WRL::ComPtr<ID3D10Blob> amplificationShader;
    Microsoft::WRL::ComPtr<ID3DBlob> meshShader;
    Microsoft::WRL::ComPtr<ID3DBlob> pixelShader;

    ShaderInfoBundle shaderInfoBundle;
    shaderInfoBundle.amplificationShader = { L"shaders/amplification.hlsl", L"ASMain", L"as_6_6" };
    shaderInfoBundle.meshShader = { L"shaders/mesh.hlsl", L"MSMain", L"ms_6_6" };
	shaderInfoBundle.pixelShader = { L"shaders/shaders.hlsl", L"PSMain", L"ps_6_6" };
    shaderInfoBundle.defines = defines;
    auto compiledBundle = CompileShaders(shaderInfoBundle);
    amplificationShader = compiledBundle.amplificationShader;
	meshShader = compiledBundle.meshShader;
	pixelShader = compiledBundle.pixelShader;

    // Create rasterizer state
    CD3DX12_RASTERIZER_DESC rasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    if (wireframe) {
        rasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
    }
    rasterizerState.FrontCounterClockwise = true;
    if (psoFlags & PSOFlags::PSO_DOUBLE_SIDED) {
        rasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    }

    // Set up the blend state
    CD3DX12_BLEND_DESC blendDesc = CD3DX12_BLEND_DESC(GetBlendDesc(blendState));

    // Set up the depth stencil state
    CD3DX12_DEPTH_STENCIL_DESC depthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    depthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_EQUAL;
    depthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;

    // Set the render target format
    DXGI_FORMAT rtvFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
    DXGI_FORMAT dsvFormat = (psoFlags & PSOFlags::PSO_SHADOW) ? DXGI_FORMAT_D32_FLOAT : DXGI_FORMAT_D32_FLOAT;

    // Define the pipeline state stream subobjects
    struct PipelineStateStream {
        CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE RootSignature;
		CD3DX12_PIPELINE_STATE_STREAM_AS AS;
        CD3DX12_PIPELINE_STATE_STREAM_MS MS;
        CD3DX12_PIPELINE_STATE_STREAM_PS PS;
        CD3DX12_PIPELINE_STATE_STREAM_RASTERIZER RasterizerState;
        CD3DX12_PIPELINE_STATE_STREAM_BLEND_DESC BlendState;
        CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL DepthStencilState;
        CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS RTVFormats;
        CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT DSVFormat;
    };

    PipelineStateStream pipelineStateStream = {};
    pipelineStateStream.RootSignature = rootSignature.Get();
	pipelineStateStream.AS = CD3DX12_SHADER_BYTECODE(amplificationShader.Get());
    pipelineStateStream.MS = CD3DX12_SHADER_BYTECODE(meshShader.Get());
    pipelineStateStream.PS = CD3DX12_SHADER_BYTECODE(pixelShader.Get());

    pipelineStateStream.RasterizerState = rasterizerState;
    pipelineStateStream.BlendState = blendDesc;
    pipelineStateStream.DepthStencilState = depthStencilState;

    D3D12_RT_FORMAT_ARRAY rtvFormats = {};
    rtvFormats.NumRenderTargets = psoFlags & PSO_SHADOW ? 0 : 1;
	if (!(psoFlags & PSOFlags::PSO_SHADOW)) {
        rtvFormats.RTFormats[0] = rtvFormat;
    }
	pipelineStateStream.RTVFormats = rtvFormats;

	pipelineStateStream.DSVFormat = dsvFormat;

    // Create the pipeline state stream descriptor
    D3D12_PIPELINE_STATE_STREAM_DESC streamDesc = {};
    streamDesc.SizeInBytes = sizeof(PipelineStateStream);
    streamDesc.pPipelineStateSubobjectStream = &pipelineStateStream;

    // Create the pipeline state
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
    auto& device = DeviceManager::GetInstance().GetDevice();
    ID3D12Device2* device2 = nullptr;
    ThrowIfFailed(device->QueryInterface(IID_PPV_ARGS(&device2)));
    ThrowIfFailed(device2->CreatePipelineState(&streamDesc, IID_PPV_ARGS(&pso)));

    return { pso, compiledBundle.resourceIDsHash, compiledBundle.resourceDescriptorSlots };
}

PipelineState PSOManager::CreateShadowMeshPSO(
    UINT psoFlags, BlendState blendState, bool wireframe) {
    // Define shader macros
    auto defines = GetShaderDefines(psoFlags);

    // Compile shaders
	Microsoft::WRL::ComPtr<ID3D10Blob> amplificationShader;
    Microsoft::WRL::ComPtr<ID3DBlob> meshShader;
    Microsoft::WRL::ComPtr<ID3DBlob> pixelShader;

    ShaderInfoBundle shaderInfoBundle;
    shaderInfoBundle.amplificationShader = { L"shaders/amplification.hlsl", L"ASMain", L"as_6_6" };
    shaderInfoBundle.meshShader = { L"shaders/mesh.hlsl", L"MSMain", L"ms_6_6" };
    shaderInfoBundle.pixelShader = { L"shaders/shaders.hlsl", L"PSMain", L"ps_6_6" };
    shaderInfoBundle.defines = defines;
    auto compiledBundle = CompileShaders(shaderInfoBundle);
    amplificationShader = compiledBundle.amplificationShader;
    meshShader = compiledBundle.meshShader;
	pixelShader = compiledBundle.pixelShader;

    // Create rasterizer state
    CD3DX12_RASTERIZER_DESC rasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    if (wireframe) {
        rasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
    }
    rasterizerState.FrontCounterClockwise = true;
    if (psoFlags & PSOFlags::PSO_DOUBLE_SIDED) {
        rasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    }

    // Set up the blend state
    CD3DX12_BLEND_DESC blendDesc = CD3DX12_BLEND_DESC(GetBlendDesc(blendState));

    // Set up the depth stencil state
    CD3DX12_DEPTH_STENCIL_DESC depthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);

    // Set the render target format
    DXGI_FORMAT rtvFormat = DXGI_FORMAT_R32_FLOAT;
    DXGI_FORMAT dsvFormat = (psoFlags & PSOFlags::PSO_SHADOW) ? DXGI_FORMAT_D32_FLOAT : DXGI_FORMAT_D32_FLOAT;

    // Define the pipeline state stream subobjects
    struct PipelineStateStream {
        CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE RootSignature;
		CD3DX12_PIPELINE_STATE_STREAM_AS AS;
        CD3DX12_PIPELINE_STATE_STREAM_MS MS;
        CD3DX12_PIPELINE_STATE_STREAM_PS PS;
        CD3DX12_PIPELINE_STATE_STREAM_RASTERIZER RasterizerState;
        CD3DX12_PIPELINE_STATE_STREAM_BLEND_DESC BlendState;
        CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL DepthStencilState;
        CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS RTVFormats;
        CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT DSVFormat;
    };

    PipelineStateStream pipelineStateStream = {};
    pipelineStateStream.RootSignature = rootSignature.Get();
	pipelineStateStream.AS = CD3DX12_SHADER_BYTECODE(amplificationShader.Get());
    pipelineStateStream.MS = CD3DX12_SHADER_BYTECODE(meshShader.Get());
    pipelineStateStream.PS = CD3DX12_SHADER_BYTECODE(pixelShader.Get());

    pipelineStateStream.RasterizerState = rasterizerState;
    pipelineStateStream.BlendState = blendDesc;
    pipelineStateStream.DepthStencilState = depthStencilState;

    D3D12_RT_FORMAT_ARRAY rtvFormats = {};
    rtvFormats.NumRenderTargets = 1;
    rtvFormats.RTFormats[0] = rtvFormat;

    pipelineStateStream.RTVFormats = rtvFormats;

    pipelineStateStream.DSVFormat = dsvFormat;

    // Create the pipeline state stream descriptor
    D3D12_PIPELINE_STATE_STREAM_DESC streamDesc = {};
    streamDesc.SizeInBytes = sizeof(PipelineStateStream);
    streamDesc.pPipelineStateSubobjectStream = &pipelineStateStream;

    // Create the pipeline state
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
    auto& device = DeviceManager::GetInstance().GetDevice();
    ID3D12Device2* device2 = nullptr;
    ThrowIfFailed(device->QueryInterface(IID_PPV_ARGS(&device2)));
    ThrowIfFailed(device2->CreatePipelineState(&streamDesc, IID_PPV_ARGS(&pso)));

    return { pso, compiledBundle.resourceIDsHash, compiledBundle.resourceDescriptorSlots };
}

PipelineState PSOManager::CreateMeshPrePassPSO(
    UINT psoFlags, BlendState blendState, bool wireframe) {
    // Define shader macros
    auto defines = GetShaderDefines(psoFlags | PSO_PREPASS);

    // Compile shaders
    Microsoft::WRL::ComPtr<ID3DBlob> amplificationShader;
    Microsoft::WRL::ComPtr<ID3DBlob> meshShader;
    Microsoft::WRL::ComPtr<ID3DBlob> pixelShader;

    ShaderInfoBundle shaderInfoBundle;
    shaderInfoBundle.amplificationShader = { L"shaders/amplification.hlsl", L"ASMain", L"as_6_6" };
    shaderInfoBundle.meshShader = { L"shaders/mesh.hlsl", L"MSMain", L"ms_6_6" };
    shaderInfoBundle.pixelShader = { L"shaders/shaders.hlsl", L"PrepassPSMain", L"ps_6_6" };
    shaderInfoBundle.defines = defines;
    auto compiledBundle = CompileShaders(shaderInfoBundle);
    amplificationShader = compiledBundle.amplificationShader;
    meshShader = compiledBundle.meshShader;
	pixelShader = compiledBundle.pixelShader;

    // Create rasterizer state
    CD3DX12_RASTERIZER_DESC rasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    if (wireframe) {
        rasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
    }
    rasterizerState.FrontCounterClockwise = true;
    if (psoFlags & PSOFlags::PSO_DOUBLE_SIDED) {
        rasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    }

    // Set up the blend state
    CD3DX12_BLEND_DESC blendDesc = CD3DX12_BLEND_DESC(GetBlendDesc(blendState));

    // Set up the depth stencil state
    CD3DX12_DEPTH_STENCIL_DESC depthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);

    // Set the render target format
    DXGI_FORMAT dsvFormat = DXGI_FORMAT_D32_FLOAT;

    // Define the pipeline state stream subobjects
    struct PipelineStateStream {
        CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE RootSignature;
        CD3DX12_PIPELINE_STATE_STREAM_AS AS;
        CD3DX12_PIPELINE_STATE_STREAM_MS MS;
        CD3DX12_PIPELINE_STATE_STREAM_PS PS;
        CD3DX12_PIPELINE_STATE_STREAM_RASTERIZER RasterizerState;
        CD3DX12_PIPELINE_STATE_STREAM_BLEND_DESC BlendState;
        CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL DepthStencilState;
        CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS RTVFormats;
        CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT DSVFormat;
    };

    PipelineStateStream pipelineStateStream = {};
    pipelineStateStream.RootSignature = rootSignature.Get();
	pipelineStateStream.AS = CD3DX12_SHADER_BYTECODE(amplificationShader.Get());
    pipelineStateStream.MS = CD3DX12_SHADER_BYTECODE(meshShader.Get());
    pipelineStateStream.PS = CD3DX12_SHADER_BYTECODE(pixelShader.Get());

    pipelineStateStream.RasterizerState = rasterizerState;
    pipelineStateStream.BlendState = blendDesc;
    pipelineStateStream.DepthStencilState = depthStencilState;

    D3D12_RT_FORMAT_ARRAY rtvFormats = {};
    if (psoFlags & PSO_DEFERRED) {
        rtvFormats.NumRenderTargets = 6;
        rtvFormats.RTFormats[0] = DXGI_FORMAT_R32G32B32A32_FLOAT; // Normals
        rtvFormats.RTFormats[1] = DXGI_FORMAT_R16G16_FLOAT; // motion vector
		rtvFormats.RTFormats[2] = DXGI_FORMAT_R32_FLOAT; // Depth
        rtvFormats.RTFormats[3] = DXGI_FORMAT_R8G8B8A8_UNORM; // Albedo
        rtvFormats.RTFormats[4] = DXGI_FORMAT_R8G8_UNORM; // Metallic and Roughness
		rtvFormats.RTFormats[5] = DXGI_FORMAT_R16G16B16A16_FLOAT; // Emissive
	}
	else {
        rtvFormats.NumRenderTargets = 3;
        rtvFormats.RTFormats[0] = DXGI_FORMAT_R32G32B32A32_FLOAT; // Normals
		rtvFormats.RTFormats[1] = DXGI_FORMAT_R16G16_FLOAT; // motion vector
		rtvFormats.RTFormats[2] = DXGI_FORMAT_R32_FLOAT; // Depth
	}
    pipelineStateStream.RTVFormats = rtvFormats;

    pipelineStateStream.DSVFormat = dsvFormat;

    // Create the pipeline state stream descriptor
    D3D12_PIPELINE_STATE_STREAM_DESC streamDesc = {};
    streamDesc.SizeInBytes = sizeof(PipelineStateStream);
    streamDesc.pPipelineStateSubobjectStream = &pipelineStateStream;

    // Create the pipeline state
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
    auto& device = DeviceManager::GetInstance().GetDevice();
    ID3D12Device2* device2 = nullptr;
    ThrowIfFailed(device->QueryInterface(IID_PPV_ARGS(&device2)));
    ThrowIfFailed(device2->CreatePipelineState(&streamDesc, IID_PPV_ARGS(&pso)));

    return { pso, compiledBundle.resourceIDsHash, compiledBundle.resourceDescriptorSlots };
}

PipelineState PSOManager::CreateMeshPPLLPSO(
    UINT psoFlags, BlendState blendState, bool wireframe) {
    // Define shader macros
    auto defines = GetShaderDefines(psoFlags);

    // Compile shaders
	Microsoft::WRL::ComPtr<ID3DBlob> amplificationShader;
    Microsoft::WRL::ComPtr<ID3DBlob> meshShader;
    Microsoft::WRL::ComPtr<ID3DBlob> pixelShader;
    
    ShaderInfoBundle shaderInfoBundle;
    shaderInfoBundle.amplificationShader = { L"shaders/amplification.hlsl", L"ASMain", L"as_6_6" };
    shaderInfoBundle.meshShader = { L"shaders/mesh.hlsl", L"MSMain", L"ms_6_6" };
    shaderInfoBundle.pixelShader = { L"shaders/PPLL.hlsl", L"PPLLFillPS", L"ps_6_6" };
    shaderInfoBundle.defines = defines;
    auto compiledBundle = CompileShaders(shaderInfoBundle);
    amplificationShader = compiledBundle.amplificationShader;
    meshShader = compiledBundle.meshShader;
	pixelShader = compiledBundle.pixelShader;

    // Create rasterizer state
    CD3DX12_RASTERIZER_DESC rasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    if (wireframe) {
        rasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
    }
    rasterizerState.FrontCounterClockwise = true;
    if (psoFlags & PSOFlags::PSO_DOUBLE_SIDED) {
        rasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    }

    // Set up the blend state
    CD3DX12_BLEND_DESC blendDesc = CD3DX12_BLEND_DESC(GetBlendDesc(blendState));

    // Set up the depth stencil state
    CD3DX12_DEPTH_STENCIL_DESC depthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	depthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;

    // Set the render target format
    DXGI_FORMAT dsvFormat = (psoFlags & PSOFlags::PSO_SHADOW) ? DXGI_FORMAT_D32_FLOAT : DXGI_FORMAT_D32_FLOAT;

    // Define the pipeline state stream subobjects
    struct PipelineStateStream {
        CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE RootSignature;
		CD3DX12_PIPELINE_STATE_STREAM_AS AS;
        CD3DX12_PIPELINE_STATE_STREAM_MS MS;
        CD3DX12_PIPELINE_STATE_STREAM_PS PS;
        CD3DX12_PIPELINE_STATE_STREAM_RASTERIZER RasterizerState;
        CD3DX12_PIPELINE_STATE_STREAM_BLEND_DESC BlendState;
        CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL DepthStencilState;
        CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT DSVFormat;
    };

    PipelineStateStream pipelineStateStream = {};
    pipelineStateStream.RootSignature = rootSignature.Get();
	pipelineStateStream.AS = CD3DX12_SHADER_BYTECODE(amplificationShader.Get());
    pipelineStateStream.MS = CD3DX12_SHADER_BYTECODE(meshShader.Get());
    pipelineStateStream.PS = CD3DX12_SHADER_BYTECODE(pixelShader.Get());

    pipelineStateStream.RasterizerState = rasterizerState;
    pipelineStateStream.BlendState = blendDesc;
    pipelineStateStream.DepthStencilState = depthStencilState;

    pipelineStateStream.DSVFormat = dsvFormat;

    // Create the pipeline state stream descriptor
    D3D12_PIPELINE_STATE_STREAM_DESC streamDesc = {};
    streamDesc.SizeInBytes = sizeof(PipelineStateStream);
    streamDesc.pPipelineStateSubobjectStream = &pipelineStateStream;

    // Create the pipeline state
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
    auto& device = DeviceManager::GetInstance().GetDevice();
    ID3D12Device2* device2 = nullptr;
    ThrowIfFailed(device->QueryInterface(IID_PPV_ARGS(&device2)));
    ThrowIfFailed(device2->CreatePipelineState(&streamDesc, IID_PPV_ARGS(&pso)));

    return { pso, compiledBundle.resourceIDsHash, compiledBundle.resourceDescriptorSlots };
}

PipelineState PSOManager::CreateDeferredPSO(UINT psoFlags) {
    auto defines = GetShaderDefines(psoFlags);


    Microsoft::WRL::ComPtr<ID3DBlob> vertexShader;
    Microsoft::WRL::ComPtr<ID3DBlob> pixelShader;

	ShaderInfoBundle shaderInfoBundle;
	shaderInfoBundle.vertexShader = { L"shaders/fullscreenVS.hlsli", L"FullscreenVSMain", L"vs_6_6" };
	shaderInfoBundle.pixelShader = { L"shaders/shaders.hlsl", L"PSMainDeferred", L"ps_6_6" };
	shaderInfoBundle.defines = defines;
	auto compiledBundle = CompileShaders(shaderInfoBundle);
	vertexShader = compiledBundle.vertexShader;
	pixelShader = compiledBundle.pixelShader;

    D3D12_INPUT_LAYOUT_DESC inputLayoutDesc = {};
    inputLayoutDesc.pInputElementDescs = nullptr;
    inputLayoutDesc.NumElements = 0;

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
    blendDesc.AlphaToCoverageEnable = FALSE;
    blendDesc.IndependentBlendEnable = FALSE;
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    D3D12_DEPTH_STENCIL_DESC depthStencilDesc = {};
    depthStencilDesc.DepthEnable = true;
    depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_GREATER;
    depthStencilDesc.StencilEnable = FALSE;
    depthStencilDesc.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
    depthStencilDesc.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
    depthStencilDesc.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
    depthStencilDesc.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    depthStencilDesc.BackFace = depthStencilDesc.FrontFace;

    DXGI_FORMAT renderTargetFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = inputLayoutDesc;
    psoDesc.pRootSignature = PSOManager::GetInstance().GetRootSignature().Get();
    psoDesc.VS = { vertexShader->GetBufferPointer(), vertexShader->GetBufferSize() };
    psoDesc.PS = { pixelShader->GetBufferPointer(), pixelShader->GetBufferSize() };
    psoDesc.RasterizerState = rasterizerDesc;
    psoDesc.BlendState = blendDesc;
    psoDesc.DepthStencilState = depthStencilDesc;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = renderTargetFormat;
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleDesc.Quality = 0;
    psoDesc.InputLayout = inputLayoutDesc;

    Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
    auto& device = DeviceManager::GetInstance().GetDevice();
    auto hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso));
    if (FAILED(hr)) {
        throw std::runtime_error("Failed to create debug PSO");
    }
    return { pso, compiledBundle.resourceIDsHash, compiledBundle.resourceDescriptorSlots };
}

std::vector<DxcDefine> PSOManager::GetShaderDefines(UINT psoFlags) {
    std::vector<DxcDefine> defines = {};
    if (psoFlags & PSOFlags::PSO_DOUBLE_SIDED) {
		DxcDefine macro;
		macro.Value = L"1";
		macro.Name = L"PSO_DOUBLE_SIDED";
		defines.insert(defines.begin(), macro);
    }
    if (psoFlags & PSOFlags::PSO_SHADOW) {
        DxcDefine macro;
        macro.Value = L"1";
        macro.Name = L"PSO_SHADOW";
        defines.insert(defines.begin(), macro);
    }
    if (psoFlags & PSOFlags::PSO_IMAGE_BASED_LIGHTING) {
        DxcDefine macro;
        macro.Value = L"1";
        macro.Name = L"PSO_IMAGE_BASED_LIGHTING";
        defines.insert(defines.begin(), macro);
    }
	if (psoFlags & PSOFlags::PSO_ALPHA_TEST) {
		DxcDefine macro;
		macro.Value = L"1";
		macro.Name = L"PSO_ALPHA_TEST";
		defines.insert(defines.begin(), macro);
	}
	if (psoFlags & PSOFlags::PSO_BLEND) {
		DxcDefine macro;
		macro.Value = L"1";
		macro.Name = L"PSO_BLEND";
		defines.insert(defines.begin(), macro);
	}
	if (psoFlags & PSOFlags::PSO_CLUSTERED_LIGHTING) {
		DxcDefine macro;
		macro.Value = L"1";
		macro.Name = L"PSO_CLUSTERED_LIGHTING";
		defines.insert(defines.begin(), macro);
	}
	if (psoFlags & PSOFlags::PSO_PREPASS) {
		DxcDefine macro;
		macro.Value = L"1";
		macro.Name = L"PSO_PREPASS";
		defines.insert(defines.begin(), macro);
	}
	if (psoFlags & PSOFlags::PSO_DEFERRED) {
        DxcDefine macro;
		macro.Value = L"1";
		macro.Name = L"PSO_DEFERRED";
		defines.insert(defines.begin(), macro);
	}
    if (!(psoFlags & PSOFlags::PSO_SCREENSPACE_REFLECTIONS)) {
        DxcDefine macro;
        macro.Value = L"1";
        macro.Name = L"PSO_SPECULAR_IBL";
        defines.insert(defines.begin(), macro);
	}

    return defines;
}

struct Replacement {
    uint32_t startByte;
    uint32_t endByte;
    std::string replacement;
};

void BuildFunctionDefs(std::unordered_map<std::string, std::vector<TSNode>>& functionDefs, const char* preprocessedSource, const TSNode& root) {
    // Search through all children of root to find function_definition nodes
    uint32_t childCount = ts_node_child_count(root);
    for (uint32_t i = 0; i < childCount; ++i) {
        TSNode node = ts_node_child(root, i);
        if (std::string(ts_node_type(node)) == "function_definition") {
            // In tree-sitter-hlsl grammar, child #1 is the identifier
            TSNode topDecl = ts_node_child_by_field_name(node, "declarator", static_cast<uint32_t>(strlen("declarator")));
            if (ts_node_is_null(topDecl)) continue;

            // From that, get the inner declarator
            TSNode innerDecl = ts_node_child_by_field_name(topDecl, "declarator", static_cast<uint32_t>(strlen("declarator")));
            if (ts_node_is_null(innerDecl)) continue;

            // Now, find the actual identifier inside `innerDecl`
            TSNode nameNode = {};
            // If this declarator *is* a bare identifier, use it directly:
            if (std::string(ts_node_type(innerDecl)) == "identifier") {
                nameNode = innerDecl;
            }
            // Otherwise, it’s some sort of qualified or templated thing:
            else {
                nameNode = ts_node_child_by_field_name(
                    innerDecl,
                    "name",
                    static_cast<uint32_t>(strlen("name"))
                );
            }

            if (ts_node_is_null(nameNode)) {
                continue;
            }

            // Extract the text out of the source buffer:
            uint32_t start = ts_node_start_byte(nameNode);
            uint32_t end = ts_node_end_byte(nameNode);
            std::string fnName(preprocessedSource + start, end - start);

            TSNode bodyNode = ts_node_child_by_field_name(node, "body", 4);
            if (!functionDefs.contains(fnName)) {
                functionDefs[fnName] = std::vector<TSNode>();
            }
            functionDefs[fnName].push_back(bodyNode);
        }
    }
}

void ParseBRSLResourceIdentifiers(std::unordered_set<std::string>& outMandatoryIdentifiers, std::unordered_set<std::string>& outOptionalIdentifiers, const DxcBuffer* pBuffer, const std::string& entryPointName) {
    const char* preprocessedSource = static_cast<const char*>(pBuffer->Ptr);
    size_t sourceSize = pBuffer->Size;

    TSParser* parser = ts_parser_new();
    ts_parser_set_language(parser, tree_sitter_hlsl());

    TSTree* tree = ts_parser_parse_string(parser, nullptr, preprocessedSource, static_cast<uint32_t>(sourceSize));
    TSNode root = ts_tree_root_node(tree);

    std::unordered_map<std::string, std::vector<TSNode>> functionDefs;

	BuildFunctionDefs(functionDefs, preprocessedSource, root);

    // Prepare for call-graph walk
    std::unordered_set<std::string> visited;
    std::vector<std::string> worklist;
    worklist.push_back(entryPointName);

    // Wherever we discover ResourceDescriptorIndex, we record replacements
    std::vector<Replacement> replacements;
    std::unordered_map<std::string, std::string> indexMap;

    // Helper to process one function body
    auto processBody = [&](const TSNode& bodyNode, auto&& processBodyRef) -> void {
        // Walk the subtree of this body looking for:
        //  a) ResourceDescriptorIndex calls -> record replacements
        //  b) Other function calls -> enqueue them if unvisited
        std::function<void(TSNode)> walk = [&](TSNode node) {
            const char* type = ts_node_type(node);

            if (strcmp(type, "call_expression") == 0) {
                // Check for ResourceDescriptorIndex(...)
                TSNode functionNode =
                    ts_node_child_by_field_name(node, "function", static_cast<uint32_t>(strlen("function")));

                if (ts_node_is_null(functionNode)) return;

                // Pull out function name from source
                uint32_t start = ts_node_start_byte(functionNode);
                uint32_t end = ts_node_end_byte(functionNode);

                // Slice out the raw text
                std::string funcName(preprocessedSource + start, end - start);

                // trim whitespace:
                auto l = funcName.find_first_not_of(" \t\n\r");
                auto r = funcName.find_last_not_of(" \t\n\r");
                if (l != std::string::npos && r != std::string::npos)
                    funcName = funcName.substr(l, r - l + 1);

                auto parseBuiltin = [&]() -> std::string {
                    TSNode argList = ts_node_child_by_field_name(node, "arguments", 9);
                    if (ts_node_named_child_count(argList) == 1) {
                        TSNode argNode = ts_node_named_child(argList, 0);

                        uint32_t start = ts_node_start_byte(argNode);
                        uint32_t end = ts_node_end_byte(argNode);

                        std::string rawText(preprocessedSource + start, end - start);

                        // if it's a quoted string literal, strip the quotes
                        if (rawText.size() >= 2 && rawText.front() == '"' && rawText.back() == '"') {
                            rawText = rawText.substr(1, rawText.size() - 2);
                        }

                        return std::move(rawText);
                    }
                    else {
						throw std::runtime_error("ResourceDescriptorIndex requires exactly one argument");
                    }
					};

                if (funcName == "ResourceDescriptorIndex") {
                    outMandatoryIdentifiers.insert(parseBuiltin());
                }
                else if (funcName == "OptionalResourceDescriptorIndex") {
                    outOptionalIdentifiers.insert(parseBuiltin());
                }
                else {
                    // Otherwise, a normal function call:
                    // Enqueue it for later processing if we know its definition
                    if (functionDefs.count(funcName) && !visited.count(funcName)) {
                        visited.insert(funcName);
                        worklist.push_back(funcName);
                    }
                }
            }

            // recurse
            uint32_t n = ts_node_child_count(node);
            for (uint32_t i = 0; i < n; ++i) {
                walk(ts_node_child(node, i));
            }
            };

        walk(bodyNode);
        };

    // Traverse the call graph
    while (!worklist.empty()) {
        std::string fn = worklist.back();
        worklist.pop_back();

        auto it = functionDefs.find(fn);
        if (it == functionDefs.end())
            continue;   // no definition in this file

        for (const auto& bodyNode : it->second) {
            processBody(bodyNode, processBody);
		}
    }

    std::string sourceText(preprocessedSource, sourceSize);
    // Emit transformed code
    std::string output;
    size_t cursor = 0;
    for (const auto& r : replacements) {
        output += sourceText.substr(cursor, r.startByte - cursor);
        output += r.replacement;
        cursor = r.endByte;
    }
    output += sourceText.substr(cursor);

    ts_tree_delete(tree);
    ts_parser_delete(parser);
}

std::string
rewriteResourceDescriptorCalls(const char* preprocessedSource,
    size_t       sourceSize,
    const std::string& entryPointName,
    const std::unordered_map<std::string, std::string>& replacementMap)
{
    TSParser* parser = ts_parser_new();
    ts_parser_set_language(parser, tree_sitter_hlsl());
    TSTree* tree = ts_parser_parse_string(
        parser, nullptr, preprocessedSource, static_cast<uint32_t>(sourceSize));
    TSNode root = ts_tree_root_node(tree);

    std::unordered_map<std::string, std::vector<TSNode>> functionDefs;

	BuildFunctionDefs(functionDefs, preprocessedSource, root);
   
    // Then do the same call-graph walk, but instead of collecting into a set,
    // build a vector<Replacement> by looking up replacementMap[id].
    // Finally, apply the text splice just as you already have:

    // [parse & build functionDefs, then BFS same as collect]

    std::vector<Replacement> replacements;
    std::unordered_set<std::string> visited;
    std::vector<std::string> worklist;
    worklist.push_back(entryPointName);

    auto processBody = [&](const TSNode& bodyNode, auto&& processBodyRef) -> void {
        // Walk the subtree of this body looking for:
        //  a) ResourceDescriptorIndex calls -> record replacements
        //  b) Other function calls -> enqueue them if unvisited
        std::function<void(TSNode)> walk = [&](TSNode node) {
            const char* type = ts_node_type(node);

            if (strcmp(type, "call_expression") == 0) {
                // Check for ResourceDescriptorIndex(...)
                TSNode functionNode =
                    ts_node_child_by_field_name(node, "function", static_cast<uint32_t>(strlen("function")));

                if (ts_node_is_null(functionNode)) return;

                // Pull out function name from source
                uint32_t start = ts_node_start_byte(functionNode);
                uint32_t end = ts_node_end_byte(functionNode);

                // Slice out the raw text
                std::string funcName(preprocessedSource + start, end - start);

                // trim whitespace:
                auto l = funcName.find_first_not_of(" \t\n\r");
                auto r = funcName.find_last_not_of(" \t\n\r");
                if (l != std::string::npos && r != std::string::npos)
                    funcName = funcName.substr(l, r - l + 1);


                if (funcName == "ResourceDescriptorIndex" || funcName == "OptionalResourceDescriptorIndex") {
                    TSNode argList = ts_node_child_by_field_name(node, "arguments", 9);
                    if (ts_node_named_child_count(argList) == 1) {
                        TSNode argNode = ts_node_named_child(argList, 0);

                        uint32_t start = ts_node_start_byte(argNode);
                        uint32_t end = ts_node_end_byte(argNode);

                        std::string rawText(preprocessedSource + start, end - start);

                        // if it's a quoted string literal, strip the quotes
                        if (rawText.size() >= 2 && rawText.front() == '"' && rawText.back() == '"') {
                            rawText = rawText.substr(1, rawText.size() - 2);
                        }

                        std::string identifier = std::move(rawText);
                        if (replacementMap.count(identifier)) {
                            replacements.push_back({
                            ts_node_start_byte(node),
                            ts_node_end_byte(node),
                            replacementMap.at(identifier)
                            });
                        }
                        else {
                            throw std::runtime_error(
								"ResourceDescriptorIndex identifier does not have mapped replacement: " + identifier);
                        }
                    }
                }
                else {
                    // Otherwise, a normal function call:
                    // Enqueue it for later processing if we know its definition
                    if (functionDefs.count(funcName) && !visited.count(funcName)) {
                        visited.insert(funcName);
                        worklist.push_back(funcName);
                    }
                }
            }

            // recurse
            uint32_t n = ts_node_child_count(node);
            for (uint32_t i = 0; i < n; ++i) {
                walk(ts_node_child(node, i));
            }
            };

        walk(bodyNode);
        };

    // Traverse the call graph
    while (!worklist.empty()) {
        std::string fn = worklist.back();
        worklist.pop_back();

        auto it = functionDefs.find(fn);
        if (it == functionDefs.end())
            continue;   // no definition in this file

        for (const auto& bodyNode : it->second) {
            processBody(bodyNode, processBody);
		}
    }

	// Sort replacements by startByte
    std::sort(replacements.begin(), replacements.end(),
        [](const Replacement& a, const Replacement& b) {
            return a.startByte < b.startByte;
		});

    std::string source(preprocessedSource, sourceSize);
    std::string out;
    size_t cursor = 0;
    for (auto& r : replacements) {
        out.append(source, cursor, r.startByte - cursor);
        out += r.replacement;
        cursor = r.endByte;
    }
    out.append(source, cursor);

    ts_tree_delete(tree);
    ts_parser_delete(parser);

    return out;
}

static uint32_t
shrinkToIncludeDecorators(const char* src, uint32_t origStart)
{
    uint32_t newStart = origStart;
    while (newStart > 0) {
        // find the '\n' that ends the *previous* line
        auto prevNL = std::string_view(src, newStart).rfind('\n');
        if (prevNL == std::string::npos) break;
        // prevNL points at the '\n' before the declaration line,
        // so the line we care about runs from (prevNL_of_that + 1) .. prevNL-1
        size_t lineEnd = prevNL;
        size_t lineBegin = std::string_view(src, prevNL).rfind('\n');
        if (lineBegin == std::string_view::npos) lineBegin = 0;
        else                                    lineBegin += 1;

        // extract that line and trim it
        auto   len = lineEnd - lineBegin;
        auto   sv = std::string_view(src + lineBegin, len);
        auto   first = sv.find_first_not_of(" \t\r\n");
        if (first == std::string_view::npos) {
            // blank line: skip it, but keep going up
            newStart = (uint32_t)lineBegin;
            continue;
        }
        if (sv[first] == '[') {
            // decorator: include this line too
            newStart = (uint32_t)lineBegin;
            continue;
        }
        // otherwise: stop looking
        break;
    }
    return newStart;
}

struct Range { uint32_t start, end; };
std::string
pruneUnusedCode(const char* preprocessedSource,
    size_t       sourceSize,
    const std::string& entryPointName,
    const std::unordered_map<std::string, std::string>& replacementMap)
{
    TSParser* parser = ts_parser_new();
    ts_parser_set_language(parser, tree_sitter_hlsl());
    TSTree* tree = ts_parser_parse_string(parser, nullptr, preprocessedSource, static_cast<uint32_t>(sourceSize));
    TSNode root = ts_tree_root_node(tree);

    std::unordered_map<std::string, std::vector<TSNode>> bodyMap, defMap;
    uint32_t topCount = ts_node_child_count(root);
    for (uint32_t i = 0; i < topCount; ++i) {
        TSNode node = ts_node_child(root, i);
        if (std::string(ts_node_type(node)) != "function_definition") continue;

        // extract the function name
        TSNode decl1 = ts_node_child_by_field_name(node, "declarator", static_cast<uint32_t>(strlen("declarator")));
        TSNode decl2 = !ts_node_is_null(decl1)
            ? ts_node_child_by_field_name(decl1, "declarator", static_cast<uint32_t>(strlen("declarator")))
            : TSNode{};
        TSNode nameNode = {};
        if (!ts_node_is_null(decl2) && std::string(ts_node_type(decl2)) == "identifier") {
            nameNode = decl2;
        }
        else if (!ts_node_is_null(decl2)) {
            nameNode = ts_node_child_by_field_name(decl2, "name", static_cast<uint32_t>(strlen("name")));
        }
        if (ts_node_is_null(nameNode)) continue;

        auto s = ts_node_start_byte(nameNode);
        auto e = ts_node_end_byte(nameNode);
        std::string fnName(preprocessedSource + s, e - s);

        // store the full def node and its body

        if (!defMap.contains(fnName)) {
            defMap[fnName] = {};
        }

        if (!bodyMap.contains(fnName)) {
            bodyMap[fnName] = {};
		}

        defMap[fnName].push_back(node);
        bodyMap[fnName].push_back(ts_node_child_by_field_name(node, "body", static_cast<uint32_t>(strlen("body"))));
    }

    // BFS from entryPointName to find reachable functions
    std::unordered_set<std::string> visited{ entryPointName };
    std::vector<std::string> work{ entryPointName };

    auto enqueueCalls = [&](TSNode body) {
        std::function<void(TSNode)> walk = [&](TSNode n) {
            if (ts_node_is_null(n)) return;
            if (std::string(ts_node_type(n)) == "call_expression") {
                TSNode fn = ts_node_child_by_field_name(n, "function", static_cast<uint32_t>(strlen("function")));
                if (!ts_node_is_null(fn)) {

                    TSNode args = ts_node_child_by_field_name(n, "arguments", static_cast<uint32_t>(strlen("arguments")));

                    uint32_t start = ts_node_start_byte(fn);
                    uint32_t end = ts_node_end_byte(args);    // include closing ')'

                    std::string callSig(
                        preprocessedSource + start,
                        end - start
                    );

                    uint32_t ss = ts_node_start_byte(fn), ee = ts_node_end_byte(fn);
                    std::string called(preprocessedSource + ss, ee - ss);
                    auto l = called.find_first_not_of(" \t\r\n"),
                        r = called.find_last_not_of(" \t\r\n");
                    if (l != std::string::npos && r != std::string::npos)
                        called = called.substr(l, r - l + 1);
                    if (bodyMap.count(called) && !visited.count(called)) {
                        visited.insert(called);
                        work.push_back(called);
                    }
                }
            }
            uint32_t c = ts_node_child_count(n);
            for (uint32_t i = 0; i < c; ++i) walk(ts_node_child(n, i));
            };
        walk(body);
        };

    while (!work.empty()) {
        auto fn = work.back(); work.pop_back();
        auto it = bodyMap.find(fn);
        if (it != bodyMap.end())
            for (auto& body : it->second) {
                enqueueCalls(body);
            }
    }

    std::vector<Range> removeRanges;
    for (auto const& kv : defMap) {
        if (!visited.count(kv.first)) {
            for (auto& defNode : kv.second) {
                uint32_t origStart = ts_node_start_byte(defNode);
                uint32_t origEnd = ts_node_end_byte(defNode);
                uint32_t adjStart = shrinkToIncludeDecorators(
                    preprocessedSource, origStart);
                removeRanges.push_back({ adjStart, origEnd });
            }
        }
    }

    // merge overlapping removeRanges
    std::sort(removeRanges.begin(), removeRanges.end(),
        [](auto& a, auto& b) { return a.start < b.start; });
    std::vector<Range> merged;
    for (auto& r : removeRanges) {
        if (merged.empty() || r.start > merged.back().end) {
            merged.push_back(r);
        }
        else {
            merged.back().end = std::max(merged.back().end, r.end);
        }
    }

    // keepRanges as complement of merged removeRanges
    std::vector<Range> keep;
    uint32_t lastEnd = 0;
    for (auto& r : merged) {
        if (lastEnd < r.start)
            keep.push_back({ lastEnd, r.start });
        lastEnd = std::max(lastEnd, r.end);
    }
    if (lastEnd < sourceSize)
        keep.push_back({ lastEnd, (uint32_t)sourceSize });

    // splice keepRanges together
    std::string out;
    out.reserve(sourceSize);
    for (auto& r : keep)
        out.append(preprocessedSource + r.start, r.end - r.start);

    // cleanup
    ts_tree_delete(tree);
    ts_parser_delete(parser);

    return out;
}

void PSOManager::GetPreprocessedBlob(
    const std::wstring& filename,
    const std::wstring& entryPoint,
    const std::wstring& target,
    std::vector<DxcDefine> defines,
    Microsoft::WRL::ComPtr<ID3DBlob>& outBlob) {

    auto exePath = std::filesystem::path(GetExePath());
    auto fullPath = exePath / filename;
    auto shaderDir = exePath / L"shaders";

    PSOManager::SourceData srcBuf;
    LoadSource(fullPath, srcBuf);
    auto includeHandler = CreateIncludeHandler();

    ShaderCompileOptions opts;
    opts.entryPoint = entryPoint;
    opts.target = target;
    opts.defines = std::move(defines);
#if BUILD_TYPE == BUILD_TYPE_DEBUG || BUILD_TYPE == BUILD_TYPE_RELEASE_DEBUG
    opts.enableDebugInfo = true;
#endif
    opts.warningsAsErrors = true;

    auto args = BuildArguments(opts, shaderDir);

    args.push_back(L"-P"); // Preprocess only
    auto preProcessedResult = InvokeCompile(
        srcBuf.buffer, args, includeHandler.Get()
    );

    preProcessedResult->GetOutput(DXC_OUT_HLSL, IID_PPV_ARGS(&outBlob), nullptr);
}

void pruneUnusedCodeForSlot(
	std::string& inOutSource,
    const std::optional<ShaderInfo>& slot)
{
    if (!slot)
        return;
    // Prune unused code
    std::string prunedSource = pruneUnusedCode(
        inOutSource.c_str(),
        inOutSource.length(),
        ws2s(slot->entryPoint),
        {}
    );
	inOutSource = std::move(prunedSource);
}

void parseBRSLResourceIdentifiersForSlot(
    const std::optional<ShaderInfo>& slot,
    const std::vector<DxcDefine>& defines,
	const DxcBuffer* preprocessedBuffer,
    std::unordered_set<std::string>& outMandatoryIDs,
    std::unordered_set<std::string>& outOptionalIDs)
{
    if (!slot)
        return;
    
    ParseBRSLResourceIdentifiers(outMandatoryIDs, outOptionalIDs, preprocessedBuffer, ws2s(slot->entryPoint));
}

std::string rewriteResourceDescriptorIndexCallsForSlot(
    const std::optional<ShaderInfo>& slot,
    const std::vector<DxcDefine>& defines,
    Microsoft::WRL::ComPtr<ID3DBlob>& outBlob,
    DxcBuffer& outBuf,
    const std::unordered_map<std::string, std::string>& replacementMap)
{
    if (!slot)
        return "";
    return rewriteResourceDescriptorCalls(
        static_cast<const char*>(outBuf.Ptr),
        outBuf.Size,
        ws2s(slot->entryPoint),
        replacementMap
	);
}

void PSOManager::CompileShaderForSlot(
    const std::optional<ShaderInfo>& slot,
    const std::vector<DxcDefine>& defines,
    const DxcBuffer& buffer,
    Microsoft::WRL::ComPtr<ID3DBlob>& outBlob)
{
    if (!slot)
        return;
    //DxcBuffer ppBuffer = {};
    //Microsoft::WRL::ComPtr<ID3DBlob> preprocessedBlob;
    //PreprocessShaderSlot(slot, defines, preprocessedBlob, ppBuffer);
    CompileShader(
        slot->filename,
        slot->entryPoint,
        slot->target,
        buffer,
        defines,
        outBlob
    );
}

uint64_t hash_list(const std::vector<std::string>& list) {
    const uint64_t FNV_offset = 146527ULL;
    const uint64_t FNV_prime = 1099511628211ULL;
    uint64_t h = FNV_offset;
    for (auto& s : list) {
        uint32_t L = uint32_t(s.size());
        // mix in the length
        for (int i = 0; i < 4; ++i) {
            h ^= (uint8_t)(L >> (i * 8));
            h *= FNV_prime;
        }
        // mix in each character
        for (unsigned char c : s) {
            h ^= c;
            h *= FNV_prime;
        }
    }
    return h;
}

ShaderBundle PSOManager::CompileShaders(const ShaderInfoBundle& info) {
    if (info.vertexShader && info.meshShader) 
		throw std::runtime_error("Cannot compile both vertex and mesh shaders in the same bundle");
	if (info.computeShader && (info.meshShader || info.amplificationShader || info.vertexShader || info.pixelShader))
		throw std::runtime_error("Cannot compile compute shader with other shader types in the same bundle");

	Microsoft::WRL::ComPtr<ID3DBlob> preprocessedAmplificationShader;
	DxcBuffer amplificationBuffer = {};
	Microsoft::WRL::ComPtr<ID3DBlob> preprocessedMeshShader;
	DxcBuffer meshBuffer = {};
	Microsoft::WRL::ComPtr<ID3DBlob> preprocessedPixelShader;
	DxcBuffer pixelBuffer = {};
	Microsoft::WRL::ComPtr<ID3DBlob> preprocessedVertexShader;
	DxcBuffer vertexBuffer = {};
	Microsoft::WRL::ComPtr<ID3DBlob> preprocessedComputeShader;
	DxcBuffer computeBuffer = {};

    PreprocessShaderSlot(info.amplificationShader, info.defines, preprocessedAmplificationShader, amplificationBuffer);
    PreprocessShaderSlot(info.meshShader, info.defines, preprocessedMeshShader, meshBuffer);
    PreprocessShaderSlot(info.pixelShader, info.defines, preprocessedPixelShader, pixelBuffer);
    PreprocessShaderSlot(info.vertexShader, info.defines, preprocessedVertexShader, vertexBuffer);
    PreprocessShaderSlot(info.computeShader, info.defines, preprocessedComputeShader, computeBuffer);

	std::unordered_set<std::string> usedMandatoryIDs;
	std::unordered_set<std::string> usedOptionalIDs;
    parseBRSLResourceIdentifiersForSlot(info.amplificationShader, info.defines, &amplificationBuffer, usedMandatoryIDs, usedOptionalIDs);
	parseBRSLResourceIdentifiersForSlot(info.meshShader, info.defines, &meshBuffer, usedMandatoryIDs, usedOptionalIDs);
    parseBRSLResourceIdentifiersForSlot(info.pixelShader, info.defines, &pixelBuffer, usedMandatoryIDs, usedOptionalIDs);
    parseBRSLResourceIdentifiersForSlot(info.vertexShader, info.defines, &vertexBuffer, usedMandatoryIDs, usedOptionalIDs);
    parseBRSLResourceIdentifiersForSlot(info.computeShader, info.defines, &computeBuffer, usedMandatoryIDs, usedOptionalIDs);

	std::unordered_map<std::string, std::string> replacementMap;
	uint32_t nextIndex = 0;
    ShaderBundle bundle = {};
    std::vector<std::string> usedMandatoryResourceIDsVec;
    for (std::string entry : usedMandatoryIDs) {
		bundle.resourceDescriptorSlots.mandatoryResourceDescriptorSlots.push_back(entry);
		replacementMap[entry] = "ResourceDescriptorIndex" + std::to_string(nextIndex++);
        usedMandatoryResourceIDsVec.push_back(entry);
    }

	std::vector<std::string> usedOptionalResourceIDsVec;
    for (std::string entry : usedOptionalIDs) {
        bundle.resourceDescriptorSlots.optionalResourceDescriptorSlots.push_back(entry);
        replacementMap[entry] = "ResourceDescriptorIndex" + std::to_string(nextIndex++);
        usedOptionalResourceIDsVec.push_back(entry);
    }


	auto newAmplification = rewriteResourceDescriptorIndexCallsForSlot(info.amplificationShader, info.defines, preprocessedAmplificationShader, amplificationBuffer, replacementMap);
    auto newMesh = rewriteResourceDescriptorIndexCallsForSlot(info.meshShader, info.defines, preprocessedMeshShader, meshBuffer, replacementMap);
    auto newPixel = rewriteResourceDescriptorIndexCallsForSlot(info.pixelShader, info.defines, preprocessedPixelShader, pixelBuffer, replacementMap);
    auto newVertex = rewriteResourceDescriptorIndexCallsForSlot(info.vertexShader, info.defines, preprocessedVertexShader, vertexBuffer, replacementMap);
	auto newCompute = rewriteResourceDescriptorIndexCallsForSlot(info.computeShader, info.defines, preprocessedComputeShader, computeBuffer, replacementMap);

	pruneUnusedCodeForSlot(newAmplification, info.amplificationShader);
	pruneUnusedCodeForSlot(newMesh, info.meshShader);
	pruneUnusedCodeForSlot(newPixel, info.pixelShader);
	pruneUnusedCodeForSlot(newVertex, info.vertexShader);
	pruneUnusedCodeForSlot(newCompute, info.computeShader);

    if (!newAmplification.empty()) {
        amplificationBuffer.Ptr = newAmplification.data();
        amplificationBuffer.Size = newAmplification.size();
	}
    if (!newMesh.empty()) {
        meshBuffer.Ptr = newMesh.data();
        meshBuffer.Size = newMesh.size();
    }
    if (!newPixel.empty()) {
        pixelBuffer.Ptr = newPixel.data();
        pixelBuffer.Size = newPixel.size();
    }
    if (!newVertex.empty()) {
        vertexBuffer.Ptr = newVertex.data();
        vertexBuffer.Size = newVertex.size();
	}
    if (!newCompute.empty()) {
        computeBuffer.Ptr = newCompute.data();
        computeBuffer.Size = newCompute.size();
	}

	CompileShaderForSlot(info.amplificationShader, info.defines, amplificationBuffer, bundle.amplificationShader);
	CompileShaderForSlot(info.meshShader, info.defines, meshBuffer, bundle.meshShader);
	CompileShaderForSlot(info.pixelShader, info.defines, pixelBuffer, bundle.pixelShader);
	CompileShaderForSlot(info.vertexShader, info.defines, vertexBuffer, bundle.vertexShader);
    CompileShaderForSlot(info.computeShader, info.defines, computeBuffer, bundle.computeShader);

    std::vector<std::string> combinedIds = {};
	combinedIds.insert(combinedIds.end(), usedMandatoryResourceIDsVec.begin(), usedMandatoryResourceIDsVec.end());
	combinedIds.insert(combinedIds.end(), usedOptionalResourceIDsVec.begin(), usedOptionalResourceIDsVec.end());

	bundle.resourceIDsHash = hash_list(combinedIds);

	return bundle;
}

// for string delimiter
std::vector<std::string> split(std::string s, std::string delimiter) {
    size_t pos_start = 0, pos_end, delim_len = delimiter.length();
    std::string token;
    std::vector<std::string> res;

    while ((pos_end = s.find(delimiter, pos_start)) != std::string::npos) {
        token = s.substr(pos_start, pos_end - pos_start);
        pos_start = pos_end + delim_len;
        res.push_back(token);
    }

    res.push_back(s.substr(pos_start));
    return res;
}
void PSOManager::CompileShader(
    const std::wstring& filename,
    const std::wstring& entryPoint, 
    const std::wstring& target, 
	const DxcBuffer& ppBuffer,
    std::vector<DxcDefine> defines,
    Microsoft::WRL::ComPtr<ID3DBlob>& outBlob)
{
    auto exePath = std::filesystem::path(GetExePath());
    auto fullPath = exePath / filename;
    auto shaderDir = exePath / L"shaders";

    ShaderCompileOptions opts;
    opts.entryPoint = entryPoint;
    opts.target = target;
    opts.defines = std::move(defines);
#if BUILD_TYPE == BUILD_TYPE_DEBUG || BUILD_TYPE == BUILD_TYPE_RELEASE_DEBUG
    opts.enableDebugInfo = true;
#endif
    opts.warningsAsErrors = true;

    auto args = BuildArguments(opts, shaderDir);

    ComPtr<IDxcIncludeHandler> includeHandler;
    HRESULT hr = pUtils->CreateDefaultIncludeHandler(&includeHandler);
    if (FAILED(hr)) {
        spdlog::error("Failed to create include handler.");
        ThrowIfFailed(hr);
        return;
    }
    auto result = InvokeCompile(ppBuffer, args, includeHandler.Get());

    ComPtr<IDxcBlobEncoding> errors;
    result->GetErrorBuffer(&errors);
    std::string errText{ (char*)errors->GetBufferPointer(),
                         errors->GetBufferSize() };

    auto obj = ExtractObject(result.Get(), filename, opts.enableDebugInfo);

    ThrowIfFailed(
        result->GetResult(reinterpret_cast<IDxcBlob**>(outBlob.GetAddressOf()))
    );
}

void PSOManager::LoadSource(const std::filesystem::path& path, PSOManager::SourceData& sd) {
    UINT32 codePage = CP_UTF8;
    ThrowIfFailed(pUtils->LoadFile(
        path.c_str(), &codePage, &sd.blob
    ));
    sd.buffer.Ptr = sd.blob->GetBufferPointer();
    sd.buffer.Size = sd.blob->GetBufferSize();
    sd.buffer.Encoding = 0; // see below
}


ComPtr<IDxcIncludeHandler> PSOManager::CreateIncludeHandler()
{
    ComPtr<IDxcIncludeHandler> handler;
    ThrowIfFailed(pUtils->CreateDefaultIncludeHandler(&handler));
    return handler;
}

std::vector<LPCWSTR> PSOManager::BuildArguments(
    const ShaderCompileOptions& opts,
    const std::filesystem::path& shaderDir)
{
    std::vector<LPCWSTR> args;

    args.push_back(L"-E"); args.push_back(opts.entryPoint.c_str());
    args.push_back(L"-T"); args.push_back(opts.target.c_str());

    if (opts.warningsAsErrors)
        args.push_back(DXC_ARG_WARNINGS_ARE_ERRORS);

    if (opts.enableDebugInfo) {
        args.push_back(DXC_ARG_DEBUG);
        args.push_back(DXC_ARG_DEBUG_NAME_FOR_SOURCE);
        args.push_back(DXC_ARG_SKIP_OPTIMIZATIONS);
    }

    for (auto& def : opts.defines) {
        args.push_back(L"-D");
        args.push_back(def.Name);
    }

    // always include shaders folder
    args.push_back(L"-I");
    args.push_back(shaderDir.c_str());

    return args;
}

ComPtr<IDxcResult> PSOManager::InvokeCompile(
    const DxcBuffer& src,
    std::vector<LPCWSTR>& arguments,
    IDxcIncludeHandler* includeHandler)
{
    ComPtr<IDxcResult> result;
    HRESULT hr = pCompiler->Compile(
        &src,
        arguments.data(),
        (UINT)arguments.size(),
        includeHandler,
        IID_PPV_ARGS(result.GetAddressOf())
    );

    // on failure or errors, pull DXC_OUT_ERRORS and log
    if (FAILED(hr)) {
        ComPtr<IDxcBlobUtf8> errs;
        result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(errs.GetAddressOf()), nullptr);
        if (errs && errs->GetStringLength())
            spdlog::error("Shader compile error: {}", errs->GetStringPointer());
        ThrowIfFailed(hr);
    }

    {
        ComPtr<IDxcBlobUtf8> errs;
        result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(errs.GetAddressOf()), nullptr);
        if (errs && errs->GetStringLength()) {
            spdlog::error("Shader compile warnings: {}", errs->GetStringPointer());
            if (strstr(errs->GetStringPointer(), "error"))
                ThrowIfFailed(E_FAIL);
        }
    }

    return result;
}

ComPtr<IDxcBlob> PSOManager::ExtractObject(
    IDxcResult* result,
    const std::wstring& filename,
    bool writeDebugArtifacts)
{
    ComPtr<IDxcBlob> objectBlob;
    ThrowIfFailed(result->GetOutput(
        DXC_OUT_OBJECT,
        IID_PPV_ARGS(objectBlob.GetAddressOf()),
        nullptr));

    if (writeDebugArtifacts) {
        auto exePath = std::filesystem::path(GetExePath());
        auto outDir = exePath / L"CompiledShaders";
        std::filesystem::create_directories(outDir);

        // derive a base name from the suggested pdb path
        ComPtr<IDxcBlobUtf16> pdbPathBlob;
        ComPtr<IDxcBlob>     pdbBlob;
        ThrowIfFailed(result->GetOutput(
            DXC_OUT_PDB,
            IID_PPV_ARGS(pdbBlob.GetAddressOf()),
            pdbPathBlob.GetAddressOf()));

        auto suggested = pdbPathBlob->GetStringPointer();
        auto baseName = std::filesystem::path(suggested).stem().wstring();

        WriteDebugArtifacts(result, outDir, baseName);
    }

    return objectBlob;
}

void PSOManager::WriteDebugArtifacts(
    IDxcResult* result,
    const std::filesystem::path& outDir,
    const std::wstring& baseName)
{
    // write .bin
    ComPtr<IDxcBlob> pdbBlob;
    ComPtr<IDxcBlob> objBlob;
    ThrowIfFailed(result->GetOutput(
        DXC_OUT_PDB, IID_PPV_ARGS(pdbBlob.GetAddressOf()), nullptr));
    ThrowIfFailed(result->GetOutput(
        DXC_OUT_OBJECT, IID_PPV_ARGS(objBlob.GetAddressOf()), nullptr));

    auto write = [&](const std::filesystem::path& path, IDxcBlob* blob) {
        std::ofstream f(path, std::ios::binary);
        if (!f) spdlog::error("Failed to open {} for writing", path.string());
        else    f.write((char*)blob->GetBufferPointer(), blob->GetBufferSize());
        };

    write(outDir / (baseName + L".bin"), objBlob.Get());
    write(outDir / (baseName + L".pdb"), pdbBlob.Get());
}

void PSOManager::createRootSignature() {
    // Root parameters
    D3D12_ROOT_PARAMETER1 parameters[10] = {};

    // PerObject buffer index
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[0].Constants.ShaderRegister = 1;
    parameters[0].Constants.RegisterSpace = 0;
    parameters[0].Constants.Num32BitValues = NumPerObjectRootConstants;
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // PerMesh buffer index
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[1].Constants.ShaderRegister = 2;
    parameters[1].Constants.RegisterSpace = 0;
    parameters[1].Constants.Num32BitValues = NumPerMeshRootConstants;
    parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // view info
    parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[2].Constants.ShaderRegister = 3; // b3 for first integer root constant
    parameters[2].Constants.RegisterSpace = 0;
    parameters[2].Constants.Num32BitValues = NumViewRootConstants;
    parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // Third integer root constant, used for settings
    parameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[3].Constants.ShaderRegister = 4;
    parameters[3].Constants.RegisterSpace = 0;
    parameters[3].Constants.Num32BitValues = NumSettingsRootConstants;
    parameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // Draw info
    parameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[4].Constants.ShaderRegister = 5;
    parameters[4].Constants.RegisterSpace = 0;
    parameters[4].Constants.Num32BitValues = NumDrawInfoRootConstants;
    parameters[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // transparency info
    parameters[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[5].Constants.ShaderRegister = 6;
    parameters[5].Constants.RegisterSpace = 0;
    parameters[5].Constants.Num32BitValues = NumTransparencyInfoRootConstants;
    parameters[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // Light clustering info
	parameters[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	parameters[6].Constants.ShaderRegister = 7;
	parameters[6].Constants.RegisterSpace = 0;
	parameters[6].Constants.Num32BitValues = NumLightClusterRootConstants;
	parameters[6].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

	parameters[7].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	parameters[7].Constants.ShaderRegister = 8;
	parameters[7].Constants.RegisterSpace = 0;
	parameters[7].Constants.Num32BitValues = NumMiscUintRootConstants;
	parameters[7].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

	parameters[8].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	parameters[8].Constants.ShaderRegister = 9;
	parameters[8].Constants.RegisterSpace = 0;
	parameters[8].Constants.Num32BitValues = NumMiscFloatRootConstants;
	parameters[8].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

	parameters[9].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	parameters[9].Constants.ShaderRegister = 10;
	parameters[9].Constants.RegisterSpace = 0;
	parameters[9].Constants.Num32BitValues = NumResourceDescriptorIndicesRootConstants;
	parameters[9].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_STATIC_SAMPLER_DESC staticSamplers[2] = {};

    // point-clamp at s0
    auto& pointClamp = staticSamplers[0];
    pointClamp.Filter         = D3D12_FILTER_MIN_MAG_MIP_POINT;
    pointClamp.AddressU       = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    pointClamp.AddressV       = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    pointClamp.AddressW       = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    pointClamp.MipLODBias     = 0;
    pointClamp.MaxAnisotropy  = 0;
    pointClamp.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    pointClamp.MinLOD         = 0;
    pointClamp.MaxLOD         = D3D12_FLOAT32_MAX;
    pointClamp.ShaderRegister = 0;
    pointClamp.RegisterSpace  = 0;
    pointClamp.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // linear-clamp at s1
    auto& linearClamp = staticSamplers[1];
    linearClamp = pointClamp;
    linearClamp.Filter         = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    linearClamp.ShaderRegister = 1;

    // Root Signature Description
    D3D12_ROOT_SIGNATURE_DESC1 rootSignatureDesc = {};
    rootSignatureDesc.NumParameters = _countof(parameters);
    rootSignatureDesc.pParameters = parameters;
    rootSignatureDesc.NumStaticSamplers = _countof(staticSamplers);
    rootSignatureDesc.pStaticSamplers   = staticSamplers;
    rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT | D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED | D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED;

    // Serialize and create the root signature
    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC versionedDesc;
    versionedDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    versionedDesc.Desc_1_1 = rootSignatureDesc;
    ThrowIfFailed(D3D12SerializeVersionedRootSignature(&versionedDesc, &signature, &error));
    auto& device = DeviceManager::GetInstance().GetDevice();
    ThrowIfFailed(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&rootSignature)));

	// Compute root signature
    ComPtr<ID3DBlob> computeSignature;
    ComPtr<ID3DBlob> computeError;
    rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED | D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED;
    versionedDesc.Desc_1_1 = rootSignatureDesc;
	ThrowIfFailed(D3D12SerializeVersionedRootSignature(&versionedDesc, &computeSignature, &computeError));
	ThrowIfFailed(device->CreateRootSignature(0, computeSignature->GetBufferPointer(), computeSignature->GetBufferSize(), IID_PPV_ARGS(&computeRootSignature)));
}

ComPtr<ID3D12RootSignature> PSOManager::GetRootSignature() {
    return rootSignature;
}

ComPtr<ID3D12RootSignature> PSOManager::GetComputeRootSignature() {
	return computeRootSignature;
}

void PSOManager::ReloadShaders() {
    m_psoCache.clear();
	m_meshPSOCache.clear();
	m_deferredPSOCache.clear();
	m_PPLLPSOCache.clear();
	m_shadowPSOCache.clear();
	m_meshPrePassPSOCache.clear();
	m_prePassPSOCache.clear();
	m_shadowMeshPSOCache.clear();
    m_prePassPSOCache.clear();
}

D3D12_BLEND_DESC PSOManager::GetBlendDesc(BlendState blendState) {
    switch (blendState) {
    case BlendState::BLEND_STATE_OPAQUE: {
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
        maskBlendDesc.AlphaToCoverageEnable = FALSE;
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