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
    ZPrepass(
        bool wireframe,
        bool meshShaders,
        bool indirect,
        bool clearGbuffer)
        :
        m_wireframe(wireframe),
        m_meshShaders(meshShaders),
        m_indirect(indirect),
        m_clearGbuffer(clearGbuffer){
        auto& settingsManager = SettingsManager::GetInstance();
        getImageBasedLightingEnabled = settingsManager.getSettingGetter<bool>("enableImageBasedLighting");
        getPunctualLightingEnabled = settingsManager.getSettingGetter<bool>("enablePunctualLighting");
        getShadowsEnabled = settingsManager.getSettingGetter<bool>("enableShadows");
		m_deferred = settingsManager.getSettingGetter<bool>("enableDeferredRendering")();
    }

    ~ZPrepass() {
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

    void Setup(const ResourceRegistryView& resourceRegistryView) override {
        auto& ecsWorld = ECSManager::GetInstance().GetWorld();
        m_opaqueMeshInstancesQuery = ecsWorld.query_builder<Components::ObjectDrawInfo, Components::OpaqueMeshInstances>().cached().cache_kind(flecs::QueryCacheAll).build();
        m_alphaTestMeshInstancesQuery = ecsWorld.query_builder<Components::ObjectDrawInfo, Components::AlphaTestMeshInstances>().cached().cache_kind(flecs::QueryCacheAll).build();
    
		m_pLinearDepthBuffer = resourceRegistryView.Request<PixelBuffer>(Builtin::PrimaryCamera::LinearDepthMap);
		m_pPrimaryDepthBuffer = resourceRegistryView.Request<PixelBuffer>(Builtin::PrimaryCamera::DepthTexture);
        m_pNormals = resourceRegistryView.Request<PixelBuffer>(Builtin::GBuffer::Normals);
		m_pMotionVectors = resourceRegistryView.Request<PixelBuffer>(Builtin::GBuffer::MotionVectors);

        if (m_deferred) {
            m_pAlbedo = resourceRegistryView.Request<PixelBuffer>(Builtin::GBuffer::Albedo);
            m_pMetallicRoughness = resourceRegistryView.Request<PixelBuffer>(Builtin::GBuffer::MetallicRoughness);
            m_pEmissive = resourceRegistryView.Request<PixelBuffer>(Builtin::GBuffer::Emissive);
        }

        if (m_indirect) {
            m_pPrimaryCameraOpaqueIndirectCommandBuffer = resourceRegistryView.Request<DynamicGloballyIndexedResource>(Builtin::PrimaryCamera::IndirectCommandBuffers::Opaque);
            m_pPrimaryCameraAlphaTestIndirectCommandBuffer = resourceRegistryView.Request<DynamicGloballyIndexedResource>(Builtin::PrimaryCamera::IndirectCommandBuffers::AlphaTest);
            if (m_meshShaders) {
                m_meshletCullingBitfieldBuffer = resourceRegistryView.Request<DynamicGloballyIndexedResource>(Builtin::PrimaryCamera::MeshletBitfield);
            }
        }

        if (m_meshShaders) {
            m_meshletOffsetBufferSRVIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::MeshResources::MeshletOffsets)->GetSRVInfo(0).index;
            m_meshletVertexIndexBufferSRVIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::MeshResources::MeshletVertexIndices)->GetSRVInfo(0).index;
            m_meshletTriangleBufferSRVIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::MeshResources::MeshletTriangles)->GetSRVInfo(0).index;
        }

		m_normalMatrixBufferSRVIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::NormalMatrixBuffer)->GetSRVInfo(0).index;
		m_postSkinningVertexBufferSRVIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::PostSkinningVertices)->GetSRVInfo(0).index;
		m_perObjectBufferSRVIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::PerObjectBuffer)->GetSRVInfo(0).index;
		m_cameraBufferSRVIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::CameraBuffer)->GetSRVInfo(0).index;
		m_perMeshInstanceBufferSRVIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::PerMeshInstanceBuffer)->GetSRVInfo(0).index;
        m_perMeshBufferSRVIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::PerMeshBuffer)->GetSRVInfo(0).index;
    }

    PassReturn Execute(RenderContext& context) override {
        auto& psoManager = PSOManager::GetInstance();

        auto& commandList = context.commandList;

        // Clear the render target
        if (m_clearGbuffer) {
            auto& rtvHandle = m_pNormals->GetRTVInfo(0).cpuHandle;
            const float clearColor[] = { 0.0f, 0.0f, 0.0f, 1.0f };
            commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
			auto& rtvHandle1 = m_pLinearDepthBuffer->GetRTVInfo(0).cpuHandle;
			auto& clearColor1 = m_pLinearDepthBuffer->GetClearColor();
			D3D12_RECT clearRect = { 0, 0, static_cast<LONG>(context.renderResolution.x), static_cast<LONG>(context.renderResolution.y) };
			commandList->ClearRenderTargetView(rtvHandle1, clearColor1.data(), 1, &clearRect);

			if (m_pMotionVectors) {
				auto& rtvHandleMotion = m_pMotionVectors->GetRTVInfo(0).cpuHandle;
                auto& motionVectorClear = m_pMotionVectors->GetClearColor();
				commandList->ClearRenderTargetView(rtvHandleMotion, motionVectorClear.data(), 0, nullptr);
			}

            if (context.globalPSOFlags & PSOFlags::PSO_DEFERRED) {
                auto& rtvHandle2 = m_pAlbedo->GetRTVInfo(0).cpuHandle;
                commandList->ClearRenderTargetView(rtvHandle2, clearColor, 0, nullptr);
                auto& rtvHandle3 = m_pMetallicRoughness->GetRTVInfo(0).cpuHandle;
                commandList->ClearRenderTargetView(rtvHandle3, clearColor, 0, nullptr);
            }

            auto& dsvHandle = m_pPrimaryDepthBuffer->GetDSVInfo(0).cpuHandle;
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

        CD3DX12_VIEWPORT viewport(0.0f, 0.0f, context.renderResolution.x, context.renderResolution.y);
        CD3DX12_RECT scissorRect(0, 0, context.renderResolution.x, context.renderResolution.y);
        commandList->RSSetViewports(1, &viewport);
        commandList->RSSetScissorRects(1, &scissorRect);

        // Render targets
		auto& dsvHandle = m_pPrimaryDepthBuffer->GetDSVInfo(0).cpuHandle;

        if (context.globalPSOFlags & PSOFlags::PSO_DEFERRED) {
            D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[6] = { m_pNormals->GetRTVInfo(0).cpuHandle, m_pMotionVectors->GetRTVInfo(0).cpuHandle, m_pLinearDepthBuffer->GetRTVInfo(0).cpuHandle, m_pAlbedo->GetRTVInfo(0).cpuHandle, m_pMetallicRoughness->GetRTVInfo(0).cpuHandle, m_pEmissive->GetRTVInfo(0).cpuHandle};
            commandList->OMSetRenderTargets(6, rtvHandles, FALSE, &dsvHandle);
        }
        else {
			D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[3] = { m_pNormals->GetRTVInfo(0).cpuHandle, m_pMotionVectors->GetRTVInfo(0).cpuHandle, m_pLinearDepthBuffer->GetRTVInfo(0).cpuHandle };
			commandList->OMSetRenderTargets(3, rtvHandles, FALSE, &dsvHandle);
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
        staticBufferIndices[NormalMatrixBufferDescriptorIndex] = m_normalMatrixBufferSRVIndex;
        staticBufferIndices[PostSkinningVertexBufferDescriptorIndex] = m_postSkinningVertexBufferSRVIndex;
        staticBufferIndices[MeshletBufferDescriptorIndex] = m_meshletOffsetBufferSRVIndex;
        staticBufferIndices[MeshletVerticesBufferDescriptorIndex] = m_meshletVertexIndexBufferSRVIndex;
        staticBufferIndices[MeshletTrianglesBufferDescriptorIndex] = m_meshletTriangleBufferSRVIndex;
        staticBufferIndices[PerObjectBufferDescriptorIndex] = m_perObjectBufferSRVIndex;
        staticBufferIndices[CameraBufferDescriptorIndex] = m_cameraBufferSRVIndex;
        staticBufferIndices[PerMeshInstanceBufferDescriptorIndex] = m_perMeshInstanceBufferSRVIndex;
        staticBufferIndices[PerMeshBufferDescriptorIndex] = m_perMeshBufferSRVIndex;
		staticBufferIndices[NormalsTextureDescriptorIndex] = m_pNormals->GetRTVInfo(0).index;
        commandList->SetGraphicsRoot32BitConstants(StaticBufferRootSignatureIndex, NumStaticBufferRootConstants, &staticBufferIndices, 0);

        if (m_indirect && m_meshShaders) {
            unsigned int variableRootConstants[NumVariableBufferRootConstants] = {};
            variableRootConstants[MeshletCullingBitfieldBufferDescriptorIndex] = m_meshletCullingBitfieldBuffer->GetResource()->GetSRVInfo(0).index;

            commandList->SetGraphicsRoot32BitConstants(VariableBufferRootSignatureIndex, NumVariableBufferRootConstants, &variableRootConstants, 0);
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

            auto opaqueIndirectBuffer = m_pPrimaryCameraOpaqueIndirectCommandBuffer;
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

            auto alphaTestIndirectBuffer = m_pPrimaryCameraAlphaTestIndirectCommandBuffer;
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

	std::shared_ptr<DynamicGloballyIndexedResource> m_pPrimaryCameraOpaqueIndirectCommandBuffer;
	std::shared_ptr<DynamicGloballyIndexedResource> m_pPrimaryCameraAlphaTestIndirectCommandBuffer;
	std::shared_ptr<DynamicGloballyIndexedResource> m_meshletCullingBitfieldBuffer;

	int m_normalMatrixBufferSRVIndex = -1;
	int m_postSkinningVertexBufferSRVIndex = -1;
	int m_meshletOffsetBufferSRVIndex = -1;
	int m_meshletVertexIndexBufferSRVIndex = -1;
	int m_meshletTriangleBufferSRVIndex = -1;
	int m_perObjectBufferSRVIndex = -1;
	int m_cameraBufferSRVIndex = -1;
	int m_perMeshInstanceBufferSRVIndex = -1;
	int m_perMeshBufferSRVIndex = -1;

    std::function<bool()> getImageBasedLightingEnabled;
    std::function<bool()> getPunctualLightingEnabled;
    std::function<bool()> getShadowsEnabled;
};
