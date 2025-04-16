#pragma once

#include <DirectX/d3dx12.h>

#include "ComputePass.h"
#include "PSOManager.h"
#include "RenderContext.h"
#include "DeviceManager.h"
#include "utilities.h"
#include "SettingsManager.h"
#include "MeshManager.h"
#include "ObjectManager.h"
#include "ECSManager.h"

class LightCullingPass : public ComputePass {
public:
	LightCullingPass(std::shared_ptr<GloballyIndexedResource> pClusterBuffer, std::shared_ptr<Buffer> pLightPagesBuffer, std::shared_ptr<Buffer> pLightPagesCounter) : m_pClusterBuffer(pClusterBuffer), m_pLightPagesBuffer(pLightPagesBuffer), m_pLightPagesCounter(pLightPagesCounter) {
		getClusterSize = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT3>("lightClusterSize");
	}

	~LightCullingPass() {
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
		auto& ecsWorld = ECSManager::GetInstance().GetWorld();
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

		auto rootSignature = PSOManager::GetInstance().GetComputeRootSignature().Get();
		commandList->SetComputeRootSignature(rootSignature);

		// Set the compute pipeline state
		commandList->SetPipelineState(m_PSO.Get());

		auto& meshManager = context.meshManager;
		auto& objectManager = context.objectManager;
		auto& cameraManager = context.cameraManager;

		unsigned int staticBufferIndices[NumStaticBufferRootConstants] = {};
		staticBufferIndices[CameraBufferDescriptorIndex] = cameraManager->GetCameraBufferSRVIndex();
		commandList->SetComputeRoot32BitConstants(StaticBufferRootSignatureIndex, NumStaticBufferRootConstants, staticBufferIndices, 0);

		unsigned int lightClusterConstants[NumLightClusterRootConstants] = {};
		lightClusterConstants[LightClusterBufferDescriptorIndex] = m_pClusterBuffer->GetUAVShaderVisibleInfo().index;
		lightClusterConstants[LightPagesBufferDescriptorIndex] = m_pLightPagesBuffer->GetUAVShaderVisibleInfo().index;
		lightClusterConstants[LightPagesCounterDescriptorIndex] = m_pLightPagesCounter->GetUAVShaderVisibleInfo().index;
		lightClusterConstants[LightPagesPoolSize] = context.lightManager->GetLightPagePoolSize();
		commandList->SetComputeRoot32BitConstants(LightClusterRootSignatureIndex, NumLightClusterRootConstants, lightClusterConstants, 0);

		auto clusterSize = getClusterSize();
		unsigned int numThreadGroups = std::ceil(((float)(clusterSize.x * clusterSize.y * clusterSize.z)) / 128);
		commandList->Dispatch(numThreadGroups, 1, 1);

		ThrowIfFailed(commandList->Close());

		//invalidated = false;

		return { { commandList.Get()} };
	}

	void Cleanup(RenderContext& context) override {

	}

	virtual void Update() override {
		// Reset UAV counter
		uint32_t zero = 0;
		UploadManager::GetInstance().UploadData(&zero, sizeof(uint32_t), m_pLightPagesCounter.get(), 0);
	}

private:

	void CreatePSO() {
		// Compile the compute shader
		Microsoft::WRL::ComPtr<ID3DBlob> computeShader;
		PSOManager::GetInstance().CompileShader(L"shaders/lightCulling.hlsl", L"CSMain", L"cs_6_6", {}, computeShader);

		struct PipelineStateStream {
			CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE RootSignature;
			CD3DX12_PIPELINE_STATE_STREAM_CS CS;
		};

		PipelineStateStream pipelineStateStream = {};
		pipelineStateStream.RootSignature = PSOManager::GetInstance().GetComputeRootSignature().Get();
		pipelineStateStream.CS = CD3DX12_SHADER_BYTECODE(computeShader.Get());

		D3D12_PIPELINE_STATE_STREAM_DESC streamDesc = {};
		streamDesc.SizeInBytes = sizeof(PipelineStateStream);
		streamDesc.pPipelineStateSubobjectStream = &pipelineStateStream;

		auto& device = DeviceManager::GetInstance().GetDevice();
		ID3D12Device2* device2 = nullptr;
		ThrowIfFailed(device->QueryInterface(IID_PPV_ARGS(&device2)));
		ThrowIfFailed(device2->CreatePipelineState(&streamDesc, IID_PPV_ARGS(&m_PSO)));
	}

	std::shared_ptr<Buffer> m_pLightPagesBuffer;
	std::shared_ptr<Buffer> m_pLightPagesCounter;
	std::function<DirectX::XMUINT3()> getClusterSize;
	std::shared_ptr<GloballyIndexedResource> m_pClusterBuffer;
	std::vector<ComPtr<ID3D12GraphicsCommandList7>> m_commandLists;
	std::vector<ComPtr<ID3D12CommandAllocator>> m_allocators;
	ComPtr<ID3D12PipelineState> m_PSO;
};