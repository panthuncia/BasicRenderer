#pragma once

#include "RenderPasses/Base/RenderPass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Utilities/Utilities.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Managers/Singletons/CommandSignatureManager.h"
#include "../../shaders/PerPassRootConstants/meshletCullingRootConstants.h"

class VisibleClusterTableCounterResetPass : public RenderPass {
	public:
	VisibleClusterTableCounterResetPass() {
	}
	~VisibleClusterTableCounterResetPass() {
	}

	void DeclareResourceUsages(RenderPassBuilder* builder) override {
		builder->WithCopyDest(Builtin::PrimaryCamera::VisibleClusterTableCounter);
	}

	void Setup() override {
		m_counter = m_resourceRegistryView->RequestPtr<GloballyIndexedResource>(Builtin::PrimaryCamera::VisibleClusterTableCounter);
	}

	void Cleanup() override {

	}

	PassReturn Execute(RenderContext& context) override {
		auto& commandList = context.commandList;
		// Copy zero to the counter buffer
		auto counterReset = ResourceManager::GetInstance().GetUAVCounterReset();

		auto counterOffset = m_counter->GetUAVCounterOffset();
		auto apiResource = m_counter->GetAPIResource();
		commandList.CopyBufferRegion(apiResource.GetHandle(), counterOffset, counterReset.GetHandle(), 0, sizeof(UINT));

		return {};
	}
private:
	GloballyIndexedResource* m_counter;
};

struct MeshletCullingPassInputs {
	bool isRemaindersPass, doResets;

	friend bool operator==(const MeshletCullingPassInputs&, const MeshletCullingPassInputs&) = default;
};

inline rg::Hash64 HashValue(const MeshletCullingPassInputs& i) {
	std::size_t seed = 0;

	boost::hash_combine(seed, i.isRemaindersPass);
	boost::hash_combine(seed, i.doResets);
	return seed;
}

class MeshletCullingPass : public ComputePass {
public:
	MeshletCullingPass(MeshletCullingPassInputs inputs) {
		m_isRemaindersPass = inputs.isRemaindersPass;
		m_doResets = inputs.doResets;

		getNumDirectionalLightCascades = SettingsManager::GetInstance().getSettingGetter<uint8_t>("numDirectionalLightCascades");
		getShadowsEnabled = SettingsManager::GetInstance().getSettingGetter<bool>("enableShadows");
		m_occlusionCullingEnabled = SettingsManager::GetInstance().getSettingGetter<bool>("enableOcclusionCulling")();

		auto& ecsWorld = ECSManager::GetInstance().GetWorld();
		lightQuery = ecsWorld.query_builder<Components::Light, Components::LightViewInfo, Components::DepthMap>().cached().cache_kind(flecs::QueryCacheAll).build();

		CreatePSO();
	}

	~MeshletCullingPass() {
	}

	void Setup() override {
		RegisterSRV(Builtin::PerObjectBuffer);
		RegisterSRV(Builtin::CameraBuffer);
		RegisterSRV(Builtin::PerMeshBuffer);
		RegisterSRV(Builtin::PerMeshInstanceBuffer);
		RegisterSRV(Builtin::MeshResources::MeshletBounds);

		RegisterUAV(Builtin::PrimaryCamera::VisibleClusterTable);
		RegisterUAV(Builtin::PrimaryCamera::VisibleClusterTableCounter);
		RegisterUAV(Builtin::MeshResources::ClusterToVisibleClusterTableIndexBuffer);

		m_primaryCameraMeshletCullingBitfieldBuffer = m_resourceRegistryView->RequestPtr<DynamicGloballyIndexedResource>(Builtin::PrimaryCamera::MeshletBitfield);
		m_primaryCameraMeshletCullingIndirectCommandBuffer = m_resourceRegistryView->RequestPtr<DynamicGloballyIndexedResource>(Builtin::PrimaryCamera::IndirectCommandBuffers::MeshletCulling);
		m_primaryCameraMeshletCullingResetIndirectCommandBuffer = m_resourceRegistryView->RequestPtr<DynamicGloballyIndexedResource>(Builtin::PrimaryCamera::IndirectCommandBuffers::MeshletCullingReset);

		m_primaryCameraLinearDepthMap = m_resourceRegistryView->RequestPtr<PixelBuffer>(Builtin::PrimaryCamera::LinearDepthMap);

		m_counter = m_resourceRegistryView->RequestPtr<Buffer>(Builtin::PrimaryCamera::VisibleClusterTableCounter);

	}

	void DeclareResourceUsages(ComputePassBuilder* builder) override {
		builder->WithShaderResource(Builtin::PerObjectBuffer, 
			Builtin::PerMeshBuffer, 
			Builtin::PerMeshInstanceBuffer, 
			Builtin::MeshResources::MeshletBounds, 
			Builtin::CameraBuffer,
			Builtin::PrimaryCamera::LinearDepthMap,
			Builtin::Shadows::LinearShadowMaps)
			.WithUnorderedAccess(Builtin::MeshletCullingBitfieldGroup, 
				Builtin::PrimaryCamera::MeshletBitfield,
				Builtin::PrimaryCamera::VisibleClusterTable,
				Builtin::PrimaryCamera::VisibleClusterTableCounter,
				Builtin::MeshResources::ClusterToVisibleClusterTableIndexBuffer)
			.WithIndirectArguments(
				Builtin::IndirectCommandBuffers::MeshletCulling, 
				Builtin::PrimaryCamera::IndirectCommandBuffers::MeshletCulling, 
				Builtin::PrimaryCamera::IndirectCommandBuffers::MeshletCullingReset);
	}

	PassReturn Execute(RenderContext& context) override {

		unsigned int numDraws = context.drawStats.numDrawsInScene;

		if (numDraws == 0) {
			return {};
		}

		auto& commandList = context.commandList;

		// Set the descriptor heaps
		commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());

		commandList.BindLayout(PSOManager::GetInstance().GetComputeRootSignature().GetHandle());

		auto& pso = m_cullingWithVisibilityDataPSO;

		commandList.BindPipeline(pso.GetAPIPipelineState().GetHandle());

		BindResourceDescriptorIndices(commandList, pso.GetResourceDescriptorSlots());

		unsigned int miscRootConstants[NumMiscUintRootConstants] = {};
		miscRootConstants[MESHLET_CULLING_BITFIELD_BUFFER_UAV_DESCRIPTOR_INDEX] = m_primaryCameraMeshletCullingBitfieldBuffer->GetResource()->GetUAVShaderVisibleInfo(0).slot.index;
		miscRootConstants[LINEAR_DEPTH_MAP_SRV_DESCRIPTOR_INDEX] = m_primaryCameraLinearDepthMap->GetSRVInfo(0).slot.index;

		commandList.PushConstants(rhi::ShaderStage::Compute, 0, MiscUintRootSignatureIndex, 0, NumMiscUintRootConstants, &miscRootConstants);

		unsigned int cameraIndex = context.viewManager->Get(context.currentScene->GetPrimaryCamera().get<Components::RenderViewRef>().viewID)->gpu.cameraBufferIndex;
		commandList.PushConstants(rhi::ShaderStage::Compute, 0, ViewRootSignatureIndex, LightViewIndex, 1, &cameraIndex);

		// Culling for main camera

		// Frustrum culling
		auto meshletCullingBuffer = m_primaryCameraMeshletCullingIndirectCommandBuffer;
		
		auto& commandSignature = CommandSignatureManager::GetInstance().GetDispatchCommandSignature();
		commandList.ExecuteIndirect(
			commandSignature.GetHandle(),
			meshletCullingBuffer->GetResource()->GetAPIResource().GetHandle(),
			0,
			meshletCullingBuffer->GetResource()->GetAPIResource().GetHandle(),
			meshletCullingBuffer->GetResource()->GetUAVCounterOffset(),
			numDraws
		);

		commandList.BindLayout(PSOManager::GetInstance().GetComputeRootSignature().GetHandle());
		commandList.BindPipeline(m_cullingPSO.GetAPIPipelineState().GetHandle());

		BindResourceDescriptorIndices(commandList, m_cullingPSO.GetResourceDescriptorSlots());

		if (getShadowsEnabled()) {

			// culling
			commandList.BindPipeline(m_cullingPSO.GetAPIPipelineState().GetHandle());
			lightQuery.each([&](flecs::entity e, Components::Light light, Components::LightViewInfo& lightViewInfo, Components::DepthMap lightDepth) {

				for (auto& viewID : lightViewInfo.viewIDs) {
					auto view = context.viewManager->Get(viewID);

					cameraIndex = view->gpu.cameraBufferIndex;
					commandList.PushConstants(rhi::ShaderStage::Compute, 0, ViewRootSignatureIndex, LightViewIndex, 1, &cameraIndex);

					miscRootConstants[MESHLET_CULLING_BITFIELD_BUFFER_UAV_DESCRIPTOR_INDEX] = view->gpu.meshletBitfieldBuffer->GetResource()->GetUAVShaderVisibleInfo(0).slot.index;
					miscRootConstants[LINEAR_DEPTH_MAP_SRV_DESCRIPTOR_INDEX] = light.type == Components::LightType::Point ? lightDepth.linearDepthMap->GetSRVInfo(SRVViewType::Texture2DArray, 0).slot.index : lightDepth.linearDepthMap->GetSRVInfo(0).slot.index;
					commandList.PushConstants(rhi::ShaderStage::Compute, 0, MiscUintRootSignatureIndex, 0, NumMiscUintRootConstants, &miscRootConstants);
					meshletCullingBuffer = view->gpu.indirectCommandBuffers.meshletCullingIndirectCommandBuffer.get();

					commandList.ExecuteIndirect(
						commandSignature.GetHandle(),
						meshletCullingBuffer->GetResource()->GetAPIResource().GetHandle(),
						0,
						meshletCullingBuffer->GetResource()->GetAPIResource().GetHandle(),
						meshletCullingBuffer->GetResource()->GetUAVCounterOffset(),
						numDraws
					);
				}
				});

			BindResourceDescriptorIndices(commandList, m_clearPSO.GetResourceDescriptorSlots());

			// Reset necessary meshlets
			if (m_doResets) {
				commandList.BindPipeline(m_clearPSO.GetAPIPipelineState().GetHandle());
				lightQuery.each([&](flecs::entity e, Components::Light light, Components::LightViewInfo& lightViewInfo, Components::DepthMap lightDepth) {

					for (auto& viewID : lightViewInfo.viewIDs) {

						auto view = context.viewManager->Get(viewID);

						cameraIndex = view->gpu.cameraBufferIndex;
						commandList.PushConstants(rhi::ShaderStage::Compute, 0, ViewRootSignatureIndex, LightViewIndex, 1, &cameraIndex);

						miscRootConstants[MESHLET_CULLING_BITFIELD_BUFFER_UAV_DESCRIPTOR_INDEX] = view->gpu.meshletBitfieldBuffer->GetResource()->GetUAVShaderVisibleInfo(0).slot.index;
						commandList.PushConstants(rhi::ShaderStage::Compute, 0, MiscUintRootSignatureIndex, 0, NumMiscUintRootConstants, &miscRootConstants);
						auto meshletCullingClearBuffer = view->gpu.indirectCommandBuffers.meshletCullingResetIndirectCommandBuffer;

						commandList.ExecuteIndirect(
							commandSignature.GetHandle(),
							meshletCullingClearBuffer->GetResource()->GetAPIResource().GetHandle(),
							0,
							meshletCullingClearBuffer->GetResource()->GetAPIResource().GetHandle(),
							meshletCullingClearBuffer->GetResource()->GetUAVCounterOffset(),
							numDraws
						);
					}
					});
			}
		}

		if (!getShadowsEnabled()) { // Otherwise, set in the shadow culling
			BindResourceDescriptorIndices(commandList, m_clearPSO.GetResourceDescriptorSlots());
		}

		// Reset necessary meshlets
		if (m_doResets) {
			auto& meshletCullingClearBuffer = m_primaryCameraMeshletCullingResetIndirectCommandBuffer;
			//commandList->SetPipelineState(m_clearPSO.Get());
			commandList.BindPipeline(m_clearPSO.GetAPIPipelineState().GetHandle());

			commandList.ExecuteIndirect(
				commandSignature.GetHandle(),
				meshletCullingClearBuffer->GetResource()->GetAPIResource().GetHandle(),
				0,
				meshletCullingClearBuffer->GetResource()->GetAPIResource().GetHandle(),
				meshletCullingClearBuffer->GetResource()->GetUAVCounterOffset(),
				numDraws
			);
		}

		return {};
	}

	void Cleanup() override {

	}

private:

	void CreatePSO() {

		std::vector<DxcDefine> defines;

		if (m_isRemaindersPass) {
			DxcDefine remainderDefine;
			remainderDefine.Name = L"REMAINDERS_PASS";
			remainderDefine.Value = L"1";
			defines.push_back(remainderDefine);
		}
		if (m_occlusionCullingEnabled) {
			DxcDefine occlusionDefine;
			occlusionDefine.Name = L"OCCLUSION_CULLING";
			occlusionDefine.Value = L"1";
			defines.push_back(occlusionDefine);
		}

		m_cullingPSO = PSOManager::GetInstance().MakeComputePipeline(
			PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
			L"shaders/meshletCulling.hlsl",
			L"MeshletCullingCSMain",
			defines,
			"Meshlet Culling Compute Pipeline");

		DxcDefine visibilityDefine;
		visibilityDefine.Name = L"WRITE_VISIBILITY_UNPACK_DATA";
		visibilityDefine.Value = L"1";
		defines.push_back(visibilityDefine);

		m_cullingWithVisibilityDataPSO = PSOManager::GetInstance().MakeComputePipeline(
			PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
			L"shaders/meshletCulling.hlsl",
			L"MeshletCullingCSMain",
			defines,
			"Meshlet Culling with Visibility Data Compute Pipeline");

		m_clearPSO = PSOManager::GetInstance().MakeComputePipeline(
			PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
			L"shaders/meshletCulling.hlsl",
			L"ClearMeshletCullingCSMain",
			{},
			"Clear Meshlet Culling Bitfields Compute Pipeline");
	}

	flecs::query<Components::Light, Components::LightViewInfo, Components::DepthMap> lightQuery;

	PipelineState m_cullingPSO;
	PipelineState m_cullingWithVisibilityDataPSO;
	PipelineState m_clearPSO;

	DynamicGloballyIndexedResource* m_primaryCameraMeshletCullingBitfieldBuffer = nullptr;
	DynamicGloballyIndexedResource* m_primaryCameraMeshletCullingIndirectCommandBuffer = nullptr;
	DynamicGloballyIndexedResource* m_primaryCameraMeshletCullingResetIndirectCommandBuffer = nullptr;
	PixelBuffer* m_primaryCameraLinearDepthMap = nullptr;
	Buffer* m_counter;

	bool m_isRemaindersPass = false;
	bool m_doResets = true;
	bool m_occlusionCullingEnabled = false;

	std::function<uint8_t()> getNumDirectionalLightCascades;
	std::function<bool()> getShadowsEnabled;
};

class RewriteOccluderMeshletVisibilityPass : public ComputePass {
public:
	RewriteOccluderMeshletVisibilityPass() {
		m_rewriteVisibilityPSO = PSOManager::GetInstance().MakeComputePipeline(
			PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
			L"shaders/meshletCulling.hlsl",
			L"RewriteOccluderMeshletVisibilityCS",
			{},
			"Rewrite Occluder Meshlet Visibility Compute Pipeline");
	};
	~RewriteOccluderMeshletVisibilityPass() override = default;

	void Setup() override {

		RegisterSRV(Builtin::PerObjectBuffer);
		RegisterSRV(Builtin::CameraBuffer);
		RegisterSRV(Builtin::PerMeshBuffer);
		RegisterSRV(Builtin::PerMeshInstanceBuffer);
		RegisterSRV(Builtin::MeshResources::MeshletBounds);

		RegisterUAV(Builtin::PrimaryCamera::VisibleClusterTable);
		RegisterUAV(Builtin::PrimaryCamera::VisibleClusterTableCounter);
		RegisterUAV(Builtin::MeshResources::ClusterToVisibleClusterTableIndexBuffer);

		m_primaryCameraMeshletBitfieldBuffer = m_resourceRegistryView->RequestPtr<DynamicGloballyIndexedResource>(Builtin::PrimaryCamera::MeshletBitfield);
		m_primaryCameraMeshletCullingIndirectCommandBuffer = m_resourceRegistryView->RequestPtr<DynamicGloballyIndexedResource>(Builtin::PrimaryCamera::IndirectCommandBuffers::MeshletCulling);
	}

	void DeclareResourceUsages(ComputePassBuilder* builder) override {
		builder->WithShaderResource(Builtin::PerObjectBuffer,
			Builtin::PerMeshBuffer,
			Builtin::PerMeshInstanceBuffer,
			Builtin::MeshResources::MeshletBounds,
			Builtin::CameraBuffer)
			.WithUnorderedAccess(Builtin::MeshletCullingBitfieldGroup,
				Builtin::PrimaryCamera::MeshletBitfield,
				Builtin::PrimaryCamera::VisibleClusterTable,
				Builtin::PrimaryCamera::VisibleClusterTableCounter,
				Builtin::MeshResources::ClusterToVisibleClusterTableIndexBuffer)
			.WithIndirectArguments(
				Builtin::IndirectCommandBuffers::MeshletCulling,
				Builtin::PrimaryCamera::IndirectCommandBuffers::MeshletCulling);
	}

	PassReturn Execute(RenderContext& context) override {
		unsigned int numDraws = context.drawStats.numDrawsInScene;
		if (numDraws == 0) {
			return {};
		}

		auto& commandList = context.commandList;
		commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());
		commandList.BindLayout(PSOManager::GetInstance().GetComputeRootSignature().GetHandle());
		commandList.BindPipeline(m_rewriteVisibilityPSO.GetAPIPipelineState().GetHandle());
		BindResourceDescriptorIndices(commandList, m_rewriteVisibilityPSO.GetResourceDescriptorSlots());

		unsigned int miscRootConstants[NumMiscUintRootConstants] = {};
		miscRootConstants[MESHLET_CULLING_BITFIELD_BUFFER_UAV_DESCRIPTOR_INDEX] = m_primaryCameraMeshletBitfieldBuffer->GetResource()->GetUAVShaderVisibleInfo(0).slot.index;
		commandList.PushConstants(rhi::ShaderStage::Compute, 0, MiscUintRootSignatureIndex, 0, NumMiscUintRootConstants, &miscRootConstants);

		auto primaryCameraView = context.viewManager->Get(context.currentScene->GetPrimaryCamera().get<Components::RenderViewRef>().viewID);
		unsigned int cameraIndex = primaryCameraView->gpu.cameraBufferIndex;
		commandList.PushConstants(rhi::ShaderStage::Compute, 0, ViewRootSignatureIndex, LightViewIndex, 1, &cameraIndex);

		auto& commandSignature = CommandSignatureManager::GetInstance().GetDispatchCommandSignature();
		auto indirectBuffer = m_primaryCameraMeshletCullingIndirectCommandBuffer;

		commandList.ExecuteIndirect(
			commandSignature.GetHandle(),
			indirectBuffer->GetResource()->GetAPIResource().GetHandle(),
			0,
			indirectBuffer->GetResource()->GetAPIResource().GetHandle(),
			indirectBuffer->GetResource()->GetUAVCounterOffset(),
			numDraws);

		return {};
	}

	void Cleanup() override {
	}

private:
	PipelineState m_rewriteVisibilityPSO;
	DynamicGloballyIndexedResource* m_primaryCameraMeshletBitfieldBuffer = nullptr;
	DynamicGloballyIndexedResource* m_primaryCameraMeshletCullingIndirectCommandBuffer = nullptr;
};