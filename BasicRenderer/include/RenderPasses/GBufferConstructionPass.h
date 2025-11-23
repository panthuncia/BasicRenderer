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
		builder->WithShaderResource(Builtin::CameraBuffer,
			Builtin::Environment::PrefilteredCubemapsGroup,
			Builtin::Light::ActiveLightIndices,
			Builtin::Light::InfoBuffer,
			Builtin::Light::PointLightCubemapBuffer,
			Builtin::Light::DirectionalLightCascadeBuffer,
			Builtin::Light::SpotLightMatrixBuffer,
			Builtin::Environment::InfoBuffer,
			Builtin::GBuffer::Normals,
			Builtin::GBuffer::Albedo,
			Builtin::GBuffer::Emissive,
			Builtin::GBuffer::MetallicRoughness,
			Builtin::PrimaryCamera::DepthTexture,
			Builtin::Environment::CurrentCubemap,
			Builtin::Shadows::ShadowMaps)
			.WithUnorderedAccess(Builtin::Color::HDRColorTarget);

		if (m_clusteredLightingEnabled) {
			builder->WithShaderResource(Builtin::Light::ClusterBuffer, Builtin::Light::PagesBuffer);
		}

		if (m_gtaoEnabled) {
			builder->WithShaderResource(Builtin::GTAO::OutputAOTerm);
		}
	}

	void Setup() override {

		if (m_clusteredLightingEnabled) {
			RegisterSRV(Builtin::Light::ClusterBuffer);
			RegisterSRV(Builtin::Light::PagesBuffer);
		}

		RegisterSRV(Builtin::Light::ActiveLightIndices);
		RegisterSRV(Builtin::Light::InfoBuffer);
		RegisterSRV(Builtin::Light::PointLightCubemapBuffer);
		RegisterSRV(Builtin::Light::SpotLightMatrixBuffer);
		RegisterSRV(Builtin::Light::DirectionalLightCascadeBuffer);
		RegisterSRV(Builtin::Environment::InfoBuffer);
		RegisterSRV(Builtin::CameraBuffer);

		if (m_gtaoEnabled)
			RegisterSRV(Builtin::GTAO::OutputAOTerm);

		RegisterSRV(Builtin::GBuffer::Normals);
		RegisterSRV(Builtin::GBuffer::Albedo);
		RegisterSRV(Builtin::GBuffer::Emissive);
		RegisterSRV(Builtin::GBuffer::MetallicRoughness);
		RegisterSRV(Builtin::PrimaryCamera::DepthTexture);

		RegisterUAV(Builtin::Color::HDRColorTarget);

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