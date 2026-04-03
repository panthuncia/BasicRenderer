#pragma once

#include <functional>

#include "RenderPasses/Base/ComputePass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "Scene/Scene.h"
#include "Managers/Singletons/SettingsManager.h"

class DeferredShadingPass : public ComputePass {
public:
	DeferredShadingPass() {
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
			Builtin::PrimaryCamera::LinearDepthMap,
			Builtin::Environment::CurrentCubemap,
			Builtin::Shadows::CLodClipmapInfo,
			Builtin::Shadows::CLodDirectionalPageViewInfo,
			Builtin::Shadows::CLodPageTable,
			Builtin::Shadows::CLodPhysicalPages,
			Builtin::Shadows::ShadowMaps)
			.WithUnorderedAccess(Builtin::Color::HDRColorTarget,
				Builtin::DebugVisualization);

		if (m_clusteredLightingEnabled) {
			builder->WithShaderResource(Builtin::Light::ClusterBuffer, Builtin::Light::PagesBuffer);
		}

		if (m_gtaoEnabled) {
			builder->WithShaderResource(Builtin::GTAO::OutputAOTerm);
		}
	}

	void Setup() override {
		RegisterSRV(SRVViewType::Texture2DArrayFull, Builtin::Shadows::CLodPageTable);
	}

	PassReturn Execute(PassExecutionContext& executionContext) override {
	    auto* renderContext = executionContext.hostData->Get<RenderContext>();
	    auto& context = *renderContext;
		auto& psoManager = PSOManager::GetInstance();
		auto& commandList = executionContext.commandList;

		commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(),
			context.samplerDescriptorHeap.GetHandle());

		commandList.BindLayout(psoManager.GetComputeRootSignature().GetHandle());

		auto& pso = psoManager.GetDeferredPSO(context.globalPSOFlags);
		commandList.BindPipeline(pso.GetAPIPipelineState().GetHandle());

		BindResourceDescriptorIndices(commandList, pso.GetResourceDescriptorSlots());

		unsigned int settings[NumSettingsRootConstants] = {};
		settings[EnableShadows] = getShadowsEnabled();
		settings[EnablePunctualLights] = getPunctualLightingEnabled();
		settings[EnableGTAO] = m_gtaoEnabled;
		commandList.PushConstants(rhi::ShaderStage::Compute, 0,
			SettingsRootSignatureIndex, 0,
			NumSettingsRootConstants, settings);

		uint32_t w = context.renderResolution.x;
		uint32_t h = context.renderResolution.y;
		const uint32_t groupSizeX = 8;
		const uint32_t groupSizeY = 8;
		uint32_t groupsX = (w + groupSizeX - 1) / groupSizeX;
		uint32_t groupsY = (h + groupSizeY - 1) / groupSizeY;

		commandList.Dispatch(groupsX, groupsY, 1);
		return {};
	}

	void Cleanup() override {
		// Cleanup the render pass
	}

private:

	std::function<bool()> getImageBasedLightingEnabled;
	std::function<bool()> getPunctualLightingEnabled;
	std::function<bool()> getShadowsEnabled;

	bool m_gtaoEnabled = true;
	bool m_clusteredLightingEnabled = true;
};