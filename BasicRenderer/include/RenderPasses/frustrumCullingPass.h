#pragma once

#include <DirectX/d3dx12.h>

#include "RenderPass.h"
#include "PSOManager.h"
#include "RenderContext.h"
#include "DeviceManager.h"
#include "utilities.h"

class FrustrumCullingPass : public RenderPass {
public:
	FrustrumCullingPass(std::shared_ptr<PSOManager> psoManager) : psoManager(psoManager) {}

	void Setup() override {
		auto& manager = DeviceManager::GetInstance();
		auto& device = manager.GetDevice();

		ThrowIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&m_commandAllocator)));
		ThrowIfFailed(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE, m_commandAllocator.Get(), nullptr, IID_PPV_ARGS(&m_commandList)));
		m_commandList->Close();

		CreatePSO();
	}

	std::vector<ID3D12GraphicsCommandList*> Execute(RenderContext& context) override {
		auto commandList = m_commandList.Get();
		ThrowIfFailed(m_commandAllocator->Reset());
		ThrowIfFailed(commandList->Reset(m_commandAllocator.Get(), nullptr));

		auto rootSignature = psoManager->GetRootSignature();
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
		
		// opaque buffer
		unsigned int[2] bufferIndices;
		bufferIndices[0] = objectManager->GetOpaqueDrawSetCommandsBufferSRVIndex();
		bufferIndices[1] = objectManager->GetActiveOpaqueDrawSetIndicesBufferSRVIndex();

		commandList->SetComputeRoot32BitConstants(6, 2, bufferIndices, 1);

		// Dispatch the compute shader
		commandList->Dispatch(1, 1, 1);

		// Close the command list
		ThrowIfFailed(commandList->Close());

		return { commandList };
	}

private:

	void CreatePSO() {
        // Compile the compute shader
        Microsoft::WRL::ComPtr<ID3DBlob> computeShader;
        CompileShader(L"shaders/frustrumCulling.hlsl", L"CSMain", L"cs_6_6", nullptr, computeShader);

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
        ThrowIfFailed(device->CreatePipelineState(&streamDesc, IID_PPV_ARGS(&m_PSO)));
	}

	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_commandList;
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_commandAllocator;
	ComPtr<ID3D12PipelineState> m_PSO;
}
