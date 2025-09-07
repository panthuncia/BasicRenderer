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
        auto& commandList = context.commandList;
        
		commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());

		rhi::PassBeginInfo passInfo{};
		rhi::ColorAttachment colorAttachment{};
		colorAttachment.rtv = m_lutTexture->GetRTVInfo(0).slot;
		colorAttachment.loadOp = rhi::LoadOp::Clear;
		colorAttachment.storeOp = rhi::StoreOp::Store;
		colorAttachment.clear = m_lutTexture->GetClearColor();
		passInfo.colors = { &colorAttachment, 1 };
		passInfo.width = 512;
		passInfo.height = 512;
		passInfo.debugName = "BRDF Integration Pass";
		commandList.BeginPass(passInfo);

		commandList.BindLayout(PSOManager::GetInstance().GetRootSignature().GetHandle());
		commandList.BindPipeline(PSO->GetHandle());

        commandList.SetPrimitiveTopology(rhi::PrimitiveTopology::TriangleStrip);
        commandList.Draw(3, 1, 0, 0); // Fullscreen triangle

        invalidated = false;

        return { };
    }

    void Cleanup(RenderContext& context) override {
        // Cleanup if necessary
    }

private:
    std::shared_ptr<PixelBuffer> m_lutTexture = nullptr;

    rhi::PipelinePtr PSO;

    void CreatePSO() {
        auto dev = DeviceManager::GetInstance().GetDevice();

        // Compile shaders
        ShaderInfoBundle sib;
        sib.vertexShader = { L"shaders/fullscreenVS.hlsli", L"FullscreenVSMain", L"vs_6_6" };
        sib.pixelShader = { L"shaders/brdfIntegration.hlsl", L"PSMain", L"ps_6_6" };
        auto compiled = PSOManager::GetInstance().CompileShaders(sib);

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
        bs.attachments[0].enable = false;                    // no blending
        bs.attachments[0].writeMask = rhi::ColorWriteEnable::All;
        rhi::SubobjBlend soBlend{ bs };

        rhi::DepthStencilState ds{};
        ds.depthEnable = false;      // depth disabled (write mask ignored)
        ds.depthWrite = false;
        ds.depthFunc = rhi::CompareOp::Less;
        rhi::SubobjDepth soDepth{ ds };

        rhi::RenderTargets rts{};
        rts.count = 1;
        rts.formats[0] = rhi::Format::R16G16_Float;
        rhi::SubobjRTVs soRTVs{ rts };

        rhi::SubobjSample soSmp{ rhi::SampleDesc{1, 0} };

        const rhi::PipelineStreamItem items[] = {
            rhi::Make(soLayout),
            rhi::Make(soVS),
            rhi::Make(soPS),
            rhi::Make(soRaster),
            rhi::Make(soBlend),
            rhi::Make(soDepth),
            rhi::Make(soRTVs),
            rhi::Make(soSmp),
        };

        PSO = dev.CreatePipeline(items, (uint32_t)std::size(items));
        if (!PSO || !PSO->IsValid()) {
            throw std::runtime_error("Failed to create BRDF integration PSO (RHI)");
        }
        PSO->SetName("BRDFIntegration.PSO");
    }
};