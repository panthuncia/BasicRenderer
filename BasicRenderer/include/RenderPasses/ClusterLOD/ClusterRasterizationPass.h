#pragma once

#include <unordered_map>

#include "RenderPasses/Base/RenderPass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "Mesh/Mesh.h"
#include "Scene/Scene.h"
#include "Materials/Material.h"
#include "Managers/Singletons/CommandSignatureManager.h"
#include "Managers/MeshManager.h"
#include "Managers/Singletons/ECSManager.h"
#include "Mesh/MeshInstance.h"
#include "Managers/LightManager.h"

struct ClusterRasterizationPassInputs {
    bool wireframe;
    bool meshShaders;
    bool clearGbuffer;

    friend bool operator==(const ClusterRasterizationPassInputs&, const ClusterRasterizationPassInputs&) = default;
};

inline rg::Hash64 HashValue(const ClusterRasterizationPassInputs& i) {
    std::size_t seed = 0;

    boost::hash_combine(seed, i.wireframe);
    boost::hash_combine(seed, i.meshShaders);
    boost::hash_combine(seed, i.clearGbuffer);
    return seed;
}

class ClusterRasterizationPass : public RenderPass {
public:
    ClusterRasterizationPass() {
        auto& ecsWorld = ECSManager::GetInstance().GetWorld();
        m_meshInstancesQuery = ecsWorld.query_builder<Components::ObjectDrawInfo, Components::PerPassMeshes>()
            .with<Components::ParticipatesInPass>(ECSManager::GetInstance().GetRenderPhaseEntity(Engine::Primary::GBufferPass))
            .cached().cache_kind(flecs::QueryCacheAll).build();
    }

    ~ClusterRasterizationPass() {
    }

    void DeclareResourceUsages(RenderPassBuilder* builder) {
		auto input = Inputs<ClusterRasterizationPassInputs>();
		m_wireframe = input.wireframe;
		m_meshShaders = input.meshShaders;
		m_clearGbuffer = input.clearGbuffer;

        builder->WithShaderResource(MESH_RESOURCE_IDFENTIFIERS,
            Builtin::MeshResources::ClusterToVisibleClusterTableIndexBuffer,
            Builtin::PerObjectBuffer,
            Builtin::NormalMatrixBuffer,
            Builtin::PerMeshBuffer,
            Builtin::PerMeshInstanceBuffer,
            Builtin::PerMaterialDataBuffer,
            Builtin::PostSkinningVertices,
            Builtin::CameraBuffer)
            .WithRenderTarget(
                Subresources(Builtin::PrimaryCamera::LinearDepthMap, Mip{ 0, 1 })
            )
            .WithDepthReadWrite(Builtin::PrimaryCamera::DepthTexture)
            .IsGeometryPass();
            builder->WithRenderTarget(
                Builtin::PrimaryCamera::VisibilityTexture);

        if (m_meshShaders) {
            builder->WithShaderResource(Builtin::PerMeshBuffer, Builtin::PrimaryCamera::MeshletBitfield);
            auto& ecsWorld = ECSManager::GetInstance().GetWorld();
            flecs::query<> indirectQuery = ecsWorld.query_builder<>()
                .with<Components::IsIndirectArguments>()
                .with<Components::ParticipatesInPass>(ECSManager::GetInstance().GetRenderPhaseEntity(Engine::Primary::GBufferPass)) // Query for command lists that participate in this pass
                //.cached().cache_kind(flecs::QueryCacheAll)
                .build();
            builder->WithIndirectArguments(ECSResourceResolver(indirectQuery));
        }
    }

    void Setup() override {

        m_pPrimaryDepthBuffer = m_resourceRegistryView->RequestPtr<PixelBuffer>(Builtin::PrimaryCamera::DepthTexture);
		m_pVisibilityBuffer = m_resourceRegistryView->RequestPtr<PixelBuffer>(Builtin::PrimaryCamera::VisibilityTexture);

        if (m_meshShaders) {
            RegisterSRV(Builtin::MeshResources::MeshletOffsets);
            RegisterSRV(Builtin::MeshResources::MeshletVertexIndices);
            RegisterSRV(Builtin::MeshResources::MeshletTriangles);
        }

        RegisterSRV(Builtin::NormalMatrixBuffer);
        RegisterSRV(Builtin::PostSkinningVertices);
        RegisterSRV(Builtin::PerObjectBuffer);
        RegisterSRV(Builtin::CameraBuffer);
        RegisterSRV(Builtin::PerMeshInstanceBuffer);
        RegisterSRV(Builtin::PerMeshBuffer);
        RegisterSRV(Builtin::PerMaterialDataBuffer);
        RegisterSRV(Builtin::MeshResources::ClusterToVisibleClusterTableIndexBuffer);
    }

    PassReturn Execute(RenderContext& context) override {
        auto& commandList = context.commandList;

        BeginPass(context);

        SetupCommonState(context, commandList);
        SetCommonRootConstants(context, commandList);

        ExecuteMeshShaderIndirect(context, commandList);
        
        return {};
    }

    void Cleanup() override {
    }

private:
    void BeginPass(RenderContext& context) {
        // Build attachments
        rhi::PassBeginInfo p{};
        p.width = context.renderResolution.x;
        p.height = context.renderResolution.y;
        p.debugName = "GBuffer Pass";

        rhi::DepthAttachment da{};
        da.dsv = m_pPrimaryDepthBuffer->GetDSVInfo(0).slot;
        da.depthStore = rhi::StoreOp::Store;

        if (m_clearGbuffer) {
            da.depthLoad = rhi::LoadOp::Clear;
            da.clear.type = rhi::ClearValueType::DepthStencil;
            da.clear.format = rhi::Format::D32_Float;
            da.clear.depthStencil.depth = 1.0f;
            da.clear.depthStencil.stencil = 0;
        }
        else {
            da.depthLoad = rhi::LoadOp::Load;
        }
        p.depth = &da;

        std::vector<rhi::ColorAttachment> colors;

		// visibility buffer
        {
            rhi::ColorAttachment ca{};
            ca.rtv = m_pVisibilityBuffer->GetRTVInfo(0).slot;
            ca.storeOp = rhi::StoreOp::Store;
			ca.loadOp = rhi::LoadOp::Load; // Clearing is handled in a separate pass
            colors.push_back(ca);
        }

        p.colors = { colors.data(), (uint32_t)colors.size() };

        context.commandList.BeginPass(p);
    }
    // Common setup code that doesn't change between techniques
    void SetupCommonState(RenderContext& context, rhi::CommandList& commandList) {

        commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());

        commandList.SetPrimitiveTopology(rhi::PrimitiveTopology::TriangleList);

        // Root signature
        commandList.BindLayout(PSOManager::GetInstance().GetRootSignature().GetHandle());
    }

    void SetCommonRootConstants(RenderContext& context, rhi::CommandList& commandList) {

    }

    void ExecuteMeshShaderIndirect(RenderContext& context, rhi::CommandList& commandList) {
        // Mesh shading with ExecuteIndirect
        auto& psoManager = PSOManager::GetInstance();

        auto commandSignature = CommandSignatureManager::GetInstance().GetDispatchMeshCommandSignature();

        // Opaque clusters

    }

private:

    flecs::query<Components::ObjectDrawInfo, Components::PerPassMeshes> m_meshInstancesQuery;
    bool m_wireframe;
    bool m_meshShaders;
    bool m_clearGbuffer = true;

    PixelBuffer* m_pPrimaryDepthBuffer;
    PixelBuffer* m_pVisibilityBuffer;

    RenderPhase m_renderPhase = Engine::Primary::GBufferPass;
};