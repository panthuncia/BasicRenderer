#pragma once

#include "RenderPasses/Base/RenderPass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "Resources/Texture.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Managers/Singletons/UploadManager.h"

class SkyboxRenderPass : public RenderPass {
public:
    SkyboxRenderPass() {
    }

    void DeclareResourceUsages(RenderPassBuilder* builder) {
        builder->WithShaderResource(Builtin::Environment::CurrentCubemap, Builtin::Environment::InfoBuffer)
            .WithDepthReadWrite(Builtin::PrimaryCamera::DepthTexture)
            .WithRenderTarget(Builtin::Color::HDRColorTarget);
    }

    void Setup() override {
        m_vertexBufferView = CreateSkyboxVertexBuffer();
        CreateSkyboxRootSignature();
        CreateSkyboxPSO();

        m_pHDRTarget = m_resourceRegistryView->Request<PixelBuffer>(Builtin::Color::HDRColorTarget);
        m_pPrimaryDepthBuffer = m_resourceRegistryView->Request<PixelBuffer>(Builtin::PrimaryCamera::DepthTexture);

        m_environmentBufferDescriptorIndex = m_resourceRegistryView->Request<GloballyIndexedResource>(Builtin::Environment::InfoBuffer)->GetSRVInfo(0).index;
    }

    PassReturn Execute(RenderContext& context) override {

        auto& commandList = context.commandList;

        ID3D12DescriptorHeap* descriptorHeaps[] = {
            context.textureDescriptorHeap, // The texture descriptor heap
            context.samplerDescriptorHeap, // The sampler descriptor heap
        };
        commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

        commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);

        CD3DX12_VIEWPORT viewport(0.0f, 0.0f, context.renderResolution.x, context.renderResolution.y);
        CD3DX12_RECT scissorRect(0, 0, context.renderResolution.x, context.renderResolution.y);
        commandList->RSSetViewports(1, &viewport);
        commandList->RSSetScissorRects(1, &scissorRect);

        //CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(context.rtvHeap->GetCPUDescriptorHandleForHeapStart(), context.frameIndex, context.rtvDescriptorSize);
        auto rtvHandle = m_pHDRTarget->GetRTVInfo(0).cpuHandle;
        auto& dsvHandle = m_pPrimaryDepthBuffer->GetDSVInfo(0).cpuHandle;

        // Clear HDR target
        auto& clearColor = m_pHDRTarget->GetClearColor();
        commandList->ClearRenderTargetView(rtvHandle, &clearColor[0], 0, nullptr);

        commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

        commandList->SetPipelineState(skyboxPSO.Get());
        commandList->SetGraphicsRootSignature(skyboxRootSignature.Get());

        auto viewMatrix = context.currentScene->GetPrimaryCamera().get<Components::Camera>().info.view;
        viewMatrix.r[3] = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f); // Skybox has no translation
        auto viewProjectionMatrix = XMMatrixMultiply(viewMatrix, context.currentScene->GetPrimaryCamera().get<Components::Camera>().info.jitteredProjection);
        commandList->SetGraphicsRoot32BitConstants(0, 16, &viewProjectionMatrix, 0);
        commandList->SetGraphicsRoot32BitConstant(1, m_environmentBufferDescriptorIndex, 0);
        commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        commandList->DrawInstanced(36, 1, 0, 0); // Skybox cube

        return {};
    }

    void Cleanup(RenderContext& context) override {
        // Cleanup if necessary
    }

private:
    D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;
    std::shared_ptr<Buffer> vertexBufferHandle;
    //std::shared_ptr<Texture> m_texture = nullptr;

    ComPtr<ID3D12RootSignature> skyboxRootSignature;
    ComPtr<ID3D12PipelineState> skyboxPSO;

    std::shared_ptr<PixelBuffer> m_pHDRTarget = nullptr;
    std::shared_ptr<PixelBuffer> m_pPrimaryDepthBuffer = nullptr;

    int m_environmentBufferDescriptorIndex = -1;

    struct SkyboxVertex {
        XMFLOAT3 position;
    };

    // Define the vertices for the full-screen triangle
    SkyboxVertex skyboxVertices[36] = {
        XMFLOAT3{-1.0,  1.0, -1.0},
        XMFLOAT3{-1.0, -1.0, -1.0 },
        XMFLOAT3{1.0, -1.0, -1.0 },
        XMFLOAT3{1.0, -1.0, -1.0 },
        XMFLOAT3{1.0,  1.0, -1.0 },
        XMFLOAT3{-1.0,  1.0, -1.0 },

        XMFLOAT3{-1.0, -1.0,  1.0 },
        XMFLOAT3{-1.0, -1.0, -1.0 },
        XMFLOAT3{-1.0,  1.0, -1.0 },
        XMFLOAT3{-1.0,  1.0, -1.0 },
        XMFLOAT3{-1.0,  1.0,  1.0 },
        XMFLOAT3{-1.0, -1.0,  1.0 },

        XMFLOAT3{1.0, -1.0, -1.0 },
        XMFLOAT3{1.0, -1.0,  1.0 },
        XMFLOAT3{1.0,  1.0,  1.0 },
        XMFLOAT3{1.0,  1.0,  1.0 },
        XMFLOAT3{1.0,  1.0, -1.0 },
        XMFLOAT3{1.0, -1.0, -1.0 },

        XMFLOAT3{-1.0, -1.0,  1.0 },
        XMFLOAT3{-1.0,  1.0,  1.0 },
        XMFLOAT3{1.0,  1.0,  1.0 },
        XMFLOAT3{1.0,  1.0,  1.0 },
        XMFLOAT3{1.0, -1.0,  1.0 },
        XMFLOAT3{-1.0, -1.0,  1.0 },

        XMFLOAT3{-1.0,  1.0, -1.0 },
        XMFLOAT3{1.0,  1.0, -1.0 },
        XMFLOAT3{1.0,  1.0,  1.0 },
        XMFLOAT3{1.0,  1.0,  1.0 },
        XMFLOAT3{-1.0,  1.0,  1.0 },
        XMFLOAT3{-1.0,  1.0, -1.0 },

        XMFLOAT3{-1.0, -1.0, -1.0 },
        XMFLOAT3{-1.0, -1.0,  1.0 },
        XMFLOAT3{1.0, -1.0, -1.0 },
        XMFLOAT3{1.0, -1.0, -1.0 },
        XMFLOAT3{-1.0, -1.0,  1.0 },
        XMFLOAT3{1.0, -1.0,  1.0 }

    };

    D3D12_VERTEX_BUFFER_VIEW CreateSkyboxVertexBuffer() {
        ComPtr<ID3D12Resource> vertexBuffer;

        const UINT vertexBufferSize = static_cast<UINT>(36 * sizeof(SkyboxVertex));


        vertexBufferHandle = ResourceManager::GetInstance().CreateBuffer(vertexBufferSize, (void*)skyboxVertices);

        UploadManager::GetInstance().UploadData((void*)skyboxVertices, vertexBufferSize, vertexBufferHandle.get(), 0);

        D3D12_VERTEX_BUFFER_VIEW vertexBufferView = {};

        vertexBufferView.BufferLocation = vertexBufferHandle->m_buffer->GetGPUVirtualAddress();
        vertexBufferView.StrideInBytes = sizeof(SkyboxVertex);
        vertexBufferView.SizeInBytes = vertexBufferSize;

        return vertexBufferView;
    }
    void CreateSkyboxRootSignature() {

        CD3DX12_ROOT_PARAMETER1 skyboxRootParameters[2] = {};

        skyboxRootParameters[0].InitAsConstants(16, 1, 0, D3D12_SHADER_VISIBILITY_VERTEX); // modified view matrix
        skyboxRootParameters[1].InitAsConstants(1, 2, 0, D3D12_SHADER_VISIBILITY_PIXEL); // Environment buffer index

        D3D12_STATIC_SAMPLER_DESC staticSamplers[2] = {};

        // point-clamp at s0
        auto& pointClamp = staticSamplers[0];
        pointClamp.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        pointClamp.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        pointClamp.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        pointClamp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        pointClamp.MipLODBias = 0;
        pointClamp.MaxAnisotropy = 0;
        pointClamp.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        pointClamp.MinLOD = 0;
        pointClamp.MaxLOD = D3D12_FLOAT32_MAX;
        pointClamp.ShaderRegister = 0;
        pointClamp.RegisterSpace = 0;
        pointClamp.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        // linear-clamp at s1
        auto& linearClamp = staticSamplers[1];
        linearClamp = pointClamp;
        linearClamp.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        linearClamp.ShaderRegister = 1;


        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
        rootSignatureDesc.Init_1_1(_countof(skyboxRootParameters), skyboxRootParameters, 2, staticSamplers, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT | D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED | D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED);

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

    void CreateSkyboxPSO() {
        // Compile shaders
        Microsoft::WRL::ComPtr<ID3DBlob> vertexShader;
        Microsoft::WRL::ComPtr<ID3DBlob> pixelShader;
        ShaderInfoBundle shaderInfoBundle;
        shaderInfoBundle.vertexShader = { L"shaders/skybox.hlsl", L"VSMain", L"vs_6_6" };
        shaderInfoBundle.pixelShader = { L"shaders/skybox.hlsl", L"PSMain", L"ps_6_6" };
        auto compiledBundle = PSOManager::GetInstance().CompileShaders(shaderInfoBundle);
        vertexShader = compiledBundle.vertexShader;
        pixelShader = compiledBundle.pixelShader;

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

        DXGI_FORMAT renderTargetFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.InputLayout = inputLayoutDesc;
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
        psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        psoDesc.SampleDesc.Count = 1;
        psoDesc.SampleDesc.Quality = 0;
        psoDesc.InputLayout = inputLayoutDesc;

        auto& device = DeviceManager::GetInstance().GetDevice();
        auto hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&skyboxPSO));
        if (FAILED(hr)) {
            throw std::runtime_error("Failed to create skybox PSO");
        }
    }
};