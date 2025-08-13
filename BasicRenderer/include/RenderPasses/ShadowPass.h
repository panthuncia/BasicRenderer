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

class ShadowPass : public RenderPass {
public:
    ShadowPass(bool wireframe, bool meshShaders, bool indirect, bool drawBlendShadows, bool clearDepths)
        : m_wireframe(wireframe),
        m_meshShaders(meshShaders),
        m_indirect(indirect),
        m_drawBlendShadows(drawBlendShadows),
        m_clearDepths(clearDepths) {
        auto& settingsManager = SettingsManager::GetInstance();
        getNumDirectionalLightCascades = SettingsManager::GetInstance().getSettingGetter<uint8_t>("numDirectionalLightCascades");
        getShadowResolution = SettingsManager::GetInstance().getSettingGetter<uint16_t>("shadowResolution");
    }

    ~ShadowPass() {
    }

    void DeclareResourceUsages(RenderPassBuilder* builder) {
        builder->WithShaderResource(Builtin::PerObjectBuffer,
            Builtin::NormalMatrixBuffer,
            Builtin::PerMeshBuffer,
            Builtin::PerMeshInstanceBuffer,
            Builtin::PostSkinningVertices,
            Builtin::CameraBuffer,
            Builtin::Light::ViewResourceGroup,
            Builtin::Light::InfoBuffer,
            Builtin::Light::PointLightCubemapBuffer,
            Builtin::Light::DirectionalLightCascadeBuffer,
            Builtin::Light::SpotLightMatrixBuffer)
            .WithRenderTarget(Subresources(Builtin::Shadows::LinearShadowMaps, Mip{ 0, 1 }))
            .WithDepthReadWrite(Builtin::Shadows::ShadowMaps)
            .IsGeometryPass();
        if (m_meshShaders) {
            builder->WithShaderResource(MESH_RESOURCE_IDFENTIFIERS, Builtin::MeshletCullingBitfieldGroup)
                .WithIndirectArguments(Builtin::IndirectCommandBuffers::Opaque,
                    Builtin::IndirectCommandBuffers::AlphaTest);
            if (m_drawBlendShadows) {
                builder->WithIndirectArguments(Builtin::IndirectCommandBuffers::Blend);
            }
        }
    }

    void Setup() override {
        auto& ecsWorld = ECSManager::GetInstance().GetWorld();
        lightQuery = ecsWorld.query_builder<Components::Light, Components::LightViewInfo, Components::DepthMap>().without<Components::SkipShadowPass>().cached().cache_kind(flecs::QueryCacheAll).build();
        m_opaqueMeshInstancesQuery = ecsWorld.query_builder<Components::ObjectDrawInfo, Components::OpaqueMeshInstances>().without<Components::SkipShadowPass>().cached().cache_kind(flecs::QueryCacheAll).build();
        m_alphaTestMeshInstancesQuery = ecsWorld.query_builder<Components::ObjectDrawInfo, Components::AlphaTestMeshInstances>().without<Components::SkipShadowPass>().cached().cache_kind(flecs::QueryCacheAll).build();
        m_blendMeshInstancesQuery = ecsWorld.query_builder<Components::ObjectDrawInfo, Components::BlendMeshInstances>().without<Components::SkipShadowPass>().cached().cache_kind(flecs::QueryCacheAll).build();

        RegisterSRV(Builtin::NormalMatrixBuffer);
        RegisterSRV(Builtin::PostSkinningVertices);
        RegisterSRV(Builtin::PerObjectBuffer);
        RegisterSRV(Builtin::CameraBuffer);
        RegisterSRV(Builtin::PerMeshInstanceBuffer);
        RegisterSRV(Builtin::PerMeshBuffer);

        RegisterSRV(Builtin::Light::InfoBuffer);
        RegisterSRV(Builtin::Light::PointLightCubemapBuffer);
        RegisterSRV(Builtin::Light::SpotLightMatrixBuffer);
        RegisterSRV(Builtin::Light::DirectionalLightCascadeBuffer);

        if (m_meshShaders) {
            RegisterSRV(Builtin::MeshResources::MeshletOffsets);
            RegisterSRV(Builtin::MeshResources::MeshletVertexIndices);
            RegisterSRV(Builtin::MeshResources::MeshletTriangles);
        }
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
            context.textureDescriptorHeap, // The texture descriptor heap
            context.samplerDescriptorHeap, // The sampler descriptor heap
        };

        commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

        auto shadowRes = getShadowResolution();
        CD3DX12_VIEWPORT viewport = CD3DX12_VIEWPORT(0.0f, 0.0f, shadowRes, shadowRes);
        CD3DX12_RECT scissorRect = CD3DX12_RECT(0, 0, shadowRes, shadowRes);
        commandList->RSSetViewports(1, &viewport);
        commandList->RSSetScissorRects(1, &scissorRect);

        commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        auto& psoManager = PSOManager::GetInstance();
        commandList->SetGraphicsRootSignature(psoManager.GetRootSignature().Get());
    }

    void SetCommonRootConstants(RenderContext& context, ID3D12GraphicsCommandList* commandList) {

    }

    void ExecuteRegular(RenderContext& context, ID3D12GraphicsCommandList7* commandList) {
        auto& psoManager = PSOManager::GetInstance();
        auto drawObjects = [&]() {

            // Opaque objects
            m_opaqueMeshInstancesQuery.each([&](flecs::entity e, Components::ObjectDrawInfo drawInfo, Components::OpaqueMeshInstances opaqueMeshes) {
                auto& meshes = opaqueMeshes.meshInstances;

                commandList->SetGraphicsRoot32BitConstants(PerObjectRootSignatureIndex, 1, &drawInfo.perObjectCBIndex, PerObjectBufferIndex);

                for (auto& pMesh : meshes) {
                    auto& mesh = *pMesh->GetMesh();
                    auto pso = psoManager.GetShadowPSO(PSOFlags::PSO_SHADOW | mesh.material->GetPSOFlags(), mesh.material->GetBlendState());
                    BindResourceDescriptorIndices(commandList, pso.GetResourceDescriptorSlots());
                    commandList->SetPipelineState(pso.GetAPIPipelineState());

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
                    auto pso = psoManager.GetShadowPSO(PSOFlags::PSO_SHADOW | PSO_DOUBLE_SIDED | mesh.material->GetPSOFlags(), mesh.material->GetBlendState());
                    BindResourceDescriptorIndices(commandList, pso.GetResourceDescriptorSlots());
                    commandList->SetPipelineState(pso.GetAPIPipelineState());

                    unsigned int perMeshIndices[NumPerMeshRootConstants] = {};
                    perMeshIndices[PerMeshBufferIndex] = static_cast<uint32_t>(mesh.GetPerMeshBufferView()->GetOffset() / sizeof(PerMeshCB));
                    perMeshIndices[PerMeshInstanceBufferIndex] = static_cast<uint32_t>((pMesh->GetPerMeshInstanceBufferOffset() / sizeof(PerMeshInstanceCB));
                    commandList->SetGraphicsRoot32BitConstants(PerMeshRootSignatureIndex, NumPerMeshRootConstants, perMeshIndices, 0);

                    D3D12_INDEX_BUFFER_VIEW indexBufferView = mesh.GetIndexBufferView();
                    commandList->IASetIndexBuffer(&indexBufferView);

                    commandList->DrawIndexedInstanced(mesh.GetIndexCount(), 1, 0, 0, 0);
                }
                });

            // Blend objects
            if (m_drawBlendShadows) {
                m_blendMeshInstancesQuery.each([&](flecs::entity e, Components::ObjectDrawInfo drawInfo, Components::BlendMeshInstances blendMeshes) {
                    auto& meshes = blendMeshes.meshInstances;

                    commandList->SetGraphicsRoot32BitConstants(PerObjectRootSignatureIndex, 1, &drawInfo.perObjectCBIndex, PerObjectBufferIndex);

                    for (auto& pMesh : meshes) {
                        auto& mesh = *pMesh->GetMesh();
                        auto pso = psoManager.GetShadowPSO(PSOFlags::PSO_SHADOW | mesh.material->GetPSOFlags(), mesh.material->GetBlendState());
                        BindResourceDescriptorIndices(commandList, pso.GetResourceDescriptorSlots());
                        commandList->SetPipelineState(pso.GetAPIPipelineState());

                        auto perMeshIndex = mesh.GetPerMeshBufferView()->GetOffset() / sizeof(PerMeshCB);
                        commandList->SetGraphicsRoot32BitConstants(PerMeshRootSignatureIndex, 1, &perMeshIndex, PerMeshBufferIndex);

                        D3D12_INDEX_BUFFER_VIEW indexBufferView = mesh.GetIndexBufferView();
                        commandList->IASetIndexBuffer(&indexBufferView);

                        commandList->DrawIndexedInstanced(mesh.GetIndexCount(), 1, 0, 0, 0);
                    }
                    });
            }
            };

        lightQuery.each([&](flecs::entity e, Components::Light light, Components::LightViewInfo& lightViewInfo, Components::DepthMap shadowMap) {
            switch (light.type) {
            case Components::LightType::Spot: {
                auto& dsvHandle = shadowMap.depthMap->GetDSVInfo(0).cpuHandle;
                auto& rtvHandle = shadowMap.linearDepthMap->GetRTVInfo(0).cpuHandle;
                commandList->OMSetRenderTargets(1, &rtvHandle, TRUE, &dsvHandle);
                if (m_clearDepths) {
                    commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
                    commandList->ClearRenderTargetView(rtvHandle, shadowMap.linearDepthMap->GetClearColor().data(), 0, nullptr);
                }

                uint32_t lightInfo[2] = { lightViewInfo.lightBufferIndex, lightViewInfo.viewInfoBufferIndex };
                commandList->SetGraphicsRoot32BitConstants(ViewRootSignatureIndex, NumViewRootConstants, &lightInfo, 0);
                drawObjects();
                break;
            }
            case Components::LightType::Point: {
                uint32_t lightViewIndex = lightViewInfo.viewInfoBufferIndex * 6;
                uint32_t lightInfo[2] = { lightViewInfo.lightBufferIndex, lightViewIndex };
                commandList->SetGraphicsRoot32BitConstants(ViewRootSignatureIndex, 1, &lightInfo, 0);
                for (int i = 0; i < 6; i++) {
                    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = shadowMap.depthMap->GetDSVInfo(0, i).cpuHandle;
                    auto& rtvHandle = shadowMap.linearDepthMap->GetRTVInfo(0, i).cpuHandle;
                    commandList->OMSetRenderTargets(1, &rtvHandle, TRUE, &dsvHandle);
                    if (m_clearDepths) {
                        commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
                        commandList->ClearRenderTargetView(rtvHandle, shadowMap.linearDepthMap->GetClearColor().data(), 0, nullptr);
                    }
                    commandList->SetGraphicsRoot32BitConstants(ViewRootSignatureIndex, 1, &lightViewIndex, LightViewIndex);
                    lightViewIndex += 1;
                    drawObjects();
                }
                break;
            }
            case Components::LightType::Directional: {
                uint32_t lightViewIndex = lightViewInfo.viewInfoBufferIndex * getNumDirectionalLightCascades();
                uint32_t lightInfo[2] = { lightViewInfo.lightBufferIndex, lightViewIndex };
                commandList->SetGraphicsRoot32BitConstants(ViewRootSignatureIndex, 1, &lightInfo, 0);
                for (int i = 0; i < getNumDirectionalLightCascades(); i++) {
                    auto& dsvHandle = shadowMap.depthMap->GetDSVInfo(0, i).cpuHandle;
                    auto& rtvHandle = shadowMap.linearDepthMap->GetRTVInfo(0, i).cpuHandle;
                    commandList->OMSetRenderTargets(1, &rtvHandle, TRUE, &dsvHandle);
                    if (m_clearDepths) {
                        commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
                        commandList->ClearRenderTargetView(rtvHandle, shadowMap.linearDepthMap->GetClearColor().data(), 0, nullptr);
                    }
                    commandList->SetGraphicsRoot32BitConstants(ViewRootSignatureIndex, 1, &lightViewIndex, LightViewIndex);
                    lightViewIndex += 1;
                    drawObjects();

                }
            }
            }
            });
    }

    void ExecuteMeshShader(RenderContext& context, ID3D12GraphicsCommandList7* commandList) {
        auto& psoManager = PSOManager::GetInstance();
        auto drawObjects = [&]() {
            // Opaque objects
            m_opaqueMeshInstancesQuery.each([&](flecs::entity e, Components::ObjectDrawInfo drawInfo, Components::OpaqueMeshInstances opaqueMeshes) {
                auto& meshes = opaqueMeshes.meshInstances;

                commandList->SetGraphicsRoot32BitConstants(PerObjectRootSignatureIndex, 1, &drawInfo.perObjectCBIndex, PerObjectBufferIndex);

                for (auto& pMesh : meshes) {
                    auto& mesh = *pMesh->GetMesh();
                    auto pso = psoManager.GetShadowMeshPSO(PSOFlags::PSO_SHADOW | mesh.material->GetPSOFlags(), mesh.material->GetBlendState());
                    BindResourceDescriptorIndices(commandList, pso.GetResourceDescriptorSlots());
                    commandList->SetPipelineState(pso.GetAPIPipelineState());

                    unsigned int perMeshIndices[NumPerMeshRootConstants] = {};
                    perMeshIndices[PerMeshBufferIndex] = static_cast<uint32_t>(mesh.GetPerMeshBufferView()->GetOffset() / sizeof(PerMeshCB));
                    perMeshIndices[PerMeshInstanceBufferIndex] = pMesh->GetPerMeshInstanceBufferOffset() / sizeof(PerMeshInstanceCB);
                    commandList->SetGraphicsRoot32BitConstants(PerMeshRootSignatureIndex, NumPerMeshRootConstants, perMeshIndices, 0);

                    commandList->DispatchMesh(mesh.GetMeshletCount(), 1, 1);
                }
                });

            // Alpha test objects
            m_alphaTestMeshInstancesQuery.each([&](flecs::entity e, Components::ObjectDrawInfo drawInfo, Components::AlphaTestMeshInstances alphaTestMeshes) {
                auto& meshes = alphaTestMeshes.meshInstances;

                commandList->SetGraphicsRoot32BitConstants(PerObjectRootSignatureIndex, 1, &drawInfo.perObjectCBIndex, PerObjectBufferIndex);

                for (auto& pMesh : meshes) {
                    auto& mesh = *pMesh->GetMesh();
                    auto pso = psoManager.GetShadowMeshPSO(PSOFlags::PSO_SHADOW | PSO_DOUBLE_SIDED | mesh.material->GetPSOFlags(), mesh.material->GetBlendState());
                    BindResourceDescriptorIndices(commandList, pso.GetResourceDescriptorSlots());
                    commandList->SetPipelineState(pso.GetAPIPipelineState());

                    unsigned int perMeshIndices[NumPerMeshRootConstants] = {};
                    perMeshIndices[PerMeshBufferIndex] = static_cast<uint32_t>(mesh.GetPerMeshBufferView()->GetOffset() / sizeof(PerMeshCB));
                    perMeshIndices[PerMeshInstanceBufferIndex] = pMesh->GetPerMeshInstanceBufferOffset() / sizeof(PerMeshInstanceCB);
                    commandList->SetGraphicsRoot32BitConstants(PerMeshRootSignatureIndex, NumPerMeshRootConstants, perMeshIndices, 0);

                    commandList->DispatchMesh(mesh.GetMeshletCount(), 1, 1);
                }
                });

            // Blend objects
            m_blendMeshInstancesQuery.each([&](flecs::entity e, Components::ObjectDrawInfo drawInfo, Components::BlendMeshInstances blendMeshes) {
                auto& meshes = blendMeshes.meshInstances;

                commandList->SetGraphicsRoot32BitConstants(PerObjectRootSignatureIndex, 1, &drawInfo.perObjectCBIndex, PerObjectBufferIndex);

                for (auto& pMesh : meshes) {
                    auto& mesh = *pMesh->GetMesh();
                    auto pso = psoManager.GetShadowMeshPSO(PSOFlags::PSO_SHADOW | mesh.material->GetPSOFlags(), mesh.material->GetBlendState());
                    BindResourceDescriptorIndices(commandList, pso.GetResourceDescriptorSlots());
                    commandList->SetPipelineState(pso.GetAPIPipelineState());

                    unsigned int perMeshIndices[NumPerMeshRootConstants] = {};
                    perMeshIndices[PerMeshBufferIndex] = static_cast<uint32_t>(mesh.GetPerMeshBufferView()->GetOffset() / sizeof(PerMeshCB));
                    perMeshIndices[PerMeshInstanceBufferIndex] = pMesh->GetPerMeshInstanceBufferOffset() / sizeof(PerMeshInstanceCB);
                    commandList->SetGraphicsRoot32BitConstants(PerMeshRootSignatureIndex, NumPerMeshRootConstants, perMeshIndices, 0);

                    commandList->DispatchMesh(mesh.GetMeshletCount(), 1, 1);
                }
                });
            };

        lightQuery.each([&](flecs::entity e, Components::Light light, Components::LightViewInfo& lightViewInfo, Components::DepthMap shadowMap) {
            float clear[4] = { 1.0, 0.0, 0.0, 0.0 };
            switch (light.type) {
            case Components::LightType::Spot: {
                auto& dsvHandle = shadowMap.depthMap->GetDSVInfo(0).cpuHandle;
                auto& rtvHandle = shadowMap.linearDepthMap->GetRTVInfo(0).cpuHandle;
                commandList->OMSetRenderTargets(1, &rtvHandle, TRUE, &dsvHandle);
                if (m_clearDepths) {
                    commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
                    commandList->ClearRenderTargetView(rtvHandle, shadowMap.linearDepthMap->GetClearColor().data(), 0, nullptr);
                }

                int lightInfo[2] = { lightViewInfo.lightBufferIndex, lightViewInfo.viewInfoBufferIndex };
                commandList->SetGraphicsRoot32BitConstants(ViewRootSignatureIndex, NumViewRootConstants, &lightInfo, 0);

                unsigned int misc[NumMiscUintRootConstants] = {};
                misc[MESHLET_CULLING_BITFIELD_BUFFER_SRV_DESCRIPTOR_INDEX] = lightViewInfo.renderViews[0].meshletBitfieldBuffer->GetResource()->GetSRVInfo(0).index;
                commandList->SetGraphicsRoot32BitConstants(MiscUintRootSignatureIndex, NumMiscUintRootConstants, &misc, 0);

                drawObjects();
                break;
            }
            case Components::LightType::Point: {
                uint32_t lightViewIndex = lightViewInfo.viewInfoBufferIndex * 6;
                uint32_t lightInfo[2] = { lightViewInfo.lightBufferIndex, lightViewIndex };
                commandList->SetGraphicsRoot32BitConstants(ViewRootSignatureIndex, 1, &lightInfo, 0);
                for (int i = 0; i < 6; i++) {
                    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = shadowMap.depthMap->GetDSVInfo(0, i).cpuHandle;
                    auto& rtvHandle = shadowMap.linearDepthMap->GetRTVInfo(0, i).cpuHandle;
                    commandList->OMSetRenderTargets(1, &rtvHandle, TRUE, &dsvHandle);
                    if (m_clearDepths) {
                        commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
                        commandList->ClearRenderTargetView(rtvHandle, shadowMap.linearDepthMap->GetClearColor().data(), 0, nullptr);
                    }
                    commandList->SetGraphicsRoot32BitConstants(ViewRootSignatureIndex, 1, &lightViewIndex, LightViewIndex);

                    unsigned int misc[NumMiscUintRootConstants] = {};
                    misc[MESHLET_CULLING_BITFIELD_BUFFER_SRV_DESCRIPTOR_INDEX] = lightViewInfo.renderViews[i].meshletBitfieldBuffer->GetResource()->GetSRVInfo(0).index;
                    commandList->SetGraphicsRoot32BitConstants(MiscUintRootSignatureIndex, NumMiscUintRootConstants, &misc, 0);

                    lightViewIndex += 1;
                    drawObjects();
                }
                break;
            }
            case Components::LightType::Directional: {
                uint32_t lightViewIndex = lightViewInfo.viewInfoBufferIndex * getNumDirectionalLightCascades();
                uint32_t lightInfo[2] = { lightViewInfo.lightBufferIndex, lightViewIndex };
                commandList->SetGraphicsRoot32BitConstants(ViewRootSignatureIndex, 1, &lightInfo, 0);
                for (int i = 0; i < getNumDirectionalLightCascades(); i++) {
                    auto& dsvHandle = shadowMap.depthMap->GetDSVInfo(0, i).cpuHandle;
                    auto& rtvHandle = shadowMap.linearDepthMap->GetRTVInfo(0, i).cpuHandle;
                    commandList->OMSetRenderTargets(1, &rtvHandle, TRUE, &dsvHandle);
                    if (m_clearDepths) {
                        commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
                        commandList->ClearRenderTargetView(rtvHandle, shadowMap.linearDepthMap->GetClearColor().data(), 0, nullptr);
                    }
                    commandList->SetGraphicsRoot32BitConstants(ViewRootSignatureIndex, 1, &lightViewIndex, LightViewIndex);

                    unsigned int misc[NumMiscUintRootConstants] = {};
                    misc[MESHLET_CULLING_BITFIELD_BUFFER_SRV_DESCRIPTOR_INDEX] = lightViewInfo.renderViews[i].meshletBitfieldBuffer->GetResource()->GetSRVInfo(0).index;
                    commandList->SetGraphicsRoot32BitConstants(MiscUintRootSignatureIndex, NumMiscUintRootConstants, &misc, 0);

                    lightViewIndex += 1;
                    drawObjects();

                }
            }
            }
            });
    }

    void ExecuteMeshShaderIndirect(RenderContext& context, ID3D12GraphicsCommandList7* commandList) {
        auto commandSignature = CommandSignatureManager::GetInstance().GetDispatchMeshCommandSignature();
        auto& psoManager = PSOManager::GetInstance();

        auto drawObjects = [&](ID3D12Resource* opaqueIndirectCommandBuffer, ID3D12Resource* alphaTestIndirectCommandBuffer, ID3D12Resource* blendIndirectCommandBuffer, size_t opaqueCommandCounterOffset, size_t alphaTestCommandCounterOffset, size_t blendIndirectCommandCounterOffset) {
            auto numOpaque = context.drawStats.numOpaqueDraws;
            if (numOpaque != 0) {
                auto pso = psoManager.GetShadowMeshPSO(PSOFlags::PSO_SHADOW, BlendState::BLEND_STATE_OPAQUE, false);
                BindResourceDescriptorIndices(commandList, pso.GetResourceDescriptorSlots());
                commandList->SetPipelineState(pso.GetAPIPipelineState());
                commandList->ExecuteIndirect(commandSignature, numOpaque, opaqueIndirectCommandBuffer, 0, opaqueIndirectCommandBuffer, opaqueCommandCounterOffset);
            }

            auto numAlphaTest = context.drawStats.numAlphaTestDraws;
            if (numAlphaTest != 0) {
                auto pso = psoManager.GetShadowMeshPSO(PSOFlags::PSO_SHADOW | PSOFlags::PSO_ALPHA_TEST | PSO_DOUBLE_SIDED, BlendState::BLEND_STATE_MASK, false);
                BindResourceDescriptorIndices(commandList, pso.GetResourceDescriptorSlots());
                commandList->SetPipelineState(pso.GetAPIPipelineState());
                commandList->ExecuteIndirect(commandSignature, numAlphaTest, alphaTestIndirectCommandBuffer, 0, alphaTestIndirectCommandBuffer, alphaTestCommandCounterOffset);
            }

            auto numBlend = context.drawStats.numBlendDraws;
            if (numBlend != 0) {
                auto pso = psoManager.GetShadowMeshPSO(PSOFlags::PSO_SHADOW | PSOFlags::PSO_BLEND, BlendState::BLEND_STATE_BLEND, false);
                BindResourceDescriptorIndices(commandList, pso.GetResourceDescriptorSlots());
                commandList->SetPipelineState(pso.GetAPIPipelineState());
                commandList->ExecuteIndirect(commandSignature, numBlend, blendIndirectCommandBuffer, 0, blendIndirectCommandBuffer, blendIndirectCommandCounterOffset);
            }
            };

        lightQuery.each([&](flecs::entity e, Components::Light light, Components::LightViewInfo& lightViewInfo, Components::DepthMap shadowMap) {
            float clear[4] = { 1.0, 0.0, 0.0, 0.0 };
            switch (light.type) {
            case Components::LightType::Spot: {
                auto& dsvHandle = shadowMap.depthMap->GetDSVInfo(0).cpuHandle;
                auto& rtvHandle = shadowMap.linearDepthMap->GetRTVInfo(0).cpuHandle;
                commandList->OMSetRenderTargets(1, &rtvHandle, TRUE, &dsvHandle);
                if (m_clearDepths) {
                    commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
                    commandList->ClearRenderTargetView(rtvHandle, shadowMap.linearDepthMap->GetClearColor().data(), 0, nullptr);
                }
                uint32_t lightInfo[2] = { lightViewInfo.lightBufferIndex, lightViewInfo.viewInfoBufferIndex };
                commandList->SetGraphicsRoot32BitConstants(ViewRootSignatureIndex, NumViewRootConstants, &lightInfo, 0);
                auto& views = lightViewInfo.renderViews;
                auto& opaque = views[0].indirectCommandBuffers.opaqueIndirectCommandBuffer;
                auto& alphaTest = views[0].indirectCommandBuffers.alphaTestIndirectCommandBuffer;
                auto& blend = views[0].indirectCommandBuffers.blendIndirectCommandBuffer;

                unsigned int misc[NumMiscUintRootConstants] = {};
                misc[MESHLET_CULLING_BITFIELD_BUFFER_SRV_DESCRIPTOR_INDEX] = lightViewInfo.renderViews[0].meshletBitfieldBuffer->GetResource()->GetSRVInfo(0).index;
                commandList->SetGraphicsRoot32BitConstants(MiscUintRootSignatureIndex, NumMiscUintRootConstants, &misc, 0);

                drawObjects(opaque->GetAPIResource(), alphaTest->GetAPIResource(), blend->GetAPIResource(), opaque->GetResource()->GetUAVCounterOffset(), alphaTest->GetResource()->GetUAVCounterOffset(), blend->GetResource()->GetUAVCounterOffset());
                break;
            }
            case Components::LightType::Point: {
                uint32_t lightViewIndex = lightViewInfo.viewInfoBufferIndex * 6;
                uint32_t lightInfo[2] = { lightViewInfo.lightBufferIndex, lightViewIndex };
                commandList->SetGraphicsRoot32BitConstants(ViewRootSignatureIndex, NumViewRootConstants, &lightInfo, 0);
                for (int i = 0; i < 6; i++) {
                    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = shadowMap.depthMap->GetDSVInfo(0, i).cpuHandle;
                    auto& rtvHandle = shadowMap.linearDepthMap->GetRTVInfo(0, i).cpuHandle;
                    commandList->OMSetRenderTargets(1, &rtvHandle, TRUE, &dsvHandle);
                    if (m_clearDepths) {
                        commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
                        commandList->ClearRenderTargetView(rtvHandle, shadowMap.linearDepthMap->GetClearColor().data(), 0, nullptr);
                    }
                    commandList->SetGraphicsRoot32BitConstants(ViewRootSignatureIndex, 1, &lightViewIndex, LightViewIndex);
                    lightViewIndex += 1;
                    auto& views = lightViewInfo.renderViews;
                    auto& opaque = views[i].indirectCommandBuffers.opaqueIndirectCommandBuffer;
                    auto& alphaTest = views[i].indirectCommandBuffers.alphaTestIndirectCommandBuffer;
                    auto& blend = views[i].indirectCommandBuffers.blendIndirectCommandBuffer;

                    unsigned int misc[NumMiscUintRootConstants] = {};
                    misc[MESHLET_CULLING_BITFIELD_BUFFER_SRV_DESCRIPTOR_INDEX] = lightViewInfo.renderViews[i].meshletBitfieldBuffer->GetResource()->GetSRVInfo(0).index;
                    commandList->SetGraphicsRoot32BitConstants(MiscUintRootSignatureIndex, NumMiscUintRootConstants, &misc, 0);

                    drawObjects(opaque->GetAPIResource(), alphaTest->GetAPIResource(), blend->GetAPIResource(), opaque->GetResource()->GetUAVCounterOffset(), alphaTest->GetResource()->GetUAVCounterOffset(), blend->GetResource()->GetUAVCounterOffset());
                }
                break;
            }
            case Components::LightType::Directional: {
                //int lightIndex = light->GetCurrentLightBufferIndex();
                uint32_t lightViewIndex = static_cast<uint32_t>(lightViewInfo.viewInfoBufferIndex * getNumDirectionalLightCascades());
                uint32_t lightInfo[2] = { lightViewInfo.lightBufferIndex, lightViewIndex };
                for (int i = 0; i < getNumDirectionalLightCascades(); i++) {
                    auto& dsvHandle = shadowMap.depthMap->GetDSVInfo(0, i).cpuHandle;
                    auto& rtvHandle = shadowMap.linearDepthMap->GetRTVInfo(0, i).cpuHandle;
                    commandList->OMSetRenderTargets(1, &rtvHandle, TRUE, &dsvHandle);
                    if (m_clearDepths) {
                        commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
                        commandList->ClearRenderTargetView(rtvHandle, shadowMap.linearDepthMap->GetClearColor().data(), 0, nullptr);
                    }
                    commandList->SetGraphicsRoot32BitConstants(ViewRootSignatureIndex, 1, &lightViewIndex, LightViewIndex);
                    lightViewIndex += 1;
                    auto& views = lightViewInfo.renderViews;
                    auto& opaque = views[i].indirectCommandBuffers.opaqueIndirectCommandBuffer;
                    auto& alphaTest = views[i].indirectCommandBuffers.alphaTestIndirectCommandBuffer;
                    auto& blend = views[i].indirectCommandBuffers.blendIndirectCommandBuffer;

                    unsigned int misc[NumMiscUintRootConstants] = {};
                    misc[MESHLET_CULLING_BITFIELD_BUFFER_SRV_DESCRIPTOR_INDEX] = lightViewInfo.renderViews[i].meshletBitfieldBuffer->GetResource()->GetSRVInfo(0).index;
                    commandList->SetGraphicsRoot32BitConstants(MiscUintRootSignatureIndex, NumMiscUintRootConstants, &misc, 0);

                    drawObjects(opaque->GetAPIResource(), alphaTest->GetAPIResource(), blend->GetAPIResource(), opaque->GetResource()->GetUAVCounterOffset(), alphaTest->GetResource()->GetUAVCounterOffset(), blend->GetResource()->GetUAVCounterOffset());
                }
            }
            }
            });
    }

private:
    flecs::query<Components::Light, Components::LightViewInfo, Components::DepthMap> lightQuery;
    flecs::query<Components::ObjectDrawInfo, Components::OpaqueMeshInstances> m_opaqueMeshInstancesQuery;
    flecs::query<Components::ObjectDrawInfo, Components::AlphaTestMeshInstances> m_alphaTestMeshInstancesQuery;
    flecs::query<Components::ObjectDrawInfo, Components::BlendMeshInstances> m_blendMeshInstancesQuery;
    bool m_wireframe;
    bool m_meshShaders;
    bool m_indirect;
    bool m_drawBlendShadows;
    bool m_clearDepths;

    float clear[4] = { 1.0, 0.0, 0.0, 0.0 };

    std::function<uint8_t()> getNumDirectionalLightCascades;
    std::function<uint16_t()> getShadowResolution;
};