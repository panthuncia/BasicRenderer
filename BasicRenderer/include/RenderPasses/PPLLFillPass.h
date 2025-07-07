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
			if (m_indirect) {
				builder->WithShaderResource(Builtin::PrimaryCamera::MeshletBitfield);
				builder->WithIndirectArguments(Builtin::PrimaryCamera::IndirectCommandBuffers::Blend);
			}
		}
	}

	void Setup() override {
		auto& ecsWorld = ECSManager::GetInstance().GetWorld();
		m_blendMeshInstancesQuery = ecsWorld.query_builder<Components::ObjectDrawInfo, Components::BlendMeshInstances>().cached().cache_kind(flecs::QueryCacheAll).build();
		
		m_pPrimaryDepthBuffer = m_resourceRegistryView->Request<PixelBuffer>(Builtin::PrimaryCamera::DepthTexture);
		m_PPLLHeadPointerTexture = m_resourceRegistryView->Request<PixelBuffer>(Builtin::PPLL::HeadPointerTexture);
		
		RegisterUAV(Builtin::PPLL::HeadPointerTexture);
		if (m_indirect) {
			m_primaryCameraBlendIndirectCommandBuffer = m_resourceRegistryView->Request<DynamicGloballyIndexedResource>(Builtin::PrimaryCamera::IndirectCommandBuffers::Blend);
			if (m_meshShaders) {
				m_primaryCameraMeshletBitfield = m_resourceRegistryView->Request<DynamicGloballyIndexedResource>(Builtin::PrimaryCamera::MeshletBitfield);
			}
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

		auto numBlend = context.drawStats.numBlendDraws;
		if (numBlend == 0) {
			return {};
		}

		auto& commandList = context.commandList;

		SetupCommonState(context, commandList);
		SetCommonRootConstants(context, commandList);

		if (m_meshShaders) {
			if (m_indirect) {
				// Indirect drawing
				ExecuteMeshShaderIndirect(context, static_cast<ID3D12GraphicsCommandList7*>(commandList));
			}
			else {
				// Regular mesh shader drawing
				ExecuteMeshShader(context, static_cast<ID3D12GraphicsCommandList7*>(commandList));
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

	void SetupCommonState(RenderContext& context, ID3D12GraphicsCommandList* commandList) {
		ID3D12DescriptorHeap* descriptorHeaps[] = {
			context.textureDescriptorHeap, // The texture descriptor heap
			context.samplerDescriptorHeap, // The sampler descriptor heap
		};

		commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

		uint32_t clearValues[4] = { 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff };
		commandList->ClearUnorderedAccessViewUint(m_PPLLHeadPointerTexture->GetUAVShaderVisibleInfo(0).gpuHandle, m_PPLLHeadPointerTexture->GetUAVNonShaderVisibleInfo(0).cpuHandle, m_PPLLHeadPointerTexture->GetAPIResource(), clearValues, 0, nullptr);

		D3D12_RESOURCE_BARRIER uavBarrier = CD3DX12_RESOURCE_BARRIER::UAV(m_PPLLHeadPointerTexture->GetAPIResource());
		commandList->ResourceBarrier(1, &uavBarrier);

		CD3DX12_VIEWPORT viewport = CD3DX12_VIEWPORT(0.0f, 0.0f, context.renderResolution.x, context.renderResolution.y);
		CD3DX12_RECT scissorRect = CD3DX12_RECT(0, 0, context.renderResolution.x, context.renderResolution.y);
		commandList->RSSetViewports(1, &viewport);
		commandList->RSSetScissorRects(1, &scissorRect);

		// Set the render target
		auto dsvHandle = m_pPrimaryDepthBuffer->GetDSVInfo(0).cpuHandle;
		commandList->OMSetRenderTargets(0, nullptr, FALSE, &dsvHandle);

		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		auto& psoManager = PSOManager::GetInstance();
		auto rootSignature = psoManager.GetRootSignature();
		commandList->SetGraphicsRootSignature(rootSignature.Get());
	}

	void SetCommonRootConstants(RenderContext& context, ID3D12GraphicsCommandList* commandList) {

		unsigned int settings[NumSettingsRootConstants] = {}; // HLSL bools are 32 bits
		settings[EnableShadows] = getShadowsEnabled();
		settings[EnablePunctualLights] = getPunctualLightingEnabled();
		settings[EnableGTAO] = m_gtaoEnabled;
		commandList->SetGraphicsRoot32BitConstants(SettingsRootSignatureIndex, NumSettingsRootConstants, &settings, 0);

		unsigned int transparencyInfo[NumTransparencyInfoRootConstants] = {};
		transparencyInfo[PPLLNodePoolSize] = m_numPPLLNodes;
		commandList->SetGraphicsRoot32BitConstants(TransparencyInfoRootSignatureIndex, NumTransparencyInfoRootConstants, &transparencyInfo, 0);
	
		unsigned int misc[NumMiscUintRootConstants] = {};
		misc[MESHLET_CULLING_BITFIELD_BUFFER_SRV_DESCRIPTOR_INDEX] = m_primaryCameraMeshletBitfield->GetResource()->GetSRVInfo(0).index;
		commandList->SetGraphicsRoot32BitConstants(MiscUintRootSignatureIndex, NumMiscUintRootConstants, &misc, 0);
	}

	void ExecuteRegular(RenderContext& context, ID3D12GraphicsCommandList7* commandList) {
		// Regular forward rendering using DrawIndexedInstanced
		auto& psoManager = PSOManager::GetInstance();
		m_blendMeshInstancesQuery.each([&](flecs::entity e, Components::ObjectDrawInfo drawInfo, Components::BlendMeshInstances blendMeshes) {
			auto& meshes = blendMeshes.meshInstances;

			commandList->SetGraphicsRoot32BitConstants(PerObjectRootSignatureIndex, 1, &drawInfo.perObjectCBIndex, PerObjectBufferIndex);

			for (auto& pMesh : meshes) {
				auto& mesh = *pMesh->GetMesh();
				auto globalFlags = context.globalPSOFlags;
				//globalFlags &= ~PSOFlags::PSO_SCREENSPACE_REFLECTIONS; // Disable SSR for transparencies for now
				auto pso = psoManager.GetPPLLPSO(context.globalPSOFlags | mesh.material->m_psoFlags, BLEND_STATE_BLEND, m_wireframe);
				BindResourceDescriptorIndices(commandList, pso.GetResourceDescriptorSlotMap());
				commandList->SetPipelineState(pso.GetAPIPipelineState());

				unsigned int perMeshIndices[NumPerMeshRootConstants] = {};
				perMeshIndices[PerMeshBufferIndex] = mesh.GetPerMeshBufferView()->GetOffset() / sizeof(PerMeshCB);
				perMeshIndices[PerMeshInstanceBufferIndex] = pMesh->GetPerMeshInstanceBufferOffset() / sizeof(PerMeshInstanceCB);
				commandList->SetGraphicsRoot32BitConstants(PerMeshRootSignatureIndex, NumPerMeshRootConstants, perMeshIndices, 0);

				D3D12_INDEX_BUFFER_VIEW indexBufferView = mesh.GetIndexBufferView();
				commandList->IASetIndexBuffer(&indexBufferView);

				commandList->DrawIndexedInstanced(mesh.GetIndexCount(), 1, 0, 0, 0);
			}
			});
	}

	void ExecuteMeshShader(RenderContext& context, ID3D12GraphicsCommandList7* commandList) {
		// Mesh shading path using DispatchMesh
		auto& psoManager = PSOManager::GetInstance();
		m_blendMeshInstancesQuery.each([&](flecs::entity e, Components::ObjectDrawInfo drawInfo, Components::BlendMeshInstances blendMeshes) {
			auto& meshes = blendMeshes.meshInstances;

			commandList->SetGraphicsRoot32BitConstants(PerObjectRootSignatureIndex, 1, &drawInfo.perObjectCBIndex, PerObjectBufferIndex);

			for (auto& pMesh : meshes) {
				auto& mesh = *pMesh->GetMesh();
				auto pso = psoManager.GetMeshPPLLPSO(context.globalPSOFlags | mesh.material->m_psoFlags, BLEND_STATE_BLEND, m_wireframe);
				BindResourceDescriptorIndices(commandList, pso.GetResourceDescriptorSlotMap());
				commandList->SetPipelineState(pso.GetAPIPipelineState());

				unsigned int perMeshIndices[NumPerMeshRootConstants] = {};
				perMeshIndices[PerMeshBufferIndex] = mesh.GetPerMeshBufferView()->GetOffset() / sizeof(PerMeshCB);
				perMeshIndices[PerMeshInstanceBufferIndex] = pMesh->GetPerMeshInstanceBufferOffset() / sizeof(PerMeshInstanceCB);
				commandList->SetGraphicsRoot32BitConstants(PerMeshRootSignatureIndex, NumPerMeshRootConstants, perMeshIndices, 0);

				commandList->DispatchMesh(mesh.GetMeshletCount(), 1, 1);
			}
			});
	}

	void ExecuteMeshShaderIndirect(RenderContext& context, ID3D12GraphicsCommandList7* commandList) {
		auto numBlend = context.drawStats.numBlendDraws;
		if (numBlend == 0) {
			return;
		}
		auto& psoManager = PSOManager::GetInstance();
		auto pso = psoManager.GetMeshPPLLPSO(context.globalPSOFlags | PSOFlags::PSO_ALPHA_TEST,  BLEND_STATE_BLEND, m_wireframe);
		BindResourceDescriptorIndices(commandList, pso.GetResourceDescriptorSlotMap());
		commandList->SetPipelineState(pso.GetAPIPipelineState());

		auto commandSignature = CommandSignatureManager::GetInstance().GetDispatchMeshCommandSignature();

		// Blended objects
		auto indirectCommandBuffer = m_primaryCameraBlendIndirectCommandBuffer;
		auto apiResource = indirectCommandBuffer->GetAPIResource();
		commandList->ExecuteIndirect(commandSignature, numBlend, apiResource, 0, apiResource, indirectCommandBuffer->GetResource()->GetUAVCounterOffset());
	}

	flecs::query<Components::ObjectDrawInfo, Components::BlendMeshInstances> m_blendMeshInstancesQuery;
	bool m_wireframe;
	bool m_meshShaders;
	bool m_indirect;
	bool m_gtaoEnabled = true;
	bool m_clusteredLightingEnabled = true;

	size_t m_numPPLLNodes;

	std::shared_ptr<PixelBuffer> m_PPLLHeadPointerTexture;
	std::shared_ptr<Buffer> m_PPLLCounter;

	std::shared_ptr<DynamicGloballyIndexedResource> m_primaryCameraMeshletBitfield = nullptr;
	std::shared_ptr<DynamicGloballyIndexedResource> m_primaryCameraBlendIndirectCommandBuffer;
	std::shared_ptr<DynamicGloballyIndexedResource> m_meshletCullingBitfieldBuffer;
	std::shared_ptr<PixelBuffer> m_pPrimaryDepthBuffer;

	std::function<bool()> getImageBasedLightingEnabled;
	std::function<bool()> getPunctualLightingEnabled;
	std::function<bool()> getShadowsEnabled;
};