#pragma once

#include <DirectX/d3dx12.h>

#include "RenderPass.h"
#include "PSOManager.h"
#include "RenderContext.h"
#include "DeviceManager.h"
#include "utilities.h"
#include "SettingsManager.h"

class FrustrumCullingPass : public RenderPass {
public:
	FrustrumCullingPass() {
		getNumDirectionalLightCascades = SettingsManager::GetInstance().getSettingGetter<uint8_t>("numDirectionalLightCascades");
	}

	void Setup() override {
		auto& manager = DeviceManager::GetInstance();
		auto& device = manager.GetDevice();
		uint8_t numFramesInFlight = SettingsManager::GetInstance().getSettingGetter<uint8_t>("numFramesInFlight")();

		for (int i = 0; i < numFramesInFlight; i++) {
			ComPtr<ID3D12CommandAllocator> allocator;
			ComPtr<ID3D12GraphicsCommandList7> commandList;
			ThrowIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
			ThrowIfFailed(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&commandList)));
			commandList->Close();
			m_allocators.push_back(allocator);
			m_commandLists.push_back(commandList);
		}

		CreatePSO();
	}

	PassReturn Execute(RenderContext& context) override {
		auto& commandList = m_commandLists[context.frameIndex];
		auto& allocator = m_allocators[context.frameIndex];
		ThrowIfFailed(allocator->Reset());
		commandList->Reset(allocator.Get(), nullptr);

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

		auto& meshManager = context.currentScene->GetMeshManager();
		auto& objectManager = context.currentScene->GetObjectManager();
		auto& cameraManager = context.currentScene->GetCameraManager();

		unsigned int staticBufferIndices[NumStaticBufferRootConstants] = {};
		staticBufferIndices[PostSkinningNormalMatrixBufferDescriptorIndex] = objectManager->GetPostSkinningNormalMatrixBufferSRVIndex();
		staticBufferIndices[PostSkinningVertexBufferDescriptorIndex] = meshManager->GetPostSkinningVertexBufferSRVIndex();
		staticBufferIndices[MeshletBufferDescriptorIndex] = meshManager->GetMeshletOffsetBufferSRVIndex();
		staticBufferIndices[MeshletVerticesBufferDescriptorIndex] = meshManager->GetMeshletIndexBufferSRVIndex();
		staticBufferIndices[MeshletTrianglesBufferDescriptorIndex] = meshManager->GetMeshletTriangleBufferSRVIndex();
		staticBufferIndices[PerObjectBufferDescriptorIndex] = objectManager->GetPerObjectBufferSRVIndex();
		staticBufferIndices[CameraBufferDescriptorIndex] = cameraManager->GetCameraBufferSRVIndex();

		commandList->SetComputeRoot32BitConstants(StaticBufferRootSignatureIndex, NumStaticBufferRootConstants, &staticBufferIndices, 0);

		unsigned int numCascades = getNumDirectionalLightCascades();

		// opaque buffer
		auto numOpaqueDraws = context.currentScene->GetNumOpaqueDraws();
		if (numOpaqueDraws > 0) {
			unsigned int numThreadGroups = std::ceil(context.currentScene->GetNumOpaqueDraws() / 64.0);
			// First, process buffer for main camera
			auto resource = context.currentScene->GetPrimaryCameraOpaqueIndirectCommandBuffer()->GetResource();
			auto apiResource = resource->GetAPIResource();
			auto uavShaderVisibleInfo = resource->GetUAVShaderVisibleInfo();
			auto uavNonShaderVisibleInfo = resource->GetUAVNonShaderVisibleInfo();
			unsigned int bufferIndices[NumVariableBufferRootConstants] = {};
			bufferIndices[PerMeshBufferDescriptorIndex] = meshManager->GetOpaquePerMeshBufferSRVIndex();
			bufferIndices[DrawSetCommandBufferDescriptorIndex] = objectManager->GetOpaqueDrawSetCommandsBufferSRVIndex();
			bufferIndices[ActiveDrawSetIndicesBufferDescriptorIndex] = objectManager->GetActiveOpaqueDrawSetIndicesBufferSRVIndex();
			bufferIndices[IndirectCommandBufferDescriptorIndex] = context.currentScene->GetPrimaryCameraOpaqueIndirectCommandBuffer()->GetResource()->GetUAVShaderVisibleInfo().index;
			bufferIndices[MaxDrawIndex] = context.currentScene->GetNumOpaqueDraws()-1;

			commandList->SetComputeRoot32BitConstants(VariableBufferRootSignatureIndex, NumVariableBufferRootConstants, bufferIndices, 0);
			unsigned int cameraIndex = context.currentScene->GetCamera()->GetCameraBufferView()->GetOffset() / sizeof(CameraInfo);
			commandList->SetComputeRoot32BitConstants(ViewRootSignatureIndex, 1, &cameraIndex, LightViewIndex);

			commandList->Dispatch(numThreadGroups, 1, 1);

			//  Then, process buffer for each light
			for (auto& lightPair : context.currentScene->GetLightIDMap()) {
				auto& light = lightPair.second;
				auto& lightViews = light->GetCameraBufferViews();
				int i = 0;
				for (auto& buffer : light->GetPerViewOpaqueIndirectCommandBuffers()) {
					bufferIndices[IndirectCommandBufferDescriptorIndex] = buffer->GetResource()->GetUAVShaderVisibleInfo().index;
					commandList->SetComputeRoot32BitConstants(VariableBufferRootSignatureIndex, 1, &bufferIndices[IndirectCommandBufferDescriptorIndex], IndirectCommandBufferDescriptorIndex);
					unsigned int lightCameraIndex = lightViews[i]->GetOffset() / sizeof(CameraInfo);
					commandList->SetComputeRoot32BitConstants(ViewRootSignatureIndex, 1, &lightCameraIndex, LightViewIndex);
					i++;
					//commandList->Dispatch(numThreadGroups, 1, 1);
				}
			}
		}
		// alpha test buffer
		auto numAlphaTestDraws = context.currentScene->GetNumAlphaTestDraws();
		if (numAlphaTestDraws > 0) {
			unsigned int numThreadGroups = std::ceil(numAlphaTestDraws / 64.0);
			auto resource = context.currentScene->GetPrimaryCameraAlphaTestIndirectCommandBuffer()->GetResource();
			auto apiResource = resource->GetAPIResource();
			auto uavShaderVisibleInfo = resource->GetUAVShaderVisibleInfo();
			auto uavNonShaderVisibleInfo = resource->GetUAVNonShaderVisibleInfo();
			unsigned int bufferIndices[NumVariableBufferRootConstants] = {};
			bufferIndices[PerMeshBufferDescriptorIndex] = meshManager->GetAlphaTestPerMeshBufferSRVIndex();
			bufferIndices[DrawSetCommandBufferDescriptorIndex] = objectManager->GetAlphaTestDrawSetCommandsBufferSRVIndex();
			bufferIndices[ActiveDrawSetIndicesBufferDescriptorIndex] = objectManager->GetActiveAlphaTestDrawSetIndicesBufferSRVIndex();
			bufferIndices[IndirectCommandBufferDescriptorIndex] = context.currentScene->GetPrimaryCameraAlphaTestIndirectCommandBuffer()->GetResource()->GetUAVShaderVisibleInfo().index;
			bufferIndices[MaxDrawIndex] = context.currentScene->GetNumAlphaTestDraws()-1;

			commandList->SetComputeRoot32BitConstants(VariableBufferRootSignatureIndex, NumVariableBufferRootConstants, bufferIndices, 0);
			unsigned int cameraIndex = context.currentScene->GetCamera()->GetCameraBufferView()->GetOffset() / sizeof(CameraInfo);
			commandList->SetComputeRoot32BitConstants(ViewRootSignatureIndex, 1, &cameraIndex, LightViewIndex);
		
			commandList->Dispatch(numThreadGroups, 1, 1);

			for (auto& lightPair : context.currentScene->GetLightIDMap()) {
				auto& light = lightPair.second;
				auto& lightViews = light->GetCameraBufferViews();
				int i = 0;
				for (auto& buffer : light->GetPerViewAlphaTestIndirectCommandBuffers()) {
					bufferIndices[3] = buffer->GetResource()->GetUAVShaderVisibleInfo().index;
					commandList->SetComputeRoot32BitConstants(VariableBufferRootSignatureIndex, 1, &bufferIndices[IndirectCommandBufferDescriptorIndex], IndirectCommandBufferDescriptorIndex);
					unsigned int lightCameraIndex = lightViews[i]->GetOffset() / sizeof(CameraInfo);
					commandList->SetComputeRoot32BitConstants(ViewRootSignatureIndex, 1, &lightCameraIndex, LightViewIndex);
					i++;
					commandList->Dispatch(numThreadGroups, 1, 1);
				}
			}
		}

		// blend buffer
		auto numBlendDraws = context.currentScene->GetNumBlendDraws();
		if (numBlendDraws > 0) {
			unsigned int numThreadGroups = std::ceil(numBlendDraws / 64.0);
			auto resource = context.currentScene->GetPrimaryCameraBlendIndirectCommandBuffer()->GetResource();
			auto apiResource = resource->GetAPIResource();
			auto uavShaderVisibleInfo = resource->GetUAVShaderVisibleInfo();
			auto uavNonShaderVisibleInfo = resource->GetUAVNonShaderVisibleInfo();
			unsigned int bufferIndices[NumVariableBufferRootConstants] = {};
			bufferIndices[PerMeshBufferDescriptorIndex] = meshManager->GetBlendPerMeshBufferSRVIndex();
			bufferIndices[DrawSetCommandBufferDescriptorIndex] = objectManager->GetBlendDrawSetCommandsBufferSRVIndex();
			bufferIndices[ActiveDrawSetIndicesBufferDescriptorIndex] = objectManager->GetActiveBlendDrawSetIndicesBufferSRVIndex();
			bufferIndices[IndirectCommandBufferDescriptorIndex] = context.currentScene->GetPrimaryCameraBlendIndirectCommandBuffer()->GetResource()->GetUAVShaderVisibleInfo().index;
			bufferIndices[MaxDrawIndex] = context.currentScene->GetNumBlendDraws() - 1;

			commandList->SetComputeRoot32BitConstants(VariableBufferRootSignatureIndex, NumVariableBufferRootConstants, bufferIndices, 0);
			unsigned int cameraIndex = context.currentScene->GetCamera()->GetCameraBufferView()->GetOffset() / sizeof(CameraInfo);
			commandList->SetComputeRoot32BitConstants(ViewRootSignatureIndex, 1, &cameraIndex, LightViewIndex);

			commandList->Dispatch(numThreadGroups, 1, 1);

			for (auto& lightPair : context.currentScene->GetLightIDMap()) {
				auto& light = lightPair.second;
				auto& lightViews = light->GetCameraBufferViews();
				int i = 0;
				for (auto& buffer : light->GetPerViewBlendIndirectCommandBuffers()) {
					bufferIndices[3] = buffer->GetResource()->GetUAVShaderVisibleInfo().index;
					commandList->SetComputeRoot32BitConstants(VariableBufferRootSignatureIndex, 1, &bufferIndices[IndirectCommandBufferDescriptorIndex], IndirectCommandBufferDescriptorIndex);
					unsigned int lightCameraIndex = lightViews[i]->GetOffset() / sizeof(CameraInfo);
					commandList->SetComputeRoot32BitConstants(ViewRootSignatureIndex, 1, &lightCameraIndex, LightViewIndex);
					i++;
					commandList->Dispatch(numThreadGroups, 1, 1);
				}
			}
		}

		ThrowIfFailed(commandList->Close());

		//invalidated = false;

		return { { commandList.Get()} };
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

	std::vector<ComPtr<ID3D12GraphicsCommandList7>> m_commandLists;
	std::vector<ComPtr<ID3D12CommandAllocator>> m_allocators;
	ComPtr<ID3D12PipelineState> m_PSO;

	std::function<uint8_t()> getNumDirectionalLightCascades;

};