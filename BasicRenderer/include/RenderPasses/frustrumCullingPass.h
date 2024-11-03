#pragma once

#include <DirectX/d3dx12.h>

#include "RenderPass.h"
#include "PSOManager.h"
#include "RenderContext.h"
#include "DeviceManager.h"
#include "utilities.h"

class FrustrumCullingPass : public RenderPass {
public:
	FrustrumCullingPass() {}

	void Setup() override {
		auto& manager = DeviceManager::GetInstance();
		auto& device = manager.GetDevice();

		ThrowIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocator)));
		ThrowIfFailed(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator.Get(), nullptr, IID_PPV_ARGS(&m_commandList)));
		m_commandList->Close();

		CreatePSO();
	}

	std::vector<ID3D12GraphicsCommandList*> Execute(RenderContext& context) override {
		auto commandList = m_commandList.Get();
		ThrowIfFailed(m_commandAllocator->Reset());
		ThrowIfFailed(commandList->Reset(m_commandAllocator.Get(), nullptr));

		auto rootSignature = PSOManager::getInstance().GetRootSignature();
		commandList->SetComputeRootSignature(rootSignature.Get());

		// Set the descriptor heaps
		ID3D12DescriptorHeap* descriptorHeaps[] = {
			ResourceManager::GetInstance().GetSRVDescriptorHeap().Get(),
			ResourceManager::GetInstance().GetSamplerDescriptorHeap().Get()
		};

		commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

		// Set the compute pipeline state
		commandList->SetPipelineState(m_PSO.Get());

		auto& objectManager = context.currentScene->GetObjectManager();
		auto& meshManager = context.currentScene->GetMeshManager();
		// opaque buffer
		auto resource = context.currentScene->GetPrimaryCameraOpaqueIndirectCommandBuffer()->GetResource();
		auto apiResource = resource->GetAPIResource();
		auto uavShaderVisibleInfo = resource->GetUAVShaderVisibleInfo();
		auto uavNonShaderVisibleInfo = resource->GetUAVNonShaderVisibleInfo();
		unsigned int bufferIndices[4] = {};
		bufferIndices[0] = meshManager->GetOpaquePerMeshBufferSRVIndex();
		bufferIndices[1] = objectManager->GetOpaqueDrawSetCommandsBufferSRVIndex();
		bufferIndices[2] = objectManager->GetActiveOpaqueDrawSetIndicesBufferSRVIndex();
		bufferIndices[3] = context.currentScene->GetPrimaryCameraOpaqueIndirectCommandBuffer()->GetResource()->GetUAVShaderVisibleInfo().index;

		commandList->SetComputeRoot32BitConstants(6, 4, bufferIndices, 0);

		commandList->Dispatch(context.currentScene->GetNumOpaqueDraws(), 1, 1);

		// transparent buffer
		resource = context.currentScene->GetPrimaryCameraTransparentIndirectCommandBuffer()->GetResource();
		apiResource = resource->GetAPIResource();
		uavShaderVisibleInfo = resource->GetUAVShaderVisibleInfo();
		uavNonShaderVisibleInfo = resource->GetUAVNonShaderVisibleInfo();
		bufferIndices[0] = meshManager->GetTransparentPerMeshBufferSRVIndex();
		bufferIndices[1] = objectManager->GetTransparentDrawSetCommandsBufferSRVIndex();
		bufferIndices[2] = objectManager->GetActiveTransparentDrawSetIndicesBufferSRVIndex();
		bufferIndices[3] = context.currentScene->GetPrimaryCameraTransparentIndirectCommandBuffer()->GetResource()->GetUAVShaderVisibleInfo().index;

		commandList->SetComputeRoot32BitConstants(6, 4, bufferIndices, 0);

		commandList->Dispatch(context.currentScene->GetNumTransparentDraws(), 1, 1);

		// Close the command list
		ThrowIfFailed(commandList->Close());

		return { commandList };
	}

	void Cleanup(RenderContext& context) override {

	}

private:

	void CreatePSO() {
		// Compile the compute shader
		Microsoft::WRL::ComPtr<ID3DBlob> computeShader;
		PSOManager::getInstance().CompileShader(L"shaders/frustrumCulling.hlsl", L"CSMain", L"cs_6_6", {}, computeShader);

		// Define the pipeline state stream subobjects for compute
		struct PipelineStateStream {
			CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE RootSignature;
			CD3DX12_PIPELINE_STATE_STREAM_CS CS;
		};

		PipelineStateStream pipelineStateStream = {};
		pipelineStateStream.RootSignature = PSOManager::getInstance().GetRootSignature().Get();
		pipelineStateStream.CS = CD3DX12_SHADER_BYTECODE(computeShader.Get());

		// Create the pipeline state stream descriptor
		D3D12_PIPELINE_STATE_STREAM_DESC streamDesc = {};
		streamDesc.SizeInBytes = sizeof(PipelineStateStream);
		streamDesc.pPipelineStateSubobjectStream = &pipelineStateStream;

		// Create the pipeline state
		auto& device = DeviceManager::GetInstance().GetDevice();
		ID3D12Device2* device2 = nullptr;
		ThrowIfFailed(device->QueryInterface(IID_PPV_ARGS(&device2)));
		ThrowIfFailed(device2->CreatePipelineState(&streamDesc, IID_PPV_ARGS(&m_PSO)));
	}

	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_commandList;
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_commandAllocator;
	ComPtr<ID3D12PipelineState> m_PSO;
};