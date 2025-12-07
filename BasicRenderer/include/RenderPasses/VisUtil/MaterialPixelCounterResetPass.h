#pragma once
#include "RenderPasses/Base/ComputePass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"

class MaterialPixelCounterResetPass : public ComputePass {
public:
    MaterialPixelCounterResetPass() {}

    void DeclareResourceUsages(ComputePassBuilder* builder) override {
        builder->WithUnorderedAccess("Builtin::VisUtil::MaterialPixelCountBuffer",
                                     "Builtin::VisUtil::NumMaterialsBuffer");
    }

    void Setup() override {
        m_pso = PSOManager::GetInstance().MakeComputePipeline(
            PSOManager::GetInstance().GetComputeRootSignature(),
            L"shaders/VisUtil.hlsl",
            L"ClearMaterialCountersCS",
            {},
            "ClearMaterialCountersPSO");

        RegisterUAV("Builtin::VisUtil::MaterialPixelCountBuffer");
        RegisterSRV("Builtin::VisUtil::NumMaterialsBuffer"); // read-only SRV for numMaterials (or CBV)
    }

    PassReturn Execute(RenderContext& context) override {
        auto& psoManager = PSOManager::GetInstance();
        auto& cl = context.commandList;

        cl.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(),
                              context.samplerDescriptorHeap.GetHandle());
        cl.BindLayout(psoManager.GetComputeRootSignature().GetHandle());
        cl.BindPipeline(m_pso.GetAPIPipelineState().GetHandle());
        BindResourceDescriptorIndices(cl, m_pso.GetResourceDescriptorSlots());

        auto numMaterials = context.materialManager->GetMaterialSlotsUsed();
        // Push: UintRootConstant0 = MaterialCount SRV descriptor index, UintRootConstant1 = MaterialPixelCountBuffer UAV index
        unsigned int rc[NumMiscUintRootConstants] = {};
        rc[0] = m_resourceDescriptorIndexHelper->GetResourceDescriptorIndex("Builtin::VisUtil::NumMaterialsBuffer");
        rc[1] = m_resourceDescriptorIndexHelper->GetResourceDescriptorIndex("Builtin::VisUtil::MaterialPixelCountBuffer");
        rc[2] = numMaterials;
        cl.PushConstants(rhi::ShaderStage::Compute, 0, MiscUintRootSignatureIndex, 0, NumMiscUintRootConstants, rc);
        
		unsigned int threadGroupCount = (numMaterials + 63) / 64;
		cl.Dispatch(threadGroupCount, 1, 1);

        return {};
    }

    void Cleanup(RenderContext&) override {}

private:
    PipelineState m_pso;
};