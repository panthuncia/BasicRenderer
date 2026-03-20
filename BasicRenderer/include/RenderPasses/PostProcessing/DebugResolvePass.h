#pragma once

#include "RenderPasses/Base/RenderPass.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"

class DebugResolvePass : public RenderPass {
public:
	DebugResolvePass() {
		CreatePSO();
	}

	void DeclareResourceUsages(RenderPassBuilder* builder) override {
		builder->WithShaderResource(Builtin::DebugVisualization, Builtin::CameraBuffer);
	}

	void Setup() override {
		RegisterSRV(Builtin::DebugVisualization);
		RegisterSRV(Builtin::CameraBuffer);
	}

	PassReturn Execute(PassExecutionContext& executionContext) override {
		auto* renderContext = executionContext.hostData->Get<RenderContext>();
		auto& context = *renderContext;
		auto& commandList = executionContext.commandList;

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

		commandList.Draw(3, 1, 0, 0); // Fullscreen triangle
		return {};
	}

	void Cleanup() override {}

private:
	rhi::PipelinePtr m_pso;
	PipelineResources m_resourceDescriptorBindings;

	void CreatePSO() {
		auto dev = DeviceManager::GetInstance().GetDevice();

		ShaderInfoBundle sib;
		sib.vertexShader = { L"shaders/fullscreenVS.hlsli", L"FullscreenVSNoViewRayMain", L"vs_6_6" };
		sib.pixelShader = { L"shaders/PostProcessing/debugResolve.hlsl", L"PSMain", L"ps_6_6" };
		auto compiled = PSOManager::GetInstance().CompileShaders(sib);
		m_resourceDescriptorBindings = compiled.resourceDescriptorSlots;

		auto& layout = PSOManager::GetInstance().GetRootSignature();
		rhi::SubobjLayout soLayout{ layout.GetHandle() };
		rhi::SubobjShader soVS{ rhi::ShaderStage::Vertex, rhi::DXIL(compiled.vertexShader.Get()), "FullscreenVSNoViewRayMain" };
		rhi::SubobjShader soPS{ rhi::ShaderStage::Pixel, rhi::DXIL(compiled.pixelShader.Get()), "PSMain" };

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

		auto result = dev.CreatePipeline(items, (uint32_t)std::size(items), m_pso);
		if (Failed(result)) {
			throw std::runtime_error("Failed to create DebugResolve PSO");
		}
		m_pso->SetName("DebugResolve.PSO");
	}
};
