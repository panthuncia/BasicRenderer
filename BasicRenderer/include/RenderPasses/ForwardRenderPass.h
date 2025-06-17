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

    void Setup(const ResourceRegistryView& resourceRegistryView) override {
        auto& ecsWorld = ECSManager::GetInstance().GetWorld();
        m_opaqueMeshInstancesQuery = ecsWorld.query_builder<Components::ObjectDrawInfo, Components::OpaqueMeshInstances>().cached().cache_kind(flecs::QueryCacheAll).build();
		m_alphaTestMeshInstancesQuery = ecsWorld.query_builder<Components::ObjectDrawInfo, Components::AlphaTestMeshInstances>().cached().cache_kind(flecs::QueryCacheAll).build();
        
        // Setup resources
        m_normalMatrixBufferDescriptorIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::NormalMatrixBuffer)->GetSRVInfo(0).index;
		m_postSkinningVertexBufferDescriptorIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::PostSkinningVertices)->GetSRVInfo(0).index;
		m_perObjectBufferDescriptorIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::PerObjectBuffer)->GetSRVInfo(0).index;
		m_cameraBufferDescriptorIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::CameraBuffer)->GetSRVInfo(0).index;
		m_perMeshInstanceBufferDescriptorIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::PerMeshInstanceBuffer)->GetSRVInfo(0).index;
        m_perMeshBufferDescriptorIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::PerMeshBuffer)->GetSRVInfo(0).index;
        
        if (m_clusteredLightingEnabled) {
            m_lightClusterBufferDescriptorIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::Light::ClusterBuffer)->GetSRVInfo(0).index;
            m_lightPagesBufferDescriptorIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::Light::PagesBuffer)->GetSRVInfo(0).index;
        }

        if (m_meshShaders) {
            m_meshletOffsetBufferDescriptorIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::MeshResources::MeshletOffsets)->GetSRVInfo(0).index;
            m_meshletVertexIndexBufferDescriptorIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::MeshResources::MeshletVertexIndices)->GetSRVInfo(0).index;
            m_meshletTriangleBufferDescriptorIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::MeshResources::MeshletTriangles)->GetSRVInfo(0).index;
        }

        m_activeLightIndicesBufferSRVIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::Light::ActiveLightIndices)->GetSRVInfo(0).index;
        m_lightBufferSRVIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::Light::InfoBuffer)->GetSRVInfo(0).index;
        m_pointLightCubemapBufferSRVIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::Light::PointLightCubemapBuffer)->GetSRVInfo(0).index;
        m_spotLightMatrixBufferSRVIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::Light::SpotLightMatrixBuffer)->GetSRVInfo(0).index;
        m_directionalLightCascadeBufferSRVIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::Light::DirectionalLightCascadeBuffer)->GetSRVInfo(0).index;

        m_environmentBufferDescriptorIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::Environment::InfoBuffer)->GetSRVInfo(0).index;
        
        if (m_indirect) {
            if (m_meshShaders)
                m_primaryCameraMeshletCullingBitfieldBuffer = resourceRegistryView.Request<DynamicGloballyIndexedResource>(Builtin::PrimaryCamera::MeshletBitfield);
            m_primaryCameraOpaqueIndirectCommandBuffer = resourceRegistryView.Request<DynamicGloballyIndexedResource>(Builtin::PrimaryCamera::IndirectCommandBuffers::Opaque);
            m_primaryCameraAlphaTestIndirectCommandBuffer = resourceRegistryView.Request<DynamicGloballyIndexedResource>(Builtin::PrimaryCamera::IndirectCommandBuffers::AlphaTest);
        }
    }

    void DeclareResourceUsages(RenderPassBuilder* builder) override {
        //builder->WithShaderResource(Builtin::CameraBuffer, Builtin::Environment::PrefilteredCubemapsGroup)
        //    .WithRenderTarget(Builtin::Color::HDRColorTarget);
    };


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

        CD3DX12_VIEWPORT viewport(0.0f, 0.0f, context.xRes, context.yRes);
        CD3DX12_RECT scissorRect(0, 0, context.xRes, context.yRes);
        commandList->RSSetViewports(1, &viewport);
        commandList->RSSetScissorRects(1, &scissorRect);

        // Render targets
        //CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(context.rtvHeap->GetCPUDescriptorHandleForHeapStart(), context.frameIndex, context.rtvDescriptorSize);
		auto rtvHandle = context.pHDRTarget->GetRTVInfo(0).cpuHandle;
        auto dsvHandle = context.pPrimaryDepthBuffer->GetDSVInfo(0).cpuHandle;
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

        auto& meshManager = context.meshManager;
        auto& objectManager = context.objectManager;
        auto& cameraManager = context.cameraManager;
        auto& lightManager = context.lightManager;

        unsigned int staticBufferIndices[NumStaticBufferRootConstants] = {};
        staticBufferIndices[NormalMatrixBufferDescriptorIndex] = m_normalMatrixBufferDescriptorIndex;
        staticBufferIndices[PostSkinningVertexBufferDescriptorIndex] = m_postSkinningVertexBufferDescriptorIndex;
        staticBufferIndices[MeshletBufferDescriptorIndex] = m_meshletOffsetBufferDescriptorIndex;
        staticBufferIndices[MeshletVerticesBufferDescriptorIndex] = m_meshletVertexIndexBufferDescriptorIndex;
        staticBufferIndices[MeshletTrianglesBufferDescriptorIndex] = m_meshletTriangleBufferDescriptorIndex;
        staticBufferIndices[PerObjectBufferDescriptorIndex] = m_perObjectBufferDescriptorIndex;
        staticBufferIndices[CameraBufferDescriptorIndex] = m_cameraBufferDescriptorIndex;
        staticBufferIndices[PerMeshInstanceBufferDescriptorIndex] = m_perMeshInstanceBufferDescriptorIndex;
		staticBufferIndices[PerMeshBufferDescriptorIndex] = m_perMeshBufferDescriptorIndex;
        staticBufferIndices[AOTextureDescriptorIndex] = m_aoTextureDescriptorIndex;

        staticBufferIndices[ActiveLightIndicesBufferDescriptorIndex] = m_activeLightIndicesBufferSRVIndex;
        staticBufferIndices[LightBufferDescriptorIndex] = m_lightBufferSRVIndex;
        staticBufferIndices[PointLightCubemapBufferDescriptorIndex] = m_pointLightCubemapBufferSRVIndex;
        staticBufferIndices[SpotLightMatrixBufferDescriptorIndex] = m_spotLightMatrixBufferSRVIndex;
        staticBufferIndices[DirectionalLightCascadeBufferDescriptorIndex] = m_directionalLightCascadeBufferSRVIndex;

		staticBufferIndices[EnvironmentBufferDescriptorIndex] = m_environmentBufferDescriptorIndex;

        commandList->SetGraphicsRoot32BitConstants(StaticBufferRootSignatureIndex, NumStaticBufferRootConstants, &staticBufferIndices, 0);

		unsigned int lightClusterInfo[NumLightClusterRootConstants] = {};
        lightClusterInfo[LightClusterBufferDescriptorIndex] = m_lightClusterBufferDescriptorIndex;
        lightClusterInfo[LightPagesBufferDescriptorIndex] = m_lightPagesBufferDescriptorIndex;
		commandList->SetGraphicsRoot32BitConstants(LightClusterRootSignatureIndex, NumLightClusterRootConstants, &lightClusterInfo, 0);

        if (m_indirect && m_meshShaders) {
            unsigned int variableBufferIndices[NumVariableBufferRootConstants] = {};
            variableBufferIndices[MeshletCullingBitfieldBufferDescriptorIndex] = m_primaryCameraMeshletCullingBitfieldBuffer->GetResource()->GetSRVInfo(0).index;
        }

    }

    void ExecuteRegular(RenderContext& context, ID3D12GraphicsCommandList* commandList) {
        // Regular forward rendering using DrawIndexedInstanced
        auto& psoManager = PSOManager::GetInstance();

        auto& meshManager = context.meshManager;

        // Opaque objects
        m_opaqueMeshInstancesQuery.each([&](flecs::entity e, Components::ObjectDrawInfo drawInfo, Components::OpaqueMeshInstances opaqueMeshes) {
            auto& meshes = opaqueMeshes.meshInstances;

            commandList->SetGraphicsRoot32BitConstants(PerObjectRootSignatureIndex, 1, &drawInfo.perObjectCBIndex, PerObjectBufferIndex);

            for (auto& pMesh : meshes) {
                auto& mesh = *pMesh->GetMesh();
                auto pso = psoManager.GetPSO(context.globalPSOFlags | mesh.material->m_psoFlags, mesh.material->m_blendState, m_wireframe);
                commandList->SetPipelineState(pso.Get());

                unsigned int perMeshIndices[NumPerMeshRootConstants] = {};
                perMeshIndices[PerMeshBufferIndex] = mesh.GetPerMeshBufferView()->GetOffset() / sizeof(PerMeshCB);
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
                auto pso = psoManager.GetPSO(context.globalPSOFlags | PSO_DOUBLE_SIDED | mesh.material->m_psoFlags, mesh.material->m_blendState, m_wireframe);
                commandList->SetPipelineState(pso.Get());

                unsigned int perMeshIndices[NumPerMeshRootConstants] = {};
                perMeshIndices[PerMeshBufferIndex] = mesh.GetPerMeshBufferView()->GetOffset() / sizeof(PerMeshCB);
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
                auto pso = psoManager.GetMeshPSO(context.globalPSOFlags | mesh.material->m_psoFlags, mesh.material->m_blendState, m_wireframe);
                commandList->SetPipelineState(pso.Get());

                unsigned int perMeshIndices[NumPerMeshRootConstants] = {};
                perMeshIndices[PerMeshBufferIndex] = mesh.GetPerMeshBufferView()->GetOffset() / sizeof(PerMeshCB);
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
                auto pso = psoManager.GetMeshPSO(context.globalPSOFlags | PSO_DOUBLE_SIDED | mesh.material->m_psoFlags, mesh.material->m_blendState, m_wireframe);
                commandList->SetPipelineState(pso.Get());

                unsigned int perMeshIndices[NumPerMeshRootConstants] = {};
                perMeshIndices[PerMeshBufferIndex] = mesh.GetPerMeshBufferView()->GetOffset() / sizeof(PerMeshCB);
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
            commandList->SetPipelineState(pso.Get());

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
            commandList->SetPipelineState(pso.Get());

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

    // Resources
    int m_normalMatrixBufferDescriptorIndex = -1;
	int m_postSkinningVertexBufferDescriptorIndex = -1;
	int m_meshletOffsetBufferDescriptorIndex = -1;
	int m_meshletVertexIndexBufferDescriptorIndex = -1;
	int m_meshletTriangleBufferDescriptorIndex = -1;
	int m_perObjectBufferDescriptorIndex = -1;
	int m_cameraBufferDescriptorIndex = -1;
	int m_perMeshInstanceBufferDescriptorIndex = -1;
	int m_perMeshBufferDescriptorIndex = -1;
	int m_lightClusterBufferDescriptorIndex = -1;
	int m_lightPagesBufferDescriptorIndex = -1;

    int m_activeLightIndicesBufferSRVIndex = -1;
    int m_lightBufferSRVIndex = -1;
    int m_pointLightCubemapBufferSRVIndex = -1;
    int m_spotLightMatrixBufferSRVIndex = -1;
    int m_directionalLightCascadeBufferSRVIndex = -1;

	int m_environmentBufferDescriptorIndex = -1;

    std::shared_ptr<DynamicGloballyIndexedResource> m_primaryCameraMeshletCullingBitfieldBuffer = nullptr;

    std::shared_ptr<DynamicGloballyIndexedResource> m_primaryCameraOpaqueIndirectCommandBuffer = nullptr;
    std::shared_ptr<DynamicGloballyIndexedResource> m_primaryCameraAlphaTestIndirectCommandBuffer = nullptr;

    std::function<bool()> getImageBasedLightingEnabled;
    std::function<bool()> getPunctualLightingEnabled;
    std::function<bool()> getShadowsEnabled;
};
