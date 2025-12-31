#pragma once

#include <functional>

#include "RenderPasses/Base/RenderPass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "Scene/Scene.h"

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

    void Setup() override {
        m_pHDRTarget = m_resourceRegistryView->RequestPtr<PixelBuffer>(Builtin::Color::HDRColorTarget);
        
        RegisterSRV(Builtin::Environment::InfoBuffer);

        if (m_gtaoEnabled)
			RegisterSRV(Builtin::GTAO::OutputAOTerm);

		RegisterSRV(Builtin::GBuffer::Normals);
		RegisterSRV(Builtin::GBuffer::Albedo);
		RegisterSRV(Builtin::GBuffer::Emissive);
		RegisterSRV(Builtin::GBuffer::MetallicRoughness);
		RegisterSRV(Builtin::PrimaryCamera::DepthTexture);
		RegisterSRV(Builtin::CameraBuffer);
		RegisterSRV(Builtin::PostProcessing::ScreenSpaceReflections);
    }

    PassReturn Execute(RenderContext& context) override {
        auto& psoManager = PSOManager::GetInstance();
        auto& commandList = context.commandList;

		commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());

		rhi::PassBeginInfo passInfo{};
		rhi::ColorAttachment colorAttachment{};
		colorAttachment.rtv = m_pHDRTarget->GetRTVInfo(0).slot;
		colorAttachment.loadOp = rhi::LoadOp::Load;
		colorAttachment.storeOp = rhi::StoreOp::Store;
		passInfo.colors = { &colorAttachment };
		passInfo.width = context.renderResolution.x;
		passInfo.height = context.renderResolution.y;
		passInfo.debugName = "Specular IBL Pass";
		commandList.BeginPass(passInfo);

        commandList.SetPrimitiveTopology(rhi::PrimitiveTopology::TriangleStrip);

        commandList.BindLayout(PSOManager::GetInstance().GetRootSignature().GetHandle());
        commandList.BindPipeline(m_pso->GetHandle());

        unsigned int settings[NumSettingsRootConstants] = {};
        settings[EnableGTAO] = m_gtaoEnabled;
		commandList.PushConstants(rhi::ShaderStage::AllGraphics, 0, SettingsRootSignatureIndex, 0, NumSettingsRootConstants, settings);

        BindResourceDescriptorIndices(commandList, m_resourceDescriptorBindings);

        commandList.Draw(3, 1, 0, 0); // Fullscreen triangle
        return {};
    }

    void Cleanup(RenderContext& context) override {
        // Cleanup the render pass
    }

private:

    rhi::PipelinePtr m_pso;

    PixelBuffer* m_pHDRTarget;
    PipelineResources m_resourceDescriptorBindings;

    bool m_gtaoEnabled = true;

    void CreatePSO() {
        auto dev = DeviceManager::GetInstance().GetDevice();

        // Compile shaders
        ShaderInfoBundle sib;
        sib.vertexShader = { L"shaders/fullscreenVS.hlsli", L"FullscreenVSMain", L"vs_6_6" };
        sib.pixelShader = { L"shaders/specularIBL.hlsl",   L"PSMain",           L"ps_6_6" };
        auto compiled = PSOManager::GetInstance().CompileShaders(sib);
        m_resourceDescriptorBindings = compiled.resourceDescriptorSlots;

        // Subobjects
        auto& layout = PSOManager::GetInstance().GetRootSignature(); // rhi::PipelineLayout&
        rhi::SubobjLayout soLayout{ layout.GetHandle() };
        rhi::SubobjShader soVS{ rhi::ShaderStage::Vertex, rhi::DXIL(compiled.vertexShader.Get()) };
        rhi::SubobjShader soPS{ rhi::ShaderStage::Pixel,  rhi::DXIL(compiled.pixelShader.Get()) };

        rhi::RasterState rs{};
        rs.fill = rhi::FillMode::Solid;
        rs.cull = rhi::CullMode::None;
        rs.frontCCW = false;
        rhi::SubobjRaster soRaster{ rs };

        rhi::BlendState bs{};
        bs.alphaToCoverage = false;
        bs.independentBlend = false;
        bs.numAttachments = 1;
        {
            auto& a0 = bs.attachments[0];
            a0.enable = true;
            a0.srcColor = rhi::BlendFactor::One;
            a0.dstColor = rhi::BlendFactor::One;
            a0.colorOp = rhi::BlendOp::Add;

            a0.srcAlpha = rhi::BlendFactor::Zero;
            a0.dstAlpha = rhi::BlendFactor::One;
            a0.alphaOp = rhi::BlendOp::Add;

            a0.writeMask = rhi::ColorWriteEnable::All;
        }
        rhi::SubobjBlend soBlend{ bs };

        rhi::DepthStencilState ds{};
        ds.depthEnable = false;
        ds.depthWrite = false;
        ds.depthFunc = rhi::CompareOp::Greater; // kept for parity; ignored when depth off
        rhi::SubobjDepth soDepth{ ds };

        rhi::RenderTargets rts{};
        rts.count = 1;
        rts.formats[0] = rhi::Format::R16G16B16A16_Float;
        rhi::SubobjRTVs soRTVs{ rts };

        rhi::SubobjDSV    soDSV{ rhi::Format::Unknown }; // no DSV
        rhi::SubobjSample soSmp{ rhi::SampleDesc{1, 0} };

        const rhi::PipelineStreamItem items[] = {
            rhi::Make(soLayout),
            rhi::Make(soVS),
            rhi::Make(soPS),
            rhi::Make(soRaster),
            rhi::Make(soBlend),
            rhi::Make(soDepth),
            rhi::Make(soRTVs),
            rhi::Make(soDSV),
            rhi::Make(soSmp),
        };

        auto result = dev.CreatePipeline(items, (uint32_t)std::size(items), m_pso);
        if (Failed(result)) {
            throw std::runtime_error("Failed to create SpecularIBL PSO (RHI)");
        }
        m_pso->SetName("SpecularIBL.PSO");
    }
};