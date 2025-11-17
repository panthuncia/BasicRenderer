#pragma once

#include <rhi_debug.h>

#include "RenderPasses/Base/RenderPass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Utilities/Utilities.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Managers/ViewManager.h"
#include "../../shaders/PerPassRootConstants/objectCullingRootConstants.h"

class ObjectCullingPass : public ComputePass {
public:
	ObjectCullingPass(bool isOccludersPass, bool enableOcclusion) : m_isOccludersPass(isOccludersPass), m_enableOcclusion(enableOcclusion) {
		getNumDirectionalLightCascades = SettingsManager::GetInstance().getSettingGetter<uint8_t>("numDirectionalLightCascades");
		getShadowsEnabled = SettingsManager::GetInstance().getSettingGetter<bool>("enableShadows");
	}

	~ObjectCullingPass() {
	}

	void DeclareResourceUsages(ComputePassBuilder* builder) {
		auto ecsWorld = ECSManager::GetInstance().GetWorld();
		flecs::query drawSetIndicesQuery = ecsWorld.query_builder<flecs::entity>()
			.with<Components::IsActiveDrawSetIndices>()
			.with<Components::ParticipatesInPass>(flecs::Wildcard)
			.build();
		flecs::query indirectCommandBuffersQuery = ecsWorld.query_builder<Components::IndirectCommandBuffers>()
			.with<Components::IsIndirectArguments>()
			.build();
		builder->WithShaderResource(Builtin::PerObjectBuffer,
				Builtin::PerMeshBuffer,
				Builtin::CameraBuffer,
				Builtin::IndirectCommandBuffers::Master)
			.WithShaderResource(ECSResourceResolver(drawSetIndicesQuery))
			.WithUnorderedAccess(Builtin::IndirectCommandBuffers::MeshletCulling,
				Builtin::MeshInstanceMeshletCullingBitfieldGroup,
				Builtin::MeshInstanceOcclusionCullingBitfieldGroup)
			.WithUnorderedAccess(ECSResourceResolver(indirectCommandBuffersQuery));
	}

	void Setup() override {
		auto& ecsWorld = ECSManager::GetInstance().GetWorld();
		lightQuery = ecsWorld.query_builder<Components::Light, Components::LightViewInfo, Components::DepthMap>().cached().cache_kind(flecs::QueryCacheAll).build();

		CreatePSO();
		
		RegisterSRV(Builtin::PerObjectBuffer);
		RegisterSRV(Builtin::CameraBuffer);
		RegisterSRV(Builtin::PerMeshBuffer);
		RegisterSRV(Builtin::IndirectCommandBuffers::Master);
	}

	PassReturn Execute(RenderContext& context) override {
		auto& commandList = context.commandList;

		// Set the descriptor heaps
		commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());

		commandList.BindLayout(PSOManager::GetInstance().GetComputeRootSignature().GetHandle());

		// Set the compute pipeline state
		commandList.BindPipeline(m_PSO.GetAPIPipelineState().GetHandle());

		BindResourceDescriptorIndices(commandList, m_PSO.GetResourceDescriptorSlots());

		unsigned int drawRootConstants[NumDrawInfoRootConstants] = {};

		unsigned int miscRootConstants[NumMiscUintRootConstants] = {};

		auto& primaryDepth = context.currentScene->GetPrimaryCamera().get<Components::DepthMap>();

		bool shadows = getShadowsEnabled();
		
		context.indirectCommandBufferManager->ForEachIndirectBuffer([&](uint64_t view,
			MaterialCompileFlags flags,
			const IndirectWorkload& wl)
			{
				if (wl.count == 0) {
					return;
				}
				uint32_t numThreadGroups = static_cast<uint32_t>(std::ceil(wl.count / 64.0));
				auto viewInfo = context.viewManager->Get(view);

				// Which camera are we processing for?
				commandList.PushConstants(rhi::ShaderStage::Compute, 0, ViewRootSignatureIndex, LightViewIndex, 1, &viewInfo->gpu.cameraBufferIndex);

				// How many draws are we processing?
				drawRootConstants[MaxDrawIndex] = wl.count - 1;
				commandList.PushConstants(rhi::ShaderStage::Compute, 0, DrawInfoRootSignatureIndex, 0, NumDrawInfoRootConstants, drawRootConstants);

				miscRootConstants[MESH_INSTANCE_MESHLET_CULLING_BITFIELD_BUFFER_UAV_DESCRIPTOR_INDEX] = viewInfo->gpu.meshInstanceMeshletCullingBitfieldBuffer->GetResource()->GetUAVShaderVisibleInfo(0).slot.index;
				miscRootConstants[MESHLET_CULLING_RESET_BUFFER_UAV_DESCRIPTOR_INDEX] = viewInfo->gpu.indirectCommandBuffers.meshletCullingResetIndirectCommandBuffer->GetResource()->GetUAVShaderVisibleInfo(0).slot.index;
				miscRootConstants[LINEAR_DEPTH_MAP_SRV_DESCRIPTOR_INDEX] = primaryDepth.linearDepthMap->GetSRVInfo(0).slot.index;
				miscRootConstants[MESH_INSTANCE_OCCLUSION_CULLING_BUFFER_UAV_DESCRIPTOR_INDEX] = viewInfo->gpu.meshInstanceOcclusionCullingBitfieldBuffer->GetResource()->GetUAVShaderVisibleInfo(0).slot.index;
				miscRootConstants[MESHLET_CULLING_INDIRECT_COMMAND_BUFFER_UAV_DESCRIPTOR_INDEX] = viewInfo->gpu.indirectCommandBuffers.meshletCullingIndirectCommandBuffer->GetResource()->GetUAVShaderVisibleInfo(0).slot.index;
				miscRootConstants[INDIRECT_COMMAND_BUFFER_UAV_DESCRIPTOR_INDEX] = wl.buffer->GetResource()->GetUAVShaderVisibleInfo(0).slot.index;
				miscRootConstants[ACTIVE_DRAW_SET_INDICES_BUFFER_SRV_DESCRIPTOR_INDEX] = context.objectManager->GetActiveDrawSetIndices(flags)->GetSRVInfo(0).slot.index;
				commandList.PushConstants(rhi::ShaderStage::Compute, 0, MiscUintRootSignatureIndex, 0, NumMiscUintRootConstants, miscRootConstants);

				commandList.Dispatch(numThreadGroups, 1, 1);
			});
		return {};
	}

	void Cleanup(RenderContext& context) override {

	}

private:

	void CreatePSO() {
		// Compile the compute shader
		Microsoft::WRL::ComPtr<ID3DBlob> computeShader;

		std::vector<DxcDefine> defines;
		if (m_isOccludersPass) {
			defines.push_back({ L"OCCLUDERS_PASS", L"1" });
		}
		if (m_enableOcclusion) {
			defines.push_back({ L"OCCLUSION_CULLING", L"1" });
		}

		m_PSO = PSOManager::GetInstance().MakeComputePipeline(
			PSOManager::GetInstance().GetComputeRootSignature(),
			L"shaders/objectCulling.hlsl",
			L"ObjectCullingCSMain",
			defines,
			m_isOccludersPass ? "ObjectCullingPass_Occluders" : "ObjectCullingPass_NonOccluders");

		defines.push_back({ L"BLEND_OBJECTS", L"1" });

		m_blendPSO = PSOManager::GetInstance().MakeComputePipeline(
			PSOManager::GetInstance().GetComputeRootSignature(),
			L"shaders/objectCulling.hlsl",
			L"ObjectCullingCSMain",
			defines,
			m_isOccludersPass ? "ObjectCullingPass_Blend_Occluders" : "ObjectCullingPass_Blend_NonOccluders");
	}
	
	flecs::query<Components::Light, Components::LightViewInfo, Components::DepthMap> lightQuery;

	PipelineState m_PSO;
	PipelineState m_blendPSO;

	std::function<uint8_t()> getNumDirectionalLightCascades;
	std::function<bool()> getShadowsEnabled;

	bool m_isOccludersPass = false;
	bool m_enableOcclusion = false;
};