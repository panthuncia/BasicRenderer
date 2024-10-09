#include "PSOManager.h"

#include <fstream>
#include <spdlog/spdlog.h>
#include <filesystem>

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
        if (psoFlags & PSOFlags::BASE_COLOR_TEXTURE || psoFlags & PSOFlags::NORMAL_MAP || psoFlags & PSOFlags::PBR_MAPS || psoFlags & PSOFlags::AO_TEXTURE || psoFlags & PSOFlags::EMISSIVE_TEXTURE) {
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
    if (psoFlags & PSOFlags::BASE_COLOR_TEXTURE || psoFlags & PSOFlags::NORMAL_MAP || psoFlags & PSOFlags::PBR_MAPS || psoFlags & PSOFlags::AO_TEXTURE || psoFlags & PSOFlags::EMISSIVE_TEXTURE) {
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
    pUtils->LoadFile((std::filesystem::path(GetExePath())/filename).c_str(), &codePage, &sourceBlob);
    //library->CreateIncludeHandler(&includeHandler);

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
#if defined(_DEBUG)
    arguments.push_back(DXC_ARG_DEBUG); //-Zi
    arguments.push_back(DXC_ARG_DEBUG_NAME_FOR_SOURCE); //-Zss
    arguments.push_back(DXC_ARG_SKIP_OPTIMIZATIONS);
#endif

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
#if defined(_DEBUG)
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
#endif
    result->GetResult(reinterpret_cast<IDxcBlob**>(shaderBlob.GetAddressOf()));
}

void PSOManager::createRootSignature() {
    // Root parameters
    D3D12_ROOT_PARAMETER1 parameters[5] = {};

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

    // Third integer root constant, used for settings
    parameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[4].Constants.ShaderRegister = 5;
    parameters[4].Constants.RegisterSpace = 0;
    parameters[4].Constants.Num32BitValues = 2; 
    parameters[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

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