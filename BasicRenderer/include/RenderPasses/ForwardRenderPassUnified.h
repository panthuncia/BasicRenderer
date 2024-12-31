#pragma once

#include <unordered_map>
#include <functional>

#include "RenderPass.h"
#include "PSOManager.h"
#include "RenderContext.h"
#include "RenderableObject.h"
#include "mesh.h"
#include "Scene.h"
#include "Material.h"
#include "SettingsManager.h"

class ForwardRenderPassUnified : public RenderPass {
public:
    ForwardRenderPassUnified(bool wireframe, bool meshShaders, bool indirect)
        : m_wireframe(wireframe), m_meshShaders(meshShaders), m_indirect(indirect) {
        auto& settingsManager = SettingsManager::GetInstance();
        getImageBasedLightingEnabled = settingsManager.getSettingGetter<bool>("enableImageBasedLighting");
        getPunctualLightingEnabled = settingsManager.getSettingGetter<bool>("enablePunctualLighting");
        getShadowsEnabled = settingsManager.getSettingGetter<bool>("enableShadows");
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
    }

    PassReturn Execute(RenderContext& context) override {
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
        CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(context.dsvHeap->GetCPUDescriptorHandleForHeapStart(), context.frameIndex, context.dsvDescriptorSize);
        commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

        commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        // Root signature
        auto& psoManager = PSOManager::GetInstance();
        auto rootSignature = psoManager.GetRootSignature();
        commandList->SetGraphicsRootSignature(rootSignature.Get());
    }

    void SetCommonRootConstants(RenderContext& context, ID3D12GraphicsCommandList* commandList) {
        unsigned int settings[NumSettingsRootConstants] = { getShadowsEnabled(), getPunctualLightingEnabled() };
        commandList->SetGraphicsRoot32BitConstants(SettingsRootSignatureIndex, NumSettingsRootConstants, &settings, 0);

        auto& meshManager = context.currentScene->GetMeshManager();
        auto& objectManager = context.currentScene->GetObjectManager();
        auto& cameraManager = context.currentScene->GetCameraManager();

        unsigned int staticBufferIndices[NumStaticBufferRootConstants] = {};
        staticBufferIndices[NormalMatrixBufferDescriptorIndex] = objectManager->GetNormalMatrixBufferSRVIndex();
        staticBufferIndices[PostSkinningVertexBufferDescriptorIndex] = meshManager->GetPostSkinningVertexBufferSRVIndex();
        staticBufferIndices[MeshletBufferDescriptorIndex] = meshManager->GetMeshletOffsetBufferSRVIndex();
        staticBufferIndices[MeshletVerticesBufferDescriptorIndex] = meshManager->GetMeshletIndexBufferSRVIndex();
        staticBufferIndices[MeshletTrianglesBufferDescriptorIndex] = meshManager->GetMeshletTriangleBufferSRVIndex();
        staticBufferIndices[PerObjectBufferDescriptorIndex] = objectManager->GetPerObjectBufferSRVIndex();
        staticBufferIndices[CameraBufferDescriptorIndex] = cameraManager->GetCameraBufferSRVIndex();

        commandList->SetGraphicsRoot32BitConstants(StaticBufferRootSignatureIndex, NumStaticBufferRootConstants, &staticBufferIndices, 0);
    }

    void ExecuteRegular(RenderContext& context, ID3D12GraphicsCommandList* commandList) {
        // Regular forward rendering using DrawIndexedInstanced
        auto& psoManager = PSOManager::GetInstance();
        unsigned int localPSOFlags = 0;
        if (getImageBasedLightingEnabled()) {
            localPSOFlags |= PSOFlags::PSO_IMAGE_BASED_LIGHTING;
        }

        auto& meshManager = context.currentScene->GetMeshManager();

        // Opaque objects
        unsigned int opaquePerMeshBufferIndex = meshManager->GetOpaquePerMeshBufferSRVIndex();
        commandList->SetGraphicsRoot32BitConstants(VariableBufferRootSignatureIndex, 1, &opaquePerMeshBufferIndex, PerMeshBufferDescriptorIndex);
        for (auto& pair : context.currentScene->GetOpaqueRenderableObjectIDMap()) {
            auto& renderable = pair.second;
            auto& meshes = renderable->GetOpaqueMeshes();

            auto perObjectIndex = renderable->GetCurrentPerObjectCBView()->GetOffset() / sizeof(PerObjectCB);
            commandList->SetGraphicsRoot32BitConstants(PerObjectRootSignatureIndex, 1, &perObjectIndex, PerObjectBufferIndex);

            for (auto& pMesh : meshes) {
                auto& mesh = *pMesh;
                auto pso = psoManager.GetPSO(localPSOFlags | mesh.material->m_psoFlags, mesh.material->m_blendState, m_wireframe);
                commandList->SetPipelineState(pso.Get());

                auto perMeshIndex = mesh.GetPerMeshBufferView()->GetOffset() / sizeof(PerMeshCB);
                commandList->SetGraphicsRoot32BitConstants(PerMeshRootSignatureIndex, 1, &perMeshIndex, PerMeshBufferIndex);

                D3D12_INDEX_BUFFER_VIEW indexBufferView = mesh.GetIndexBufferView();
                commandList->IASetIndexBuffer(&indexBufferView);
                commandList->DrawIndexedInstanced(mesh.GetIndexCount(), 1, 0, 0, 0);
            }
        }

        // Alpha test objects
        unsigned int alphaTestPerMeshBufferIndex = meshManager->GetAlphaTestPerMeshBufferSRVIndex();
        commandList->SetGraphicsRoot32BitConstants(VariableBufferRootSignatureIndex, 1, &alphaTestPerMeshBufferIndex, PerMeshBufferDescriptorIndex);
        for (auto& pair : context.currentScene->GetAlphaTestRenderableObjectIDMap()) {
            auto& renderable = pair.second;
            auto& meshes = renderable->GetAlphaTestMeshes();

            auto perObjectIndex = renderable->GetCurrentPerObjectCBView()->GetOffset() / sizeof(PerObjectCB);
            commandList->SetGraphicsRoot32BitConstants(PerObjectRootSignatureIndex, 1, &perObjectIndex, PerObjectBufferIndex);

            for (auto& pMesh : meshes) {
                auto& mesh = *pMesh;
                auto pso = psoManager.GetPSO(localPSOFlags | mesh.material->m_psoFlags, mesh.material->m_blendState, m_wireframe);
                commandList->SetPipelineState(pso.Get());

                auto perMeshIndex = mesh.GetPerMeshBufferView()->GetOffset() / sizeof(PerMeshCB);
                commandList->SetGraphicsRoot32BitConstants(PerMeshRootSignatureIndex, 1, &perMeshIndex, PerMeshBufferIndex);

                D3D12_INDEX_BUFFER_VIEW indexBufferView = mesh.GetIndexBufferView();
                commandList->IASetIndexBuffer(&indexBufferView);
                commandList->DrawIndexedInstanced(mesh.GetIndexCount(), 1, 0, 0, 0);
            }
        }
    }

    void ExecuteMeshShader(RenderContext& context, ID3D12GraphicsCommandList7* commandList) {
        // Mesh shading path using DispatchMesh
        auto& psoManager = PSOManager::GetInstance();
        unsigned int localPSOFlags = 0;
        if (getImageBasedLightingEnabled()) {
            localPSOFlags |= PSOFlags::PSO_IMAGE_BASED_LIGHTING;
        }

        auto& meshManager = context.currentScene->GetMeshManager();

        // Opaque objects
        unsigned int opaquePerMeshBufferIndex = meshManager->GetOpaquePerMeshBufferSRVIndex();
        commandList->SetGraphicsRoot32BitConstants(VariableBufferRootSignatureIndex, 1, &opaquePerMeshBufferIndex, PerMeshBufferDescriptorIndex);
        for (auto& pair : context.currentScene->GetOpaqueRenderableObjectIDMap()) {
            auto& renderable = pair.second;
            auto& meshes = renderable->GetOpaqueMeshes();

            auto perObjectIndex = renderable->GetCurrentPerObjectCBView()->GetOffset() / sizeof(PerObjectCB);
            commandList->SetGraphicsRoot32BitConstants(PerObjectRootSignatureIndex, 1, &perObjectIndex, PerObjectBufferIndex);

            for (auto& pMesh : meshes) {
                auto& mesh = *pMesh;
                auto pso = psoManager.GetMeshPSO(localPSOFlags | mesh.material->m_psoFlags, mesh.material->m_blendState, m_wireframe);
                commandList->SetPipelineState(pso.Get());

                auto perMeshIndex = mesh.GetPerMeshBufferView()->GetOffset() / sizeof(PerMeshCB);
                commandList->SetGraphicsRoot32BitConstants(PerMeshRootSignatureIndex, 1, &perMeshIndex, PerMeshBufferIndex);

                // Mesh shaders use DispatchMesh
                commandList->DispatchMesh(mesh.GetMeshletCount(), 1, 1);
            }
        }

        // Alpha test objects
        unsigned int alphaTestPerMeshBufferIndex = meshManager->GetAlphaTestPerMeshBufferSRVIndex();
        commandList->SetGraphicsRoot32BitConstants(VariableBufferRootSignatureIndex, 1, &alphaTestPerMeshBufferIndex, PerMeshBufferDescriptorIndex);
        for (auto& pair : context.currentScene->GetAlphaTestRenderableObjectIDMap()) {
            auto& renderable = pair.second;
            auto& meshes = renderable->GetAlphaTestMeshes();

            auto perObjectIndex = renderable->GetCurrentPerObjectCBView()->GetOffset() / sizeof(PerObjectCB);
            commandList->SetGraphicsRoot32BitConstants(PerObjectRootSignatureIndex, 1, &perObjectIndex, PerObjectBufferIndex);

            for (auto& pMesh : meshes) {
                auto& mesh = *pMesh;
                auto pso = psoManager.GetMeshPSO(localPSOFlags | mesh.material->m_psoFlags, mesh.material->m_blendState, m_wireframe);
                commandList->SetPipelineState(pso.Get());

                auto perMeshIndex = mesh.GetPerMeshBufferView()->GetOffset() / sizeof(PerMeshCB);
                commandList->SetGraphicsRoot32BitConstants(PerMeshRootSignatureIndex, 1, &perMeshIndex, PerMeshBufferIndex);

                commandList->DispatchMesh(mesh.GetMeshletCount(), 1, 1);
            }
        }
    }

    void ExecuteMeshShaderIndirect(RenderContext& context, ID3D12GraphicsCommandList7* commandList) {
        // Mesh shading with ExecuteIndirect
        auto& psoManager = PSOManager::GetInstance();
        unsigned int localPSOFlags = 0;
        if (getImageBasedLightingEnabled()) {
            localPSOFlags |= PSOFlags::PSO_IMAGE_BASED_LIGHTING;
        }

        auto& meshManager = context.currentScene->GetMeshManager();
        auto commandSignature = context.currentScene->GetIndirectCommandBufferManager()->GetCommandSignature();

        // Opaque indirect draws
        auto numOpaque = context.currentScene->GetNumOpaqueDraws();
        if (numOpaque > 0) {
            unsigned int opaquePerMeshBufferIndex = meshManager->GetOpaquePerMeshBufferSRVIndex();
            commandList->SetGraphicsRoot32BitConstants(VariableBufferRootSignatureIndex, 1, &opaquePerMeshBufferIndex, PerMeshBufferDescriptorIndex);

            auto opaqueIndirectBuffer = context.currentScene->GetPrimaryCameraOpaqueIndirectCommandBuffer();
            auto pso = psoManager.GetMeshPSO(localPSOFlags, BlendState::BLEND_STATE_OPAQUE, m_wireframe);
            commandList->SetPipelineState(pso.Get());

            auto apiResource = opaqueIndirectBuffer->GetAPIResource();
            commandList->ExecuteIndirect(
                commandSignature.Get(),
                numOpaque,
                apiResource, 0,
                apiResource,
                opaqueIndirectBuffer->GetResource()->GetUAVCounterOffset()
            );
        }

        // Alpha test indirect draws
        auto numAlphaTest = context.currentScene->GetNumAlphaTestDraws();
        if (numAlphaTest > 0) {
            unsigned int alphaTestPerMeshBufferIndex = meshManager->GetAlphaTestPerMeshBufferSRVIndex();
            commandList->SetGraphicsRoot32BitConstants(VariableBufferRootSignatureIndex, 1, &alphaTestPerMeshBufferIndex, PerMeshBufferDescriptorIndex);

            auto alphaTestIndirectBuffer = context.currentScene->GetPrimaryCameraAlphaTestIndirectCommandBuffer();
            auto pso = psoManager.GetMeshPSO(localPSOFlags | PSOFlags::PSO_ALPHA_TEST | PSOFlags::PSO_DOUBLE_SIDED,
                BlendState::BLEND_STATE_MASK, m_wireframe);
            commandList->SetPipelineState(pso.Get());

            auto apiResource = alphaTestIndirectBuffer->GetAPIResource();
            commandList->ExecuteIndirect(
                commandSignature.Get(),
                numAlphaTest,
                apiResource, 0,
                apiResource,
                alphaTestIndirectBuffer->GetResource()->GetUAVCounterOffset()
            );
        }
    }

private:
    std::vector<ComPtr<ID3D12GraphicsCommandList7>> m_commandLists;
    std::vector<ComPtr<ID3D12CommandAllocator>> m_allocators;
    bool m_wireframe;
    bool m_meshShaders;
    bool m_indirect;

    std::function<bool()> getImageBasedLightingEnabled;
    std::function<bool()> getPunctualLightingEnabled;
    std::function<bool()> getShadowsEnabled;
};
