#pragma once
#include "RenderPasses/Base/ComputePass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"

// Pass A: per-block exclusive scan producing per-element local offsets and per-block totals.
// Dispatch dimension: x = numBlocks, where numBlocks = ceil(NumMaterials / blockSize).
class MaterialBlockScanPass : public ComputePass {
public:
    explicit MaterialBlockScanPass() {
        m_pso = PSOManager::GetInstance().MakeComputePipeline(
            PSOManager::GetInstance().GetComputeRootSignature(),
            L"shaders/materialPrefixSum.hlsl",
            L"BlockScanCS",
            {},
            "VisUtil_BlockScanPSO");
    }

    void DeclareResourceUsages(ComputePassBuilder* b) override {
        b->WithShaderResource("Builtin::VisUtil::MaterialPixelCountBuffer")
         .WithUnorderedAccess("Builtin::VisUtil::MaterialOffsetBuffer",
                              "Builtin::VisUtil::BlockSumsBuffer");
    }

    void Setup() override {

        RegisterSRV("Builtin::VisUtil::MaterialPixelCountBuffer");
        RegisterUAV("Builtin::VisUtil::MaterialOffsetBuffer");
        RegisterUAV("Builtin::VisUtil::BlockSumsBuffer");
    }

    PassReturn Execute(RenderContext& ctx) override {
        auto& pm = PSOManager::GetInstance();
        auto& cl = ctx.commandList;

		auto numMaterials = ctx.materialManager->GetCompileFlagsSlotsUsed();
        // numBlocks = ceil(N / K)
        const uint32_t numBlocks = (numMaterials + m_blockSize - 1) / m_blockSize;

        cl.SetDescriptorHeaps(ctx.textureDescriptorHeap.GetHandle(), ctx.samplerDescriptorHeap.GetHandle());
        cl.BindLayout(pm.GetComputeRootSignature().GetHandle());
        cl.BindPipeline(m_pso.GetAPIPipelineState().GetHandle());
        BindResourceDescriptorIndices(cl, m_pso.GetResourceDescriptorSlots());

        // Root constants:
        // UintRootConstant0 = NumMaterials
        unsigned int rc[NumMiscUintRootConstants] = {};
        rc[0] = numMaterials;
        cl.PushConstants(rhi::ShaderStage::Compute, 0, MiscUintRootSignatureIndex, 0, NumMiscUintRootConstants, rc);

        cl.Dispatch(numBlocks, 1, 1);
        return {};
    }

    void Cleanup(RenderContext&) override {}

private:
        PipelineState m_pso;
        // block size used by the shader (materialPrefixSum.hlsl). Keep in sync.
        uint32_t m_blockSize = 1024;
};