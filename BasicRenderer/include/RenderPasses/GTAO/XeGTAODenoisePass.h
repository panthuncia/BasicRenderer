#pragma once

#include "RenderPasses/Base/ComputePass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "Resources/Texture.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Managers/Singletons/UploadManager.h"

class GTAODenoisePass : public ComputePass {
public:
    GTAODenoisePass(std::shared_ptr<GloballyIndexedResource> pGTAOConstantBuffer, int workingBufferIndex) : m_pGTAOConstantBuffer(pGTAOConstantBuffer), m_workingAOBufferIndex(workingBufferIndex) {}

    void Setup(const ResourceRegistryView& resourceRegistryView) override {
		CreateXeGTAOComputePSO();
    }

    void DeclareResourceUsages(ComputePassBuilder* builder) {
        builder->WithShaderResource(Builtin::GTAO::WorkingEdges, Builtin::GTAO::WorkingAOTerm1)
            .WithUnorderedAccess(Builtin::GTAO::OutputAOTerm);
    }

    PassReturn Execute(RenderContext& context) override {

        auto& psoManager = PSOManager::GetInstance();
        auto& commandList = context.commandList;

        ID3D12DescriptorHeap* descriptorHeaps[] = {
            context.textureDescriptorHeap, // The texture descriptor heap
            context.samplerDescriptorHeap, // The sampler descriptor heap
        };
        commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

		// Set the root signature
		commandList->SetComputeRootSignature(psoManager.GetRootSignature().Get());
		commandList->SetPipelineState(DenoiseLastPassPSO.Get());

        unsigned int gtaoConstants[NumMiscUintRootConstants] = {};
        gtaoConstants[UintRootConstant0] = m_pGTAOConstantBuffer->GetCBVInfo().index;
        gtaoConstants[UintRootConstant1] = m_workingAOBufferIndex;
            
        commandList->SetComputeRoot32BitConstants(MiscUintRootSignatureIndex, NumMiscUintRootConstants, gtaoConstants, 0);

        commandList->Dispatch((context.xRes + (XE_GTAO_NUMTHREADS_X*2)-1) / (XE_GTAO_NUMTHREADS_X*2), (context.yRes + XE_GTAO_NUMTHREADS_Y-1) / XE_GTAO_NUMTHREADS_Y, 1 );
    
        return {};
    }

    void Cleanup(RenderContext& context) override {
        // Cleanup if necessary
    }

private:
    std::shared_ptr<GloballyIndexedResource> m_pGTAOConstantBuffer;

    ComPtr<ID3D12PipelineState> DenoisePassPSO;
    ComPtr<ID3D12PipelineState> DenoiseLastPassPSO;

	unsigned int m_workingAOBufferIndex = 0;

    void CreateXeGTAOComputePSO()
    {
        auto device = DeviceManager::GetInstance().GetDevice();

        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature = PSOManager::GetInstance().GetRootSignature().Get();
        psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
       
		Microsoft::WRL::ComPtr<ID3DBlob> CSDenoisePass;
		PSOManager::GetInstance().CompileShader(L"shaders/GTAO.hlsl", L"CSDenoisePass", L"cs_6_6", {}, CSDenoisePass);
		psoDesc.CS = CD3DX12_SHADER_BYTECODE(CSDenoisePass.Get());
		ThrowIfFailed(device->CreateComputePipelineState(
			&psoDesc, IID_PPV_ARGS(&DenoisePassPSO)));
		Microsoft::WRL::ComPtr<ID3DBlob> CSDenoiseLastPass;
		PSOManager::GetInstance().CompileShader(L"shaders/GTAO.hlsl", L"CSDenoiseLastPass", L"cs_6_6", {}, CSDenoiseLastPass);
		psoDesc.CS = CD3DX12_SHADER_BYTECODE(CSDenoiseLastPass.Get());
		ThrowIfFailed(device->CreateComputePipelineState(
			&psoDesc, IID_PPV_ARGS(&DenoiseLastPassPSO)));
    }
};
