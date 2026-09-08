#pragma once

#include <unordered_map>
#include <functional>

#include <spdlog/spdlog.h>

#include "RenderPasses/Base/TypedRenderGraphPass.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "Scene/Scene.h"
#include "Materials/colorspaces.h"
#include "RenderPasses/PreparedFullscreenDraw.h"

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

class TonemappingPass : public org::TypedRenderGraphPass<
    TonemappingPass, br::render::PreparedFullscreenDraw> {
public:
	explicit TonemappingPass(bool bloomEnabled = false)
        : m_bloomEnabled(bloomEnabled) {
		CreatePSO();
		getTonemapType = SettingsManager::GetInstance().getSettingGetter<unsigned int>("tonemapType");
        m_pLPMConstants = LazyDynamicStructuredBuffer<LPMConstants>::CreateShared(1, "AMD LPM constants", 1, true);
	}

    std::shared_ptr<Resource> ProvideResource(ResourceIdentifier const& key) override {
        if (key == m_providedResources[0]) {
			return m_pLPMConstants;
        }
		return nullptr;
    }
    std::vector<ResourceIdentifier> GetSupportedKeys() override {
		return m_providedResources;
    }

    void Declare(org::PassBuilder& builder) {
        builder.WithShaderResource(Builtin::PostProcessing::UpscaledHDR, Builtin::CameraBuffer, "FFX::LPMConstants")
            .WithRenderTarget(Builtin::Backbuffer);
        if (m_bloomEnabled) {
            builder.WithShaderResource(Subresources(Builtin::PostProcessing::BloomTexture, Mip{ 1, 2 }));
        }
		builder.WithConstantBuffer(Builtin::PerFrameBuffer);
    }

	void Initialize() {
        if (m_bloomEnabled) {
            m_pBloomTarget = m_resourceRegistryView->RequestPtr<PixelBuffer>(
                Builtin::PostProcessing::BloomTexture);
        }

        LPMConstants lpmConstants = {};
        
        lpmConstants.shoulder = true;
        lpmConstants.con = false;
        lpmConstants.soft = false;
        lpmConstants.con2 = false;
        lpmConstants.clip = true;
        lpmConstants.scaleOnly = false;
        
        // Rest will be filled in by the luminanceHistogramAverage shader

        BUFFER_UPLOAD(&lpmConstants, sizeof(LPMConstants), org::runtime::UploadTarget::FromShared(m_pLPMConstants), 0);
    }

	br::render::PreparedFullscreenDraw Prepare(const org::PassPrepareContext& preparation) {
		const auto* context = preparation.preparationData->Get<UpdateContext>();
		br::render::PreparedFullscreenDraw data{};
		data.resourceHeap = context->textureDescriptorHeap.GetHandle(); data.samplerHeap = context->samplerDescriptorHeap.GetHandle();
		data.externalRenderTarget = org::ExternalBindingKey::SwapchainColor;
		data.loadOp = rhi::LoadOp::Clear;
		data.clear.rgba[3] = 1.0f; data.width = context->outputResolution.x; data.height = context->outputResolution.y;
		data.layout = PSOManager::GetInstance().GetRootSignature().GetHandle();
        br::render::BindPreparedProgram(
            data, preparation, m_pso, m_resourceDescriptorBindings);
		data.constants[LPM_CONSTANTS_BUFFER_SRV_DESCRIPTOR_INDEX] = m_pLPMConstants->GetSRVInfo(0).slot.index;
		data.constants[TONEMAP_TYPE] = getTonemapType(); data.constants[TONEMAP_BLOOM_ENABLED] = m_bloomEnabled ? 1u : 0u;
		if (m_bloomEnabled) {
			data.constants[TONEMAP_BLOOM_MIP1_SRV_DESCRIPTOR_INDEX] = m_pBloomTarget->GetSRVInfo(1).slot.index;
			data.constants[TONEMAP_BLOOM_MIP2_SRV_DESCRIPTOR_INDEX] = m_pBloomTarget->GetSRVInfo(2).slot.index;
			data.constants[TONEMAP_BLOOM_FILTER_RADIUS] = as_uint(0.001f);
			data.constants[TONEMAP_BLOOM_ASPECT_RATIO] = as_uint(context->outputResolution.x / static_cast<float>(context->outputResolution.y));
		}
		return data;
	}

    static void Record(const br::render::PreparedFullscreenDraw& data,
        org::PassRecordContext& recording) {
        br::render::RecordPreparedFullscreenDraw(data, recording);
    }

private:

    std::shared_ptr<rhi::PipelinePtr> m_pso;
    PipelineResources m_resourceDescriptorBindings;

    std::shared_ptr<LazyDynamicStructuredBuffer<LPMConstants>> m_pLPMConstants;

    std::function<unsigned int()> getTonemapType;
    bool m_bloomEnabled = false;
    PixelBuffer* m_pBloomTarget = nullptr;

    std::vector<ResourceIdentifier> m_providedResources = {
		"FFX::LPMConstants"
	};

    void CreatePSO() {
        auto dev = DeviceManager::GetInstance().GetDevice();

        // Compile shaders
        ShaderInfoBundle sib;
        sib.vertexShader = { L"shaders/fullscreenVS.hlsli", L"FullscreenVSNoViewRayMain", L"vs_6_6" };
        sib.pixelShader = { L"shaders/PostProcessing/tonemapping.hlsl", L"PSMain", L"ps_6_6" };
        auto compiled = PSOManager::GetInstance().CompileShaders(sib);
        m_resourceDescriptorBindings = compiled.resourceDescriptorSlots;

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
        {
            auto& a0 = bs.attachments[0];
			a0.enable = false;
            a0.srcColor = rhi::BlendFactor::SrcAlpha;
            a0.dstColor = rhi::BlendFactor::InvSrcAlpha;
            a0.colorOp = rhi::BlendOp::Add;
            a0.srcAlpha = rhi::BlendFactor::One;
            a0.dstAlpha = rhi::BlendFactor::InvSrcAlpha;
            a0.alphaOp = rhi::BlendOp::Add;
            a0.writeMask = rhi::ColorWriteEnable::All;
        }
        rhi::SubobjBlend soBlend{ bs };

        rhi::DepthStencilState ds{};
        ds.depthEnable = false;
        ds.depthWrite = false;
        ds.depthFunc = rhi::CompareOp::Greater;
        rhi::SubobjDepth soDepth{ ds };

        rhi::RenderTargets rts{};
        rts.count = 1;
        rts.formats[0] = rhi::Format::R8G8B8A8_UNorm;
        rhi::SubobjRTVs soRTVs{ rts };

        rhi::SubobjDSV    soDSV{ rhi::Format::D32_Float };
        rhi::SubobjSample soSmp{ rhi::SampleDesc{1, 0} };
        rhi::SubobjPrimitiveTopology soTopo{ rhi::PrimitiveTopology::TriangleStrip };

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
			rhi::Make(soTopo)
        };

        rhi::PipelinePtr pipeline;
        auto result = dev.CreatePipeline(items, (uint32_t)std::size(items), pipeline);
        if (Failed(result)) {
            throw std::runtime_error("Failed to create tonemapping PSO (RHI)");
        }
        pipeline->SetName("Tonemapping.PSO");
        m_pso = std::make_shared<rhi::PipelinePtr>(std::move(pipeline));
    }
};
