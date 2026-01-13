#pragma once

#include <unordered_map>
#include <functional>

#include "RenderPasses/Base/RenderPass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "Mesh/Mesh.h"
#include "Scene/Scene.h"
#include "Materials/Material.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Managers/Singletons/CommandSignatureManager.h"
#include "Managers/MeshManager.h"
#include "Managers/Singletons/ECSManager.h"
#include "Mesh/MeshInstance.h"
#include "Managers/LightManager.h"

struct GBufferPassInputs {
    bool wireframe;
    bool meshShaders;
    bool indirect;
    bool clearGbuffer;

    friend bool operator==(const GBufferPassInputs&, const GBufferPassInputs&) = default;
};

inline rg::Hash64 HashValue(const GBufferPassInputs& i) {
    std::size_t seed = 0;

    boost::hash_combine(seed, i.wireframe);
    boost::hash_combine(seed, i.meshShaders);
    boost::hash_combine(seed, i.indirect);
    boost::hash_combine(seed, i.clearGbuffer);
    return seed;
}

// TODO: Prepass for forward-rendered geometry, requires better object and indirect workload queries
class GBufferPass : public RenderPass {
public:
    GBufferPass(){
        auto& settingsManager = SettingsManager::GetInstance();
        getImageBasedLightingEnabled = settingsManager.getSettingGetter<bool>("enableImageBasedLighting");
        getPunctualLightingEnabled = settingsManager.getSettingGetter<bool>("enablePunctualLighting");
        getShadowsEnabled = settingsManager.getSettingGetter<bool>("enableShadows");

        auto& ecsWorld = ECSManager::GetInstance().GetWorld();
        m_meshInstancesQuery = ecsWorld.query_builder<Components::ObjectDrawInfo, Components::PerPassMeshes>()
            .with<Components::ParticipatesInPass>(ECSManager::GetInstance().GetRenderPhaseEntity(Engine::Primary::GBufferPass))
            .cached().cache_kind(flecs::QueryCacheAll).build();
    }

    ~GBufferPass() {
    }

    void DeclareResourceUsages(RenderPassBuilder* builder) {
		auto input = Inputs<GBufferPassInputs>();
        m_wireframe = input.wireframe;
		m_meshShaders = input.meshShaders;
		m_indirect = input.indirect;
		m_clearGbuffer = input.clearGbuffer;

        builder->WithShaderResource(MESH_RESOURCE_IDFENTIFIERS,
            Builtin::PerObjectBuffer,
            Builtin::NormalMatrixBuffer,
            Builtin::PerMeshBuffer,
            Builtin::PerMeshInstanceBuffer,
            Builtin::PerMaterialDataBuffer,
            Builtin::PostSkinningVertices,
            Builtin::CameraBuffer)
            .WithRenderTarget(
                Subresources(Builtin::PrimaryCamera::LinearDepthMap, Mip{ 0, 1 }),
                Builtin::GBuffer::Normals,
                Builtin::GBuffer::MotionVectors,
                Builtin::GBuffer::Albedo,
                Builtin::GBuffer::MetallicRoughness,
                Builtin::GBuffer::Emissive)
            .WithDepthReadWrite(Builtin::PrimaryCamera::DepthTexture)
            .IsGeometryPass();

        if (m_meshShaders) {
            builder->WithShaderResource(Builtin::PerMeshBuffer, Builtin::PrimaryCamera::MeshletBitfield);
            if (m_indirect) {
				auto& ecsWorld = ECSManager::GetInstance().GetWorld();
                flecs::query<> indirectQuery = ecsWorld.query_builder<>()
                    .with<Components::IsIndirectArguments>()
                    .with<Components::ParticipatesInPass>(ECSManager::GetInstance().GetRenderPhaseEntity(Engine::Primary::GBufferPass)) // Query for command lists that participate in this pass
                    //.cached().cache_kind(flecs::QueryCacheAll)
                    .build();
                builder->WithIndirectArguments(ECSResourceResolver(indirectQuery));
            }
        }
    }

    void Setup() override {

        m_pLinearDepthBuffer = m_resourceRegistryView->RequestPtr<PixelBuffer>(Builtin::PrimaryCamera::LinearDepthMap);
        m_pPrimaryDepthBuffer = m_resourceRegistryView->RequestPtr<PixelBuffer>(Builtin::PrimaryCamera::DepthTexture);
        m_pNormals = m_resourceRegistryView->RequestPtr<PixelBuffer>(Builtin::GBuffer::Normals);
        m_pMotionVectors = m_resourceRegistryView->RequestPtr<PixelBuffer>(Builtin::GBuffer::MotionVectors);
        m_pAlbedo = m_resourceRegistryView->RequestPtr<PixelBuffer>(Builtin::GBuffer::Albedo);
        m_pMetallicRoughness = m_resourceRegistryView->RequestPtr<PixelBuffer>(Builtin::GBuffer::MetallicRoughness);
        m_pEmissive = m_resourceRegistryView->RequestPtr<PixelBuffer>(Builtin::GBuffer::Emissive);

        if (m_meshShaders) {
            m_primaryCameraMeshletBitfield = m_resourceRegistryView->RequestPtr<DynamicGloballyIndexedResource>(Builtin::PrimaryCamera::MeshletBitfield);
        }

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
    }

    PassReturn Execute(RenderContext& context) override {
        auto& commandList = context.commandList;

        BeginPass(context);

        SetupCommonState(context, commandList);
        SetCommonRootConstants(context, commandList);


        if (m_meshShaders) {
            if (m_indirect) {
                // Indirect drawing
                ExecuteMeshShaderIndirect(context, commandList);
            }
            else {
                // Regular mesh shader drawing
                ExecuteMeshShader(context, commandList);
            }
        }
        else {
            // Regular forward rendering
            ExecuteRegular(context, commandList);
        }
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

        // normals
        {
            rhi::ColorAttachment ca{};
            ca.rtv = m_pNormals->GetRTVInfo(0).slot;
            ca.storeOp = rhi::StoreOp::Store;
            if (m_clearGbuffer) {
                ca.loadOp = rhi::LoadOp::Clear;
                ca.clear.type = rhi::ClearValueType::Color;
                ca.clear.format = rhi::Format::R16G16B16A16_Float;
                ca.clear.rgba[0] = 0.0f; ca.clear.rgba[1] = 0.0f; ca.clear.rgba[2] = 0.0f; ca.clear.rgba[3] = 1.0f;
            }
            else {
                ca.loadOp = rhi::LoadOp::Load;
            }
            colors.push_back(ca);
        }

        // motion vectors
        if (m_pMotionVectors) {
            rhi::ColorAttachment ca{};
            ca.rtv = m_pMotionVectors->GetRTVInfo(0).slot;
            ca.storeOp = rhi::StoreOp::Store;
            if (m_clearGbuffer) {
                ca.loadOp = rhi::LoadOp::Clear;
                ca.clear = m_pMotionVectors->GetClearColor();
            }
            else {
                ca.loadOp = rhi::LoadOp::Load;
            }
            colors.push_back(ca);
        }

        // linear depth (color RT used as linear-depth target)
        {
            rhi::ColorAttachment ca{};
            ca.rtv = m_pLinearDepthBuffer->GetRTVInfo(0).slot;
            ca.storeOp = rhi::StoreOp::Store;
            if (m_clearGbuffer) {
                ca.loadOp = rhi::LoadOp::Clear;
				ca.clear = m_pLinearDepthBuffer->GetClearColor();
            }
            else {
                ca.loadOp = rhi::LoadOp::Load;
            }
            colors.push_back(ca);
        }

        // albedo
        {
            rhi::ColorAttachment ca{};
            ca.rtv = m_pAlbedo->GetRTVInfo(0).slot;
            ca.storeOp = rhi::StoreOp::Store;
            if (m_clearGbuffer) {
                ca.loadOp = rhi::LoadOp::Clear;
				ca.clear = m_pAlbedo->GetClearColor();
            }
            else {
                ca.loadOp = rhi::LoadOp::Load;
            }
            colors.push_back(ca);
        }

        // metallic/roughness
        {
            rhi::ColorAttachment ca{};
            ca.rtv = m_pMetallicRoughness->GetRTVInfo(0).slot;
            ca.storeOp = rhi::StoreOp::Store;
            if (m_clearGbuffer) {
                ca.loadOp = rhi::LoadOp::Clear;
				ca.clear = m_pMetallicRoughness->GetClearColor();
            }
            else {
                ca.loadOp = rhi::LoadOp::Load;
            }
            colors.push_back(ca);
        }

		// emissive
        {
			rhi::ColorAttachment ca{};
			ca.rtv = m_pEmissive->GetRTVInfo(0).slot;
			ca.storeOp = rhi::StoreOp::Store;
            if (m_clearGbuffer) {
                ca.loadOp = rhi::LoadOp::Clear;
				ca.clear = m_pEmissive->GetClearColor();
            }
            else {
                ca.loadOp = rhi::LoadOp::Load;
			}
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
        unsigned int settings[NumSettingsRootConstants] = { getShadowsEnabled(), getPunctualLightingEnabled() };
		commandList.PushConstants(rhi::ShaderStage::AllGraphics, 0, SettingsRootSignatureIndex, 0, NumSettingsRootConstants, &settings);

        if (m_indirect || m_meshShaders) {
            unsigned int misc[NumMiscUintRootConstants] = {};
            misc[MESHLET_CULLING_BITFIELD_BUFFER_SRV_DESCRIPTOR_INDEX] = m_primaryCameraMeshletBitfield->GetResource()->GetSRVInfo(0).slot.index;
			commandList.PushConstants(rhi::ShaderStage::AllGraphics, 0, MiscUintRootSignatureIndex, 0, NumMiscUintRootConstants, &misc);
        }
    }

    void ExecuteRegular(RenderContext& context, rhi::CommandList& commandList) {
        // Regular forward rendering using DrawIndexedInstanced
        auto& psoManager = PSOManager::GetInstance();

        // Opaque objects
        m_meshInstancesQuery.each([&](flecs::entity e, Components::ObjectDrawInfo drawInfo, Components::PerPassMeshes opaqueMeshes) {
            auto& meshes = opaqueMeshes.meshesByPass[m_GBufferRenderPhase.hash];

			commandList.PushConstants(rhi::ShaderStage::AllGraphics, 0, PerObjectRootSignatureIndex, PerObjectBufferIndex, 1, &drawInfo.perObjectCBIndex);

            for (auto& pMesh : meshes) {
                auto& mesh = *pMesh->GetMesh();
                auto& pso = psoManager.GetPrePassPSO(context.globalPSOFlags | mesh.material->GetPSOFlags() | PSOFlags::PSO_DEFERRED, mesh.material->Technique().compileFlags);
                BindResourceDescriptorIndices(commandList, pso.GetResourceDescriptorSlots());
				commandList.BindPipeline(pso.GetAPIPipelineState().GetHandle());

                unsigned int perMeshIndices[NumPerMeshRootConstants] = {};
                perMeshIndices[PerMeshBufferIndex] = static_cast<unsigned int>(mesh.GetPerMeshBufferView()->GetOffset() / sizeof(PerMeshCB));
                perMeshIndices[PerMeshInstanceBufferIndex] = static_cast<uint32_t>(pMesh->GetPerMeshInstanceBufferOffset() / sizeof(PerMeshInstanceCB));
				commandList.PushConstants(rhi::ShaderStage::AllGraphics, 0, PerMeshRootSignatureIndex, 0, NumPerMeshRootConstants, perMeshIndices);

				commandList.SetIndexBuffer(mesh.GetIndexBufferView());
                commandList.DrawIndexed(mesh.GetIndexCount(), 1, 0, 0, 0);
            }
            });
    }

    void ExecuteMeshShader(RenderContext& context, rhi::CommandList& commandList) {
        // Mesh shading path using DispatchMesh
        auto& psoManager = PSOManager::GetInstance();

        m_meshInstancesQuery.each([&](flecs::entity e, Components::ObjectDrawInfo drawInfo, Components::PerPassMeshes perPassMeshes) {
            auto& meshes = perPassMeshes.meshesByPass[m_GBufferRenderPhase.hash];

			commandList.PushConstants(rhi::ShaderStage::AllGraphics, 0, PerObjectRootSignatureIndex, PerObjectBufferIndex, 1, &drawInfo.perObjectCBIndex);

            for (auto& pMesh : meshes) {
                auto& mesh = *pMesh->GetMesh();
                auto& pso = psoManager.GetMeshPrePassPSO(context.globalPSOFlags | mesh.material->GetPSOFlags() | PSOFlags::PSO_DEFERRED, mesh.material->Technique().compileFlags);
                BindResourceDescriptorIndices(commandList, pso.GetResourceDescriptorSlots());
				commandList.BindPipeline(pso.GetAPIPipelineState().GetHandle());

                unsigned int perMeshIndices[NumPerMeshRootConstants] = {};
                perMeshIndices[PerMeshBufferIndex] = static_cast<unsigned int>(mesh.GetPerMeshBufferView()->GetOffset() / sizeof(PerMeshCB));
                perMeshIndices[PerMeshInstanceBufferIndex] = static_cast<uint32_t>(pMesh->GetPerMeshInstanceBufferOffset() / sizeof(PerMeshInstanceCB));
				commandList.PushConstants(rhi::ShaderStage::AllGraphics, 0, PerMeshRootSignatureIndex, 0, NumPerMeshRootConstants, perMeshIndices);

                // Mesh shaders use DispatchMesh
                commandList.DispatchMesh(mesh.GetMeshletCount(), 1, 1);
            }
            });
    }

    void ExecuteMeshShaderIndirect(RenderContext& context, rhi::CommandList& commandList) {
        // Mesh shading with ExecuteIndirect
        auto& psoManager = PSOManager::GetInstance();

        auto commandSignature = CommandSignatureManager::GetInstance().GetDispatchMeshCommandSignature();

        // Opaque indirect draws
        auto workloads = context.indirectCommandBufferManager->GetBuffersForRenderPhase(
            context.currentScene->GetPrimaryCamera().get<Components::RenderViewRef>().viewID,
            m_GBufferRenderPhase);
		for (auto& workload : workloads) {
            auto& pso = psoManager.GetMeshPrePassPSO(context.globalPSOFlags | PSOFlags::PSO_DEFERRED, workload.first, m_wireframe);
            BindResourceDescriptorIndices(commandList, pso.GetResourceDescriptorSlots());
			commandList.BindPipeline(pso.GetAPIPipelineState().GetHandle());

            auto apiResource = workload.second.buffer->GetAPIResource();
            commandList.ExecuteIndirect(
                commandSignature.GetHandle(),
                apiResource.GetHandle(),
                0,
                apiResource.GetHandle(),
                workload.second.buffer->GetResource()->GetUAVCounterOffset(),
                workload.second.count
            );
        }

    }

private:

    flecs::query<Components::ObjectDrawInfo, Components::PerPassMeshes> m_meshInstancesQuery;
    bool m_wireframe;
    bool m_meshShaders;
    bool m_indirect;
    bool m_clearGbuffer = true;

    PixelBuffer* m_pLinearDepthBuffer;
    PixelBuffer* m_pPrimaryDepthBuffer;
    PixelBuffer* m_pNormals;
    PixelBuffer* m_pMotionVectors;
    PixelBuffer* m_pAlbedo;
    PixelBuffer* m_pMetallicRoughness;
    PixelBuffer* m_pEmissive;

    DynamicGloballyIndexedResource* m_primaryCameraMeshletBitfield = nullptr;

	RenderPhase m_GBufferRenderPhase = Engine::Primary::GBufferPass;
	RenderPhase m_PrePassRenderPhase = Engine::Primary::ZPrepass;

    std::function<bool()> getImageBasedLightingEnabled;
    std::function<bool()> getPunctualLightingEnabled;
    std::function<bool()> getShadowsEnabled;
};