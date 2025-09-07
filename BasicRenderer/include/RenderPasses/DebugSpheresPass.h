#pragma once

#include <unordered_map>
#include <functional>

#include "RenderPasses/Base/RenderPass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "Mesh/Mesh.h"
#include "Scene/Scene.h"
#include "Materials/Material.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Managers/Singletons/ECSManager.h"

class DebugSpherePass : public RenderPass {
public:
	DebugSpherePass() {
		CreateDebugRootSignature();
		CreateDebugMeshPSO();
	}
	~DebugSpherePass() {
	}

	void DeclareResourceUsages(RenderPassBuilder* builder) {
		builder->WithShaderResource(Builtin::PerObjectBuffer, Builtin::PerMeshBuffer, Builtin::CameraBuffer)
			.WithDepthReadWrite(Builtin::PrimaryCamera::DepthTexture)
			.IsGeometryPass();
	}

	void Setup() override {
		auto& ecsWorld = ECSManager::GetInstance().GetWorld();
		m_opaqueMeshInstancesQuery = ecsWorld.query_builder<Components::ObjectDrawInfo, Components::OpaqueMeshInstances>().cached().cache_kind(flecs::QueryCacheAll).build();
		m_alphaTestMeshInstancesQuery = ecsWorld.query_builder<Components::ObjectDrawInfo, Components::AlphaTestMeshInstances>().cached().cache_kind(flecs::QueryCacheAll).build();
		m_blendMeshInstancesQuery = ecsWorld.query_builder<Components::ObjectDrawInfo, Components::BlendMeshInstances>().cached().cache_kind(flecs::QueryCacheAll).build();
	
		m_pPrimaryDepthBuffer = m_resourceRegistryView->Request<PixelBuffer>(Builtin::PrimaryCamera::DepthTexture);

		m_cameraBufferSRVIndex = m_resourceRegistryView->Request<GloballyIndexedResource>(Builtin::CameraBuffer)->GetSRVInfo(0).index;
		m_objectBufferSRVIndex = m_resourceRegistryView->Request<GloballyIndexedResource>(Builtin::PerObjectBuffer)->GetSRVInfo(0).index;
	}

	PassReturn Execute(RenderContext& context) override {
		auto& commandList = context.commandList;

		ID3D12DescriptorHeap* descriptorHeaps[] = {
			context.textureDescriptorHeap, // The texture descriptor heap
			context.samplerDescriptorHeap, // The sampler descriptor heap
		};

		commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

		CD3DX12_VIEWPORT viewport = CD3DX12_VIEWPORT(0.0f, 0.0f, static_cast<float>(context.outputResolution.x), static_cast<float>(context.outputResolution.y));
		CD3DX12_RECT scissorRect = CD3DX12_RECT(0, 0, context.outputResolution.x, context.outputResolution.y);
		commandList->RSSetViewports(1, &viewport);
		commandList->RSSetScissorRects(1, &scissorRect);

		// Set the render target
		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(context.rtvHeap->GetCPUDescriptorHandleForHeapStart(), context.frameIndex, context.rtvDescriptorSize);
		auto& dsvHandle = m_pPrimaryDepthBuffer->GetDSVInfo(0).cpuHandle;
		commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		auto rootSignature = m_debugRootSignature;
		commandList->SetGraphicsRootSignature(rootSignature.Get());


		auto pso = m_pso;
		commandList->SetPipelineState(pso.Get());
		
		struct Constants {
			float center[3];
			float padding;
			float radius;
			uint32_t perObjectIndex;
			uint32_t cameraBufferIndex;
			uint32_t objectBufferIndex;
		};
		Constants constants;
		constants.center[0] = 0.0;
		constants.center[1] = 0.0;
		constants.center[2] = 0.0;
		constants.radius = 1.0;
		constants.perObjectIndex = 0;
		constants.cameraBufferIndex = m_cameraBufferSRVIndex;
		constants.objectBufferIndex = m_objectBufferSRVIndex;

		commandList->SetGraphicsRoot32BitConstants(0, 8, &constants, 0);

		m_opaqueMeshInstancesQuery.each([&](flecs::entity e, Components::ObjectDrawInfo drawInfo, Components::OpaqueMeshInstances opaqueMeshes) {
			auto& meshes = opaqueMeshes.meshInstances;

			for (auto& pMesh : meshes) {
				auto meshData = pMesh->GetMesh()->GetPerMeshCBData();
				constants.center[0] = meshData.boundingSphere.sphere.x;
				constants.center[1] = meshData.boundingSphere.sphere.y;
				constants.center[2] = meshData.boundingSphere.sphere.z;
				constants.radius = meshData.boundingSphere.sphere.w;
				constants.perObjectIndex = drawInfo.perObjectCBIndex;
				commandList->SetGraphicsRoot32BitConstants(0, 6, &constants, 0);
				commandList->DispatchMesh(1, 1, 1);
			}
			});

		m_alphaTestMeshInstancesQuery.each([&](flecs::entity e, Components::ObjectDrawInfo drawInfo, Components::AlphaTestMeshInstances alphaTestMeshes) {
			auto& meshes = alphaTestMeshes.meshInstances;

			for (auto& pMesh : meshes) {
				auto meshData = pMesh->GetMesh()->GetPerMeshCBData();
				constants.center[0] = meshData.boundingSphere.sphere.x;
				constants.center[1] = meshData.boundingSphere.sphere.y;
				constants.center[2] = meshData.boundingSphere.sphere.z;
				constants.radius = meshData.boundingSphere.sphere.w;
				constants.perObjectIndex = drawInfo.perObjectCBIndex;
				commandList->SetGraphicsRoot32BitConstants(0, 6, &constants, 0);
				commandList->DispatchMesh(1, 1, 1);
			}
			});

		m_blendMeshInstancesQuery.each([&](flecs::entity e, Components::ObjectDrawInfo drawInfo, Components::BlendMeshInstances blendMeshes) {
			auto& meshes = blendMeshes.meshInstances;

			for (auto& pMesh : meshes) {
				auto meshData = pMesh->GetMesh()->GetPerMeshCBData();
				constants.center[0] = meshData.boundingSphere.sphere.x;
				constants.center[1] = meshData.boundingSphere.sphere.y;
				constants.center[2] = meshData.boundingSphere.sphere.z;
				constants.radius = meshData.boundingSphere.sphere.w;
				constants.perObjectIndex = drawInfo.perObjectCBIndex;
				commandList->SetGraphicsRoot32BitConstants(0, 6, &constants, 0);
				commandList->DispatchMesh(1, 1, 1);
			}
			});
		return {};
	}

	void Cleanup(RenderContext& context) override {
		// Cleanup the render pass
	}

private:

	void CreateDebugRootSignature() {
		auto device = DeviceManager::GetInstance().GetDevice();

		rhi::PipelineLayoutDesc desc = {};
		desc.flags = rhi::PipelineLayoutFlags::PF_AllowInputAssembler;
		rhi::PushConstantRangeDesc pushConstant = { rhi::ShaderStage::Mesh, 8, 0, 1 };

		rhi::LayoutBindingRange binding = {};
		binding.set = 0;
		binding.binding = 0;
		binding.count = 1;
		binding.readOnly = true;
		binding.visibility = rhi::ShaderStage::AllGraphics;
		desc.ranges = rhi::Span<rhi::LayoutBindingRange>{ &binding, 1 };
		desc.pushConstants = rhi::Span<rhi::PushConstantRangeDesc>{ &pushConstant };
		desc.staticSamplers = rhi::Span<rhi::StaticSamplerDesc>{};
		m_debugLayout = device.CreatePipelineLayout(desc);

	}

	void CreateDebugMeshPSO() {

		auto dev = DeviceManager::GetInstance().GetDevice();

		// Compile shaders
		ShaderInfoBundle sib;
		sib.meshShader = { L"shaders/sphere.hlsl", L"MSMain",        L"ms_6_6" };
		sib.pixelShader = { L"shaders/sphere.hlsl", L"SpherePSMain",  L"ps_6_6" };
		auto compiled = PSOManager::GetInstance().CompileShaders(sib);

		// Subobjects
		rhi::SubobjLayout soLayout{ m_debugLayout->GetHandle() };

		rhi::SubobjShader soMS{ rhi::ShaderStage::Mesh,  rhi::DXIL(compiled.meshShader.Get()) };
		rhi::SubobjShader soPS{ rhi::ShaderStage::Pixel, rhi::DXIL(compiled.pixelShader.Get()) };

		rhi::RasterState rs{};
		rs.fill = rhi::FillMode::Wireframe;
		rs.cull = rhi::CullMode::None;
		rs.frontCCW = true;
		rhi::SubobjRaster soRaster{ rs };

		rhi::BlendState bs{};
		bs.alphaToCoverage = false;
		bs.independentBlend = false;
		bs.numAttachments = 1;
		bs.attachments[0].enable = false;                     // no blending
		bs.attachments[0].writeMask = rhi::ColorWriteEnable::All;
		rhi::SubobjBlend soBlend{ bs };

		rhi::DepthStencilState ds{};
		ds.depthEnable = true;
		ds.depthWrite = false;                                  // D3D12_DEPTH_WRITE_MASK_ZERO
		ds.depthFunc = rhi::CompareOp::Less;                   // default in your DX path
		rhi::SubobjDepth soDepth{ ds };

		rhi::RenderTargets rts{};
		rts.count = 1;
		rts.formats[0] = rhi::Format::R8G8B8A8_UNorm;
		rhi::SubobjRTVs soRTVs{ rts };

		// Your original used D24_UNORM_S8_UINT. If your RHI format enum doesn’t carry D24,
		// you can either set Unknown (let backend infer) or use D32_Float consistently.
		rhi::SubobjDSV    soDSV{ rhi::Format::D32_Float };
		rhi::SubobjSample soSmp{ rhi::SampleDesc{1, 0} };

		const rhi::PipelineStreamItem items[] = {
			rhi::Make(soLayout),
			rhi::Make(soMS),
			rhi::Make(soPS),
			rhi::Make(soRaster),
			rhi::Make(soBlend),
			rhi::Make(soDepth),
			rhi::Make(soRTVs),
			rhi::Make(soDSV),
			rhi::Make(soSmp),
		};

		m_pso = dev.CreatePipeline(items, (uint32_t)std::size(items));
		if (!m_pso || !m_pso->IsValid()) {
			throw std::runtime_error("Failed to create Debug Mesh PSO (RHI)");
		}
		m_pso->SetName("Debug.Mesh.Wireframe");

	}

	flecs::query<Components::ObjectDrawInfo, Components::OpaqueMeshInstances> m_opaqueMeshInstancesQuery;
	flecs::query<Components::ObjectDrawInfo, Components::AlphaTestMeshInstances> m_alphaTestMeshInstancesQuery;
	flecs::query<Components::ObjectDrawInfo, Components::BlendMeshInstances> m_blendMeshInstancesQuery;
	rhi::PipelineLayoutPtr m_debugLayout;
	rhi::PipelinePtr m_pso;
	bool m_wireframe;

	std::shared_ptr<PixelBuffer> m_pPrimaryDepthBuffer;

	int m_cameraBufferSRVIndex = -1;
	int m_objectBufferSRVIndex = -1;

	std::function<bool()> getImageBasedLightingEnabled;
	std::function<bool()> getPunctualLightingEnabled;
	std::function<bool()> getShadowsEnabled;

};