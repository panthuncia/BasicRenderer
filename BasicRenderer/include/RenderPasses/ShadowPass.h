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

    void Setup(const ResourceRegistryView& resourceRegistryView) override {
        auto& ecsWorld = ECSManager::GetInstance().GetWorld();
        lightQuery = ecsWorld.query_builder<Components::Light, Components::LightViewInfo, Components::DepthMap>().without<Components::SkipShadowPass>().cached().cache_kind(flecs::QueryCacheAll).build();
        m_opaqueMeshInstancesQuery = ecsWorld.query_builder<Components::ObjectDrawInfo, Components::OpaqueMeshInstances>().without<Components::SkipShadowPass>().cached().cache_kind(flecs::QueryCacheAll).build();
        m_alphaTestMeshInstancesQuery = ecsWorld.query_builder<Components::ObjectDrawInfo, Components::AlphaTestMeshInstances>().without<Components::SkipShadowPass>().cached().cache_kind(flecs::QueryCacheAll).build();
        m_blendMeshInstancesQuery = ecsWorld.query_builder<Components::ObjectDrawInfo, Components::BlendMeshInstances>().without<Components::SkipShadowPass>().cached().cache_kind(flecs::QueryCacheAll).build();
    
        m_normalMatrixBufferIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::NormalMatrixBuffer)->GetSRVInfo(0).index;
        m_postSkinningVertexBufferIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::PostSkinningVertices)->GetSRVInfo(0).index;
        m_perObjectBufferIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::PerObjectBuffer)->GetSRVInfo(0).index;
        m_cameraBufferIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::CameraBuffer)->GetSRVInfo(0).index;
        m_perMeshInstanceBufferIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::PerMeshInstanceBuffer)->GetSRVInfo(0).index;
        m_perMeshBufferIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::PerMeshBuffer)->GetSRVInfo(0).index;

        m_lightBufferSRVIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::Light::InfoBuffer)->GetSRVInfo(0).index;
        m_pointLightCubemapBufferSRVIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::Light::PointLightCubemapBuffer)->GetSRVInfo(0).index;
        m_spotLightMatrixBufferSRVIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::Light::SpotLightMatrixBuffer)->GetSRVInfo(0).index;
        m_directionalLightCascadeBufferSRVIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::Light::DirectionalLightCascadeBuffer)->GetSRVInfo(0).index;
    
        if (m_meshShaders) {
            m_meshletOffsetBufferIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::MeshResources::MeshletOffsets)->GetSRVInfo(0).index;
            m_meshletVertexIndexBufferIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::MeshResources::MeshletVertexIndices)->GetSRVInfo(0).index;
            m_meshletTriangleBufferIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::MeshResources::MeshletTriangles)->GetSRVInfo(0).index;
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
        auto& meshManager = context.meshManager;
        auto& objectManager = context.objectManager;
        auto& cameraManager = context.cameraManager;

        unsigned int staticBufferIndices[NumStaticBufferRootConstants] = {};
        staticBufferIndices[NormalMatrixBufferDescriptorIndex] = m_normalMatrixBufferIndex;
        staticBufferIndices[PostSkinningVertexBufferDescriptorIndex] = m_postSkinningVertexBufferIndex;
        staticBufferIndices[MeshletBufferDescriptorIndex] = m_meshletOffsetBufferIndex;
        staticBufferIndices[MeshletVerticesBufferDescriptorIndex] = m_meshletVertexIndexBufferIndex;
        staticBufferIndices[MeshletTrianglesBufferDescriptorIndex] = m_meshletTriangleBufferIndex;
        staticBufferIndices[PerObjectBufferDescriptorIndex] = m_perObjectBufferIndex;
        staticBufferIndices[CameraBufferDescriptorIndex] = m_cameraBufferIndex;
        staticBufferIndices[PerMeshInstanceBufferDescriptorIndex] = m_perMeshInstanceBufferIndex;
        staticBufferIndices[PerMeshBufferDescriptorIndex] = m_perMeshBufferIndex;

        staticBufferIndices[LightBufferDescriptorIndex] = m_lightBufferSRVIndex;
        staticBufferIndices[PointLightCubemapBufferDescriptorIndex] = m_pointLightCubemapBufferSRVIndex;
        staticBufferIndices[SpotLightMatrixBufferDescriptorIndex] = m_spotLightMatrixBufferSRVIndex;
        staticBufferIndices[DirectionalLightCascadeBufferDescriptorIndex] = m_directionalLightCascadeBufferSRVIndex;

        commandList->SetGraphicsRoot32BitConstants(StaticBufferRootSignatureIndex, NumStaticBufferRootConstants, &staticBufferIndices, 0);
    }

    void ExecuteRegular(RenderContext& context, ID3D12GraphicsCommandList* commandList) {
		auto& psoManager = PSOManager::GetInstance();
        auto drawObjects = [&]() {

            // Opaque objects
            m_opaqueMeshInstancesQuery.each([&](flecs::entity e, Components::ObjectDrawInfo drawInfo, Components::OpaqueMeshInstances opaqueMeshes) {
                auto& meshes = opaqueMeshes.meshInstances;

                commandList->SetGraphicsRoot32BitConstants(PerObjectRootSignatureIndex, 1, &drawInfo.perObjectCBIndex, PerObjectBufferIndex);

                for (auto& pMesh : meshes) {
                    auto& mesh = *pMesh->GetMesh();
                    auto pso = psoManager.GetShadowPSO(PSOFlags::PSO_SHADOW | mesh.material->m_psoFlags, mesh.material->m_blendState);
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
                    auto pso = psoManager.GetShadowPSO(PSOFlags::PSO_SHADOW | PSO_DOUBLE_SIDED | mesh.material->m_psoFlags, mesh.material->m_blendState);
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

            // Blend objects
            if (m_drawBlendShadows) {
                m_blendMeshInstancesQuery.each([&](flecs::entity e, Components::ObjectDrawInfo drawInfo, Components::BlendMeshInstances blendMeshes) {
                    auto& meshes = blendMeshes.meshInstances;

                    commandList->SetGraphicsRoot32BitConstants(PerObjectRootSignatureIndex, 1, &drawInfo.perObjectCBIndex, PerObjectBufferIndex);

                    for (auto& pMesh : meshes) {
                        auto& mesh = *pMesh->GetMesh();
                        auto pso = psoManager.GetShadowPSO(PSOFlags::PSO_SHADOW | mesh.material->m_psoFlags, mesh.material->m_blendState);
                        commandList->SetPipelineState(pso.Get());

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

                int lightInfo[2] = { lightViewInfo.lightBufferIndex, lightViewInfo.viewInfoBufferIndex };
                commandList->SetGraphicsRoot32BitConstants(ViewRootSignatureIndex, NumViewRootConstants, &lightInfo, 0);
                drawObjects();
                break;
            }
            case Components::LightType::Point: {
                int lightViewIndex = lightViewInfo.viewInfoBufferIndex * 6;
                int lightInfo[2] = { lightViewInfo.lightBufferIndex, lightViewIndex };
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
                int lightViewIndex = lightViewInfo.viewInfoBufferIndex * getNumDirectionalLightCascades();
                int lightInfo[2] = { lightViewInfo.lightBufferIndex, lightViewIndex };
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
                    auto pso = psoManager.GetShadowMeshPSO(PSOFlags::PSO_SHADOW | mesh.material->m_psoFlags, mesh.material->m_blendState);
                    commandList->SetPipelineState(pso.Get());

                    unsigned int perMeshIndices[NumPerMeshRootConstants] = {};
                    perMeshIndices[PerMeshBufferIndex] = mesh.GetPerMeshBufferView()->GetOffset() / sizeof(PerMeshCB);
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
                    auto pso = psoManager.GetShadowMeshPSO(PSOFlags::PSO_SHADOW | PSO_DOUBLE_SIDED | mesh.material->m_psoFlags, mesh.material->m_blendState);
                    commandList->SetPipelineState(pso.Get());

                    unsigned int perMeshIndices[NumPerMeshRootConstants] = {};
                    perMeshIndices[PerMeshBufferIndex] = mesh.GetPerMeshBufferView()->GetOffset() / sizeof(PerMeshCB);
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
                    auto pso = psoManager.GetShadowMeshPSO(PSOFlags::PSO_SHADOW | mesh.material->m_psoFlags, mesh.material->m_blendState);
                    commandList->SetPipelineState(pso.Get());

                    unsigned int perMeshIndices[NumPerMeshRootConstants] = {};
                    perMeshIndices[PerMeshBufferIndex] = mesh.GetPerMeshBufferView()->GetOffset() / sizeof(PerMeshCB);
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

                unsigned int variableBufferIndices[NumVariableBufferRootConstants] = {};
                variableBufferIndices[MeshletCullingBitfieldBufferDescriptorIndex] = lightViewInfo.renderViews[0].meshletBitfieldBuffer->GetResource()->GetSRVInfo(0).index;
				commandList->SetGraphicsRoot32BitConstants(VariableBufferRootSignatureIndex, NumVariableBufferRootConstants, &variableBufferIndices, 0);

                drawObjects();
                break;
            }
            case Components::LightType::Point: {
                int lightViewIndex = lightViewInfo.viewInfoBufferIndex * 6;
                int lightInfo[2] = { lightViewInfo.lightBufferIndex, lightViewIndex };
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

                    unsigned int variableBufferIndices[NumVariableBufferRootConstants] = {};
					variableBufferIndices[MeshletCullingBitfieldBufferDescriptorIndex] = lightViewInfo.renderViews[i].meshletBitfieldBuffer->GetResource()->GetSRVInfo(0).index;
					commandList->SetGraphicsRoot32BitConstants(VariableBufferRootSignatureIndex, NumVariableBufferRootConstants, &variableBufferIndices, 0);

                    lightViewIndex += 1;
                    drawObjects();
                }
                break;
            }
            case Components::LightType::Directional: {
                int lightViewIndex = lightViewInfo.viewInfoBufferIndex * getNumDirectionalLightCascades();
                int lightInfo[2] = { lightViewInfo.lightBufferIndex, lightViewIndex };
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

                    unsigned int variableBufferIndices[NumVariableBufferRootConstants] = {};
					variableBufferIndices[MeshletCullingBitfieldBufferDescriptorIndex] = lightViewInfo.renderViews[i].meshletBitfieldBuffer->GetResource()->GetSRVInfo(0).index;
					commandList->SetGraphicsRoot32BitConstants(VariableBufferRootSignatureIndex, NumVariableBufferRootConstants, &variableBufferIndices, 0);

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
                commandList->SetPipelineState(pso.Get());
                commandList->ExecuteIndirect(commandSignature, numOpaque, opaqueIndirectCommandBuffer, 0, opaqueIndirectCommandBuffer, opaqueCommandCounterOffset);
            }

            auto numAlphaTest = context.drawStats.numAlphaTestDraws;
            if (numAlphaTest != 0) {
                auto pso = psoManager.GetShadowMeshPSO(PSOFlags::PSO_SHADOW | PSOFlags::PSO_ALPHA_TEST | PSO_DOUBLE_SIDED, BlendState::BLEND_STATE_MASK, false);
                commandList->SetPipelineState(pso.Get());
                commandList->ExecuteIndirect(commandSignature, numAlphaTest, alphaTestIndirectCommandBuffer, 0, alphaTestIndirectCommandBuffer, alphaTestCommandCounterOffset);
            }

            auto numBlend = context.drawStats.numBlendDraws;
            if (numBlend != 0) {
                auto pso = psoManager.GetShadowMeshPSO(PSOFlags::PSO_SHADOW | PSOFlags::PSO_BLEND, BlendState::BLEND_STATE_BLEND, false);
                commandList->SetPipelineState(pso.Get());
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
                int lightInfo[2] = { lightViewInfo.lightBufferIndex, lightViewInfo.viewInfoBufferIndex };
                commandList->SetGraphicsRoot32BitConstants(ViewRootSignatureIndex, NumViewRootConstants, &lightInfo, 0);
                auto& views = lightViewInfo.renderViews;
                auto& opaque = views[0].indirectCommandBuffers.opaqueIndirectCommandBuffer;
				auto& alphaTest = views[0].indirectCommandBuffers.alphaTestIndirectCommandBuffer;
				auto& blend = views[0].indirectCommandBuffers.blendIndirectCommandBuffer;

                unsigned int variableBufferIndices[NumVariableBufferRootConstants] = {};
                variableBufferIndices[MeshletCullingBitfieldBufferDescriptorIndex] = lightViewInfo.renderViews[0].meshletBitfieldBuffer->GetResource()->GetSRVInfo(0).index;
                commandList->SetGraphicsRoot32BitConstants(VariableBufferRootSignatureIndex, NumVariableBufferRootConstants, &variableBufferIndices, 0);

                drawObjects(opaque->GetAPIResource(), alphaTest->GetAPIResource(), blend->GetAPIResource(), opaque->GetResource()->GetUAVCounterOffset(), alphaTest->GetResource()->GetUAVCounterOffset(), blend->GetResource()->GetUAVCounterOffset());
                break;
            }
            case Components::LightType::Point: {
                int lightViewIndex = lightViewInfo.viewInfoBufferIndex * 6;
                int lightInfo[2] = { lightViewInfo.lightBufferIndex, lightViewIndex };
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

                    unsigned int variableBufferIndices[NumVariableBufferRootConstants] = {};
                    variableBufferIndices[MeshletCullingBitfieldBufferDescriptorIndex] = lightViewInfo.renderViews[i].meshletBitfieldBuffer->GetResource()->GetSRVInfo(0).index;
                    commandList->SetGraphicsRoot32BitConstants(VariableBufferRootSignatureIndex, NumVariableBufferRootConstants, &variableBufferIndices, 0);

                    drawObjects(opaque->GetAPIResource(), alphaTest->GetAPIResource(), blend->GetAPIResource(), opaque->GetResource()->GetUAVCounterOffset(), alphaTest->GetResource()->GetUAVCounterOffset(), blend->GetResource()->GetUAVCounterOffset());
                }
                break;
            }
            case Components::LightType::Directional: {
                //int lightIndex = light->GetCurrentLightBufferIndex();
                int lightViewIndex = lightViewInfo.viewInfoBufferIndex * getNumDirectionalLightCascades();
                int lightInfo[2] = { lightViewInfo.lightBufferIndex, lightViewIndex };
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

                    unsigned int variableBufferIndices[NumVariableBufferRootConstants] = {};
                    variableBufferIndices[MeshletCullingBitfieldBufferDescriptorIndex] = lightViewInfo.renderViews[i].meshletBitfieldBuffer->GetResource()->GetSRVInfo(0).index;
                    commandList->SetGraphicsRoot32BitConstants(VariableBufferRootSignatureIndex, NumVariableBufferRootConstants, &variableBufferIndices, 0);

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

    int m_normalMatrixBufferIndex = -1;
    int m_postSkinningVertexBufferIndex = -1;
    int m_meshletOffsetBufferIndex = -1;
    int m_meshletVertexIndexBufferIndex = -1;
    int m_meshletTriangleBufferIndex = -1;
    int m_perObjectBufferIndex = -1;
    int m_cameraBufferIndex = -1;
    int m_perMeshInstanceBufferIndex = -1;
    int m_perMeshBufferIndex = -1;

    int m_lightBufferSRVIndex = -1;
    int m_pointLightCubemapBufferSRVIndex = -1;
    int m_spotLightMatrixBufferSRVIndex = -1;
    int m_directionalLightCascadeBufferSRVIndex = -1;

    std::function<uint8_t()> getNumDirectionalLightCascades;
    std::function<uint16_t()> getShadowResolution;
};
