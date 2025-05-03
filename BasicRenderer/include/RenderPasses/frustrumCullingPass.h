#pragma once

#include <DirectX/d3dx12.h>

#include "RenderPasses/Base/RenderPass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Utilities/Utilities.h"
#include "Managers/Singletons/SettingsManager.h"

class FrustrumCullingPass : public ComputePass {
public:
	FrustrumCullingPass() {
		getNumDirectionalLightCascades = SettingsManager::GetInstance().getSettingGetter<uint8_t>("numDirectionalLightCascades");
	}

	~FrustrumCullingPass() {
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
		staticBufferIndices[NormalMatrixBufferDescriptorIndex] = objectManager->GetNormalMatrixBufferSRVIndex();
		staticBufferIndices[PostSkinningVertexBufferDescriptorIndex] = meshManager->GetPostSkinningVertexBufferSRVIndex();
		staticBufferIndices[MeshletBufferDescriptorIndex] = meshManager->GetMeshletOffsetBufferSRVIndex();
		staticBufferIndices[MeshletVerticesBufferDescriptorIndex] = meshManager->GetMeshletIndexBufferSRVIndex();
		staticBufferIndices[MeshletTrianglesBufferDescriptorIndex] = meshManager->GetMeshletTriangleBufferSRVIndex();
		staticBufferIndices[PerObjectBufferDescriptorIndex] = objectManager->GetPerObjectBufferSRVIndex();
		staticBufferIndices[CameraBufferDescriptorIndex] = cameraManager->GetCameraBufferSRVIndex();
		staticBufferIndices[PerMeshBufferDescriptorIndex] = meshManager->GetPerMeshBufferSRVIndex();
		staticBufferIndices[DrawSetCommandBufferDescriptorIndex] = objectManager->GetMasterIndirectCommandsBufferSRVIndex();

		commandList->SetComputeRoot32BitConstants(StaticBufferRootSignatureIndex, NumStaticBufferRootConstants, &staticBufferIndices, 0);

		unsigned int numCascades = getNumDirectionalLightCascades();
		unsigned int cameraIndex = context.currentScene->GetPrimaryCamera().get<Components::RenderView>()->cameraBufferIndex;
		// opaque buffer
		auto numOpaqueDraws = context.drawStats.numOpaqueDraws;
		if (numOpaqueDraws > 0) {
			unsigned int numThreadGroups = std::ceil(numOpaqueDraws / 64.0);
			// First, process buffer for main camera
			auto resource = context.currentScene->GetPrimaryCameraOpaqueIndirectCommandBuffer()->GetResource();
			auto apiResource = resource->GetAPIResource();
			auto uavShaderVisibleInfo = resource->GetUAVShaderVisibleInfo();
			auto uavNonShaderVisibleInfo = resource->GetUAVNonShaderVisibleInfo();
			unsigned int bufferIndices[NumVariableBufferRootConstants] = {};
			bufferIndices[ActiveDrawSetIndicesBufferDescriptorIndex] = objectManager->GetActiveOpaqueDrawSetIndicesBufferSRVIndex();
			bufferIndices[IndirectCommandBufferDescriptorIndex] = context.currentScene->GetPrimaryCameraOpaqueIndirectCommandBuffer()->GetResource()->GetUAVShaderVisibleInfo()[0].index;
			bufferIndices[MaxDrawIndex] = numOpaqueDraws-1;

			commandList->SetComputeRoot32BitConstants(VariableBufferRootSignatureIndex, NumVariableBufferRootConstants, bufferIndices, 0);
			//unsigned int cameraIndex = context.currentScene->GetCamera()->GetCameraBufferView()->GetOffset() / sizeof(CameraInfo);
			commandList->SetComputeRoot32BitConstants(ViewRootSignatureIndex, 1, &cameraIndex, LightViewIndex);

			commandList->Dispatch(numThreadGroups, 1, 1);

			lightQuery.each([&](flecs::entity e, Components::LightViewInfo& lightViewInfo) {
				int i = 0;
				for (auto& view : lightViewInfo.renderViews) {
					auto& buffer = view.indirectCommandBuffers.opaqueIndirectCommandBuffer;
					bufferIndices[IndirectCommandBufferDescriptorIndex] = buffer->GetResource()->GetUAVShaderVisibleInfo()[0].index;
					commandList->SetComputeRoot32BitConstants(VariableBufferRootSignatureIndex, 1, &bufferIndices[IndirectCommandBufferDescriptorIndex], IndirectCommandBufferDescriptorIndex);
					unsigned int lightCameraIndex = view.cameraBufferView->GetOffset() / sizeof(CameraInfo);
					commandList->SetComputeRoot32BitConstants(ViewRootSignatureIndex, 1, &lightCameraIndex, LightViewIndex);
					i++;
					commandList->Dispatch(numThreadGroups, 1, 1);
				}
				});
		}
		// alpha test buffer
		auto numAlphaTestDraws = context.drawStats.numAlphaTestDraws;
		if (numAlphaTestDraws > 0) {
			unsigned int numThreadGroups = std::ceil(numAlphaTestDraws / 64.0);
			auto resource = context.currentScene->GetPrimaryCameraAlphaTestIndirectCommandBuffer()->GetResource();
			auto apiResource = resource->GetAPIResource();
			auto uavShaderVisibleInfo = resource->GetUAVShaderVisibleInfo();
			auto uavNonShaderVisibleInfo = resource->GetUAVNonShaderVisibleInfo();
			unsigned int bufferIndices[NumVariableBufferRootConstants] = {};
			bufferIndices[ActiveDrawSetIndicesBufferDescriptorIndex] = objectManager->GetActiveAlphaTestDrawSetIndicesBufferSRVIndex();
			bufferIndices[IndirectCommandBufferDescriptorIndex] = context.currentScene->GetPrimaryCameraAlphaTestIndirectCommandBuffer()->GetResource()->GetUAVShaderVisibleInfo()[0].index;
			bufferIndices[MaxDrawIndex] = numAlphaTestDraws-1;

			commandList->SetComputeRoot32BitConstants(VariableBufferRootSignatureIndex, NumVariableBufferRootConstants, bufferIndices, 0);
			//unsigned int cameraIndex = context.currentScene->GetCamera()->GetCameraBufferView()->GetOffset() / sizeof(CameraInfo);
			commandList->SetComputeRoot32BitConstants(ViewRootSignatureIndex, 1, &cameraIndex, LightViewIndex);
		
			commandList->Dispatch(numThreadGroups, 1, 1);

			lightQuery.each([&](flecs::entity e, Components::LightViewInfo& lightViewInfo) {
				int i = 0;
				for (auto& view : lightViewInfo.renderViews) {
					auto& buffer = view.indirectCommandBuffers.alphaTestIndirectCommandBuffer;
					bufferIndices[IndirectCommandBufferDescriptorIndex] = buffer->GetResource()->GetUAVShaderVisibleInfo()[0].index;
					commandList->SetComputeRoot32BitConstants(VariableBufferRootSignatureIndex, 1, &bufferIndices[IndirectCommandBufferDescriptorIndex], IndirectCommandBufferDescriptorIndex);
					unsigned int lightCameraIndex = view.cameraBufferView->GetOffset() / sizeof(CameraInfo);
					commandList->SetComputeRoot32BitConstants(ViewRootSignatureIndex, 1, &lightCameraIndex, LightViewIndex);
					i++;
					commandList->Dispatch(numThreadGroups, 1, 1);
				}
				});
		}

		// blend buffer
		auto numBlendDraws = context.drawStats.numBlendDraws;
		if (numBlendDraws > 0) {
			unsigned int numThreadGroups = std::ceil(numBlendDraws / 64.0);
			auto resource = context.currentScene->GetPrimaryCameraBlendIndirectCommandBuffer()->GetResource();
			auto apiResource = resource->GetAPIResource();
			auto uavShaderVisibleInfo = resource->GetUAVShaderVisibleInfo();
			auto uavNonShaderVisibleInfo = resource->GetUAVNonShaderVisibleInfo();
			unsigned int bufferIndices[NumVariableBufferRootConstants] = {};
			bufferIndices[ActiveDrawSetIndicesBufferDescriptorIndex] = objectManager->GetActiveBlendDrawSetIndicesBufferSRVIndex();
			bufferIndices[IndirectCommandBufferDescriptorIndex] = context.currentScene->GetPrimaryCameraBlendIndirectCommandBuffer()->GetResource()->GetUAVShaderVisibleInfo()[0].index;
			bufferIndices[MaxDrawIndex] = numBlendDraws - 1;

			commandList->SetComputeRoot32BitConstants(VariableBufferRootSignatureIndex, NumVariableBufferRootConstants, bufferIndices, 0);
			//unsigned int cameraIndex = context.currentScene->GetCamera()->GetCameraBufferView()->GetOffset() / sizeof(CameraInfo);
			commandList->SetComputeRoot32BitConstants(ViewRootSignatureIndex, 1, &cameraIndex, LightViewIndex);

			commandList->Dispatch(numThreadGroups, 1, 1);

			lightQuery.each([&](flecs::entity e, Components::LightViewInfo& lightViewInfo) {
				int i = 0;
				for (auto& view : lightViewInfo.renderViews) {
					auto& buffer = view.indirectCommandBuffers.blendIndirectCommandBuffer;
					bufferIndices[IndirectCommandBufferDescriptorIndex] = buffer->GetResource()->GetUAVShaderVisibleInfo()[0].index;
					commandList->SetComputeRoot32BitConstants(VariableBufferRootSignatureIndex, 1, &bufferIndices[IndirectCommandBufferDescriptorIndex], IndirectCommandBufferDescriptorIndex);
					unsigned int lightCameraIndex = view.cameraBufferView->GetOffset() / sizeof(CameraInfo);
					commandList->SetComputeRoot32BitConstants(ViewRootSignatureIndex, 1, &lightCameraIndex, LightViewIndex);
					i++;
					commandList->Dispatch(numThreadGroups, 1, 1);
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
		PSOManager::GetInstance().CompileShader(L"shaders/frustrumCulling.hlsl", L"CSMain", L"cs_6_6", {}, computeShader);

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
	}
	
	flecs::query<Components::LightViewInfo> lightQuery;

	ComPtr<ID3D12PipelineState> m_PSO;

	std::function<uint8_t()> getNumDirectionalLightCascades;

};