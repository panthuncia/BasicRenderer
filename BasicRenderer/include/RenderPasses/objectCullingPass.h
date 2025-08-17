#pragma once

#include <DirectX/d3dx12.h>

#include "RenderPasses/Base/RenderPass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Utilities/Utilities.h"
#include "Managers/Singletons/SettingsManager.h"
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
		builder->WithShaderResource(Builtin::PerObjectBuffer,
				Builtin::PerMeshBuffer,
				Builtin::CameraBuffer,
				Builtin::IndirectCommandBuffers::Master,
				Builtin::ActiveDrawSetIndices::Opaque,
				Builtin::ActiveDrawSetIndices::AlphaTest,
				Builtin::ActiveDrawSetIndices::Blend)
			.WithUnorderedAccess(Builtin::IndirectCommandBuffers::Opaque,
				Builtin::IndirectCommandBuffers::AlphaTest,
				Builtin::IndirectCommandBuffers::MeshletCulling,
				Builtin::MeshInstanceMeshletCullingBitfieldGroup,
				Builtin::MeshInstanceOcclusionCullingBitfieldGroup);
	}

	void Setup() override {
		auto& ecsWorld = ECSManager::GetInstance().GetWorld();
		lightQuery = ecsWorld.query_builder<Components::Light, Components::LightViewInfo, Components::DepthMap>().cached().cache_kind(flecs::QueryCacheAll).build();

		CreatePSO();
		
		RegisterSRV(Builtin::PerObjectBuffer);
		RegisterSRV(Builtin::CameraBuffer);
		RegisterSRV(Builtin::PerMeshBuffer);
		RegisterSRV(Builtin::IndirectCommandBuffers::Master);
		//RegisterSRV(Builtin::ActiveDrawSetIndices::Opaque);
		//RegisterSRV(Builtin::ActiveDrawSetIndices::AlphaTest);
		//RegisterSRV(Builtin::ActiveDrawSetIndices::Blend);
		m_activeOpaqueDrawSetIndicesBufferSRVIndex = m_resourceRegistryView->Request<GloballyIndexedResource>(Builtin::ActiveDrawSetIndices::Opaque)->GetSRVInfo(0).index;
		m_activeAlphaTestDrawSetIndicesBufferSRVIndex = m_resourceRegistryView->Request<GloballyIndexedResource>(Builtin::ActiveDrawSetIndices::AlphaTest)->GetSRVInfo(0).index;
		m_activeBlendDrawSetIndicesBufferSRVIndex = m_resourceRegistryView->Request<GloballyIndexedResource>(Builtin::ActiveDrawSetIndices::Blend)->GetSRVInfo(0).index;
	}

	PassReturn Execute(RenderContext& context) override {
		auto& commandList = context.commandList;

		// Set the descriptor heaps
		ID3D12DescriptorHeap* descriptorHeaps[] = {
			ResourceManager::GetInstance().GetSRVDescriptorHeap().Get(),
			ResourceManager::GetInstance().GetSamplerDescriptorHeap().Get()
		};

		commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

		auto rootSignature = PSOManager::GetInstance().GetRootSignature();
		commandList->SetComputeRootSignature(rootSignature.Get());

		// Set the compute pipeline state
		commandList->SetPipelineState(m_PSO.Get());

		BindResourceDescriptorIndices(commandList, m_resourceDescriptorBindings);

		unsigned int drawRootConstants[NumDrawInfoRootConstants] = {};

		unsigned int miscRootConstants[NumMiscUintRootConstants] = {};

		auto& primaryView = context.currentScene->GetPrimaryCamera().get<Components::RenderView>();
		auto& primaryDepth = context.currentScene->GetPrimaryCamera().get<Components::DepthMap>();
		unsigned int cameraIndex = primaryView.cameraBufferIndex;

		bool shadows = getShadowsEnabled();
		// opaque buffer
		auto numOpaqueDraws = context.drawStats.numOpaqueDraws;
		if (numOpaqueDraws > 0) {
			uint32_t numThreadGroups = static_cast<uint32_t>(std::ceil(numOpaqueDraws / 64.0));
			// First, process buffer for main camera
			commandList->SetComputeRoot32BitConstants(ViewRootSignatureIndex, 1, &cameraIndex, LightViewIndex);

			drawRootConstants[MaxDrawIndex] = numOpaqueDraws-1;
			commandList->SetComputeRoot32BitConstants(DrawInfoRootSignatureIndex, NumDrawInfoRootConstants, drawRootConstants, 0);

			miscRootConstants[MESH_INSTANCE_MESHLET_CULLING_BITFIELD_BUFFER_UAV_DESCRIPTOR_INDEX] = primaryView.meshInstanceMeshletCullingBitfieldBuffer->GetResource()->GetUAVShaderVisibleInfo(0).index;
			miscRootConstants[MESHLET_CULLING_RESET_BUFFER_UAV_DESCRIPTOR_INDEX] = primaryView.indirectCommandBuffers.meshletCullingResetIndirectCommandBuffer->GetResource()->GetUAVShaderVisibleInfo(0).index;
			miscRootConstants[LINEAR_DEPTH_MAP_SRV_DESCRIPTOR_INDEX] = primaryDepth.linearDepthMap->GetSRVInfo(0).index;
			miscRootConstants[MESH_INSTANCE_OCCLUSION_CULLING_BUFFER_UAV_DESCRIPTOR_INDEX] = primaryView.meshInstanceOcclusionCullingBitfieldBuffer->GetResource()->GetUAVShaderVisibleInfo(0).index;
			miscRootConstants[MESHLET_CULLING_INDIRECT_COMMAND_BUFFER_UAV_DESCRIPTOR_INDEX] = primaryView.indirectCommandBuffers.meshletCullingIndirectCommandBuffer->GetResource()->GetUAVShaderVisibleInfo(0).index;
			miscRootConstants[INDIRECT_COMMAND_BUFFER_UAV_DESCRIPTOR_INDEX] = primaryView.indirectCommandBuffers.opaqueIndirectCommandBuffer->GetResource()->GetUAVShaderVisibleInfo(0).index;
			miscRootConstants[ACTIVE_DRAW_SET_INDICES_BUFFER_SRV_DESCRIPTOR_INDEX] = m_activeOpaqueDrawSetIndicesBufferSRVIndex;
			commandList->SetComputeRoot32BitConstants(MiscUintRootSignatureIndex, NumMiscUintRootConstants, miscRootConstants, 0);

			commandList->Dispatch(numThreadGroups, 1, 1);

			if (shadows) {
				lightQuery.each([&](flecs::entity e, Components::Light light, Components::LightViewInfo& lightViewInfo, Components::DepthMap lightDepth) {
					int i = 0;
					for (auto& view : lightViewInfo.renderViews) {

						uint32_t lightCameraIndex = static_cast<uint32_t>(view.cameraBufferView->GetOffset() / sizeof(CameraInfo));
						commandList->SetComputeRoot32BitConstants(ViewRootSignatureIndex, 1, &lightCameraIndex, LightViewIndex);

						miscRootConstants[MESH_INSTANCE_MESHLET_CULLING_BITFIELD_BUFFER_UAV_DESCRIPTOR_INDEX] = view.meshInstanceMeshletCullingBitfieldBuffer->GetResource()->GetUAVShaderVisibleInfo(0).index;
						miscRootConstants[MESHLET_CULLING_RESET_BUFFER_UAV_DESCRIPTOR_INDEX] = view.indirectCommandBuffers.meshletCullingResetIndirectCommandBuffer->GetResource()->GetUAVShaderVisibleInfo(0).index;
						miscRootConstants[LINEAR_DEPTH_MAP_SRV_DESCRIPTOR_INDEX] = light.type == Components::LightType::Point ? lightDepth.linearDepthMap->GetSRVInfo(SRVViewType::Texture2DArray, 0).index : lightDepth.linearDepthMap->GetSRVInfo(0).index;
						miscRootConstants[MESH_INSTANCE_OCCLUSION_CULLING_BUFFER_UAV_DESCRIPTOR_INDEX] = view.meshInstanceOcclusionCullingBitfieldBuffer->GetResource()->GetUAVShaderVisibleInfo(0).index;
						miscRootConstants[MESHLET_CULLING_INDIRECT_COMMAND_BUFFER_UAV_DESCRIPTOR_INDEX] = view.indirectCommandBuffers.meshletCullingIndirectCommandBuffer->GetResource()->GetUAVShaderVisibleInfo(0).index;
						miscRootConstants[INDIRECT_COMMAND_BUFFER_UAV_DESCRIPTOR_INDEX] = view.indirectCommandBuffers.opaqueIndirectCommandBuffer->GetResource()->GetUAVShaderVisibleInfo(0).index;
						miscRootConstants[ACTIVE_DRAW_SET_INDICES_BUFFER_SRV_DESCRIPTOR_INDEX] = m_activeOpaqueDrawSetIndicesBufferSRVIndex;
						commandList->SetComputeRoot32BitConstants(MiscUintRootSignatureIndex, NumMiscUintRootConstants, miscRootConstants, 0);

						i++;
						commandList->Dispatch(numThreadGroups, 1, 1);
					}
					});
			}
		}
		// alpha test buffer
		auto numAlphaTestDraws = context.drawStats.numAlphaTestDraws;
		if (numAlphaTestDraws > 0) {
			uint32_t numThreadGroups = static_cast<uint32_t>(std::ceil(numAlphaTestDraws / 64.0));

			commandList->SetComputeRoot32BitConstants(ViewRootSignatureIndex, 1, &cameraIndex, LightViewIndex);

			drawRootConstants[MaxDrawIndex] = numAlphaTestDraws - 1;
			commandList->SetComputeRoot32BitConstants(DrawInfoRootSignatureIndex, NumDrawInfoRootConstants, drawRootConstants, 0);

			miscRootConstants[MESH_INSTANCE_MESHLET_CULLING_BITFIELD_BUFFER_UAV_DESCRIPTOR_INDEX] = primaryView.meshInstanceMeshletCullingBitfieldBuffer->GetResource()->GetUAVShaderVisibleInfo(0).index;
			miscRootConstants[MESHLET_CULLING_RESET_BUFFER_UAV_DESCRIPTOR_INDEX] = primaryView.indirectCommandBuffers.meshletCullingResetIndirectCommandBuffer->GetResource()->GetUAVShaderVisibleInfo(0).index;
			miscRootConstants[LINEAR_DEPTH_MAP_SRV_DESCRIPTOR_INDEX] = primaryDepth.linearDepthMap->GetSRVInfo(0).index;
			miscRootConstants[MESH_INSTANCE_OCCLUSION_CULLING_BUFFER_UAV_DESCRIPTOR_INDEX] = primaryView.meshInstanceOcclusionCullingBitfieldBuffer->GetResource()->GetUAVShaderVisibleInfo(0).index;
			miscRootConstants[MESHLET_CULLING_INDIRECT_COMMAND_BUFFER_UAV_DESCRIPTOR_INDEX] = primaryView.indirectCommandBuffers.meshletCullingIndirectCommandBuffer->GetResource()->GetUAVShaderVisibleInfo(0).index;
			miscRootConstants[INDIRECT_COMMAND_BUFFER_UAV_DESCRIPTOR_INDEX] = primaryView.indirectCommandBuffers.alphaTestIndirectCommandBuffer->GetResource()->GetUAVShaderVisibleInfo(0).index;
			miscRootConstants[ACTIVE_DRAW_SET_INDICES_BUFFER_SRV_DESCRIPTOR_INDEX] = m_activeAlphaTestDrawSetIndicesBufferSRVIndex;
			commandList->SetComputeRoot32BitConstants(MiscUintRootSignatureIndex, NumMiscUintRootConstants, miscRootConstants, 0);
		
			commandList->Dispatch(numThreadGroups, 1, 1);

			if (shadows) {
				lightQuery.each([&](flecs::entity e, Components::Light light, Components::LightViewInfo& lightViewInfo, Components::DepthMap lightDepth) {
					int i = 0;
					for (auto& view : lightViewInfo.renderViews) {

						uint32_t lightCameraIndex = static_cast<uint32_t>(view.cameraBufferView->GetOffset() / sizeof(CameraInfo));
						commandList->SetComputeRoot32BitConstants(ViewRootSignatureIndex, 1, &lightCameraIndex, LightViewIndex);

						miscRootConstants[MESH_INSTANCE_MESHLET_CULLING_BITFIELD_BUFFER_UAV_DESCRIPTOR_INDEX] = view.meshInstanceMeshletCullingBitfieldBuffer->GetResource()->GetUAVShaderVisibleInfo(0).index;
						miscRootConstants[MESHLET_CULLING_RESET_BUFFER_UAV_DESCRIPTOR_INDEX] = view.indirectCommandBuffers.meshletCullingResetIndirectCommandBuffer->GetResource()->GetUAVShaderVisibleInfo(0).index;
						miscRootConstants[LINEAR_DEPTH_MAP_SRV_DESCRIPTOR_INDEX] = light.type == Components::LightType::Point ? lightDepth.linearDepthMap->GetSRVInfo(SRVViewType::Texture2DArray, 0).index : lightDepth.linearDepthMap->GetSRVInfo(0).index;
						miscRootConstants[MESH_INSTANCE_OCCLUSION_CULLING_BUFFER_UAV_DESCRIPTOR_INDEX] = view.meshInstanceOcclusionCullingBitfieldBuffer->GetResource()->GetUAVShaderVisibleInfo(0).index;
						miscRootConstants[MESHLET_CULLING_INDIRECT_COMMAND_BUFFER_UAV_DESCRIPTOR_INDEX] = view.indirectCommandBuffers.meshletCullingIndirectCommandBuffer->GetResource()->GetUAVShaderVisibleInfo(0).index;
						miscRootConstants[INDIRECT_COMMAND_BUFFER_UAV_DESCRIPTOR_INDEX] = view.indirectCommandBuffers.alphaTestIndirectCommandBuffer->GetResource()->GetUAVShaderVisibleInfo(0).index;
						miscRootConstants[ACTIVE_DRAW_SET_INDICES_BUFFER_SRV_DESCRIPTOR_INDEX] = m_activeAlphaTestDrawSetIndicesBufferSRVIndex;
						commandList->SetComputeRoot32BitConstants(MiscUintRootSignatureIndex, NumMiscUintRootConstants, miscRootConstants, 0);

						i++;
						commandList->Dispatch(numThreadGroups, 1, 1);
					}
					});
			}
		}

		// blend buffer
		if (!m_isOccludersPass) {
			commandList->SetPipelineState(m_blendPSO.Get());
			auto numBlendDraws = context.drawStats.numBlendDraws;
			if (numBlendDraws > 0) {
				uint32_t numThreadGroups = static_cast<uint32_t>(std::ceil(numBlendDraws / 64.0));

				commandList->SetComputeRoot32BitConstants(ViewRootSignatureIndex, 1, &cameraIndex, LightViewIndex);

				drawRootConstants[MaxDrawIndex] = numBlendDraws - 1;
				commandList->SetComputeRoot32BitConstants(DrawInfoRootSignatureIndex, NumDrawInfoRootConstants, drawRootConstants, 0);

				miscRootConstants[MESH_INSTANCE_MESHLET_CULLING_BITFIELD_BUFFER_UAV_DESCRIPTOR_INDEX] = primaryView.meshInstanceMeshletCullingBitfieldBuffer->GetResource()->GetUAVShaderVisibleInfo(0).index;
				miscRootConstants[MESHLET_CULLING_RESET_BUFFER_UAV_DESCRIPTOR_INDEX] = primaryView.indirectCommandBuffers.meshletCullingResetIndirectCommandBuffer->GetResource()->GetUAVShaderVisibleInfo(0).index;
				miscRootConstants[LINEAR_DEPTH_MAP_SRV_DESCRIPTOR_INDEX] = primaryDepth.linearDepthMap->GetSRVInfo(0).index;
				miscRootConstants[MESH_INSTANCE_OCCLUSION_CULLING_BUFFER_UAV_DESCRIPTOR_INDEX] = primaryView.meshInstanceOcclusionCullingBitfieldBuffer->GetResource()->GetUAVShaderVisibleInfo(0).index;
				miscRootConstants[MESHLET_CULLING_INDIRECT_COMMAND_BUFFER_UAV_DESCRIPTOR_INDEX] = primaryView.indirectCommandBuffers.meshletCullingIndirectCommandBuffer->GetResource()->GetUAVShaderVisibleInfo(0).index;
				miscRootConstants[INDIRECT_COMMAND_BUFFER_UAV_DESCRIPTOR_INDEX] = primaryView.indirectCommandBuffers.blendIndirectCommandBuffer->GetResource()->GetUAVShaderVisibleInfo(0).index;
				miscRootConstants[ACTIVE_DRAW_SET_INDICES_BUFFER_SRV_DESCRIPTOR_INDEX] = m_activeBlendDrawSetIndicesBufferSRVIndex;
				commandList->SetComputeRoot32BitConstants(MiscUintRootSignatureIndex, NumMiscUintRootConstants, miscRootConstants, 0);

				commandList->Dispatch(numThreadGroups, 1, 1);

				if (shadows) {
					lightQuery.each([&](flecs::entity e, Components::Light light, Components::LightViewInfo& lightViewInfo, Components::DepthMap lightDepth) {
						int i = 0;
						for (auto& view : lightViewInfo.renderViews) {

							unsigned int lightCameraIndex = static_cast<uint32_t>(view.cameraBufferView->GetOffset() / sizeof(CameraInfo));
							commandList->SetComputeRoot32BitConstants(ViewRootSignatureIndex, 1, &lightCameraIndex, LightViewIndex);

							miscRootConstants[MESH_INSTANCE_MESHLET_CULLING_BITFIELD_BUFFER_UAV_DESCRIPTOR_INDEX] = view.meshInstanceMeshletCullingBitfieldBuffer->GetResource()->GetUAVShaderVisibleInfo(0).index;
							miscRootConstants[MESHLET_CULLING_RESET_BUFFER_UAV_DESCRIPTOR_INDEX] = view.indirectCommandBuffers.meshletCullingResetIndirectCommandBuffer->GetResource()->GetUAVShaderVisibleInfo(0).index;
							miscRootConstants[LINEAR_DEPTH_MAP_SRV_DESCRIPTOR_INDEX] = light.type == Components::LightType::Point ? lightDepth.linearDepthMap->GetSRVInfo(SRVViewType::Texture2DArray, 0).index : lightDepth.linearDepthMap->GetSRVInfo(0).index;
							miscRootConstants[MESH_INSTANCE_OCCLUSION_CULLING_BUFFER_UAV_DESCRIPTOR_INDEX] = view.meshInstanceOcclusionCullingBitfieldBuffer->GetResource()->GetUAVShaderVisibleInfo(0).index;
							miscRootConstants[MESHLET_CULLING_INDIRECT_COMMAND_BUFFER_UAV_DESCRIPTOR_INDEX] = view.indirectCommandBuffers.meshletCullingIndirectCommandBuffer->GetResource()->GetUAVShaderVisibleInfo(0).index;
							miscRootConstants[INDIRECT_COMMAND_BUFFER_UAV_DESCRIPTOR_INDEX] = view.indirectCommandBuffers.blendIndirectCommandBuffer->GetResource()->GetUAVShaderVisibleInfo(0).index;
							miscRootConstants[ACTIVE_DRAW_SET_INDICES_BUFFER_SRV_DESCRIPTOR_INDEX] = m_activeBlendDrawSetIndicesBufferSRVIndex;
							commandList->SetComputeRoot32BitConstants(MiscUintRootSignatureIndex, NumMiscUintRootConstants, miscRootConstants, 0);

							i++;
							commandList->Dispatch(numThreadGroups, 1, 1);
						}
						});
				}
			}
		}
		return {};
	}

	void Cleanup(RenderContext& context) override {

	}

private:

	PipelineResources m_resourceDescriptorBindings;
	int m_activeOpaqueDrawSetIndicesBufferSRVIndex = -1;
	int m_activeAlphaTestDrawSetIndicesBufferSRVIndex = -1;
	int m_activeBlendDrawSetIndicesBufferSRVIndex = -1;

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

		ShaderInfoBundle shaderInfoBundle;
		shaderInfoBundle.computeShader = { L"shaders/objectCulling.hlsl", L"ObjectCullingCSMain", L"cs_6_6" };
		shaderInfoBundle.defines = defines;
		auto compiledBundle = PSOManager::GetInstance().CompileShaders(shaderInfoBundle);
		computeShader = compiledBundle.computeShader;
		m_resourceDescriptorBindings = compiledBundle.resourceDescriptorSlots;

		struct PipelineStateStream {
			CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE RootSignature;
			CD3DX12_PIPELINE_STATE_STREAM_CS CS;
		};

		PipelineStateStream pipelineStateStream = {};
		pipelineStateStream.RootSignature = PSOManager::GetInstance().GetRootSignature().Get();
		pipelineStateStream.CS = CD3DX12_SHADER_BYTECODE(computeShader.Get());

		D3D12_PIPELINE_STATE_STREAM_DESC streamDesc = {};
		streamDesc.SizeInBytes = sizeof(PipelineStateStream);
		streamDesc.pPipelineStateSubobjectStream = &pipelineStateStream;

		auto& device = DeviceManager::GetInstance().GetDevice();
		ID3D12Device2* device2 = nullptr;
		ThrowIfFailed(device->QueryInterface(IID_PPV_ARGS(&device2)));
		ThrowIfFailed(device2->CreatePipelineState(&streamDesc, IID_PPV_ARGS(&m_PSO)));

		defines.push_back({ L"BLEND_OBJECTS", L"1" });

		shaderInfoBundle.computeShader = { L"shaders/objectCulling.hlsl", L"ObjectCullingCSMain", L"cs_6_6" };
		shaderInfoBundle.defines = defines;
		compiledBundle = PSOManager::GetInstance().CompileShaders(shaderInfoBundle);
		computeShader = compiledBundle.computeShader;

		pipelineStateStream.CS = CD3DX12_SHADER_BYTECODE(computeShader.Get());
		ThrowIfFailed(device2->CreatePipelineState(&streamDesc, IID_PPV_ARGS(&m_blendPSO)));
	}
	
	flecs::query<Components::Light, Components::LightViewInfo, Components::DepthMap> lightQuery;

	ComPtr<ID3D12PipelineState> m_PSO;
	ComPtr<ID3D12PipelineState> m_blendPSO;

	std::function<uint8_t()> getNumDirectionalLightCascades;
	std::function<bool()> getShadowsEnabled;

	bool m_isOccludersPass = false;
	bool m_enableOcclusion = false;
};