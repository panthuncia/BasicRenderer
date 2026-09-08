#pragma once

#include <functional>

#include "RenderPasses/Base/TypedRenderGraphPass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "Scene/Scene.h"
#include "Managers/Singletons/SettingsManager.h"
#include "RenderPasses/PreparedComputeDispatch.h"

class DeferredShadingPass : public org::TypedRenderGraphPass<DeferredShadingPass, br::render::PreparedComputeDispatch> {
public:
	explicit DeferredShadingPass(bool skyboxEnabled = false)
		: m_skyboxEnabled(skyboxEnabled) {
		auto& settingsManager = SettingsManager::GetInstance();
		getImageBasedLightingEnabled = settingsManager.getSettingGetter<bool>("enableImageBasedLighting");
		getPunctualLightingEnabled = settingsManager.getSettingGetter<bool>("enablePunctualLighting");
		getShadowsEnabled = settingsManager.getSettingGetter<bool>("enableShadows");
		m_gtaoEnabled = settingsManager.getSettingGetter<bool>("enableGTAO")();
		m_clusteredLightingEnabled = settingsManager.getSettingGetter<bool>("enableClusteredLighting")();
	}

	void Declare(org::PassBuilder& builder) {
		builder.WithShaderResource(Builtin::CameraBuffer,
			Builtin::Environment::PrefilteredCubemapsGroup,
			Builtin::Light::ActiveLightIndices,
			Builtin::Light::InfoBuffer,
			Builtin::Light::PointLightCubemapBuffer,
			Builtin::Light::DirectionalLightCascadeBuffer,
			Builtin::Light::SpotLightMatrixBuffer,
			Builtin::Environment::InfoBuffer,
			Builtin::PerMaterialOpenPBRDataBuffer,
			Builtin::Surface::BaseColorOpacity,
			Builtin::Surface::NormalRoughness,
			Builtin::Surface::SpecularAo,
			Builtin::Surface::Emissive,
			Builtin::Surface::Identity,
			Builtin::Surface::Payload0,
			Builtin::Surface::Payload1,
			Builtin::Surface::Records,
			Builtin::Environment::CurrentCubemap,
			Builtin::OpenPBR::FuzzLTC,
			Builtin::OpenPBR::IdealMetalEnergyComplement,
			Builtin::OpenPBR::IdealMetalAverageEnergyComplement,
			Builtin::OpenPBR::OpaqueDielectricEnergyComplement,
			Builtin::OpenPBR::OpaqueDielectricAverageEnergyComplement,
			Builtin::Noise::BlueNoise2D)
			.WithShaderResource(Subresources(Builtin::PrimaryCamera::LinearDepthMap, Mip{ 0, 1 }))
			.WithUnorderedAccess(Builtin::Color::HDRColorTarget,
				Builtin::DebugVisualization,
				Builtin::Surface::Motion);

			if (getShadowsEnabled()) {
				builder.WithShaderResource(Builtin::Shadows::CLodClipmapInfo,
					Builtin::Shadows::CLodCompactMainCamera,
					Builtin::Shadows::CLodCompactShadowCameras,
					Builtin::Shadows::CLodDirectionalPageViewInfo,
					Builtin::Shadows::CLodPageMetadata,
					Builtin::Shadows::CLodPageTable,
					Builtin::Shadows::CLodPhysicalPages)
					.WithUnorderedAccess(Builtin::Shadows::CLodStats);
			}

		if (m_clusteredLightingEnabled) {
			builder.WithShaderResource(Builtin::Light::ClusterBuffer, Builtin::Light::PagesBuffer);
		}

		if (m_gtaoEnabled) {
			builder.WithShaderResource(Builtin::GTAO::OutputAOTerm);
		}

		builder.WithConstantBuffer(Builtin::PerFrameBuffer)
			.PreferQueue(org::QueueKind::Compute);
	}

	void Initialize() {
		RegisterSRV(SRVViewType::Texture2DArrayFull, Builtin::OpenPBR::OpaqueDielectricEnergyComplement);
		if (getShadowsEnabled()) {
			RegisterSRV(SRVViewType::Texture2DArrayFull, Builtin::Shadows::CLodPageTable);
		}
	}

	br::render::PreparedComputeDispatch Prepare(const org::PassPrepareContext& preparation) {
		const auto* update = preparation.preparationData->Get<UpdateContext>();
		const auto* render = preparation.preparationData->Get<RenderContext>();
		if (!update && !render) throw std::logic_error("DeferredShadingPass requires frame context");
		const auto globalFlags = update ? update->globalPSOFlags : render->globalPSOFlags;
		const auto resolution = update ? update->renderResolution : render->renderResolution;
		auto& pso = PSOManager::GetInstance().GetDeferredPSO(globalFlags);
		br::render::PreparedComputeDispatch data{};
		data.layout = PSOManager::GetInstance().GetComputeRootSignature().GetHandle();
		auto program = CaptureProgramBinding(preparation, pso);
		data.program = program.program;
		data.descriptorIndices = std::move(program.descriptorIndices);
		data.constants[MiscEnableShadows] = getShadowsEnabled();
		data.constants[MiscEnableShadows + 1] = getPunctualLightingEnabled();
		data.constants[MiscEnableShadows + 2] = m_gtaoEnabled;
		data.constants[MiscEnableShadows + 3] = m_skyboxEnabled;
		data.groupsX = (resolution.x + 7u) / 8u; data.groupsY = (resolution.y + 7u) / 8u;
		return data;
	}
	static void Record(const br::render::PreparedComputeDispatch& data, org::PassRecordContext& recording) {
		br::render::RecordPreparedComputeDispatch(data, recording);
	}

private:

	std::function<bool()> getImageBasedLightingEnabled;
	std::function<bool()> getPunctualLightingEnabled;
	std::function<bool()> getShadowsEnabled;

	bool m_gtaoEnabled = true;
	bool m_clusteredLightingEnabled = true;
	bool m_skyboxEnabled = false;
};
