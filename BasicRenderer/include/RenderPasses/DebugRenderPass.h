#pragma once

#include "RenderPasses/Base/RenderPass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "Resources/Texture.h"
#include "Resources/ResourceHandles.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Managers/Singletons/UploadManager.h"

class DebugRenderPass : public RenderPass {
public:
    DebugRenderPass() {}

    void Setup() override {
		CreateDebugRootSignature();
		CreateDebugPSO();
    }

    PassReturn Execute(RenderContext& context) override {
        if (m_texture == nullptr) {
            return {};
        }
        auto& psoManager = PSOManager::GetInstance();
        auto& commandList = context.commandList;

        ID3D12DescriptorHeap* descriptorHeaps[] = {
            context.textureDescriptorHeap, // The texture descriptor heap
            context.samplerDescriptorHeap, // The sampler descriptor heap
        };
        commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

        commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);

        CD3DX12_VIEWPORT viewport(0.0f, 0.0f, context.xRes, context.yRes);
        CD3DX12_RECT scissorRect(0, 0, context.xRes, context.yRes);
        commandList->RSSetViewports(1, &viewport);
        commandList->RSSetScissorRects(1, &scissorRect);

        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(context.rtvHeap->GetCPUDescriptorHandleForHeapStart(), context.frameIndex, context.rtvDescriptorSize);
        commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

        commandList->SetPipelineState(debugPSO.Get());
        commandList->SetGraphicsRootSignature(debugRootSignature.Get());

        commandList->SetGraphicsRootDescriptorTable(0, m_texture->GetSRVInfo()[0].gpuHandle);
        auto viewMatrix = XMMatrixTranspose(XMMatrixMultiply(XMMatrixScaling(0.2f, 0.2f, 1.0f), XMMatrixTranslation(0.7, -0.7, 0)));
        commandList->SetGraphicsRoot32BitConstants(1, 16, &viewMatrix, 0);

        commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        commandList->DrawInstanced(4, 1, 0, 0); // Fullscreen quad
        return {};
    }

    void Cleanup(RenderContext& context) override {
        // Cleanup if necessary
    }

    void SetTexture(PixelBuffer* texture) {
		m_texture = texture;
    }

private:
	D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;
    std::shared_ptr<Buffer> vertexBufferHandle;
    PixelBuffer* m_texture = nullptr;

    ComPtr<ID3D12RootSignature> debugRootSignature;
    ComPtr<ID3D12PipelineState> debugPSO;

    struct DebugVertex {
        XMFLOAT3 position;
        XMFLOAT2 texcoord;
    };

    // Define the vertices for the full-screen triangle
    DebugVertex fullscreenTriangleVertices[4] = {
        { XMFLOAT3(-1.0f,  1.0f, 0.0f), XMFLOAT2(0.0, 0.0)},
        { XMFLOAT3(1.0f,  1.0f, 0.0f), XMFLOAT2(1.0, 0.0) },
        { XMFLOAT3(-1.0f, -1.0f, 0.0f), XMFLOAT2(0.0, 1.0) },
        { XMFLOAT3(1.0f, -1.0f, 0.0f), XMFLOAT2(1.0, 1.0) }

    };

    // Create the vertex buffer for the full-screen triangle
    D3D12_VERTEX_BUFFER_VIEW CreateFullscreenTriangleVertexBuffer(ID3D12Device* device) {
        ComPtr<ID3D12Resource> vertexBuffer;

        const UINT vertexBufferSize = static_cast<UINT>(4 * sizeof(DebugVertex));

        // Create a default heap for the vertex buffer
        CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
        CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize);

        vertexBufferHandle = ResourceManager::GetInstance().CreateBuffer(vertexBufferSize, (void*)fullscreenTriangleVertices);
		UploadManager::GetInstance().UploadData((void*)fullscreenTriangleVertices, vertexBufferSize, vertexBufferHandle.get(), 0);

        D3D12_VERTEX_BUFFER_VIEW vertexBufferView = {};

        vertexBufferView.BufferLocation = vertexBufferHandle->m_buffer->GetGPUVirtualAddress();
        vertexBufferView.StrideInBytes = sizeof(DebugVertex);
        vertexBufferView.SizeInBytes = vertexBufferSize;

        return vertexBufferView;
    }

    void CreateDebugRootSignature() {
        CD3DX12_DESCRIPTOR_RANGE1 debugDescriptorRangeSRV;
        debugDescriptorRangeSRV.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0); // t0 in the shader

        CD3DX12_ROOT_PARAMETER1 debugRootParameters[2] = {};
        debugRootParameters[0].InitAsDescriptorTable(1, &debugDescriptorRangeSRV, D3D12_SHADER_VISIBILITY_PIXEL); // Pixel shader will use the SRV
        debugRootParameters[1].InitAsConstants(16, 0, 0, D3D12_SHADER_VISIBILITY_VERTEX); // Vertex shader will use the constant buffer

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
        samplerDesc.ShaderRegister = 0;
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

    void CreateDebugPSO() {
        // Compile shaders
        Microsoft::WRL::ComPtr<ID3DBlob> vertexShader;
        Microsoft::WRL::ComPtr<ID3DBlob> pixelShader;
        PSOManager::GetInstance().CompileShader(L"shaders/debug.hlsl", L"VSMain", L"vs_6_6", {}, vertexShader);
        PSOManager::GetInstance().CompileShader(L"shaders/debug.hlsl", L"PSMain", L"ps_6_6", {}, pixelShader);

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
        psoDesc.InputLayout = inputLayoutDesc;
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
};
