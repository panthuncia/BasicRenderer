#pragma once

#include <DirectX/d3dx12.h>

#include "ComputePass.h"
#include "PSOManager.h"
#include "RenderContext.h"
#include "DeviceManager.h"
#include "utilities.h"
#include "SettingsManager.h"

class SkinningPass : public ComputePass {
public:
	SkinningPass() {
	}

	void Setup() override {
		auto& manager = DeviceManager::GetInstance();
		auto& device = manager.GetDevice();
		uint8_t numFramesInFlight = SettingsManager::GetInstance().getSettingGetter<uint8_t>("numFramesInFlight")();

		for (int i = 0; i < numFramesInFlight; i++) {
			ComPtr<ID3D12CommandAllocator> allocator;
			ComPtr<ID3D12GraphicsCommandList7> commandList;
			ThrowIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&allocator)));
			ThrowIfFailed(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE, allocator.Get(), nullptr, IID_PPV_ARGS(&commandList)));
			commandList->Close();
			m_allocators.push_back(allocator);
			m_commandLists.push_back(commandList);
		}
		CreatePSO();
	}

	ComputePassReturn Execute(RenderContext& context) override {
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

		auto rootSignature = PSOManager::GetInstance().GetRootSignature().Get();
		commandList->SetComputeRootSignature(rootSignature);

		// Set the compute pipeline state
		commandList->SetPipelineState(m_PSO.Get());

		auto& meshManager = context.currentScene->GetMeshManager();
		auto& objectManager = context.currentScene->GetObjectManager();

		unsigned int staticBufferIndices[NumStaticBufferRootConstants] = {};
		staticBufferIndices[PreSkinningVertexBufferDescriptorIndex] = meshManager->GetPreSkinningVertexBufferSRVIndex();
		staticBufferIndices[NormalMatrixBufferDescriptorIndex] = objectManager->GetNormalMatrixBufferSRVIndex();
		staticBufferIndices[PostSkinningVertexBufferDescriptorIndex] = meshManager->GetPostSkinningVertexBufferUAVIndex();
		staticBufferIndices[PerObjectBufferDescriptorIndex] = objectManager->GetPerObjectBufferSRVIndex();

		commandList->SetComputeRoot32BitConstants(StaticBufferRootSignatureIndex, NumStaticBufferRootConstants, staticBufferIndices, 0);

		auto opaquePerMeshBufferIndex = meshManager->GetOpaquePerMeshBufferSRVIndex();
		commandList->SetComputeRoot32BitConstants(VariableBufferRootSignatureIndex, 1, &opaquePerMeshBufferIndex, PerMeshBufferDescriptorIndex);
		for (auto& pair : context.currentScene->GetOpaqueSkinnedRenderableObjectIDMap()) {
			auto& renderable = pair.second;
			auto& meshes = renderable->GetOpaqueMeshes();

			auto perObjectIndex = renderable->GetCurrentPerObjectCBView()->GetOffset() / sizeof(PerObjectCB);
			commandList->SetComputeRoot32BitConstants(PerObjectRootSignatureIndex, 1, &perObjectIndex, PerObjectBufferIndex);

			for (auto& pMesh : meshes) {
				auto& mesh = *pMesh;
				auto perMeshIndex = mesh.GetPerMeshBufferView()->GetOffset() / sizeof(PerMeshCB);
				commandList->SetComputeRoot32BitConstants(PerMeshRootSignatureIndex, 1, &perMeshIndex, PerMeshBufferIndex);

				commandList->Dispatch(mesh.GetNumVertices(), 1, 1);
			}
		}

		auto alphaTestPerMeshBufferIndex = meshManager->GetAlphaTestPerMeshBufferSRVIndex();
		commandList->SetComputeRoot32BitConstants(VariableBufferRootSignatureIndex, 1, &alphaTestPerMeshBufferIndex, PerMeshBufferDescriptorIndex);
		for (auto& pair : context.currentScene->GetAlphaTestSkinnedRenderableObjectIDMap()) {
			auto& renderable = pair.second;
			auto& meshes = renderable->GetAlphaTestMeshes();

			auto perObjectIndex = renderable->GetCurrentPerObjectCBView()->GetOffset() / sizeof(PerObjectCB);
			commandList->SetComputeRoot32BitConstants(PerObjectRootSignatureIndex, 1, &perObjectIndex, PerObjectBufferIndex);

			for (auto& pMesh : meshes) {
				auto& mesh = *pMesh;
				auto perMeshIndex = mesh.GetPerMeshBufferView()->GetOffset() / sizeof(PerMeshCB);
				commandList->SetComputeRoot32BitConstants(PerMeshRootSignatureIndex, 1, &perMeshIndex, PerMeshBufferIndex);

				commandList->Dispatch(mesh.GetNumVertices(), 1, 1);
			}
		}

		auto blendPerMeshBufferIndex = meshManager->GetBlendPerMeshBufferSRVIndex();
		commandList->SetComputeRoot32BitConstants(VariableBufferRootSignatureIndex, 1, &blendPerMeshBufferIndex, PerMeshBufferDescriptorIndex);
		for (auto& pair : context.currentScene->GetBlendSkinnedRenderableObjectIDMap()) {
			auto& renderable = pair.second;
			auto& meshes = renderable->GetBlendMeshes();

			auto perObjectIndex = renderable->GetCurrentPerObjectCBView()->GetOffset() / sizeof(PerObjectCB);
			commandList->SetComputeRoot32BitConstants(PerObjectRootSignatureIndex, 1, &perObjectIndex, PerObjectBufferIndex);

			for (auto& pMesh : meshes) {
				auto& mesh = *pMesh;
				auto perMeshIndex = mesh.GetPerMeshBufferView()->GetOffset() / sizeof(PerMeshCB);
				commandList->SetComputeRoot32BitConstants(PerMeshRootSignatureIndex, 1, &perMeshIndex, PerMeshBufferIndex);

				commandList->Dispatch(mesh.GetNumVertices(), 1, 1);
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
		PSOManager::GetInstance().CompileShader(L"shaders/skinning.hlsl", L"CSMain", L"cs_6_6", {}, computeShader);

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
};