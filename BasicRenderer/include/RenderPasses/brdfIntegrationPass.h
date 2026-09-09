#pragma once

#include "RenderPasses/Base/TypedRenderGraphPass.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "RenderPasses/PreparedFullscreenDraw.h"

#include <string>

class BRDFIntegrationPass
    : public org::TypedRenderGraphPass<BRDFIntegrationPass,
          br::render::PreparedFullscreenDraw> {
public:
    BRDFIntegrationPass() {
        CreatePSO();
    }

    void Declare(org::PassBuilder& builder) {
        m_lutBinding = builder.BindRenderTarget(ResourceIdentifier{Builtin::BRDFLUT});
    }

    void Initialize() {
		m_lutTexture = m_resourceRegistryView->RequestPtr<PixelBuffer>(Builtin::BRDFLUT);
    }

    br::render::PreparedFullscreenDraw Prepare(const org::PassPrepareContext& preparation) {
        br::render::PreparedFullscreenDraw data{};
        data.renderTargetReference = preparation.CaptureDescriptor(
            m_lutBinding, m_lutTexture->GetRTVInfo(0).slot);
        data.loadOp = rhi::LoadOp::Clear;
        data.clear = m_lutTexture->GetClearColor(); data.width = 512; data.height = 512;
        data.debugName = "BRDF Integration Pass";
        br::render::BindPreparedProgram(
            data, preparation, PSO);
        invalidated = false;
        return data;
    }

    static void Record(const br::render::PreparedFullscreenDraw& data,
        org::PassRecordContext& recording) {
        br::render::RecordPreparedFullscreenDraw(data, recording);
    }

private:
    PixelBuffer* m_lutTexture = nullptr;
    org::ResourceBindingToken m_lutBinding;

    PipelineState PSO;

    void CreatePSO() {
        auto dev = DeviceManager::GetInstance().GetDevice();

        // Compile shaders
        ShaderInfoBundle sib;
        sib.vertexShader = { L"shaders/fullscreenVS.hlsli", L"FullscreenVSNoViewRayMain", L"vs_6_6" };
        sib.pixelShader = { L"shaders/brdfIntegration.hlsl", L"PSMain", L"ps_6_6" };
        auto compiled = PSOManager::GetInstance().CompileShaders(sib);

        // Subobjects
        auto& layout = PSOManager::GetInstance().GetRootSignature(); // rhi::PipelineLayout&
        rhi::SubobjLayout soLayout{ layout.GetHandle() };
        rhi::SubobjShader soVS{ rhi::ShaderStage::Vertex, rhi::DXIL(compiled.vertexShader.Get()), "FullscreenVSNoViewRayMain" };
        rhi::SubobjShader soPS{ rhi::ShaderStage::Pixel,  rhi::DXIL(compiled.pixelShader.Get()), "PSMain" };

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

        rhi::SubobjDSV soDSV{ rhi::Format::D32_Float };
        rhi::SubobjSample soSmp{ rhi::SampleDesc{1, 0} };
		rhi::SubobjPrimitiveTopology soTopo{ rhi::PrimitiveTopology::TriangleStrip };

        const rhi::PipelineStreamItem items[] = {
            rhi::Make(soLayout),
            rhi::Make(soVS),
            rhi::Make(soPS),
			rhi::Make(soTopo),
            rhi::Make(soRaster),
            rhi::Make(soBlend),
            rhi::Make(soDepth),
            rhi::Make(soRTVs),
            rhi::Make(soDSV),
            rhi::Make(soSmp),
        };

        rhi::PipelinePtr pipeline;
        auto result = dev.CreatePipeline(items, (uint32_t)std::size(items), pipeline);
        if (Failed(result)) {
            throw std::runtime_error(
                std::string("Failed to create BRDF integration PSO (RHI): ") +
                rhi::ResultName(result) +
                " (" +
                std::to_string(static_cast<uint32_t>(result)) +
                ")");
        }
        pipeline->SetName("BRDFIntegration.PSO");
        PSO = PipelineState(std::move(pipeline), compiled.resourceIDsHash,
            compiled.resourceDescriptorSlots, PSOManager::GetInstance().CaptureLayoutOwner(soLayout.layout),
            soLayout.layout);
    }
};
