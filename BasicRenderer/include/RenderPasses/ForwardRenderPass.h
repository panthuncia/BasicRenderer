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
#include "Managers/ObjectManager.h"
#include "Managers/CameraManager.h"
#include "Managers/Singletons/ECSManager.h"
#include "Mesh/MeshInstance.h"
#include "Managers/LightManager.h"
#include "Resources/ECSResourceResolver.h"
#include "../../shaders/PerPassRootConstants/amplificationShaderRootConstants.h"

class ForwardRenderPass : public RenderPass {
public:
    ForwardRenderPass(bool wireframe,
        bool meshShaders,
        bool indirect,
        int aoTextureDescriptorIndex)
        : m_wireframe(wireframe),
        m_meshShaders(meshShaders),
        m_indirect(indirect),
        m_aoTextureDescriptorIndex(aoTextureDescriptorIndex)
    {
        auto& settingsManager = SettingsManager::GetInstance();
        getImageBasedLightingEnabled = settingsManager.getSettingGetter<bool>("enableImageBasedLighting");
        getPunctualLightingEnabled = settingsManager.getSettingGetter<bool>("enablePunctualLighting");
        getShadowsEnabled = settingsManager.getSettingGetter<bool>("enableShadows");
        m_gtaoEnabled = settingsManager.getSettingGetter<bool>("enableGTAO")();
        m_clusteredLightingEnabled = settingsManager.getSettingGetter<bool>("enableClusteredLighting")();
        m_deferred = settingsManager.getSettingGetter<bool>("enableDeferredRendering")();
    }

    ~ForwardRenderPass() {
    }

    void DeclareResourceUsages(RenderPassBuilder* builder) override {
        builder->WithShaderResource(Builtin::CameraBuffer,
            Builtin::Environment::PrefilteredCubemapsGroup,
            Builtin::Light::ActiveLightIndices,
            Builtin::Light::InfoBuffer,
            Builtin::Light::PointLightCubemapBuffer,
            Builtin::Light::DirectionalLightCascadeBuffer,
            Builtin::Light::SpotLightMatrixBuffer,
            Builtin::Environment::InfoBuffer,
            Builtin::Environment::CurrentCubemap,
            Builtin::NormalMatrixBuffer,
            Builtin::PerObjectBuffer,
            Builtin::PerMeshBuffer,
            Builtin::PerMeshInstanceBuffer,
            Builtin::PostSkinningVertices,
            Builtin::Shadows::ShadowMaps)
            .WithRenderTarget(Builtin::Color::HDRColorTarget)
            .WithDepthReadWrite(Builtin::PrimaryCamera::DepthTexture)
            .IsGeometryPass();
        if (m_clusteredLightingEnabled) {
            builder->WithShaderResource(Builtin::Light::ClusterBuffer, Builtin::Light::PagesBuffer);
        }

        if (m_gtaoEnabled) {
            builder->WithShaderResource(Builtin::GTAO::OutputAOTerm);
        }
        auto& ecsWorld = ECSManager::GetInstance().GetWorld();
        if (m_meshShaders) {
            builder->WithShaderResource(MESH_RESOURCE_IDFENTIFIERS, Builtin::PrimaryCamera::MeshletBitfield);
            if (m_indirect) { // Indirect draws only supported with mesh shaders, becasue I'm not writing a separate codepath for doing it the bad way
				auto forwardPassEntity = m_ecsPhaseEntities[Engine::Primary::ForwardPass];
				flecs::query<> indirectQuery = ecsWorld.query_builder<flecs::entity>()
                    .with<Components::IsIndirectArguments>()
					.with<Components::ParticipatesInPass>(forwardPassEntity) // Query for command lists that participate in this pass
                    .cached().cache_kind(flecs::QueryCacheAll)
                    .build();
                builder->WithIndirectArguments(indirectQuery);
            }
        }
    }

    void Setup() override {
        auto& ecsWorld = ECSManager::GetInstance().GetWorld();
        m_meshInstancesQuery = ecsWorld.query_builder<Components::ObjectDrawInfo, Components::MeshInstances>().cached().cache_kind(flecs::QueryCacheAll).build();

        // Setup resources
        m_pPrimaryDepthBuffer = m_resourceRegistryView->Request<PixelBuffer>(Builtin::PrimaryCamera::DepthTexture);
        m_pHDRTarget = m_resourceRegistryView->Request<PixelBuffer>(Builtin::Color::HDRColorTarget);

        RegisterSRV(Builtin::NormalMatrixBuffer);
        RegisterSRV(Builtin::PostSkinningVertices);
        RegisterSRV(Builtin::PerObjectBuffer);
        RegisterSRV(Builtin::CameraBuffer);
        RegisterSRV(Builtin::PerMeshInstanceBuffer);
        RegisterSRV(Builtin::PerMeshBuffer);

        if (m_clusteredLightingEnabled) {
            RegisterSRV(Builtin::Light::ClusterBuffer);
            RegisterSRV(Builtin::Light::PagesBuffer);
        }

        if (m_meshShaders) {
            RegisterSRV(Builtin::MeshResources::MeshletOffsets);
            RegisterSRV(Builtin::MeshResources::MeshletVertexIndices);
            RegisterSRV(Builtin::MeshResources::MeshletTriangles);
        }

        RegisterSRV(Builtin::Light::ActiveLightIndices);
        RegisterSRV(Builtin::Light::InfoBuffer);
        RegisterSRV(Builtin::Light::PointLightCubemapBuffer);
        RegisterSRV(Builtin::Light::SpotLightMatrixBuffer);
        RegisterSRV(Builtin::Light::DirectionalLightCascadeBuffer);
        RegisterSRV(Builtin::Environment::InfoBuffer);

        if (m_meshShaders)
            m_primaryCameraMeshletBitfield = m_resourceRegistryView->Request<DynamicGloballyIndexedResource>(Builtin::PrimaryCamera::MeshletBitfield);
    }

    PassReturn Execute(RenderContext& context) override {
        auto& commandList = context.commandList;

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

    void Cleanup(RenderContext& context) override {
    }

private:
    // Common setup code that doesn't change between techniques
    void SetupCommonState(RenderContext& context, rhi::CommandList& commandList) {

		commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());

		rhi::PassBeginInfo passInfo{};
		rhi::ColorAttachment colorAttachment{};
		colorAttachment.rtv = m_pHDRTarget->GetRTVInfo(0).slot;
		colorAttachment.loadOp = rhi::LoadOp::Load;
		colorAttachment.storeOp = rhi::StoreOp::Store;
		colorAttachment.clear = m_pHDRTarget->GetClearColor();
		passInfo.colors = { &colorAttachment, 1 };
		rhi::DepthAttachment depthAttachment{};
		depthAttachment.dsv = m_pPrimaryDepthBuffer->GetDSVInfo(0).slot;
		depthAttachment.depthLoad = rhi::LoadOp::Load;
		depthAttachment.depthStore = rhi::StoreOp::Store;
		depthAttachment.stencilLoad = rhi::LoadOp::DontCare;
		depthAttachment.stencilStore = rhi::StoreOp::DontCare;
		depthAttachment.clear = m_pPrimaryDepthBuffer->GetClearColor();
		passInfo.depth = &depthAttachment;
		passInfo.width = context.renderResolution.x;
		passInfo.height = context.renderResolution.y;
		passInfo.debugName = "Forward Render Pass";
		commandList.BeginPass(passInfo);

        commandList.SetPrimitiveTopology(rhi::PrimitiveTopology::TriangleList);
		commandList.BindLayout(PSOManager::GetInstance().GetRootSignature().GetHandle());
    }

    void SetCommonRootConstants(RenderContext& context, rhi::CommandList& commandList) {
        unsigned int settings[NumSettingsRootConstants] = { getShadowsEnabled(), getPunctualLightingEnabled(), m_gtaoEnabled };
		commandList.PushConstants(rhi::ShaderStage::AllGraphics, 0, SettingsRootSignatureIndex, 0, NumSettingsRootConstants, &settings);

        if (m_meshShaders) {
            unsigned int misc[NumMiscUintRootConstants] = {};
            misc[MESHLET_CULLING_BITFIELD_BUFFER_SRV_DESCRIPTOR_INDEX] = m_primaryCameraMeshletBitfield->GetResource()->GetSRVInfo(0).slot.index;
			commandList.PushConstants(rhi::ShaderStage::AllGraphics, 0, MiscUintRootSignatureIndex, 0, NumMiscUintRootConstants, &misc);
        }
    }

    void ExecuteRegular(RenderContext& context, rhi::CommandList& commandList) {
        // Regular forward rendering using DrawIndexedInstanced
        auto& psoManager = PSOManager::GetInstance();

        m_meshInstancesQuery.each([&](flecs::entity e, Components::ObjectDrawInfo drawInfo, Components::MeshInstances meshInstancesComponent) {
            auto& meshes = meshInstancesComponent.meshInstances;

            commandList.PushConstants(rhi::ShaderStage::AllGraphics, 0, PerObjectRootSignatureIndex, 0, 1, &drawInfo.perObjectCBIndex);

            for (auto& pMesh : meshes) {
                auto& mesh = *pMesh->GetMesh();
                auto& pso = psoManager.GetPSO(context.globalPSOFlags | mesh.material->GetPSOFlags(), mesh.material->Technique().compileFlags, m_wireframe);
                BindResourceDescriptorIndices(commandList, pso.GetResourceDescriptorSlots());
				commandList.BindPipeline(pso.GetAPIPipelineState().GetHandle());

                unsigned int perMeshIndices[NumPerMeshRootConstants] = {};
                perMeshIndices[PerMeshBufferIndex] = static_cast<uint32_t>(mesh.GetPerMeshBufferView()->GetOffset() / sizeof(PerMeshCB));
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


        // Opaque objects
        m_meshInstancesQuery.each([&](flecs::entity e, Components::ObjectDrawInfo drawInfo, Components::MeshInstances opaqueMeshes) {
            auto& meshes = opaqueMeshes.meshInstances;

			commandList.PushConstants(rhi::ShaderStage::AllGraphics, 0, PerObjectRootSignatureIndex, 0, 1, &drawInfo.perObjectCBIndex);

            for (auto& pMesh : meshes) {
                auto& mesh = *pMesh->GetMesh();
                auto& pso = psoManager.GetMeshPSO(context.globalPSOFlags | mesh.material->GetPSOFlags(), mesh.material->Technique().compileFlags, m_wireframe);
                BindResourceDescriptorIndices(commandList, pso.GetResourceDescriptorSlots());
				commandList.BindPipeline(pso.GetAPIPipelineState().GetHandle());

                unsigned int perMeshIndices[NumPerMeshRootConstants] = {};
                perMeshIndices[PerMeshBufferIndex] = static_cast<uint32_t>(mesh.GetPerMeshBufferView()->GetOffset() / sizeof(PerMeshCB));
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
		auto manager = context.indirectCommandBufferManager;

        auto primaryViewID = context.currentScene->GetPrimaryCamera().get<Components::RenderView>().viewID; // TODO: Better way of accessing this?
        auto workloads = manager->GetBuffersForRenderPhase(primaryViewID, Engine::Primary::ForwardPass);

		for (auto& workload : workloads) {

            auto& indirectBuffer = workload.second;
			auto materialCompileFlags = workload.first;
            auto& pso = psoManager.GetMeshPSO(context.globalPSOFlags, materialCompileFlags, m_wireframe);
            BindResourceDescriptorIndices(commandList, pso.GetResourceDescriptorSlots());
			commandList.BindPipeline(pso.GetAPIPipelineState().GetHandle());

            auto apiResource = workload.second.buffer->GetAPIResource();

			commandList.ExecuteIndirect(
				commandSignature.GetHandle(), 
                apiResource.GetHandle(), 
                0, 
                apiResource.GetHandle(), 
                workload.second.buffer->GetResource()->GetUAVCounterOffset(),
                workload.second.count);
        }
    }

private:
    flecs::query<Components::ObjectDrawInfo, Components::MeshInstances> m_meshInstancesQuery;
    bool m_wireframe;
    bool m_meshShaders;
    bool m_indirect;
    unsigned int m_aoTextureDescriptorIndex;
    bool m_gtaoEnabled = true;
    bool m_clusteredLightingEnabled = true;
    bool m_deferred = false;

    std::shared_ptr<DynamicGloballyIndexedResource> m_primaryCameraMeshletBitfield = nullptr;
    std::shared_ptr<DynamicGloballyIndexedResource> m_primaryCameraMeshletCullingBitfieldBuffer = nullptr;
    std::shared_ptr<PixelBuffer> m_pPrimaryDepthBuffer = nullptr;
    std::shared_ptr<PixelBuffer> m_pHDRTarget = nullptr;

    std::function<bool()> getImageBasedLightingEnabled;
    std::function<bool()> getPunctualLightingEnabled;
    std::function<bool()> getShadowsEnabled;
};