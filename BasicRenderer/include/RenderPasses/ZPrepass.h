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

class ZPrepass : public RenderPass {
public:
    ZPrepass(std::shared_ptr<GloballyIndexedResource> pNormals,
        std::shared_ptr<GloballyIndexedResource> pAlbedo,
        std::shared_ptr<GloballyIndexedResource> pMetallicRoughness,
		std::shared_ptr<GloballyIndexedResource> pEmissive,
        bool wireframe,
        bool meshShaders,
        bool indirect,
        bool clearGbuffer)
        : m_pNormals(pNormals),
        m_pAlbedo(pAlbedo), 
		m_pMetallicRoughness(pMetallicRoughness),
		m_pEmissive(pEmissive),
        m_wireframe(wireframe),
        m_meshShaders(meshShaders),
        m_indirect(indirect),
        m_clearGbuffer(clearGbuffer){
        auto& settingsManager = SettingsManager::GetInstance();
        getImageBasedLightingEnabled = settingsManager.getSettingGetter<bool>("enableImageBasedLighting");
        getPunctualLightingEnabled = settingsManager.getSettingGetter<bool>("enablePunctualLighting");
        getShadowsEnabled = settingsManager.getSettingGetter<bool>("enableShadows");
    }

    ~ZPrepass() {
    }

    void Setup() override {
        auto& ecsWorld = ECSManager::GetInstance().GetWorld();
        m_opaqueMeshInstancesQuery = ecsWorld.query_builder<Components::ObjectDrawInfo, Components::OpaqueMeshInstances>().cached().cache_kind(flecs::QueryCacheAll).build();
        m_alphaTestMeshInstancesQuery = ecsWorld.query_builder<Components::ObjectDrawInfo, Components::AlphaTestMeshInstances>().cached().cache_kind(flecs::QueryCacheAll).build();
    }

    PassReturn Execute(RenderContext& context) override {
        auto& psoManager = PSOManager::GetInstance();

        auto& commandList = context.commandList;

        // Clear the render target
        if (m_clearGbuffer) {
            auto& rtvHandle = m_pNormals->GetRTVInfo(0).cpuHandle;
            const float clearColor[] = { 0.0f, 0.0f, 0.0f, 1.0f };
            commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

            if (context.globalPSOFlags & PSOFlags::PSO_DEFERRED) {
                auto& rtvHandle2 = m_pAlbedo->GetRTVInfo(0).cpuHandle;
                commandList->ClearRenderTargetView(rtvHandle2, clearColor, 0, nullptr);
                auto& rtvHandle3 = m_pMetallicRoughness->GetRTVInfo(0).cpuHandle;
                commandList->ClearRenderTargetView(rtvHandle3, clearColor, 0, nullptr);
            }

            auto& dsvHandle = context.pPrimaryDepthBuffer->GetDSVInfo(0).cpuHandle;
            commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
        }

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
		auto& dsvHandle = context.pPrimaryDepthBuffer->GetDSVInfo(0).cpuHandle;

        if (context.globalPSOFlags & PSOFlags::PSO_DEFERRED) {
            D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[4] = { m_pNormals->GetRTVInfo(0).cpuHandle, m_pAlbedo->GetRTVInfo(0).cpuHandle, m_pMetallicRoughness->GetRTVInfo(0).cpuHandle, m_pEmissive->GetRTVInfo(0).cpuHandle};
            commandList->OMSetRenderTargets(4, rtvHandles, FALSE, &dsvHandle);
        }
        else {
			D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[1] = { m_pNormals->GetRTVInfo(0).cpuHandle };
			commandList->OMSetRenderTargets(1, rtvHandles, FALSE, &dsvHandle);
        }

        commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        // Root signature
        auto& psoManager = PSOManager::GetInstance();
        auto rootSignature = psoManager.GetRootSignature();
        commandList->SetGraphicsRootSignature(rootSignature.Get());
    }

    void SetCommonRootConstants(RenderContext& context, ID3D12GraphicsCommandList* commandList) {
        unsigned int settings[NumSettingsRootConstants] = { getShadowsEnabled(), getPunctualLightingEnabled() };
        commandList->SetGraphicsRoot32BitConstants(SettingsRootSignatureIndex, NumSettingsRootConstants, &settings, 0);

        auto& meshManager = context.meshManager;
        auto& objectManager = context.objectManager;
        auto& cameraManager = context.cameraManager;

        unsigned int staticBufferIndices[NumStaticBufferRootConstants] = {};
        staticBufferIndices[NormalMatrixBufferDescriptorIndex] = objectManager->GetNormalMatrixBufferSRVIndex();
        staticBufferIndices[PostSkinningVertexBufferDescriptorIndex] = meshManager->GetPostSkinningVertexBufferSRVIndex();
        staticBufferIndices[MeshletBufferDescriptorIndex] = meshManager->GetMeshletOffsetBufferSRVIndex();
        staticBufferIndices[MeshletVerticesBufferDescriptorIndex] = meshManager->GetMeshletIndexBufferSRVIndex();
        staticBufferIndices[MeshletTrianglesBufferDescriptorIndex] = meshManager->GetMeshletTriangleBufferSRVIndex();
        staticBufferIndices[PerObjectBufferDescriptorIndex] = objectManager->GetPerObjectBufferSRVIndex();
        staticBufferIndices[CameraBufferDescriptorIndex] = cameraManager->GetCameraBufferSRVIndex();
        staticBufferIndices[PerMeshInstanceBufferDescriptorIndex] = meshManager->GetPerMeshInstanceBufferSRVIndex();
        staticBufferIndices[PerMeshBufferDescriptorIndex] = meshManager->GetPerMeshBufferSRVIndex();
		staticBufferIndices[NormalsTextureDescriptorIndex] = m_pNormals->GetRTVInfo(0).index;
        commandList->SetGraphicsRoot32BitConstants(StaticBufferRootSignatureIndex, NumStaticBufferRootConstants, &staticBufferIndices, 0);

		unsigned int variableRootConstants[NumVariableBufferRootConstants] = {};
		variableRootConstants[MeshletCullingBitfieldBufferDescriptorIndex] = context.currentScene->GetPrimaryCameraMeshletFrustrumCullingBitfieldBuffer()->GetResource()->GetUAVShaderVisibleInfo(0).index;

		commandList->SetGraphicsRoot32BitConstants(VariableBufferRootSignatureIndex, NumVariableBufferRootConstants, &variableRootConstants, 0);
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
                auto pso = psoManager.GetPrePassPSO(context.globalPSOFlags | mesh.material->m_psoFlags, mesh.material->m_blendState, m_wireframe);
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
                auto pso = psoManager.GetPrePassPSO(context.globalPSOFlags | PSO_DOUBLE_SIDED | mesh.material->m_psoFlags, mesh.material->m_blendState, m_wireframe);
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
                auto pso = psoManager.GetMeshPrePassPSO(context.globalPSOFlags | mesh.material->m_psoFlags, mesh.material->m_blendState, m_wireframe);
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
                auto pso = psoManager.GetMeshPrePassPSO(context.globalPSOFlags | PSO_DOUBLE_SIDED | mesh.material->m_psoFlags, mesh.material->m_blendState, m_wireframe);
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

            auto opaqueIndirectBuffer = context.currentScene->GetPrimaryCameraOpaqueIndirectCommandBuffer();
            auto pso = psoManager.GetMeshPrePassPSO(context.globalPSOFlags, BlendState::BLEND_STATE_OPAQUE, m_wireframe);
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

            auto alphaTestIndirectBuffer = context.currentScene->GetPrimaryCameraAlphaTestIndirectCommandBuffer();
            auto pso = psoManager.GetMeshPrePassPSO(context.globalPSOFlags | PSOFlags::PSO_ALPHA_TEST | PSOFlags::PSO_DOUBLE_SIDED,
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
    std::shared_ptr<GloballyIndexedResource> m_pNormals;
	std::shared_ptr<GloballyIndexedResource> m_pAlbedo;
	std::shared_ptr<GloballyIndexedResource> m_pMetallicRoughness;
    std::shared_ptr<GloballyIndexedResource> m_pEmissive;

    flecs::query<Components::ObjectDrawInfo, Components::OpaqueMeshInstances> m_opaqueMeshInstancesQuery;
    flecs::query<Components::ObjectDrawInfo, Components::AlphaTestMeshInstances> m_alphaTestMeshInstancesQuery;
    bool m_wireframe;
    bool m_meshShaders;
    bool m_indirect;
    bool m_clearGbuffer = true;

    std::function<bool()> getImageBasedLightingEnabled;
    std::function<bool()> getPunctualLightingEnabled;
    std::function<bool()> getShadowsEnabled;
};
