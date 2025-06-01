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
	PPLLFillPass(bool wireframe, std::shared_ptr<PixelBuffer> PPLLHeads, std::shared_ptr<Buffer> PPLLBuffer, std::shared_ptr<Buffer> PPLLCounter, size_t numPPLLNodes, bool meshShaders, bool indirect, unsigned int aoTextureDescriptorIndex, unsigned int normalsTextureDescriptorIndex) : m_wireframe(wireframe), m_meshShaders(meshShaders), m_indirect(indirect), m_aoTextureDescriptorIndex(aoTextureDescriptorIndex), m_normalsTextureDescriptorIndex(normalsTextureDescriptorIndex) {
		auto& settingsManager = SettingsManager::GetInstance();
		getImageBasedLightingEnabled = settingsManager.getSettingGetter<bool>("enableImageBasedLighting");
		getPunctualLightingEnabled = settingsManager.getSettingGetter<bool>("enablePunctualLighting");
		getShadowsEnabled = settingsManager.getSettingGetter<bool>("enableShadows");
		m_gtaoEnabled = settingsManager.getSettingGetter<bool>("enableGTAO")();

		m_PPLLHeadPointerTexture = PPLLHeads;
		m_PPLLBuffer = PPLLBuffer;
		m_PPLLCounter = PPLLCounter;
		m_numPPLLNodes = numPPLLNodes;
	}

	~PPLLFillPass() {
	}

	void Setup() override {
		auto& ecsWorld = ECSManager::GetInstance().GetWorld();
		m_blendMeshInstancesQuery = ecsWorld.query_builder<Components::ObjectDrawInfo, Components::BlendMeshInstances>().cached().cache_kind(flecs::QueryCacheAll).build();
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

		CD3DX12_VIEWPORT viewport = CD3DX12_VIEWPORT(0.0f, 0.0f, context.xRes, context.yRes);
		CD3DX12_RECT scissorRect = CD3DX12_RECT(0, 0, context.xRes, context.yRes);
		commandList->RSSetViewports(1, &viewport);
		commandList->RSSetScissorRects(1, &scissorRect);

		// Set the render target
		auto dsvHandle = context.pPrimaryDepthBuffer->GetDSVInfo(0).cpuHandle;
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

		unsigned int staticBufferIndices[NumStaticBufferRootConstants] = {};
		auto& meshManager = context.meshManager;
		auto& objectManager = context.objectManager;
		auto& cameraManager = context.cameraManager;
		auto& lightManager = context.lightManager;

		staticBufferIndices[NormalMatrixBufferDescriptorIndex] = objectManager->GetNormalMatrixBufferSRVIndex();
		staticBufferIndices[PostSkinningVertexBufferDescriptorIndex] = meshManager->GetPostSkinningVertexBufferSRVIndex();
		staticBufferIndices[MeshletBufferDescriptorIndex] = meshManager->GetMeshletOffsetBufferSRVIndex();
		staticBufferIndices[MeshletVerticesBufferDescriptorIndex] = meshManager->GetMeshletVertexIndexBufferSRVIndex();
		staticBufferIndices[MeshletTrianglesBufferDescriptorIndex] = meshManager->GetMeshletTriangleBufferSRVIndex();
		staticBufferIndices[PerObjectBufferDescriptorIndex] = objectManager->GetPerObjectBufferSRVIndex();
		staticBufferIndices[CameraBufferDescriptorIndex] = cameraManager->GetCameraBufferSRVIndex();
		staticBufferIndices[PerMeshInstanceBufferDescriptorIndex] = meshManager->GetPerMeshInstanceBufferSRVIndex();
		staticBufferIndices[PerMeshBufferDescriptorIndex] = meshManager->GetPerMeshBufferSRVIndex();
		staticBufferIndices[NormalsTextureDescriptorIndex] = m_normalsTextureDescriptorIndex;
		commandList->SetGraphicsRoot32BitConstants(StaticBufferRootSignatureIndex, NumStaticBufferRootConstants, &staticBufferIndices, 0);

		unsigned int lightClusterInfo[NumLightClusterRootConstants] = {};
		lightClusterInfo[LightClusterBufferDescriptorIndex] = lightManager->GetClusterBuffer()->GetSRVInfo(0).index;
		lightClusterInfo[LightPagesBufferDescriptorIndex] = lightManager->GetLightPagesBuffer()->GetSRVInfo(0).index;
		commandList->SetGraphicsRoot32BitConstants(LightClusterRootSignatureIndex, NumLightClusterRootConstants, &lightClusterInfo, 0);

		unsigned int transparencyInfo[NumTransparencyInfoRootConstants] = {};
		transparencyInfo[PPLLHeadBufferDescriptorIndex] = m_PPLLHeadPointerTexture->GetSRVInfo(0).index;
		transparencyInfo[PPLLNodeBufferDescriptorIndex] = m_PPLLBuffer->GetSRVInfo(0).index;
		transparencyInfo[PPLLCounterBufferDescriptorIndex] = m_PPLLCounter->GetSRVInfo(0).index;
		transparencyInfo[PPLLNodePoolSize] = m_numPPLLNodes;
		commandList->SetGraphicsRoot32BitConstants(TransparencyInfoRootSignatureIndex, NumTransparencyInfoRootConstants, &transparencyInfo, 0);

		unsigned int variableRootConstants[NumVariableBufferRootConstants] = {};
		variableRootConstants[MeshletCullingBitfieldBufferDescriptorIndex] = context.currentScene->GetPrimaryCameraMeshletFrustrumCullingBitfieldBuffer()->GetResource()->GetUAVShaderVisibleInfo(0).index;

		commandList->SetGraphicsRoot32BitConstants(VariableBufferRootSignatureIndex, NumVariableBufferRootConstants, &variableRootConstants, 0);

		// PPLL heads & buffer
		uint32_t indices[NumTransparencyInfoRootConstants] = { m_PPLLHeadPointerTexture->GetUAVShaderVisibleInfo(0).index, m_PPLLBuffer->GetUAVShaderVisibleInfo(0).index, m_PPLLCounter->GetUAVShaderVisibleInfo(0).index, m_numPPLLNodes};
		commandList->SetGraphicsRoot32BitConstants(TransparencyInfoRootSignatureIndex, 4, &indices, 0);
	}

	void ExecuteRegular(RenderContext& context, ID3D12GraphicsCommandList* commandList) {
		// Regular forward rendering using DrawIndexedInstanced
		auto& psoManager = PSOManager::GetInstance();
		m_blendMeshInstancesQuery.each([&](flecs::entity e, Components::ObjectDrawInfo drawInfo, Components::BlendMeshInstances blendMeshes) {
			auto& meshes = blendMeshes.meshInstances;

			commandList->SetGraphicsRoot32BitConstants(PerObjectRootSignatureIndex, 1, &drawInfo.perObjectCBIndex, PerObjectBufferIndex);

			for (auto& pMesh : meshes) {
				auto& mesh = *pMesh->GetMesh();
				auto pso = psoManager.GetPPLLPSO(context.globalPSOFlags | mesh.material->m_psoFlags, BLEND_STATE_BLEND, m_wireframe);
				commandList->SetPipelineState(pso.Get());

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
				commandList->SetPipelineState(pso.Get());

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
		commandList->SetPipelineState(pso.Get());

		auto commandSignature = CommandSignatureManager::GetInstance().GetDispatchMeshCommandSignature();

		// Blended objects
		auto indirectCommandBuffer = context.currentScene->GetPrimaryCameraBlendIndirectCommandBuffer();
		auto apiResource = indirectCommandBuffer->GetAPIResource();
		commandList->ExecuteIndirect(commandSignature, numBlend, apiResource, 0, apiResource, indirectCommandBuffer->GetResource()->GetUAVCounterOffset());
	}

	flecs::query<Components::ObjectDrawInfo, Components::BlendMeshInstances> m_blendMeshInstancesQuery;
	bool m_wireframe;
	bool m_meshShaders;
	bool m_indirect;
	bool m_gtaoEnabled = true;

	unsigned int m_normalsTextureDescriptorIndex = 0;
	unsigned int m_aoTextureDescriptorIndex = 0;

	size_t m_numPPLLNodes;

	std::shared_ptr<PixelBuffer> m_PPLLHeadPointerTexture;
	std::shared_ptr<Buffer> m_PPLLBuffer;
	std::shared_ptr<Buffer> m_PPLLCounter;

	std::function<bool()> getImageBasedLightingEnabled;
	std::function<bool()> getPunctualLightingEnabled;
	std::function<bool()> getShadowsEnabled;
};