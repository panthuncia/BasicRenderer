#pragma once

#include "RenderPasses/Base/ComputePass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"

class GTAODenoisePass : public ComputePass {
public:
    GTAODenoisePass() {
        CreateXeGTAOComputePSO();
    }

    void DeclareResourceUsages(ComputePassBuilder* builder) {
        builder->WithShaderResource(Builtin::GTAO::WorkingEdges, Builtin::GTAO::WorkingAOTerm1)
            .WithUnorderedAccess(Builtin::GTAO::OutputAOTerm)
            .WithConstantBuffer("Builtin::GTAO::ConstantsBuffer");
    }

    void Setup() override {
        RegisterCBV("Builtin::GTAO::ConstantsBuffer");
        m_workingAOBufferIndex = m_resourceRegistryView->RequestPtr<GloballyIndexedResource>(Builtin::GTAO::WorkingAOTerm1)->GetSRVInfo(0).slot.index;
    }

    PassReturn Execute(RenderContext& context) override {

        auto& psoManager = PSOManager::GetInstance();
        auto& commandList = context.commandList;

		commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());

		// Set the root signature
		commandList.BindLayout(psoManager.GetRootSignature().GetHandle());
		commandList.BindPipeline(DenoiseLastPassPSO.GetAPIPipelineState().GetHandle());

		BindResourceDescriptorIndices(commandList, DenoiseLastPassPSO.GetResourceDescriptorSlots());

        unsigned int gtaoConstants[NumMiscUintRootConstants] = {};
        gtaoConstants[UintRootConstant0] = m_workingAOBufferIndex;
            
		commandList.PushConstants(rhi::ShaderStage::Compute, 0, MiscUintRootSignatureIndex, 0, NumMiscUintRootConstants, gtaoConstants);

        commandList.Dispatch((context.renderResolution.x + (XE_GTAO_NUMTHREADS_X*2)-1) / (XE_GTAO_NUMTHREADS_X*2), (context.renderResolution.y + XE_GTAO_NUMTHREADS_Y-1) / XE_GTAO_NUMTHREADS_Y, 1 );
    
        return {};
    }

    void Cleanup() override {
        // Cleanup if necessary
    }

private:
    std::shared_ptr<GloballyIndexedResource> m_pGTAOConstantBuffer;

    PipelineState DenoisePassPSO;
    PipelineState DenoiseLastPassPSO;

	unsigned int m_workingAOBufferIndex = 0;

    void CreateXeGTAOComputePSO()
    {
        auto device = DeviceManager::GetInstance().GetDevice();

		auto& psoManager = PSOManager::GetInstance();
        DenoisePassPSO = psoManager.MakeComputePipeline(
            psoManager.GetRootSignature(),
            L"shaders/GTAO.hlsl",
            L"CSDenoisePass",
            {},
			"GTAO Denoise Pass");

		DenoiseLastPassPSO = psoManager.MakeComputePipeline(
			psoManager.GetRootSignature(),
			L"shaders/GTAO.hlsl",
			L"CSDenoiseLastPass",
			{},
			"GTAO Denoise Last Pass");
    }
};
