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

    void Setup() override {
		CreateXeGTAOComputePSO();
    }

    void DeclareResourceUsages(ComputePassBuilder* builder) {
        builder->WithShaderResource(Builtin::GTAO::WorkingEdges, Builtin::GTAO::WorkingAOTerm1)
            .WithUnorderedAccess(Builtin::GTAO::OutputAOTerm);
    }

    PassReturn Execute(RenderContext& context) override {

        auto& psoManager = PSOManager::GetInstance();
        auto& commandList = context.commandList;

		commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());

		// Set the root signature
		commandList.BindLayout(psoManager.GetRootSignature().GetHandle());
		commandList.BindPipeline(DenoiseLastPassPSO.GetAPIPipelineState().GetHandle());

        unsigned int gtaoConstants[NumMiscUintRootConstants] = {};
        gtaoConstants[UintRootConstant0] = m_pGTAOConstantBuffer->GetCBVInfo().slot.index;
        gtaoConstants[UintRootConstant1] = m_workingAOBufferIndex;
            
		commandList.PushConstants(rhi::ShaderStage::Compute, 0, MiscUintRootSignatureIndex, 0, NumMiscUintRootConstants, gtaoConstants);

        commandList.Dispatch((context.renderResolution.x + (XE_GTAO_NUMTHREADS_X*2)-1) / (XE_GTAO_NUMTHREADS_X*2), (context.renderResolution.y + XE_GTAO_NUMTHREADS_Y-1) / XE_GTAO_NUMTHREADS_Y, 1 );
    
        return {};
    }

    void Cleanup(RenderContext& context) override {
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
