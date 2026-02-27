#pragma once
#include <vector>
#include <cstdint>

#include "RenderPasses/Base/ComputePass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Managers/Singletons/CommandSignatureManager.h"
#include "Managers/MaterialManager.h"
#include "Render/RenderContext.h"
#include "Render/IndirectCommand.h"
#include "Render/GraphExtensions/CLodExtensionComponents.h"

class EvaluateMaterialGroupsPass : public ComputePass {
public:
    EvaluateMaterialGroupsPass() {
        auto& ecsWorld = ECSManager::GetInstance().GetWorld();

        // Global LOD extension visibility buffer tag
        auto visBufferTag = ecsWorld.component<CLodExtensionVisibilityBufferTag>();

        // Query for entities with the visibility buffer tag
        m_visibleClustersQuery =
            ecsWorld.query_builder<>()
            .with<CLodExtensionTypeTag>(visBufferTag)
            .with<VisibleClustersBufferTag>()
            .build();
    }

    void DeclareResourceUsages(ComputePassBuilder* b) override {
        b->WithShaderResource(ECSResourceResolver(m_visibleClustersQuery));

        b->WithShaderResource("Builtin::VisUtil::PixelListBuffer",
            MESH_RESOURCE_IDFENTIFIERS,
            Builtin::PrimaryCamera::VisibilityTexture,
            //Builtin::PrimaryCamera::VisibleClusterTable,
            Builtin::PerMeshInstanceBuffer,
            Builtin::PerObjectBuffer,
            Builtin::PerMeshBuffer,
            Builtin::CameraBuffer,
            Builtin::PostSkinningVertices,
            Builtin::NormalMatrixBuffer,
            Builtin::PerMaterialDataBuffer,
            Builtin::CLod::Offsets,
			Builtin::CLod::GroupChunks,
			Builtin::CLod::Groups,
            Builtin::CLod::CompressedMeshletVertexIndices,
			Builtin::CLod::CompressedPositions,
            Builtin::CLod::CompressedNormals)
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
        //RegisterSRV(Builtin::PrimaryCamera::VisibleClusterTable);
        RegisterSRV(Builtin::CameraBuffer);
        RegisterSRV(Builtin::PostSkinningVertices);
        RegisterSRV(Builtin::NormalMatrixBuffer);
		RegisterSRV(Builtin::CLod::Offsets);
        RegisterSRV(Builtin::CLod::GroupChunks);
		RegisterSRV(Builtin::CLod::Groups);
		RegisterSRV(Builtin::CLod::CompressedMeshletVertexIndices);
		RegisterSRV(Builtin::CLod::CompressedPositions);
		RegisterSRV(Builtin::CLod::CompressedNormals);

        RegisterUAV(Builtin::GBuffer::Normals);
        RegisterUAV(Builtin::GBuffer::Albedo);
        RegisterUAV(Builtin::GBuffer::Emissive);
        RegisterUAV(Builtin::GBuffer::MetallicRoughness);
		RegisterUAV(Builtin::GBuffer::MotionVectors);

        std::vector<GloballyIndexedResource*> visibleClusterResources;
        m_visibleClustersQuery.each([&](flecs::entity e) {
            auto& res = e.get<Components::Resource>();
            auto test = std::static_pointer_cast<GloballyIndexedResource>(res.resource.lock());
            if (test) {
                visibleClusterResources.push_back(test.get());
            }
            });

        if (visibleClusterResources.size() != 1) {
            throw std::runtime_error("BuildPixelListPass: Expected exactly one visible cluster buffer resource.");
        }

        m_visibleClusterBufferSRVIndex = visibleClusterResources[0]->GetSRVInfo(0).slot.index;

        m_materialEvalCmds = m_resourceRegistryView->RequestPtr<Resource>("Builtin::IndirectCommandBuffers::MaterialEvaluationCommandBuffer");
    }

    PassReturn Execute(PassExecutionContext& executionContext) override {
        auto* renderContext = executionContext.hostData->Get<RenderContext>();
        auto& ctx = *renderContext;
        auto& cl = executionContext.commandList;
        auto& psoMgr = PSOManager::GetInstance();

        cl.SetDescriptorHeaps(ctx.textureDescriptorHeap.GetHandle(), ctx.samplerDescriptorHeap.GetHandle());
        cl.BindLayout(psoMgr.GetComputeRootSignature().GetHandle());

        // Execute one indirect compute per active material slot.
        const auto& active = ctx.materialManager->GetActiveCompileFlags();
        const auto& sig = CommandSignatureManager::GetInstance().GetMaterialEvaluationCommandSignature();

        const uint64_t stride = sizeof(MaterialEvaluationIndirectCommand);
        auto argBuf = m_materialEvalCmds->GetAPIResource();

		for (MaterialCompileFlags flags : active) { // TODO: cache on material flag changes, avoid in-frame compile
			unsigned int slot = ctx.materialManager->GetCompileFlagsSlot(flags);
			// Bind pipeline for this material compile flag set
            auto psoIter = m_psoCache.find(flags);
            if (psoIter == m_psoCache.end()) {
                auto [newIter, _] = m_psoCache.emplace(
                    flags,
                    psoMgr.MakeComputePipeline(
                        psoMgr.GetComputeRootSignature().GetHandle(),
                        L"shaders/VisUtil.hlsl",
                        L"EvaluateMaterialGroupCS",
                        psoMgr.GetShaderDefines(0, flags),
                        "VisUtil_EvaluateMaterialGroupPSO"));
                psoIter = newIter;
            }

            PipelineState& pso = psoIter->second;

            cl.BindPipeline(pso.GetAPIPipelineState().GetHandle());
            BindResourceDescriptorIndices(cl, pso.GetResourceDescriptorSlots());

            // Set per-pass root constants
            unsigned int miscRootConstants[NumMiscUintRootConstants] = {};
            miscRootConstants[VISBUF_VISIBLE_CLUSTERS_BUFFER_DESCRIPTOR_INDEX] = m_visibleClusterBufferSRVIndex;
            cl.PushConstants(rhi::ShaderStage::Compute, 0, MiscUintRootSignatureIndex, 0, NumMiscUintRootConstants, miscRootConstants);

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

    void Cleanup() override {}

private:
    std::unordered_map<MaterialCompileFlags, PipelineState> m_psoCache;
    Resource* m_materialEvalCmds;
    flecs::query<> m_visibleClustersQuery;
    uint32_t m_visibleClusterBufferSRVIndex = 0;
};