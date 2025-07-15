#pragma once

#include <unordered_map>
#include <functional>

#include "RenderPasses/Base/RenderPass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "Mesh/Mesh.h"
#include "Scene/Scene.h"
#include "Resources/TextureDescription.h"
#include "Managers/Singletons/UploadManager.h"
#include "Colorspaces.h"

#include "../shaders/FidelityFX/ffx_a.h"
A_STATIC AF1 fs2S;
A_STATIC AF1 hdr10S;
A_STATIC AU1 ctl[24 * 4];

A_STATIC void LpmSetupOut(AU1 i, inAU4 v)
{
    for (int j = 0; j < 4; ++j) { ctl[i * 4 + j] = v[j]; }
}
#include "../shaders/FidelityFX/ffx_lpm.h"
#include "../shaders/PerPassRootConstants/tonemapRootConstants.h"

class TonemappingPass : public RenderPass {
public:
	TonemappingPass() {
		CreatePSO();
		getTonemapType = SettingsManager::GetInstance().getSettingGetter<unsigned int>("tonemapType");
	}

    void DeclareResourceUsages(RenderPassBuilder* builder) {
        builder->WithShaderResource(Builtin::PostProcessing::UpscaledHDR, Builtin::CameraBuffer);
    }

	void Setup() override {

        m_pLPMConstants = ResourceManager::GetInstance().CreateIndexedLazyDynamicStructuredBuffer<LPMConstants>(1, L"AMD LPM constants");
        LPMConstants lpmConstants = {};
        
        lpmConstants.shoulder = true;
        lpmConstants.con = false;
        lpmConstants.soft = false;
        lpmConstants.con2 = false;
        lpmConstants.clip = true;
        lpmConstants.scaleOnly = false;
		float scaleC = 1.0f;
		float softGap = 0.001f;
		float hdrMax = 400.0f; // TODO : replace with actual HDR max luminance
        
		float Lk = 0.5f;//computeKeyLuminance(); // TODO: compute key luminance from HDR input
        float exposure = log2(hdrMax * 0.18f / std::max(Lk, 1e-5f));
        float contrast = 0.0f;
        float shoulderContrast = 1.0f;

		float saturation[3] = { 0.0f, 0.0f, 0.0f };
		float crosstalk[3] = { 1.0f, 1.0f, 1.0f };

        LpmSetup(lpmConstants.shoulder, lpmConstants.con, lpmConstants.soft, lpmConstants.con2, lpmConstants.clip, lpmConstants.scaleOnly,
            (float*)&Rec709.r, (float*)&Rec709.g, (float*)&Rec709.b, (float*)&Rec709.D65,
            (float*)&Rec709.r, (float*)&Rec709.g, (float*)&Rec709.b, (float*)&Rec709.D65,
            (float*)&Rec709.r, (float*)&Rec709.g, (float*)&Rec709.b, (float*)&Rec709.D65,
            scaleC, softGap, hdrMax, exposure, contrast, shoulderContrast, (float*)&saturation, (float*)&crosstalk);

		memcpy(lpmConstants.u_ctl, ctl, sizeof(ctl));

		UploadManager::GetInstance().UploadData(&lpmConstants, sizeof(LPMConstants), m_pLPMConstants.get(), 0);

        RegisterSRV(Builtin::PostProcessing::UpscaledHDR);
		RegisterSRV(Builtin::CameraBuffer);
    }

	PassReturn Execute(RenderContext& context) override {
		auto& psoManager = PSOManager::GetInstance();
		auto& commandList = context.commandList;

		ID3D12DescriptorHeap* descriptorHeaps[] = {
			context.textureDescriptorHeap, // The texture descriptor heap
			context.samplerDescriptorHeap, // The sampler descriptor heap
		};

		commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(context.rtvHeap->GetCPUDescriptorHandleForHeapStart(), context.frameIndex, context.rtvDescriptorSize);
		commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

		CD3DX12_VIEWPORT viewport = CD3DX12_VIEWPORT(0.0f, 0.0f, context.outputResolution.x, context.outputResolution.y);
		CD3DX12_RECT scissorRect = CD3DX12_RECT(0, 0, context.outputResolution.x, context.outputResolution.y);
		commandList->RSSetViewports(1, &viewport);
		commandList->RSSetScissorRects(1, &scissorRect);

		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

		commandList->SetPipelineState(m_pso.Get());
		auto rootSignature = psoManager.GetRootSignature();
		commandList->SetGraphicsRootSignature(rootSignature.Get());

        BindResourceDescriptorIndices(commandList, m_resourceDescriptorBindings);

		unsigned int misc[NumMiscUintRootConstants] = {};
		misc[LPM_CONSTANTS_BUFFER_SRV_DESCRIPTOR_INDEX] = m_pLPMConstants->GetSRVInfo(0).index;
		misc[TONEMAP_TYPE] = getTonemapType();

		commandList->SetGraphicsRoot32BitConstants(MiscUintRootSignatureIndex, NumMiscUintRootConstants, misc, 0);

		commandList->DrawInstanced(3, 1, 0, 0); // Fullscreen triangle
		return {};
	}

	void Cleanup(RenderContext& context) override {
		// Cleanup the render pass
	}

private:

    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pso;
    PipelineResources m_resourceDescriptorBindings;

    std::shared_ptr<LazyDynamicStructuredBuffer<LPMConstants>> m_pLPMConstants;

    std::function<unsigned int()> getTonemapType;

    void CreatePSO() {
        Microsoft::WRL::ComPtr<ID3DBlob> vertexShader;
        Microsoft::WRL::ComPtr<ID3DBlob> pixelShader;
        //PSOManager::GetInstance().CompileShader(L"shaders/fullscreenVS.hlsli", L"FullscreenVSMain", L"vs_6_6", {}, vertexShader);
        //PSOManager::GetInstance().CompileShader(L"shaders/PostProcessing/tonemapping.hlsl", L"PSMain", L"ps_6_6", {}, pixelShader);
		ShaderInfoBundle shaderInfoBundle;
		shaderInfoBundle.vertexShader = { L"shaders/fullscreenVS.hlsli", L"FullscreenVSMain", L"vs_6_6" };
		shaderInfoBundle.pixelShader = { L"shaders/PostProcessing/tonemapping.hlsl", L"PSMain", L"ps_6_6" };
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
        depthStencilDesc.DepthEnable = false;
        depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_GREATER;
        depthStencilDesc.StencilEnable = FALSE;
        depthStencilDesc.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
        depthStencilDesc.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
        depthStencilDesc.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
        depthStencilDesc.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        depthStencilDesc.BackFace = depthStencilDesc.FrontFace;

        DXGI_FORMAT renderTargetFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

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
        auto hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pso));
        if (FAILED(hr)) {
            throw std::runtime_error("Failed to create debug PSO");
        }
    }
};