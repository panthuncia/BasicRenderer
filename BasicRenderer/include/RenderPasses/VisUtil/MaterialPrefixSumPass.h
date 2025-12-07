#pragma once
#include "RenderPasses/Base/ComputePass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"

class MaterialPrefixSumPass : public ComputePass {
public:
    MaterialPrefixSumPass(uint32_t numMaterials) : m_numMaterials(numMaterials) {}

    void DeclareResourceUsages(ComputePassBuilder* b) override {
        b->WithShaderResource("Builtin::VisUtil::MaterialPixelCountBuffer")
         .WithUnorderedAccess("Builtin::VisUtil::MaterialOffsetBuffer",
                              "Builtin::VisUtil::TotalPixelCountBuffer");
    }

    void Setup() override {
        m_pso = PSOManager::GetInstance().MakeComputePipeline(
            PSOManager::GetInstance().GetComputeRootSignature(),
            L"shaders/VisUtil.hlsl",
            L"MaterialPrefixSumCS",
            {},
            "MaterialPrefixSumPSO");

        RegisterSRV("Builtin::VisUtil::MaterialPixelCountBuffer");
        RegisterUAV("Builtin::VisUtil::MaterialOffsetBuffer");
        RegisterUAV("Builtin::VisUtil::TotalPixelCountBuffer");
    }

    PassReturn Execute(RenderContext& ctx) override {
        auto& pm = PSOManager::GetInstance();
        auto& cl = ctx.commandList;

        cl.SetDescriptorHeaps(ctx.textureDescriptorHeap.GetHandle(), ctx.samplerDescriptorHeap.GetHandle());
        cl.BindLayout(pm.GetComputeRootSignature().GetHandle());
        cl.BindPipeline(m_pso.GetAPIPipelineState().GetHandle());
        BindResourceDescriptorIndices(cl, m_pso.GetResourceDescriptorSlots());

        unsigned int rc[NumMiscUintRootConstants] = {};
        // UintRootConstant0 = materialCount SRV, 1 = offsets UAV, 2 = total UAV, 3 = numMaterials value
        rc[0] = m_resourceDescriptorIndexHelper->GetResourceDescriptorIndex("Builtin::VisUtil::MaterialPixelCountBuffer");
        rc[1] = m_resourceDescriptorIndexHelper->GetResourceDescriptorIndex("Builtin::VisUtil::MaterialOffsetBuffer");
        rc[2] = m_resourceDescriptorIndexHelper->GetResourceDescriptorIndex("Builtin::VisUtil::TotalPixelCountBuffer");
        rc[3] = m_numMaterials;
        cl.PushConstants(rhi::ShaderStage::Compute, 0, MiscUintRootSignatureIndex, 0, NumMiscUintRootConstants, rc);

        cl.Dispatch(1, 1, 1); // single-group scan for moderate material counts; switch to multi-pass for large N
        return {};
    }

    void Cleanup(RenderContext&) override {}

private:
    PipelineState m_pso;
    uint32_t m_numMaterials;
};