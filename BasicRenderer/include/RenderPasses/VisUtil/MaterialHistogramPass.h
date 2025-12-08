#pragma once
#include "RenderPasses/Base/ComputePass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"

class MaterialHistogramPass : public ComputePass {
public:
    void DeclareResourceUsages(ComputePassBuilder* b) override {
        b->WithShaderResource(MESH_RESOURCE_IDFENTIFIERS,
                              Builtin::PrimaryCamera::VisibilityTexture,
                              Builtin::PrimaryCamera::VisibleClusterTable,
                              Builtin::PerMeshInstanceBuffer,
                              Builtin::PerMeshBuffer)
         .WithUnorderedAccess("Builtin::VisUtil::MaterialPixelCountBuffer");
    }

    void Setup() override {
        m_pso = PSOManager::GetInstance().MakeComputePipeline(
            PSOManager::GetInstance().GetComputeRootSignature(),
            L"shaders/VisUtil.hlsl",
            L"MaterialHistogramCS",
            {},
            "MaterialHistogramPSO");

        RegisterSRV(Builtin::PrimaryCamera::VisibilityTexture);
        RegisterSRV(Builtin::PrimaryCamera::VisibleClusterTable);
        RegisterSRV(Builtin::PerMeshInstanceBuffer);
        RegisterSRV(Builtin::PerMeshBuffer);
        RegisterUAV("Builtin::VisUtil::MaterialPixelCountBuffer");
    }

    PassReturn Execute(RenderContext& ctx) override {
        auto& pm = PSOManager::GetInstance();
        auto& cl = ctx.commandList;

        cl.SetDescriptorHeaps(ctx.textureDescriptorHeap.GetHandle(), ctx.samplerDescriptorHeap.GetHandle());
        cl.BindLayout(pm.GetComputeRootSignature().GetHandle());
        cl.BindPipeline(m_pso.GetAPIPipelineState().GetHandle());
        BindResourceDescriptorIndices(cl, m_pso.GetResourceDescriptorSlots());

        const uint32_t groupSizeX = 8, groupSizeY = 8;
        uint32_t x = (ctx.renderResolution.x + groupSizeX - 1) / groupSizeX;
        uint32_t y = (ctx.renderResolution.y + groupSizeY - 1) / groupSizeY;
        cl.Dispatch(x, y, 1);
        return {};
    }

    void Cleanup(RenderContext&) override {}

private:
    PipelineState m_pso;
};