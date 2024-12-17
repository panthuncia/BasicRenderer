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
	FrustrumCullingPass(bool drawShadows) {
		getNumDirectionalLightCascades = SettingsManager::GetInstance().getSettingGetter<uint8_t>("numDirectionalLightCascades");
		m_drawShadows = drawShadows;
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
		CreateRootSignature();
		CreatePSO();
	}

	PassReturn Execute(RenderContext& context) override {
		auto& commandList = m_commandLists[context.frameIndex];
		auto& allocator = m_allocators[context.frameIndex];
		ThrowIfFailed(allocator->Reset());
		commandList->Reset(allocator.Get(), nullptr);

		auto rootSignature = m_rootSignature;//PSOManager::GetInstance().GetRootSignature();
		commandList->SetComputeRootSignature(rootSignature.Get());

		// Set the descriptor heaps
		ID3D12DescriptorHeap* descriptorHeaps[] = {
			ResourceManager::GetInstance().GetSRVDescriptorHeap().Get(),
			ResourceManager::GetInstance().GetSamplerDescriptorHeap().Get()
		};

		commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

		// Set the compute pipeline state
		commandList->SetPipelineState(m_PSO.Get());

		unsigned int staticBufferIndices[6] = {};
		auto& meshManager = context.currentScene->GetMeshManager();
		auto& objectManager = context.currentScene->GetObjectManager();
		auto& cameraManager = context.currentScene->GetCameraManager();
		staticBufferIndices[0] = meshManager->GetVertexBufferIndex();
		staticBufferIndices[1] = meshManager->GetMeshletOffsetBufferIndex();
		staticBufferIndices[2] = meshManager->GetMeshletIndexBufferIndex();
		staticBufferIndices[3] = meshManager->GetMeshletTriangleBufferIndex();
		staticBufferIndices[4] = objectManager->GetPerObjectBufferSRVIndex();
		staticBufferIndices[5] = cameraManager->GetCameraBufferSRVIndex();

		commandList->SetComputeRoot32BitConstants(5, 6, &staticBufferIndices, 0);

		unsigned int numCascades = getNumDirectionalLightCascades();

		// opaque buffer
		auto numOpaqueDraws = context.currentScene->GetNumOpaqueDraws();
		if (numOpaqueDraws > 0) {
			unsigned int numThreadGroups = std::ceil(context.currentScene->GetNumOpaqueDraws() / 64.0);
			// First, process buffer for main camera
			auto resource = context.currentScene->GetPrimaryCameraOpaqueIndirectCommandBuffer()->GetResource();
			auto apiResource = resource->GetAPIResource();
			auto& uavShaderVisibleInfo = resource->GetUAVShaderVisibleInfo();
			unsigned int bufferIndices[5] = {};
			bufferIndices[0] = meshManager->GetOpaquePerMeshBufferSRVIndex();
			bufferIndices[1] = objectManager->GetOpaqueDrawSetCommandsBufferSRVIndex();
			bufferIndices[2] = objectManager->GetActiveOpaqueDrawSetIndicesBufferSRVIndex();
			bufferIndices[3] = context.currentScene->GetPrimaryCameraOpaqueIndirectCommandBuffer()->GetResource()->GetUAVShaderVisibleInfo().index;
			bufferIndices[4] = context.currentScene->GetNumOpaqueDraws()-1;

			commandList->SetComputeRoot32BitConstants(6, 5, bufferIndices, 0);
			unsigned int cameraIndex = context.currentScene->GetCamera()->GetCameraBufferView()->GetOffset() / sizeof(CameraInfo);
			commandList->SetComputeRoot32BitConstants(3, 1, &cameraIndex, 0);


			commandList->SetComputeRootDescriptorTable(8, CD3DX12_GPU_DESCRIPTOR_HANDLE(ResourceManager::GetInstance().GetSRVDescriptorHeap()->GetGPUDescriptorHandleForHeapStart(), uavShaderVisibleInfo.index, context.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)));

			commandList->Dispatch(numThreadGroups, 1, 1);

			D3D12_RESOURCE_BARRIER uavBarrier = {};
			uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
			uavBarrier.UAV.pResource = apiResource;
			uavBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			commandList->ResourceBarrier(1, &uavBarrier);

			if (m_drawShadows) {
				//  Then, process buffer for each light
				for (auto& lightPair : context.currentScene->GetLightIDMap()) {
					auto& light = lightPair.second;
					auto& lightViews = light->GetCameraBufferViews();
					int i = 0;
					for (auto& buffer : light->GetPerViewOpaqueIndirectCommandBuffers()) {
						bufferIndices[3] = buffer->GetResource()->GetUAVShaderVisibleInfo().index;
						commandList->SetComputeRoot32BitConstants(6, 1, &bufferIndices[3], 3);
						unsigned int lightCameraIndex = lightViews[i]->GetOffset() / sizeof(CameraInfo);
						commandList->SetComputeRoot32BitConstants(3, 1, &lightCameraIndex, 0);
						i++;

						commandList->SetComputeRootDescriptorTable(8, buffer->GetResource()->GetUAVShaderVisibleInfo().gpuHandle);

						commandList->Dispatch(numThreadGroups, 1, 1);

						auto barrier = CD3DX12_RESOURCE_BARRIER::UAV(buffer->GetResource()->GetAPIResource());
						commandList->ResourceBarrier(1, &barrier);
					}
				}
			}
		}
		// alpha test buffer
		auto numAlphaTestDraws = context.currentScene->GetNumAlphaTestDraws();
		if (numAlphaTestDraws > 0) {
			unsigned int numThreadGroups = std::ceil(numAlphaTestDraws / 64.0);
			auto resource = context.currentScene->GetPrimaryCameraAlphaTestIndirectCommandBuffer()->GetResource();
			auto apiResource = resource->GetAPIResource();
			auto& uavShaderVisibleInfo = resource->GetUAVShaderVisibleInfo();
			unsigned int bufferIndices[5] = {};
			bufferIndices[0] = meshManager->GetAlphaTestPerMeshBufferSRVIndex();
			bufferIndices[1] = objectManager->GetAlphaTestDrawSetCommandsBufferSRVIndex();
			bufferIndices[2] = objectManager->GetActiveAlphaTestDrawSetIndicesBufferSRVIndex();
			bufferIndices[3] = context.currentScene->GetPrimaryCameraAlphaTestIndirectCommandBuffer()->GetResource()->GetUAVShaderVisibleInfo().index;
			bufferIndices[4] = context.currentScene->GetNumAlphaTestDraws()-1;

			commandList->SetComputeRoot32BitConstants(6, 5, bufferIndices, 0);
			unsigned int cameraIndex = context.currentScene->GetCamera()->GetCameraBufferView()->GetOffset() / sizeof(CameraInfo);
			commandList->SetComputeRoot32BitConstants(3, 1, &cameraIndex, 0);

			commandList->SetComputeRootDescriptorTable(8, uavShaderVisibleInfo.gpuHandle);
		
			commandList->Dispatch(numThreadGroups, 1, 1);

			auto barrier = CD3DX12_RESOURCE_BARRIER::UAV(apiResource);
			commandList->ResourceBarrier(1, &barrier);

			if (m_drawShadows) {
				for (auto& lightPair : context.currentScene->GetLightIDMap()) {
					auto& light = lightPair.second;
					auto& lightViews = light->GetCameraBufferViews();
					int i = 0;
					for (auto& buffer : light->GetPerViewAlphaTestIndirectCommandBuffers()) {
						bufferIndices[3] = buffer->GetResource()->GetUAVShaderVisibleInfo().index;
						commandList->SetComputeRoot32BitConstants(6, 1, &bufferIndices[3], 3);
						unsigned int lightCameraIndex = lightViews[i]->GetOffset() / sizeof(CameraInfo);
						commandList->SetComputeRoot32BitConstants(3, 1, &lightCameraIndex, 0);
						i++;

						commandList->SetComputeRootDescriptorTable(8, buffer->GetResource()->GetUAVShaderVisibleInfo().gpuHandle);

						commandList->Dispatch(numThreadGroups, 1, 1);

						barrier = CD3DX12_RESOURCE_BARRIER::UAV(buffer->GetResource()->GetAPIResource());
						commandList->ResourceBarrier(1, &barrier);
					}
				}
			}
		}

		// blend buffer
		auto numBlendDraws = context.currentScene->GetNumBlendDraws();
		if (numBlendDraws > 0) {
			unsigned int numThreadGroups = std::ceil(numBlendDraws / 64.0);
			auto resource = context.currentScene->GetPrimaryCameraBlendIndirectCommandBuffer()->GetResource();
			auto apiResource = resource->GetAPIResource();
			auto& uavShaderVisibleInfo = resource->GetUAVShaderVisibleInfo();
			unsigned int bufferIndices[5] = {};
			bufferIndices[0] = meshManager->GetBlendPerMeshBufferSRVIndex();
			bufferIndices[1] = objectManager->GetBlendDrawSetCommandsBufferSRVIndex();
			bufferIndices[2] = objectManager->GetActiveBlendDrawSetIndicesBufferSRVIndex();
			bufferIndices[3] = context.currentScene->GetPrimaryCameraBlendIndirectCommandBuffer()->GetResource()->GetUAVShaderVisibleInfo().index;
			bufferIndices[4] = context.currentScene->GetNumBlendDraws() - 1;

			commandList->SetComputeRoot32BitConstants(6, 5, bufferIndices, 0);
			unsigned int cameraIndex = context.currentScene->GetCamera()->GetCameraBufferView()->GetOffset() / sizeof(CameraInfo);
			commandList->SetComputeRoot32BitConstants(3, 1, &cameraIndex, 0);

			commandList->SetComputeRootDescriptorTable(8, uavShaderVisibleInfo.gpuHandle);

			commandList->Dispatch(numThreadGroups, 1, 1);

			auto barrier = CD3DX12_RESOURCE_BARRIER::UAV(apiResource);
			commandList->ResourceBarrier(1, &barrier);

			if (m_drawShadows) {
				for (auto& lightPair : context.currentScene->GetLightIDMap()) {
					auto& light = lightPair.second;
					auto& lightViews = light->GetCameraBufferViews();
					int i = 0;
					for (auto& buffer : light->GetPerViewBlendIndirectCommandBuffers()) {
						bufferIndices[3] = buffer->GetResource()->GetUAVShaderVisibleInfo().index;
						commandList->SetComputeRoot32BitConstants(6, 1, &bufferIndices[3], 3);
						unsigned int lightCameraIndex = lightViews[i]->GetOffset() / sizeof(CameraInfo);
						commandList->SetComputeRoot32BitConstants(3, 1, &lightCameraIndex, 0);
						i++;

						commandList->SetComputeRootDescriptorTable(8, buffer->GetResource()->GetUAVShaderVisibleInfo().gpuHandle);

						commandList->Dispatch(numThreadGroups, 1, 1);

						barrier = CD3DX12_RESOURCE_BARRIER::UAV(buffer->GetResource()->GetAPIResource());
						commandList->ResourceBarrier(1, &barrier);
					}
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

	void CreateRootSignature() {
		D3D12_ROOT_PARAMETER1 parameters[9] = {};

		// PerObject buffer index
		parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
		parameters[0].Constants.ShaderRegister = 1;
		parameters[0].Constants.RegisterSpace = 0;
		parameters[0].Constants.Num32BitValues = 1;
		parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

		// PerMesh buffer index
		parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
		parameters[1].Constants.ShaderRegister = 2;
		parameters[1].Constants.RegisterSpace = 0;
		parameters[1].Constants.Num32BitValues = 1;
		parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

		// First integer root constant, used for shadow light ID
		parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
		parameters[2].Constants.ShaderRegister = 3; // b3 for first integer root constant
		parameters[2].Constants.RegisterSpace = 0;
		parameters[2].Constants.Num32BitValues = 1;
		parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

		// Second integer root constant, used for shadow light view offset
		parameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
		parameters[3].Constants.ShaderRegister = 4; // b4 for second integer root constant
		parameters[3].Constants.RegisterSpace = 0;
		parameters[3].Constants.Num32BitValues = 1; // Single integer
		parameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

		// Third integer root constant, used for settings
		parameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
		parameters[4].Constants.ShaderRegister = 5;
		parameters[4].Constants.RegisterSpace = 0;
		parameters[4].Constants.Num32BitValues = 2;
		parameters[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

		// Static buffer indices
		parameters[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
		parameters[5].Constants.ShaderRegister = 6;
		parameters[5].Constants.RegisterSpace = 0;
		parameters[5].Constants.Num32BitValues = 6;
		parameters[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

		// Variable buffer indices
		parameters[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
		parameters[6].Constants.ShaderRegister = 7;
		parameters[6].Constants.RegisterSpace = 0;
		parameters[6].Constants.Num32BitValues = 5;
		parameters[6].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

		// transparency info
		parameters[7].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
		parameters[7].Constants.ShaderRegister = 8;
		parameters[7].Constants.RegisterSpace = 0;
		parameters[7].Constants.Num32BitValues = 4;
		parameters[7].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

		parameters[8].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		parameters[8].DescriptorTable.NumDescriptorRanges = 1;
		auto range = CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE, 0);
		parameters[8].DescriptorTable.pDescriptorRanges = &range;

		// Root Signature Description
		D3D12_ROOT_SIGNATURE_DESC1 rootSignatureDesc = {};
		rootSignatureDesc.NumParameters = _countof(parameters);
		rootSignatureDesc.pParameters = parameters;
		rootSignatureDesc.NumStaticSamplers = 0;
		rootSignatureDesc.pStaticSamplers = nullptr;
		rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT | D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED | D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED;

		// Serialize and create the root signature
		ComPtr<ID3DBlob> signature;
		ComPtr<ID3DBlob> error;
		D3D12_VERSIONED_ROOT_SIGNATURE_DESC versionedDesc;
		versionedDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
		versionedDesc.Desc_1_1 = rootSignatureDesc;
		ThrowIfFailed(D3D12SerializeVersionedRootSignature(&versionedDesc, &signature, &error));
		auto& device = DeviceManager::GetInstance().GetDevice();
		ThrowIfFailed(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)));
	}

	void CreatePSO() {
		// Compile the compute shader
		Microsoft::WRL::ComPtr<ID3DBlob> computeShader;
		PSOManager::GetInstance().CompileShader(L"shaders/frustrumCulling.hlsl", L"CSMain", L"cs_6_6", {}, computeShader);

		struct PipelineStateStream {
			CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE RootSignature;
			CD3DX12_PIPELINE_STATE_STREAM_CS CS;
		};

		PipelineStateStream pipelineStateStream = {};
		pipelineStateStream.RootSignature = m_rootSignature.Get();
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
	ComPtr<ID3D12RootSignature> m_rootSignature;

	bool m_drawShadows = false;

	std::function<uint8_t()> getNumDirectionalLightCascades;

};