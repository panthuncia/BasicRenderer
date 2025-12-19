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
#include "Materials/colorspaces.h"

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
        m_pLPMConstants = ResourceManager::GetInstance().CreateIndexedLazyDynamicStructuredBuffer<LPMConstants>(1, L"AMD LPM constants", 1, true);
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

    void DeclareResourceUsages(RenderPassBuilder* builder) {
        builder->WithShaderResource(Builtin::PostProcessing::UpscaledHDR, Builtin::CameraBuffer, "FFX::LPMConstants");
    }

	void Setup() override {

        LPMConstants lpmConstants = {};
        
        lpmConstants.shoulder = true;
        lpmConstants.con = false;
        lpmConstants.soft = false;
        lpmConstants.con2 = false;
        lpmConstants.clip = true;
        lpmConstants.scaleOnly = false;
        
        // Rest will be filled in by the luminanceHistogramAverage shader

        BUFFER_UPLOAD(&lpmConstants, sizeof(LPMConstants), m_pLPMConstants, 0);

        RegisterSRV(Builtin::PostProcessing::UpscaledHDR);
		RegisterSRV(Builtin::CameraBuffer);
		RegisterSRV("FFX::LPMConstants");
    }

	PassReturn Execute(RenderContext& context) override {
		auto& psoManager = PSOManager::GetInstance();
		auto& commandList = context.commandList;

		commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());

		rhi::PassBeginInfo passInfo{};
		rhi::ColorAttachment colorAttachment{};
		colorAttachment.rtv = { context.rtvHeap.GetHandle(), context.frameIndex };
		colorAttachment.loadOp = rhi::LoadOp::Load;
		colorAttachment.storeOp = rhi::StoreOp::Store;
		passInfo.colors = { &colorAttachment };
		passInfo.width = context.outputResolution.x;
		passInfo.height = context.outputResolution.y;
		commandList.BeginPass(passInfo);

		commandList.SetPrimitiveTopology(rhi::PrimitiveTopology::TriangleStrip);

		commandList.BindLayout(PSOManager::GetInstance().GetRootSignature().GetHandle());
        commandList.BindPipeline(m_pso->GetHandle());


        BindResourceDescriptorIndices(commandList, m_resourceDescriptorBindings);

		unsigned int misc[NumMiscUintRootConstants] = {};
		misc[LPM_CONSTANTS_BUFFER_SRV_DESCRIPTOR_INDEX] = m_pLPMConstants->GetSRVInfo(0).slot.index;
		misc[TONEMAP_TYPE] = getTonemapType();

		commandList.PushConstants(rhi::ShaderStage::Pixel, 0, MiscUintRootSignatureIndex, 0, NumMiscUintRootConstants, misc);

		commandList.Draw(3, 1, 0, 0); // Fullscreen triangle
		return {};
	}

	void Cleanup(RenderContext& context) override {
		// Cleanup the render pass
	}

private:

    rhi::PipelinePtr m_pso;
    PipelineResources m_resourceDescriptorBindings;

    std::shared_ptr<LazyDynamicStructuredBuffer<LPMConstants>> m_pLPMConstants;

    std::function<unsigned int()> getTonemapType;

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
            throw std::runtime_error("Failed to create tonemapping PSO (RHI)");
        }
        m_pso->SetName("Tonemapping.PSO");
    }
};