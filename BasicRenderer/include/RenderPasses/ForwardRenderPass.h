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

        if (m_meshShaders) {
            builder->WithShaderResource(MESH_RESOURCE_IDFENTIFIERS, Builtin::PrimaryCamera::MeshletBitfield);
            if (m_indirect) { // Indirect draws only supported with mesh shaders, becasue I'm not writing a separate codepath for doing it the bad way
                builder->WithIndirectArguments(Builtin::PrimaryCamera::IndirectCommandBuffers::Opaque,
                    Builtin::PrimaryCamera::IndirectCommandBuffers::AlphaTest);
            }
        }
    }

    void Setup() override {
        auto& ecsWorld = ECSManager::GetInstance().GetWorld();
        m_opaqueMeshInstancesQuery = ecsWorld.query_builder<Components::ObjectDrawInfo, Components::OpaqueMeshInstances>().cached().cache_kind(flecs::QueryCacheAll).build();
        m_alphaTestMeshInstancesQuery = ecsWorld.query_builder<Components::ObjectDrawInfo, Components::AlphaTestMeshInstances>().cached().cache_kind(flecs::QueryCacheAll).build();

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

        if (m_indirect) {
            m_primaryCameraOpaqueIndirectCommandBuffer = m_resourceRegistryView->Request<DynamicGloballyIndexedResource>(Builtin::PrimaryCamera::IndirectCommandBuffers::Opaque);
            m_primaryCameraAlphaTestIndirectCommandBuffer = m_resourceRegistryView->Request<DynamicGloballyIndexedResource>(Builtin::PrimaryCamera::IndirectCommandBuffers::AlphaTest);
        }
        if (m_meshShaders)
            m_primaryCameraMeshletBitfield = m_resourceRegistryView->Request<DynamicGloballyIndexedResource>(Builtin::PrimaryCamera::MeshletBitfield);
    }

    PassReturn Execute(RenderContext& context) override {
        auto& psoManager = PSOManager::GetInstance();

        auto& commandList = context.commandList;

        SetupCommonState(context, commandList);
        SetCommonRootConstants(context, commandList);


        if (m_meshShaders) {
            if (m_indirect) {
                // Indirect drawing
                ExecuteMeshShaderIndirect(context, static_cast<ID3D12GraphicsCommandList7*>(commandList));
            }
            else {
                // Regular mesh shader drawing
                ExecuteMeshShader(context, static_cast<ID3D12GraphicsCommandList7*>(commandList));
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
    void SetupCommonState(RenderContext& context, ID3D12GraphicsCommandList* commandList) {
        ID3D12DescriptorHeap* descriptorHeaps[] = {
            context.textureDescriptorHeap,
            context.samplerDescriptorHeap,
        };
        commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

        CD3DX12_VIEWPORT viewport(0.0f, 0.0f, static_cast<float>(context.renderResolution.x), static_cast<float>(context.renderResolution.y));
        CD3DX12_RECT scissorRect(0, 0, context.renderResolution.x, context.renderResolution.y);
        commandList->RSSetViewports(1, &viewport);
        commandList->RSSetScissorRects(1, &scissorRect);

        // Render targets
        //CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(context.rtvHeap->GetCPUDescriptorHandleForHeapStart(), context.frameIndex, context.rtvDescriptorSize);
        auto rtvHandle = m_pHDRTarget->GetRTVInfo(0).cpuHandle;
        auto dsvHandle = m_pPrimaryDepthBuffer->GetDSVInfo(0).cpuHandle;
        commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

        commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        // Root signature
        auto& psoManager = PSOManager::GetInstance();
        auto rootSignature = psoManager.GetRootSignature();
        commandList->SetGraphicsRootSignature(rootSignature.Get());
    }

    void SetCommonRootConstants(RenderContext& context, ID3D12GraphicsCommandList* commandList) {
        unsigned int settings[NumSettingsRootConstants] = { getShadowsEnabled(), getPunctualLightingEnabled(), m_gtaoEnabled };
        commandList->SetGraphicsRoot32BitConstants(SettingsRootSignatureIndex, NumSettingsRootConstants, &settings, 0);

        if (m_meshShaders) {
            unsigned int misc[NumMiscUintRootConstants] = {};
            misc[MESHLET_CULLING_BITFIELD_BUFFER_SRV_DESCRIPTOR_INDEX] = m_primaryCameraMeshletBitfield->GetResource()->GetSRVInfo(0).index;
            commandList->SetGraphicsRoot32BitConstants(MiscUintRootSignatureIndex, NumMiscUintRootConstants, &misc, 0);
        }
    }

    void ExecuteRegular(RenderContext& context, ID3D12GraphicsCommandList7* commandList) {
        // Regular forward rendering using DrawIndexedInstanced
        auto& psoManager = PSOManager::GetInstance();

        auto& meshManager = context.meshManager;

        // Opaque objects
        m_opaqueMeshInstancesQuery.each([&](flecs::entity e, Components::ObjectDrawInfo drawInfo, Components::OpaqueMeshInstances opaqueMeshes) {
            auto& meshes = opaqueMeshes.meshInstances;

            commandList->SetGraphicsRoot32BitConstants(PerObjectRootSignatureIndex, 1, &drawInfo.perObjectCBIndex, PerObjectBufferIndex);

            for (auto& pMesh : meshes) {
                auto& mesh = *pMesh->GetMesh();
                auto pso = psoManager.GetPSO(context.globalPSOFlags | mesh.material->GetPSOFlags(), mesh.material->GetBlendState(), m_wireframe);
                BindResourceDescriptorIndices(commandList, pso.GetResourceDescriptorSlots());
                commandList->SetPipelineState(pso.GetAPIPipelineState());

                unsigned int perMeshIndices[NumPerMeshRootConstants] = {};
                perMeshIndices[PerMeshBufferIndex] = static_cast<uint32_t>(mesh.GetPerMeshBufferView()->GetOffset() / sizeof(PerMeshCB));
                perMeshIndices[PerMeshInstanceBufferIndex] = pMesh->GetPerMeshInstanceBufferOffset() / sizeof(PerMeshInstanceCB);
                commandList->SetGraphicsRoot32BitConstants(PerMeshRootSignatureIndex, NumPerMeshRootConstants, perMeshIndices, 0);

                D3D12_INDEX_BUFFER_VIEW indexBufferView = mesh.GetIndexBufferView();
                commandList->IASetIndexBuffer(&indexBufferView);
                commandList->DrawIndexedInstanced(mesh.GetIndexCount(), 1, 0, 0, 0);
            }
            });

        // Alpha test objects
        m_alphaTestMeshInstancesQuery.each([&](flecs::entity e, Components::ObjectDrawInfo drawInfo, Components::AlphaTestMeshInstances alphaTestMeshes) {
            auto& meshes = alphaTestMeshes.meshInstances;

            commandList->SetGraphicsRoot32BitConstants(PerObjectRootSignatureIndex, 1, &drawInfo.perObjectCBIndex, PerObjectBufferIndex);

            for (auto& pMesh : meshes) {
                auto& mesh = *pMesh->GetMesh();
                auto pso = psoManager.GetPSO(context.globalPSOFlags | PSO_DOUBLE_SIDED | mesh.material->GetPSOFlags(), mesh.material->GetBlendState(), m_wireframe);
                BindResourceDescriptorIndices(commandList, pso.GetResourceDescriptorSlots());
                commandList->SetPipelineState(pso.GetAPIPipelineState());

                unsigned int perMeshIndices[NumPerMeshRootConstants] = {};
                perMeshIndices[PerMeshBufferIndex] = static_cast<uint32_t>(mesh.GetPerMeshBufferView()->GetOffset() / sizeof(PerMeshCB));
                perMeshIndices[PerMeshInstanceBufferIndex] = pMesh->GetPerMeshInstanceBufferOffset() / sizeof(PerMeshInstanceCB);
                commandList->SetGraphicsRoot32BitConstants(PerMeshRootSignatureIndex, NumPerMeshRootConstants, perMeshIndices, 0);

                D3D12_INDEX_BUFFER_VIEW indexBufferView = mesh.GetIndexBufferView();
                commandList->IASetIndexBuffer(&indexBufferView);
                commandList->DrawIndexedInstanced(mesh.GetIndexCount(), 1, 0, 0, 0);
            }
            });
    }

    void ExecuteMeshShader(RenderContext& context, ID3D12GraphicsCommandList7* commandList) {
        // Mesh shading path using DispatchMesh
        auto& psoManager = PSOManager::GetInstance();

        auto& meshManager = context.meshManager;

        // Opaque objects
        m_opaqueMeshInstancesQuery.each([&](flecs::entity e, Components::ObjectDrawInfo drawInfo, Components::OpaqueMeshInstances opaqueMeshes) {
            auto& meshes = opaqueMeshes.meshInstances;

            commandList->SetGraphicsRoot32BitConstants(PerObjectRootSignatureIndex, 1, &drawInfo.perObjectCBIndex, PerObjectBufferIndex);

            for (auto& pMesh : meshes) {
                auto& mesh = *pMesh->GetMesh();
                auto pso = psoManager.GetMeshPSO(context.globalPSOFlags | mesh.material->GetPSOFlags(), mesh.material->GetBlendState(), m_wireframe);
                BindResourceDescriptorIndices(commandList, pso.GetResourceDescriptorSlots());
                commandList->SetPipelineState(pso.GetAPIPipelineState());

                unsigned int perMeshIndices[NumPerMeshRootConstants] = {};
                perMeshIndices[PerMeshBufferIndex] = static_cast<uint32_t>(mesh.GetPerMeshBufferView()->GetOffset() / sizeof(PerMeshCB));
                perMeshIndices[PerMeshInstanceBufferIndex] = pMesh->GetPerMeshInstanceBufferOffset() / sizeof(PerMeshInstanceCB);
                commandList->SetGraphicsRoot32BitConstants(PerMeshRootSignatureIndex, NumPerMeshRootConstants, perMeshIndices, 0);

                // Mesh shaders use DispatchMesh
                commandList->DispatchMesh(mesh.GetMeshletCount(), 1, 1);
            }
            });

        // Alpha test objects
        m_alphaTestMeshInstancesQuery.each([&](flecs::entity e, Components::ObjectDrawInfo drawInfo, Components::AlphaTestMeshInstances alphaTestMeshes) {
            auto& meshes = alphaTestMeshes.meshInstances;

            commandList->SetGraphicsRoot32BitConstants(PerObjectRootSignatureIndex, 1, &drawInfo.perObjectCBIndex, PerObjectBufferIndex);

            for (auto& pMesh : meshes) {
                auto& mesh = *pMesh->GetMesh();
                auto pso = psoManager.GetMeshPSO(context.globalPSOFlags | PSO_DOUBLE_SIDED | mesh.material->GetPSOFlags(), mesh.material->GetBlendState(), m_wireframe);
                BindResourceDescriptorIndices(commandList, pso.GetResourceDescriptorSlots());
                commandList->SetPipelineState(pso.GetAPIPipelineState());

                unsigned int perMeshIndices[NumPerMeshRootConstants] = {};
                perMeshIndices[PerMeshBufferIndex] = static_cast<uint32_t>(mesh.GetPerMeshBufferView()->GetOffset() / sizeof(PerMeshCB));
                perMeshIndices[PerMeshInstanceBufferIndex] = pMesh->GetPerMeshInstanceBufferOffset() / sizeof(PerMeshInstanceCB);
                commandList->SetGraphicsRoot32BitConstants(PerMeshRootSignatureIndex, NumPerMeshRootConstants, perMeshIndices, 0);

                commandList->DispatchMesh(mesh.GetMeshletCount(), 1, 1);
            }
            });
    }

    void ExecuteMeshShaderIndirect(RenderContext& context, ID3D12GraphicsCommandList7* commandList) {
        // Mesh shading with ExecuteIndirect
        auto& psoManager = PSOManager::GetInstance();

        auto& meshManager = context.meshManager;
        auto commandSignature = CommandSignatureManager::GetInstance().GetDispatchMeshCommandSignature();

        // Opaque indirect draws
        auto numOpaque = context.drawStats.numOpaqueDraws;
        if (numOpaque > 0) {

            auto opaqueIndirectBuffer = m_primaryCameraOpaqueIndirectCommandBuffer;
            auto pso = psoManager.GetMeshPSO(context.globalPSOFlags, BlendState::BLEND_STATE_OPAQUE, m_wireframe);
            BindResourceDescriptorIndices(commandList, pso.GetResourceDescriptorSlots());
            commandList->SetPipelineState(pso.GetAPIPipelineState());

            auto apiResource = opaqueIndirectBuffer->GetAPIResource();
            commandList->ExecuteIndirect(
                commandSignature,
                numOpaque,
                apiResource, 0,
                apiResource,
                opaqueIndirectBuffer->GetResource()->GetUAVCounterOffset()
            );
        }

        // Alpha test indirect draws
        auto numAlphaTest = context.drawStats.numAlphaTestDraws;
        if (numAlphaTest > 0) {

            auto alphaTestIndirectBuffer = m_primaryCameraAlphaTestIndirectCommandBuffer;
            auto pso = psoManager.GetMeshPSO(context.globalPSOFlags | PSOFlags::PSO_ALPHA_TEST | PSOFlags::PSO_DOUBLE_SIDED,
                BlendState::BLEND_STATE_MASK, m_wireframe);
            BindResourceDescriptorIndices(commandList, pso.GetResourceDescriptorSlots());
            commandList->SetPipelineState(pso.GetAPIPipelineState());

            auto apiResource = alphaTestIndirectBuffer->GetAPIResource();
            commandList->ExecuteIndirect(
                commandSignature,
                numAlphaTest,
                apiResource, 0,
                apiResource,
                alphaTestIndirectBuffer->GetResource()->GetUAVCounterOffset()
            );
        }
    }

private:
    flecs::query<Components::ObjectDrawInfo, Components::OpaqueMeshInstances> m_opaqueMeshInstancesQuery;
    flecs::query<Components::ObjectDrawInfo, Components::AlphaTestMeshInstances> m_alphaTestMeshInstancesQuery;
    bool m_wireframe;
    bool m_meshShaders;
    bool m_indirect;
    unsigned int m_aoTextureDescriptorIndex;
    bool m_gtaoEnabled = true;
    bool m_clusteredLightingEnabled = true;
    bool m_deferred = false;

    std::shared_ptr<DynamicGloballyIndexedResource> m_primaryCameraMeshletBitfield = nullptr;
    std::shared_ptr<DynamicGloballyIndexedResource> m_primaryCameraMeshletCullingBitfieldBuffer = nullptr;
    std::shared_ptr<DynamicGloballyIndexedResource> m_primaryCameraOpaqueIndirectCommandBuffer = nullptr;
    std::shared_ptr<DynamicGloballyIndexedResource> m_primaryCameraAlphaTestIndirectCommandBuffer = nullptr;
    std::shared_ptr<PixelBuffer> m_pPrimaryDepthBuffer = nullptr;
    std::shared_ptr<PixelBuffer> m_pHDRTarget = nullptr;

    std::function<bool()> getImageBasedLightingEnabled;
    std::function<bool()> getPunctualLightingEnabled;
    std::function<bool()> getShadowsEnabled;
};