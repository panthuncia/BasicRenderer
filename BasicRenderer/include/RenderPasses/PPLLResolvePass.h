#pragma once

#include <functional>

#include "RenderPasses/Base/RenderPass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "Scene/Scene.h"
#include "Managers/Singletons/SettingsManager.h"

class PPLLResolvePass : public RenderPass {
public:
	PPLLResolvePass() {
		auto& settingsManager = SettingsManager::GetInstance();
		getImageBasedLightingEnabled = settingsManager.getSettingGetter<bool>("enableImageBasedLighting");
		getPunctualLightingEnabled = settingsManager.getSettingGetter<bool>("enablePunctualLighting");
		getShadowsEnabled = settingsManager.getSettingGetter<bool>("enableShadows");
        CreatePSO();
	}

	void DeclareResourceUsages(RenderPassBuilder* builder) {
		builder->WithShaderResource(Builtin::PPLL::HeadPointerTexture, Builtin::PPLL::DataBuffer, Builtin::CameraBuffer)
			.WithRenderTarget(Builtin::Color::HDRColorTarget);
	}

	void Setup() override {

		m_pHDRTarget = m_resourceRegistryView->RequestPtr<PixelBuffer>(Builtin::Color::HDRColorTarget);

		RegisterSRV(Builtin::PPLL::HeadPointerTexture);
		RegisterSRV(Builtin::PPLL::DataBuffer);
		RegisterSRV(Builtin::CameraBuffer);
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
		passInfo.height = context.renderResolution.y;
		passInfo.width = context.renderResolution.x;
		commandList.BeginPass(passInfo);

		commandList.SetPrimitiveTopology(rhi::PrimitiveTopology::TriangleStrip);

		commandList.BindLayout(psoManager.GetRootSignature().GetHandle());
		commandList.BindPipeline(m_pso->GetHandle());

		unsigned int settings[2] = { getShadowsEnabled(), getPunctualLightingEnabled() }; // HLSL bools are 32 bits
		commandList.PushConstants(rhi::ShaderStage::AllGraphics, 0, SettingsRootSignatureIndex, 0, 2, settings);

		BindResourceDescriptorIndices(commandList, m_resourceDescriptorBindings);

		unsigned int localPSOFlags = 0;
		if (getImageBasedLightingEnabled()) {
			localPSOFlags |= PSOFlags::PSO_IMAGE_BASED_LIGHTING;
		}

		commandList.Draw(3, 1, 0, 0); // Fullscreen triangle
		return {};
	}

    void Cleanup() override {
        // Cleanup the render pass
	}

private:
	rhi::PipelinePtr m_pso;

	PixelBuffer* m_pHDRTarget;

	std::function<bool()> getImageBasedLightingEnabled;
	std::function<bool()> getPunctualLightingEnabled;
	std::function<bool()> getShadowsEnabled;

	PipelineResources m_resourceDescriptorBindings;

	void CreatePSO() {
		// Compile shaders
        auto dev = DeviceManager::GetInstance().GetDevice();

        ShaderInfoBundle sib;
        sib.vertexShader = { L"shaders/fullscreenVS.hlsli", L"FullscreenVSMain", L"vs_6_6" };
        sib.pixelShader = { L"shaders/PPLL.hlsl",          L"PPLLResolvePS",    L"ps_6_6" };

        auto compiled = PSOManager::GetInstance().CompileShaders(sib);
        m_resourceDescriptorBindings = compiled.resourceDescriptorSlots;

        auto& layout = PSOManager::GetInstance().GetRootSignature(); // rhi::PipelineLayoutPtr
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
        ds.depthFunc = rhi::CompareOp::Less;
        rhi::SubobjDepth soDepth{ ds };

        rhi::RenderTargets rts{};
        rts.count = 1;
        rts.formats[0] = rhi::Format::R16G16B16A16_Float;
        rhi::SubobjRTVs soRTV{ rts };

        rhi::SubobjSample soSmp{ rhi::SampleDesc{ 1, 0 } };

        const rhi::PipelineStreamItem items[] = {
            rhi::Make(soLayout),
            rhi::Make(soVS),
            rhi::Make(soPS),
            rhi::Make(soRaster),
            rhi::Make(soBlend),
            rhi::Make(soDepth),
            rhi::Make(soRTV),
            rhi::Make(soSmp),
        };

        auto result = dev.CreatePipeline(items, (uint32_t)std::size(items), m_pso);
        if (Failed(result)) {
            throw std::runtime_error("PPLL Resolve: failed to create PSO (RHI)");
        }
        m_pso->SetName("PPLL.Resolve.PSO");
	}
};