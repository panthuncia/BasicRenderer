#pragma once

#include "RenderPasses/Base/ComputePass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Utilities/Utilities.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Managers/Singletons/ECSManager.h"

class SkinningPass : public ComputePass {
public:
	SkinningPass() {
		getMeshShadersEnabled = SettingsManager::GetInstance().getSettingGetter<bool>("enableMeshShader");
		auto& ecsWorld = ECSManager::GetInstance().GetWorld();
		skinnedQuery = ecsWorld.query_builder<Components::Skinned, Components::ObjectDrawInfo, Components::MeshInstances>().cached().cache_kind(flecs::QueryCacheAll).build();
		CreatePSO();
	}

	~SkinningPass() {
	}

	void DeclareResourceUsages(ComputePassBuilder* builder) {
		builder->WithShaderResource(
			Builtin::PerObjectBuffer, 
			Builtin::PerMeshBuffer, 
			Builtin::PerMeshInstanceBuffer, 
			Builtin::PreSkinningVertices, 
			Builtin::NormalMatrixBuffer, 
			Builtin::SkeletonResources::InverseBindMatrices,
			Builtin::SkeletonResources::BoneTransforms,
			Builtin::SkeletonResources::SkinningInstanceInfo)
			.WithUnorderedAccess(Builtin::PostSkinningVertices);
	}

	void Setup() override {

		RegisterSRV(Builtin::PreSkinningVertices);
		RegisterSRV(Builtin::NormalMatrixBuffer);
		RegisterSRV(Builtin::PerObjectBuffer);
		RegisterSRV(Builtin::PerMeshInstanceBuffer);
		RegisterSRV(Builtin::PerMeshBuffer);
		RegisterSRV(Builtin::SkeletonResources::InverseBindMatrices);
		RegisterSRV(Builtin::SkeletonResources::BoneTransforms);
		RegisterSRV(Builtin::SkeletonResources::SkinningInstanceInfo);

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
		skinnedQuery.each([&](flecs::entity e, Components::Skinned s, Components::ObjectDrawInfo drawInfo, Components::MeshInstances meshInstances) {
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
			L"shaders/skinning.hlsl",
			L"CSMain",
			{},
			"Skinning CS");
	}

	flecs::query<Components::Skinned, Components::ObjectDrawInfo, Components::MeshInstances> skinnedQuery;
	PipelineState m_PSO;

	std::function<bool()> getMeshShadersEnabled;
};