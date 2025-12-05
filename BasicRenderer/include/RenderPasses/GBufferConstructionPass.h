#pragma once

#include <unordered_map>
#include <functional>

#include "RenderPasses/Base/ComputePass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "Managers/Singletons/SettingsManager.h"


class GBufferConstructionPass : public ComputePass {
public:
	GBufferConstructionPass() {
		auto& settingsManager = SettingsManager::GetInstance();
		getImageBasedLightingEnabled = settingsManager.getSettingGetter<bool>("enableImageBasedLighting");
		getPunctualLightingEnabled = settingsManager.getSettingGetter<bool>("enablePunctualLighting");
		getShadowsEnabled = settingsManager.getSettingGetter<bool>("enableShadows");
		m_gtaoEnabled = settingsManager.getSettingGetter<bool>("enableGTAO")();
		m_clusteredLightingEnabled = settingsManager.getSettingGetter<bool>("enableClusteredLighting")();
	}

	void DeclareResourceUsages(ComputePassBuilder* builder) override {
		builder->WithShaderResource(MESH_RESOURCE_IDFENTIFIERS,
			Builtin::PrimaryCamera::VisibilityTexture,
			Builtin::PrimaryCamera::VisibleClusterTable,
			Builtin::PerMeshInstanceBuffer,
			Builtin::PerObjectBuffer,
			Builtin::PerMeshBuffer,
			Builtin::CameraBuffer,
			Builtin::PostSkinningVertices,
			Builtin::NormalMatrixBuffer)
			.WithUnorderedAccess(Builtin::GBuffer::Normals,
				Builtin::GBuffer::Albedo,
				Builtin::GBuffer::Emissive,
				Builtin::GBuffer::MetallicRoughness);
	}

	void Setup() override {

		RegisterSRV(Builtin::MeshResources::MeshletOffsets);
		RegisterSRV(Builtin::MeshResources::MeshletVertexIndices);
		RegisterSRV(Builtin::MeshResources::MeshletTriangles);
		RegisterSRV(Builtin::PerMeshInstanceBuffer);
		RegisterSRV(Builtin::PerObjectBuffer);
		RegisterSRV(Builtin::PerMeshBuffer);
		RegisterSRV(Builtin::PrimaryCamera::VisibilityTexture);
		RegisterSRV(Builtin::PrimaryCamera::VisibleClusterTable);
		RegisterSRV(Builtin::CameraBuffer);
		RegisterSRV(Builtin::PostSkinningVertices);
		RegisterSRV(Builtin::NormalMatrixBuffer);

		RegisterUAV(Builtin::GBuffer::Normals);
		RegisterUAV(Builtin::GBuffer::Albedo);
		RegisterUAV(Builtin::GBuffer::Emissive);
		RegisterUAV(Builtin::GBuffer::MetallicRoughness);

		CreatePSO();
	}

	PassReturn Execute(RenderContext& context) override {
		auto& psoManager = PSOManager::GetInstance();
		auto& commandList = context.commandList;

		commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(),
			context.samplerDescriptorHeap.GetHandle());

		commandList.BindLayout(psoManager.GetComputeRootSignature().GetHandle());

		commandList.BindPipeline(m_pso.GetAPIPipelineState().GetHandle());

		BindResourceDescriptorIndices(commandList, m_pso.GetResourceDescriptorSlots());

		uint32_t w = context.renderResolution.x;
		uint32_t h = context.renderResolution.y;
		const uint32_t groupSizeX = 8;
		const uint32_t groupSizeY = 8;
		uint32_t groupsX = (w + groupSizeX - 1) / groupSizeX;
		uint32_t groupsY = (h + groupSizeY - 1) / groupSizeY;

		commandList.Dispatch(groupsX, groupsY, 1);
		return {};
	}

	void Cleanup(RenderContext& context) override {
		// Cleanup the render pass
	}

private:

	std::function<bool()> getImageBasedLightingEnabled;
	std::function<bool()> getPunctualLightingEnabled;
	std::function<bool()> getShadowsEnabled;

	bool m_gtaoEnabled = true;
	bool m_clusteredLightingEnabled = true;

	PipelineState m_pso;

	void CreatePSO() {
		m_pso = PSOManager::GetInstance().MakeComputePipeline(
			PSOManager::GetInstance().GetComputeRootSignature(),
			L"shaders/gbuffer.hlsl",
			L"GBufferConstructionCSMain",
			{},
			"GBufferConstructionPSO"
		);
	}
};