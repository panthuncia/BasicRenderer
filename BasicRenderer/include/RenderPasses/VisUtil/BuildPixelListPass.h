#pragma once
#include "RenderPasses/Base/ComputePass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"

class BuildPixelListPass : public ComputePass {
public:
    BuildPixelListPass() {
        m_pso = PSOManager::GetInstance().MakeComputePipeline(
            PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
            L"shaders/VisUtil.hlsl",
            L"BuildPixelListCS",
            {},
            "BuildPixelListPSO");
	}
    void DeclareResourceUsages(ComputePassBuilder* b) override {
        b->WithShaderResource(MESH_RESOURCE_IDFENTIFIERS,
                              Builtin::PrimaryCamera::VisibilityTexture,
                              Builtin::PrimaryCamera::VisibleClusterTable,
                              Builtin::PerMeshInstanceBuffer,
                              Builtin::PerMeshBuffer,
                              Builtin::PerMaterialDataBuffer,
                              "Builtin::VisUtil::MaterialOffsetBuffer")
         .WithUnorderedAccess("Builtin::VisUtil::MaterialWriteCursorBuffer",
                              "Builtin::VisUtil::PixelListBuffer");
    }

    void Setup() override {
        RegisterSRV(Builtin::PrimaryCamera::VisibilityTexture);
        RegisterSRV(Builtin::PrimaryCamera::VisibleClusterTable);
        RegisterSRV(Builtin::PerMeshInstanceBuffer);
        RegisterSRV(Builtin::PerMeshBuffer);
        RegisterSRV(Builtin::PerMaterialDataBuffer);
        RegisterSRV("Builtin::VisUtil::MaterialOffsetBuffer");

        RegisterUAV("Builtin::VisUtil::MaterialWriteCursorBuffer");
        RegisterUAV("Builtin::VisUtil::PixelListBuffer");
    }

    PassReturn Execute(RenderContext& ctx) override {
        auto& pm = PSOManager::GetInstance();
        auto& cl = ctx.commandList;

        cl.SetDescriptorHeaps(ctx.textureDescriptorHeap.GetHandle(), ctx.samplerDescriptorHeap.GetHandle());
        cl.BindLayout(pm.GetComputeRootSignature().GetHandle());
        cl.BindPipeline(m_pso.GetAPIPipelineState().GetHandle());
        BindResourceDescriptorIndices(cl, m_pso.GetResourceDescriptorSlots());

        const uint32_t gsX = 8, gsY = 8;
        uint32_t x = (ctx.renderResolution.x + gsX - 1) / gsX;
        uint32_t y = (ctx.renderResolution.y + gsY - 1) / gsY;
        cl.Dispatch(x, y, 1);

        return {};
    }

    void Cleanup() override {}

private:
    PipelineState m_pso;
};