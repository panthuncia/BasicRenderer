#pragma once

#include "RenderPasses/Base/ComputePass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Utilities/Utilities.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Managers/MeshManager.h"
#include "Managers/ObjectManager.h"
#include "Managers/Singletons/ECSManager.h"

class LightCullingPass : public ComputePass {
public:
	LightCullingPass() {
		getClusterSize = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT3>("lightClusterSize");
	}

	~LightCullingPass() {
	}

	void DeclareResourceUsages(ComputePassBuilder* builder) {
		builder->WithShaderResource(Builtin::CameraBuffer, Builtin::Light::ActiveLightIndices, Builtin::Light::InfoBuffer)
			.WithUnorderedAccess(Builtin::Light::ClusterBuffer, Builtin::Light::PagesBuffer, Builtin::Light::PagesCounter);
	}

	void Setup() override {
		CreatePSO();

		m_pLightPagesCounter = m_resourceRegistryView->Request<Buffer>(Builtin::Light::PagesCounter);
	
		RegisterSRV(Builtin::CameraBuffer);
		RegisterSRV(Builtin::Light::ActiveLightIndices);
		RegisterSRV(Builtin::Light::InfoBuffer);

		RegisterUAV(Builtin::Light::ClusterBuffer);
		RegisterUAV(Builtin::Light::PagesCounter);
		RegisterUAV(Builtin::Light::PagesBuffer);
	}

	PassReturn Execute(RenderContext& context) override {
		auto& commandList = context.commandList;

		// Set the descriptor heaps
		commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());

		commandList.BindLayout(PSOManager::GetInstance().GetComputeRootSignature().GetHandle());
		commandList.BindPipeline(m_PSO.GetAPIPipelineState().GetHandle());

		BindResourceDescriptorIndices(commandList, m_PSO.GetResourceDescriptorSlots());

		unsigned int lightClusterConstants[NumLightClusterRootConstants] = {};
		lightClusterConstants[LightPagesPoolSize] = context.lightManager->GetLightPagePoolSize();
		commandList.PushConstants(rhi::ShaderStage::Compute, 0, LightClusterRootSignatureIndex, 0, NumLightClusterRootConstants, lightClusterConstants);

		auto clusterSize = getClusterSize();
		unsigned int numThreadGroups = static_cast<unsigned int>(std::ceil(((float)(clusterSize.x * clusterSize.y * clusterSize.z)) / 128));
		commandList.Dispatch(numThreadGroups, 1, 1);
		return {};
	}

	void Cleanup(RenderContext& context) override {

	}

	virtual void Update() override {
		// Reset UAV counter
		uint32_t zero = 0;
		BUFFER_UPLOAD(&zero, sizeof(uint32_t), m_pLightPagesCounter, 0);
	}

private:

	std::shared_ptr<Buffer> m_pLightPagesCounter = nullptr;

	void CreatePSO() {
		m_PSO = PSOManager::GetInstance().MakeComputePipeline(
			PSOManager::GetInstance().GetComputeRootSignature(),
			L"shaders/lightCulling.hlsl",
			L"CSMain",
			{},
			"Light Culling CS");
	}

	std::function<DirectX::XMUINT3()> getClusterSize;
	PipelineState m_PSO;
};