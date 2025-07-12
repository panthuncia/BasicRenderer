#pragma once

#include <DirectX/d3dx12.h>

#include "RenderPasses/Base/ComputePass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Utilities/Utilities.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Managers/MeshManager.h"
#include "Managers/ObjectManager.h"
#include "Managers/Singletons/ECSManager.h"

class ClusterGenerationPass : public ComputePass {
public:
	ClusterGenerationPass() {
		getClusterSize = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT3>("lightClusterSize");
	}

	~ClusterGenerationPass() {
	}

	void DeclareResourceUsages(ComputePassBuilder* builder) {
		builder->WithShaderResource(Builtin::CameraBuffer)
			.WithUnorderedAccess(Builtin::Light::ClusterBuffer);
	}

	void Setup() override {
		auto& ecsWorld = ECSManager::GetInstance().GetWorld();
		CreatePSO();

		RegisterSRV(Builtin::CameraBuffer);
		RegisterUAV(Builtin::Light::ClusterBuffer);
	}

	PassReturn Execute(RenderContext& context) override {
		auto& commandList = context.commandList;

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

		BindResourceDescriptorIndices(commandList, m_resourceDescriptorBindings);

		auto clusterSize = getClusterSize();
		commandList->Dispatch(clusterSize.x, clusterSize.y, clusterSize.z);
		return {};
	}

	void Cleanup(RenderContext& context) override {

	}

private:
	PipelineResources m_resourceDescriptorBindings;

	void CreatePSO() {
		// Compile the compute shader
		Microsoft::WRL::ComPtr<ID3DBlob> computeShader;
		//PSOManager::GetInstance().CompileShader(L"shaders/clustering.hlsl", L"CSMain", L"cs_6_6", {}, computeShader);
		ShaderInfoBundle shaderInfoBundle;
		shaderInfoBundle.computeShader = { L"shaders/clustering.hlsl", L"CSMain", L"cs_6_6" };
		auto compiledBundle = PSOManager::GetInstance().CompileShaders(shaderInfoBundle);
		computeShader = compiledBundle.computeShader;
		m_resourceDescriptorBindings = compiledBundle.resourceDescriptorSlots;

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

	std::function<DirectX::XMUINT3()> getClusterSize;
	ComPtr<ID3D12PipelineState> m_PSO;
};