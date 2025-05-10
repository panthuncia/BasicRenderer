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

class MeshletFrustrumCullingPass : public ComputePass {
public:
	MeshletFrustrumCullingPass() {
		getNumDirectionalLightCascades = SettingsManager::GetInstance().getSettingGetter<uint8_t>("numDirectionalLightCascades");
		getShadowsEnabled = SettingsManager::GetInstance().getSettingGetter<bool>("enableShadows");
	}

	~MeshletFrustrumCullingPass() {
	}

	void Setup() override {
		auto& ecsWorld = ECSManager::GetInstance().GetWorld();
		lightQuery = ecsWorld.query_builder<Components::LightViewInfo>().cached().cache_kind(flecs::QueryCacheAll).build();

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
		staticBufferIndices[PerMeshInstanceBufferDescriptorIndex] = meshManager->GetPerMeshInstanceBufferSRVIndex();

		commandList->SetComputeRoot32BitConstants(StaticBufferRootSignatureIndex, NumStaticBufferRootConstants, &staticBufferIndices, 0);

		unsigned int miscRootConstants[NumMiscUintRootConstants] = {};
		miscRootConstants[UintRootConstant0] = context.meshManager->GetMeshletBoundsBufferSRVIndex();
		miscRootConstants[UintRootConstant1] = context.currentScene->GetPrimaryCameraMeshletFrustrumCullingBitfieldBuffer()->GetResource()->GetUAVShaderVisibleInfo(0).index;

		commandList->SetComputeRoot32BitConstants(MiscUintRootSignatureIndex, NumMiscUintRootConstants, &miscRootConstants, 0);

		unsigned int cameraIndex = context.currentScene->GetPrimaryCamera().get<Components::RenderView>()->cameraBufferIndex;
		commandList->SetComputeRoot32BitConstants(ViewRootSignatureIndex, 1, &cameraIndex, LightViewIndex);

		// Culling for main camera
		unsigned int numDraws = context.drawStats.numOpaqueDraws + context.drawStats.numAlphaTestDraws + context.drawStats.numBlendDraws;

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

		auto meshletCullingClearBuffer = context.currentScene->GetPrimaryCameraMeshletFrustrumCullingResetIndirectCommandBuffer();
		commandList->SetPipelineState(m_clearPSO.Get());

		commandList->ExecuteIndirect(
			commandSignature,
			numDraws,
			meshletCullingClearBuffer->GetResource()->GetAPIResource(),
			0,
			meshletCullingClearBuffer->GetResource()->GetAPIResource(),
			meshletCullingClearBuffer->GetResource()->GetUAVCounterOffset()
		);

		if (getShadowsEnabled()) {
			lightQuery.each([&](flecs::entity e, Components::LightViewInfo& lightViewInfo) {
				commandList->SetPipelineState(m_PSO.Get());

				for (auto& view : lightViewInfo.renderViews) {

					cameraIndex = view.cameraBufferIndex;
					commandList->SetComputeRoot32BitConstants(ViewRootSignatureIndex, 1, &cameraIndex, LightViewIndex);

					miscRootConstants[UintRootConstant1] = view.meshletBitfieldBuffer->GetResource()->GetUAVShaderVisibleInfo(0).index;
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

			lightQuery.each([&](flecs::entity e, Components::LightViewInfo& lightViewInfo) {
				commandList->SetPipelineState(m_clearPSO.Get());

				for (auto& view : lightViewInfo.renderViews) {

					cameraIndex = view.cameraBufferIndex;
					commandList->SetComputeRoot32BitConstants(ViewRootSignatureIndex, 1, &cameraIndex, LightViewIndex);

					miscRootConstants[UintRootConstant1] = view.meshletBitfieldBuffer->GetResource()->GetUAVShaderVisibleInfo(0).index;
					commandList->SetComputeRoot32BitConstants(MiscUintRootSignatureIndex, NumMiscUintRootConstants, &miscRootConstants, 0);
					meshletCullingClearBuffer = view.indirectCommandBuffers.meshletFrustrumCullingResetIndirectCommandBuffer;
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

		return {};
	}

	void Cleanup(RenderContext& context) override {

	}

private:

	void CreatePSO() {
		// Compile the compute shader
		Microsoft::WRL::ComPtr<ID3DBlob> computeShader;
		PSOManager::GetInstance().CompileShader(L"shaders/frustrumCulling.hlsl", L"MeshletFrustrumCullingCSMain", L"cs_6_6", {}, computeShader);

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

		PSOManager::GetInstance().CompileShader(L"shaders/frustrumCulling.hlsl", L"ClearMeshletFrustrumCullingCSMain", L"cs_6_6", {}, computeShader);

		pipelineStateStream.CS = CD3DX12_SHADER_BYTECODE(computeShader.Get());
		ThrowIfFailed(device2->CreatePipelineState(&streamDesc, IID_PPV_ARGS(&m_clearPSO)));
	}

	flecs::query<Components::LightViewInfo> lightQuery;

	ComPtr<ID3D12PipelineState> m_PSO;
	ComPtr<ID3D12PipelineState> m_clearPSO;

	std::function<uint8_t()> getNumDirectionalLightCascades;
	std::function<bool()> getShadowsEnabled;
};