#pragma once

#include <DirectX/d3dx12.h>

#include "RenderPasses/Base/RenderPass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Utilities/Utilities.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Managers/MeshManager.h"
#include "Managers/Singletons/CommandSignatureManager.h"
#include "../../shaders/PerPassRootConstants/meshletCullingRootConstants.h"

class MeshletCullingPass : public ComputePass {
public:
	MeshletCullingPass(bool isOccludersPass, bool isRemaindersPass = false, bool doResets = true) :
		m_isOccludersPass(isOccludersPass), 
		m_isRemaindersPass(isRemaindersPass),
		m_doResets(doResets){
		getNumDirectionalLightCascades = SettingsManager::GetInstance().getSettingGetter<uint8_t>("numDirectionalLightCascades");
		getShadowsEnabled = SettingsManager::GetInstance().getSettingGetter<bool>("enableShadows");
		m_occlusionCullingEnabled = SettingsManager::GetInstance().getSettingGetter<bool>("enableOcclusionCulling")();
	}

	~MeshletCullingPass() {
	}

	void Setup() override {
		auto& ecsWorld = ECSManager::GetInstance().GetWorld();
		lightQuery = ecsWorld.query_builder<Components::Light, Components::LightViewInfo, Components::DepthMap>().cached().cache_kind(flecs::QueryCacheAll).build();

		CreatePSO();

		RegisterSRV(Builtin::PerObjectBuffer);
		RegisterSRV(Builtin::CameraBuffer);
		RegisterSRV(Builtin::PerMeshBuffer);
		RegisterSRV(Builtin::PerMeshInstanceBuffer);
		RegisterSRV(Builtin::MeshResources::MeshletBounds);

		m_primaryCameraMeshletCullingBitfieldBuffer = m_resourceRegistryView->Request<DynamicGloballyIndexedResource>(Builtin::PrimaryCamera::MeshletBitfield);
		m_primaryCameraMeshletCullingIndirectCommandBuffer = m_resourceRegistryView->Request<DynamicGloballyIndexedResource>(Builtin::PrimaryCamera::IndirectCommandBuffers::MeshletFrustrumCulling);
		m_primaryCameraMeshletCullingResetIndirectCommandBuffer = m_resourceRegistryView->Request<DynamicGloballyIndexedResource>(Builtin::PrimaryCamera::IndirectCommandBuffers::MeshletCullingReset);

		m_primaryCameraLinearDepthMap = m_resourceRegistryView->Request<PixelBuffer>(Builtin::PrimaryCamera::LinearDepthMap);
	}

	void DeclareResourceUsages(ComputePassBuilder* builder) override {
		builder->WithShaderResource(Builtin::PerObjectBuffer, 
			Builtin::PerMeshBuffer, 
			Builtin::PerMeshInstanceBuffer, 
			Builtin::MeshResources::MeshletBounds, 
			Builtin::CameraBuffer,
			Builtin::PrimaryCamera::LinearDepthMap,
			Builtin::Shadows::LinearShadowMaps)
			.WithUnorderedAccess(Builtin::MeshletCullingBitfieldGroup, Builtin::PrimaryCamera::MeshletBitfield)
			.WithIndirectArguments(
				Builtin::IndirectCommandBuffers::MeshletCulling, 
				Builtin::PrimaryCamera::IndirectCommandBuffers::MeshletFrustrumCulling, 
				Builtin::PrimaryCamera::IndirectCommandBuffers::MeshletCullingReset);
	}

	PassReturn Execute(RenderContext& context) override {

		unsigned int numDraws = context.drawStats.numOpaqueDraws + context.drawStats.numAlphaTestDraws + context.drawStats.numBlendDraws;

		if (numDraws == 0) {
			return {};
		}

		auto& commandList = context.commandList;

		// Set the descriptor heaps
		commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());

		commandList.BindLayout(PSOManager::GetInstance().GetComputeRootSignature().GetHandle());
		commandList.BindPipeline(m_frustrumCullingPSO.GetAPIPipelineState().GetHandle());


		BindResourceDescriptorIndices(commandList, m_resourceDescriptorBindings);

		unsigned int miscRootConstants[NumMiscUintRootConstants] = {};
		miscRootConstants[MESHLET_CULLING_BITFIELD_BUFFER_UAV_DESCRIPTOR_INDEX] = m_primaryCameraMeshletCullingBitfieldBuffer->GetResource()->GetUAVShaderVisibleInfo(0).slot.index;
		miscRootConstants[LINEAR_DEPTH_MAP_SRV_DESCRIPTOR_INDEX] = m_primaryCameraLinearDepthMap->GetSRVInfo(0).slot.index;

		commandList.PushConstants(rhi::ShaderStage::Compute, 0, MiscUintRootSignatureIndex, 0, NumMiscUintRootConstants, &miscRootConstants);

		unsigned int cameraIndex = context.currentScene->GetPrimaryCamera().get<Components::RenderView>().cameraBufferIndex;
		commandList.PushConstants(rhi::ShaderStage::Compute, 0, ViewRootSignatureIndex, LightViewIndex, 1, &cameraIndex);

		// Culling for main camera

		// Frustrum culling
		auto meshletCullingBuffer = m_primaryCameraMeshletCullingIndirectCommandBuffer;
		
		auto commandSignature = CommandSignatureManager::GetInstance().GetDispatchCommandSignature();
		commandList.ExecuteIndirect(
			commandSignature.GetHandle(),
			meshletCullingBuffer->GetResource()->GetAPIResource().GetHandle(),
			0,
			meshletCullingBuffer->GetResource()->GetAPIResource().GetHandle(),
			meshletCullingBuffer->GetResource()->GetUAVCounterOffset(),
			numDraws
		);

		// Reset necessary meshlets
		if (m_doResets) {
			auto meshletCullingClearBuffer = m_primaryCameraMeshletCullingResetIndirectCommandBuffer;
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

		if (getShadowsEnabled()) {

			// Frustrum culling
			commandList.BindPipeline(m_frustrumCullingPSO.GetAPIPipelineState().GetHandle());
			lightQuery.each([&](flecs::entity e, Components::Light light, Components::LightViewInfo& lightViewInfo, Components::DepthMap lightDepth) {

				for (auto& view : lightViewInfo.renderViews) {

					cameraIndex = view.cameraBufferIndex;
					commandList.PushConstants(rhi::ShaderStage::Compute, 0, ViewRootSignatureIndex, LightViewIndex, 1, &cameraIndex);

					miscRootConstants[MESHLET_CULLING_BITFIELD_BUFFER_UAV_DESCRIPTOR_INDEX] = view.meshletBitfieldBuffer->GetResource()->GetUAVShaderVisibleInfo(0).slot.index;
					miscRootConstants[LINEAR_DEPTH_MAP_SRV_DESCRIPTOR_INDEX] = light.type == Components::LightType::Point ? lightDepth.linearDepthMap->GetSRVInfo(SRVViewType::Texture2DArray, 0).slot.index : lightDepth.linearDepthMap->GetSRVInfo(0).slot.index;
					commandList.PushConstants(rhi::ShaderStage::Compute, 0, MiscUintRootSignatureIndex, 0, NumMiscUintRootConstants, &miscRootConstants);
					meshletCullingBuffer = view.indirectCommandBuffers.meshletCullingIndirectCommandBuffer;

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

			// Reset necessary meshlets
			if (m_doResets) {
				commandList.BindPipeline(m_clearPSO.GetAPIPipelineState().GetHandle());
				lightQuery.each([&](flecs::entity e, Components::Light light, Components::LightViewInfo& lightViewInfo, Components::DepthMap lightDepth) {

					for (auto& view : lightViewInfo.renderViews) {

						cameraIndex = view.cameraBufferIndex;
						commandList.PushConstants(rhi::ShaderStage::Compute, 0, ViewRootSignatureIndex, LightViewIndex, 1, &cameraIndex);

						miscRootConstants[MESHLET_CULLING_BITFIELD_BUFFER_UAV_DESCRIPTOR_INDEX] = view.meshletBitfieldBuffer->GetResource()->GetUAVShaderVisibleInfo(0).slot.index;
						commandList.PushConstants(rhi::ShaderStage::Compute, 0, MiscUintRootSignatureIndex, 0, NumMiscUintRootConstants, &miscRootConstants);
						auto meshletCullingClearBuffer = view.indirectCommandBuffers.meshletCullingResetIndirectCommandBuffer;

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

		return {};
	}

	void Cleanup(RenderContext& context) override {

	}

private:

	void CreatePSO() {
		DxcDefine occludersDefine;
		occludersDefine.Name = L"OCCLUDERS_PASS";
		occludersDefine.Value = L"1";
		std::vector<DxcDefine> defines;
		if (m_isOccludersPass) {
			defines.push_back(occludersDefine);
		}
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

		m_frustrumCullingPSO = PSOManager::GetInstance().MakeComputePipeline(
			PSOManager::GetInstance().GetComputeRootSignature(),
			L"shaders/meshletCulling.hlsl",
			L"MeshletFrustrumCullingCSMain",
			defines,
			"Meshlet Frustrum Culling Compute Pipeline");

		m_clearPSO = PSOManager::GetInstance().MakeComputePipeline(
			PSOManager::GetInstance().GetComputeRootSignature(),
			L"shaders/meshletCulling.hlsl",
			L"ClearMeshletFrustrumCullingCSMain",
			{},
			"Clear Meshlet Frustrum Culling Compute Pipeline");
	}

	flecs::query<Components::Light, Components::LightViewInfo, Components::DepthMap> lightQuery;

	PipelineState m_frustrumCullingPSO;
	PipelineState m_clearPSO;

	PipelineResources m_resourceDescriptorBindings;

	std::shared_ptr<DynamicGloballyIndexedResource> m_primaryCameraMeshletCullingBitfieldBuffer = nullptr;
	std::shared_ptr<DynamicGloballyIndexedResource> m_primaryCameraMeshletCullingIndirectCommandBuffer = nullptr;
	std::shared_ptr<DynamicGloballyIndexedResource> m_primaryCameraMeshletCullingResetIndirectCommandBuffer = nullptr;
	std::shared_ptr<PixelBuffer> m_primaryCameraLinearDepthMap = nullptr;

	bool m_isOccludersPass = false;
	bool m_isRemaindersPass = false;
	bool m_doResets = true;
	bool m_occlusionCullingEnabled = false;

	std::function<uint8_t()> getNumDirectionalLightCascades;
	std::function<bool()> getShadowsEnabled;
};