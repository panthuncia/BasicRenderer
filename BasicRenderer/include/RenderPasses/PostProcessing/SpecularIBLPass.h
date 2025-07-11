#pragma once

#include <unordered_map>
#include <functional>

#include "RenderPasses/Base/RenderPass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "Mesh/Mesh.h"
#include "Scene/Scene.h"
#include "Resources/TextureDescription.h"

class SpecularIBLPass : public RenderPass {
public:
    SpecularIBLPass() {
        CreatePSO();
        auto& settingsManager = SettingsManager::GetInstance();
        m_gtaoEnabled = settingsManager.getSettingGetter<bool>("enableGTAO")();
    }

    void DeclareResourceUsages(RenderPassBuilder* builder) {
        builder->WithShaderResource(Builtin::PostProcessing::ScreenSpaceReflections, 
            Builtin::Environment::InfoBuffer,
            Builtin::GBuffer::Normals,
            Builtin::GBuffer::Albedo,
            Builtin::GBuffer::Emissive,
            Builtin::GBuffer::MetallicRoughness,
            Builtin::PrimaryCamera::DepthTexture,
            Builtin::CameraBuffer)
            .WithRenderTarget(Builtin::Color::HDRColorTarget);

        if (m_gtaoEnabled) {
            builder->WithShaderResource(Builtin::GTAO::OutputAOTerm);
        }
    }

    void Setup(const ResourceRegistryView& resourceRegistryView) override {
        m_pHDRTarget = resourceRegistryView.Request<PixelBuffer>(Builtin::Color::HDRColorTarget);
		m_pScreenSpaceReflections = resourceRegistryView.Request<PixelBuffer>(Builtin::PostProcessing::ScreenSpaceReflections);
        m_environmentBufferDescriptorIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::Environment::InfoBuffer)->GetSRVInfo(0).index;
        
        if (m_gtaoEnabled)
            m_aoTextureSRVIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::GTAO::OutputAOTerm)->GetSRVInfo(0).index;

        m_normalsTextureSRVIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::GBuffer::Normals)->GetSRVInfo(0).index;
        m_albedoTextureSRVIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::GBuffer::Albedo)->GetSRVInfo(0).index;
        m_metallicRoughnessTextureSRVIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::GBuffer::MetallicRoughness)->GetSRVInfo(0).index;
        m_emissiveTextureSRVIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::GBuffer::Emissive)->GetSRVInfo(0).index;
        m_depthBufferSRVIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::PrimaryCamera::DepthTexture)->GetSRVInfo(0).index;
		m_cameraBufferSRVIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::CameraBuffer)->GetSRVInfo(0).index;
    }

    PassReturn Execute(RenderContext& context) override {
        auto& psoManager = PSOManager::GetInstance();
        auto& commandList = context.commandList;

        ID3D12DescriptorHeap* descriptorHeaps[] = {
            context.textureDescriptorHeap, // The texture descriptor heap
            context.samplerDescriptorHeap, // The sampler descriptor heap
        };

        commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_pHDRTarget->GetRTVInfo(0).cpuHandle);
        commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

        CD3DX12_VIEWPORT viewport = CD3DX12_VIEWPORT(0.0f, 0.0f, context.renderResolution.x, context.renderResolution.y);
        CD3DX12_RECT scissorRect = CD3DX12_RECT(0, 0, context.renderResolution.x, context.renderResolution.y);
        commandList->RSSetViewports(1, &viewport);
        commandList->RSSetScissorRects(1, &scissorRect);

        commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

        commandList->SetPipelineState(m_pso.Get());
        auto rootSignature = psoManager.GetRootSignature();
        commandList->SetGraphicsRootSignature(rootSignature.Get());

        unsigned int settings[NumSettingsRootConstants] = {};
        settings[EnableGTAO] = m_gtaoEnabled;
        commandList->SetGraphicsRoot32BitConstants(SettingsRootSignatureIndex, NumSettingsRootConstants, &settings, 0);

        unsigned int staticBufferIndices[NumStaticBufferRootConstants] = {};
        staticBufferIndices[EnvironmentBufferDescriptorIndex] = m_environmentBufferDescriptorIndex;
        staticBufferIndices[AOTextureDescriptorIndex] = m_aoTextureSRVIndex;
        staticBufferIndices[NormalsTextureDescriptorIndex] = m_normalsTextureSRVIndex;
        staticBufferIndices[AlbedoTextureDescriptorIndex] = m_albedoTextureSRVIndex;
        staticBufferIndices[MetallicRoughnessTextureDescriptorIndex] = m_metallicRoughnessTextureSRVIndex;
        staticBufferIndices[EmissiveTextureDescriptorIndex] = m_emissiveTextureSRVIndex;
		staticBufferIndices[CameraBufferDescriptorIndex] = m_cameraBufferSRVIndex;

        commandList->SetGraphicsRoot32BitConstants(StaticBufferRootSignatureIndex, NumStaticBufferRootConstants, &staticBufferIndices, 0);

        unsigned int misc[NumMiscUintRootConstants] = {};
        //misc[0] = m_pHDRTarget->GetUAVShaderVisibleInfo(0).index;
		misc[UintRootConstant0] = m_pScreenSpaceReflections->GetSRVInfo(0).index;
		misc[UintRootConstant1] = m_depthBufferSRVIndex;

        commandList->SetGraphicsRoot32BitConstants(MiscUintRootSignatureIndex, NumMiscUintRootConstants, &misc, 0);

        commandList->DrawInstanced(3, 1, 0, 0); // Fullscreen triangle
        return {};
    }

    void Cleanup(RenderContext& context) override {
        // Cleanup the render pass
    }

private:

    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pso;

    std::shared_ptr<PixelBuffer> m_pHDRTarget;
	std::shared_ptr<PixelBuffer> m_pScreenSpaceReflections;

    int m_environmentBufferDescriptorIndex = -1;
    int m_aoTextureSRVIndex = -1;
    int m_normalsTextureSRVIndex = -1;
    int m_albedoTextureSRVIndex = -1;
    int m_metallicRoughnessTextureSRVIndex = -1;
    int m_emissiveTextureSRVIndex = -1;
    int m_depthBufferSRVIndex = -1;
	int m_cameraBufferSRVIndex = -1;

    bool m_gtaoEnabled = true;

    void CreatePSO() {
        Microsoft::WRL::ComPtr<ID3DBlob> vertexShader;
        Microsoft::WRL::ComPtr<ID3DBlob> pixelShader;
        PSOManager::GetInstance().CompileShader(L"shaders/fullscreenVS.hlsli", L"FullscreenVSMain", L"vs_6_6", {}, vertexShader);
        PSOManager::GetInstance().CompileShader(L"shaders/specularIBL.hlsl", L"PSMain", L"ps_6_6", {}, pixelShader);


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
        blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
        blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
        blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ZERO;
        blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
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
        //psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
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