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
    ForwardRenderPass(bool wireframe, bool meshShaders, bool indirect, int aoTextureDescriptorIndex, int normalsTextureDescriptorIndex)
        : m_wireframe(wireframe), m_meshShaders(meshShaders), m_indirect(indirect), m_aoTextureDescriptorIndex(aoTextureDescriptorIndex), m_normalsTextureDescriptorIndex(normalsTextureDescriptorIndex) {
        auto& settingsManager = SettingsManager::GetInstance();
        getImageBasedLightingEnabled = settingsManager.getSettingGetter<bool>("enableImageBasedLighting");
        getPunctualLightingEnabled = settingsManager.getSettingGetter<bool>("enablePunctualLighting");
        getShadowsEnabled = settingsManager.getSettingGetter<bool>("enableShadows");
		m_gtaoEnabled = settingsManager.getSettingGetter<bool>("enableGTAO")();
    }

	~ForwardRenderPass() {
	}

    void Setup() override {
        auto& manager = DeviceManager::GetInstance();
        auto& device = manager.GetDevice();
        uint8_t numFramesInFlight = SettingsManager::GetInstance().getSettingGetter<uint8_t>("numFramesInFlight")();

        for (int i = 0; i < numFramesInFlight; i++) {
            ComPtr<ID3D12CommandAllocator> allocator;
            ThrowIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));

            ComPtr<ID3D12GraphicsCommandList7> commandList7;
            ThrowIfFailed(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&commandList7)));
            commandList7->Close();
            m_allocators.push_back(allocator);
            m_commandLists.push_back(commandList7);
        }
        auto& ecsWorld = ECSManager::GetInstance().GetWorld();
        m_opaqueMeshInstancesQuery = ecsWorld.query_builder<Components::ObjectDrawInfo, Components::OpaqueMeshInstances>().cached().cache_kind(flecs::QueryCacheAll).build();
		m_alphaTestMeshInstancesQuery = ecsWorld.query_builder<Components::ObjectDrawInfo, Components::AlphaTestMeshInstances>().cached().cache_kind(flecs::QueryCacheAll).build();
    }

    RenderPassReturn Execute(RenderContext& context) override {
        auto& psoManager = PSOManager::GetInstance();

        // Reset and get the appropriate command list
        auto& allocator = m_allocators[context.frameIndex];
        allocator->Reset();

        ID3D12GraphicsCommandList* commandList = m_commandLists[context.frameIndex].Get();
        commandList->Reset(allocator.Get(), nullptr);

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


        commandList->Close();
        return { { commandList } };
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
        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(context.rtvHeap->GetCPUDescriptorHandleForHeapStart(), context.frameIndex, context.rtvDescriptorSize);
		auto dsvHandle = context.pPrimaryDepthBuffer->GetDSVInfos()[0].cpuHandle;
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
        staticBufferIndices[NormalMatrixBufferDescriptorIndex] = objectManager->GetNormalMatrixBufferSRVIndex();
        staticBufferIndices[PostSkinningVertexBufferDescriptorIndex] = meshManager->GetPostSkinningVertexBufferSRVIndex();
        staticBufferIndices[MeshletBufferDescriptorIndex] = meshManager->GetMeshletOffsetBufferSRVIndex();
        staticBufferIndices[MeshletVerticesBufferDescriptorIndex] = meshManager->GetMeshletIndexBufferSRVIndex();
        staticBufferIndices[MeshletTrianglesBufferDescriptorIndex] = meshManager->GetMeshletTriangleBufferSRVIndex();
        staticBufferIndices[PerObjectBufferDescriptorIndex] = objectManager->GetPerObjectBufferSRVIndex();
        staticBufferIndices[CameraBufferDescriptorIndex] = cameraManager->GetCameraBufferSRVIndex();
        staticBufferIndices[PerMeshInstanceBufferDescriptorIndex] = meshManager->GetPerMeshInstanceBufferSRVIndex();
		staticBufferIndices[PerMeshBufferDescriptorIndex] = meshManager->GetPerMeshBufferSRVIndex();
        staticBufferIndices[AOTextureDescriptorIndex] = m_aoTextureDescriptorIndex;
        staticBufferIndices[NormalsTextureDescriptorIndex] = m_normalsTextureDescriptorIndex;
        commandList->SetGraphicsRoot32BitConstants(StaticBufferRootSignatureIndex, NumStaticBufferRootConstants, &staticBufferIndices, 0);

		unsigned int lightClusterInfo[NumLightClusterRootConstants] = {};
        lightClusterInfo[LightClusterBufferDescriptorIndex] = lightManager->GetClusterBuffer()->GetSRVInfo()[0].index;
        lightClusterInfo[LightPagesBufferDescriptorIndex] = lightManager->GetLightPagesBuffer()->GetSRVInfo()[0].index;
		commandList->SetGraphicsRoot32BitConstants(LightClusterRootSignatureIndex, NumLightClusterRootConstants, &lightClusterInfo, 0);

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

            auto opaqueIndirectBuffer = context.currentScene->GetPrimaryCameraOpaqueIndirectCommandBuffer();
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

            auto alphaTestIndirectBuffer = context.currentScene->GetPrimaryCameraAlphaTestIndirectCommandBuffer();
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
    std::vector<ComPtr<ID3D12GraphicsCommandList7>> m_commandLists;
    std::vector<ComPtr<ID3D12CommandAllocator>> m_allocators;
    bool m_wireframe;
    bool m_meshShaders;
    bool m_indirect;
	int m_aoTextureDescriptorIndex;
	int m_normalsTextureDescriptorIndex;
    bool m_gtaoEnabled = true;

    std::function<bool()> getImageBasedLightingEnabled;
    std::function<bool()> getPunctualLightingEnabled;
    std::function<bool()> getShadowsEnabled;
};
