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
#include "Managers/Singletons/ECSManager.h"
#include "Mesh/MeshInstance.h"
#include "Managers/LightManager.h"

class GBufferPass : public RenderPass {
public:
    GBufferPass(
        bool wireframe,
        bool meshShaders,
        bool indirect,
        bool clearGbuffer)
        :
        m_wireframe(wireframe),
        m_meshShaders(meshShaders),
        m_indirect(indirect),
        m_clearGbuffer(clearGbuffer) {
        auto& settingsManager = SettingsManager::GetInstance();
        getImageBasedLightingEnabled = settingsManager.getSettingGetter<bool>("enableImageBasedLighting");
        getPunctualLightingEnabled = settingsManager.getSettingGetter<bool>("enablePunctualLighting");
        getShadowsEnabled = settingsManager.getSettingGetter<bool>("enableShadows");
        m_deferred = settingsManager.getSettingGetter<bool>("enableDeferredRendering")();
    }

    ~GBufferPass() {
    }

    void DeclareResourceUsages(RenderPassBuilder* builder) {
        builder->WithShaderResource(MESH_RESOURCE_IDFENTIFIERS,
            Builtin::PerObjectBuffer,
            Builtin::NormalMatrixBuffer,
            Builtin::PerMeshBuffer,
            Builtin::PerMeshInstanceBuffer,
            Builtin::PostSkinningVertices,
            Builtin::CameraBuffer)
            .WithRenderTarget(
                Subresources(Builtin::PrimaryCamera::LinearDepthMap, Mip{ 0, 1 }),
                Builtin::GBuffer::Normals,
                Builtin::GBuffer::MotionVectors)
            .WithDepthReadWrite(Builtin::PrimaryCamera::DepthTexture)
            .IsGeometryPass();
        if (m_deferred) {
            builder->WithRenderTarget(
                Builtin::GBuffer::Albedo,
                Builtin::GBuffer::MetallicRoughness,
                Builtin::GBuffer::Emissive);
        }

        if (m_meshShaders) {
            builder->WithShaderResource(Builtin::PerMeshBuffer, Builtin::PrimaryCamera::MeshletBitfield);
            if (m_indirect) {
                builder->WithIndirectArguments(Builtin::PrimaryCamera::IndirectCommandBuffers::Opaque,
                    Builtin::PrimaryCamera::IndirectCommandBuffers::AlphaTest);
            }
        }
    }

    void Setup() override {
        auto& ecsWorld = ECSManager::GetInstance().GetWorld();
        m_opaqueMeshInstancesQuery = ecsWorld.query_builder<Components::ObjectDrawInfo, Components::OpaqueMeshInstances>().cached().cache_kind(flecs::QueryCacheAll).build();
        m_alphaTestMeshInstancesQuery = ecsWorld.query_builder<Components::ObjectDrawInfo, Components::AlphaTestMeshInstances>().cached().cache_kind(flecs::QueryCacheAll).build();

        m_pLinearDepthBuffer = m_resourceRegistryView->Request<PixelBuffer>(Builtin::PrimaryCamera::LinearDepthMap);
        m_pPrimaryDepthBuffer = m_resourceRegistryView->Request<PixelBuffer>(Builtin::PrimaryCamera::DepthTexture);
        m_pNormals = m_resourceRegistryView->Request<PixelBuffer>(Builtin::GBuffer::Normals);
        m_pMotionVectors = m_resourceRegistryView->Request<PixelBuffer>(Builtin::GBuffer::MotionVectors);

        if (m_deferred) {
            m_pAlbedo = m_resourceRegistryView->Request<PixelBuffer>(Builtin::GBuffer::Albedo);
            m_pMetallicRoughness = m_resourceRegistryView->Request<PixelBuffer>(Builtin::GBuffer::MetallicRoughness);
            m_pEmissive = m_resourceRegistryView->Request<PixelBuffer>(Builtin::GBuffer::Emissive);
        }

        if (m_indirect) {
            m_pPrimaryCameraOpaqueIndirectCommandBuffer = m_resourceRegistryView->Request<DynamicGloballyIndexedResource>(Builtin::PrimaryCamera::IndirectCommandBuffers::Opaque);
            m_pPrimaryCameraAlphaTestIndirectCommandBuffer = m_resourceRegistryView->Request<DynamicGloballyIndexedResource>(Builtin::PrimaryCamera::IndirectCommandBuffers::AlphaTest);
        }
        if (m_meshShaders) {
            m_primaryCameraMeshletBitfield = m_resourceRegistryView->Request<DynamicGloballyIndexedResource>(Builtin::PrimaryCamera::MeshletBitfield);
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

    void Cleanup(RenderContext& context) override {
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

        if (context.globalPSOFlags & PSOFlags::PSO_DEFERRED) {
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
        m_opaqueMeshInstancesQuery.each([&](flecs::entity e, Components::ObjectDrawInfo drawInfo, Components::OpaqueMeshInstances opaqueMeshes) {
            auto& meshes = opaqueMeshes.meshInstances;

			commandList.PushConstants(rhi::ShaderStage::AllGraphics, 0, PerObjectRootSignatureIndex, PerObjectBufferIndex, 1, &drawInfo.perObjectCBIndex);

            for (auto& pMesh : meshes) {
                auto& mesh = *pMesh->GetMesh();
                auto& pso = psoManager.GetPrePassPSO(context.globalPSOFlags | mesh.material->GetPSOFlags(), mesh.material->GetBlendState(), m_wireframe);
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

        // Alpha test objects
        m_alphaTestMeshInstancesQuery.each([&](flecs::entity e, Components::ObjectDrawInfo drawInfo, Components::AlphaTestMeshInstances alphaTestMeshes) {
            auto& meshes = alphaTestMeshes.meshInstances;

			commandList.PushConstants(rhi::ShaderStage::AllGraphics, 0, PerObjectRootSignatureIndex, PerObjectBufferIndex, 1, &drawInfo.perObjectCBIndex);

            for (auto& pMesh : meshes) {
                auto& mesh = *pMesh->GetMesh();
                auto& pso = psoManager.GetPrePassPSO(context.globalPSOFlags | PSO_DOUBLE_SIDED | mesh.material->GetPSOFlags(), mesh.material->GetBlendState(), m_wireframe);
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

        // Opaque objects
        m_opaqueMeshInstancesQuery.each([&](flecs::entity e, Components::ObjectDrawInfo drawInfo, Components::OpaqueMeshInstances opaqueMeshes) {
            auto& meshes = opaqueMeshes.meshInstances;

			commandList.PushConstants(rhi::ShaderStage::AllGraphics, 0, PerObjectRootSignatureIndex, PerObjectBufferIndex, 1, &drawInfo.perObjectCBIndex);

            for (auto& pMesh : meshes) {
                auto& mesh = *pMesh->GetMesh();
                auto& pso = psoManager.GetMeshPrePassPSO(context.globalPSOFlags | mesh.material->GetPSOFlags(), mesh.material->GetBlendState(), m_wireframe);
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

        // Alpha test objects
        m_alphaTestMeshInstancesQuery.each([&](flecs::entity e, Components::ObjectDrawInfo drawInfo, Components::AlphaTestMeshInstances alphaTestMeshes) {
            auto& meshes = alphaTestMeshes.meshInstances;

			commandList.PushConstants(rhi::ShaderStage::AllGraphics, 0, PerObjectRootSignatureIndex, PerObjectBufferIndex, 1, &drawInfo.perObjectCBIndex);

            for (auto& pMesh : meshes) {
                auto& mesh = *pMesh->GetMesh();
                auto& pso = psoManager.GetMeshPrePassPSO(context.globalPSOFlags | PSO_DOUBLE_SIDED | mesh.material->GetPSOFlags(), mesh.material->GetBlendState(), m_wireframe);
                BindResourceDescriptorIndices(commandList, pso.GetResourceDescriptorSlots());
				commandList.BindPipeline(pso.GetAPIPipelineState().GetHandle());

                unsigned int perMeshIndices[NumPerMeshRootConstants] = {};
                perMeshIndices[PerMeshBufferIndex] = static_cast<unsigned int>(mesh.GetPerMeshBufferView()->GetOffset() / sizeof(PerMeshCB));
                perMeshIndices[PerMeshInstanceBufferIndex] = static_cast<uint32_t>(pMesh->GetPerMeshInstanceBufferOffset() / sizeof(PerMeshInstanceCB));
				commandList.PushConstants(rhi::ShaderStage::AllGraphics, 0, PerMeshRootSignatureIndex, 0, NumPerMeshRootConstants, perMeshIndices);

                commandList.DispatchMesh(mesh.GetMeshletCount(), 1, 1);
            }
            });
    }

    void ExecuteMeshShaderIndirect(RenderContext& context, rhi::CommandList& commandList) {
        // Mesh shading with ExecuteIndirect
        auto& psoManager = PSOManager::GetInstance();

        auto commandSignature = CommandSignatureManager::GetInstance().GetDispatchMeshCommandSignature();

        // Opaque indirect draws
        auto numOpaque = context.drawStats.numOpaqueDraws;
        if (numOpaque > 0) {

            auto opaqueIndirectBuffer = m_pPrimaryCameraOpaqueIndirectCommandBuffer;
            auto& pso = psoManager.GetMeshPrePassPSO(context.globalPSOFlags, BlendState::BLEND_STATE_OPAQUE, m_wireframe);
            BindResourceDescriptorIndices(commandList, pso.GetResourceDescriptorSlots());
			commandList.BindPipeline(pso.GetAPIPipelineState().GetHandle());

            auto apiResource = opaqueIndirectBuffer->GetAPIResource();
            commandList.ExecuteIndirect(
                commandSignature.GetHandle(),
                apiResource.GetHandle(),
                0,
                apiResource.GetHandle(),
                opaqueIndirectBuffer->GetResource()->GetUAVCounterOffset(),
                numOpaque
            );
        }

        // Alpha test indirect draws
        auto numAlphaTest = context.drawStats.numAlphaTestDraws;
        if (numAlphaTest > 0) {

            auto alphaTestIndirectBuffer = m_pPrimaryCameraAlphaTestIndirectCommandBuffer;
            auto& pso = psoManager.GetMeshPrePassPSO(context.globalPSOFlags | PSOFlags::PSO_ALPHA_TEST | PSOFlags::PSO_DOUBLE_SIDED,
                BlendState::BLEND_STATE_MASK, m_wireframe);
            BindResourceDescriptorIndices(commandList, pso.GetResourceDescriptorSlots());
			commandList.BindPipeline(pso.GetAPIPipelineState().GetHandle());

            auto apiResource = alphaTestIndirectBuffer->GetAPIResource();
            commandList.ExecuteIndirect(
                commandSignature.GetHandle(),
                apiResource.GetHandle(),
                0,
                apiResource.GetHandle(),
                alphaTestIndirectBuffer->GetResource()->GetUAVCounterOffset(),
                numAlphaTest
			);
        }
    }

private:

    flecs::query<Components::ObjectDrawInfo, Components::OpaqueMeshInstances> m_opaqueMeshInstancesQuery;
    flecs::query<Components::ObjectDrawInfo, Components::AlphaTestMeshInstances> m_alphaTestMeshInstancesQuery;
    bool m_wireframe;
    bool m_meshShaders;
    bool m_indirect;
    bool m_clearGbuffer = true;
    bool m_deferred = false;

    std::shared_ptr<PixelBuffer> m_pLinearDepthBuffer;
    std::shared_ptr<PixelBuffer> m_pPrimaryDepthBuffer;
    std::shared_ptr<PixelBuffer> m_pNormals;
    std::shared_ptr<PixelBuffer> m_pMotionVectors;
    std::shared_ptr<PixelBuffer> m_pAlbedo;
    std::shared_ptr<PixelBuffer> m_pMetallicRoughness;
    std::shared_ptr<PixelBuffer> m_pEmissive;

    std::shared_ptr<DynamicGloballyIndexedResource> m_primaryCameraMeshletBitfield = nullptr;
    std::shared_ptr<DynamicGloballyIndexedResource> m_pPrimaryCameraOpaqueIndirectCommandBuffer;
    std::shared_ptr<DynamicGloballyIndexedResource> m_pPrimaryCameraAlphaTestIndirectCommandBuffer;

    std::function<bool()> getImageBasedLightingEnabled;
    std::function<bool()> getPunctualLightingEnabled;
    std::function<bool()> getShadowsEnabled;
};