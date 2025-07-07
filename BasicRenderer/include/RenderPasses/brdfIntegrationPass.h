#pragma once

#include "RenderPasses/Base/RenderPass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"

class BRDFIntegrationPass : public RenderPass {
public:
    BRDFIntegrationPass() {
    }

    void DeclareResourceUsages(RenderPassBuilder* builder) override {
        builder->WithRenderTarget(Builtin::BRDFLUT);
    }

    void Setup() override {

		m_lutTexture = m_resourceRegistryView->Request<PixelBuffer>(Builtin::BRDFLUT);
        CreatePSO();
    }

    PassReturn Execute(RenderContext& context) override {
        auto& psoManager = PSOManager::GetInstance();
        auto& commandList = context.commandList;
        ID3D12DescriptorHeap* descriptorHeaps[] = {
            context.textureDescriptorHeap, // The texture descriptor heap
            context.samplerDescriptorHeap, // The sampler descriptor heap
        };

        commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
        CD3DX12_VIEWPORT viewport(0.0f, 0.0f, 512, 512);
        CD3DX12_RECT scissorRect(0, 0, 512, 512);
        commandList->RSSetViewports(1, &viewport);
        commandList->RSSetScissorRects(1, &scissorRect);

        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_lutTexture->GetRTVInfo(0).cpuHandle;
        commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

        commandList->SetPipelineState(PSO.Get());
        commandList->SetGraphicsRootSignature(PSOManager::GetInstance().GetRootSignature().Get());

        commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        commandList->DrawInstanced(3, 1, 0, 0); // Fullscreen triangle

        invalidated = false;

        return { };
    }

    void Cleanup(RenderContext& context) override {
        // Cleanup if necessary
    }

private:
    std::shared_ptr<PixelBuffer> m_lutTexture = nullptr;

    ComPtr<ID3D12PipelineState> PSO;

    void CreatePSO() {
        // Compile shaders
        Microsoft::WRL::ComPtr<ID3DBlob> vertexShader;
        Microsoft::WRL::ComPtr<ID3DBlob> pixelShader;
		ShaderInfoBundle shaderInfoBundle;
		shaderInfoBundle.vertexShader = { L"shaders/fullscreenVS.hlsli", L"FullscreenVSMain", L"vs_6_6" };
		shaderInfoBundle.pixelShader = { L"shaders/brdfIntegration.hlsl", L"PSMain", L"ps_6_6" };
		auto compiledBundle = PSOManager::GetInstance().CompileShaders(shaderInfoBundle);
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

        DXGI_FORMAT renderTargetFormat = DXGI_FORMAT_R16G16_FLOAT;

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
        psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
        psoDesc.SampleDesc.Count = 1;
        psoDesc.SampleDesc.Quality = 0;
        psoDesc.InputLayout = inputLayoutDesc;

        auto& device = DeviceManager::GetInstance().GetDevice();
        auto hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&PSO));
        if (FAILED(hr)) {
            throw std::runtime_error("Failed to create debug PSO");
        }
    }
};