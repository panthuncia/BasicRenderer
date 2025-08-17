
#include <unordered_map>
#include <functional>

#include "RenderPasses/Base/RenderPass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "Mesh/Mesh.h"
#include "Scene/Scene.h"
#include "Resources/TextureDescription.h"
#include "Managers/Singletons/UploadManager.h"
#include "../shaders/PerPassRootConstants/bloomSampleRootConstants.h"

class BloomSamplePass : public RenderPass {
public:
    // mipIndex selects which mip is used as render target, and which is used as shader resource.
    // E.g. DownsamplePassIndex 0 will downsample from mip 0 to mip 1, and use mip 1 as the render target.
    // If isUpsample is true, it will upsample from mip 1 to mip 0.
    BloomSamplePass(unsigned int mipIndex, bool isUpsample) : m_mipIndex(mipIndex), m_isUpsample(isUpsample) {
        CreatePSO();
    }

    void DeclareResourceUsages(RenderPassBuilder* builder) override {
        if (!m_isUpsample) {
            builder->WithShaderResource(Subresources(Builtin::PostProcessing::UpscaledHDR, Mip{ m_mipIndex, 1 }))
                .WithRenderTarget(Subresources(Builtin::PostProcessing::UpscaledHDR, Mip{ m_mipIndex + 1, 1 }));
        }
        else {
            builder->WithShaderResource(Subresources(Builtin::PostProcessing::UpscaledHDR, Mip{ m_mipIndex + 1, 1 }))
                .WithRenderTarget(Subresources(Builtin::PostProcessing::UpscaledHDR, Mip{ m_mipIndex, 1 }));
        }
    }

    void Setup() override {
        m_pHDRTarget = m_resourceRegistryView->Request<PixelBuffer>(Builtin::PostProcessing::UpscaledHDR);

        RegisterSRV(Builtin::PostProcessing::UpscaledHDR, m_mipIndex + (m_isUpsample ? 1 : 0));
    }

    PassReturn Execute(RenderContext& context) override {
        auto& psoManager = PSOManager::GetInstance();
        auto& commandList = context.commandList;

        ID3D12DescriptorHeap* descriptorHeaps[] = {
            context.textureDescriptorHeap, // The texture descriptor heap
            context.samplerDescriptorHeap, // The sampler descriptor heap
        };

        commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

        unsigned int mipOffset = m_isUpsample ? 0 : 1;

        auto& rtvHandle = m_pHDRTarget->GetRTVInfo(m_mipIndex + mipOffset).cpuHandle;
        commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

        // Calculate viewport from mip level
        unsigned int width = m_pHDRTarget->GetWidth() >> (m_mipIndex + mipOffset);
        unsigned int height = m_pHDRTarget->GetHeight() >> (m_mipIndex + mipOffset);

        CD3DX12_VIEWPORT viewport = CD3DX12_VIEWPORT(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height));
        CD3DX12_RECT scissorRect = CD3DX12_RECT(0, 0, width, width);
        commandList->RSSetViewports(1, &viewport);
        commandList->RSSetScissorRects(1, &scissorRect);

        commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

        if (m_isUpsample) {
            commandList->SetPipelineState(m_upsamplePso.Get());
        }
        else {
            commandList->SetPipelineState(m_downsamplePso.Get());
        }

        auto rootSignature = psoManager.GetRootSignature();
        commandList->SetGraphicsRootSignature(rootSignature.Get());

		BindResourceDescriptorIndices(commandList, m_resourceDescriptorBindings);

        unsigned int misc[NumMiscUintRootConstants] = {};
        // misc[UintRootConstant0] = m_pHDRTarget->GetSRVInfo(m_mipIndex + (m_isUpsample ? 1 : 0)).index;
        misc[MIP_INDEX] = m_mipIndex;
        misc[MIP_WIDTH] = m_pHDRTarget->GetWidth() >> m_mipIndex;
        misc[MIP_HEIGHT] = m_pHDRTarget->GetHeight() >> m_mipIndex;
        commandList->SetGraphicsRoot32BitConstants(MiscUintRootSignatureIndex, NumMiscUintRootConstants, &misc, 0);

        float miscFloats[NumMiscFloatRootConstants] = {};
        if (m_isUpsample) {
            miscFloats[FILTER_RADIUS] = 0.001f; // Kernel size
            miscFloats[ASPECT_RATIO] = misc[MIP_WIDTH] / (float)misc[MIP_HEIGHT]; // Aspect ratio
        }
        else {
            miscFloats[SRC_TEXEL_SIZE_X] = 1.0f / misc[MIP_WIDTH]; // Texel size X
            miscFloats[SRC_TEXEL_SIZE_Y] = 1.0f / misc[MIP_HEIGHT]; // Texel size Y
        }
        commandList->SetGraphicsRoot32BitConstants(MiscFloatRootSignatureIndex, NumMiscFloatRootConstants, &miscFloats, 0);

        commandList->DrawInstanced(3, 1, 0, 0); // Fullscreen triangle
        return {};
    }

    void Cleanup(RenderContext& context) override {
        // Cleanup the render pass
    }

private:

    unsigned int m_mipIndex;
    bool m_isUpsample = false;

    ComPtr<ID3D12PipelineState> m_downsamplePso;
    ComPtr<ID3D12PipelineState> m_upsamplePso;

    std::shared_ptr<PixelBuffer> m_pHDRTarget;

	PipelineResources m_resourceDescriptorBindings;

    void CreatePSO() {
        Microsoft::WRL::ComPtr<ID3DBlob> vertexShader;
        Microsoft::WRL::ComPtr<ID3DBlob> pixelShader;

        ShaderInfoBundle shaderInfoBundle;
        shaderInfoBundle.vertexShader = { L"shaders/fullscreenVS.hlsli", L"FullscreenVSNoViewRayMain", L"vs_6_6" };
        shaderInfoBundle.pixelShader = { L"shaders/PostProcessing/bloomDownsample.hlsl", L"downsample", L"ps_6_6" };
        auto compiledBundle = PSOManager::GetInstance().CompileShaders(shaderInfoBundle);
        vertexShader = compiledBundle.vertexShader;
        pixelShader = compiledBundle.pixelShader;
		m_resourceDescriptorBindings = compiledBundle.resourceDescriptorSlots;

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
        if (m_isUpsample) {
            blendDesc.AlphaToCoverageEnable = FALSE;
            blendDesc.IndependentBlendEnable = FALSE;

            D3D12_RENDER_TARGET_BLEND_DESC& rt0 = blendDesc.RenderTarget[0];
            rt0.BlendEnable = TRUE;                  // turn on blending
            rt0.LogicOpEnable = FALSE;                 // we’re not using logic ops
            rt0.SrcBlend = D3D12_BLEND_ONE;       // use source color * 1
            rt0.DestBlend = D3D12_BLEND_ONE;       // use dest color * 1
            rt0.BlendOp = D3D12_BLEND_OP_ADD;    // add them: out = src + dst

            rt0.SrcBlendAlpha = D3D12_BLEND_ONE;
            rt0.DestBlendAlpha = D3D12_BLEND_ZERO;
            rt0.BlendOpAlpha = D3D12_BLEND_OP_ADD;
            rt0.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        }
        else {
            blendDesc.AlphaToCoverageEnable = FALSE;
            blendDesc.IndependentBlendEnable = FALSE;
            blendDesc.RenderTarget[0].BlendEnable = FALSE;
            blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
            blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
            blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
            blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
            blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
            blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
            blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        }

        D3D12_DEPTH_STENCIL_DESC depthStencilDesc = {};
        depthStencilDesc.DepthEnable = false;
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

        auto& device = DeviceManager::GetInstance().GetDevice();
        auto hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_downsamplePso));
        if (FAILED(hr)) {
            throw std::runtime_error("Failed to create upsample PSO");
        }

        // Upsample
        //PSOManager::GetInstance().CompileShader(L"shaders/PostProcessing/bloom.hlsl", L"upsample", L"ps_6_6", {}, pixelShader);
        shaderInfoBundle.pixelShader = { L"shaders/PostProcessing/bloomUpsample.hlsl", L"upsample", L"ps_6_6" };
        compiledBundle = PSOManager::GetInstance().CompileShaders(shaderInfoBundle);
        pixelShader = compiledBundle.pixelShader;

        psoDesc.PS = { pixelShader->GetBufferPointer(), pixelShader->GetBufferSize() };
        hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_upsamplePso));
        if (FAILED(hr)) {
            throw std::runtime_error("Failed to create bloom upsample PSO");
        }
    }
};