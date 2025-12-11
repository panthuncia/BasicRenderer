#pragma once

#include <unordered_map>
#include <functional>

#include <rhi_debug.h>

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
#include "../../shaders/PerPassRootConstants/amplificationShaderRootConstants.h"

class ShadowPass : public RenderPass {
public:
    ShadowPass(bool wireframe, bool meshShaders, bool indirect, bool drawBlendShadows, bool clearDepths)
        : m_wireframe(wireframe),
        m_meshShaders(meshShaders),
        m_indirect(indirect),
        m_drawBlendShadows(drawBlendShadows),
        m_clearDepths(clearDepths) {
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
            Builtin::PerMaterialDataBuffer,
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
            auto& ecsWorld = ECSManager::GetInstance().GetWorld();
            auto shadowPassEntity = ECSManager::GetInstance().GetRenderPhaseEntity(Engine::Primary::ShadowMapsPass);
            flecs::query<> indirectQuery = ecsWorld.query_builder<>()
                .with<Components::IsIndirectArguments>()
                .with<Components::ParticipatesInPass>(shadowPassEntity) // Query for command lists that participate in this pass
                //.cached().cache_kind(flecs::QueryCacheAll)
                .build();
            builder->WithIndirectArguments(ECSResourceResolver(indirectQuery))
                .WithShaderResource(Builtin::MeshResources::MeshletOffsets,
                    Builtin::MeshResources::MeshletVertexIndices,
                    Builtin::MeshResources::MeshletTriangles);
        }
    }

    void Setup() override {
        auto& ecsWorld = ECSManager::GetInstance().GetWorld();
        lightQuery = ecsWorld.query_builder<Components::Light, Components::LightViewInfo, Components::DepthMap>().without<Components::SkipShadowPass>().cached().cache_kind(flecs::QueryCacheAll).build();
        m_meshInstancesQuery = ecsWorld.query_builder<Components::ObjectDrawInfo, Components::PerPassMeshes>()
			.with<Components::ParticipatesInPass>(ECSManager::GetInstance().GetRenderPhaseEntity(Engine::Primary::ShadowMapsPass))
            .cached().cache_kind(flecs::QueryCacheAll).build();

        RegisterSRV(Builtin::NormalMatrixBuffer);
        RegisterSRV(Builtin::PostSkinningVertices);
        RegisterSRV(Builtin::PerObjectBuffer);
        RegisterSRV(Builtin::CameraBuffer);
        RegisterSRV(Builtin::PerMeshInstanceBuffer);
        RegisterSRV(Builtin::PerMeshBuffer);
        RegisterSRV(Builtin::PerMaterialDataBuffer);

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

        auto shadowRes = getShadowResolution();

		rhi::PassBeginInfo passBeginInfo = {};
		passBeginInfo.width = shadowRes;
		passBeginInfo.height = shadowRes;
		passBeginInfo.debugName = "Shadow Pass";
		commandList.BeginPass(passBeginInfo);

        commandList.SetPrimitiveTopology(rhi::PrimitiveTopology::TriangleList);

		commandList.BindLayout(PSOManager::GetInstance().GetRootSignature().GetHandle());
    }

    void SetCommonRootConstants(RenderContext& context, rhi::CommandList& commandList) {

    }

    void ExecuteRegular(RenderContext& context, rhi::CommandList& commandList) {
        auto& psoManager = PSOManager::GetInstance();
        auto drawObjects = [&]() {

            // Opaque objects
            
            m_meshInstancesQuery.each([&](flecs::entity e, Components::ObjectDrawInfo drawInfo, Components::PerPassMeshes meshInstancesComponent) {
                auto& meshes = meshInstancesComponent.meshesByPass[m_renderPhase.hash];

				commandList.PushConstants(rhi::ShaderStage::AllGraphics, 0, PerObjectRootSignatureIndex, PerObjectBufferIndex, 1, &drawInfo.perObjectCBIndex);

                for (auto& pMesh : meshes) {
                    auto& mesh = *pMesh->GetMesh();
                    auto& pso = psoManager.GetShadowPSO(PSOFlags::PSO_SHADOW | mesh.material->GetPSOFlags(), mesh.material->Technique().compileFlags);
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
            };

        lightQuery.each([&](flecs::entity e, Components::Light light, Components::LightViewInfo& lightViewInfo, Components::DepthMap shadowMap) {
            rhi::debug::Scope scope(commandList, rhi::colors::Blue, e.name().c_str());
            switch (light.type) {
            case Components::LightType::Spot: {
				rhi::PassBeginInfo passBeginInfo = {};
				rhi::DepthAttachment depthAttachment = {};
                depthAttachment.dsv = shadowMap.depthMap->GetDSVInfo(0).slot;
				depthAttachment.depthLoad = m_clearDepths ? rhi::LoadOp::Clear : rhi::LoadOp::Load;
				depthAttachment.depthStore = rhi::StoreOp::Store;
				depthAttachment.clear = shadowMap.depthMap->GetClearColor();
				rhi::ColorAttachment rtvAttachment = {};
				rtvAttachment.rtv = shadowMap.linearDepthMap->GetRTVInfo(0).slot;
				rtvAttachment.clear = shadowMap.linearDepthMap->GetClearColor();
				rtvAttachment.loadOp = m_clearDepths ? rhi::LoadOp::Clear : rhi::LoadOp::Load;
				rtvAttachment.storeOp = rhi::StoreOp::Store;
				passBeginInfo.colors = { &rtvAttachment };
				passBeginInfo.depth = &depthAttachment;
				passBeginInfo.width = lightViewInfo.depthResX;
				passBeginInfo.height = lightViewInfo.depthResY;
				passBeginInfo.debugName = "Shadow Pass - Spot Light";
				commandList.BeginPass(passBeginInfo);


                uint32_t lightInfo[2] = { lightViewInfo.lightBufferIndex, lightViewInfo.viewInfoBufferIndex };
				commandList.PushConstants(rhi::ShaderStage::AllGraphics, 0, ViewRootSignatureIndex, 0, 2, lightInfo);
                drawObjects();
                break;
            }
            case Components::LightType::Point: {
                uint32_t lightViewIndex = lightViewInfo.viewInfoBufferIndex * 6;
                uint32_t lightInfo[2] = { lightViewInfo.lightBufferIndex, lightViewIndex };
				commandList.PushConstants(rhi::ShaderStage::AllGraphics, 0, ViewRootSignatureIndex, 0, 2, lightInfo);
                for (int i = 0; i < 6; i++) {
                    std::string name = "View " + std::to_string(i);
                    rhi::debug::Scope scope(commandList, rhi::colors::Cyan, name.c_str());
					rhi::PassBeginInfo passBeginInfo = {};
					rhi::DepthAttachment depthAttachment = {};
					depthAttachment.dsv = shadowMap.depthMap->GetDSVInfo(0, i).slot;
					depthAttachment.depthLoad = m_clearDepths ? rhi::LoadOp::Clear : rhi::LoadOp::Load;
					depthAttachment.depthStore = rhi::StoreOp::Store;
					depthAttachment.clear = shadowMap.depthMap->GetClearColor();
					passBeginInfo.depth = &depthAttachment;
					rhi::ColorAttachment rtvAttachment = {};
					rtvAttachment.rtv = shadowMap.linearDepthMap->GetRTVInfo(0, i).slot;
					rtvAttachment.clear = shadowMap.linearDepthMap->GetClearColor();
					rtvAttachment.loadOp = m_clearDepths ? rhi::LoadOp::Clear : rhi::LoadOp::Load;
					rtvAttachment.storeOp = rhi::StoreOp::Store;
					passBeginInfo.colors = { &rtvAttachment };
					passBeginInfo.width = lightViewInfo.depthResX;
					passBeginInfo.height = lightViewInfo.depthResY;
					passBeginInfo.debugName = "Shadow Pass - Point Light";
					commandList.BeginPass(passBeginInfo);

					commandList.PushConstants(rhi::ShaderStage::AllGraphics, 0, ViewRootSignatureIndex, LightViewIndex, 1, &lightViewIndex);
                    lightViewIndex += 1;
                    drawObjects();
                }
                break;
            }
            case Components::LightType::Directional: {
                uint32_t lightViewIndex = lightViewInfo.viewInfoBufferIndex * getNumDirectionalLightCascades();
                uint32_t lightInfo[2] = { lightViewInfo.lightBufferIndex, lightViewIndex };
				commandList.PushConstants(rhi::ShaderStage::AllGraphics, 0, ViewRootSignatureIndex, 0, 2, lightInfo);
                for (int i = 0; i < getNumDirectionalLightCascades(); i++) {
                    std::string name = "View " + std::to_string(i);
                    rhi::debug::Scope scope(commandList, rhi::colors::Cyan, name.c_str());
					rhi::PassBeginInfo passBeginInfo = {};
					rhi::DepthAttachment depthAttachment = {};
					depthAttachment.dsv = shadowMap.depthMap->GetDSVInfo(0, i).slot;
					depthAttachment.depthLoad = m_clearDepths ? rhi::LoadOp::Clear : rhi::LoadOp::Load;
					depthAttachment.depthStore = rhi::StoreOp::Store;
					depthAttachment.clear = shadowMap.depthMap->GetClearColor();
					passBeginInfo.depth = &depthAttachment;
					rhi::ColorAttachment rtvAttachment = {};
					rtvAttachment.rtv = shadowMap.linearDepthMap->GetRTVInfo(0, i).slot;
					rtvAttachment.clear = shadowMap.linearDepthMap->GetClearColor();
					rtvAttachment.loadOp = m_clearDepths ? rhi::LoadOp::Clear : rhi::LoadOp::Load;
					rtvAttachment.storeOp = rhi::StoreOp::Store;
					passBeginInfo.colors = { &rtvAttachment };
					passBeginInfo.width = lightViewInfo.depthResX;
					passBeginInfo.height = lightViewInfo.depthResY;
					passBeginInfo.debugName = "Shadow Pass - Directional Light";
					commandList.BeginPass(passBeginInfo);

					commandList.PushConstants(rhi::ShaderStage::AllGraphics, 0, ViewRootSignatureIndex, LightViewIndex, 1, &lightViewIndex);
                    lightViewIndex += 1;
                    drawObjects();

                }
            }
            }
            });
    }

    void ExecuteMeshShader(RenderContext& context, rhi::CommandList& commandList) {
        auto& psoManager = PSOManager::GetInstance();
        auto drawObjects = [&]() {
            // Opaque objects
            m_meshInstancesQuery.each([&](flecs::entity e, Components::ObjectDrawInfo drawInfo, Components::PerPassMeshes meshInstancesComponent) {
                auto& meshes = meshInstancesComponent.meshesByPass[m_renderPhase.hash];

				commandList.PushConstants(rhi::ShaderStage::AllGraphics, 0, PerObjectRootSignatureIndex, PerObjectBufferIndex, 1, &drawInfo.perObjectCBIndex);

                for (auto& pMesh : meshes) {
                    auto& mesh = *pMesh->GetMesh();
                    auto& pso = psoManager.GetShadowMeshPSO(PSOFlags::PSO_SHADOW | mesh.material->GetPSOFlags(), mesh.material->Technique().compileFlags);
                    BindResourceDescriptorIndices(commandList, pso.GetResourceDescriptorSlots());
					commandList.BindPipeline(pso.GetAPIPipelineState().GetHandle());

                    unsigned int perMeshIndices[NumPerMeshRootConstants] = {};
                    perMeshIndices[PerMeshBufferIndex] = static_cast<uint32_t>(mesh.GetPerMeshBufferView()->GetOffset() / sizeof(PerMeshCB));
                    perMeshIndices[PerMeshInstanceBufferIndex] = static_cast<uint32_t>(pMesh->GetPerMeshInstanceBufferOffset() / sizeof(PerMeshInstanceCB));
					commandList.PushConstants(rhi::ShaderStage::AllGraphics, 0, PerMeshRootSignatureIndex, 0, NumPerMeshRootConstants, perMeshIndices);

                    commandList.DispatchMesh(mesh.GetMeshletCount(), 1, 1);
                }
                });
            };

        lightQuery.each([&](flecs::entity e, Components::Light light, Components::LightViewInfo& lightViewInfo, Components::DepthMap shadowMap) {
            rhi::debug::Scope scope(commandList, rhi::colors::Blue, e.name().c_str());
            float clear[4] = { 1.0, 0.0, 0.0, 0.0 };
            switch (light.type) {
            case Components::LightType::Spot: {
				rhi::PassBeginInfo passBeginInfo = {};
				rhi::DepthAttachment depthAttachment = {};
				depthAttachment.dsv = shadowMap.depthMap->GetDSVInfo(0).slot;
				depthAttachment.depthLoad = m_clearDepths ? rhi::LoadOp::Clear : rhi::LoadOp::Load;
				depthAttachment.depthStore = rhi::StoreOp::Store;
				depthAttachment.clear = shadowMap.depthMap->GetClearColor();
				passBeginInfo.depth = &depthAttachment;
				rhi::ColorAttachment rtvAttachment = {};
				rtvAttachment.rtv = shadowMap.linearDepthMap->GetRTVInfo(0).slot;
				rtvAttachment.clear = shadowMap.linearDepthMap->GetClearColor();
				rtvAttachment.loadOp = m_clearDepths ? rhi::LoadOp::Clear : rhi::LoadOp::Load;
				rtvAttachment.storeOp = rhi::StoreOp::Store;
				passBeginInfo.colors = { &rtvAttachment };
				passBeginInfo.width = lightViewInfo.depthResX;
				passBeginInfo.height = lightViewInfo.depthResY;
				passBeginInfo.debugName = "Shadow Pass - Spot Light";
				commandList.BeginPass(passBeginInfo);

                uint32_t lightInfo[2] = { lightViewInfo.lightBufferIndex, lightViewInfo.viewInfoBufferIndex };
				commandList.PushConstants(rhi::ShaderStage::AllGraphics, 0, ViewRootSignatureIndex, 0, 2, lightInfo);

                unsigned int misc[NumMiscUintRootConstants] = {};
                misc[MESHLET_CULLING_BITFIELD_BUFFER_SRV_DESCRIPTOR_INDEX] = context.viewManager->Get(lightViewInfo.viewIDs[0])->gpu.meshletBitfieldBuffer->GetResource()->GetSRVInfo(0).slot.index;
				commandList.PushConstants(rhi::ShaderStage::AllGraphics, 0, MiscUintRootSignatureIndex, 0, NumMiscUintRootConstants, &misc);

                drawObjects();
                break;
            }
            case Components::LightType::Point: {
                uint32_t lightViewIndex = lightViewInfo.viewInfoBufferIndex * 6;
                uint32_t lightInfo[2] = { lightViewInfo.lightBufferIndex, lightViewIndex };
				commandList.PushConstants(rhi::ShaderStage::AllGraphics, 0, ViewRootSignatureIndex, 0, 2, lightInfo);
                for (int i = 0; i < 6; i++) {
					std::string name = "View " + std::to_string(i);
					rhi::debug::Scope scope(commandList, rhi::colors::Cyan, name.c_str());
					rhi::PassBeginInfo passBeginInfo = {};
					rhi::DepthAttachment depthAttachment = {};
					depthAttachment.dsv = shadowMap.depthMap->GetDSVInfo(0, i).slot;
					depthAttachment.depthLoad = m_clearDepths ? rhi::LoadOp::Clear : rhi::LoadOp::Load;
					depthAttachment.depthStore = rhi::StoreOp::Store;
					depthAttachment.clear = shadowMap.depthMap->GetClearColor();
					passBeginInfo.depth = &depthAttachment;
					rhi::ColorAttachment rtvAttachment = {};
					rtvAttachment.rtv = shadowMap.linearDepthMap->GetRTVInfo(0, i).slot;
					rtvAttachment.clear = shadowMap.linearDepthMap->GetClearColor();
					rtvAttachment.loadOp = m_clearDepths ? rhi::LoadOp::Clear : rhi::LoadOp::Load;
					rtvAttachment.storeOp = rhi::StoreOp::Store;
					passBeginInfo.colors = { &rtvAttachment };
					passBeginInfo.width = lightViewInfo.depthResX;
					passBeginInfo.height = lightViewInfo.depthResY;
					passBeginInfo.debugName = "Shadow Pass - Point Light";
					commandList.BeginPass(passBeginInfo);
					commandList.PushConstants(rhi::ShaderStage::AllGraphics, 0, ViewRootSignatureIndex, LightViewIndex, 1, &lightViewIndex);

                    unsigned int misc[NumMiscUintRootConstants] = {};
                    misc[MESHLET_CULLING_BITFIELD_BUFFER_SRV_DESCRIPTOR_INDEX] = context.viewManager->Get(lightViewInfo.viewIDs[i])->gpu.meshletBitfieldBuffer->GetResource()->GetSRVInfo(0).slot.index;
					commandList.PushConstants(rhi::ShaderStage::AllGraphics, 0, MiscUintRootSignatureIndex, 0, NumMiscUintRootConstants, &misc);

                    lightViewIndex += 1;
                    drawObjects();
                }
                break;
            }
            case Components::LightType::Directional: {
                uint32_t lightViewIndex = lightViewInfo.viewInfoBufferIndex * getNumDirectionalLightCascades();
                uint32_t lightInfo[2] = { lightViewInfo.lightBufferIndex, lightViewIndex };
				commandList.PushConstants(rhi::ShaderStage::AllGraphics, 0, ViewRootSignatureIndex, 0, 2, lightInfo);
                for (int i = 0; i < getNumDirectionalLightCascades(); i++) {
                    std::string name = "View " + std::to_string(i);
                    rhi::debug::Scope scope(commandList, rhi::colors::Cyan, name.c_str());
					rhi::PassBeginInfo passBeginInfo = {};
					rhi::DepthAttachment depthAttachment = {};
					depthAttachment.dsv = shadowMap.depthMap->GetDSVInfo(0, i).slot;
					depthAttachment.depthLoad = m_clearDepths ? rhi::LoadOp::Clear : rhi::LoadOp::Load;
					depthAttachment.depthStore = rhi::StoreOp::Store;
					depthAttachment.clear = shadowMap.depthMap->GetClearColor();
					passBeginInfo.depth = &depthAttachment;
					rhi::ColorAttachment rtvAttachment = {};
					rtvAttachment.rtv = shadowMap.linearDepthMap->GetRTVInfo(0, i).slot;
					rtvAttachment.clear = shadowMap.linearDepthMap->GetClearColor();
					rtvAttachment.loadOp = m_clearDepths ? rhi::LoadOp::Clear : rhi::LoadOp::Load;
					rtvAttachment.storeOp = rhi::StoreOp::Store;
					passBeginInfo.colors = { &rtvAttachment };
					passBeginInfo.width = lightViewInfo.depthResX;
					passBeginInfo.height = lightViewInfo.depthResY;
					passBeginInfo.debugName = "Shadow Pass - Directional Light";
					commandList.BeginPass(passBeginInfo);
					commandList.PushConstants(rhi::ShaderStage::AllGraphics, 0, ViewRootSignatureIndex, LightViewIndex, 1, &lightViewIndex);

                    unsigned int misc[NumMiscUintRootConstants] = {};
                    misc[MESHLET_CULLING_BITFIELD_BUFFER_SRV_DESCRIPTOR_INDEX] = context.viewManager->Get(lightViewInfo.viewIDs[i])->gpu.meshletBitfieldBuffer->GetResource()->GetSRVInfo(0).slot.index;
					commandList.PushConstants(rhi::ShaderStage::AllGraphics, 0, MiscUintRootSignatureIndex, 0, NumMiscUintRootConstants, &misc);

                    lightViewIndex += 1;
                    drawObjects();

                }
            }
            }
            });
    }

    void ExecuteMeshShaderIndirect(RenderContext& context, rhi::CommandList& commandList) {
        auto commandSignature = CommandSignatureManager::GetInstance().GetDispatchMeshCommandSignature();
        auto& psoManager = PSOManager::GetInstance();

        auto drawObjects = [&](const rhi::ResourceHandle& indirectCommandBuffer, const MaterialCompileFlags flags, const uint32_t numDraws, const size_t indirectCommandCounterOffset) {

            if (numDraws != 0) {
                auto& pso = psoManager.GetShadowMeshPSO(PSOFlags::PSO_SHADOW, flags, false);
                BindResourceDescriptorIndices(commandList, pso.GetResourceDescriptorSlots());
				commandList.BindPipeline(pso.GetAPIPipelineState().GetHandle());
                commandList.ExecuteIndirect(commandSignature.GetHandle(), indirectCommandBuffer, 0, indirectCommandBuffer, indirectCommandCounterOffset, numDraws);
            }

            };

        lightQuery.each([&](flecs::entity e, Components::Light light, Components::LightViewInfo& lightViewInfo, Components::DepthMap shadowMap) {
            rhi::debug::Scope scope(commandList, rhi::colors::Blue, e.name().c_str());
            float clear[4] = { 1.0, 0.0, 0.0, 0.0 };
            switch (light.type) {
            case Components::LightType::Spot: {
				rhi::PassBeginInfo passBeginInfo = {};
				rhi::DepthAttachment depthAttachment = {};
				depthAttachment.dsv = shadowMap.depthMap->GetDSVInfo(0).slot;
				depthAttachment.depthLoad = m_clearDepths ? rhi::LoadOp::Clear : rhi::LoadOp::Load;
				depthAttachment.depthStore = rhi::StoreOp::Store;
				depthAttachment.clear = shadowMap.depthMap->GetClearColor();
				passBeginInfo.depth = &depthAttachment;
				rhi::ColorAttachment rtvAttachment = {};
				rtvAttachment.rtv = shadowMap.linearDepthMap->GetRTVInfo(0).slot;
				rtvAttachment.clear = shadowMap.linearDepthMap->GetClearColor();
				rtvAttachment.loadOp = m_clearDepths ? rhi::LoadOp::Clear : rhi::LoadOp::Load;
				rtvAttachment.storeOp = rhi::StoreOp::Store;
				passBeginInfo.colors = { &rtvAttachment };
				passBeginInfo.width = lightViewInfo.depthResX;
				passBeginInfo.height = lightViewInfo.depthResY;
				passBeginInfo.debugName = "Shadow Pass - Spot Light";
				commandList.BeginPass(passBeginInfo);
                
                uint32_t lightInfo[2] = { lightViewInfo.lightBufferIndex, lightViewInfo.viewInfoBufferIndex };
				commandList.PushConstants(rhi::ShaderStage::AllGraphics, 0, ViewRootSignatureIndex, 0, 2, lightInfo);
                auto& views = lightViewInfo.viewIDs;
                auto workloads = context.indirectCommandBufferManager->GetBuffersForRenderPhase(views[0], Engine::Primary::ShadowMapsPass);

                unsigned int misc[NumMiscUintRootConstants] = {};
                misc[MESHLET_CULLING_BITFIELD_BUFFER_SRV_DESCRIPTOR_INDEX] = context.viewManager->Get(lightViewInfo.viewIDs[0])->gpu.meshletBitfieldBuffer->GetResource()->GetSRVInfo(0).slot.index;
				commandList.PushConstants(rhi::ShaderStage::AllGraphics, 0, MiscUintRootSignatureIndex, 0, NumMiscUintRootConstants, &misc);

				for (auto& workload : workloads) {
                    drawObjects(workload.second.buffer->GetAPIResource().GetHandle(), workload.first, workload.second.count, workload.second.buffer->GetResource()->GetUAVCounterOffset());
                }
                break;
            }
            case Components::LightType::Point: {
                uint32_t lightViewIndex = lightViewInfo.viewInfoBufferIndex * 6;
                uint32_t lightInfo[2] = { lightViewInfo.lightBufferIndex, lightViewIndex };
				commandList.PushConstants(rhi::ShaderStage::AllGraphics, 0, ViewRootSignatureIndex, 0, 2, lightInfo);
                for (int i = 0; i < 6; i++) {
                    std::string name = "View " + std::to_string(i);
                    rhi::debug::Scope scope(commandList, rhi::colors::Cyan, name.c_str());
					rhi::PassBeginInfo passBeginInfo = {};
					rhi::DepthAttachment depthAttachment = {};
					depthAttachment.dsv = shadowMap.depthMap->GetDSVInfo(0, i).slot;
					depthAttachment.depthLoad = m_clearDepths ? rhi::LoadOp::Clear : rhi::LoadOp::Load;
					depthAttachment.depthStore = rhi::StoreOp::Store;
					depthAttachment.clear = shadowMap.depthMap->GetClearColor();
					passBeginInfo.depth = &depthAttachment;
					rhi::ColorAttachment rtvAttachment = {};
					rtvAttachment.rtv = shadowMap.linearDepthMap->GetRTVInfo(0, i).slot;
					rtvAttachment.clear = shadowMap.linearDepthMap->GetClearColor();
					rtvAttachment.loadOp = m_clearDepths ? rhi::LoadOp::Clear : rhi::LoadOp::Load;
					rtvAttachment.storeOp = rhi::StoreOp::Store;
					passBeginInfo.colors = { &rtvAttachment };
					passBeginInfo.width = lightViewInfo.depthResX;
					passBeginInfo.height = lightViewInfo.depthResY;
					passBeginInfo.debugName = "Shadow Pass - Point Light";
					commandList.BeginPass(passBeginInfo);

					commandList.PushConstants(rhi::ShaderStage::AllGraphics, 0, ViewRootSignatureIndex, LightViewIndex, 1, &lightViewIndex);
                    lightViewIndex += 1;
                    auto& views = lightViewInfo.viewIDs;

                    unsigned int misc[NumMiscUintRootConstants] = {};
                    misc[MESHLET_CULLING_BITFIELD_BUFFER_SRV_DESCRIPTOR_INDEX] = context.viewManager->Get(views[i])->gpu.meshletBitfieldBuffer->GetResource()->GetSRVInfo(0).slot.index;
					commandList.PushConstants(rhi::ShaderStage::AllGraphics, 0, MiscUintRootSignatureIndex, 0, NumMiscUintRootConstants, &misc);

                    for (auto& workload : context.indirectCommandBufferManager->GetBuffersForRenderPhase(views[i], Engine::Primary::ShadowMapsPass)) {
                        drawObjects(workload.second.buffer->GetAPIResource().GetHandle(), workload.first, workload.second.count, workload.second.buffer->GetResource()->GetUAVCounterOffset());
					}
                }
                break;
            }
            case Components::LightType::Directional: {
                uint32_t lightViewIndex = static_cast<uint32_t>(lightViewInfo.viewInfoBufferIndex * getNumDirectionalLightCascades());
                uint32_t lightInfo[2] = { lightViewInfo.lightBufferIndex, lightViewIndex };
                commandList.PushConstants(rhi::ShaderStage::AllGraphics, 0, ViewRootSignatureIndex, 0, 2, lightInfo);
                for (int i = 0; i < getNumDirectionalLightCascades(); i++) {
                    std::string name = "View " + std::to_string(i);
                    rhi::debug::Scope scope(commandList, rhi::colors::Cyan, name.c_str());
					rhi::PassBeginInfo passBeginInfo = {};
					rhi::DepthAttachment depthAttachment = {};
					depthAttachment.dsv = shadowMap.depthMap->GetDSVInfo(0, i).slot;
					depthAttachment.depthLoad = m_clearDepths ? rhi::LoadOp::Clear : rhi::LoadOp::Load;
					depthAttachment.depthStore = rhi::StoreOp::Store;
					depthAttachment.clear = shadowMap.depthMap->GetClearColor();
					passBeginInfo.depth = &depthAttachment;
					rhi::ColorAttachment rtvAttachment = {};
					rtvAttachment.rtv = shadowMap.linearDepthMap->GetRTVInfo(0, i).slot;
					rtvAttachment.clear = shadowMap.linearDepthMap->GetClearColor();
					rtvAttachment.loadOp = m_clearDepths ? rhi::LoadOp::Clear : rhi::LoadOp::Load;
					rtvAttachment.storeOp = rhi::StoreOp::Store;
					passBeginInfo.colors = { &rtvAttachment };
					passBeginInfo.width = lightViewInfo.depthResX;
					passBeginInfo.height = lightViewInfo.depthResY;
					passBeginInfo.debugName = "Shadow Pass - Directional Light";
					commandList.BeginPass(passBeginInfo);

					commandList.PushConstants(rhi::ShaderStage::AllGraphics, 0, ViewRootSignatureIndex, LightViewIndex, 1, &lightViewIndex);
                    lightViewIndex += 1;
                    auto& views = lightViewInfo.viewIDs;

                    unsigned int misc[NumMiscUintRootConstants] = {};
                    misc[MESHLET_CULLING_BITFIELD_BUFFER_SRV_DESCRIPTOR_INDEX] = context.viewManager->Get(views[i])->gpu.meshletBitfieldBuffer->GetResource()->GetSRVInfo(0).slot.index;
					commandList.PushConstants(rhi::ShaderStage::AllGraphics, 0, MiscUintRootSignatureIndex, 0, NumMiscUintRootConstants, &misc);

                    for (auto& workload : context.indirectCommandBufferManager->GetBuffersForRenderPhase(views[i], Engine::Primary::ShadowMapsPass)) {
                        drawObjects(workload.second.buffer->GetAPIResource().GetHandle(), workload.first, workload.second.count, workload.second.buffer->GetResource()->GetUAVCounterOffset());
                    }
                }
            }
            }
            });
    }

private:
    flecs::query<Components::Light, Components::LightViewInfo, Components::DepthMap> lightQuery;
    flecs::query<Components::ObjectDrawInfo, Components::PerPassMeshes> m_meshInstancesQuery;
    bool m_wireframe;
    bool m_meshShaders;
    bool m_indirect;
    bool m_drawBlendShadows;
    bool m_clearDepths;

    float clear[4] = { 1.0, 0.0, 0.0, 0.0 };

    RenderPhase m_renderPhase = Engine::Primary::ShadowMapsPass;

    std::function<uint8_t()> getNumDirectionalLightCascades;
    std::function<uint16_t()> getShadowResolution;
};