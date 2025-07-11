#include "Managers/Singletons/PSOManager.h"

#include <fstream>
#include <spdlog/spdlog.h>
#include <filesystem>

#include <directx/d3dx12.h>
#include "Utilities/Utilities.h"
#include "Managers/Singletons/DeviceManager.h"

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
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> PSOManager::GetPSO(UINT psoFlags, BlendState blendState, bool wireframe) {
    PSOKey key(psoFlags, blendState, wireframe);
    if (m_psoCache.find(key) == m_psoCache.end()) {
        m_psoCache[key] = CreatePSO(psoFlags, blendState, wireframe);
    }
    return m_psoCache[key];
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> PSOManager::GetShadowPSO(UINT psoFlags, BlendState blendState, bool wireframe) {
    PSOKey key(psoFlags, blendState, wireframe);
    if (m_shadowPSOCache.find(key) == m_shadowPSOCache.end()) {
        m_shadowPSOCache[key] = CreateShadowPSO(psoFlags, blendState, wireframe);
    }
    return m_shadowPSOCache[key];
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> PSOManager::GetShadowMeshPSO(UINT psoFlags, BlendState blendState, bool wireframe) {
    PSOKey key(psoFlags, blendState, wireframe);
    if (m_shadowMeshPSOCache.find(key) == m_shadowMeshPSOCache.end()) {
        m_shadowMeshPSOCache[key] = CreateShadowMeshPSO(psoFlags, blendState, wireframe);
    }
    return m_shadowMeshPSOCache[key];
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> PSOManager::GetPrePassPSO(UINT psoFlags, BlendState blendState, bool wireframe) {
    PSOKey key(psoFlags, blendState, wireframe);
    if (m_prePassPSOCache.find(key) == m_prePassPSOCache.end()) {
        m_prePassPSOCache[key] = CreatePrePassPSO(psoFlags, blendState, wireframe);
    }
    return m_prePassPSOCache[key];
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> PSOManager::GetMeshPrePassPSO(UINT psoFlags, BlendState blendState, bool wireframe) {
    PSOKey key(psoFlags, blendState, wireframe);
    if (m_meshPrePassPSOCache.find(key) == m_meshPrePassPSOCache.end()) {
        m_meshPrePassPSOCache[key] = CreateMeshPrePassPSO(psoFlags, blendState, wireframe);
    }
    return m_meshPrePassPSOCache[key];
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> PSOManager::GetPPLLPSO(UINT psoFlags, BlendState blendState, bool wireframe) {
    PSOKey key(psoFlags, blendState, wireframe);
    if (m_PPLLPSOCache.find(key) == m_PPLLPSOCache.end()) {
        m_PPLLPSOCache[key] = CreatePPLLPSO(psoFlags, blendState, wireframe);
    }
    return m_PPLLPSOCache[key];
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> PSOManager::GetMeshPSO(UINT psoFlags, BlendState blendState, bool wireframe) {
    PSOKey key(psoFlags, blendState, wireframe);
    if (m_meshPSOCache.find(key) == m_meshPSOCache.end()) {
        m_meshPSOCache[key] = CreateMeshPSO(psoFlags, blendState, wireframe);
    }
    return m_meshPSOCache[key];
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> PSOManager::GetMeshPPLLPSO(UINT psoFlags, BlendState blendState, bool wireframe) {
    PSOKey key(psoFlags, blendState, wireframe);
    if (m_meshPPLLPSOCache.find(key) == m_meshPPLLPSOCache.end()) {
        m_meshPPLLPSOCache[key] = CreateMeshPPLLPSO(psoFlags, blendState, wireframe);
    }
    return m_meshPPLLPSOCache[key];
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> PSOManager::GetDeferredPSO(UINT psoFlags) {
    if (m_deferredPSOCache.find(psoFlags) == m_deferredPSOCache.end()) {
        m_deferredPSOCache[psoFlags] = CreateDeferredPSO(psoFlags);
    }
    return m_deferredPSOCache[psoFlags];
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> PSOManager::CreatePSO(UINT psoFlags, BlendState blendState, bool wireframe) {
    // Define shader macros
    auto defines = GetShaderDefines(psoFlags);

    // Compile shaders
    Microsoft::WRL::ComPtr<ID3DBlob> vertexShader;
    Microsoft::WRL::ComPtr<ID3DBlob> pixelShader;
    CompileShader(L"shaders/shaders.hlsl", L"VSMain", L"vs_6_6", defines, vertexShader);
    CompileShader(L"shaders/shaders.hlsl", L"PSMain", L"ps_6_6", defines, pixelShader);

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

    return pso;
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> PSOManager::CreateShadowPSO(UINT psoFlags, BlendState blendState, bool wireframe) {
    // Define shader macros
    auto defines = GetShaderDefines(psoFlags);

    // Compile shaders
    Microsoft::WRL::ComPtr<ID3DBlob> vertexShader;
    Microsoft::WRL::ComPtr<ID3DBlob> pixelShader;
    CompileShader(L"shaders/shaders.hlsl", L"VSMain", L"vs_6_6", defines, vertexShader);
    CompileShader(L"shaders/shaders.hlsl", L"PSMain", L"ps_6_6", defines, pixelShader);

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

    return pso;
}


Microsoft::WRL::ComPtr<ID3D12PipelineState> PSOManager::CreatePrePassPSO(UINT psoFlags, BlendState blendState, bool wireframe) {
    // Define shader macros
    auto defines = GetShaderDefines(psoFlags | PSO_PREPASS);

    // Compile shaders
    Microsoft::WRL::ComPtr<ID3DBlob> vertexShader;
    Microsoft::WRL::ComPtr<ID3DBlob> pixelShader;
    CompileShader(L"shaders/shaders.hlsl", L"VSMain", L"vs_6_6", defines, vertexShader);
    CompileShader(L"shaders/shaders.hlsl", L"PrepassPSMain", L"ps_6_6", defines, pixelShader);

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

    return pso;
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> PSOManager::CreatePPLLPSO(UINT psoFlags, BlendState blendState, bool wireframe) {
    // Define shader macros
    auto defines = GetShaderDefines(psoFlags);

    // Compile shaders
    Microsoft::WRL::ComPtr<ID3DBlob> vertexShader;
    Microsoft::WRL::ComPtr<ID3DBlob> pixelShader;
    CompileShader(L"shaders/shaders.hlsl", L"VSMain", L"vs_6_6", defines, vertexShader);
    CompileShader(L"shaders/PPLL.hlsl", L"PPLLFillPS", L"ps_6_6", defines, pixelShader);

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

    return pso;
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> PSOManager::CreateMeshPSO(
    UINT psoFlags, BlendState blendState, bool wireframe) {
    // Define shader macros
    auto defines = GetShaderDefines(psoFlags);

    // Compile shaders
	Microsoft::WRL::ComPtr<ID3D10Blob> amplificationShader;
    Microsoft::WRL::ComPtr<ID3DBlob> meshShader;
    Microsoft::WRL::ComPtr<ID3DBlob> pixelShader;

	CompileShader(L"shaders/mesh.hlsl", L"ASMain", L"as_6_6", defines, amplificationShader);
    CompileShader(L"shaders/mesh.hlsl", L"MSMain", L"ms_6_6", defines, meshShader);
    //if (!(psoFlags & PSOFlags::PSO_SHADOW)) {
        CompileShader(L"shaders/shaders.hlsl", L"PSMain", L"ps_6_6", defines, pixelShader);
    //}

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

    return pso;
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> PSOManager::CreateShadowMeshPSO(
    UINT psoFlags, BlendState blendState, bool wireframe) {
    // Define shader macros
    auto defines = GetShaderDefines(psoFlags);

    // Compile shaders
	Microsoft::WRL::ComPtr<ID3D10Blob> amplificationShader;
    Microsoft::WRL::ComPtr<ID3DBlob> meshShader;
    Microsoft::WRL::ComPtr<ID3DBlob> pixelShader;

	CompileShader(L"shaders/mesh.hlsl", L"ASMain", L"as_6_6", defines, amplificationShader);
    CompileShader(L"shaders/mesh.hlsl", L"MSMain", L"ms_6_6", defines, meshShader);
    //if (!(psoFlags & PSOFlags::PSO_SHADOW)) {
    CompileShader(L"shaders/shaders.hlsl", L"PSMain", L"ps_6_6", defines, pixelShader);
    //}

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

    return pso;
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> PSOManager::CreateMeshPrePassPSO(
    UINT psoFlags, BlendState blendState, bool wireframe) {
    // Define shader macros
    auto defines = GetShaderDefines(psoFlags | PSO_PREPASS);

    // Compile shaders
    Microsoft::WRL::ComPtr<ID3DBlob> amplificationShader;
    Microsoft::WRL::ComPtr<ID3DBlob> meshShader;
    Microsoft::WRL::ComPtr<ID3DBlob> pixelShader;

	CompileShader(L"shaders/mesh.hlsl", L"ASMain", L"as_6_6", defines, amplificationShader);
    CompileShader(L"shaders/mesh.hlsl", L"MSMain", L"ms_6_6", defines, meshShader);
    //if (!(psoFlags & PSOFlags::PSO_SHADOW)) {
    CompileShader(L"shaders/shaders.hlsl", L"PrepassPSMain", L"ps_6_6", defines, pixelShader);
    //}

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

    return pso;
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> PSOManager::CreateMeshPPLLPSO(
    UINT psoFlags, BlendState blendState, bool wireframe) {
    // Define shader macros
    auto defines = GetShaderDefines(psoFlags);

    // Compile shaders
	Microsoft::WRL::ComPtr<ID3DBlob> amplificationShader;
    Microsoft::WRL::ComPtr<ID3DBlob> meshShader;
    Microsoft::WRL::ComPtr<ID3DBlob> pixelShader;
    
	CompileShader(L"shaders/mesh.hlsl", L"ASMain", L"as_6_6", defines, amplificationShader);
    CompileShader(L"shaders/mesh.hlsl", L"MSMain", L"ms_6_6", defines, meshShader);
    //if (!(psoFlags & PSOFlags::PSO_SHADOW)) {
    CompileShader(L"shaders/PPLL.hlsl", L"PPLLFillPS", L"ps_6_6", defines, pixelShader);
    //}

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

    return pso;
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> PSOManager::CreateDeferredPSO(UINT psoFlags) {
    auto defines = GetShaderDefines(psoFlags);


    Microsoft::WRL::ComPtr<ID3DBlob> vertexShader;
    Microsoft::WRL::ComPtr<ID3DBlob> pixelShader;
    PSOManager::GetInstance().CompileShader(L"shaders/fullscreenVS.hlsli", L"FullscreenVSMain", L"vs_6_6", defines, vertexShader);
    PSOManager::GetInstance().CompileShader(L"shaders/shaders.hlsl", L"PSMainDeferred", L"ps_6_6", defines, pixelShader);

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
    return pso;
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

void PSOManager::CompileShader(const std::wstring& filename, const std::wstring& entryPoint, const std::wstring& target, std::vector<DxcDefine> defines, Microsoft::WRL::ComPtr<ID3DBlob>& shaderBlob) {
    ComPtr<IDxcBlobEncoding> sourceBlob;
    ComPtr<IDxcResult> result;
    ComPtr<IDxcIncludeHandler> includeHandler;

    UINT32 codePage = CP_UTF8;
    pUtils->LoadFile((std::filesystem::path(GetExePath()) / filename).c_str(), &codePage, &sourceBlob);

    HRESULT hr = pUtils->CreateDefaultIncludeHandler(&includeHandler);
    if (FAILED(hr)) {
        spdlog::error("Failed to create include handler.");
        ThrowIfFailed(hr);
        return;
    }

    DxcBuffer sourceBuffer;
    sourceBuffer.Ptr = sourceBlob->GetBufferPointer();
    sourceBuffer.Size = sourceBlob->GetBufferSize();
    sourceBuffer.Encoding = 0;

    std::vector<LPCWSTR> arguments;
    // -E for the entry point
    arguments.push_back(L"-E");
    arguments.push_back(entryPoint.c_str());

    // -T for the target profile (eg. ps_6_6)
    arguments.push_back(L"-T");
    arguments.push_back(target.c_str());

    arguments.push_back(DXC_ARG_WARNINGS_ARE_ERRORS); //-WX
#if BUILD_TYPE == BUILD_TYPE_DEBUG || BUILD_TYPE == BUILD_TYPE_RELEASE_DEBUG
    arguments.push_back(DXC_ARG_DEBUG); //-Zi
    arguments.push_back(DXC_ARG_DEBUG_NAME_FOR_SOURCE); //-Zss
    arguments.push_back(DXC_ARG_SKIP_OPTIMIZATIONS);
#endif

    for (const auto& define : defines)
    {
        arguments.push_back(L"-D");
        arguments.push_back(define.Name);
    }

    std::wstring includePath = (std::filesystem::path(GetExePath()) / L"shaders").wstring();
    arguments.push_back(L"-I");
    arguments.push_back(includePath.c_str());

    print("Compiling with arguments: ");
    for (auto& arg : arguments) {
        std::wcout << arg << " ";
    }
    std::wcout << std::endl;

    // Compile the shader
    hr = pCompiler->Compile(
        &sourceBuffer,
        arguments.data(),
        arguments.size(),
        includeHandler.Get(),
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

    ComPtr<IDxcBlob> objectBlob;
    hr = result->GetOutput(
        DXC_OUT_OBJECT,
        IID_PPV_ARGS(objectBlob.GetAddressOf()),
        nullptr);
    ThrowIfFailed(hr);

#if BUILD_TYPE == BUILD_TYPE_DEBUG || BUILD_TYPE == BUILD_TYPE_RELEASE_DEBUG

    // Retrieve the separate PDB debug info

    ComPtr<IDxcBlob> pdbBlob;
	ComPtr<IDxcBlobUtf16> pdbPathBlob;
    hr = result->GetOutput(
        DXC_OUT_PDB,
        IID_PPV_ARGS(pdbBlob.GetAddressOf()),
        pdbPathBlob.GetAddressOf());
    ThrowIfFailed(hr);

    const wchar_t* suggestedPdbPath = pdbPathBlob->GetStringPointer();
    std::filesystem::path pdbFs(suggestedPdbPath);

    auto hashedFileName = pdbFs.stem();    

    // Derive a base name and ensure output folder exists
    namespace fs = std::filesystem;
    fs::path exePath(GetExePath());
    fs::path outDir = exePath / L"CompiledShaders";
    fs::create_directories(outDir);

    fs::path shaderPath(filename);
    std::wstring baseName = hashedFileName;

    // Write out the full shader binary
    fs::path binPath = outDir / (baseName + L".bin");
    {
        std::ofstream binFile(binPath, std::ios::binary);
        if (!binFile) {
            spdlog::error("Failed to open {} for writing shader binary", 
                binPath.string());
        } else {
            binFile.write(
                reinterpret_cast<const char*>(objectBlob->GetBufferPointer()),
                objectBlob->GetBufferSize());
            //spdlog::info("Wrote shader binary: {}", binPath.string());
        }
    }

    // Write out the separate PDB
    fs::path pdbPath = outDir / (baseName + L".pdb");
    {
        std::ofstream pdbFile(pdbPath, std::ios::binary);
        if (!pdbFile) {
            spdlog::error("Failed to open {} for writing shader PDB", 
                pdbPath.string());
        } else {
            pdbFile.write(
                reinterpret_cast<const char*>(pdbBlob->GetBufferPointer()),
                pdbBlob->GetBufferSize());
            //spdlog::info("Wrote shader PDB: {}", pdbPath.string());
        }
    }
#endif

    // hand back the runtime blob for PSO creation
    ThrowIfFailed(result->GetResult(
        reinterpret_cast<IDxcBlob**>(shaderBlob.GetAddressOf())));
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

	// Static buffer indices
	parameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	parameters[4].Constants.ShaderRegister = 5;
	parameters[4].Constants.RegisterSpace = 0;
	parameters[4].Constants.Num32BitValues = NumStaticBufferRootConstants;
	parameters[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // Variable buffer indices
    parameters[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[5].Constants.ShaderRegister = 6;
    parameters[5].Constants.RegisterSpace = 0;
    parameters[5].Constants.Num32BitValues = NumVariableBufferRootConstants;
    parameters[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // transparency info
    parameters[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[6].Constants.ShaderRegister = 7;
    parameters[6].Constants.RegisterSpace = 0;
    parameters[6].Constants.Num32BitValues = NumTransparencyInfoRootConstants;
    parameters[6].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // Light clustering info
	parameters[7].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	parameters[7].Constants.ShaderRegister = 8;
	parameters[7].Constants.RegisterSpace = 0;
	parameters[7].Constants.Num32BitValues = NumLightClusterRootConstants;
	parameters[7].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

	parameters[8].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	parameters[8].Constants.ShaderRegister = 9;
	parameters[8].Constants.RegisterSpace = 0;
	parameters[8].Constants.Num32BitValues = NumMiscUintRootConstants;
	parameters[8].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

	parameters[9].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	parameters[9].Constants.ShaderRegister = 10;
	parameters[9].Constants.RegisterSpace = 0;
	parameters[9].Constants.Num32BitValues = NumMiscFloatRootConstants;
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