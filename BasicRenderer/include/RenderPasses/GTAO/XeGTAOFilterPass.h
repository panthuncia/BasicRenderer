#pragma once

#include "RenderPasses/Base/ComputePass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"

class GTAOFilterPass : public ComputePass {
public:
    GTAOFilterPass(){}

    void Setup() override {
		CreateXeGTAOComputePSO();
        RegisterCBV("Builtin::GTAO::ConstantsBuffer");
    }

    void DeclareResourceUsages(ComputePassBuilder* builder){
        builder->WithShaderResource(Builtin::GBuffer::Normals, Builtin::PrimaryCamera::DepthTexture)
            .WithUnorderedAccess(Builtin::GTAO::WorkingDepths)
            .WithConstantBuffer("Builtin::GTAO::ConstantsBuffer");
    }

    PassReturn Execute(RenderContext& context) override {
        auto& psoManager = PSOManager::GetInstance();
        auto& commandList = context.commandList;

		commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());

		// Set the compute pipeline state
		commandList.BindLayout(psoManager.GetRootSignature().GetHandle());
		commandList.BindPipeline(PrefilterDepths16x16PSO.GetAPIPipelineState().GetHandle());

        BindResourceDescriptorIndices(commandList, PrefilterDepths16x16PSO.GetResourceDescriptorSlots());

        // Dispatch
        // note: in CSPrefilterDepths16x16 each is thread group handles a 16x16 block (with [numthreads(8, 8, 1)] and each logical thread handling a 2x2 block)
		unsigned int x = (context.renderResolution.x + 16 - 1) / 16;
		unsigned int y = (context.renderResolution.y + 16 - 1) / 16;
		commandList.Dispatch(x, y, 1);

        return {};
    }

    void Cleanup(RenderContext& context) override {
        // Cleanup if necessary
    }

private:

    PipelineState PrefilterDepths16x16PSO;

    void CreateXeGTAOComputePSO()
    {
        auto& psoManager = PSOManager::GetInstance();
        PrefilterDepths16x16PSO = psoManager.MakeComputePipeline(
            psoManager.GetRootSignature(),
            L"shaders/GTAO.hlsl",
            L"CSPrefilterDepths16x16"
		);
    }
};
