#pragma once

#include "RenderPasses/Base/ComputePass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Managers/Singletons/SettingsManager.h"
#include "../shaders/PerPassRootConstants/lightCullingRootConstants.h"

class LightCullingPass : public ComputePass {
public:
	LightCullingPass() {
		getClusterSize = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT3>("lightClusterSize");
		CreatePSO();
	}

	~LightCullingPass() {
	}

	void DeclareResourceUsages(ComputePassBuilder* builder) {
		builder->WithShaderResource(Builtin::CameraBuffer, Builtin::Light::ActiveLightIndices, Builtin::Light::InfoBuffer)
			.WithUnorderedAccess(Builtin::Light::ClusterBuffer, Builtin::Light::PagesBuffer, Builtin::Light::PagesCounter);
	}

	void Setup() override {

		m_lightPagesCounterHandle = m_resourceRegistryView->RequestHandle(Builtin::Light::PagesCounter);
		m_pLightPagesCounter = m_resourceRegistryView->Resolve<Buffer>(m_lightPagesCounterHandle);

		RegisterSRV(Builtin::CameraBuffer);
		RegisterSRV(Builtin::Light::ActiveLightIndices);
		RegisterSRV(Builtin::Light::InfoBuffer);

		RegisterUAV(Builtin::Light::ClusterBuffer);
		RegisterUAV(Builtin::Light::PagesCounter);
		RegisterUAV(Builtin::Light::PagesBuffer);
	}

	PassReturn Execute(PassExecutionContext& executionContext) override {
		auto* renderContext = executionContext.hostData ? const_cast<RenderContext*>(executionContext.hostData->Get<RenderContext>()) : nullptr;
		if (!renderContext) return {};
		auto& context = *renderContext;
		auto& commandList = context.commandList;

		// Set the descriptor heaps
		commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());

		commandList.BindLayout(PSOManager::GetInstance().GetComputeRootSignature().GetHandle());
		commandList.BindPipeline(m_PSO.GetAPIPipelineState().GetHandle());

		BindResourceDescriptorIndices(commandList, m_PSO.GetResourceDescriptorSlots());

		unsigned int miscUintRootConstants[NumMiscUintRootConstants] = {};
		miscUintRootConstants[LIGHT_PAGES_POOL_SIZE] = context.lightManager->GetLightPagePoolSize();
		commandList.PushConstants(rhi::ShaderStage::Compute, 0, MiscUintRootSignatureIndex, 0, NumMiscUintRootConstants, miscUintRootConstants);

		auto clusterSize = getClusterSize();
		unsigned int numThreadGroups = static_cast<unsigned int>(std::ceil(((float)(clusterSize.x * clusterSize.y * clusterSize.z)) / 128));
		commandList.Dispatch(numThreadGroups, 1, 1);
		return {};
	}

	void Cleanup() override {

	}

	virtual void Update(const UpdateExecutionContext& context) override {
		// Reset UAV counter
		uint32_t zero = 0;
		BUFFER_UPLOAD(&zero, sizeof(uint32_t), UploadManager::UploadTarget::FromHandle(m_lightPagesCounterHandle), 0);
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