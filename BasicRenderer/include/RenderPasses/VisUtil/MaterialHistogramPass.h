#pragma once
#include "RenderPasses/Base/TypedRenderGraphPass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "Render/GraphExtensions/CLodExtensionComponents.h"
#include "Render/GraphExtensions/ClusterLOD/CLodCommon.h"
#include "Materials/TechniqueDescriptor.h"
#include "../shaders/PerPassRootConstants/visUtilRootConstants.h"
#include "RenderPasses/PreparedComputeDispatch.h"

class MaterialHistogramPass : public org::TypedRenderGraphPass<MaterialHistogramPass, br::render::PreparedComputeDispatch> {
public:
    MaterialHistogramPass() {
        m_pso = PSOManager::GetInstance().MakeComputePipeline(
            PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
            L"shaders/VisUtil.hlsl",
            L"MaterialHistogramCS",
            {},
            "MaterialHistogramPSO");

        auto& ecsWorld = RendererECSManager::GetInstance().GetWorld();

        // Global LOD extension visibility buffer tag
        auto visBufferTag = ecsWorld.component<CLodExtensionVisibilityBufferTag>();

        // Query for entities with the visibility buffer tag
        m_visibleClustersQuery =
            ecsWorld.query_builder<>()
            .with<CLodExtensionTypeTag>(visBufferTag)
            .with<VisibleClustersBufferTag>()
            .build();

        m_reyesDiceQueueQuery =
            ecsWorld.query_builder<>()
            .with<CLodExtensionTypeTag>(visBufferTag)
            .with<CLodReyesDiceQueueTag>()
            .build();
    }
    void Declare(org::PassBuilder& b) {

        b.WithShaderResource(ECSResourceResolver(m_visibleClustersQuery));
        b.WithShaderResource(ECSResourceResolver(m_reyesDiceQueueQuery));
	    b.WithShaderResource(Builtin::PrimaryCamera::VisibilityTexture,
                              //Builtin::PrimaryCamera::VisibleClusterTable,
                              Builtin::PerMeshInstanceBuffer,
                              Builtin::InstanceDrawRecordBuffer,
                              Builtin::PerMeshBuffer,
                              Builtin::PerMaterialDataBuffer)
         .WithUnorderedAccess("Builtin::VisUtil::MaterialPixelCountBuffer");
		b.WithConstantBuffer(Builtin::PerFrameBuffer)
         .PreferQueue(org::QueueKind::Compute);
    }

    void Initialize() {
        RefreshResourcePointers();
        RefreshDescriptorIndices();
    }

    void RefreshResourcePointers() {
        std::vector<GloballyIndexedResource*> visibleClusterResources;
        m_visibleClustersQuery.each([&](flecs::entity e) {
            auto& res = e.get<Components::Resource>();
            auto test = std::static_pointer_cast<GloballyIndexedResource>(res.resource.lock());
            if (test) {
                visibleClusterResources.push_back(test.get());
            }
            const auto capacity = e.get<CLodVisibleClusterCapacity>();
            m_patchVisibilityIndexBase = CLodReyesPatchVisibilityIndexBase(capacity.maxVisibleClusters);
            });

        if (visibleClusterResources.size() != 1) {
            throw std::runtime_error("BuildPixelListPass: Expected exactly one visible cluster buffer resource.");
        }

        m_visibleClusterResource = visibleClusterResources[0];
        m_reyesDiceQueueResource = nullptr;
        m_reyesDiceQueueBufferSRVIndex = 0xFFFFFFFFu;

        std::vector<GloballyIndexedResource*> reyesDiceQueueResources;
        m_reyesDiceQueueQuery.each([&](flecs::entity e) {
            if (const auto res = e.try_get<Components::Resource>(); res) {
                if (const auto test = std::static_pointer_cast<GloballyIndexedResource>(res->resource.lock()); test) {
                    reyesDiceQueueResources.push_back(test.get());
                }
            }
            });
        if (reyesDiceQueueResources.size() == 1) {
            m_reyesDiceQueueResource = reyesDiceQueueResources[0];
        }
    }

    void RefreshDescriptorIndices() {
        if (m_visibleClusterResource) {
            m_visibleClusterBufferSRVIndex = m_visibleClusterResource->GetSRVInfo(0).slot.index;
        }
        m_reyesDiceQueueBufferSRVIndex = m_reyesDiceQueueResource
            ? m_reyesDiceQueueResource->GetSRVInfo(0).slot.index
            : 0xFFFFFFFFu;
    }

    br::render::PreparedComputeDispatch Prepare(const org::PassPrepareContext& preparation) {
        const auto* update = preparation.preparationData->Get<UpdateContext>();
        const auto* render = preparation.preparationData->Get<RenderContext>();
        if (!update && !render) throw std::logic_error("MaterialHistogramPass requires frame context");
        RefreshDescriptorIndices();
        auto payload = m_pso.GetPayload();
        br::render::PreparedComputeDispatch data{};
        data.resourceHeap = update ? update->textureDescriptorHeap.GetHandle() : render->textureDescriptorHeap.GetHandle();
        data.samplerHeap = update ? update->samplerDescriptorHeap.GetHandle() : render->samplerDescriptorHeap.GetHandle();
        data.layout = PSOManager::GetInstance().GetComputeRootSignature().GetHandle();
        data.pipeline = payload->pso.Get().GetHandle();
        data.pipelineOwner = std::move(payload);
        data.descriptorIndices = CaptureResourceDescriptorIndices(data.pipelineOwner->pipelineResources);
        data.constants[VISBUF_VISIBLE_CLUSTERS_BUFFER_DESCRIPTOR_INDEX] = m_visibleClusterBufferSRVIndex;
        data.constants[VISBUF_REYES_DICE_QUEUE_DESCRIPTOR_INDEX] = m_reyesDiceQueueBufferSRVIndex;
        data.constants[VISBUF_REYES_PATCH_INDEX_BASE] = m_patchVisibilityIndexBase;
        uint32_t voxelMaterialBin = 0xFFFFFFFFu;
        auto* materialManager = update ? update->materialManager : render->materialManager;
        materialManager->TryGetCompileFlagsSlot(MaterialCompileFlags::MaterialCompileVoxel, voxelMaterialBin);
        data.constants[VISBUF_VOXEL_MATERIAL_BIN_INDEX] = voxelMaterialBin;
        const auto resolution = update ? update->renderResolution : render->renderResolution;
        data.groupsX = (resolution.x + 7u) / 8u;
        data.groupsY = (resolution.y + 7u) / 8u;
        return data;
    }

    static void Record(const br::render::PreparedComputeDispatch& data, org::PassRecordContext& recording) {
        br::render::RecordPreparedComputeDispatch(data, recording);
    }

    void ShutdownPass() {
        m_visibleClustersQuery = {};
        m_reyesDiceQueueQuery = {};
    }

private:
    PipelineState m_pso;
	flecs::query<> m_visibleClustersQuery;
    flecs::query<> m_reyesDiceQueueQuery;
    GloballyIndexedResource* m_visibleClusterResource = nullptr;
    GloballyIndexedResource* m_reyesDiceQueueResource = nullptr;
    uint32_t m_visibleClusterBufferSRVIndex = 0;
    uint32_t m_reyesDiceQueueBufferSRVIndex = 0xFFFFFFFFu;
    uint32_t m_patchVisibilityIndexBase = 0u;
};
