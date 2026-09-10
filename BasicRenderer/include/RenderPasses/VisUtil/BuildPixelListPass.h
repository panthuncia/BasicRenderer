#pragma once
#include "RenderPasses/Base/TypedRenderGraphPass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "Render/GraphExtensions/CLodExtensionComponents.h"
#include "Render/GraphExtensions/ClusterLOD/CLodCommon.h"
#include "Materials/TechniqueDescriptor.h"
#include "../shaders/PerPassRootConstants/visUtilRootConstants.h"
#include "RenderPasses/PreparedComputeDispatch.h"
#include "Render/MaterialStateArtifacts.h"

struct BuildPixelListBindings {
    org::ResourceBindingToken visibleClusters, reyesDiceQueue;
    bool hasReyesDiceQueue = false;
    uint32_t patchVisibilityIndexBase = 0;
};

class BuildPixelListPass : public org::TypedRenderGraphPass<BuildPixelListPass,
    br::render::PreparedComputeDispatch, BuildPixelListBindings> {
public:
    BuildPixelListPass() {
        m_pso = PSOManager::GetInstance().MakeComputePipeline(
            PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
            L"shaders/VisUtil.hlsl",
            L"BuildPixelListCS",
            {},
            "BuildPixelListPSO");

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
    BuildPixelListBindings Declare(org::PassBuilder& b) {
        RefreshResourcePointers();
        BuildPixelListBindings bindings{b.BindShaderResource(m_visibleClusterResource)};
        if (m_reyesDiceQueueResource) {
            bindings.reyesDiceQueue = b.BindShaderResource(m_reyesDiceQueueResource);
            bindings.hasReyesDiceQueue = true;
        }
        bindings.patchVisibilityIndexBase = m_patchVisibilityIndexBase;

        b.WithShaderResource(Builtin::PrimaryCamera::VisibilityTexture,
                              //Builtin::PrimaryCamera::VisibleClusterTable,
                              Builtin::PerMeshInstanceBuffer,
                              Builtin::InstanceDrawRecordBuffer,
                              Builtin::PerMeshBuffer,
                              Builtin::PerMaterialDataBuffer,
                              "Builtin::VisUtil::MaterialOffsetBuffer")
         .WithUnorderedAccess("Builtin::VisUtil::MaterialWriteCursorBuffer",
                              "Builtin::VisUtil::PixelListBuffer");
		b.WithConstantBuffer(Builtin::PerFrameBuffer)
         .PreferQueue(org::QueueKind::Compute);
        return bindings;
    }

    void Initialize() {
        RefreshResourcePointers();
    }

    void RefreshResourcePointers() {
		std::vector<std::shared_ptr<GloballyIndexedResource>> visibleClusterResources;
        m_visibleClustersQuery.each([&](flecs::entity e) {
			auto& res = e.get<Components::Resource>();
			auto test = std::static_pointer_cast<GloballyIndexedResource>(res.resource.lock());
            if (test) {
                visibleClusterResources.push_back(std::move(test));
            }
			const auto capacity = e.get<CLodVisibleClusterCapacity>();
			m_patchVisibilityIndexBase = CLodReyesPatchVisibilityIndexBase(capacity.maxVisibleClusters);
			});

		if (visibleClusterResources.size() != 1) {
			throw std::runtime_error("BuildPixelListPass: Expected exactly one visible cluster buffer resource.");
		}

        m_visibleClusterResource = std::move(visibleClusterResources[0]);
        m_reyesDiceQueueResource.reset();

        std::vector<std::shared_ptr<GloballyIndexedResource>> reyesDiceQueueResources;
        m_reyesDiceQueueQuery.each([&](flecs::entity e) {
            if (const auto res = e.try_get<Components::Resource>(); res) {
                if (const auto test = std::static_pointer_cast<GloballyIndexedResource>(res->resource.lock()); test) {
                    reyesDiceQueueResources.push_back(std::move(test));
                }
            }
            });
        if (reyesDiceQueueResources.size() == 1) {
            m_reyesDiceQueueResource = std::move(reyesDiceQueueResources[0]);
        }
    }

    br::render::PreparedComputeDispatch Prepare(const BuildPixelListBindings& bindings,
        const org::PassPrepareContext& preparation) const {
        const auto* update = preparation.preparationData->Get<UpdateContext>();
        const auto* render = preparation.preparationData->Get<RenderContext>();
        if (!update && !render) throw std::logic_error("BuildPixelListPass requires frame context");
        br::render::PreparedComputeDispatch data{};
        data.layout = PSOManager::GetInstance().GetComputeRootSignature().GetHandle();
        auto program = CaptureProgramBinding(preparation, m_pso);
        data.program = program.program;
        data.descriptorIndices = std::move(program.descriptorIndices);
        data.constants[VISBUF_VISIBLE_CLUSTERS_BUFFER_DESCRIPTOR_INDEX] = preparation.ResolveView(
            bindings.visibleClusters, {org::BindlessViewKind::ShaderResource}).index;
        data.constants[VISBUF_REYES_DICE_QUEUE_DESCRIPTOR_INDEX] = bindings.hasReyesDiceQueue
            ? preparation.ResolveView(bindings.reyesDiceQueue, {org::BindlessViewKind::ShaderResource}).index
            : 0xFFFFFFFFu;
        data.constants[VISBUF_REYES_PATCH_INDEX_BASE] = bindings.patchVisibilityIndexBase;
        uint32_t voxelMaterialBin = 0xFFFFFFFFu;
        const auto& published = update ? update->publishedRendererState : render->publishedRendererState;
        const auto materialState = published
            ? published->materials.payload.Get<br::render::PublishedMaterialState>() : nullptr;
        if (materialState) {
            const bool foundVoxelSlot = materialState->TryGetCompileFlagsSlot(
                MaterialCompileFlags::MaterialCompileVoxel, voxelMaterialBin);
            (void)foundVoxelSlot;
        }
        data.constants[VISBUF_VOXEL_MATERIAL_BIN_INDEX] = voxelMaterialBin;
        const auto resolution = update ? update->renderResolution : render->renderResolution;
        data.groupsX = (resolution.x + 7u) / 8u;
        data.groupsY = (resolution.y + 7u) / 8u;
        return data;
    }

    static void Record(const BuildPixelListBindings&, const br::render::PreparedComputeDispatch& data,
        org::PassRecordContext& recording) {
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
    std::shared_ptr<GloballyIndexedResource> m_visibleClusterResource;
    std::shared_ptr<GloballyIndexedResource> m_reyesDiceQueueResource;
	uint32_t m_patchVisibilityIndexBase = 0u;
};
