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

class MeshletCullingPass : public ComputePass {
public:
	MeshletCullingPass(bool isOccludersPass, bool isRemaindersPass = false, bool doResets = true) :
		m_isOccludersPass(isOccludersPass), 
		m_isRemaindersPass(isRemaindersPass),
		m_doResets(doResets){
		getNumDirectionalLightCascades = SettingsManager::GetInstance().getSettingGetter<uint8_t>("numDirectionalLightCascades");
		getShadowsEnabled = SettingsManager::GetInstance().getSettingGetter<bool>("enableShadows");
	}

	~MeshletCullingPass() {
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
		commandList->SetPipelineState(m_frustrumCullingPSO.Get());

		auto& meshManager = context.meshManager;
		auto& objectManager = context.objectManager;
		auto& cameraManager = context.cameraManager;

		unsigned int staticBufferIndices[NumStaticBufferRootConstants] = {};
		staticBufferIndices[PerObjectBufferDescriptorIndex] = objectManager->GetPerObjectBufferSRVIndex();
		staticBufferIndices[CameraBufferDescriptorIndex] = cameraManager->GetCameraBufferSRVIndex();
		staticBufferIndices[PerMeshBufferDescriptorIndex] = meshManager->GetPerMeshBufferSRVIndex();
		staticBufferIndices[PerMeshInstanceBufferDescriptorIndex] = meshManager->GetPerMeshInstanceBufferSRVIndex();

		commandList->SetComputeRoot32BitConstants(StaticBufferRootSignatureIndex, NumStaticBufferRootConstants, &staticBufferIndices, 0);

		unsigned int miscRootConstants[NumMiscUintRootConstants] = {};
		miscRootConstants[UintRootConstant0] = context.meshManager->GetMeshletBoundsBufferSRVIndex();
		miscRootConstants[UintRootConstant1] = context.currentScene->GetPrimaryCameraMeshletFrustrumCullingBitfieldBuffer()->GetResource()->GetUAVShaderVisibleInfo(0).index;
		auto primaryDepth = context.currentScene->GetPrimaryCamera().get<Components::DepthMap>();
		miscRootConstants[UintRootConstant2] = primaryDepth->linearDepthMap->GetSRVInfo(0).index;

		commandList->SetComputeRoot32BitConstants(MiscUintRootSignatureIndex, NumMiscUintRootConstants, &miscRootConstants, 0);

		unsigned int cameraIndex = context.currentScene->GetPrimaryCamera().get<Components::RenderView>()->cameraBufferIndex;
		commandList->SetComputeRoot32BitConstants(ViewRootSignatureIndex, 1, &cameraIndex, LightViewIndex);

		// Culling for main camera
		unsigned int numDraws = context.drawStats.numOpaqueDraws + context.drawStats.numAlphaTestDraws + context.drawStats.numBlendDraws;

		// Frustrum culling
		auto meshletCullingBuffer = context.currentScene->GetPrimaryCameraMeshletFrustrumCullingIndirectCommandBuffer();
		
		auto commandSignature = CommandSignatureManager::GetInstance().GetDispatchCommandSignature();
		commandList->ExecuteIndirect(
			commandSignature,
			numDraws,
			meshletCullingBuffer->GetResource()->GetAPIResource(),
			0,
			meshletCullingBuffer->GetResource()->GetAPIResource(),
			meshletCullingBuffer->GetResource()->GetUAVCounterOffset()
		);

		// Occlusion culling
		//if (!m_isOccludersPass) { // Occluders pass builds HZB, and it is used in the later culling pass
		//	auto meshletOcclusionCullingBuffer = context.currentScene->GetPrimaryCameraMeshletOcclusionCullingIndirectCommandBuffer();
		//	commandList->SetPipelineState(m_occlusionCullingPSO.Get());

		//	auto primaryDepth = context.currentScene->GetPrimaryCamera().get<Components::DepthMap>();
		//	miscRootConstants[UintRootConstant2] = primaryDepth->linearDepthMap->GetSRVInfo(0).index;
		//	commandList->SetComputeRoot32BitConstants(MiscUintRootSignatureIndex, NumMiscUintRootConstants, &miscRootConstants, 0);

		//	commandList->ExecuteIndirect(
		//		commandSignature,
		//		numDraws,
		//		meshletOcclusionCullingBuffer->GetResource()->GetAPIResource(),
		//		0,
		//		meshletOcclusionCullingBuffer->GetResource()->GetAPIResource(),
		//		meshletOcclusionCullingBuffer->GetResource()->GetUAVCounterOffset()
		//	);
		//}

		// Reset necessary meshlets
		if (m_doResets) {
			auto meshletCullingClearBuffer = context.currentScene->GetPrimaryCameraMeshletCullingResetIndirectCommandBuffer();
			commandList->SetPipelineState(m_clearPSO.Get());

			commandList->ExecuteIndirect(
				commandSignature,
				numDraws,
				meshletCullingClearBuffer->GetResource()->GetAPIResource(),
				0,
				meshletCullingClearBuffer->GetResource()->GetAPIResource(),
				meshletCullingClearBuffer->GetResource()->GetUAVCounterOffset()
			);
		}

		if (getShadowsEnabled()) {

			// Frustrum culling
			commandList->SetPipelineState(m_frustrumCullingPSO.Get());
			lightQuery.each([&](flecs::entity e, Components::Light light, Components::LightViewInfo& lightViewInfo, Components::DepthMap lightDepth) {

				for (auto& view : lightViewInfo.renderViews) {

					cameraIndex = view.cameraBufferIndex;
					commandList->SetComputeRoot32BitConstants(ViewRootSignatureIndex, 1, &cameraIndex, LightViewIndex);

					miscRootConstants[UintRootConstant1] = view.meshletBitfieldBuffer->GetResource()->GetUAVShaderVisibleInfo(0).index;
					miscRootConstants[UintRootConstant2] = light.type == Components::LightType::Point ? lightDepth.linearDepthMap->GetSRVInfo(SRVViewType::Texture2DArray, 0).index : lightDepth.linearDepthMap->GetSRVInfo(0).index;
					commandList->SetComputeRoot32BitConstants(MiscUintRootSignatureIndex, NumMiscUintRootConstants, &miscRootConstants, 0);
					meshletCullingBuffer = view.indirectCommandBuffers.meshletFrustrumCullingIndirectCommandBuffer;
					commandList->ExecuteIndirect(
						commandSignature,
						numDraws,
						meshletCullingBuffer->GetResource()->GetAPIResource(),
						0,
						meshletCullingBuffer->GetResource()->GetAPIResource(),
						meshletCullingBuffer->GetResource()->GetUAVCounterOffset()
					);
				}
				});

			// Occlusion culling
			//if (!m_isOccludersPass) { // Occluders pass builds HZB, and it is used in the later culling pass
			//	commandList->SetPipelineState(m_occlusionCullingPSO.Get());
			//	lightQuery.each([&](flecs::entity e, Components::LightViewInfo& lightViewInfo, Components::DepthMap lightDepth) {
			//		for (auto& view : lightViewInfo.renderViews) {
			//			cameraIndex = view.cameraBufferIndex;
			//			commandList->SetComputeRoot32BitConstants(ViewRootSignatureIndex, 1, &cameraIndex, LightViewIndex);
			//			miscRootConstants[UintRootConstant2] = lightDepth.linearDepthMap->GetSRVInfo(0).index;
			//			commandList->SetComputeRoot32BitConstants(MiscUintRootSignatureIndex, NumMiscUintRootConstants, &miscRootConstants, 0);
			//			auto meshletOcclusionCullingBuffer = view.indirectCommandBuffers.meshletOcclusionCullingIndirectCommandBuffer;
			//			commandList->ExecuteIndirect(
			//				commandSignature,
			//				numDraws,
			//				meshletOcclusionCullingBuffer->GetResource()->GetAPIResource(),
			//				0,
			//				meshletOcclusionCullingBuffer->GetResource()->GetAPIResource(),
			//				meshletOcclusionCullingBuffer->GetResource()->GetUAVCounterOffset()
			//			);
			//		}
			//		});
			//}

			// Reset necessary meshlets
			if (m_doResets) {
				commandList->SetPipelineState(m_clearPSO.Get());
				lightQuery.each([&](flecs::entity e, Components::Light light, Components::LightViewInfo& lightViewInfo, Components::DepthMap lightDepth) {

					for (auto& view : lightViewInfo.renderViews) {

						cameraIndex = view.cameraBufferIndex;
						commandList->SetComputeRoot32BitConstants(ViewRootSignatureIndex, 1, &cameraIndex, LightViewIndex);

						miscRootConstants[UintRootConstant1] = view.meshletBitfieldBuffer->GetResource()->GetUAVShaderVisibleInfo(0).index;
						commandList->SetComputeRoot32BitConstants(MiscUintRootSignatureIndex, NumMiscUintRootConstants, &miscRootConstants, 0);
						auto meshletCullingClearBuffer = view.indirectCommandBuffers.meshletCullingResetIndirectCommandBuffer;
						commandList->ExecuteIndirect(
							commandSignature,
							numDraws,
							meshletCullingClearBuffer->GetResource()->GetAPIResource(),
							0,
							meshletCullingClearBuffer->GetResource()->GetAPIResource(),
							meshletCullingClearBuffer->GetResource()->GetUAVCounterOffset()
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
		// Compile the compute shader
		Microsoft::WRL::ComPtr<ID3DBlob> computeShader;
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
		PSOManager::GetInstance().CompileShader(L"shaders/culling.hlsl", L"MeshletFrustrumCullingCSMain", L"cs_6_6", defines, computeShader);

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
		ThrowIfFailed(device2->CreatePipelineState(&streamDesc, IID_PPV_ARGS(&m_frustrumCullingPSO)));

		PSOManager::GetInstance().CompileShader(L"shaders/culling.hlsl", L"MeshletOcclusionCullingCSMain", L"cs_6_6", {}, computeShader);

		pipelineStateStream.CS = CD3DX12_SHADER_BYTECODE(computeShader.Get());
		ThrowIfFailed(device2->CreatePipelineState(&streamDesc, IID_PPV_ARGS(&m_occlusionCullingPSO)));

		PSOManager::GetInstance().CompileShader(L"shaders/culling.hlsl", L"ClearMeshletFrustrumCullingCSMain", L"cs_6_6", {}, computeShader);

		pipelineStateStream.CS = CD3DX12_SHADER_BYTECODE(computeShader.Get());
		ThrowIfFailed(device2->CreatePipelineState(&streamDesc, IID_PPV_ARGS(&m_clearPSO)));
	}

	flecs::query<Components::Light, Components::LightViewInfo, Components::DepthMap> lightQuery;

	ComPtr<ID3D12PipelineState> m_frustrumCullingPSO;
	ComPtr<ID3D12PipelineState> m_occlusionCullingPSO;
	ComPtr<ID3D12PipelineState> m_clearPSO;

	bool m_isOccludersPass = false;
	bool m_isRemaindersPass = false;
	bool m_doResets = true;

	std::function<uint8_t()> getNumDirectionalLightCascades;
	std::function<bool()> getShadowsEnabled;
};