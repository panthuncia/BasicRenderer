#pragma once

#include <DirectX/d3dx12.h>

#include "RenderPasses/Base/RenderPass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Utilities/Utilities.h"
#include "Managers/Singletons/SettingsManager.h"

class ObjectCullingPass : public ComputePass {
public:
	ObjectCullingPass(bool isOccludersPass, bool enableOcclusion) : m_isOccludersPass(isOccludersPass), m_enableOcclusion(enableOcclusion) {
		getNumDirectionalLightCascades = SettingsManager::GetInstance().getSettingGetter<uint8_t>("numDirectionalLightCascades");
		getShadowsEnabled = SettingsManager::GetInstance().getSettingGetter<bool>("enableShadows");
	}

	~ObjectCullingPass() {
	}

	void Setup() override {
		auto& ecsWorld = ECSManager::GetInstance().GetWorld();
		lightQuery = ecsWorld.query_builder<Components::Light, Components::LightViewInfo, Components::DepthMap>().cached().cache_kind(flecs::QueryCacheAll).build();

		CreatePSO();
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

		auto& meshManager = context.meshManager;
		auto& objectManager = context.objectManager;
		auto& cameraManager = context.cameraManager;

		unsigned int staticBufferIndices[NumStaticBufferRootConstants] = {};
		staticBufferIndices[PerObjectBufferDescriptorIndex] = objectManager->GetPerObjectBufferSRVIndex();
		staticBufferIndices[CameraBufferDescriptorIndex] = cameraManager->GetCameraBufferSRVIndex();
		staticBufferIndices[PerMeshBufferDescriptorIndex] = meshManager->GetPerMeshBufferSRVIndex();
		staticBufferIndices[DrawSetCommandBufferDescriptorIndex] = objectManager->GetMasterIndirectCommandsBufferSRVIndex();

		commandList->SetComputeRoot32BitConstants(StaticBufferRootSignatureIndex, NumStaticBufferRootConstants, &staticBufferIndices, 0);

		unsigned int miscRootConstants[NumMiscUintRootConstants] = {};

		unsigned int numCascades = getNumDirectionalLightCascades();
		auto primaryView = context.currentScene->GetPrimaryCamera().get<Components::RenderView>();
		auto primaryDepth = context.currentScene->GetPrimaryCamera().get<Components::DepthMap>();
		unsigned int cameraIndex = primaryView->cameraBufferIndex;

		bool shadows = getShadowsEnabled();
		// opaque buffer
		auto numOpaqueDraws = context.drawStats.numOpaqueDraws;
		if (numOpaqueDraws > 0) {
			unsigned int numThreadGroups = std::ceil(numOpaqueDraws / 64.0);
			// First, process buffer for main camera
			auto resource = context.currentScene->GetPrimaryCameraOpaqueIndirectCommandBuffer()->GetResource();
			auto apiResource = resource->GetAPIResource();
			//auto uavShaderVisibleInfo = resource->GetUAVShaderVisibleInfo();
			//auto uavNonShaderVisibleInfo = resource->GetUAVNonShaderVisibleInfo();
			unsigned int bufferIndices[NumVariableBufferRootConstants] = {};
			bufferIndices[ActiveDrawSetIndicesBufferDescriptorIndex] = objectManager->GetActiveOpaqueDrawSetIndicesBufferSRVIndex();
			bufferIndices[IndirectCommandBufferDescriptorIndex] = context.currentScene->GetPrimaryCameraOpaqueIndirectCommandBuffer()->GetResource()->GetUAVShaderVisibleInfo(0).index;
			bufferIndices[MaxDrawIndex] = numOpaqueDraws-1;
			bufferIndices[MeshletCullingIndirectCommandBufferDescriptorIndex] = context.currentScene->GetPrimaryCameraMeshletFrustrumCullingIndirectCommandBuffer()->GetResource()->GetUAVShaderVisibleInfo(0).index;

			commandList->SetComputeRoot32BitConstants(VariableBufferRootSignatureIndex, NumVariableBufferRootConstants, bufferIndices, 0);
			//unsigned int cameraIndex = context.currentScene->GetCamera()->GetCameraBufferView()->GetOffset() / sizeof(CameraInfo);
			commandList->SetComputeRoot32BitConstants(ViewRootSignatureIndex, 1, &cameraIndex, LightViewIndex);

			miscRootConstants[UintRootConstant0] = primaryView->meshInstanceMeshletCullingBitfieldBuffer->GetResource()->GetUAVShaderVisibleInfo(0).index;
			miscRootConstants[UintRootConstant1] = primaryView->indirectCommandBuffers.meshletCullingResetIndirectCommandBuffer->GetResource()->GetUAVShaderVisibleInfo(0).index;
			miscRootConstants[UintRootConstant2] = primaryDepth->linearDepthMap->GetSRVInfo(0).index;
			miscRootConstants[UintRootConstant3] = primaryView->meshInstanceOcclusionCullingBitfieldBuffer->GetResource()->GetUAVShaderVisibleInfo(0).index;
			miscRootConstants[UintRootConstant5] = primaryView->indirectCommandBuffers.meshletOcclusionCullingIndirectCommandBuffer->GetResource()->GetUAVShaderVisibleInfo(0).index;
			commandList->SetComputeRoot32BitConstants(MiscUintRootSignatureIndex, NumMiscUintRootConstants, miscRootConstants, 0);

			commandList->Dispatch(numThreadGroups, 1, 1);

			if (shadows) {
				lightQuery.each([&](flecs::entity e, Components::Light light, Components::LightViewInfo& lightViewInfo, Components::DepthMap lightDepth) {
					int i = 0;
					for (auto& view : lightViewInfo.renderViews) {
						auto& buffer = view.indirectCommandBuffers.opaqueIndirectCommandBuffer;
						bufferIndices[IndirectCommandBufferDescriptorIndex] = buffer->GetResource()->GetUAVShaderVisibleInfo(0).index;
						bufferIndices[MeshletCullingIndirectCommandBufferDescriptorIndex] = view.indirectCommandBuffers.meshletFrustrumCullingIndirectCommandBuffer->GetResource()->GetUAVShaderVisibleInfo(0).index;
						commandList->SetComputeRoot32BitConstants(VariableBufferRootSignatureIndex, NumVariableBufferRootConstants, bufferIndices, 0);

						unsigned int lightCameraIndex = view.cameraBufferView->GetOffset() / sizeof(CameraInfo);
						commandList->SetComputeRoot32BitConstants(ViewRootSignatureIndex, 1, &lightCameraIndex, LightViewIndex);

						miscRootConstants[UintRootConstant0] = view.meshInstanceMeshletCullingBitfieldBuffer->GetResource()->GetUAVShaderVisibleInfo(0).index;
						miscRootConstants[UintRootConstant1] = view.indirectCommandBuffers.meshletCullingResetIndirectCommandBuffer->GetResource()->GetUAVShaderVisibleInfo(0).index;
						miscRootConstants[UintRootConstant2] = light.type == Components::LightType::Point ? lightDepth.linearDepthMap->GetSRVInfo(SRVViewType::Texture2DArray, 0).index : lightDepth.linearDepthMap->GetSRVInfo(0).index;
						miscRootConstants[UintRootConstant3] = view.meshInstanceOcclusionCullingBitfieldBuffer->GetResource()->GetUAVShaderVisibleInfo(0).index;
						miscRootConstants[UintRootConstant5] = view.indirectCommandBuffers.meshletOcclusionCullingIndirectCommandBuffer->GetResource()->GetUAVShaderVisibleInfo(0).index;
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
			unsigned int numThreadGroups = std::ceil(numAlphaTestDraws / 64.0);
			auto resource = context.currentScene->GetPrimaryCameraAlphaTestIndirectCommandBuffer()->GetResource();
			auto apiResource = resource->GetAPIResource();
			//auto uavShaderVisibleInfo = resource->GetUAVShaderVisibleInfo();
			//auto uavNonShaderVisibleInfo = resource->GetUAVNonShaderVisibleInfo();
			unsigned int bufferIndices[NumVariableBufferRootConstants] = {};
			bufferIndices[ActiveDrawSetIndicesBufferDescriptorIndex] = objectManager->GetActiveAlphaTestDrawSetIndicesBufferSRVIndex();
			bufferIndices[IndirectCommandBufferDescriptorIndex] = context.currentScene->GetPrimaryCameraAlphaTestIndirectCommandBuffer()->GetResource()->GetUAVShaderVisibleInfo(0).index;
			bufferIndices[MaxDrawIndex] = numAlphaTestDraws-1;
			bufferIndices[MeshletCullingIndirectCommandBufferDescriptorIndex] = context.currentScene->GetPrimaryCameraMeshletFrustrumCullingIndirectCommandBuffer()->GetResource()->GetUAVShaderVisibleInfo(0).index;

			commandList->SetComputeRoot32BitConstants(VariableBufferRootSignatureIndex, NumVariableBufferRootConstants, bufferIndices, 0);
			//unsigned int cameraIndex = context.currentScene->GetCamera()->GetCameraBufferView()->GetOffset() / sizeof(CameraInfo);
			commandList->SetComputeRoot32BitConstants(ViewRootSignatureIndex, 1, &cameraIndex, LightViewIndex);

			miscRootConstants[UintRootConstant0] = primaryView->meshInstanceMeshletCullingBitfieldBuffer->GetResource()->GetUAVShaderVisibleInfo(0).index;
			miscRootConstants[UintRootConstant1] = primaryView->indirectCommandBuffers.meshletCullingResetIndirectCommandBuffer->GetResource()->GetUAVShaderVisibleInfo(0).index;
			miscRootConstants[UintRootConstant2] = primaryDepth->linearDepthMap->GetSRVInfo(0).index;
			miscRootConstants[UintRootConstant3] = primaryView->meshInstanceOcclusionCullingBitfieldBuffer->GetResource()->GetUAVShaderVisibleInfo(0).index;
			miscRootConstants[UintRootConstant5] = primaryView->indirectCommandBuffers.meshletOcclusionCullingIndirectCommandBuffer->GetResource()->GetUAVShaderVisibleInfo(0).index;
			commandList->SetComputeRoot32BitConstants(MiscUintRootSignatureIndex, NumMiscUintRootConstants, miscRootConstants, 0);
		
			commandList->Dispatch(numThreadGroups, 1, 1);

			if (shadows) {
				lightQuery.each([&](flecs::entity e, Components::Light light, Components::LightViewInfo& lightViewInfo, Components::DepthMap lightDepth) {
					int i = 0;
					for (auto& view : lightViewInfo.renderViews) {
						auto& buffer = view.indirectCommandBuffers.alphaTestIndirectCommandBuffer;
						bufferIndices[IndirectCommandBufferDescriptorIndex] = buffer->GetResource()->GetUAVShaderVisibleInfo(0).index;
						bufferIndices[MeshletCullingIndirectCommandBufferDescriptorIndex] = view.indirectCommandBuffers.meshletFrustrumCullingIndirectCommandBuffer->GetResource()->GetUAVShaderVisibleInfo(0).index;
						commandList->SetComputeRoot32BitConstants(VariableBufferRootSignatureIndex, NumVariableBufferRootConstants, bufferIndices, 0);
						unsigned int lightCameraIndex = view.cameraBufferView->GetOffset() / sizeof(CameraInfo);
						commandList->SetComputeRoot32BitConstants(ViewRootSignatureIndex, 1, &lightCameraIndex, LightViewIndex);

						miscRootConstants[UintRootConstant0] = view.meshInstanceMeshletCullingBitfieldBuffer->GetResource()->GetUAVShaderVisibleInfo(0).index;
						miscRootConstants[UintRootConstant1] = view.indirectCommandBuffers.meshletCullingResetIndirectCommandBuffer->GetResource()->GetUAVShaderVisibleInfo(0).index;
						miscRootConstants[UintRootConstant2] = light.type == Components::LightType::Point ? lightDepth.linearDepthMap->GetSRVInfo(SRVViewType::Texture2DArray, 0).index : lightDepth.linearDepthMap->GetSRVInfo(0).index;
						miscRootConstants[UintRootConstant3] = view.meshInstanceOcclusionCullingBitfieldBuffer->GetResource()->GetUAVShaderVisibleInfo(0).index;
						miscRootConstants[UintRootConstant5] = view.indirectCommandBuffers.meshletOcclusionCullingIndirectCommandBuffer->GetResource()->GetUAVShaderVisibleInfo(0).index;
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
				unsigned int numThreadGroups = std::ceil(numBlendDraws / 64.0);
				auto resource = context.currentScene->GetPrimaryCameraBlendIndirectCommandBuffer()->GetResource();
				auto apiResource = resource->GetAPIResource();
				//auto uavShaderVisibleInfo = resource->GetUAVShaderVisibleInfo();
				//auto uavNonShaderVisibleInfo = resource->GetUAVNonShaderVisibleInfo();
				unsigned int bufferIndices[NumVariableBufferRootConstants] = {};
				bufferIndices[ActiveDrawSetIndicesBufferDescriptorIndex] = objectManager->GetActiveBlendDrawSetIndicesBufferSRVIndex();
				bufferIndices[IndirectCommandBufferDescriptorIndex] = context.currentScene->GetPrimaryCameraBlendIndirectCommandBuffer()->GetResource()->GetUAVShaderVisibleInfo(0).index;
				bufferIndices[MaxDrawIndex] = numBlendDraws - 1;
				bufferIndices[MeshletCullingIndirectCommandBufferDescriptorIndex] = context.currentScene->GetPrimaryCameraMeshletFrustrumCullingIndirectCommandBuffer()->GetResource()->GetUAVShaderVisibleInfo(0).index;

				commandList->SetComputeRoot32BitConstants(VariableBufferRootSignatureIndex, NumVariableBufferRootConstants, bufferIndices, 0);
				//unsigned int cameraIndex = context.currentScene->GetCamera()->GetCameraBufferView()->GetOffset() / sizeof(CameraInfo);
				commandList->SetComputeRoot32BitConstants(ViewRootSignatureIndex, 1, &cameraIndex, LightViewIndex);

				miscRootConstants[UintRootConstant0] = primaryView->meshInstanceMeshletCullingBitfieldBuffer->GetResource()->GetUAVShaderVisibleInfo(0).index;
				miscRootConstants[UintRootConstant1] = primaryView->indirectCommandBuffers.meshletCullingResetIndirectCommandBuffer->GetResource()->GetUAVShaderVisibleInfo(0).index;
				miscRootConstants[UintRootConstant2] = primaryDepth->linearDepthMap->GetSRVInfo(0).index;
				miscRootConstants[UintRootConstant3] = primaryView->meshInstanceOcclusionCullingBitfieldBuffer->GetResource()->GetUAVShaderVisibleInfo(0).index;
				miscRootConstants[UintRootConstant5] = primaryView->indirectCommandBuffers.meshletOcclusionCullingIndirectCommandBuffer->GetResource()->GetUAVShaderVisibleInfo(0).index;
				commandList->SetComputeRoot32BitConstants(MiscUintRootSignatureIndex, NumMiscUintRootConstants, miscRootConstants, 0);

				commandList->Dispatch(numThreadGroups, 1, 1);

				if (shadows) {
					lightQuery.each([&](flecs::entity e, Components::Light light, Components::LightViewInfo& lightViewInfo, Components::DepthMap lightDepth) {
						int i = 0;
						for (auto& view : lightViewInfo.renderViews) {
							auto& buffer = view.indirectCommandBuffers.blendIndirectCommandBuffer;
							bufferIndices[IndirectCommandBufferDescriptorIndex] = buffer->GetResource()->GetUAVShaderVisibleInfo(0).index;
							bufferIndices[MeshletCullingIndirectCommandBufferDescriptorIndex] = view.indirectCommandBuffers.meshletFrustrumCullingIndirectCommandBuffer->GetResource()->GetUAVShaderVisibleInfo(0).index;
							commandList->SetComputeRoot32BitConstants(VariableBufferRootSignatureIndex, NumVariableBufferRootConstants, bufferIndices, 0);
							unsigned int lightCameraIndex = view.cameraBufferView->GetOffset() / sizeof(CameraInfo);
							commandList->SetComputeRoot32BitConstants(ViewRootSignatureIndex, 1, &lightCameraIndex, LightViewIndex);

							miscRootConstants[UintRootConstant0] = view.meshInstanceMeshletCullingBitfieldBuffer->GetResource()->GetUAVShaderVisibleInfo(0).index;
							miscRootConstants[UintRootConstant1] = view.indirectCommandBuffers.meshletCullingResetIndirectCommandBuffer->GetResource()->GetUAVShaderVisibleInfo(0).index;
							miscRootConstants[UintRootConstant2] = light.type == Components::LightType::Point ? lightDepth.linearDepthMap->GetSRVInfo(SRVViewType::Texture2DArray, 0).index : lightDepth.linearDepthMap->GetSRVInfo(0).index;
							miscRootConstants[UintRootConstant3] = view.meshInstanceOcclusionCullingBitfieldBuffer->GetResource()->GetUAVShaderVisibleInfo(0).index;
							miscRootConstants[UintRootConstant5] = view.indirectCommandBuffers.meshletOcclusionCullingIndirectCommandBuffer->GetResource()->GetUAVShaderVisibleInfo(0).index;
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

		PSOManager::GetInstance().CompileShader(L"shaders/culling.hlsl", L"ObjectCullingCSMain", L"cs_6_6", defines, computeShader);

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

		PSOManager::GetInstance().CompileShader(L"shaders/culling.hlsl", L"ObjectCullingCSMain", L"cs_6_6", defines, computeShader);
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