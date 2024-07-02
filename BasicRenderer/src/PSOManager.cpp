#include "PSOManager.h"
#include "DirectX/d3dx12.h"
#include "Utilities.h"
#include "DeviceManager.h"

Microsoft::WRL::ComPtr<ID3D12PipelineState> PSOManager::GetPSO(UINT psoFlags) {
    if (m_psoCache.find(psoFlags) == m_psoCache.end()) {
        m_psoCache[psoFlags] = CreatePSO(psoFlags);
    }
    return m_psoCache[psoFlags];
}



Microsoft::WRL::ComPtr<ID3D12PipelineState> PSOManager::CreatePSO(UINT psoFlags) {
    // Define shader macros
    auto defines = GetShaderDefines(psoFlags);

    // Compile shaders
    Microsoft::WRL::ComPtr<ID3DBlob> vertexShader;
    Microsoft::WRL::ComPtr<ID3DBlob> pixelShader;
    CompileShader(L"shaders/shaders.hlsl", "VSMain", "vs_5_0", defines.data(), vertexShader);
    CompileShader(L"shaders/shaders.hlsl", "PSMain", "ps_5_0", defines.data(), pixelShader);

    // Define the vertex input layout
    std::vector<D3D12_INPUT_ELEMENT_DESC> inputElementDescs = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    if (psoFlags & PSOFlags::VERTEX_COLORS) {
        inputElementDescs.push_back({ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 });
    }

    // Create the pipeline state object
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { inputElementDescs.data(), static_cast<unsigned int>(inputElementDescs.size())};
    psoDesc.pRootSignature = rootSignature.Get();
    psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader.Get());
    psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader.Get());
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

    Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
    auto device = DeviceManager::getInstance().getDevice();
    ThrowIfFailed(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso)));

    return pso;
}

std::vector<D3D_SHADER_MACRO> PSOManager::GetShaderDefines(UINT psoFlags) {
    std::vector<D3D_SHADER_MACRO> defines = {
    { NULL, NULL }
    };
    if (psoFlags & PSOFlags::VERTEX_COLORS) {
        D3D_SHADER_MACRO macro;
        macro.Definition = "1";
        macro.Name = "VERTEX_COLORS";
        defines.insert(defines.begin(), macro);
    }
    return defines;
}

void PSOManager::CompileShader(const std::wstring& filename, const std::string& entryPoint, const std::string& target, const D3D_SHADER_MACRO* defines, Microsoft::WRL::ComPtr<ID3DBlob>& shaderBlob) {
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
    UINT compileFlags = 0;
#if defined(_DEBUG)
    compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    HRESULT hr = D3DCompileFromFile(filename.c_str(), defines, D3D_COMPILE_STANDARD_FILE_INCLUDE, entryPoint.c_str(), target.c_str(), compileFlags, 0, &shaderBlob, &errorBlob);
    if (FAILED(hr)) {
        if (errorBlob) {
            std::string errorMsg(static_cast<char*>(errorBlob->GetBufferPointer()), errorBlob->GetBufferSize());
            throw std::runtime_error("Shader compilation error: " + errorMsg);
        }
        ThrowIfFailed(hr);
    }
}

void PSOManager::createRootSignature() {
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
    auto device = DeviceManager::getInstance().getDevice();
    ThrowIfFailed(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&rootSignature)));
}