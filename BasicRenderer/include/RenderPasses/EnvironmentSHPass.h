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
#include "Managers/EnvironmentManager.h"
#include "Managers/Singletons/ResourceManager.h"
class EnvironmentSHPass : public ComputePass {
public:
	EnvironmentSHPass() {
		D3D12_SAMPLER_DESC shSamplerDesc = {};
		shSamplerDesc.Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
		shSamplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		shSamplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		shSamplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		shSamplerDesc.MipLODBias = 0.0f;
		shSamplerDesc.MaxAnisotropy = 1;
		shSamplerDesc.BorderColor[0] = 0;
		shSamplerDesc.BorderColor[1] = 0;
		shSamplerDesc.BorderColor[2] = 0;
		shSamplerDesc.BorderColor[3] = 0;
		shSamplerDesc.MinLOD = 0.0f;
		shSamplerDesc.MaxLOD = D3D12_FLOAT32_MAX;

		m_samplerIndex = ResourceManager::GetInstance().CreateIndexedSampler(shSamplerDesc);
	}

	~EnvironmentSHPass() {
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

		auto rootSignature = PSOManager::GetInstance().GetComputeRootSignature().Get();
		commandList->SetComputeRootSignature(rootSignature);

		// Set the compute pipeline state
		commandList->SetPipelineState(m_PSO.Get());

		// Root parameters
		unsigned int miscParams[NumMiscUintRootConstants] = { };
		miscParams[UintRootConstant1] = m_samplerIndex; // Sampler index
		miscParams[UintRootConstant2] = context.environmentManager->GetEnvironmentBufferUAVDescriptorIndex();

		float miscFloatParams[NumMiscFloatRootConstants] = { };

		auto environments = context.environmentManager->GetAndClearEnvironmentsToPrefilter();
		for (auto& env : environments) {
			auto cubemapRes = env->GetReflectionCubemapResolution();
			miscParams[UintRootConstant0] = cubemapRes; // Resolution
			miscParams[UintRootConstant3] = env->GetEnvironmentIndex(); // Environment index

			//miscFloatParams[FloatRootConstant0] =  (4.0f * XM_PI / (cubemapRes * cubemapRes * 6)); // Weight

			commandList->SetComputeRoot32BitConstants(MiscUintRootSignatureIndex, NumMiscUintRootConstants, miscParams, 0);
			commandList->SetComputeRoot32BitConstants(MiscFloatRootSignatureIndex, NumMiscFloatRootConstants, miscFloatParams, 0);

			// dispatch over X×Y tiles, Z=6 faces
			unsigned int groupsX = (cubemapRes + 15) / 16;
			unsigned int groupsY = (cubemapRes + 15) / 16;
			unsigned int groupsZ = 6;
			commandList->Dispatch(groupsX, groupsY, groupsZ);
		}

		ThrowIfFailed(commandList->Close());

		return { { commandList.Get()} };
	}

	void Cleanup(RenderContext& context) override {

	}

private:

	void CreatePSO() {
		// Compile the compute shader
		Microsoft::WRL::ComPtr<ID3DBlob> computeShader;
		PSOManager::GetInstance().CompileShader(L"shaders/SphericalHarmonics.hlsl", L"CSMain", L"cs_6_6", {}, computeShader);

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

	unsigned int m_samplerIndex = 0;
	std::vector<ComPtr<ID3D12GraphicsCommandList7>> m_commandLists;
	std::vector<ComPtr<ID3D12CommandAllocator>> m_allocators;
	ComPtr<ID3D12PipelineState> m_PSO;
};