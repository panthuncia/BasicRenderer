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
    EvaluateMaterialGroupsPass() {}

    void DeclareResourceUsages(ComputePassBuilder* b) override {
        b->WithShaderResource("Builtin::VisUtil::PixelListBuffer",
            MESH_RESOURCE_IDFENTIFIERS,
            Builtin::PrimaryCamera::VisibilityTexture,
            Builtin::PrimaryCamera::VisibleClusterTable,
            Builtin::PerMeshInstanceBuffer,
            Builtin::PerObjectBuffer,
            Builtin::PerMeshBuffer,
            Builtin::CameraBuffer,
            Builtin::PostSkinningVertices,
            Builtin::NormalMatrixBuffer,
            Builtin::PerMaterialDataBuffer)
            .WithUnorderedAccess(Builtin::GBuffer::Normals,
                Builtin::GBuffer::Albedo,
                Builtin::GBuffer::Emissive,
                Builtin::GBuffer::MetallicRoughness,
                Builtin::GBuffer::MotionVectors)
            .WithIndirectArguments("Builtin::IndirectCommandBuffers::MaterialEvaluationCommandBuffer");
    }

    void Setup() override {
        RegisterSRV("Builtin::VisUtil::PixelListBuffer");
        RegisterSRV(Builtin::MeshResources::MeshletOffsets);
        RegisterSRV(Builtin::MeshResources::MeshletVertexIndices);
        RegisterSRV(Builtin::MeshResources::MeshletTriangles);
        RegisterSRV(Builtin::PerMeshInstanceBuffer);
        RegisterSRV(Builtin::PerObjectBuffer);
        RegisterSRV(Builtin::PerMeshBuffer);
        RegisterSRV(Builtin::PerMaterialDataBuffer);
        RegisterSRV(Builtin::PrimaryCamera::VisibilityTexture);
        RegisterSRV(Builtin::PrimaryCamera::VisibleClusterTable);
        RegisterSRV(Builtin::CameraBuffer);
        RegisterSRV(Builtin::PostSkinningVertices);
        RegisterSRV(Builtin::NormalMatrixBuffer);

        RegisterUAV(Builtin::GBuffer::Normals);
        RegisterUAV(Builtin::GBuffer::Albedo);
        RegisterUAV(Builtin::GBuffer::Emissive);
        RegisterUAV(Builtin::GBuffer::MetallicRoughness);
		RegisterUAV(Builtin::GBuffer::MotionVectors);

        m_materialEvalCmds = m_resourceRegistryView->Request<Resource>("Builtin::IndirectCommandBuffers::MaterialEvaluationCommandBuffer");
    }

    PassReturn Execute(RenderContext& ctx) override {
        auto& cl = ctx.commandList;
        auto& psoMgr = PSOManager::GetInstance();

        cl.SetDescriptorHeaps(ctx.textureDescriptorHeap.GetHandle(), ctx.samplerDescriptorHeap.GetHandle());
        cl.BindLayout(psoMgr.GetComputeRootSignature().GetHandle());

        // Execute one indirect compute per active material slot.
        const auto& active = ctx.materialManager->GetActiveCompileFlags();
        const auto& sig = CommandSignatureManager::GetInstance().GetMaterialEvaluationCommandSignature();

        const uint64_t stride = sizeof(MaterialEvaluationIndirectCommand);
        auto argBuf = m_materialEvalCmds->GetAPIResource();

        for (MaterialCompileFlags flags : active) {
			unsigned int slot = ctx.materialManager->GetCompileFlagsSlot(flags);
			// Bind pipeline for this material compile flag set
            auto psoIter = m_psoCache.find(flags);
            if (psoIter == m_psoCache.end()) {
                auto [newIter, _] = m_psoCache.emplace(
                    flags,
                    psoMgr.MakeComputePipeline(
                        psoMgr.GetComputeRootSignature(),
                        L"shaders/VisUtil.hlsl",
                        L"EvaluateMaterialGroupCS",
                        psoMgr.GetShaderDefines(0, flags),
                        "VisUtil_EvaluateMaterialGroupPSO"));
                psoIter = newIter;
            }

            PipelineState& pso = psoIter->second;

            cl.BindPipeline(pso.GetAPIPipelineState().GetHandle());
            BindResourceDescriptorIndices(cl, pso.GetResourceDescriptorSlots());

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
    std::unordered_map<MaterialCompileFlags, PipelineState> m_psoCache;
    std::shared_ptr<Resource> m_materialEvalCmds;
};