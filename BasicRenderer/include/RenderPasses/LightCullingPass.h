#pragma once

#include "RenderPasses/Base/TypedRenderGraphPass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Managers/Singletons/SettingsManager.h"
#include "../shaders/PerPassRootConstants/lightCullingRootConstants.h"
#include "RenderPasses/PreparedComputeDispatch.h"

class LightCullingPass : public org::TypedRenderGraphPass<LightCullingPass, br::render::PreparedComputeDispatch> {
public:
	LightCullingPass() {
		getClusterSize = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT3>("lightClusterSize");
		CreatePSO();
	}

	~LightCullingPass() {
	}

	void Declare(org::PassBuilder& builder) {
		builder.WithShaderResource(Builtin::CameraBuffer, Builtin::Light::ActiveLightIndices, Builtin::Light::InfoBuffer)
			.WithUnorderedAccess(Builtin::Light::ClusterBuffer, Builtin::Light::PagesBuffer, Builtin::Light::PagesCounter);
		builder.WithConstantBuffer(Builtin::PerFrameBuffer)
			.PreferQueue(org::QueueKind::Compute);
	}

	void Initialize() {

		m_lightPagesCounterHandle = m_resourceRegistryView->RequestHandle(Builtin::Light::PagesCounter);
		m_pLightPagesCounter = m_resourceRegistryView->Resolve<Buffer>(m_lightPagesCounterHandle);
	}

	br::render::PreparedComputeDispatch Prepare(const org::PassPrepareContext& preparation) {
		const auto* update = preparation.preparationData->Get<UpdateContext>();
		const auto* render = preparation.preparationData->Get<RenderContext>();
		if (!update && !render) throw std::logic_error("LightCullingPass requires frame context");
		br::render::PreparedComputeDispatch data{};
		data.layout = PSOManager::GetInstance().GetComputeRootSignature().GetHandle();
		auto program = CaptureProgramBinding(preparation, m_PSO);
		data.program = program.program;
		data.descriptorIndices = std::move(program.descriptorIndices);
		const auto state = update ? update->publishedRendererState : render->publishedRendererState;
		const auto lights = state
			? state->lights.payload.Get<br::render::PublishedLightTableState>() : nullptr;
		data.constants[LIGHT_PAGES_POOL_SIZE] = lights
			? lights->lightPagePoolSize
			: (update ? update->preparedLightPagePoolSize : render->preparedLightPagePoolSize);
		const auto clusterSize = getClusterSize();
		data.groupsX = (clusterSize.x * clusterSize.y * clusterSize.z + 127u) / 128u;
		return data;
	}
	static void Record(const br::render::PreparedComputeDispatch& data, org::PassRecordContext& recording) {
		br::render::RecordPreparedComputeDispatch(data, recording);
	}

	void Update(const UpdateExecutionContext& context) override {
		// Reset UAV counter
		uint32_t zero = 0;
		BUFFER_UPLOAD(&zero, sizeof(uint32_t), org::runtime::UploadTarget::FromHandle(m_lightPagesCounterHandle), 0);
	}

private:

	Buffer* m_pLightPagesCounter = nullptr;
	ResourceRegistry::RegistryHandle m_lightPagesCounterHandle;

	void CreatePSO() {
		m_PSO = PSOManager::GetInstance().MakeComputePipeline(
			PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
			L"shaders/lightCulling.hlsl",
			L"CSMain",
			{},
			"Light Culling CS");
	}

	std::function<DirectX::XMUINT3()> getClusterSize;
	PipelineState m_PSO;
};
