#pragma once

#include "RenderPasses/Base/ComputePass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "Resources/Texture.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Managers/Singletons/UploadManager.h"

class GTAOFilterPass : public ComputePass {
public:
    GTAOFilterPass(std::shared_ptr<GloballyIndexedResource> pGTAOConstantBuffer) : m_pGTAOConstantBuffer(pGTAOConstantBuffer) {}

    void Setup(const ResourceRegistryView& resourceRegistryView) override {
		CreateXeGTAOComputePSO();
    }

    void DeclareResourceUsages(ComputePassBuilder* builder){
        builder->WithShaderResource(Builtin::GBuffer::Normals, Builtin::PrimaryCamera::DepthTexture)
            .WithUnorderedAccess(Builtin::GTAO::WorkingDepths);
    }

    PassReturn Execute(RenderContext& context) override {
        auto& psoManager = PSOManager::GetInstance();
        auto& commandList = context.commandList;

        ID3D12DescriptorHeap* descriptorHeaps[] = {
            context.textureDescriptorHeap, // The texture descriptor heap
            context.samplerDescriptorHeap, // The sampler descriptor heap
        };
        commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

		// Set the compute pipeline state
		commandList->SetComputeRootSignature(psoManager.GetRootSignature().Get());
		commandList->SetPipelineState(PrefilterDepths16x16PSO.Get());

        unsigned int passConstants[NumMiscUintRootConstants] = {};
        passConstants[0] = m_pGTAOConstantBuffer->GetCBVInfo().index;

		commandList->SetComputeRoot32BitConstants(MiscUintRootSignatureIndex, NumMiscUintRootConstants, passConstants, 0);

        // Dispatch
        // note: in CSPrefilterDepths16x16 each is thread group handles a 16x16 block (with [numthreads(8, 8, 1)] and each logical thread handling a 2x2 block)
		unsigned int x = (context.xRes + 16 - 1) / 16;
		unsigned int y = (context.yRes + 16 - 1) / 16;
		commandList->Dispatch(x, y, 1);

        return {};
    }

    void Cleanup(RenderContext& context) override {
        // Cleanup if necessary
    }

private:
    std::shared_ptr<GloballyIndexedResource> m_pGTAOConstantBuffer;

    ComPtr<ID3D12PipelineState> PrefilterDepths16x16PSO;

    void CreateXeGTAOComputePSO()
    {
        auto device = DeviceManager::GetInstance().GetDevice();

        Microsoft::WRL::ComPtr<ID3DBlob> CSPrefilterDepths16x16;
        PSOManager::GetInstance().CompileShader(L"shaders/GTAO.hlsl", L"CSPrefilterDepths16x16", L"cs_6_6", {}, CSPrefilterDepths16x16);
        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.pRootSignature = PSOManager::GetInstance().GetRootSignature().Get();
        psoDesc.CS = CD3DX12_SHADER_BYTECODE(CSPrefilterDepths16x16.Get());
        psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
        ThrowIfFailed(device->CreateComputePipelineState(
            &psoDesc, IID_PPV_ARGS(&PrefilterDepths16x16PSO)));
    }
};
