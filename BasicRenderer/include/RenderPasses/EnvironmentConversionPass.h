#pragma once

# include <directx/d3d12.h>
#include <filesystem>

#include "RenderPasses/Base/RenderPass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "Resources/Texture.h"
#include "Utilities/Utilities.h"
#include "Managers/Singletons/UploadManager.h"
#include "Managers/Singletons/ReadbackManager.h"
#include "Managers/EnvironmentManager.h"

class EnvironmentConversionPass : public RenderPass {
public:
    EnvironmentConversionPass() {
        m_viewMatrices = GetCubemapViewMatrices({0.0, 0.0, 0.0});
        getSkyboxResolution = SettingsManager::GetInstance().getSettingGetter<uint16_t>("skyboxResolution");
    }

    void Setup() override {
        m_vertexBufferView = CreateSkyboxVertexBuffer();
		CreateEnvironmentConversionRootSignature();
		CreateEnvironmentConversionPSO();
    }

	// This pass was broken into multiple passes to avoid device timeout on slower GPUs
    PassReturn Execute(RenderContext& context) override {

		uint16_t skyboxRes = getSkyboxResolution();
        CD3DX12_VIEWPORT viewport(0.0f, 0.0f, skyboxRes, skyboxRes);
        CD3DX12_RECT scissorRect(0, 0, skyboxRes, skyboxRes);

        auto projection = XMMatrixPerspectiveFovRH(XM_PI / 2, 1.0, 0.1, 2.0);


		std::vector<ID3D12GraphicsCommandList*> commandLists;
		
        auto commandList = context.commandList;

        ID3D12DescriptorHeap* descriptorHeaps[] = {
            ResourceManager::GetInstance().GetSRVDescriptorHeap().Get(), // The texture descriptor heap
            ResourceManager::GetInstance().GetSamplerDescriptorHeap().Get()
        };

        commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
        commandList->SetGraphicsRootSignature(environmentConversionRootSignature.Get());
        commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
        commandList->RSSetViewports(1, &viewport);
        commandList->RSSetScissorRects(1, &scissorRect);
        commandList->SetPipelineState(environmentConversionPSO.Get());

        EnvironmentManager& manager = *context.environmentManager;
		auto environments = manager.GetAndClearEncironmentsToConvert();
        for (auto& env : environments) {
            auto texture = env->GetHDRITexture();
            commandList->SetGraphicsRootDescriptorTable(0, texture->GetBuffer()->GetSRVInfo(0).gpuHandle);

            for (int i = 0; i < 6; i++) {

                CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandles[1];
                rtvHandles[0] = env->GetEnvironmentCubemap()->GetBuffer()->GetRTVInfo(i).cpuHandle;

                commandList->OMSetRenderTargets(1, rtvHandles, FALSE, nullptr);

                auto viewMatrix = m_viewMatrices[i];
                auto viewProjectionMatrix = XMMatrixMultiply(viewMatrix, projection);
                commandList->SetGraphicsRoot32BitConstants(1, 16, &viewProjectionMatrix, 0);
                commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                commandList->DrawInstanced(36, 1, 0, 0); // Skybox cube
            }
        }

        return {};
    }

    void Cleanup(RenderContext& context) override {
        // Cleanup if necessary
    }

private:

    D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;
    std::shared_ptr<Buffer> vertexBufferHandle;
    std::array<XMMATRIX, 6> m_viewMatrices;

    std::function<uint16_t()> getSkyboxResolution;

    ComPtr<ID3D12RootSignature> environmentConversionRootSignature;
    ComPtr<ID3D12PipelineState> environmentConversionPSO;

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
    // Create the vertex buffer for the skybox
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

    void CreateEnvironmentConversionRootSignature() {
        CD3DX12_DESCRIPTOR_RANGE1 environmentDescriptorRangeSRV;
        environmentDescriptorRangeSRV.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0); // t0 in the shader

        CD3DX12_ROOT_PARAMETER1 environmentRootParameters[2] = {};
        environmentRootParameters[0].InitAsDescriptorTable(1, &environmentDescriptorRangeSRV, D3D12_SHADER_VISIBILITY_PIXEL); // Pixel shader will use the SRV
        environmentRootParameters[1].InitAsConstants(16, 1, 0, D3D12_SHADER_VISIBILITY_VERTEX); // Vertex shader will use the constant buffer (b1)

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

    void CreateEnvironmentConversionPSO() {
        // Compile shaders
        Microsoft::WRL::ComPtr<ID3DBlob> vertexShader;
        Microsoft::WRL::ComPtr<ID3DBlob> pixelShader;
        PSOManager::GetInstance().CompileShader(L"shaders/envToCubemap.hlsl", L"VSMain", L"vs_6_6", {}, vertexShader);
        PSOManager::GetInstance().CompileShader(L"shaders/envToCubemap.hlsl", L"PSMain", L"ps_6_6", {}, pixelShader);

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


        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.InputLayout = inputLayoutDesc;
        psoDesc.pRootSignature = environmentConversionRootSignature.Get();
        psoDesc.VS = { vertexShader->GetBufferPointer(), vertexShader->GetBufferSize() };
        psoDesc.PS = { pixelShader->GetBufferPointer(), pixelShader->GetBufferSize() };
        psoDesc.RasterizerState = rasterizerDesc;
        psoDesc.BlendState = blendDesc;
        psoDesc.DepthStencilState = depthStencilDesc;
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        psoDesc.SampleDesc.Count = 1;
        psoDesc.SampleDesc.Quality = 0;
        psoDesc.InputLayout = inputLayoutDesc;

        auto& device = DeviceManager::GetInstance().GetDevice();
        auto hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&environmentConversionPSO));
        if (FAILED(hr)) {
            throw std::runtime_error("Failed to create skybox PSO");
        }
    }
};
