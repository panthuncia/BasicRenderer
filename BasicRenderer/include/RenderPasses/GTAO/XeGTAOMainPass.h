#pragma once

#include "RenderPasses/Base/ComputePass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "ThirdParty/XeGTAO.h"

class GTAOMainPass : public ComputePass {
public:
    GTAOMainPass() {
        CreateXeGTAOComputePSO();
    }

    void DeclareResourceUsages(ComputePassBuilder* builder) {
        builder->WithShaderResource(Builtin::GBuffer::Normals, Builtin::GTAO::WorkingDepths, Builtin::CameraBuffer)
            .WithUnorderedAccess(Builtin::GTAO::WorkingEdges, Builtin::GTAO::WorkingAOTerm1)
            .WithConstantBuffer("Builtin::GTAO::ConstantsBuffer");
    }

    void Setup() override {
        RegisterSRV(Builtin::CameraBuffer);
        RegisterSRV(Builtin::GBuffer::Normals);
        RegisterCBV("Builtin::GTAO::ConstantsBuffer");
    }

    PassReturn Execute(RenderContext& context) override {
        frameIndex++;
        auto& psoManager = PSOManager::GetInstance();
        auto& commandList = context.commandList;

		commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());

		commandList.BindLayout(psoManager.GetRootSignature().GetHandle());
		commandList.BindPipeline(GTAOHighPSO.GetAPIPipelineState().GetHandle());

        BindResourceDescriptorIndices(commandList, GTAOHighPSO.GetResourceDescriptorSlots());

        unsigned int passConstants[NumMiscUintRootConstants] = {};
		passConstants[0] = frameIndex % 64; // For spatiotemporal denoising

		commandList.PushConstants(rhi::ShaderStage::Compute, 0, MiscUintRootSignatureIndex, 0, NumMiscUintRootConstants, passConstants);

        commandList.Dispatch((context.renderResolution.x + (XE_GTAO_NUMTHREADS_X * 2) - 1) / (XE_GTAO_NUMTHREADS_X), (context.renderResolution.y + XE_GTAO_NUMTHREADS_Y - 1) / XE_GTAO_NUMTHREADS_Y, 1);
        return {};
    }

    void Cleanup(RenderContext& context) override {
        // Cleanup if necessary
    }

private:

    PipelineState PrefilterDepths16x16PSO;
    PipelineState GTAOLowPSO;
    PipelineState GTAOMediumPSO;
    PipelineState GTAOHighPSO;
    PipelineState GTAOUltraPSO;
    PipelineState DenoisePassPSO;
    PipelineState DenoiseLastPassPSO;
    PipelineState GenerateNormalsPSO;

    uint64_t frameIndex = 0;

    void CreateXeGTAOComputePSO() {

		GTAOUltraPSO = PSOManager::GetInstance().MakeComputePipeline(
			PSOManager::GetInstance().GetRootSignature(),
			L"shaders/GTAO.hlsl",
			L"CSGTAOUltra",
			{},
			"GTAO Ultra Quality");

		GTAOHighPSO = PSOManager::GetInstance().MakeComputePipeline(
			PSOManager::GetInstance().GetRootSignature(),
			L"shaders/GTAO.hlsl",
			L"CSGTAOHigh",
			{},
			"GTAO High Quality");

		GTAOMediumPSO = PSOManager::GetInstance().MakeComputePipeline(
			PSOManager::GetInstance().GetRootSignature(),
			L"shaders/GTAO.hlsl",
			L"CSGTAOMedium",
			{},
			"GTAO Medium Quality");

		GTAOLowPSO = PSOManager::GetInstance().MakeComputePipeline(
			PSOManager::GetInstance().GetRootSignature(),
			L"shaders/GTAO.hlsl",
			L"CSGTAOLow",
			{},
			"GTAO Low Quality");
		
    }
};
