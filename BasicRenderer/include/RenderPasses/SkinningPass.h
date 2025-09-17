#pragma once

#include <DirectX/d3dx12.h>

#include "RenderPasses/Base/ComputePass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Utilities/Utilities.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Managers/MeshManager.h"
#include "Managers/ObjectManager.h"
#include "Managers/Singletons/ECSManager.h"

class SkinningPass : public ComputePass {
public:
	SkinningPass() {
		getMeshShadersEnabled = SettingsManager::GetInstance().getSettingGetter<bool>("enableMeshShader");
	}

	~SkinningPass() {
	}

	void DeclareResourceUsages(ComputePassBuilder* builder) {
		builder->WithShaderResource(Builtin::PerObjectBuffer, Builtin::PerMeshBuffer, Builtin::PerMeshInstanceBuffer, Builtin::PreSkinningVertices, Builtin::NormalMatrixBuffer)
			.WithUnorderedAccess(Builtin::PostSkinningVertices);
	}

	void Setup() override {
		auto& ecsWorld = ECSManager::GetInstance().GetWorld();
		opaqueQuery = ecsWorld.query_builder<Components::OpaqueSkinned, Components::ObjectDrawInfo, Components::OpaqueMeshInstances>().cached().cache_kind(flecs::QueryCacheAll).build();
		alphaTestQuery = ecsWorld.query_builder<Components::AlphaTestSkinned, Components::ObjectDrawInfo, Components::AlphaTestMeshInstances>().cached().cache_kind(flecs::QueryCacheAll).build();
		blendQuery = ecsWorld.query_builder<Components::BlendSkinned, Components::ObjectDrawInfo, Components::BlendMeshInstances>().cached().cache_kind(flecs::QueryCacheAll).build();
		CreatePSO();

		RegisterSRV(Builtin::PreSkinningVertices);
		RegisterSRV(Builtin::NormalMatrixBuffer);
		RegisterSRV(Builtin::PerObjectBuffer);
		RegisterSRV(Builtin::PerMeshInstanceBuffer);
		RegisterSRV(Builtin::PerMeshBuffer);

		RegisterUAV(Builtin::PostSkinningVertices);
	}

	PassReturn Execute(RenderContext& context) override {
		auto& commandList = context.commandList;

		// Set the descriptor heaps
		commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());

		commandList.BindLayout(PSOManager::GetInstance().GetComputeRootSignature().GetHandle());
		commandList.BindPipeline(m_PSO.GetAPIPipelineState().GetHandle());

		BindResourceDescriptorIndices(commandList, m_PSO.GetResourceDescriptorSlots());

		auto meshShadersEnabled = getMeshShadersEnabled();

		unsigned int perMeshConstants[NumPerMeshRootConstants] = {};
		opaqueQuery.each([&](flecs::entity e, Components::OpaqueSkinned s, Components::ObjectDrawInfo drawInfo, Components::OpaqueMeshInstances meshInstances) {
			auto& meshes = meshInstances.meshInstances;

			commandList.PushConstants(rhi::ShaderStage::Compute, 0, PerObjectRootSignatureIndex, PerObjectBufferIndex, 1, &drawInfo.perObjectCBIndex);

			for (auto& pMesh : meshes) {
				auto& mesh = *pMesh->GetMesh();
				perMeshConstants[PerMeshBufferIndex] = static_cast<unsigned int>(mesh.GetPerMeshBufferView()->GetOffset() / sizeof(PerMeshCB));
				perMeshConstants[PerMeshInstanceBufferIndex] = static_cast<uint32_t>(pMesh->GetPerMeshInstanceBufferOffset() / sizeof(PerMeshInstanceCB));
				commandList.PushConstants(rhi::ShaderStage::Compute, 0, PerMeshRootSignatureIndex, PerMeshBufferIndex, NumPerMeshRootConstants, &perMeshConstants);

				unsigned int numGroups = static_cast<unsigned int>(std::ceil(mesh.GetNumVertices(meshShadersEnabled) / 64.0));
				commandList.Dispatch(numGroups, 1, 1);
			}
			});

		alphaTestQuery.each([&](flecs::entity e, Components::AlphaTestSkinned s, Components::ObjectDrawInfo drawInfo, Components::AlphaTestMeshInstances meshInstances) {
			auto& meshes = meshInstances.meshInstances;

			commandList.PushConstants(rhi::ShaderStage::Compute, 0, PerObjectRootSignatureIndex, PerObjectBufferIndex, 1, &drawInfo.perObjectCBIndex);

			for (auto& pMesh : meshes) {
				auto& mesh = *pMesh->GetMesh();
				perMeshConstants[PerMeshBufferIndex] = static_cast<unsigned int>(mesh.GetPerMeshBufferView()->GetOffset() / sizeof(PerMeshCB));
				perMeshConstants[PerMeshInstanceBufferIndex] = static_cast<uint32_t>(pMesh->GetPerMeshInstanceBufferOffset() / sizeof(PerMeshInstanceCB));
				commandList.PushConstants(rhi::ShaderStage::Compute, 0, PerMeshRootSignatureIndex, PerMeshBufferIndex, NumPerMeshRootConstants, &perMeshConstants);

				unsigned int numGroups = static_cast<uint32_t>(std::ceil(mesh.GetNumVertices(meshShadersEnabled) / 64.0));
				commandList.Dispatch(numGroups, 1, 1);
			}
			});

		blendQuery.each([&](flecs::entity e, Components::BlendSkinned s, Components::ObjectDrawInfo drawInfo, Components::BlendMeshInstances meshInstances) {
			auto& meshes = meshInstances.meshInstances;

			commandList.PushConstants(rhi::ShaderStage::Compute, 0, PerObjectRootSignatureIndex, PerObjectBufferIndex, 1, &drawInfo.perObjectCBIndex);

			for (auto& pMesh : meshes) {
				auto& mesh = *pMesh->GetMesh();
				perMeshConstants[PerMeshBufferIndex] = static_cast<unsigned int>(mesh.GetPerMeshBufferView()->GetOffset() / sizeof(PerMeshCB));
				perMeshConstants[PerMeshInstanceBufferIndex] = static_cast<uint32_t>(pMesh->GetPerMeshInstanceBufferOffset() / sizeof(PerMeshInstanceCB));
				commandList.PushConstants(rhi::ShaderStage::Compute, 0, PerMeshRootSignatureIndex, PerMeshBufferIndex, NumPerMeshRootConstants, &perMeshConstants);

				unsigned int numGroups = static_cast<unsigned int>(std::ceil(mesh.GetNumVertices(meshShadersEnabled) / 64.0));
				commandList.Dispatch(numGroups, 1, 1);
			}
			});
		return {};
	}

	void Cleanup(RenderContext& context) override {

	}

private:

	void CreatePSO() {
		m_PSO = PSOManager::GetInstance().MakeComputePipeline(
			PSOManager::GetInstance().GetComputeRootSignature(),
			L"shaders/skinning.brsl",
			L"CSMain",
			{},
			"Skinning CS");
	}

	flecs::query<Components::OpaqueSkinned, Components::ObjectDrawInfo, Components::OpaqueMeshInstances> opaqueQuery;
	flecs::query<Components::AlphaTestSkinned, Components::ObjectDrawInfo, Components::AlphaTestMeshInstances> alphaTestQuery;
	flecs::query<Components::BlendSkinned, Components::ObjectDrawInfo, Components::BlendMeshInstances> blendQuery;
	PipelineState m_PSO;

	std::function<bool()> getMeshShadersEnabled;
};