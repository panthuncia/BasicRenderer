#pragma once

#include "RenderPasses/Base/ComputePass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "Resources/Texture.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Managers/Singletons/UploadManager.h"
#include "ThirdParty/XeGTAO.h"

class GTAOMainPass : public ComputePass {
public:
    GTAOMainPass(std::shared_ptr<GloballyIndexedResource> pGTAOConstantBuffer) : m_pGTAOConstantBuffer(pGTAOConstantBuffer) {}

    void Setup(const ResourceRegistryView& resourceRegistryView) override {
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
		CreateXeGTAOComputePSO();
    }

    PassReturn Execute(RenderContext& context) override {
        frameIndex++;
        auto& psoManager = PSOManager::GetInstance();
        auto& commandList = context.commandList;

        ID3D12DescriptorHeap* descriptorHeaps[] = {
            context.textureDescriptorHeap, // The texture descriptor heap
            context.samplerDescriptorHeap, // The sampler descriptor heap
        };
        commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

		commandList->SetComputeRootSignature(psoManager.GetRootSignature().Get());
		commandList->SetPipelineState(GTAOHighPSO.Get());

		unsigned int staticBufferIndices[NumStaticBufferRootConstants] = {};
        staticBufferIndices[CameraBufferDescriptorIndex] = context.cameraManager->GetCameraBufferSRVIndex();

		commandList->SetComputeRoot32BitConstants(StaticBufferRootSignatureIndex, NumStaticBufferRootConstants, staticBufferIndices, 0);

        unsigned int passConstants[NumMiscUintRootConstants] = {};
        passConstants[0] = m_pGTAOConstantBuffer->GetCBVInfo().index;
		passConstants[1] = frameIndex % 64; // For spatiotemporal denoising

        commandList->SetComputeRoot32BitConstants(MiscUintRootSignatureIndex, NumMiscUintRootConstants, passConstants, 0);

        commandList->Dispatch((context.xRes + (XE_GTAO_NUMTHREADS_X * 2) - 1) / (XE_GTAO_NUMTHREADS_X), (context.yRes + XE_GTAO_NUMTHREADS_Y - 1) / XE_GTAO_NUMTHREADS_Y, 1);
        return {};
    }

    void Cleanup(RenderContext& context) override {
        // Cleanup if necessary
    }

private:
    std::shared_ptr<GloballyIndexedResource> m_pGTAOConstantBuffer;

    std::vector<ComPtr<ID3D12GraphicsCommandList7>> m_commandLists;
    std::vector<ComPtr<ID3D12CommandAllocator>> m_allocators;

    ComPtr<ID3D12RootSignature> gtaoRootSignature;

    ComPtr<ID3D12PipelineState> PrefilterDepths16x16PSO;
    ComPtr<ID3D12PipelineState> GTAOLowPSO;
    ComPtr<ID3D12PipelineState> GTAOMediumPSO;
    ComPtr<ID3D12PipelineState> GTAOHighPSO;
    ComPtr<ID3D12PipelineState> GTAOUltraPSO;
    ComPtr<ID3D12PipelineState> DenoisePassPSO;
    ComPtr<ID3D12PipelineState> DenoiseLastPassPSO;
    ComPtr<ID3D12PipelineState> GenerateNormalsPSO;

    uint64_t frameIndex = 0;

    void CreateXeGTAOComputePSO() {
        auto device = DeviceManager::GetInstance().GetDevice();

        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature = PSOManager::GetInstance().GetRootSignature().Get();
        psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
        
		Microsoft::WRL::ComPtr<ID3DBlob> CSGTAOLow;
		PSOManager::GetInstance().CompileShader(L"shaders/GTAO.hlsl", L"CSGTAOLow", L"cs_6_6", {}, CSGTAOLow);
		psoDesc.CS = CD3DX12_SHADER_BYTECODE(CSGTAOLow.Get());
		ThrowIfFailed(device->CreateComputePipelineState(
			&psoDesc, IID_PPV_ARGS(&GTAOLowPSO)));
		Microsoft::WRL::ComPtr<ID3DBlob> CSGTAOMedium;
		PSOManager::GetInstance().CompileShader(L"shaders/GTAO.hlsl", L"CSGTAOMedium", L"cs_6_6", {}, CSGTAOMedium);
		psoDesc.CS = CD3DX12_SHADER_BYTECODE(CSGTAOMedium.Get());
		ThrowIfFailed(device->CreateComputePipelineState(
			&psoDesc, IID_PPV_ARGS(&GTAOMediumPSO)));
		Microsoft::WRL::ComPtr<ID3DBlob> CSGTAOHigh;
		PSOManager::GetInstance().CompileShader(L"shaders/GTAO.hlsl", L"CSGTAOHigh", L"cs_6_6", {}, CSGTAOHigh);
		psoDesc.CS = CD3DX12_SHADER_BYTECODE(CSGTAOHigh.Get());
		ThrowIfFailed(device->CreateComputePipelineState(
			&psoDesc, IID_PPV_ARGS(&GTAOHighPSO)));
		Microsoft::WRL::ComPtr<ID3DBlob> CSGTAOUltra;
		PSOManager::GetInstance().CompileShader(L"shaders/GTAO.hlsl", L"CSGTAOUltra", L"cs_6_6", {}, CSGTAOUltra);
		psoDesc.CS = CD3DX12_SHADER_BYTECODE(CSGTAOUltra.Get());
		ThrowIfFailed(device->CreateComputePipelineState(
			&psoDesc, IID_PPV_ARGS(&GTAOUltraPSO)));
    }
};
