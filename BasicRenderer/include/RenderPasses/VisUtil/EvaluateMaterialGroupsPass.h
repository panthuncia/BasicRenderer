#pragma once
#include <vector>
#include <cstdint>

#include "RenderPasses/Base/ComputePass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Managers/Singletons/CommandSignatureManager.h"
#include "Managers/MaterialManager.h"
#include "Render/RenderContext.h"
#include "Render/IndirectCommand.h"

class EvaluateMaterialGroupsPass : public ComputePass {
public:
    EvaluateMaterialGroupsPass() = default;

    void DeclareResourceUsages(ComputePassBuilder* b) override {
        b->WithShaderResource("Builtin::VisUtil::PixelListBuffer")
            .WithIndirectArguments("Builtin::IndirectCommandBuffers::MaterialEvaluationCommandBuffer");
    }

    void Setup() override {
        // TODO: Variants
        m_pso = PSOManager::GetInstance().MakeComputePipeline(
            PSOManager::GetInstance().GetComputeRootSignature(),
            L"shaders/VisUtil.hlsl",
            L"EvaluateMaterialGroupBasicCS",
            {},
            "VisUtil_EvaluateMaterialGroupsPSO");

        RegisterSRV("Builtin::VisUtil::PixelListBuffer");

        m_materialEvalCmds = m_resourceRegistryView->Request<Resource>("Builtin::IndirectCommandBuffers::MaterialEvaluationCommandBuffer");
    }

    PassReturn Execute(RenderContext& ctx) override {
        auto& cl = ctx.commandList;
        auto& psoMgr = PSOManager::GetInstance();

        cl.SetDescriptorHeaps(ctx.textureDescriptorHeap.GetHandle(), ctx.samplerDescriptorHeap.GetHandle());
        cl.BindLayout(psoMgr.GetComputeRootSignature().GetHandle());
        cl.BindPipeline(m_pso.GetAPIPipelineState().GetHandle());
        BindResourceDescriptorIndices(cl, m_pso.GetResourceDescriptorSlots());

        // Execute one indirect compute per active material slot.
        const auto& active = ctx.materialManager->GetActiveMaterialSlots();
        const auto& sig = CommandSignatureManager::GetInstance().GetMaterialEvaluationCommandSignature();

        const uint64_t stride = sizeof(MaterialEvaluationIndirectCommand);
        auto argBuf = m_materialEvalCmds->GetAPIResource();

        for (unsigned int slot : active) {
            const uint64_t argOffset = static_cast<uint64_t>(slot) * stride;
            cl.ExecuteIndirect(
                sig.GetHandle(),
                argBuf.GetHandle(), argOffset,
                rhi::ResourceHandle{}, 0, // no count buffer
                1                         // single command
            );
        }

        return {};
    }

    void Cleanup(RenderContext&) override {}

private:
    PipelineState m_pso;
    std::shared_ptr<Resource> m_materialEvalCmds;
};