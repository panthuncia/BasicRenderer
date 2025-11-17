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
#include "Managers/Singletons/ResourceManager.h"
#include "Resources/TextureDescription.h"
#include "Managers/Singletons/UploadManager.h"
#include "Managers/Singletons/ECSManager.h"
#include "Mesh/MeshInstance.h"

class PPLLFillPass : public RenderPass {
public:
	PPLLFillPass(bool wireframe, size_t numPPLLNodes, bool meshShaders, bool indirect) : m_wireframe(wireframe), m_meshShaders(meshShaders), m_indirect(indirect) {
		auto& settingsManager = SettingsManager::GetInstance();
		getImageBasedLightingEnabled = settingsManager.getSettingGetter<bool>("enableImageBasedLighting");
		getPunctualLightingEnabled = settingsManager.getSettingGetter<bool>("enablePunctualLighting");
		getShadowsEnabled = settingsManager.getSettingGetter<bool>("enableShadows");
		m_gtaoEnabled = settingsManager.getSettingGetter<bool>("enableGTAO")();

		m_numPPLLNodes = numPPLLNodes;
		m_clusteredLightingEnabled = settingsManager.getSettingGetter<bool>("enableClusteredLighting")();
	}

	~PPLLFillPass() {
	}

	void DeclareResourceUsages(RenderPassBuilder* builder) override {
		builder->WithUnorderedAccess(Builtin::PPLL::HeadPointerTexture, Builtin::PPLL::DataBuffer, Builtin::PPLL::Counter)
			.WithShaderResource(Builtin::Light::BufferGroup,
				Builtin::PostSkinningVertices,
				Builtin::PerObjectBuffer,
				Builtin::NormalMatrixBuffer,
				Builtin::PerMeshBuffer,
				Builtin::PerMeshInstanceBuffer,
				Builtin::Environment::PrefilteredCubemapsGroup,
				Builtin::Environment::InfoBuffer,
				Builtin::CameraBuffer,
				Builtin::GBuffer::Normals,
				Builtin::Light::ActiveLightIndices,
				Builtin::Light::InfoBuffer,
				Builtin::Light::PointLightCubemapBuffer,
				Builtin::Light::SpotLightMatrixBuffer,
				Builtin::Light::DirectionalLightCascadeBuffer,
				Builtin::Shadows::ShadowMaps)
			.WithDepthReadWrite(Builtin::PrimaryCamera::DepthTexture)
			.IsGeometryPass();

		if (m_gtaoEnabled) {
			builder->WithShaderResource(Builtin::GTAO::OutputAOTerm);
		}
		if (m_clusteredLightingEnabled) {
			builder->WithShaderResource(Builtin::Light::ClusterBuffer, Builtin::Light::PagesBuffer);
		}
		if (m_meshShaders) {
			builder->WithShaderResource(MESH_RESOURCE_IDFENTIFIERS);
			builder->WithShaderResource(Builtin::PrimaryCamera::MeshletBitfield);
			if (m_indirect) {
				auto& ecsWorld = ECSManager::GetInstance().GetWorld();
				auto oitFillPassEntity = ECSManager::GetInstance().GetRenderPhaseEntity(Engine::Primary::OITAccumulationPass);
				flecs::query<> indirectQuery = ecsWorld.query_builder<>()
					.with<Components::IsIndirectArguments>()
					.with<Components::ParticipatesInPass>(oitFillPassEntity) // Query for command lists that participate in this pass
					//.cached().cache_kind(flecs::QueryCacheAll)
					.build();
			}
		}
	}

	void Setup() override {
		auto& ecsWorld = ECSManager::GetInstance().GetWorld();
		m_blendMeshInstancesQuery = ecsWorld.query_builder<Components::ObjectDrawInfo, Components::PerPassMeshes>()
			.with<Components::ParticipatesInPass>(ECSManager::GetInstance().GetRenderPhaseEntity(Engine::Primary::OITAccumulationPass))
			.cached().cache_kind(flecs::QueryCacheAll)
			.build();
		
		m_pPrimaryDepthBuffer = m_resourceRegistryView->Request<PixelBuffer>(Builtin::PrimaryCamera::DepthTexture);
		m_PPLLHeadPointerTexture = m_resourceRegistryView->Request<PixelBuffer>(Builtin::PPLL::HeadPointerTexture);
		
		RegisterUAV(Builtin::PPLL::HeadPointerTexture);

		if (m_meshShaders) {
			m_primaryCameraMeshletBitfield = m_resourceRegistryView->Request<DynamicGloballyIndexedResource>(Builtin::PrimaryCamera::MeshletBitfield);
		}

		m_PPLLCounter = m_resourceRegistryView->Request<Buffer>(Builtin::PPLL::Counter);
		RegisterUAV(Builtin::PPLL::Counter);
		
		RegisterUAV(Builtin::PPLL::DataBuffer);
		RegisterSRV(Builtin::NormalMatrixBuffer);
		RegisterSRV(Builtin::PostSkinningVertices);
		RegisterSRV(Builtin::PerObjectBuffer);
		RegisterSRV(Builtin::CameraBuffer);
		RegisterSRV(Builtin::PerMeshInstanceBuffer);
		RegisterSRV(Builtin::PerMeshBuffer);
		RegisterSRV(Builtin::GBuffer::Normals);

		if (m_clusteredLightingEnabled) {
			RegisterSRV(Builtin::Light::ClusterBuffer);
			RegisterSRV(Builtin::Light::PagesBuffer);
		}

		if (m_meshShaders) {
			RegisterSRV(Builtin::MeshResources::MeshletOffsets);
			RegisterSRV(Builtin::MeshResources::MeshletVertexIndices);
			RegisterSRV(Builtin::MeshResources::MeshletTriangles);
		}

		RegisterSRV(Builtin::Light::ActiveLightIndices);
		RegisterSRV(Builtin::Light::InfoBuffer);
		RegisterSRV(Builtin::Light::PointLightCubemapBuffer);
		RegisterSRV(Builtin::Light::SpotLightMatrixBuffer);
		RegisterSRV(Builtin::Light::DirectionalLightCascadeBuffer);
		RegisterSRV(Builtin::Environment::InfoBuffer);
	
	}

	PassReturn Execute(RenderContext& context) override {

		auto& commandList = context.commandList;

		SetupCommonState(context, commandList);
		SetCommonRootConstants(context, commandList);

		if (m_meshShaders) {
			if (m_indirect) {
				// Indirect drawing
				ExecuteMeshShaderIndirect(context, commandList);
			}
			else {
				// Regular mesh shader drawing
				ExecuteMeshShader(context, commandList);
			}
		}
		else {
			// Regular forward rendering
			ExecuteRegular(context, commandList);
		} 
		return {};
	}

	virtual void Update() override {
		// Reset UAV counter
		uint32_t zero = 0;
		UploadManager::GetInstance().UploadData(&zero, sizeof(uint32_t), m_PPLLCounter.get(), 0);
	}

	void Cleanup(RenderContext& context) override {
		// Cleanup the render pass
	}

private:

	void SetupCommonState(RenderContext& context, rhi::CommandList& commandList) {

		commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());

		uint32_t clearValues[4] = { 0, 0, 0, 0 };

		rhi::UavClearInfo clearInfo = {};
		clearInfo.cpuVisible = m_PPLLCounter->GetUAVNonShaderVisibleInfo(0).slot;
		clearInfo.shaderVisible = m_PPLLCounter->GetUAVShaderVisibleInfo(0).slot;
		clearInfo.resource = m_PPLLCounter->GetAPIResource();
		commandList.ClearUavUint(clearInfo, clearValues);

		uint32_t clearValues1[4] = { 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff };

		clearInfo.cpuVisible = m_PPLLHeadPointerTexture->GetUAVNonShaderVisibleInfo(0).slot;
		clearInfo.shaderVisible = m_PPLLHeadPointerTexture->GetUAVShaderVisibleInfo(0).slot;
		clearInfo.resource = m_PPLLHeadPointerTexture->GetAPIResource();
		commandList.ClearUavUint(clearInfo, clearValues1);

		// UAV barrier TODO: Is this necessary with the clear above?
		rhi::BarrierBatch barriers;
		rhi::BufferBarrier counterBarrier;
		counterBarrier.afterAccess = rhi::ResourceAccessType::UnorderedAccess;
		counterBarrier.beforeAccess = rhi::ResourceAccessType::UnorderedAccess;
		counterBarrier.afterSync = rhi::ResourceSyncState::PixelShading;
		counterBarrier.beforeSync = rhi::ResourceSyncState::ClearUnorderedAccessView;
		counterBarrier.buffer = m_PPLLCounter->GetAPIResource().GetHandle();

		rhi::TextureBarrier headPointerBarrier;
		headPointerBarrier.afterAccess = rhi::ResourceAccessType::UnorderedAccess;
		headPointerBarrier.beforeAccess = rhi::ResourceAccessType::UnorderedAccess;
		headPointerBarrier.afterLayout = rhi::ResourceLayout::UnorderedAccess;
		headPointerBarrier.beforeLayout = rhi::ResourceLayout::UnorderedAccess;
		headPointerBarrier.afterSync = rhi::ResourceSyncState::PixelShading;
		headPointerBarrier.beforeSync = rhi::ResourceSyncState::ClearUnorderedAccessView;
		headPointerBarrier.texture = m_PPLLHeadPointerTexture->GetAPIResource().GetHandle();

		barriers.textures = { &headPointerBarrier };
		barriers.buffers = { &counterBarrier };
		commandList.Barriers(barriers);


		//CD3DX12_VIEWPORT viewport = CD3DX12_VIEWPORT(0.0f, 0.0f, static_cast<float>(context.renderResolution.x), static_cast<float>(context.renderResolution.y));
		//CD3DX12_RECT scissorRect = CD3DX12_RECT(0, 0, context.renderResolution.x, context.renderResolution.y);
		//commandList->RSSetViewports(1, &viewport);
		//commandList->RSSetScissorRects(1, &scissorRect);

		//// Set the render target
		//auto dsvHandle = m_pPrimaryDepthBuffer->GetDSVInfo(0).cpuHandle;
		//commandList->OMSetRenderTargets(0, nullptr, FALSE, &dsvHandle);

		rhi::PassBeginInfo passInfo = {};
		rhi::DepthAttachment depthAttachment = {};
		depthAttachment.dsv = m_pPrimaryDepthBuffer->GetDSVInfo(0).slot;
		depthAttachment.depthLoad = rhi::LoadOp::Load;
		depthAttachment.depthStore = rhi::StoreOp::Store;
		depthAttachment.stencilLoad = rhi::LoadOp::DontCare;
		depthAttachment.stencilStore = rhi::StoreOp::DontCare;
		depthAttachment.clear = m_pPrimaryDepthBuffer->GetClearColor();
		passInfo.depth = &depthAttachment;
		passInfo.width = context.renderResolution.x;
		passInfo.height = context.renderResolution.y;
		commandList.BeginPass(passInfo);

		commandList.SetPrimitiveTopology(rhi::PrimitiveTopology::TriangleList);

		commandList.BindLayout(PSOManager::GetInstance().GetRootSignature().GetHandle());
	}

	void SetCommonRootConstants(RenderContext& context, rhi::CommandList& commandList) {

		unsigned int settings[NumSettingsRootConstants] = {}; // HLSL bools are 32 bits
		settings[EnableShadows] = getShadowsEnabled();
		settings[EnablePunctualLights] = getPunctualLightingEnabled();
		settings[EnableGTAO] = m_gtaoEnabled;
		commandList.PushConstants(rhi::ShaderStage::AllGraphics, 0, SettingsRootSignatureIndex, 0, NumSettingsRootConstants, &settings);

		unsigned int transparencyInfo[NumTransparencyInfoRootConstants] = {};
		transparencyInfo[PPLLNodePoolSize] = static_cast<uint32_t>(m_numPPLLNodes); // TODO: This needs to be 64-bit, or we will run out of nodes. PPLL in general may not be ideal for higher resolutions.
		commandList.PushConstants(rhi::ShaderStage::AllGraphics, 0, TransparencyInfoRootSignatureIndex, 0, NumTransparencyInfoRootConstants, &transparencyInfo);

		if (m_meshShaders) {
			unsigned int misc[NumMiscUintRootConstants] = {};
			misc[MESHLET_CULLING_BITFIELD_BUFFER_SRV_DESCRIPTOR_INDEX] = m_primaryCameraMeshletBitfield->GetResource()->GetSRVInfo(0).slot.index;
			commandList.PushConstants(rhi::ShaderStage::AllGraphics, 0, MiscUintRootSignatureIndex, 0, NumMiscUintRootConstants, &misc);
		}
	}

	void ExecuteRegular(RenderContext& context, rhi::CommandList& commandList) {
		// Regular forward rendering using DrawIndexedInstanced
		auto& psoManager = PSOManager::GetInstance();
		m_blendMeshInstancesQuery.each([&](flecs::entity e, Components::ObjectDrawInfo drawInfo, Components::PerPassMeshes blendMeshes) {
			auto& meshes = blendMeshes.meshesByPass[m_renderPhase.hash];

			commandList.PushConstants(rhi::ShaderStage::Pixel, 0, PerObjectRootSignatureIndex, PerObjectBufferIndex, 1, &drawInfo.perObjectCBIndex);

			for (auto& pMesh : meshes) {
				auto& mesh = *pMesh->GetMesh();
				auto& pso = psoManager.GetPPLLPSO(context.globalPSOFlags | mesh.material->GetPSOFlags(), mesh.material->Technique().compileFlags, m_wireframe);
				BindResourceDescriptorIndices(commandList, pso.GetResourceDescriptorSlots());
				commandList.BindPipeline(pso.GetAPIPipelineState().GetHandle());

				unsigned int perMeshIndices[NumPerMeshRootConstants] = {};
				perMeshIndices[PerMeshBufferIndex] = static_cast<unsigned int>(mesh.GetPerMeshBufferView()->GetOffset() / sizeof(PerMeshCB));
				perMeshIndices[PerMeshInstanceBufferIndex] = static_cast<uint32_t>(pMesh->GetPerMeshInstanceBufferOffset() / sizeof(PerMeshInstanceCB));
				commandList.PushConstants(rhi::ShaderStage::AllGraphics, 0, PerMeshRootSignatureIndex, 0, NumPerMeshRootConstants, perMeshIndices);

				commandList.SetIndexBuffer(mesh.GetIndexBufferView());

				commandList.DrawIndexed(mesh.GetIndexCount(), 1, 0, 0, 0);
			}
			});
	}

	void ExecuteMeshShader(RenderContext& context, rhi::CommandList& commandList) {
		// Mesh shading path using DispatchMesh
		auto& psoManager = PSOManager::GetInstance();
		m_blendMeshInstancesQuery.each([&](flecs::entity e, Components::ObjectDrawInfo drawInfo, Components::PerPassMeshes blendMeshes) {
			auto& meshes = blendMeshes.meshesByPass[m_renderPhase.hash];

			commandList.PushConstants(rhi::ShaderStage::Pixel, 0, PerObjectRootSignatureIndex, PerObjectBufferIndex, 1, &drawInfo.perObjectCBIndex);

			for (auto& pMesh : meshes) {
				auto& mesh = *pMesh->GetMesh();
				auto& pso = psoManager.GetMeshPPLLPSO(context.globalPSOFlags | mesh.material->GetPSOFlags(), mesh.material->Technique().compileFlags, m_wireframe);
				BindResourceDescriptorIndices(commandList, pso.GetResourceDescriptorSlots());
				commandList.BindPipeline(pso.GetAPIPipelineState().GetHandle());

				unsigned int perMeshIndices[NumPerMeshRootConstants] = {};
				perMeshIndices[PerMeshBufferIndex] = static_cast<unsigned int>(mesh.GetPerMeshBufferView()->GetOffset() / sizeof(PerMeshCB));
				perMeshIndices[PerMeshInstanceBufferIndex] = static_cast<uint32_t>(pMesh->GetPerMeshInstanceBufferOffset() / sizeof(PerMeshInstanceCB));
				commandList.PushConstants(rhi::ShaderStage::AllGraphics, 0, PerMeshRootSignatureIndex, 0, NumPerMeshRootConstants, perMeshIndices);

				commandList.DispatchMesh(mesh.GetMeshletCount(), 1, 1);
			}
			});
	}

	void ExecuteMeshShaderIndirect(RenderContext& context, rhi::CommandList& commandList) {
		auto& psoManager = PSOManager::GetInstance();

		auto commandSignature = CommandSignatureManager::GetInstance().GetDispatchMeshCommandSignature();

		auto workloads = context.indirectCommandBufferManager->GetBuffersForRenderPhase(
			context.currentScene->GetPrimaryCamera().get<Components::RenderViewRef>().viewID,
			Engine::Primary::OITAccumulationPass);

		for (auto& workload : workloads) {
			auto& pso = psoManager.GetMeshPPLLPSO(context.globalPSOFlags, workload.first, m_wireframe);
			commandList.BindPipeline(pso.GetAPIPipelineState().GetHandle());

			BindResourceDescriptorIndices(commandList, pso.GetResourceDescriptorSlots());

			auto apiResource = workload.second.buffer->GetAPIResource();
			commandList.ExecuteIndirect(
				commandSignature.GetHandle(),
				apiResource.GetHandle(),
				0,
				apiResource.GetHandle(),
				workload.second.buffer->GetResource()->GetUAVCounterOffset(),
				workload.second.count);
		}
	}

	flecs::query<Components::ObjectDrawInfo, Components::PerPassMeshes> m_blendMeshInstancesQuery;
	bool m_wireframe;
	bool m_meshShaders;
	bool m_indirect;
	bool m_gtaoEnabled = true;
	bool m_clusteredLightingEnabled = true;

	uint64_t m_numPPLLNodes;

	std::shared_ptr<PixelBuffer> m_PPLLHeadPointerTexture;
	std::shared_ptr<Buffer> m_PPLLCounter;

	std::shared_ptr<DynamicGloballyIndexedResource> m_primaryCameraMeshletBitfield = nullptr;
	std::shared_ptr<DynamicGloballyIndexedResource> m_meshletCullingBitfieldBuffer;
	std::shared_ptr<PixelBuffer> m_pPrimaryDepthBuffer;

	RenderPhase m_renderPhase = Engine::Primary::OITAccumulationPass;

	std::function<bool()> getImageBasedLightingEnabled;
	std::function<bool()> getPunctualLightingEnabled;
	std::function<bool()> getShadowsEnabled;
};