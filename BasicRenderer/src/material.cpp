#include "material.h"

#include <stdexcept>

// Helper function for throwing if a DirectX function fails
inline void ThrowIfFailed(HRESULT hr) {
    if (FAILED(hr)) {
        throw std::runtime_error("HRESULT failed");
    }
}

Material::Material(ComPtr<ID3D12Device> device, const std::wstring& shaderFile)
    : device(device), pConstantBuffer(nullptr) {
    LoadShaders(shaderFile);
    CreateRootSignature();
    CreatePipelineState();
    CreateConstantBuffer();
}

void Material::LoadShaders(const std::wstring& shaderFile) {
    UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;

    // Compile vertex shader
    ComPtr<ID3DBlob> error;
    HRESULT hr = D3DCompileFromFile(shaderFile.c_str(), nullptr, nullptr, "VSMain", "vs_5_0", compileFlags, 0, &vertexShader, &error);
    if (FAILED(hr)) {
        if (error) {
            std::string errMsg(static_cast<char*>(error->GetBufferPointer()), error->GetBufferSize());
            throw std::runtime_error("Vertex shader compilation error: " + errMsg);
        }
        ThrowIfFailed(hr);
    }

    // Compile pixel shader
    hr = D3DCompileFromFile(shaderFile.c_str(), nullptr, nullptr, "PSMain", "ps_5_0", compileFlags, 0, &pixelShader, &error);
    if (FAILED(hr)) {
        if (error) {
            std::string errMsg(static_cast<char*>(error->GetBufferPointer()), error->GetBufferSize());
            throw std::runtime_error("Pixel shader compilation error: " + errMsg);
        }
        ThrowIfFailed(hr);
    }
}

void Material::CreateRootSignature() {
    // Create a descriptor range for the PerMaterialCBV
    CD3DX12_DESCRIPTOR_RANGE range;
    range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0); // b0 is the first descriptor table

    // Create a root parameter and bind the descriptor range to it
    CD3DX12_ROOT_PARAMETER rootParameter;
    rootParameter.InitAsDescriptorTable(1, &range, D3D12_SHADER_VISIBILITY_ALL);

    // Create the root signature
    CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
    rootSignatureDesc.Init(1, &rootParameter, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    ThrowIfFailed(D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error));
    ThrowIfFailed(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&rootSignature)));
}

void Material::CreatePipelineState() {
    // Define the vertex input layout
    inputElementDescs[0] = { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
    inputElementDescs[1] = { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };

    // Describe and create the graphics pipeline state object (PSO)
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
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleDesc.Quality = 0;

    ThrowIfFailed(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState)));
}

void Material::CreateConstantBuffer() {
    const UINT constantBufferSize = sizeof(PerMaterialCB);

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
}

void Material::UpdateConstantBuffer(const PerMaterialCB& cbData) {
    memcpy(pConstantBuffer, &cbData, sizeof(cbData));
}

void Material::Bind(ComPtr<ID3D12GraphicsCommandList> commandList) {
    // Set the root signature and pipeline state
    commandList->SetGraphicsRootSignature(rootSignature.Get());
    commandList->SetPipelineState(pipelineState.Get());

    // Bind the constant buffer
    commandList->SetGraphicsRootConstantBufferView(0, constantBuffer->GetGPUVirtualAddress());
}